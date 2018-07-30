/*-
 * Copyright (c) 2018 TAO Zhijiang<taozhijiang@gmail.com>
 *
 * Licensed under the BSD-3-Clause license, see LICENSE for full information.
 *
 */


#ifndef _TZ_EVENT_TYPES_H__
#define _TZ_EVENT_TYPES_H__

#include <memory>
#include <vector>
#include <string>

//
// 注意，这边的定义都是和thrift中对应的，因为考虑到效率和optional参数存取的问题，
// 没有直接使用thrift产生的结构信息
//

// 提交事件信息

struct event_data_t {
    std::string name;
    int64_t     msgid;      // 消息ID，只需要在time_identity_event 域下唯一即可
    int64_t     value;
    std::string flag;       // 标识区分，比如成功、失败、结果类别等
};

struct event_report_t {
    std::string version;    // 1.0.0

    time_t      time;       // 事件发生时间

    std::string host;       // 事件主机名或者主机IP
    std::string serv;       // 汇报服务名
    std::string entity_idx; // 汇报服务标识(多实例时候使用，否则默认1)

    std::vector<event_data_t> data;    // 事件不必相同，但是必须同一个time
};

typedef struct std::shared_ptr<event_report_t> event_report_ptr_t;


// 查询条件信息

enum GroupType {
    kGroupNone = 0,
    kGroupbyTime,
    kGroupbyFlag,
};

struct event_cond_t {
    std::string version;

    time_t      start;
    time_t      interval_sec;

    std::string host;
    std::string serv;
    std::string entity_idx;

    std::string name;
    std::string flag;

    enum GroupType groupby;
};

// 查询结果信息

struct event_info_t {
    time_t      time;
    std::string flag;

    int         count;
    int64_t     value_sum;
    int64_t     value_avg;
    double      value_std;
};

struct event_query_t {
    std::string version;
    time_t      time;
    time_t      interval_sec;

    // 如果请求参数有，则原样返回，否则不返回
    std::string host;
    std::string serv;
    std::string entity_idx;
    std::string name;
    std::string flag;

    event_info_t summary;
    std::vector<event_info_t> info;
};

#endif // _TZ_EVENT_TYPES_H__
