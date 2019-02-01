/*-
 * Copyright (c) 2018 TAO Zhijiang<taozhijiang@gmail.com>
 *
 * Licensed under the BSD-3-Clause license, see LICENSE for full information.
 *
 */

#ifndef __TZHTTPD_HTTP_HANDLER_H__
#define __TZHTTPD_HTTP_HANDLER_H__

#include <functional>

#include <xtra_rhel6.h>

#include <libconfig.h++>

#include <boost/atomic/atomic.hpp>
#include <boost/thread/locks.hpp>

#include "StrUtil.h"

#include "HttpProto.h"

namespace tzhttpd {


// 在HttpExecutor中保留的Handler

class HttpParser;

typedef std::function<int (const HttpParser& http_parser, \
                           std::string& response, std::string& status_line, std::vector<std::string>& add_header)> HttpGetHandler;
typedef std::function<int (const HttpParser& http_parser, const std::string& post_data, \
                           std::string& response, std::string& status_line, std::vector<std::string>& add_header)> HttpPostHandler;

struct HttpHandlerObject {

    std::string            path_;
    boost::atomic<int64_t> success_count_;
    boost::atomic<int64_t> fail_count_;

    boost::atomic<bool>    built_in_;       // built_in handler，无需引用计数
    boost::atomic<bool>    working_;        //  启用，禁用等标记


    const enum HTTP_METHOD http_method_;    // 当前存储的METHOD
    HttpGetHandler         http_get_handler_;
    HttpPostHandler        http_post_handler_;

    HttpHandlerObject(const std::string& path,
                      const HttpGetHandler& get_handler,
                      bool built_in = false, bool working = true):
        path_(path),
        success_count_(0), fail_count_(0),
        built_in_(built_in), working_(working),
        http_method_(HTTP_METHOD::GET),
        http_get_handler_(get_handler) {
    }

    HttpHandlerObject(const std::string& path,
                      const HttpPostHandler& post_handler,
                      bool built_in = false, bool working = true):
        path_(path),
        success_count_(0), fail_count_(0),
        built_in_(built_in), working_(working),
        http_method_(HTTP_METHOD::POST),
        http_post_handler_(post_handler) {
    }

    bool check_basic_auth(const std::string& uri, const std::string auth_str) const;
};

typedef std::shared_ptr<HttpHandlerObject>  HttpHandlerObjectPtr;

} // tzhttpd


#endif // __TZHTTPD_HTTP_HANDLER_H__

