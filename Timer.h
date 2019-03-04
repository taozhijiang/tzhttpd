/*-
 * Copyright (c) 2018-2019 TAO Zhijiang<taozhijiang@gmail.com>
 *
 * Licensed under the BSD-3-Clause license, see LICENSE for full information.
 *
 */

#ifndef __TZHTTPD_TIMER_H__
#define __TZHTTPD_TIMER_H__

#include <xtra_asio.h>

#include <boost/thread.hpp>

#include <functional>
#include <memory>

#include "EQueue.h"
#include "Log.h"

// 提供定时回调接口服务

typedef std::function<void (const boost::system::error_code& ec)> TimerEventCallable;

namespace tzhttpd {

class TimerObject: public std::enable_shared_from_this<TimerObject> {
public:
    TimerObject(io_service& ioservice,
                const TimerEventCallable& func, uint64_t msec,
                bool forever):
        io_service_(ioservice),
        steady_timer_(),
        func_(func),
        timeout_(msec),
        forever_(forever) {
    }

    ~TimerObject() {
        tzhttpd_log_err("Good, Timer released...");
    }

    bool init() {
        steady_timer_.reset(new steady_timer(io_service_));
        if (!steady_timer_) {
            return false;
        }

        steady_timer_->expires_from_now(boost::chrono::milliseconds(timeout_));
        steady_timer_->async_wait(
                std::bind(&TimerObject::timer_run, shared_from_this(), std::placeholders::_1));
        return true;
    }

    void timer_run(const boost::system::error_code& ec) {
        if (func_) {
            func_(ec);
        } else {
            tzhttpd_log_err("critical, func not initialized");
        }

        if (forever_) {
            steady_timer_->expires_from_now(boost::chrono::milliseconds(timeout_));
            steady_timer_->async_wait(
                    std::bind(&TimerObject::timer_run, shared_from_this(), std::placeholders::_1));
        }
    }

private:
    io_service& io_service_;
    std::unique_ptr<steady_timer> steady_timer_;
    TimerEventCallable func_;
    uint64_t timeout_;
    bool forever_;
};

class Timer {

public:
    static Timer& instance();

    bool init() {

        // 创建io_service工作线程
        io_service_thread_ = boost::thread(std::bind(&Timer::io_service_run, this));
        return true;
    }

    io_service& get_io_service() {
        return  io_service_;
    }

    bool add_timer(const TimerEventCallable& func, uint64_t msec, bool forever) {
        std::shared_ptr<TimerObject> timer = std::make_shared<TimerObject>(io_service_, std::move(func), msec, forever);
        if (!timer || !timer->init()) {
            return false;
        }
        return true;
    }

public:

private:

    Timer():
        io_service_thread_(),
        io_service_(),
        work_guard_(new io_service::work(io_service_)){
    }

    ~Timer() {
        work_guard_.reset();
    }

    Timer(const Timer&) = delete;
    Timer& operator=(const Timer&) = delete;

    // 再启一个io_service_，主要使用Timer单例和boost::asio异步框架
    // 来处理定时器等常用服务
    boost::thread io_service_thread_;
    io_service io_service_;

    // io_service如果没有任务，会直接退出执行，所以需要
    // 一个强制的work来持有之
    std::unique_ptr<io_service::work> work_guard_;

    void io_service_run() {

        tzhttpd_log_notice("Timer io_service thread running...");

        // io_service would not have had any work to do,
        // and consequently io_service::run() would have returned immediately.

        boost::system::error_code ec;
        io_service_.run(ec);

        tzhttpd_log_notice("Timer io_service thread terminated ...");
    }

};

} // end namespace tzhttpd


#endif // __TZHTTPD_TIMER_H__
