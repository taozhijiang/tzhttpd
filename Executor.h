/*-
 * Copyright (c) 2018-2019 TAO Zhijiang<taozhijiang@gmail.com>
 *
 * Licensed under the BSD-3-Clause license, see LICENSE for full information.
 *
 */

#ifndef __TZHTTPD_EXECUTOR_H__
#define __TZHTTPD_EXECUTOR_H__

#include <xtra_rhel.h>

#include <other/Log.h>
#include <container/EQueue.h>
#include <concurrency/ThreadPool.h>

#include "ServiceIf.h"

#include "Global.h"

namespace tzhttpd {

// 简短的结构体，用来从HttpExecutor传递配置信息
// 因为主机相关的信息是在HttpExecutor中解析的

struct ExecutorConf {
    int exec_thread_number_;
    int exec_thread_number_hard_;  // 允许最大的线程数目
    int exec_thread_step_queue_size_;
};

class Executor : public ServiceIf,
    public std::enable_shared_from_this<Executor> {

public:

    explicit Executor(std::shared_ptr<ServiceIf> service_impl) :
        service_impl_(service_impl),
        http_req_queue_(),
        conf_lock_(),
        conf_({ }) {
    }

    void handle_http_request(std::shared_ptr<HttpReqInstance> http_req_instance)override {
        http_req_queue_.PUSH(http_req_instance);
    }

    std::string instance_name()override {
        return service_impl_->instance_name();
    }

    int add_get_handler(const std::string& uri_regex, const HttpGetHandler& handler, bool built_in)override {
        return service_impl_->add_get_handler(uri_regex, handler, built_in);
    }

    int add_post_handler(const std::string& uri_regex, const HttpPostHandler& handler, bool built_in)override {
        return service_impl_->add_post_handler(uri_regex, handler, built_in);
    }

    bool exist_handler(const std::string& uri_regex, enum HTTP_METHOD method)override {
        return service_impl_->exist_handler(uri_regex, method);
    }

    int drop_handler(const std::string& uri_regex, enum HTTP_METHOD method)override {
        return service_impl_->drop_handler(uri_regex, method);
    }



    bool init();
    int module_runtime(const libconfig::Config& conf)override;
    int module_status(std::string& module, std::string& key, std::string& value)override;

private:
    // point to HttpExecutor, forward some request
    std::shared_ptr<ServiceIf> service_impl_;
    roo::EQueue<std::shared_ptr<HttpReqInstance>> http_req_queue_;


private:
    // 这个锁保护conf_使用的，因为使用频率不是很高，所以所有访问
    // conf_的都使用这个锁也不会造成问题
    std::mutex   conf_lock_;
    ExecutorConf conf_;

    roo::ThreadPool executor_threads_;
    void executor_service_run(roo::ThreadObjPtr ptr);  // main task loop

public:

    int executor_start() {

        roo::log_warning("about to start executor for host %s ... ", instance_name().c_str());
        executor_threads_.start_threads();
        return 0;
    }

    int executor_stop_graceful() {

        roo::log_warning("about to stop executor for host %s ... ", instance_name().c_str());
        executor_threads_.graceful_stop_threads();

        return 0;
    }

    int executor_join() {

        roo::log_warning("about to join executor for host %s ... ", instance_name().c_str());
        executor_threads_.join_threads();
        return 0;
    }

private:
    void executor_threads_adjust(const boost::system::error_code& ec);

};

} // end namespace tzhttpd


#endif // __TZHTTPD_EXECUTOR_H__
