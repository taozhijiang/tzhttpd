/*-
 * Copyright (c) 2018-2019 TAO Zhijiang<taozhijiang@gmail.com>
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
        tzhttpd_log_err("invalid name or func param.");
        return -1;
    }

    std::lock_guard<std::mutex> lock(lock_);
    calls_.push_back({name, func});
    tzhttpd_log_debug("register status for %s success.",  name.c_str());

    return 0;
}

int Status::module_status(std::string& module, std::string& name, std::string& val) {

    module = "tzhttpd";
    name   = "Status";

    std::stringstream ss;
    ss << "registered status: " << std::endl;

    int i = 1;
    for (auto it = calls_.begin(); it != calls_.end(); ++it) {
        ss << "\t" << i++ << ". "<< it->first << std::endl;
    }

    val = ss.str();
    return 0;
}

int Status::collect_status(std::string& output) {

    std::lock_guard<std::mutex> lock(lock_);

    std::vector<std::pair<std::string, std::string>> results;
    for (auto iter = calls_.begin(); iter != calls_.end(); ++iter) {

        std::string strModule;
        std::string strName;
        std::string strValue;

        int ret = iter->second(strModule, strName, strValue);
        if (ret == 0) {
            std::string real = strModule + ":" + strName;
            results.push_back({real,strValue});
        } else {
            tzhttpd_log_err("call collect_status of %s failed with: %d",  iter->first.c_str(), ret);
        }
    }

    std::stringstream ss;

    ss << std::endl << std::endl <<"  *** SYSTEM RUNTIME STATUS ***  " << std::endl << std::endl;
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
