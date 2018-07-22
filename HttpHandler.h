/*-
 * Copyright (c) 2018 TAO Zhijiang<taozhijiang@gmail.com>
 *
 * Licensed under the BSD-3-Clause license, see LICENSE for full information.
 *
 */

#ifndef __TZHTTPD_HTTP_HANDLER_H__
#define __TZHTTPD_HTTP_HANDLER_H__

// 所有的http uri 路由

#include <libconfig.h++>

#include <vector>
#include <string>
#include <memory>

#include <boost/algorithm/string.hpp>
#include <boost/regex.hpp>
#include <boost/thread/locks.hpp>
#include <boost/thread/shared_mutex.hpp>

#include "StrUtil.h"

#include "CgiHelper.h"
#include "CgiWrapper.h"
#include "SlibLoader.h"

#include "HttpCfgHelper.h"

namespace tzhttpd {

class HttpParser;

typedef std::function<int (const HttpParser& http_parser, \
                           std::string& response, std::string& status_line, std::vector<std::string>& add_header)> HttpGetHandler;
typedef std::function<int (const HttpParser& http_parser, const std::string& post_data, \
                           std::string& response, std::string& status_line, std::vector<std::string>& add_header)> HttpPostHandler;

template<typename T>
struct HttpHandlerObject {

    std::string            path_;
    boost::atomic<bool>    built_in_;      // built_in handler，无需引用计数
    boost::atomic<int64_t> success_cnt_;
    boost::atomic<int64_t> fail_cnt_;

    boost::atomic<bool>    working_;       //  启用，禁用等标记

    T handler_;

    explicit HttpHandlerObject(const std::string& path, const T& t,
                               bool built_in = false, bool working = true):
        path_(path),
        built_in_(built_in), success_cnt_(0), fail_cnt_(0), working_(working),
        handler_(t) {
    }
};

typedef HttpHandlerObject<HttpGetHandler>  HttpGetHandlerObject;
typedef HttpHandlerObject<HttpPostHandler> HttpPostHandlerObject;

typedef std::shared_ptr<HttpGetHandlerObject>  HttpGetHandlerObjectPtr;
typedef std::shared_ptr<HttpPostHandlerObject> HttpPostHandlerObjectPtr;


class UriRegex: public boost::regex {
public:
    explicit UriRegex(const std::string& regexStr) :
        boost::regex(regexStr), str_(regexStr) {
    }

    std::string str() const {
        return str_;
    }

private:
    std::string str_;
};


class HttpHandler {

public:

    HttpHandler():
        vhost_name_({}) {
    }

    explicit HttpHandler(std::string vhost, const std::string& redirect):
        vhost_name_({}), http_docu_root_({}),
        http_docu_index_({}), redirect_str_(redirect) {
        vhost_name_ = StrUtil::drop_host_port(vhost);
    }

    explicit HttpHandler(std::string vhost,
                         const std::string& docu_root, const std::vector<std::string>& docu_index):
        vhost_name_({}), http_docu_root_(docu_root),
        http_docu_index_(docu_index), redirect_str_({}) {
        vhost_name_ = StrUtil::drop_host_port(vhost);
    }

