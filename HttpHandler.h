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
#include "SlibLoader.h"

namespace tzhttpd {

class HttpParser;

typedef std::function<int (const HttpParser& http_parser, \
                           std::string& response, std::string& status_line, std::vector<std::string>& add_header)> HttpGetHandler;
typedef std::function<int (const HttpParser& http_parser, const std::string& post_data, \
                           std::string& response, std::string& status_line, std::vector<std::string>& add_header)> HttpPostHandler;

class UriRegex: public boost::regex {
public:
    explicit UriRegex(const std::string& regexStr) :
        boost::regex(regexStr), str_(regexStr) {
    }

    std::string str() {
        return str_;
    }

private:
    std::string str_;
};


class HttpHandler {

public:
    // 特例化模板
    int register_http_get_handler(std::string uri_regex, const HttpGetHandler& handler);
    int register_http_post_handler(std::string uri_regex, const HttpPostHandler& handler);

    int find_http_get_handler(std::string uri, HttpGetHandler& handler);
    int find_http_post_handler(std::string uri, HttpPostHandler& handler);

    int update_run_cfg(const libconfig::Config& cfg);

    std::string pure_uri_path(std::string uri) {  // copy
        uri = boost::algorithm::trim_copy(boost::to_lower_copy(uri));
        while (uri[uri.size()-1] == '/' && uri.size() > 1)  // 全部的小写字母，去除尾部
            uri = uri.substr(0, uri.size()-1);

        return uri;
    }

private:

    int parse_cfg(const libconfig::Config& cfg, const std::string& key, std::map<std::string, std::string>& path_map);

    boost::shared_mutex rwlock_;

    // 使用vector保存handler，保证是先注册handler具有高优先级
    std::vector<std::pair<UriRegex, HttpPostHandler>> post_handler_;
    std::vector<std::pair<UriRegex, HttpGetHandler>>  get_handler_;

};

namespace http_handler {

int default_http_get_handler(const HttpParser& http_parser, std::string& response,
                             std::string& status_line, std::vector<std::string>& add_header);

// @/manage?cmd=xxx&auth=d44bfc666db304b2f72b4918c8b46f78
int manage_http_get_handler(const HttpParser& http_parser, std::string& response,
                            std::string& status_line, std::vector<std::string>& add_header);


// deal with cgi request


class CgiWrapper {
public:
    explicit CgiWrapper(const std::string& dl_path):
        dl_path_(dl_path),
        dl_({}) {
    }
    bool load_dl();

protected:
    std::string dl_path_;
    std::shared_ptr<SLibLoader> dl_;
};


class CgiGetWrapper: public CgiWrapper {

public:
    explicit CgiGetWrapper(const std::string& dl_path):
        CgiWrapper(dl_path) {
    }

    bool init();
    int operator()(const HttpParser& http_parser,
                   std::string& response, std::string& status_line,
                   std::vector<std::string>& add_header);

private:
    cgi_get_handler_t func_;
};



class CgiPostWrapper: public CgiWrapper {

public:

    explicit CgiPostWrapper(const std::string& dl_path):
        CgiWrapper(dl_path) {
    }

    bool init();
    int operator()(const HttpParser& http_parser, const std::string& post_data,
                   std::string& response, std::string& status_line,
                   std::vector<std::string>& add_header);

private:
    cgi_post_handler_t func_;
};


} // end namespace http_handler



} // end namespace tzhttpd


#endif //__TZHTTPD_HTTP_HANDLER_H__
