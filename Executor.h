#ifndef __TZHTTPD_EXECUTOR_H__
#define __TZHTTPD_EXECUTOR_H__

#include <xtra_rhel6.h>

#include "Log.h"
#include "EQueue.h"
#include "ServiceIf.h"

#include "ThreadPool.h"

namespace tzhttpd {


class Executor: public ServiceIf {

public:

    explicit Executor(std::shared_ptr<ServiceIf> service_impl):
        service_impl_(service_impl),
        http_req_queue_() {
    }

    void handle_http_request(std::shared_ptr<HttpReqInstance> http_req_instance) override {
        http_req_queue_.PUSH(http_req_instance);
    }

    std::string instance_name() override {
        return service_impl_->instance_name();
    }

    int register_get_handler(const std::string& uri_regex, const HttpGetHandler& handler) override {
        return service_impl_->register_get_handler(uri_regex, handler);
    }

    int register_post_handler(const std::string& uri_regex, const HttpPostHandler& handler) override {
        return service_impl_->register_post_handler(uri_regex, handler);
    }


    bool init() {

        if (!executor_threads_.init_threads(
            std::bind(&Executor::executor_service_run, this, std::placeholders::_1), 5)) {
            tzhttpd_log_err("executor_service_run init task for %s failed!", instance_name().c_str());
            return false;
        }

        return true;
    }

private:
    // point to HttpExecutor, forward some request
    std::shared_ptr<ServiceIf> service_impl_;
    EQueue<std::shared_ptr<HttpReqInstance>> http_req_queue_;


private:
    ThreadPool executor_threads_;
    void executor_service_run(ThreadObjPtr ptr);  // main task loop

public:

    int executor_start() {

        tzhttpd_log_notice("about to start executor for %s ... ", instance_name().c_str());
        executor_threads_.start_threads();
        return 0;
    }

    int executor_stop_graceful() {

        tzhttpd_log_notice("about to stop executor for %s ... ", instance_name().c_str());
        executor_threads_.graceful_stop_threads();

        return 0;
    }

    int executor_join() {

        tzhttpd_log_notice("about to join executor for %s ... ", instance_name().c_str());
        executor_threads_.join_threads();
        return 0;
    }

};

} // end tzhttpd


#endif // __TZHTTPD_EXECUTOR_H__
