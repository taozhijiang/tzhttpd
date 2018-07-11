/*-
 * Copyright (c) 2018 TAO Zhijiang<taozhijiang@gmail.com>
 *
 * Licensed under the BSD-3-Clause license, see LICENSE for full information.
 *
 */

#include "HttpCfgHelper.h"
#include "Log.h"

namespace tzhttpd {

HttpCfgHelper& HttpCfgHelper::instance() {
    static HttpCfgHelper helper;
    return helper;
}

bool HttpCfgHelper::init(const std::string& cfgfile) {

    cfg_ptr_.reset( new libconfig::Config() );
    if (!cfg_ptr_) {
        return false;
    }

    // try load and explain the cfg_file first.
    try {
        cfg_ptr_->readFile(cfgfile.c_str());
    } catch(libconfig::FileIOException &fioex) {
        fprintf(stderr, "I/O error while reading file: %s.", cfgfile.c_str());
        tzhttpd_log_err( "I/O error while reading file: %s.", cfgfile.c_str());
        cfg_ptr_.reset();
        return false;
    } catch(libconfig::ParseException &pex) {
        fprintf(stderr, "Parse error at %d - %s", pex.getLine(), pex.getError());
        tzhttpd_log_err( "Parse error at %d - %s", pex.getLine(), pex.getError());
        cfg_ptr_.reset();
        return false;
    }

    cfgfile_ = cfgfile;
    return true;
}

int HttpCfgHelper::update_cfg() {

    if (cfgfile_.empty()) {
        tzhttpd_log_err("cfg_file is empty, may not init HttpCfgHelper ? ...");
        return -1;
    }

    if (in_process_) {
        tzhttpd_log_err("!!! already in apply configure process, please try it later!");
        return 0;
    }

    auto cfg = load_cfg_file();
    if (!cfg) {
        tzhttpd_log_err("load config file %s failed!", cfgfile_.c_str());
        return false;
    }

    std::lock_guard<std::mutex> lock(lock_);

    std::swap(cfg, cfg_ptr_);
    cfg_update_time_ = ::time(NULL);

    int ret = 0;
    for (auto it = calls_.begin(); it != calls_.end(); ++it) {
        ret += (*it)(*cfg_ptr_); // call it!
    }

    tzhttpd_log_alert("ConfigHelper::update_cfg total callback return: %d", ret);
    in_process_ = false;

    return ret;
}

int HttpCfgHelper::register_cfg_callback(CfgUpdateCallable func) {

    if (!func){
        return -1;
    }

    std::lock_guard<std::mutex> lock(lock_);
    calls_.push_back(func);
    return 0;
}


} // end namespace tzhttpd
