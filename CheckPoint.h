/*-
 * Copyright (c) 2018-2019 TAO Zhijiang<taozhijiang@gmail.com>
 *
 * Licensed under the BSD-3-Clause license, see LICENSE for full information.
 *
 */

#ifndef __TZHTTPD_CHECK_POINT__
#define __TZHTTPD_CHECK_POINT__

#include <sys/time.h>
#include <string>

#include "Log.h"

namespace tzhttpd {

// Log Store

typedef void(* CP_log_store_func_t)(int priority, const char *format, ...);
extern CP_log_store_func_t checkpoint_log_store_func_impl_;
void set_checkpoint_log_store_func(CP_log_store_func_t func);



// Event Report

typedef int (* CP_report_event_func_t)(const std::string& name, int64_t value, bool failed);
extern CP_report_event_func_t checkpoint_report_event_func_impl_ ;
void set_checkpoint_report_event_func(CP_report_event_func_t func);


// impl

struct CountPerfByMs {

    CountPerfByMs(const std::string& event):
        error_(false), event_(event) {
        ::gettimeofday(&start_, NULL);
    }

    ~CountPerfByMs() {
        struct timeval end;
        ::gettimeofday(&end, NULL);

        int64_t time_ms = ( 1000000 * ( end.tv_sec - start_.tv_sec ) + end.tv_usec - start_.tv_usec ) / 1000; // ms

        if(checkpoint_report_event_func_impl_) {
            checkpoint_report_event_func_impl_(event_, time_ms, error_);
        } else {
            tzhttpd_log_debug("%s - success: %s, perf: %ldms, ", event_.c_str(), error_ ? "false" : "true", time_ms);
        }
    }

    CountPerfByMs(const CountPerfByMs&) = delete;
    CountPerfByMs& operator=(const CountPerfByMs&) = delete;

    void set_error() {
        error_ = true;
    }

private:
    struct timeval start_;
    bool error_;                // 是否是调用错误等标记
    std::string event_;
};


struct CountPerfByUs {

    CountPerfByUs(const std::string& event):
        error_(false), event_(event) {
        ::gettimeofday(&start_, NULL);
    }

    ~CountPerfByUs() {
        struct timeval end;
        ::gettimeofday(&end, NULL);

        int64_t time_us = ( 1000000 * ( end.tv_sec - start_.tv_sec ) + end.tv_usec - start_.tv_usec ); // us
        if(checkpoint_report_event_func_impl_) {
            checkpoint_report_event_func_impl_(event_, time_us, error_);
        } else {
            tzhttpd_log_debug("%s - success: %s, perf: %ldus, ", event_.c_str(), error_ ? "false" : "true", time_us);
        }
    }

    CountPerfByUs(const CountPerfByUs&) = delete;
    CountPerfByUs& operator=(const CountPerfByUs&) = delete;

private:
    struct timeval start_;
    bool error_;                // 是否是调用错误等标记
    std::string event_;
};


} // end namespace tzhttpd


// macro should be outside of namspace
#define PUT_COUNT_FUNC_MS_PERF(T)  tzhttpd::CountPerfByMs \
                    checker( std::string(__FILE__) + " " + std::string( __func__ ), #T ); \
                    (void) checker

#define PUT_COUNT_FUNC_US_PERF(T)  tzhttpd::CountPerfByUs \
                    checker( std::string(__FILE__) + " " + std::string( __func__ ), #T ); \
                    (void) checker

#endif // __TZHTTPD_CHECK_POINT__
