/*-
 * Copyright (c) 2018-2019 TAO Zhijiang<taozhijiang@gmail.com>
 *
 * Licensed under the BSD-3-Clause license, see LICENSE for full information.
 *
 */

#ifndef __TZHTTPD_HTTP_HANDLER_H__
#define __TZHTTPD_HTTP_HANDLER_H__

#include <functional>

#include <xtra_rhel.h>

#include <libconfig.h++>

#include <boost/thread/locks.hpp>

#include "HttpProto.h"

namespace tzhttpd {


// 在HttpExecutor中保留的Handler

class HttpParser;

typedef std::function<int(const HttpParser& http_parser,\
                              std::string& response, std::string& status_line, std::vector<std::string>& add_header)> HttpGetHandler;
typedef std::function<int(const HttpParser& http_parser, const std::string& post_data,\
                              std::string& response, std::string& status_line, std::vector<std::string>& add_header)> HttpPostHandler;

struct HttpHandlerObject {

    const std::string   path_;

    int32_t             success_count_;
    int32_t             fail_count_;

    bool                built_in_;       // built_in handler,无法被卸载更新


    HttpGetHandler      http_get_handler_;
    HttpPostHandler     http_post_handler_;

    HttpHandlerObject(const std::string& path,
                      const HttpGetHandler& get_handler,
                      bool built_in = false) :
        path_(path),
        success_count_(0), fail_count_(0),
        built_in_(built_in),
        http_get_handler_(get_handler) {
    }

    HttpHandlerObject(const std::string& path,
                      const HttpPostHandler& post_handler,
                      bool built_in = false) :
        path_(path),
        success_count_(0), fail_count_(0),
        built_in_(built_in),
        http_post_handler_(post_handler) {
    }

    HttpHandlerObject(const std::string& path,
                      const HttpGetHandler& get_handler,
                      const HttpPostHandler& post_handler,
                      bool built_in = false) :
        path_(path),
        success_count_(0), fail_count_(0),
        built_in_(built_in),
        http_get_handler_(get_handler),
        http_post_handler_(post_handler) {
    }

    void update_get_handler(const HttpGetHandler& get_handler) {
        http_get_handler_ = get_handler;
    }

    void update_post_handler(const HttpPostHandler& post_handler) {
        http_post_handler_ = post_handler;
    }
};

typedef std::shared_ptr<HttpHandlerObject>  HttpHandlerObjectPtr;

} // end namespace tzhttpd


#endif // __TZHTTPD_HTTP_HANDLER_H__

