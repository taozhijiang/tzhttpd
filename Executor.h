#ifndef __TZHTTPD_EXECUTOR_H__
#define __TZHTTPD_EXECUTOR_H__

#include <xtra_asio.h>
#include <xtra_rhel6.h>

#include "Log.h"
#include "EQueue.h"
#include "ServiceIf.h"

#include "ConfHelper.h"
#include "ThreadPool.h"

namespace tzhttpd {

// 简短的结构体，用来从HttpExecutor传递配置信息
// 因为主机相关的信息是在HttpExecutor中解析的

struct ExecutorConf {
    int exec_thread_number_;
    int exec_thread_number_hard_;  // 允许最大的线程数目
    int exec_thread_step_queue_size_;
};

class Executor: public ServiceIf,
                public std::enable_shared_from_this<Executor> {

public:

    explicit Executor(std::shared_ptr<ServiceIf> service_impl):
        service_impl_(service_impl),
        http_req_queue_(),
        conf_lock_(),
        conf_({}),
        threads_adjust_timer_() {
    }

    void handle_http_request(std::shared_ptr<HttpReqInstance> http_req_instance) override {
        http_req_queue_.PUSH(http_req_instance);
    }

    std::string instance_name() override {
        return service_impl_->instance_name();
    }

    int add_get_handler(const std::string& uri_regex, const HttpGetHandler& handler) override {
        return service_impl_->add_get_handler(uri_regex, handler);
    }

    int add_post_handler(const std::string& uri_regex, const HttpPostHandler& handler) override {
        return service_impl_->add_post_handler(uri_regex, handler);
    }

    bool exist_get_handler(const std::string& uri_regex) override {
        return service_impl_->exist_get_handler(uri_regex);
    }

    bool exist_post_handler(const std::string& uri_regex) override {
        return service_impl_->exist_post_handler(uri_regex);
    }

    bool init();
    int update_runtime_conf(const libconfig::Config& conf);

    int module_status(std::string& strKey, std::string& strValue);

private:
    // point to HttpExecutor, forward some request
    std::shared_ptr<ServiceIf> service_impl_;
    EQueue<std::shared_ptr<HttpReqInstance>> http_req_queue_;


private:
    // 这个锁保护conf_使用的，因为使用频率不是很高，所以所有访问
    // conf_的都使用这个锁也不会造成问题
    std::mutex   conf_lock_;
    ExecutorConf conf_;

    ThreadPool executor_threads_;
    void executor_service_run(ThreadObjPtr ptr);  // main task loop

public:

    int executor_start() {

        tzhttpd_log_notice("about to start executor for host %s ... ", instance_name().c_str());
        executor_threads_.start_threads();
        return 0;
    }

    int executor_stop_graceful() {

        tzhttpd_log_notice("about to stop executor for host %s ... ", instance_name().c_str());
        executor_threads_.graceful_stop_threads();

        return 0;
    }

    int executor_join() {

        tzhttpd_log_notice("about to join executor for host %s ... ", instance_name().c_str());
        executor_threads_.join_threads();
        return 0;
    }

private:
    // 根据http_req_queue_自动伸缩线程负载
    std::unique_ptr<steady_timer> threads_adjust_timer_;
    void executor_threads_adjust();

};

} // end tzhttpd


#endif // __TZHTTPD_EXECUTOR_H__
