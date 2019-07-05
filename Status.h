/*-
 * Copyright (c) 2018-2019 TAO Zhijiang<taozhijiang@gmail.com>
 *
 * Licensed under the BSD-3-Clause license, see LICENSE for full information.
 *
 */

#ifndef __TZHTTPD_STATUS__
#define __TZHTTPD_STATUS__

// 各个模块可以上报自己的状态，以json的格式输出来，然后这个模块进行数据汇总
// 内部模块采用 internal/status进行状态的展示

#include <mutex>
#include <vector>
#include <functional>

#include <string>
#include <sstream>

#include <other/Log.h>


namespace tzhttpd {

// 配置动态更新回调函数接口类型
// 注意keyOut的构造，输出的时候按照这个排序

typedef std::function<int (std::string& module, std::string& name, std::string& val)> StatusCallable;


class Status {

public:
    static Status& instance();

    int register_status_callback(const std::string& name, StatusCallable func);
    int collect_status(std::string& output);

    // self
    int module_status(std::string& module, std::string& name, std::string& val);

private:
    Status():
        lock_(),
        calls_() {

        register_status_callback(
            "tzhttpd-Status",
            std::bind(&Status::module_status, this,
                      std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));

    }

    ~Status()
        {
    }

    Status(const Status&) = delete;
    Status& operator=(const Status&) = delete;

private:

    // 使用vector，这样能保留原始的注册顺序，甚至多次注册
    std::mutex lock_;
    std::vector<std::pair<std::string, StatusCallable>> calls_;
};



} // end namespace tzhttpd

#endif // __TZHTTPD_STATUS__
