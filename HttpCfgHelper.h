/*-
 * Copyright (c) 2018 TAO Zhijiang<taozhijiang@gmail.com>
 *
 * Licensed under the BSD-3-Clause license, see LICENSE for full information.
 *
 */

#ifndef __TZHTTPD_HTTP_CFG_HELPER__
#define __TZHTTPD_HTTP_CFG_HELPER__

#include <memory>
#include <mutex>
#include <vector>
#include <functional>

#include <libconfig.h++>

#include <boost/optional.hpp>
#include <boost/noncopyable.hpp>

#include "StrUtil.h"
#include "Log.h"


// 值拷贝

namespace tzhttpd {

typedef std::function<int (const libconfig::Config& cfg)> CfgUpdateCallable;

class HttpCfgHelper: public boost::noncopyable {
public:
    static HttpCfgHelper& instance();

    bool init(const std::string& cfgfile);

    int  update_cfg();
    int  register_cfg_callback(CfgUpdateCallable func);
    std::string get_cfgfile() const {
        return cfgfile_;
    }

    template <typename T>
    bool get_config_value(const std::string& key, T& t) {

        if (!cfg_ptr_ ||
            cfg_update_time_ < ::time(NULL) - 10*60 ) { // 超过10min，重新读取配置文件

            tzhttpd_log_debug("reloading config file, last update from %ld - %ld",
                              cfg_update_time_, ::time(NULL));

            auto cfg_ptr = load_cfg_file();
            if (!cfg_ptr) {
                tzhttpd_log_err("load config file %s failed!", cfgfile_.c_str());
                return false;
            }

            std::lock_guard<std::mutex> lock(lock_);

            std::swap(cfg_ptr, cfg_ptr_);
            cfg_update_time_ = ::time(NULL);
            return ConfUtil::conf_value(*cfg_ptr_, key, t);

        } else {

            std::lock_guard<std::mutex> lock(lock_);
            return ConfUtil::conf_value(*cfg_ptr_, key, t);
        }
    }

    std::unique_ptr<libconfig::Config> get_config() {
        return load_cfg_file();
    }

private:

    std::unique_ptr<libconfig::Config> load_cfg_file() {

        std::unique_ptr<libconfig::Config> cfg_ptr(new libconfig::Config());
        if (!cfg_ptr) {
            tzhttpd_log_err("new libconfig::Config failed!");
            return cfg_ptr;
        }

        try {
            cfg_ptr->readFile(cfgfile_.c_str());
        } catch(libconfig::FileIOException &fioex) {
            tzhttpd_log_err("I/O error while reading file: %s.", cfgfile_.c_str());
            cfg_ptr.reset();
        } catch(libconfig::ParseException &pex) {
            tzhttpd_log_err("Parse error at %d - %s", pex.getLine(), pex.getError());
            cfg_ptr.reset();
        }

        return cfg_ptr;
    }

    HttpCfgHelper():
        cfgfile_(), cfg_ptr_(), in_process_(false) {
    }

    ~HttpCfgHelper(){}

private:
    std::string cfgfile_;
    std::unique_ptr<libconfig::Config> cfg_ptr_;
    time_t cfg_update_time_;

    bool in_process_;
    std::mutex lock_;
    std::vector<CfgUpdateCallable> calls_;
};

} // end namespace tzhttpd

#endif // __TZHTTPD_HTTP_CFG_HELPER__