    bool init(const libconfig::Setting& setting) {

        if (!redirect_str_.empty()) {

            auto pos = redirect_str_.find('~');
            if (pos == std::string::npos) {
                tzhttpd_log_err("error redirect config: %s", redirect_str_.c_str());
                return false;
            }

            std::string code = boost::trim_copy(redirect_str_.substr(0, pos));
            std::string uri  = boost::trim_copy(redirect_str_.substr(pos+1));

            if (code != "301" && code != "302") {
                tzhttpd_log_err("error redirect config: %s", redirect_str_.c_str());
                return false;
            }

            http_redirect_get_phandler_obj_ = std::make_shared<HttpGetHandlerObject>("[redirect]",
                                                   std::bind(&HttpHandler::http_redirect_handler, this,
                                                             code, uri,
                                                             std::placeholders::_1, dummy_post,
                                                             std::placeholders::_2,
                                                             std::placeholders::_3, std::placeholders::_4 ), true);
            http_redirect_post_phandler_obj_ = std::make_shared<HttpPostHandlerObject>("[redirect]",
                                                   std::bind(&HttpHandler::http_redirect_handler, this,
                                                             code, uri,
                                                             std::placeholders::_1, std::placeholders::_2,
                                                             std::placeholders::_3, std::placeholders::_4,
                                                             std::placeholders::_5 ), true);
            if (!http_redirect_get_phandler_obj_ || !http_redirect_post_phandler_obj_) {
                tzhttpd_log_err("Create redirect handler for %s failed!", vhost_name_.c_str());
                return false;
            }

            // configured redirect, pass following configure
            tzhttpd_log_alert("redirect %s configure ok for host %s",
                              redirect_str_.c_str(), vhost_name_.c_str());
            return true;
        }


        default_http_get_phandler_obj_ = std::make_shared<HttpGetHandlerObject>("[default]",
                                               std::bind(&HttpHandler::default_http_get_handler, this,
                                                         std::placeholders::_1, std::placeholders::_2,
                                                         std::placeholders::_3, std::placeholders::_4 ), true);
        if (!default_http_get_phandler_obj_) {
            tzhttpd_log_err("Create default get handler for %s failed!", vhost_name_.c_str());
            return false;
        }

        if (setting.exists("cache_control")) {
            const libconfig::Setting &http_cache_control = setting["cache_control"];
            for(int i = 0; i < http_cache_control.getLength(); ++i) {
                const libconfig::Setting& ctrl_item = http_cache_control[i];
                std::string suffix {};
                std::string ctrl_head {};

                ConfUtil::conf_value(ctrl_item, "suffix", suffix);
                ConfUtil::conf_value(ctrl_item, "header", ctrl_head);
                if(suffix.empty() || ctrl_head.empty()) {
                    tzhttpd_log_err("skip err cache ctrl configure item ...");
                    continue;
                }

                // parse
                {
                    std::vector<std::string> suffixes {};
                    boost::split(suffixes, suffix, boost::is_any_of(";"));
                    for (auto iter = suffixes.begin(); iter != suffixes.cend(); ++ iter){
                        std::string tmp = boost::trim_copy(*iter);
                        if (tmp.empty())
                            continue;

                        cache_controls_[tmp] = ctrl_head;
                    }
                }
            }

            // total display
            tzhttpd_log_debug("total %d cache ctrl for vhost %s",
                              static_cast<int>(cache_controls_.size()), vhost_name_.c_str());
            for (auto iter = cache_controls_.begin(); iter != cache_controls_.end(); ++iter) {
                tzhttpd_log_debug("%s => %s", iter->first.c_str(), iter->second.c_str());
            }
        }

        return true;
    }

    std::string get_vhost_name() {
        return vhost_name_;
    }

public:
    // check_exist
    bool check_exist_http_get_handler(const std::string& uri_r) {
        return do_check_exist_http_handler(uri_r, get_handler_);
    }

    bool check_exist_http_post_handler(const std::string& uri_r) {
        return do_check_exist_http_handler(uri_r, post_handler_);
    }

    // switch on/off
    int switch_http_get_handler(const std::string& uri_r, bool on) {
        return do_switch_http_handler(uri_r, on, get_handler_);
    }

    int switch_http_post_handler(const std::string& uri_r, bool on) {
        return do_switch_http_handler(uri_r, on, post_handler_);
    }

