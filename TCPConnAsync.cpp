/*-
 * Copyright (c) 2018 TAO Zhijiang<taozhijiang@gmail.com>
 *
 * Licensed under the BSD-3-Clause license, see LICENSE for full information.
 *
 */

#include <thread>
#include <functional>

#include <boost/algorithm/string.hpp>

#include "HttpServer.h"
#include "HttpProto.h"
#include "TCPConnAsync.h"

namespace tzhttpd {

namespace http_handler {
extern int default_http_get_handler(const HttpParser& http_parser, std::string& response, string& status);
} // end namespace http_handler

TCPConnAsync::TCPConnAsync(std::shared_ptr<ip::tcp::socket> p_socket,
                           HttpServer& server):
    ConnIf(p_socket),
    was_cancelled_(false),
    ops_cancel_mutex_(),
    ops_cancel_timer_(),
    http_server_(server),
    http_parser_(),
    strand_(std::make_shared<io_service::strand>(server.io_service_)) {

    set_tcp_nodelay(true);

    r_size_ = 0;
    w_size_ = 0;
    w_pos_  = 0;

    set_tcp_nonblocking(true);

    // 可以被后续resize增加
    p_buffer_ = std::make_shared<std::vector<char> >(16*1024, 0);
    p_write_  = std::make_shared<std::vector<char> >(16*1024, 0);

}

TCPConnAsync::~TCPConnAsync() {
    http_server_.conn_drop(this);
    tzhttpd_log_debug("TCPConnAsync SOCKET RELEASED!!!");
}

void TCPConnAsync::start() override {

    set_conn_stat(ConnStat::kConnWorking);
    r_size_ = w_size_ = w_pos_ = 0;

    do_read_head();
}

void TCPConnAsync::stop() {
    set_conn_stat(ConnStat::kConnPending);
}

// Wrapping the handler with strand.wrap. This will return a new handler, that will dispatch through the strand.
// Posting or dispatching directly through the strand.

void TCPConnAsync::do_read_head() {

    if (get_conn_stat() != ConnStat::kConnWorking) {
        tzhttpd_log_err("Socket Status Error: %d", get_conn_stat());
        return;
    }

    if (was_ops_cancelled()) {
        return;
    }

    tzhttpd_log_debug("strand read read_until ... in thread %#lx", (long)pthread_self());

    set_ops_cancel_timeout();
    async_read_until(*sock_ptr_, request_,
                        http_proto::header_crlfcrlf_str,
                             strand_->wrap(
                                 std::bind(&TCPConnAsync::read_head_handler,
                                     shared_from_this(),
                                     std::placeholders::_1,
                                     std::placeholders::_2)));
    return;
}

void TCPConnAsync::read_head_handler(const boost::system::error_code& ec, size_t bytes_transferred) {

    http_server_.conn_touch(shared_from_this());
    revoke_ops_cancel_timeout();

    if (ec) {
        handle_socket_ec(ec);
        return;
    }

    SAFE_ASSERT(bytes_transferred > 0);

    std::string head_str (boost::asio::buffers_begin(request_.data()),
                            boost::asio::buffers_begin(request_.data()) + request_.size());

    request_.consume(bytes_transferred); // skip the already head

    if (!http_parser_.parse_request_header(head_str.c_str())) {
        tzhttpd_log_err( "Parse request error: %s", head_str.c_str());
        goto error_return;
    }

    // Header must already recv here, do the uri parse work,
    // And store the items in params
    if (!http_parser_.parse_request_uri()) {
        std::string uri = http_parser_.find_request_header(http_proto::header_options::request_uri);
        tzhttpd_log_err("Prase request uri failed: %s", uri.c_str());
        goto error_return;
    }

    if (http_parser_.get_method() == HTTP_METHOD::GET) {

        // HTTP GET handler
        SAFE_ASSERT(http_parser_.find_request_header(http_proto::header_options::content_length).empty());

        std::string real_path_info = http_parser_.find_request_header(http_proto::header_options::request_path_info);
        HttpGetHandler handler;
        std::string response_body;
        std::string response_status;

        if (http_server_.find_http_get_handler(real_path_info, handler) != 0){
            tzhttpd_log_err("uri %s handler not found, using default handler!", real_path_info.c_str());
            handler = http_handler::default_http_get_handler;
        } else if(!handler) {
            tzhttpd_log_err("real_path_info %s found, but handler empty!", real_path_info.c_str());
            fill_std_http_for_send(http_proto::StatusCode::client_error_bad_request);
            goto write_return;
        }

        handler(http_parser_, response_body, response_status); // just call it!
        if (response_body.empty() || response_status.empty()) {
            tzhttpd_log_err("caller not generate response body!");  // default status OK
            fill_std_http_for_send(http_proto::StatusCode::success_ok);
        } else {
            fill_http_for_send(response_body, response_status);
        }

        goto write_return;

    } else if (http_parser_.get_method() == HTTP_METHOD::POST ) {

        // HTTP POST handler

        size_t len = ::atoi(http_parser_.find_request_header(http_proto::header_options::content_length).c_str());
        r_size_ = 0;
        size_t additional_size = request_.size(); // net additional body size

        SAFE_ASSERT( additional_size <= len );
        if (len + 1 > p_buffer_->size()) {
            tzhttpd_log_info( "relarge receive buffer size to: %d", (len + 256));
            p_buffer_->resize(len + 256);
        }

        // first async_read_until may read more additional data, if so
        // then move additional data possible
        if( additional_size ) {

            std::string additional (boost::asio::buffers_begin(request_.data()),
                      boost::asio::buffers_begin(request_.data()) + additional_size);

            memcpy(p_buffer_->data(), additional.c_str(), additional_size + 1);
            r_size_ = additional_size;
            request_.consume(additional_size); // skip the head part
        }

        // normally, we will return these 2 cases
        if (additional_size < len) {
            // need to read more data here, write to r_size_

            // if cancel, abort following ops
            if (was_ops_cancelled()) {
                handle_socket_ec(ec);
                return;
            }

            do_read_body();
        }
        else {
            // call the process callback directly
            read_body_handler(ec, 0);   // already updated r_size_
        }

        return;

    } else {
        tzhttpd_log_err("Invalid or unsupport request method: %s",
                http_parser_.find_request_header(http_proto::header_options::request_method).c_str());
        fill_std_http_for_send(http_proto::StatusCode::client_error_bad_request);
        goto write_return;
    }

error_return:
    fill_std_http_for_send(http_proto::StatusCode::server_error_internal_server_error);
    request_.consume(request_.size());
    r_size_ = 0;

write_return:
    do_write();

    // If HTTP 1.0 or HTTP 1.1 without Keep-Alived, close the connection directly
    // Else, trigger the next generation read again!

    // 算了，强制一个读操作，从而可以引发其错误处理
    if (keep_continue()) {
        return start();
    }
}

void TCPConnAsync::do_read_body() {

    if (get_conn_stat() != ConnStat::kConnWorking) {
        tzhttpd_log_err("Socket Status Error: %d", get_conn_stat());
        return;
    }

    if (was_ops_cancelled()) {
        return;
    }

    size_t len = ::atoi(http_parser_.find_request_header(http_proto::header_options::content_length).c_str());

    tzhttpd_log_debug("strand read async_read exactly... in thread %#lx", (long)pthread_self());

    set_ops_cancel_timeout();
    async_read(*sock_ptr_, buffer(p_buffer_->data() + r_size_, len - r_size_),
                    boost::asio::transfer_at_least(len - r_size_),
                             strand_->wrap(
                                 std::bind(&TCPConnAsync::read_body_handler,
                                     shared_from_this(),
                                     std::placeholders::_1,
                                     std::placeholders::_2)));
    return;
}


void TCPConnAsync::read_body_handler(const boost::system::error_code& ec, size_t bytes_transferred) {

    http_server_.conn_touch(shared_from_this());
    revoke_ops_cancel_timeout();

    if (ec) {
        handle_socket_ec(ec);
        return;
    }

    size_t len = ::atoi(http_parser_.find_request_header(http_proto::header_options::content_length).c_str());
    r_size_ += bytes_transferred;
    if (r_size_ < len) {
        // need to read more, do again!
        do_read_body();
        return;
    }

    std::string real_path_info = http_parser_.find_request_header(http_proto::header_options::request_path_info);
    HttpPostHandler handler;
    std::string response_body;
    std::string response_status;
    if (http_server_.find_http_post_handler(real_path_info, handler) != 0){
        tzhttpd_log_err("uri %s handler not found, and no default!", real_path_info.c_str());
        fill_std_http_for_send(http_proto::StatusCode::client_error_not_found);
    } else {
        if (handler) {
            handler(http_parser_, std::string(p_buffer_->data(), r_size_), response_body, response_status); // call it!
            if (response_body.empty() || response_status.empty()) {
                tzhttpd_log_err("caller not generate response body!");
                fill_std_http_for_send(http_proto::StatusCode::success_ok);
            } else {
                fill_http_for_send(response_body, response_status);
            }
        } else {
            tzhttpd_log_err("real_path_info %s found, but handler empty!", real_path_info.c_str());
            fill_std_http_for_send(http_proto::StatusCode::client_error_bad_request);
        }
    }

    // default, OK
    // go through write return;

 // write_return:
    do_write();

    // If HTTP 1.0 or HTTP 1.1 without Keep-Alived, close the connection directly
    // Else, trigger the next generation read again!

    // 算了，强制一个读操作，从而可以引发其错误处理
    if (keep_continue()) {
        return start();
    }
}


void TCPConnAsync::do_write() override {

    if (get_conn_stat() != ConnStat::kConnWorking) {
        tzhttpd_log_err("Socket Status Error: %d", get_conn_stat());
        return;
    }

    SAFE_ASSERT(w_size_);
    SAFE_ASSERT(w_pos_ < w_size_);

    tzhttpd_log_debug("strand write async_write exactly... in thread thread %#lx", (long)pthread_self());

    set_ops_cancel_timeout();
    async_write(*sock_ptr_, buffer(p_write_->data() + w_pos_, w_size_ - w_pos_),
                    boost::asio::transfer_at_least(w_size_ - w_pos_),
                              strand_->wrap(
                                 std::bind(&TCPConnAsync::write_handler,
                                     shared_from_this(),
                                     std::placeholders::_1,
                                     std::placeholders::_2)));
    return;
}


void TCPConnAsync::write_handler(const boost::system::error_code& ec, size_t bytes_transferred) {

    http_server_.conn_touch(shared_from_this());
    revoke_ops_cancel_timeout();

    if (ec) {
        handle_socket_ec(ec);
        return;
    }

    SAFE_ASSERT(bytes_transferred > 0);

    w_pos_ += bytes_transferred;

    if (w_pos_ < w_size_) {

        if (was_ops_cancelled()) {
            handle_socket_ec(ec);
            return;
        }

        tzhttpd_log_debug("need additional write operation: %lu ~ %lu", w_pos_, w_size_);
        do_write();

    } else {

        // reset
        w_pos_ = w_size_ = 0;

    }

}


void TCPConnAsync::fill_http_for_send(const string& str, const string& status_line, const std::vector<std::string>& additional_header) {

    string content = http_proto::http_response_generate(str, status_line, keep_continue(), additional_header);
    if (content.size() + 1 > p_write_->size())
        p_write_->resize(content.size() + 1);

    ::memcpy(p_write_->data(), content.c_str(), content.size() + 1); // copy '\0' but not transform it

    w_size_ = content.size();
    w_pos_  = 0;

    return;
}


void TCPConnAsync::fill_std_http_for_send(enum http_proto::StatusCode code) {

    string http_ver = http_parser_.get_version();
    string content = http_proto::http_std_response_generate(http_ver, code, keep_continue());
    if (content.size() + 1 > p_write_->size())
        p_write_->resize(content.size() + 1);

    ::memcpy(p_write_->data(), content.c_str(), content.size() + 1); // copy '\0' but not transform it

    w_size_ = content.size();
    w_pos_  = 0;

    return;
}


// http://www.boost.org/doc/libs/1_44_0/doc/html/boost_asio/reference/error__basic_errors.html
bool TCPConnAsync::handle_socket_ec(const boost::system::error_code& ec ) {

    boost::system::error_code ignore_ec;
    bool close_socket = false;

    if (ec == boost::asio::error::eof) {
        tzhttpd_log_alert("Peer closed up ...");
        close_socket = true;
    } else if (ec == boost::asio::error::connection_reset) {
        tzhttpd_log_alert("Connection reset by peer ...");
        close_socket = true;
    } else if (ec == boost::asio::error::operation_aborted) {
        tzhttpd_log_alert("Operation aborted(cancel) ..."); // like timer ...
    } else if (ec == boost::asio::error::bad_descriptor) {
        tzhttpd_log_alert("Bad file descriptor ...");
        close_socket = true;
    } else if (ec == boost::asio::error::timed_out) {
        tzhttpd_log_alert("Connection timed out ...");
        close_socket = true;
    } else {
        tzhttpd_log_alert("Undetected error %d, %s ...", ec, ec.message().c_str());
        close_socket = true;
    }


    if (close_socket || was_ops_cancelled()) {
        revoke_ops_cancel_timeout();
        ops_cancel();
        sock_shutdown(ShutdownType::kShutdownBoth);
        sock_close();
    }

    return close_socket;
}

// 测试方法 while:; do echo -e "GET / HTTP/1.1\nhost: test.domain\n\n"; sleep 3; done | telnet 127.0.0.1 8899
bool TCPConnAsync::keep_continue() {

    std::string connection =  http_parser_.find_request_header(http_proto::header_options::connection);
    if (!connection.empty()) {
        if (boost::iequals(connection, "Close")) {
            return false;
        } else if (boost::iequals(connection, "Keep-Alive")){
            return true;
        } else {
            tzhttpd_log_err("unknown connection value: %s", connection.c_str());
        }
    }

    if (http_parser_.get_version() > "1.0" ) {
        return true;
    }

    return false;
}

void TCPConnAsync::set_ops_cancel_timeout() {

    std::lock_guard<std::mutex> lock(ops_cancel_mutex_);

    if (http_server_.ops_cancel_time_out() == 0){
        SAFE_ASSERT(!ops_cancel_timer_);
        return;
    }

    ops_cancel_timer_.reset( new boost::asio::deadline_timer (http_server_.io_service_,
                                      boost::posix_time::seconds(http_server_.ops_cancel_time_out())) );
    SAFE_ASSERT(http_server_.ops_cancel_time_out());
    ops_cancel_timer_->async_wait(std::bind(&TCPConnAsync::ops_cancel_timeout_call, shared_from_this(),
                                           std::placeholders::_1));
    tzhttpd_log_debug("register ops_cancel_time_out %d sec", http_server_.ops_cancel_time_out());
}

void TCPConnAsync::revoke_ops_cancel_timeout() {

    std::lock_guard<std::mutex> lock(ops_cancel_mutex_);

    boost::system::error_code ignore_ec;
    if (ops_cancel_timer_) {
        ops_cancel_timer_->cancel(ignore_ec);
        ops_cancel_timer_.reset();
    }
}

void TCPConnAsync::ops_cancel_timeout_call(const boost::system::error_code& ec) {

    if (ec == 0){
        tzhttpd_log_info("ops_cancel_timeout_call called with timeout: %d", http_server_.ops_cancel_time_out());
        ops_cancel();
        sock_shutdown(ShutdownType::kShutdownBoth);
        sock_close();
    } else if ( ec == boost::asio::error::operation_aborted) {
        // normal cancel
    } else {
        tzhttpd_log_debug("ops_cancel_timeout_call called with unknow error %d ...", ec);
    }
}


} // end namespace tzhttpd
