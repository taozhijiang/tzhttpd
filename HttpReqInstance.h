/*-
 * Copyright (c) 2018-2019 TAO Zhijiang<taozhijiang@gmail.com>
 *
 * Licensed under the BSD-3-Clause license, see LICENSE for full information.
 *
 */

#ifndef __TZHTTPD_HTTP_REQ_INSTANCE_H__
#define __TZHTTPD_HTTP_REQ_INSTANCE_H__

#include <memory>

#include "Buffer.h"
#include "TcpConnAsync.h"

#include "HttpProto.h"

namespace tzhttpd {

struct HttpReqInstance {
    HttpReqInstance(enum HTTP_METHOD method,
                    std::shared_ptr<TcpConnAsync> socket,
                    const std::string& hostname,
                    const std::string& uri,
                    std::shared_ptr<HttpParser> http_parser,
                    const std::string& data) :
        method_(method),
        hostname_(hostname),
        uri_(uri),
        http_parser_(http_parser),
        data_(data),
        start_(::time(NULL)),
        full_socket_(socket) {
    }

    const HTTP_METHOD method_;
    const std::string hostname_;
    const std::string uri_;
    std::shared_ptr<HttpParser> http_parser_;  // move here
    std::string data_;        // post data, 如果有的话

    time_t start_;            // 请求创建的时间
    std::weak_ptr<TcpConnAsync> full_socket_; // 可能socket提前在网络层已经释放了


    std::string str() {
        std::stringstream ss;

        ss << "HTTP: ";
        ss << "method:" << HTTP_METHOD_STRING(method_) << ", ";
        ss << "hostname:" << hostname_ << ", ";
        ss << "uri:" << uri_ << ", ";
        ss << "data:" << data_;

        return ss.str();
    }

    void http_std_response(enum http_proto::StatusCode code) {

        if (auto sock = full_socket_.lock()) {
            sock->fill_std_http_for_send(http_parser_, code);
            sock->do_write(http_parser_);
            return;
        }

        roo::log_err("connection already released before.");
    }

    void http_response(const std::string& response_str,
                       const std::string& status_str,
                       const std::vector<std::string>& headers) {

        if (auto sock = full_socket_.lock()) {
            sock->fill_http_for_send(http_parser_, response_str, status_str, headers);
            sock->do_write(http_parser_);
            return;
        }

        roo::log_err("connection already released before.");
    }
};

} // tzhttpd


#endif // __TZHTTPD_HTTP_REQ_INSTANCE_H__
