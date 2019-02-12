#include "Dispatcher.h"
#include "HttpReqInstance.h"
#include "HttpExecutor.h"

#include "Executor.h"

namespace tzhttpd {


bool Executor::init() {

    if (auto http_executor = dynamic_cast<HttpExecutor *>(service_impl_.get())) {
        conf_ = http_executor->get_executor_conf();
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
        threads_adjust_timer_.reset(new steady_timer (Dispatcher::instance().get_io_service()));
        if (!threads_adjust_timer_) {
            tzhttpd_log_err("create thread adjust timer failed.");
            return false;
        }

        tzhttpd_log_debug("we will support thread adjust for %s, with param %d:%d",
                          instance_name().c_str(), conf_.exec_thread_number_hard_,  conf_.exec_thread_step_queue_size_);
        threads_adjust_timer_->expires_from_now(boost::chrono::seconds(1));
        threads_adjust_timer_->async_wait(
                    std::bind(&Executor::executor_threads_adjust, this));
    }

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


void Executor::executor_threads_adjust() {

    SAFE_ASSERT(conf_.exec_thread_step_queue_size_ > 0);

    // 进行检查，看是否需要伸缩线程池
    int expect_thread = conf_.exec_thread_number_;

    int queueSize = http_req_queue_.SIZE();
    tzhttpd_log_notice("current queue size for virtual host %s: %d", instance_name().c_str(), queueSize);
    if (queueSize > conf_.exec_thread_step_queue_size_) {
        expect_thread = queueSize / conf_.exec_thread_step_queue_size_;
    }
    if (expect_thread > conf_.exec_thread_number_hard_) {
        expect_thread = conf_.exec_thread_number_hard_;
    }

    executor_threads_.resize_threads(expect_thread);


    threads_adjust_timer_->expires_from_now(boost::chrono::seconds(1));
    threads_adjust_timer_->async_wait(
            std::bind(&Executor::executor_threads_adjust, this));

}


} // end tzhttpd