    // update_handler
    int update_http_get_handler(const std::string& uri_r, bool on) {

        auto cfg_ptr = HttpCfgHelper::instance().get_config();
        if (!cfg_ptr) {
            tzhttpd_log_err("load config file error: %s", HttpCfgHelper::instance().get_cfgfile().c_str());
            return -1;
        }

        std::string uri = StrUtil::pure_uri_path(uri_r);
        std::string key = "http.cgi_get_handlers";
        std::map<std::string, std::string> path_map {};
        // TODO
//        parse_cfg(*cfg_ptr, key, path_map);
        if (path_map.find(uri) == path_map.end()) {
            tzhttpd_log_err("find get dl path error for: %s", uri.c_str());
            return -2;
        }
        std::string dl_path = path_map.at(uri);

        if(do_unload_http_handler<HttpGetHandlerObjectPtr>(uri_r, on, get_handler_) != 0) {
            tzhttpd_log_err("unload get handler for %s failed!", uri.c_str());
            return -3;
        }

        http_handler::CgiGetWrapper getter(dl_path);
        if (!getter.init()) {
            tzhttpd_log_err("init get for %s @ %s failed, skip it!", uri.c_str(), dl_path.c_str());
            return -4;
        }

        register_http_get_handler(uri, getter, false, on);
        tzhttpd_log_debug("register_http_get_handler for %s @ %s OK!", uri.c_str(), dl_path.c_str());

        return 0;

    }

    int update_http_post_handler(const std::string& uri_r, bool on) {

        auto cfg_ptr = HttpCfgHelper::instance().get_config();
        if (!cfg_ptr) {
            tzhttpd_log_err("load config file error: %s", HttpCfgHelper::instance().get_cfgfile().c_str());
            return -1;
        }

        std::string uri = StrUtil::pure_uri_path(uri_r);
        std::string key = "http.cgi_post_handlers";
        std::map<std::string, std::string> path_map {};
        // TODO
 //       parse_cfg(*cfg_ptr, key, path_map);
        if (path_map.find(uri) == path_map.end()) {
            tzhttpd_log_err("find post dl path error for: %s", uri.c_str());
            return -2;
        }

        std::string dl_path = path_map.at(uri);

        if(do_unload_http_handler<HttpGetHandlerObjectPtr>(uri_r, on, get_handler_) != 0) {
            tzhttpd_log_err("unload get handler for %s failed!", uri.c_str());
            return -3;
        }

        http_handler::CgiPostWrapper poster(dl_path);
        if (!poster.init()) {
            tzhttpd_log_err("init post for %s @ %s failed, skip it!", uri.c_str(), dl_path.c_str());
            return -4;
        }

        register_http_post_handler(uri, poster, false, on);
        tzhttpd_log_debug("register_http_post_handler for %s @ %s OK!", uri.c_str(), dl_path.c_str());

        return 0;
    }


    int register_http_get_handler(const std::string& uri_r, const HttpGetHandler& handler,
                                  bool built_in, bool working = true);
    int register_http_post_handler(const std::string& uri_r, const HttpPostHandler& handler,
                                   bool built_in, bool working = true);

    // uri match
    int find_http_get_handler(std::string uri, HttpGetHandlerObjectPtr& phandler_obj);
    int find_http_post_handler(std::string uri, HttpPostHandlerObjectPtr& phandler_obj);

    int update_run_cfg(const libconfig::Setting& setting);

private:
    template<typename T>
    bool do_check_exist_http_handler(const std::string& uri_r, const T& handlers);

    template<typename T>
    int do_switch_http_handler(const std::string& uri_r, bool on, T& handlers);

    template<typename Ptr, typename T>
    int do_unload_http_handler(const std::string& uri_r, bool on, T& handlers);

private:

    std::string vhost_name_;
    std::string http_docu_root_;
    std::vector<std::string> http_docu_index_;
    std::string redirect_str_;

    // default http get handler, important for web_server
    int default_http_get_handler(const HttpParser& http_parser, std::string& response,
                                 std::string& status_line, std::vector<std::string>& add_header);
    std::shared_ptr<HttpGetHandlerObject> default_http_get_phandler_obj_;

