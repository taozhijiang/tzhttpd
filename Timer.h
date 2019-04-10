/*-
 * Copyright (c) 2018-2019 TAO Zhijiang<taozhijiang@gmail.com>
 *
 * Licensed under the BSD-3-Clause license, see LICENSE for full information.
 *
 */

#ifndef __TZHTTPD_TIMER_H__
#define __TZHTTPD_TIMER_H__

#include <xtra_rhel.h>

#include <boost/asio.hpp>

#include <boost/asio/steady_timer.hpp>
using boost::asio::steady_timer;

#include "EQueue.h"
#include "Log.h"


// 提供定时回调接口服务

typedef std::function<void (const boost::system::error_code& ec)> TimerEventCallable;

namespace tzhttpd {

class TimerObject: public std::enable_shared_from_this<TimerObject> {

public:
    TimerObject(boost::asio::io_service& ioservice,
                const TimerEventCallable& func, uint64_t msec,
                bool forever):
        io_service_(ioservice),
        steady_timer_(),
        func_(func),
        timeout_(msec),
        forever_(forever) {
    }

    ~TimerObject() {
        revoke_timer();
        tzhttpd_log_debug("Good, Timer released...");
    }


    // 禁止拷贝
    TimerObject(const TimerObject&) = delete;
    TimerObject& operator=(const TimerObject&) = delete;


    bool init();

    void cancel_timer() {

        if (steady_timer_) {
            boost::system::error_code ec;
            steady_timer_->cancel(ec);

            steady_timer_.reset();
        }
    }

    void revoke_timer() {
        forever_ = false;
        cancel_timer();
    }

private:
    void timer_run(const boost::system::error_code& ec);

private:
    boost::asio::io_service& io_service_;
    std::unique_ptr<steady_timer> steady_timer_;
    TimerEventCallable func_;
    uint64_t timeout_;
    bool forever_;
};



// 注意，这里的Timer不持有任何TimerObject对象的智能指针，
//      TimerObject完全是依靠shared_from_this()自持有的
class Timer {

public:
    static Timer& instance();

    bool init() {

        // 创建io_service工作线程
        io_service_thread_ = boost::thread(std::bind(&Timer::io_service_run, this));
        return true;
    }

    void threads_join() {
		work_guard_.reset();
        io_service_thread_.join();
    }

    boost::asio::io_service& get_io_service() {
        return  io_service_;
    }


    // 增强版的定时器，返回TimerObject，可以控制定时器的取消
    bool add_timer(const TimerEventCallable& func, uint64_t msec, bool forever);
    std::shared_ptr<TimerObject> add_better_timer(const TimerEventCallable& func, uint64_t msec, bool forever);


private:

    Timer():
        io_service_thread_(),
        io_service_(),
        work_guard_(new boost::asio::io_service::work(io_service_)){
    }

    ~Timer() {
        io_service_.stop();
        work_guard_.reset();
    }

    // 禁止拷贝
    Timer(const Timer&) = delete;
    Timer& operator=(const Timer&) = delete;


    // 再启一个io_service_，主要使用Timer单例和boost::asio异步框架
    // 来处理定时器等常用服务
    boost::thread io_service_thread_;
    boost::asio::io_service io_service_;

    // io_service如果没有任务，会直接退出执行，所以需要
    // 一个强制的work来持有之
    std::unique_ptr<boost::asio::io_service::work> work_guard_;

    void io_service_run() {

        tzhttpd_log_notice("Timer io_service thread running...");

        // io_service would not have had any work to do,
        // and consequently io_service::run() would have returned immediately.

        boost::system::error_code ec;
        io_service_.run(ec);

        tzhttpd_log_notice("Timer io_service thread terminated ...");
        tzhttpd_log_notice("error_code: {%d} %s", ec.value(), ec.message().c_str());
    }

};

} // end namespace tzhttpd


#endif // __TZHTTPD_TIMER_H__
