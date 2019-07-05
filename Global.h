/*-
 * Copyright (c) 2018-2019 TAO Zhijiang<taozhijiang@gmail.com>
 *
 * Licensed under the BSD-3-Clause license, see LICENSE for full information.
 *
 */

#ifndef __TZHTTPD_GLOBAL_H__
#define __TZHTTPD_GLOBAL_H__

#include <xtra_rhel.h>
#include <memory>

#include <concurrency/Timer.h>
#include <scaffold/Status.h>
#include <scaffold/Setting.h>

namespace tzhttpd {

class Global {
public:
    static Global& instance();
    bool init(const std::string& setting_file);
    
private:
    Global():
        initialized_(false){
    }

    ~Global() = default;

    Global(const Global&) = delete;
    Global& operator=(const Global&) = delete;
    
    bool initialized_;
    

public:

    // 使用Roo中的实现，但是都是内部持有独立的实例
    std::shared_ptr<roo::Setting> setting_ptr_;
    std::shared_ptr<roo::Status> status_ptr_;
    std::shared_ptr<roo::Timer> timer_ptr_;
    
};

} // end namespace tzhttpd

#endif // __TZHTTPD_GLOBAL_H__
