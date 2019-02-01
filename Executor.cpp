#include <xtra_rhel6.h>

#include "HttpReqInstance.h"
#include "Executor.h"

namespace tzhttpd {


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
