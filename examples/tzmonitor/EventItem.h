/*-
 * Copyright (c) 2019 TAO Zhijiang<taozhijiang@gmail.com>
 *
 * Licensed under the BSD-3-Clause license, see LICENSE for full information.
 *
 */


#ifndef __BUSINESS_EVENT_ITEM_H__
#define __BUSINESS_EVENT_ITEM_H__

// in include dir, also as API
#include <map>
#include <sstream>

#include "EventTypes.h"

// 提交插入条目，数据规整后的结果，直接和数据库交互
struct event_insert_t {

    // 索引使用
    std::string service;

    std::string entity_idx;
    time_t      timestamp;
    uint8_t     step;

    std::string metric;
    std::string tag;

    int32_t     count;
    int64_t     value_sum;
    int32_t     value_avg;
    int32_t     value_min;
    int32_t     value_max;
    int32_t     value_p10;
    int32_t     value_p50;
    int32_t     value_p90;
};

struct service_metric_t {
    std::string metric;
    std::string tag;
};

// 相同timestamp内的事件汇聚
// 主要是删除了service数据

// key:metric
typedef std::map<std::string, std::vector<event_data_t>> events_by_metric_t;
typedef std::shared_ptr<events_by_metric_t>              events_by_metric_ptr_t;

struct events_by_time_t {
public:
    events_by_time_t(time_t tm, time_t step):
        timestamp_(tm),
        step_(step) {
    }

public:
    time_t             timestamp_;
    time_t             step_;
    events_by_metric_t data_;
};
typedef std::shared_ptr<events_by_time_t>                events_by_time_ptr_t;

typedef std::map<time_t, events_by_time_ptr_t>           timed_events_ptr_t;


// # 来做分隔符，所以字段中不允许有它
static inline
std::string construct_identity(const std::string& service, const std::string& entity_idx) {
    std::stringstream ss;

    ss << service;
    if (!entity_idx.empty()) {
        ss << "#" << entity_idx;
    }

    return ss.str();
}

#endif // __BUSINESS_EVENT_ITEM_H__
