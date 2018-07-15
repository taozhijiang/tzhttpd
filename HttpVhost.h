/*-
 * Copyright (c) 2018 TAO Zhijiang<taozhijiang@gmail.com>
 *
 * Licensed under the BSD-3-Clause license, see LICENSE for full information.
 *
 */

#ifndef __TZHTTPD_HTTP_VHOST_H__
#define __TZHTTPD_HTTP_VHOST_H__

#include <string>

#include "Log.h"
#include "HttpHandler.h"

namespace tzhttpd {



class HttpVhost {

public:
    bool init();
    int update_run_cfg(const libconfig::Config& cfg);

public:

    // register handler in default_vhost_
    int register_http_get_handler(std::string uri_regex, const HttpGetHandler& handler, bool built_in) {
        return default_vhost_.register_http_get_handler(uri_regex, handler, built_in);
    }

    int register_http_post_handler(std::string uri_regex, const HttpPostHandler& handler, bool built_in) {
        return default_vhost_.register_http_post_handler(uri_regex, handler, built_in);
    }


    // register handler in specified vhost
    int register_http_get_handler(std::string vhost,
                                  std::string uri_regex, const HttpGetHandler& handler, bool built_in) {
        auto iter = vhosts_.find(vhost);
        if (iter != vhosts_.end()) {
            return iter->second.register_http_get_handler(uri_regex, handler, built_in);
        }
        tzhttpd_log_err("vhost: %s not found for get %s !", vhost.c_str(), uri_regex.c_str());
        return -1;
    }

    int register_http_post_handler(std::string vhost,
                                   std::string uri_regex, const HttpPostHandler& handler, bool built_in) {
        auto iter = vhosts_.find(vhost);
        if (iter != vhosts_.end()) {
            return iter->second.register_http_post_handler(uri_regex, handler, built_in);
        }
        tzhttpd_log_err("vhost: %s not found for post %s !", vhost.c_str(), uri_regex.c_str());
        return -1;
    }

    // find handler with vhost, if not registered vhost, using default one
    int find_http_get_handler(std::string vhost,
                              std::string uri, HttpGetHandlerObjectPtr& phandler_obj) {
        auto iter = vhosts_.find(vhost);
        if (iter != vhosts_.end()) {
            tzhttpd_log_debug("finding post %s:%s in vhosts_", vhost.c_str(), uri.c_str());
            return iter->second.find_http_get_handler(uri, phandler_obj);
        }

        tzhttpd_log_debug("finding post %s:%s in default_vhost_", vhost.c_str(), uri.c_str());
        return default_vhost_.find_http_get_handler(uri, phandler_obj);
    }

    int find_http_post_handler(std::string vhost,
                               std::string uri, HttpPostHandlerObjectPtr& phandler_obj) {
        auto iter = vhosts_.find(vhost);
        if (iter != vhosts_.end()) {
            tzhttpd_log_debug("finding post %s:%s in vhosts_", vhost.c_str(), uri.c_str());
            return iter->second.find_http_post_handler(uri, phandler_obj);
        }

        tzhttpd_log_debug("finding post %s:%s in default_vhost_", vhost.c_str(), uri.c_str());
        return default_vhost_.find_http_post_handler(uri, phandler_obj);
    }


    //
    // internel manage part
    //

    // handler switch on/off
    int switch_http_get_handler(std::string vhost, const std::string& uri_r, bool on) {
        if (vhost.empty()) {
            return default_vhost_.switch_http_get_handler(uri_r, on);
        }

        auto iter = vhosts_.find(vhost);
        if (iter == vhosts_.end()) {
            tzhttpd_log_err("swith get %s:%s, but vhost not found!", vhost.c_str(), uri_r.c_str());
            return -1;
        }
        return iter->second.switch_http_get_handler(uri_r, on);
    }

    int switch_http_post_handler(std::string vhost, const std::string& uri_r, bool on) {
        if (vhost.empty()) {
            return default_vhost_.switch_http_post_handler(uri_r, on);
        }

        auto iter = vhosts_.find(vhost);
        if (iter == vhosts_.end()) {
            tzhttpd_log_err("swith post %s:%s, but vhost not found!", vhost.c_str(), uri_r.c_str());
            return -1;
        }
        return iter->second.switch_http_post_handler(uri_r, on);
    }

    // update_handler
    int update_http_get_handler(std::string vhost, const std::string& uri_r, bool on) {
        if (vhost.empty()) {
            return default_vhost_.update_http_get_handler(uri_r, on);
        }

        auto iter = vhosts_.find(vhost);
        if (iter == vhosts_.end()) {
            tzhttpd_log_err("update get %s:%s, but vhost not found!", vhost.c_str(), uri_r.c_str());
            return -1;
        }
        return iter->second.update_http_get_handler(uri_r, on);
    }


    int update_http_post_handler(std::string vhost, const std::string& uri_r, bool on) {
        if (vhost.empty()) {
            return default_vhost_.update_http_post_handler(uri_r, on);
        }

        auto iter = vhosts_.find(vhost);
        if (iter == vhosts_.end()) {
            tzhttpd_log_err("update post %s:%s, but vhost not found!", vhost.c_str(), uri_r.c_str());
            return -1;
        }
        return iter->second.update_http_post_handler(uri_r, on);
    }

private:

    // vhosts_ 在启动的时候从配置文件加载，之后就不更新了
    // 因为理论上这会很少变动，而如果支持更新就需要额外的锁同步，反而会影响服务的整体性能

    std::map<std::string, HttpHandler> vhosts_;
    HttpHandler default_vhost_;


    // impl in HttpServerManager.cpp
    //
    // @/internel_manage?cmd=xxx&auth=d44bfc666db304b2f72b4918c8b46f78
    int internel_manage_http_get_handler(const HttpParser& http_parser, std::string& response,
                                         std::string& status_line, std::vector<std::string>& add_header);

};


} // end namespace tzhttpd


#endif //__TZHTTPD_HTTP_VHOST_H__
