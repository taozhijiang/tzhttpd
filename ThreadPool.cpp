/*-
 * Copyright (c) 2018-2019 TAO Zhijiang<taozhijiang@gmail.com>
 *
 * Licensed under the BSD-3-Clause license, see LICENSE for full information.
 *
 */

#include <mutex>
#include <functional>

#include <boost/chrono.hpp>

#include <other/Log.h>
#include "ThreadPool.h"

// impl details

namespace tzhttpd {

class ThreadPool::Impl {

public:
    explicit Impl(uint32_t pool_size):
        pool_size_(pool_size) {
    }

    Impl(const Impl&) = delete;
    Impl& operator=(const Impl&) = delete;

    void callable_wrapper(ThreadObjPtr ptr){

        // 先于线程工作之前的所有预备工作
        while (ptr->status_ == ThreadStatus::kInit)
            ::usleep(500*1000);

        func_(ptr);
    }

    virtual ~Impl() {
        graceful_stop_tasks();
    }

    // pool_size_ 已经校验
    bool init(ThreadRunnable func) {

        if (!func) {
            roo::log_err("Invalid runnable object!");
            return false;
        }
        func_ = func; // record it

        for (uint32_t i=0; i<pool_size_; ++i) {
            ThreadObjPtr workobj(new ThreadObj(ThreadStatus::kInit));
            if (!workobj) {
                roo::log_err("create ThreadObj failed!");
                return false;
            }
            ThreadPtr worker(new boost::thread(std::bind(&ThreadPool::Impl::callable_wrapper, this, workobj)));
            if (!worker || !workobj) {
                roo::log_err("create thread failed!");
                return false;
            }

            workers_[worker] = workobj;
            roo::log_warning("created task: #%d success ...", i);
        }

        return true;
    }

    bool init(ThreadRunnable func, uint32_t pool_size) {

        roo::log_warning("update pool_size from %d to %d", pool_size_, pool_size);
        pool_size_ = pool_size;
        return init(func);
    }

    void start_tasks() {

        std::map<ThreadPtr, ThreadObjPtr>::iterator it;
        for (it = workers_.begin(); it != workers_.end(); ++it) {
            it->second->status_ = ThreadStatus::kRunning;
        }
    }

    void suspend_tasks() {

        std::map<ThreadPtr, ThreadObjPtr>::iterator it;
        for (it = workers_.begin(); it != workers_.end(); ++it) {
            it->second->status_ = ThreadStatus::kSuspend;
        }
    }

    int incr_thread_pool() {
        return spawn_task(1);
    }

    int decr_thread_pool() {
        return reduce_task(1);
    }

    int resize_thread_pool(uint32_t num){

        int diff = num - workers_.size();
        if (diff == 0) {
            return 0;
        } else if (diff > 0) {
            return spawn_task(diff);
        } else if (diff < 0) {
            return reduce_task(::abs(diff));
        }

        return 0;
    }

    uint32_t get_pool_size() {
        return static_cast<uint32_t>(workers_.size());
    }

    void immediate_stop_tasks(){

        std::map<ThreadPtr, ThreadObjPtr>::iterator it;
        for (it = workers_.begin(); it != workers_.end(); ++it) {
            it->second->status_ = ThreadStatus::kTerminating;
        }
        pool_size_ = 0;
        return;
    }

    void join_tasks() {
        do {
            for (auto iter = workers_.begin(); iter != workers_.end(); ) {

#if __cplusplus >= 201103L

                if (iter->first->joinable()) {
                    iter->first->join();
                }
                iter = workers_.erase(iter);

#else
                auto curr_iter = iter ++;
                if (curr_iter->first->joinable()) {
                    curr_iter->first->join();
                }
                workers_.erase(curr_iter);
#endif

            }
        } while (!workers_.empty());
    }

    bool graceful_stop(ThreadPtr worker, uint32_t timed_seconds) {

        std::map<ThreadPtr, ThreadObjPtr>::iterator it;

        it = workers_.find(worker);
        if (it == workers_.end()) {
            roo::log_err("target worker not found!");
            return false;
        }

        // 处于ThreadStatus::kInit状态的线程此时也可以进来
        enum ThreadStatus old_status = it->second->status_;
        it->second->status_ = ThreadStatus::kTerminating;
        if (timed_seconds) {
            it->first->try_join_for(boost::chrono::seconds(timed_seconds));
        } else {
            it->first->join();
        }

        if (it->second->status_ != ThreadStatus::kDead) {
            roo::log_err("gracefulStop failed!");
            it->second->status_ = old_status; // 恢复状态

            return false;
        }

         roo::log_warning("Joined Task Success ...");

        // release this thread object
        pool_size_ --;
        workers_.erase(worker);
        return true;
    }