    // http redirect part
    std::string dummy_post;
    int http_redirect_handler(std::string red_code, std::string red_uri,
                              const HttpParser& http_parser, const std::string& post_data,
                              std::string& response,
                              std::string& status_line, std::vector<std::string>& add_header);
    std::shared_ptr<HttpGetHandlerObject> http_redirect_get_phandler_obj_;
    std::shared_ptr<HttpPostHandlerObject> http_redirect_post_phandler_obj_;

    int parse_cfg(const libconfig::Setting& setting, const std::string& key, std::map<std::string, std::string>& path_map);


    // 使用vector保存handler，保证是先注册handler具有高优先级
    boost::shared_mutex rwlock_;
    std::vector<std::pair<UriRegex, HttpPostHandlerObjectPtr>> post_handler_;
    std::vector<std::pair<UriRegex, HttpGetHandlerObjectPtr>>  get_handler_;

    std::map<std::string, std::string> cache_controls_;

};


// template code should be .h

template<typename T>
bool HttpHandler::do_check_exist_http_handler(const std::string& uri_r, const T& handlers) {

    std::string uri = StrUtil::pure_uri_path(uri_r);
    boost::shared_lock<boost::shared_mutex> rlock(rwlock_);

    for (auto it = handlers.begin(); it != handlers.end(); ++ it) {
        if (it->first.str() == uri ) {
            return true;
        }
    }

    return false;
}


template<typename T>
int HttpHandler::do_switch_http_handler(const std::string& uri_r, bool on, T& handlers) {

    std::string uri = StrUtil::pure_uri_path(uri_r);
    boost::lock_guard<boost::shared_mutex> wlock(rwlock_);

    for (auto it = handlers.begin(); it != handlers.end(); ++ it) {
        if (it->first.str() == uri ) {
            if (it->second->working_ == on) {
                tzhttpd_log_err("uri handler for %s already in %s status...",
                                it->first.str().c_str(), on ? "on" : "off");
                return -1;
            } else {
                tzhttpd_log_alert("uri handler for %s update from %s to %s status...",
                                 it->first.str().c_str(),
                                 it->second->working_ ? "on" : "off", on ? "on" : "off");
                it->second->working_ = on;
                return 0;
            }
        }
    }

    tzhttpd_log_err("uri for %s not found, update status failed...!", uri.c_str());
    return -2;
}


template<typename Ptr, typename T>
int HttpHandler::do_unload_http_handler(const std::string& uri_r, bool on, T& handlers) {

    Ptr p_handler_object{};

    boost::lock_guard<boost::shared_mutex> wlock(rwlock_); // 持有互斥锁，不会再有新的请求了

    auto it = handlers.begin();
    for (it = handlers.begin(); it != handlers.end(); ++ it) {
        if (it->first.str() == uri_r ) {
            p_handler_object = it->second;
            break;
        }
    }

    if (p_handler_object && p_handler_object->built_in_) {
        tzhttpd_log_err("handler for %s is built_in type, we do not consider support replacement.",
                        p_handler_object->path_.c_str());
        return -1;
    }

    int retry_count = 10;
    if (p_handler_object) {

        while (p_handler_object.use_count() > 2 && -- retry_count > 0) {
            ::usleep(1000);
        }

        if (p_handler_object.use_count() > 2) {
            tzhttpd_log_err("handler for %s use_count: %ld, may disable it first and update...",
                            uri_r.c_str(), p_handler_object.use_count());
            return -2;
        }


        // safe remove the handler and (may) unload dll

        SAFE_ASSERT(it < handlers.end());
        handlers.erase(it);

        tzhttpd_log_alert("remove handler %s done!", uri_r.c_str());
    } else {

        tzhttpd_log_alert("handler %s not installed, just pass!", uri_r.c_str());
    }

    return 0;
}

} // end namespace tzhttpd


#endif //__TZHTTPD_HTTP_HANDLER_H__
