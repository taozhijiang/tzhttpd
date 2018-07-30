/*-
 * Copyright (c) 2018 TAO Zhijiang<taozhijiang@gmail.com>
 *
 * Licensed under the BSD-3-Clause license, see LICENSE for full information.
 *
 */


#ifndef _TZ_MONITOR_CLIENT_H__
#define _TZ_MONITOR_CLIENT_H__

#include <errno.h>
#include <libconfig.h++>

#include <memory>
#include <boost/noncopyable.hpp>
#include "EventTypes.h"

typedef void(* CP_log_store_func_t)(int priority, const char *format, ...);

namespace TzMonitor {

class TzMonitorClient: public boost::noncopyable {
public:
    explicit TzMonitorClient(std::string entity_idx = "1");
    TzMonitorClient(std::string host, std::string serv, std::string entity_idx = "1");

    ~TzMonitorClient();

    bool init(const std::string& cfgFile, CP_log_store_func_t log_func);
    bool init(const libconfig::Config& cfg, CP_log_store_func_t log_func);
    int update_run_cfg(const libconfig::Config& cfg);

    int report_event(const std::string& name, int64_t value, std::string flag = "T");

    // 常用便捷接口
    int retrieve_stat(const std::string& name, int64_t& count, int64_t& avg, time_t intervel_sec = 60);
    int retrieve_stat(const std::string& name, const std::string& flag, int64_t& count, int64_t& avg, time_t intervel_sec = 60);

    int retrieve_stat_flag(const std::string& name, event_query_t& stat, time_t intervel_sec = 60);
    int retrieve_stat_time(const std::string& name, event_query_t& stat, time_t intervel_sec = 60);
    int retrieve_stat_time(const std::string& name, const std::string& flag, event_query_t& stat, time_t intervel_sec = 60);

    // 最底层的接口，可以做更加精细化的查询
    int retrieve_stat(const event_cond_t& cond, event_query_t& stat);

private:
    class Impl;
    std::shared_ptr<Impl> impl_ptr_;
};

} // end namespace

#endif // _TZ_MONITOR_CLIENT_H__
