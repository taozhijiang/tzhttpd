/*-
 * Copyright (c) 2018-2019 TAO Zhijiang<taozhijiang@gmail.com>
 *
 * Licensed under the BSD-3-Clause license, see LICENSE for full information.
 *
 */

#include <mutex>
#include <functional>

#include <boost/chrono.hpp>

#include <other/Log.h>
#include "Global.h"

namespace tzhttpd {
    
Global& Global::instance() {
    static Global instance;
    return instance;
}

bool Global::init(const std::string& setting_file) {

    initialized_ = true;

    timer_ptr_ = std::make_shared<roo::Timer>();
    if (!timer_ptr_ || !timer_ptr_->init()) {
        roo::log_err("Create and init roo::Timer service failed.");
        return false;
    }

    status_ptr_ = std::make_shared<roo::Status>();
    if (!status_ptr_) {
        roo::log_err("Create roo::Status failed.");
        return false;
    }

    setting_ptr_ = std::make_shared<roo::Setting>();
    if (!setting_ptr_ || !setting_ptr_->init(setting_file)) {
        roo::log_err("Create and init roo::Setting with cfg %s failed.", setting_file.c_str());
        return false;
    }
    
    return true;
}



} // end namespace tzhttpd
