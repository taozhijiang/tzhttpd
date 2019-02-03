#ifndef __TZHTTPD_DISPATCHER_H__
#define __TZHTTPD_DISPATCHER_H__

#include <cinttypes>
#include <memory>

#include <map>
#include <mutex>


#include <boost/noncopyable.hpp>

#include "Log.h"

#include "ServiceIf.h"
#include "Executor.h"

#include "HttpExecutor.h"
#include "HttpReqInstance.h"


namespace tzhttpd {

class Dispatcher: public boost::noncopyable,
                  public ServiceIf {
public:
    static Dispatcher& instance();

    bool init() {

        initialized_ = true;

        // 注册默认default vhost
        SAFE_ASSERT(!default_service_);

        // 创建 default virtual host
        // http impl
        auto default_http_impl = std::make_shared<HttpExecutor>("[default]");
        if (!default_http_impl|| !default_http_impl->init()) {
            tzhttpd_log_err("create default http_impl failed.");
            return false;
        }

        // http executor
        default_service_.reset(new Executor(default_http_impl));
        Executor* executor = dynamic_cast<Executor *>(default_service_.get());

        SAFE_ASSERT(executor);
        if (!executor || !executor->init()) {
            tzhttpd_log_err("init default virtual host executor failed.");
            return false;
        }

        executor->executor_start();
        tzhttpd_log_debug("start default virtual host executor: %s success",  executor->instance_name().c_str());
        //


        for (auto iter = services_.begin(); iter != services_.end(); ++iter) {
            Executor* executor = dynamic_cast<Executor *>(iter->second.get());
            SAFE_ASSERT(executor);

            executor->executor_start();
            tzhttpd_log_debug("start virtual host executor for %s success",  executor->instance_name().c_str());
        }

        return true;
    }


    void handle_http_request(std::shared_ptr<HttpReqInstance> http_req_instance) override {

        std::shared_ptr<ServiceIf> service;
        auto it = services_.find(http_req_instance->hostname_);
        if (it != services_.end()) {
            service = it->second;
        }

        if (!service) {
            tzhttpd_log_debug("find http service_impl (virtualhost) for %s failed, using default.",
                              http_req_instance->hostname_.c_str());
            service = default_service_;
        }

        service->handle_http_request(http_req_instance);
    }

    std::string instance_name() override {
        return "HttpDispatcher";
    }

    // 注册虚拟主机
    int register_virtual_host(const std::string& hostname);

    // 外部注册http handler的接口
    int register_http_get_handler(const std::string& hostname, const std::string& uri_regex,
                                  const HttpGetHandler& handler);
    int register_http_post_handler(const std::string& hostname, const std::string& uri_regex,
                                   const HttpPostHandler& handler);

private:

    int register_get_handler(const std::string& uri_regex, const HttpGetHandler& handler) override {
        SAFE_ASSERT(false);
        tzhttpd_log_err("YOU SHOULD NOT CALL THIS FUNC...");
        return -1;
    }

    int register_post_handler(const std::string& uri_regex, const HttpPostHandler& handler) override {
        SAFE_ASSERT(false);
        tzhttpd_log_err("YOU SHOULD NOT CALL THIS FUNC...");
        return -1;
    }

    Dispatcher():
        initialized_(false),
        services_({}) {
    }

    bool initialized_;

    // 系统在启动的时候进行注册初始化，然后再提供服务，所以
    // 这边就不使用锁结构进行保护了，防止影响性能
    std::map<std::string, std::shared_ptr<ServiceIf>> services_;

    // 默认的http虚拟主机
    std::shared_ptr<ServiceIf> default_service_;

};

} // tzhttpd


#endif // __TZHTTPD_SERVICE_DISPATCHER_H__
