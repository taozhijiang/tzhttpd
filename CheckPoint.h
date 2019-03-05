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

typedef int (* CP_report_event_func_t)(const std::string& metric, int32_t value, const std::string& tag);
extern CP_report_event_func_t checkpoint_report_event_func_impl_ ;
void set_checkpoint_report_event_func(CP_report_event_func_t func);


// impl

struct CountPerfByMs {

    explicit CountPerfByMs(const std::string& metric, std::string tag = "T"):
        metric_(metric),
        tag_(tag) {
        ::gettimeofday(&start_, NULL);
    }

    ~CountPerfByMs() {
        struct timeval end;
        ::gettimeofday(&end, NULL);

        int64_t time_ms = ( 1000000 * ( end.tv_sec - start_.tv_sec ) + end.tv_usec - start_.tv_usec ) / 1000; // ms

        if(checkpoint_report_event_func_impl_) {
            checkpoint_report_event_func_impl_(metric_, static_cast<int32_t>(time_ms), tag_);
        } else {
            tzhttpd_log_debug("report metric:%s, value:%d, tag:%s",
                              metric_.c_str(), static_cast<int32_t>(time_ms), tag_.c_str());
        }
    }

    CountPerfByMs(const CountPerfByMs&) = delete;
    CountPerfByMs& operator=(const CountPerfByMs&) = delete;

    void set_tag(const std::string& tag) {
        tag_ = tag;
    }

private:
    struct timeval start_;
    const std::string metric_;
    std::string tag_;                // 是否是调用错误等标记
};


struct CountPerfByUs {

    explicit CountPerfByUs(const std::string& metric, std::string tag = "T"):
        metric_(metric),
        tag_(tag) {
        ::gettimeofday(&start_, NULL);
    }

    ~CountPerfByUs() {
        struct timeval end;
        ::gettimeofday(&end, NULL);

        int64_t time_us = ( 1000000 * ( end.tv_sec - start_.tv_sec ) + end.tv_usec - start_.tv_usec ); // us
        if(checkpoint_report_event_func_impl_) {
            checkpoint_report_event_func_impl_(metric_, static_cast<int32_t>(time_us), tag_);
        } else {
            tzhttpd_log_debug("report metric:%s, value:%d, tag:%s",
                              metric_.c_str(), static_cast<int32_t>(time_us), tag_.c_str());
        }
    }

    CountPerfByUs(const CountPerfByUs&) = delete;
    CountPerfByUs& operator=(const CountPerfByUs&) = delete;

    void set_tag(const std::string& tag) {
        tag_ = tag;
    }

private:
    struct timeval start_;
    const std::string metric_;
    std::string tag_;                // 是否是调用错误等标记
};


// raw reporter
struct ReportEvent {

    static void report_event(const std::string& metric, int32_t value, std::string tag = "T") {
        if(checkpoint_report_event_func_impl_) {
            checkpoint_report_event_func_impl_(metric, value, tag);
        } else {
            tzhttpd_log_debug("report metric:%s, value:%d, tag:%s",
                              metric.c_str(), value, tag.c_str());
        }
    }
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
