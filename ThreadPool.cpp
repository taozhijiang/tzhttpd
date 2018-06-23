/*-
 * Copyright (c) 2018 TAO Zhijiang<taozhijiang@gmail.com>
 *
 * Licensed under the BSD-3-Clause license, see LICENSE for full information.
 *
 */
 
#include <mutex>
#include <functional>

#include <boost/noncopyable.hpp>
#include <boost/thread.hpp>
//#include <boost/thread/thread_time.hpp>

#include "Log.h"
#include "ThreadPool.h"

namespace tzhttpd {

// impl

class ThreadPool::Impl : private boost::noncopyable {
public:
    explicit Impl(uint8_t thread_num):
        thread_num_(thread_num) {
    }

    void callable_wrapper(ThreadObjPtr ptr){

        // 先于线程工作之前的所有预备工作

        while (ptr->status_ == ThreadStatus::kThreadInit)
            ::usleep(500*1000);

        func_(ptr);
    }

    virtual ~Impl() {
        graceful_stop_tasks();
    }

    // thread_num 已经校验
    bool init(ThreadRunnable func) {

        if (!func) {
            log_err("Invalid runnable object!");
            return false;
        }
        func_ = func; // record it

        for (int i=0; i<thread_num_; ++i) {
            ThreadObjPtr workobj(new ThreadObj(ThreadStatus::kThreadInit));
            if (!workobj) {
                log_err("create ThreadObj failed!");
                return false;
            }
            ThreadPtr worker(new boost::thread(std::bind(&ThreadPool::Impl::callable_wrapper, this, workobj)));
            if (!worker || !workobj) {
                log_err("create thread failed!");
                return false;
            }

            workers_[worker] = workobj;
            log_alert("Created Task: #%d ...", i);
        }

        return true;
    }

    bool init(ThreadRunnable func, uint8_t thread_num) {
        log_alert("update thread_num from %d to %d", thread_num_, thread_num);
        thread_num_ = thread_num;

        return init(func);
    }

    void start_tasks() {
        std::map<ThreadPtr, ThreadObjPtr>::iterator it;

        for (it = workers_.begin(); it != workers_.end(); ++it) {
            it->second->status_ = ThreadStatus::kThreadRunning;
        }
    }

    void suspend_tasks() {
        std::map<ThreadPtr, ThreadObjPtr>::iterator it;

        for (it = workers_.begin(); it != workers_.end(); ++it) {
            it->second->status_ = ThreadStatus::kThreadSuspend;
        }
    }

    int incr_thread_pool() {
        return spawn_task(1);
    }

    int decr_thread_pool() {
        return reduce_task(1);
    }

    int resize_thread_pool(uint8_t num){

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

    uint8_t get_thread_pool_size() {
        return static_cast<uint8_t>(workers_.size());
    }

    void immediate_stop_tasks(){
        std::map<ThreadPtr, ThreadObjPtr>::iterator it;

        for (it = workers_.begin(); it != workers_.end(); ++it) {
            it->second->status_ = ThreadStatus::kThreadTerminating;
        }
        thread_num_ = 0;
        return;
    }

    void join_tasks() {
        do {
            for (auto iter = workers_.begin(); iter != workers_.end(); ++iter) {
                iter->first->join();
            }
        } while (!workers_.empty());
    }

    bool graceful_stop(ThreadPtr worker, uint8_t timed_seconds) {
        std::map<ThreadPtr, ThreadObjPtr>::iterator it;

        it = workers_.find(worker);
        if (it == workers_.end()) {
            log_err("Target worker not found!");
            return false;
        }

        // 处于ThreadStatus::kThreadInit状态的线程此时也可以进来
        enum ThreadStatus old_status = it->second->status_;
        it->second->status_ = ThreadStatus::kThreadTerminating;
        if (timed_seconds) {
            const boost::system_time timeout = boost::get_system_time() + boost::posix_time::milliseconds(timed_seconds * 1000);
            it->first->timed_join(timeout);
        } else {
            it->first->join();
        }

        if (it->second->status_ != ThreadStatus::kThreadDead) {
            log_err("gracefulStop failed!");
            it->second->status_ = old_status; // 恢复状态

            return false;
        }

        // release this thread object
        thread_num_ --;
        workers_.erase(worker);
        return true;
    }

    void graceful_stop_tasks(){

        for (auto it = workers_.begin(); it != workers_.end(); ++it) {
            it->second->status_ = ThreadStatus::kThreadTerminating;
        }

        while (!workers_.empty()) {

            auto it = workers_.begin();
            it->first->join();

            if (it->second->status_ != ThreadStatus::kThreadDead) {
                log_alert("may gracefulStop failed!");
            }

            // release this thread object
            thread_num_ --;
            workers_.erase(it->first);

            log_err("current size: %ld", workers_.size());
        }

        log_alert("Good! thread pool clean up down!");
    }

private:
    int spawn_task(uint8_t num){

        for (int i = 0; i < num; ++i) {
            ThreadObjPtr workobj(new ThreadObj(ThreadStatus::kThreadInit));
            if (!workobj) {
                log_err("create ThreadObj failed!");
                return -1;
            }
            ThreadPtr worker(new boost::thread(std::bind(&ThreadPool::Impl::callable_wrapper, this, workobj)));
            if (!worker || !workobj) {
                log_err("create thread failed!");
                return -1;
            }

            workers_[worker] = workobj;
            thread_num_ ++;
            log_alert("Created Additional Task: #%d ...", i);

            log_alert("Start Additional Task: #%d ...", i);
            workobj->status_ = ThreadStatus::kThreadRunning;
        }

        log_alert("Current ThreadPool size: %d", thread_num_);
        return 0;
    }

    int reduce_task(uint8_t num) {
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

        log_alert("Current ThreadPool size: %d", thread_num_);
        return ((currsize - workers_.size()) >= num) ? 0 : -1;
    }

private:
    ThreadRunnable func_;

    std::mutex lock_;
    uint8_t thread_num_;
    std::map<ThreadPtr, ThreadObjPtr> workers_;
};



// call forward

bool ThreadPool::init_threads(ThreadRunnable func) {
    return impl_ptr_->init(func);
}

bool ThreadPool::init_threads(ThreadRunnable func, uint8_t thread_num) {
    if (thread_num == 0 || thread_num > kMaxiumThreadPoolSize ){
        log_err("Invalid thread_number %d, CRITICAL !!!", thread_num);
        log_err("Using default 1");
        thread_num = 1;
    }
    return impl_ptr_->init(func, thread_num);
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

int ThreadPool::resize_threads(uint8_t thread_num) {
    return impl_ptr_->resize_thread_pool(thread_num);
}

size_t ThreadPool::get_thread_pool_size() {
    return impl_ptr_->get_thread_pool_size();
}

ThreadPool::ThreadPool(uint8_t thread_num) {

    if (thread_num == 0 || thread_num > kMaxiumThreadPoolSize ){
        log_err("Invalid thread_number %d, CRITICAL !!!", thread_num);
        ::abort();
    }

    impl_ptr_.reset(new Impl(thread_num));
    if (!impl_ptr_) {
        log_err("create thread pool impl failed, CRITICAL!!!!");
        ::abort();
    }
}

ThreadPool::~ThreadPool() {
}


} // end namespace tzhttpd
