#include <xtra_rhel6.h>

#include "HttpReqInstance.h"
#include "HttpExecutor.h"
#include "Executor.h"

namespace tzhttpd {


bool Executor::init() {

    if (auto http_executor = dynamic_cast<HttpExecutor *>(service_impl_.get())) {
        conf_ = http_executor->get_executor_conf();
    }

    if (conf_.exec_thread_number_ <= 0 || conf_.exec_thread_number_ > 100) {
        tzhttpd_log_err("invalid exec_thread_pool_size setting: %d", conf_.exec_thread_number_);
        return false;
    }
    if (!executor_threads_.init_threads(
        std::bind(&Executor::executor_service_run, this, std::placeholders::_1), conf_.exec_thread_number_)) {
        tzhttpd_log_err("executor_service_run init task for %s failed!", instance_name().c_str());
        return false;
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

//        log_notice("Queue Size: %u", rpc_queue_.SIZE());
    }

    ptr->status_ = ThreadStatus::kDead;
    tzhttpd_log_info("io_service thread %#lx is about to terminate ... ", (long)pthread_self());

    return;

}


} // end tzhttpd
