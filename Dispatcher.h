/*-
 * Copyright (c) 2018-2019 TAO Zhijiang<taozhijiang@gmail.com>
 *
 * Licensed under the BSD-3-Clause license, see LICENSE for full information.
 *
 */

#ifndef __TZHTTPD_DISPATCHER_H__
#define __TZHTTPD_DISPATCHER_H__

#include <cinttypes>
#include <memory>

#include <map>
#include <mutex>


#include <boost/noncopyable.hpp>

#include "Log.h"

#include "Executor.h"

#include "HttpReqInstance.h"


namespace tzhttpd {

class Dispatcher: public boost::noncopyable {

public:
    static Dispatcher& instance();

    bool init();

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

    int update_runtime_conf(const libconfig::Config& conf);

    // 注册虚拟主机
    int add_virtual_host(const std::string& hostname);

    // 外部注册http handler的接口
    int add_http_get_handler(const std::string& hostname, const std::string& uri_regex,
                             const HttpGetHandler& handler, bool built_in);
    int add_http_post_handler(const std::string& hostname, const std::string& uri_regex,
                              const HttpPostHandler& handler, bool built_in);

    int drop_http_handler(const std::string& hostname, const std::string& uri_regex, enum HTTP_METHOD method);

private:

    Dispatcher():
        initialized_(false),
        services_({}){
    }

    ~Dispatcher() {
    }

    bool initialized_;

    // 系统在启动的时候进行注册初始化，然后再提供服务，所以
    // 这边就不使用锁结构进行保护了，防止影响性能
    std::map<std::string, std::shared_ptr<Executor>> services_;

    // 默认的http虚拟主机
    std::shared_ptr<Executor> default_service_;
};

} // end namespace tzhttpd


#endif // __TZHTTPD_SERVICE_DISPATCHER_H__
