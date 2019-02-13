/*-
 * Copyright (c) 2018 TAO Zhijiang<taozhijiang@gmail.com>
 *
 * Licensed under the BSD-3-Clause license, see LICENSE for full information.
 *
 */

#include "Status.h"

namespace tzhttpd {

Status& Status::instance() {
    static Status helper;
    return helper;
}


int Status::register_status_callback(const std::string& name, StatusCallable func) {

    if (name.empty() || !func){
        return -1;
    }

    std::lock_guard<std::mutex> lock(lock_);
    calls_[name] = func;
    return 0;
}

int Status::collect_status(std::string& output) {

    std::lock_guard<std::mutex> lock(lock_);

    std::map<std::string, std::string> results;
    for (auto iter = calls_.begin(); iter != calls_.end(); ++iter) {
        std::string strKey;
        std::string strValue;

        int ret = iter->second(strKey, strValue);
        if (ret == 0) {
            results[strKey] = strValue;
        } else {
            tzhttpd_log_err("call collect_status of %s failed with: %d",  iter->first.c_str(), ret);
        }
    }

    std::stringstream ss;

    ss << "  *** SYSTEM RUNTIME STATUS ***  " << std::endl << std::endl;
    for (auto iter = results.begin(); iter != results.end(); ++iter) {
        ss << "[" << iter->first << "]" << std::endl;
        ss << iter->second << std::endl;
        ss << " ------------------------------- " << std::endl;
        ss << std::endl;
    }

    output = ss.str();
    return 0;
}

} // end namespace tzhttpd
