/*-
 * Copyright (c) 2018-2019 TAO Zhijiang<taozhijiang@gmail.com>
 *
 * Licensed under the BSD-3-Clause license, see LICENSE for full information.
 *
 */

#include "Timer.h"

namespace tzhttpd {

Timer& Timer::instance() {
    static Timer handler;
    return handler;
}


bool TimerObject::init() {
    steady_timer_.reset(new steady_timer(io_service_));
    if (!steady_timer_) {
        tzhttpd_log_err("new steady_timer failed.");
        return false;
    }

    steady_timer_->expires_from_now(milliseconds(timeout_));
    steady_timer_->async_wait(
            std::bind(&TimerObject::timer_run, shared_from_this(), std::placeholders::_1));

    tzhttpd_log_info("successful add timer with milliseconds %lu", timeout_);
    return true;
}


void TimerObject::timer_run(const boost::system::error_code& ec) {

    if (ec == boost::asio::error::operation_aborted) {
        tzhttpd_log_notice("timer was cancelled...");
    } else {

        // 正常，或者其他错误码则转发给应用程序处理
        if (func_) {
            func_(ec);
        } else {
            tzhttpd_log_err("critical, func not initialized");
        }

    }

    // 即使cancel了，还是可以触发下一步的事件，除非手动恢复forever_
    if (forever_) {
        steady_timer_->expires_from_now(milliseconds(timeout_));
        steady_timer_->async_wait(
            std::bind(&TimerObject::timer_run, shared_from_this(), std::placeholders::_1));
        tzhttpd_log_info("renew forever timer with milliseconds %lu", timeout_);
    }
}



// Timer



bool Timer::add_timer(const TimerEventCallable& func, uint64_t msec, bool forever) {
    std::shared_ptr<TimerObject> timer
            = std::make_shared<TimerObject>(io_service_, func, msec, forever);

    if (!timer || !timer->init()) {
        tzhttpd_log_err("create and init timer failed.");
        return false;
    }
    return true;
}

// 增强版的定时器，返回TimerObject，可以控制定时器的取消
std::shared_ptr<TimerObject> Timer::add_better_timer(const TimerEventCallable& func, uint64_t msec, bool forever) {
    std::shared_ptr<TimerObject> timer
            = std::make_shared<TimerObject>(io_service_, func, msec, forever);

    if (!timer || !timer->init()) {
        tzhttpd_log_err("create and init timer failed.");
        timer.reset();
    }
    return timer;
}

} // end tzhttpd
