/*-
 * Copyright (c) 2018 TAO Zhijiang<taozhijiang@gmail.com>
 *
 * Licensed under the BSD-3-Clause license, see LICENSE for full information.
 *
 */

#include "ConfHelper.h"

namespace tzhttpd {

ConfHelper& ConfHelper::instance() {
    static ConfHelper helper;
    return helper;
}

bool ConfHelper::init(std::string cfgfile) {

    cfgfile_ = cfgfile;

    conf_ptr_.reset( new libconfig::Config() );
    if (!conf_ptr_) {
        tzhttpd_log_err("create libconfig failed.");
        return false;
    }

    // try load and explain the cfg_file first.
    try {
        conf_ptr_->readFile(cfgfile.c_str());
    } catch(libconfig::FileIOException &fioex) {
        fprintf(stderr, "I/O error while reading file: %s.", cfgfile.c_str());
        tzhttpd_log_err( "I/O error while reading file: %s.", cfgfile.c_str());
        conf_ptr_.reset();
    } catch(libconfig::ParseException &pex) {
        fprintf(stderr, "Parse error at %d - %s", pex.getLine(), pex.getError());
        tzhttpd_log_err( "Parse error at %d - %s", pex.getLine(), pex.getError());
        conf_ptr_.reset();
    }

    // when init, parse conf failed was critical.
    if (!conf_ptr_) {
        return false;
    }

    return true;
}

int ConfHelper::update_runtime_conf() {

    if (cfgfile_.empty()) {
        tzhttpd_log_err("param cfg_file is not set, may not init HttpConfHelper ???");
        return -1;
    }

    if (in_process_) {
        tzhttpd_log_err("!!! already in process, please try again later!");
        return 0;
    }

    auto conf = load_conf_file();
    if (!conf) {
        in_process_ = false;
        tzhttpd_log_err("load config file %s failed.", cfgfile_.c_str());
        return false;
    }

    std::lock_guard<std::mutex> lock(lock_);

    // 重新读取配置并且解析成功之后，才更新这个指针
    std::swap(conf, conf_ptr_);
    conf_update_time_ = ::time(NULL);

    int ret = 0;
    for (auto it = calls_.begin(); it != calls_.end(); ++it) {
        ret += (*it)(*conf_ptr_); // call it!
    }

    tzhttpd_log_alert("ConfHelper::update_runtime_conf total callback return: %d", ret);
    in_process_ = false;

    return ret;
}

int ConfHelper::register_conf_callback(ConfUpdateCallable func) {

    if (!func){
        return -1;
    }

    std::lock_guard<std::mutex> lock(lock_);
    calls_.push_back(func);
    return 0;
}


} // end namespace tzhttpd
