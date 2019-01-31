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
#include "CheckPoint.h"

namespace tzhttpd {

namespace http_handler {
extern int default_http_get_handler(const HttpParser& http_parser,
                                    std::string& response, string& status, std::vector<std::string>& add_header);
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
    set_tcp_nonblocking(true);
}

TCPConnAsync::~TCPConnAsync() {
    http_server_.conn_drop(this);
    tzhttpd_log_debug("TCPConnAsync SOCKET RELEASED!!!");
}

void TCPConnAsync::start() override {

    set_conn_stat(ConnStat::kWorking);
    do_read_head();
}

void TCPConnAsync::stop() {
    set_conn_stat(ConnStat::kPending);
}

// Wrapping the handler with strand.wrap. This will return a new handler, that will dispatch through the strand.
// Posting or dispatching directly through the strand.

void TCPConnAsync::do_read_head() {

    if (get_conn_stat() != ConnStat::kWorking) {
        tzhttpd_log_err("Socket Status Error: %d", get_conn_stat());
        return;
    }

    if (was_ops_cancelled()) {
        return;
    }

    tzhttpd_log_debug("strand read read_until ... in thread %#lx", (long)pthread_self());

    set_ops_cancel_timeout();
    async_read_until(*socket_, request_,
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
        std::string vhost_name = StrUtil::drop_host_port(
                                    http_parser_.find_request_header(http_proto::header_options::host));
        HttpGetHandlerObjectPtr phandler_obj {};
        std::string response_body;
        std::string response_status;
        std::vector<std::string> response_header;

        if (http_server_.find_http_get_handler(vhost_name, real_path_info, phandler_obj) != 0){
            tzhttpd_log_err("uri %s handler not found!", real_path_info.c_str());
            fill_std_http_for_send(http_proto::StatusCode::client_error_bad_request);
            goto write_return;
        }

        if(!phandler_obj) {
            tzhttpd_log_err("real_path_info %s found, but handler empty!", real_path_info.c_str());
            fill_std_http_for_send(http_proto::StatusCode::client_error_bad_request);
            goto write_return;
        }

        if (!phandler_obj->working_) {
            tzhttpd_log_err("get handler for %s is disabled ...", real_path_info.c_str());
            fill_std_http_for_send(http_proto::StatusCode::server_error_service_unavailable);
            goto write_return;
        }

        if (!phandler_obj->check_basic_auth(real_path_info,
                                            http_parser_.find_request_header(http_proto::header_options::auth))) {
            tzhttpd_log_err("basic_auth for %s failed ...", real_path_info.c_str());
            fill_std_http_for_send(http_proto::StatusCode::client_error_unauthorized);
            goto write_return;
        }

        {
            std::string key = "GET_" + phandler_obj->path_;
            CountPerfByMs call_perf { key };

            // just call it!
            int call_code = phandler_obj->handler_(http_parser_, response_body, response_status, response_header);
            if (call_code == 0) {
                ++ phandler_obj->success_cnt_;
            } else {
                ++ phandler_obj->fail_cnt_;
                call_perf.set_error();
            }

            if (response_status.empty()) {
                if(call_code == 0) {
                    tzhttpd_log_notice("response_status empty, call_code == 0, default success.");
                    fill_std_http_for_send(http_proto::StatusCode::success_ok);
                } else {
                    tzhttpd_log_notice("response_status empty, call_code == %d, default error.", call_code);
                    fill_std_http_for_send(http_proto::StatusCode::server_error_internal_server_error);
                }
            } else {
                fill_http_for_send(response_body, response_status, response_header);
            }
        }


        goto write_return;

    } else if (http_parser_.get_method() == HTTP_METHOD::POST ) {

        // HTTP POST handler

        size_t len = ::atoi(http_parser_.find_request_header(http_proto::header_options::content_length).c_str());
        recv_bound_.length_hint_ = len;  // 登记需要读取的长度
        size_t additional_size = request_.size(); // net additional body size

        SAFE_ASSERT( additional_size <= len );

        // first async_read_until may read more additional data, if so
        // then move additional data possible
        if( additional_size ) {

            std::string additional (boost::asio::buffers_begin(request_.data()),
                                    boost::asio::buffers_begin(request_.data()) + additional_size);
            recv_bound_.buffer_.append_internal(additional);
            request_.consume(additional_size);
        }

        // normally, we will return these 2 cases
        if (additional_size < len) {
            // need to read more data here,

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

    if (get_conn_stat() != ConnStat::kWorking) {
        tzhttpd_log_err("Socket Status Error: %d", get_conn_stat());
        return;
    }

    if (was_ops_cancelled()) {
        return;
    }

    if (recv_bound_.length_hint_ == 0) {
        recv_bound_.length_hint_ = ::atoi(http_parser_.find_request_header(http_proto::header_options::content_length).c_str());
    }

    tzhttpd_log_debug("strand read async_read exactly... in thread %#lx", (long)pthread_self());

    size_t to_read = std::min(static_cast<size_t>(recv_bound_.length_hint_ - recv_bound_.buffer_.get_length()),
                              static_cast<size_t>(kFixedIoBufferSize));

    set_ops_cancel_timeout();
    async_read(*socket_, buffer(recv_bound_.io_block_, to_read),
                    boost::asio::transfer_at_least(to_read),
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

    SAFE_ASSERT(bytes_transferred > 0);

    std::string additional(recv_bound_.io_block_, bytes_transferred);
    recv_bound_.buffer_.append_internal(additional);

    if (recv_bound_.buffer_.get_length() < recv_bound_.length_hint_) {
        // need to read more, do again!
        do_read_body();
        return;
    }

    std::string real_path_info = http_parser_.find_request_header(http_proto::header_options::request_path_info);
    std::string vhost_name = StrUtil::drop_host_port(
                                        http_parser_.find_request_header(http_proto::header_options::host));
    HttpPostHandlerObjectPtr phandler_obj {};
    std::string response_body;
    std::string response_status;
    std::vector<std::string> response_header;

    if (http_server_.find_http_post_handler(vhost_name, real_path_info, phandler_obj) != 0){
        tzhttpd_log_err("uri %s handler not found, and no default!", real_path_info.c_str());
        fill_std_http_for_send(http_proto::StatusCode::client_error_not_found);
    } else {
        if (phandler_obj) {

            if (!phandler_obj->working_) {
                tzhttpd_log_err("post handler for %s is disabled ...", real_path_info.c_str());
                fill_std_http_for_send(http_proto::StatusCode::server_error_service_unavailable);
                goto write_return;
            }

            if (!phandler_obj->check_basic_auth(real_path_info,
                                                http_parser_.find_request_header(http_proto::header_options::auth))) {
                tzhttpd_log_err("basic_auth for %s failed ...", real_path_info.c_str());
                fill_std_http_for_send(http_proto::StatusCode::client_error_unauthorized);
                goto write_return;
            }

            {
                std::string key = "POST_" + phandler_obj->path_;
                CountPerfByMs call_perf { key };

                // just call it!
                std::string post_body;
                recv_bound_.buffer_.consume(post_body, recv_bound_.length_hint_);
                int call_code = phandler_obj->handler_(http_parser_,
                                                       post_body, response_body,
                                                       response_status, response_header); // call it!
                if (call_code == 0) {
                    ++ phandler_obj->success_cnt_;
                } else {
                    ++ phandler_obj->fail_cnt_;
                    call_perf.set_error();
                }

                if (response_status.empty()) {
                    if(call_code == 0) {
                        tzhttpd_log_notice("response_status empty, call_code == 0, default success.");
                        fill_std_http_for_send(http_proto::StatusCode::success_ok);
                    } else {
                        tzhttpd_log_notice("response_status empty, call_code == %d, default error.", call_code);
                        fill_std_http_for_send(http_proto::StatusCode::server_error_internal_server_error);
                    }
                } else {
                    fill_http_for_send(response_body, response_status, response_header);
                }

            }

        } else {
            tzhttpd_log_err("real_path_info %s found, but handler empty!", real_path_info.c_str());
            fill_std_http_for_send(http_proto::StatusCode::client_error_bad_request);
        }
    }

    // default, OK
    // go through write return;

write_return:
    do_write();

    // If HTTP 1.0 or HTTP 1.1 without Keep-Alived, close the connection directly
    // Else, trigger the next generation read again!

    // 算了，强制一个读操作，从而可以引发其错误处理
    if (keep_continue()) {
        return start();
    }
}


void TCPConnAsync::do_write() override {

    if (get_conn_stat() != ConnStat::kWorking) {
        tzhttpd_log_err("Socket Status Error: %d", get_conn_stat());
        return;
    }

    if(send_bound_.buffer_.get_length() == 0)
        return;

    SAFE_ASSERT(send_bound_.buffer_.get_length() > 0);

    tzhttpd_log_debug("strand write async_write exactly... in thread thread %#lx", (long)pthread_self());

    size_t to_write = std::min(static_cast<size_t>(send_bound_.buffer_.get_length()),
                               static_cast<size_t>(kFixedIoBufferSize));

    send_bound_.buffer_.consume(send_bound_.io_block_, to_write);
    set_ops_cancel_timeout();
    async_write(*socket_, buffer(send_bound_.io_block_, to_write),
                    boost::asio::transfer_exactly(to_write),
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

    //
    // 再次触发写，如果为空就直接返回
    // 函数中会检查，如果内容为空，就直接返回不执行写操作

    do_write();
}


void TCPConnAsync::fill_http_for_send(const string& str, const string& status_line, const std::vector<std::string>& additional_header) {

    string content = http_proto::http_response_generate(str, status_line, keep_continue(), additional_header);
    send_bound_.buffer_.append_internal(content);

    std::string str_method = "UNKNOWN";
    auto method = http_parser_.get_method();
    if (method == HTTP_METHOD::GET) {
        str_method = "GET";
    } else if(method == HTTP_METHOD::POST) {
        str_method = "POST";
    }
    tzhttpd_log_info("\n =====> \"%s %s\" %s",
                     str_method.c_str(), http_parser_.get_uri().c_str(), status_line.c_str());

    return;
}


void TCPConnAsync::fill_std_http_for_send(enum http_proto::StatusCode code) {

    string http_ver = http_parser_.get_version();
    std::string status_line = generate_response_status_line(http_ver, code);
    string content = http_proto::http_std_response_generate(http_ver, status_line, keep_continue());

    send_bound_.buffer_.append_internal(content);

    std::string str_method = "UNKNOWN";
    auto method = http_parser_.get_method();
    if (method == HTTP_METHOD::GET) {
        str_method = "GET";
    } else if(method == HTTP_METHOD::POST) {
        str_method = "POST";
    }
    tzhttpd_log_info("\n =====> \"%s %s\" %s",
                     str_method.c_str(), http_parser_.get_uri().c_str(), status_line.c_str());

    return;
}


// http://www.boost.org/doc/libs/1_44_0/doc/html/boost_asio/reference/error__basic_errors.html
bool TCPConnAsync::handle_socket_ec(const boost::system::error_code& ec ) {

    boost::system::error_code ignore_ec;
    bool close_socket = false;

    if (ec == boost::asio::error::eof ||
        ec == boost::asio::error::connection_reset ||
        ec == boost::asio::error::timed_out ||
        ec == boost::asio::error::bad_descriptor ) {
        tzhttpd_log_err("error_code: {%d} %s", ec.value(), ec.message().c_str());
        close_socket = true;
    } else if (ec == boost::asio::error::operation_aborted) {
        // like itimeout trigger
        tzhttpd_log_err("error_code: {%d} %s", ec.value(), ec.message().c_str());
    } else {
        tzhttpd_log_err("Undetected error %d, %s ...", ec.value(), ec.message().c_str());
        close_socket = true;
    }


    if (close_socket || was_ops_cancelled()) {
        revoke_ops_cancel_timeout();
        ops_cancel();
        sock_shutdown_and_close(ShutdownType::kBoth);
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
        sock_shutdown_and_close(ShutdownType::kBoth);
    } else if ( ec == boost::asio::error::operation_aborted) {
        // normal cancel
    } else {
        tzhttpd_log_err("unknown and won't handle error_code: {%d} %s", ec.value(), ec.message().c_str());
    }
}


} // end namespace tzhttpd