    void graceful_stop_tasks(){

        for (auto it = workers_.begin(); it != workers_.end(); ++it) {
            it->second->status_ = ThreadStatus::kTerminating;
        }

        while (!workers_.empty()) {

            auto it = workers_.begin();
            it->first->join();

            if (it->second->status_ != ThreadStatus::kDead) {
                roo::log_warning("may gracefulStop failed!");
            }

            // release this thread object
            pool_size_ --;
            workers_.erase(it->first);

            roo::log_err("current size: %ld", workers_.size());
        }

        roo::log_warning("Good! thread pool clean up down!");
    }

private:
    int spawn_task(uint32_t num){

        for (uint32_t i = 0; i < num; ++i) {
            ThreadObjPtr workobj(new ThreadObj(ThreadStatus::kInit));
            if (!workobj) {
                roo::log_err("create ThreadObj failed!");
                return -1;
            }
            ThreadPtr worker(new boost::thread(std::bind(&ThreadPool::Impl::callable_wrapper, this, workobj)));
            if (!worker || !workobj) {
                roo::log_err("create thread failed!");
                return -1;
            }

            workers_[worker] = workobj;
            pool_size_ ++;
            roo::log_warning("Created Additional Task: #%d ...", i);

            roo::log_warning("Start Additional Task: #%d ...", i);
            workobj->status_ = ThreadStatus::kRunning;
        }

        roo::log_warning("Current ThreadPool size: %d", pool_size_);
        return 0;
    }

    int reduce_task(uint32_t num) {

        size_t max_try = 50;
        size_t currsize = workers_.size();

        std::map<ThreadPtr, ThreadObjPtr>::iterator it;
        do {
            while (!workers_.empty()) {

                it = workers_.begin();
                graceful_stop(it->first, 10);
                max_try --;

                if (!max_try)
                    break;

                if ((currsize - workers_.size()) >= num)
                    break;
            }
        } while (0);

        roo::log_warning("current ThreadPool size: %d", pool_size_);
        return ((currsize - workers_.size()) >= num) ? 0 : -1;
    }

private:
    ThreadRunnable func_;

    std::mutex lock_;
    uint32_t pool_size_;
    std::map<ThreadPtr, ThreadObjPtr> workers_;
};



// call forward

bool ThreadPool::init_threads(ThreadRunnable func) {
    return impl_ptr_->init(func);
}

bool ThreadPool::init_threads(ThreadRunnable func, uint32_t pool_size) {
    if (pool_size == 0 || pool_size > kMaxiumThreadPoolSize ){
        roo::log_err("Invalid pool_sizeber %d, CRITICAL !!!", pool_size);
        roo::log_err("Using default 1");
        pool_size = 1;
    }
    return impl_ptr_->init(func, pool_size);
}

void ThreadPool::start_threads() {
    return impl_ptr_->start_tasks();
}

void ThreadPool::suspend_threads() {
    return impl_ptr_->suspend_tasks();
}

void ThreadPool::graceful_stop_threads() {
    return impl_ptr_->graceful_stop_tasks();
}

void ThreadPool::immediate_stop_threads() {
    return impl_ptr_->immediate_stop_tasks();
}

void ThreadPool::join_threads() {
    return impl_ptr_->join_tasks();
}

int ThreadPool::resize_threads(uint32_t pool_size) {
    return impl_ptr_->resize_thread_pool(pool_size);
}

uint32_t ThreadPool::get_pool_size() {
    return impl_ptr_->get_pool_size();
}

ThreadPool::ThreadPool(uint32_t pool_size) {

    if (pool_size == 0 || pool_size > kMaxiumThreadPoolSize ){
        roo::log_err("invalid pool_sizeber %d, CRITICAL !!!", pool_size);
        ::abort();
    }

    impl_ptr_.reset(new Impl(pool_size));
    if (!impl_ptr_) {
        roo::log_err("create thread pool impl failed, CRITICAL!!!!");
        ::abort();
    }
}

ThreadPool::~ThreadPool() {
}

} // end namespace tzhttpd
