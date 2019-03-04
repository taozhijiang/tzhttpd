/*-
 * Copyright (c) 2018-2019 TAO Zhijiang<taozhijiang@gmail.com>
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
    kInit = 1,
    kRunning = 2,
    kSuspend = 3,
    kTerminating = 4,
    kDead = 5,
};

struct ThreadObj {
    ThreadObj(enum ThreadStatus status):
        status_(status) {
    }
    enum ThreadStatus status_;
};

const static uint32_t kMaxiumThreadPoolSize = 65535;

// 因为需要适用timed_join，但是std::thread没有，所以这里仍然使用boost::thread

typedef std::shared_ptr<boost::thread>       ThreadPtr;
typedef std::shared_ptr<ThreadObj>           ThreadObjPtr;
typedef std::function<void (ThreadObjPtr)>   ThreadRunnable;

class ThreadPool {

    // 先于线程工作之前的所有预备工作
    class Impl;
    std::unique_ptr<Impl> impl_ptr_;

public:

    ThreadPool(uint32_t pool_size = 1);
    ~ThreadPool();

    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;


    bool init_threads(ThreadRunnable func);
    bool init_threads(ThreadRunnable func, uint32_t pool_size);

    void start_threads();
    void suspend_threads();

    int resize_threads(uint32_t);
    uint32_t get_pool_size();

    // release this thread object
    void graceful_stop_threads();
    void immediate_stop_threads();
    void join_threads();

};

} // end namespace tzhttpd

#endif // __TZHTTPD_THREAD_POOL_H__
