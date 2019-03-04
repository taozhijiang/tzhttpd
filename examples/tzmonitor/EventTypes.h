/*-
 * Copyright (c) 2019 TAO Zhijiang<taozhijiang@gmail.com>
 *
 * Licensed under the BSD-3-Clause license, see LICENSE for full information.
 *
 */


#ifndef __BUSINESS_EVENT_TYPES_H__
#define __BUSINESS_EVENT_TYPES_H__

#include <memory>
#include <vector>
#include <string>

//
// 注意，这边的定义都是和thrift中对应的，因为考虑到效率和optional参数存取的问题，
// 没有直接使用thrift产生的结构信息
//

// 提交事件信息

struct event_data_t {
    int64_t     msgid;      // 消息ID，只需要在time_identity_event 域下唯一即可
    std::string metric;
    std::string tag;       // 标识区分，比如成功、失败、结果类别等
    int64_t     value;
};

struct event_report_t {
    std::string version;    // 1.0.0
    time_t      timestamp;  // 事件发生时间
    std::string service;    // 汇报服务名
    std::string entity_idx; // 汇报服务标识(多实例时候使用，否则默认为空，主机名字也编辑在这里面)

    std::vector<event_data_t> data;    // 事件不必相同，但是必须同一个time
};

typedef struct std::shared_ptr<event_report_t> event_report_ptr_t;


// 查询条件信息

enum GroupType {
    kGroupNone = 0,
    kGroupbyTimestamp,
    kGroupbyTag,
};

struct event_cond_t {
    std::string version;
    time_t      tm_interval; // 0表示无限制
    std::string service;
    std::string metric;

    // 可选参数
    time_t      tm_start;
    std::string entity_idx;
    std::string tag;

    // 有些存储引擎不支持
    enum GroupType groupby;
};

// 查询结果信息

struct event_info_t {

    // groupby的时候会返回对应group信息
    time_t      timestamp;
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

struct event_select_t {

    std::string version;

    time_t      timestamp;
    time_t      tm_interval;
    std::string service;
    std::string metric;

    // 如果请求参数有，则原样返回，否则不返回

    std::string entity_idx;
    std::string tag;

    event_info_t summary;
    std::vector<event_info_t> info;
};

struct event_handler_conf_t {
    int event_linger_;
    int event_step_;
    std::string store_type_;
};

#endif // __BUSINESS_EVENT_TYPES_H__
