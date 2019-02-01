/*-
 * Copyright (c) 2018 TAO Zhijiang<taozhijiang@gmail.com>
 *
 * Licensed under the BSD-3-Clause license, see LICENSE for full information.
 *
 */

#ifndef __TZHTTPD_HTTP_EXECUTOR_H__
#define __TZHTTPD_HTTP_EXECUTOR_H__


#include <xtra_rhel6.h>

#include <libconfig.h++>

#include <boost/atomic/atomic.hpp>
#include <boost/thread/locks.hpp>

#include "StrUtil.h"

#include "HttpProto.h"
#include "ServiceIf.h"
#include "HttpHandler.h"

namespace tzhttpd {


class HttpExecutor: public ServiceIf {

public:

    HttpExecutor(const std::string& hostname):
        default_get_handler_(),
        EMPTY_STRING(),
        hostname_(hostname) {
    }


    bool init() {

        HttpGetHandler func = std::bind(&HttpExecutor::default_get_handler, this,
                              std::placeholders::_1, std::placeholders::_2,
                              std::placeholders::_3, std::placeholders::_4);
        default_get_handler_.reset(new HttpHandlerObject(EMPTY_STRING, func, true, true));
        if (!default_get_handler_) {
            return false;
        }

        return true;
    }

    bool init(const libconfig::Setting& setting) {

#if 0
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


            // configured redirect, pass following configure
            tzhttpd_log_alert("redirect %s configure ok for host %s",
                              redirect_str_.c_str(), hostname_.c_str());
            return true;
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
                              static_cast<int>(cache_controls_.size()), hostname_.c_str());
            for (auto iter = cache_controls_.begin(); iter != cache_controls_.end(); ++iter) {
                tzhttpd_log_debug("%s => %s", iter->first.c_str(), iter->second.c_str());
            }
        }

        if (setting.exists("compress_control")) {

            std::string suffix {};
            ConfUtil::conf_value(setting, "compress_control", suffix);

            std::vector<std::string> suffixes {};
            boost::split(suffixes, suffix, boost::is_any_of(";"));
            for (auto iter = suffixes.begin(); iter != suffixes.cend(); ++ iter){
                std::string tmp = boost::trim_copy(*iter);
                if (tmp.empty())
                    continue;

                compress_controls_.insert(tmp);
            }

            tzhttpd_log_debug("total %d compress ctrl for vhost %s",
                              static_cast<int>(compress_controls_.size()), hostname_.c_str());
        }

#endif
        return true;
    }

    void handle_http_request(std::shared_ptr<HttpReqInstance> http_req_instance);

    std::string instance_name() override {
        return hostname_;
    }

    int register_get_handler(const HttpGetHandler& handler) override {
        return 0;
    }

    int register_post_handler(const HttpPostHandler& handler) override {
        return 0;
    }

private:

    // 路由选择算法
    int do_find_handler(const enum HTTP_METHOD& method,
                        const std::string& uri,
                        HttpHandlerObjectPtr& handler);


private:
    std::string hostname_;
    std::string http_docu_root_;
    std::vector<std::string> http_docu_index_;
    const std::string EMPTY_STRING;


    // default http get handler, important for web_server
    HttpHandlerObjectPtr default_get_handler_;
    int default_get_handler(const HttpParser& http_parser, std::string& response,
                            std::string& status_line, std::vector<std::string>& add_header);

    // 30x 重定向使用
    // http redirect part
    std::string redirect_str_;
    int http_redirect_handler(std::string red_code, std::string red_uri,
                              const HttpParser& http_parser, const std::string& post_data,
                              std::string& response,
                              std::string& status_line, std::vector<std::string>& add_header);


    // 使用vector保存handler，保证是先注册handler具有高优先级
    boost::shared_mutex rwlock_;
    std::vector<std::pair<UriRegex, HttpHandlerObjectPtr>> handlers_;

    // 缓存策略规则
    std::map<std::string, std::string> cache_controls_;

    // 压缩控制
    std::set<std::string> compress_controls_;
};

} // tzhttpd


#endif // __TZHTTPD_HTTP_EXECUTOR_H__

