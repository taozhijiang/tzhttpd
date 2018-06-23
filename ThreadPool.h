/*-
 * Copyright (c) 2018 TAO Zhijiang<taozhijiang@gmail.com>
 *
 * Licensed under the BSD-3-Clause license, see LICENSE for full information.
 *
 */
 
#ifndef __TZHTTPD_THREAD_POOL_H__
#define __TZHTTPD_THREAD_POOL_H__

#include <boost/thread.hpp>
#include <memory>

namespace tzhttpd {

enum ThreadStatus {
    kThreadInit = 1,
    kThreadRunning = 2,
    kThreadSuspend = 3,
    kThreadTerminating = 4,
    kThreadDead = 5,
};

struct ThreadObj {
    ThreadObj(enum ThreadStatus status):
        status_(status) {
    }
    enum ThreadStatus status_;
};

const static uint8_t kMaxiumThreadPoolSize = 200;

// 因为需要适用timed_join，但是std::thread没有，所以这里仍然使用boost::thread

typedef std::shared_ptr<boost::thread>       ThreadPtr;
typedef std::shared_ptr<ThreadObj>           ThreadObjPtr;
typedef std::function<void (ThreadObjPtr)>   ThreadRunnable;

class ThreadPool: private boost::noncopyable {
        // 先于线程工作之前的所有预备工作
    class Impl;
    std::unique_ptr<Impl> impl_ptr_;

public:

    ThreadPool(uint8_t thread_num = 1);
    ~ThreadPool();


    bool init_threads(ThreadRunnable func);
    bool init_threads(ThreadRunnable func, uint8_t thread_num);

    void start_threads();
    void suspend_threads();

    // release this thread object
    void graceful_stop_threads();
    void immediate_stop_threads();
    void join_threads();

    int resize_threads(uint8_t thread_num);
    size_t get_thread_pool_size();
};

} // end namespace tzhttpd

#endif // __TZHTTPD_THREAD_POOL_H__
