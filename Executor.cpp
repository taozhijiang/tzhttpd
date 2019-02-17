/*-
 * Copyright (c) 2018-2019 TAO Zhijiang<taozhijiang@gmail.com>
 *
 * Licensed under the BSD-3-Clause license, see LICENSE for full information.
 *
 */

#include "Dispatcher.h"
#include "HttpReqInstance.h"
#include "HttpExecutor.h"

#include "Executor.h"
#include "Status.h"

namespace tzhttpd {


bool Executor::init() {

    std::lock_guard<std::mutex> lock(conf_lock_);

    if (auto http_executor = dynamic_cast<HttpExecutor *>(service_impl_.get())) {
        conf_ = http_executor->get_executor_conf();
    } else {
        tzhttpd_log_err("cast instance failed.");
        return false;
    }

    if (conf_.exec_thread_number_hard_ < conf_.exec_thread_number_) {
        conf_.exec_thread_number_hard_ = conf_.exec_thread_number_;
    }

    if (conf_.exec_thread_number_ <= 0 || conf_.exec_thread_number_ > 100 ||
        conf_.exec_thread_number_hard_ > 100 ||
        conf_.exec_thread_number_hard_ < conf_.exec_thread_number_ )
    {
        tzhttpd_log_err("invalid exec_thread_pool_size setting: %d, %d",
                        conf_.exec_thread_number_, conf_.exec_thread_number_hard_);
        return false;
    }

    if (conf_.exec_thread_step_queue_size_ < 0) {
        tzhttpd_log_err("invalid exec_thread_step_queue_size setting: %d",
                        conf_.exec_thread_step_queue_size_);
        return false;
    }

    if (!executor_threads_.init_threads(
        std::bind(&Executor::executor_service_run, this, std::placeholders::_1), conf_.exec_thread_number_)) {
        tzhttpd_log_err("executor_service_run init task for %s failed!", instance_name().c_str());
        return false;
    }

    if (conf_.exec_thread_number_hard_ > conf_.exec_thread_number_ &&
        conf_.exec_thread_step_queue_size_ > 0)
    {
        threads_adjust_timer_.reset(new steady_timer(Dispatcher::instance().get_io_service()));
        if (!threads_adjust_timer_) {
            tzhttpd_log_err("create thread adjust timer failed.");
            return false;
        }

        tzhttpd_log_debug("we will support thread adjust for %s, with param hard %d, queue_step %d",
                          instance_name().c_str(),
                          conf_.exec_thread_number_hard_, conf_.exec_thread_step_queue_size_);
        threads_adjust_timer_->expires_from_now(boost::chrono::seconds(1));
        threads_adjust_timer_->async_wait(
            std::bind(&Executor::executor_threads_adjust, shared_from_this(), std::placeholders::_1));
    }

    Status::instance().register_status_callback(
                "executor_" + instance_name(),
                std::bind(&Executor::module_status, shared_from_this(),
                          std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));


    return true;
}


void Executor::executor_service_run(ThreadObjPtr ptr) {

    tzhttpd_log_alert("executor_service thread %#lx about to loop ...", (long)pthread_self());

    while (true) {

        std::shared_ptr<HttpReqInstance> http_req_instance {};

        if (unlikely(ptr->status_ == ThreadStatus::kTerminating)) {
            tzhttpd_log_err("thread %#lx is about to terminating...", (long)pthread_self());
            break;
        }

        // 线程启动
        if (unlikely(ptr->status_ == ThreadStatus::kSuspend)) {
            ::usleep(1*1000*1000);
            continue;
        }

        if (!http_req_queue_.POP(http_req_instance, 1000 /*1s*/) || !http_req_instance) {
            continue;
        }

        // execute RPC handler
        service_impl_->handle_http_request(http_req_instance);
    }

    ptr->status_ = ThreadStatus::kDead;
    tzhttpd_log_info("io_service thread %#lx is about to terminate ... ", (long)pthread_self());

    return;

}


void Executor::executor_threads_adjust(const boost::system::error_code& ec) {

    ExecutorConf conf {};

    {
        std::lock_guard<std::mutex> lock(conf_lock_);
        conf = conf_;
    }

    SAFE_ASSERT(conf.exec_thread_step_queue_size_ > 0);

    // 进行检查，看是否需要伸缩线程池
    int expect_thread = conf.exec_thread_number_;

    int queueSize = http_req_queue_.SIZE();
    if (queueSize > conf.exec_thread_step_queue_size_) {
        expect_thread = queueSize / conf.exec_thread_step_queue_size_;
    }
    if (expect_thread > conf.exec_thread_number_hard_) {
        expect_thread = conf.exec_thread_number_hard_;
    }

    if (expect_thread != conf.exec_thread_number_) {
        tzhttpd_log_notice("start thread number: %d, expect resize to %d",
                           conf.exec_thread_number_, expect_thread);
    }
    executor_threads_.resize_threads(expect_thread);


    threads_adjust_timer_->expires_from_now(boost::chrono::seconds(1));
    threads_adjust_timer_->async_wait(
            std::bind(&Executor::executor_threads_adjust, shared_from_this(), std::placeholders::_1));

}

int Executor::module_status(std::string& strModule, std::string& strKey, std::string& strValue) {

    strModule = "tzhttpd";
    strKey = "executor_" + instance_name();

    std::stringstream ss;

    ss << "\t" << "instance_name: " << instance_name() << std::endl;
    ss << "\t" << "exec_thread_number: " << conf_.exec_thread_number_ << std::endl;
    ss << "\t" << "exec_thread_number_hard(maxium): " << conf_.exec_thread_number_hard_ << std::endl;
    ss << "\t" << "exec_thread_step_queue_size: " << conf_.exec_thread_step_queue_size_ << std::endl;

    ss << "\t" << std::endl;

    ss << "\t" << "current_thread_number: " << executor_threads_.get_pool_size() << std::endl;
    ss << "\t" << "current_queue_size: " << http_req_queue_.SIZE() << std::endl;

    std::string nullModule;
    std::string subKey;
    std::string subValue;
    service_impl_->module_status(nullModule, subKey, subValue);

    // collect
    strValue = ss.str() + subValue;

    return 0;
}


int Executor::update_runtime_conf(const libconfig::Config& conf) {

    int ret = service_impl_->update_runtime_conf(conf);

    // 如果返回0，表示配置文件已经正确解析了，同时ExecutorConf也重新初始化了
    if (ret == 0) {
        if (auto http_executor = dynamic_cast<HttpExecutor *>(service_impl_.get())) {

            tzhttpd_log_notice("update ExecutorConf for host %s", instance_name().c_str());

            std::lock_guard<std::mutex> lock(conf_lock_);
            conf_ = http_executor->get_executor_conf();
        }
    }
    return ret;
}

} // end namespace tzhttpd
