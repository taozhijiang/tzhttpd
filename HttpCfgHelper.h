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

private:
    HttpCfgHelper():
        cfgfile_(), cfg_ptr_(), in_process_(false) {
    }

    ~HttpCfgHelper(){}

private:
    std::string cfgfile_;
    std::unique_ptr<libconfig::Config> cfg_ptr_;

    bool in_process_;
    std::mutex lock_;
    std::vector<CfgUpdateCallable> calls_;
};

} // end namespace tzhttpd

#endif // __TZHTTPD_HTTP_CFG_HELPER__
