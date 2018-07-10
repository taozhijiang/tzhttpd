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

#include "CgiHelper.h"
#include "CgiWrapper.h"
#include "SlibLoader.h"

namespace tzhttpd {

class HttpParser;

typedef std::function<int (const HttpParser& http_parser, \
                           std::string& response, std::string& status_line, std::vector<std::string>& add_header)> HttpGetHandler;
typedef std::function<int (const HttpParser& http_parser, const std::string& post_data, \
                           std::string& response, std::string& status_line, std::vector<std::string>& add_header)> HttpPostHandler;

template<typename T>
struct HttpHandlerObject {

    boost::atomic<bool>    built_in_;      // built_in handler，无需引用计数
    boost::atomic<int64_t> success_cnt_;
    boost::atomic<int64_t> fail_cnt_;

    boost::atomic<bool>    working_;       // 正在

    T handler_;

    explicit HttpHandlerObject(const T& t, bool built_in = false):
    built_in_(built_in), success_cnt_(0), fail_cnt_(0), working_(true),
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
    bool check_exist_http_get_handler(std::string uri_regex) {
        return do_check_exist_http_handler(uri_regex, get_handler_);
    }

    bool check_exist_http_post_handler(std::string uri_regex) {
        return do_check_exist_http_handler(uri_regex, post_handler_);
    }

    int switch_http_get_handler(std::string uri_regex, bool on) {
        return do_switch_http_handler(uri_regex, on, get_handler_);
    }
    int switch_http_post_handler(std::string uri_regex, bool on) {
        return do_switch_http_handler(uri_regex, on, post_handler_);
    }

    int register_http_get_handler(std::string uri_regex, const HttpGetHandler& handler, bool built_in);
    int register_http_post_handler(std::string uri_regex, const HttpPostHandler& handler, bool built_in);

    // uri match
    int find_http_get_handler(std::string uri, HttpGetHandlerObjectPtr& phandler_obj);
    int find_http_post_handler(std::string uri, HttpPostHandlerObjectPtr& phandler_obj);

    int update_run_cfg(const libconfig::Config& cfg);

    std::string pure_uri_path(std::string uri) {  // copy
        uri = boost::algorithm::trim_copy(boost::to_lower_copy(uri));
        while (uri[uri.size()-1] == '/' && uri.size() > 1)  // 全部的小写字母，去除尾部
            uri = uri.substr(0, uri.size()-1);

        return uri;
    }

private:
    template<typename T>
    bool do_check_exist_http_handler(std::string uri_regex, const T& handlers);

    template<typename T>
    int do_switch_http_handler(std::string uri_regex, bool on, T& handlers);

private:

    int parse_cfg(const libconfig::Config& cfg, const std::string& key, std::map<std::string, std::string>& path_map);

    boost::shared_mutex rwlock_;

    // 使用vector保存handler，保证是先注册handler具有高优先级
    std::vector<std::pair<UriRegex, HttpPostHandlerObjectPtr>> post_handler_;
    std::vector<std::pair<UriRegex, HttpGetHandlerObjectPtr>>  get_handler_;

};

namespace http_handler {

int default_http_get_handler(const HttpParser& http_parser, std::string& response,
                             std::string& status_line, std::vector<std::string>& add_header);

extern std::shared_ptr<HttpGetHandlerObject> default_http_get_phandler_obj;


} // end namespace http_handler
} // end namespace tzhttpd


#endif //__TZHTTPD_HTTP_HANDLER_H__
