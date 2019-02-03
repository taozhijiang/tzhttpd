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
#include "TcpConnAsync.h"
#include "CheckPoint.h"

#include "Dispatcher.h"
#include "HttpReqInstance.h"

namespace tzhttpd {

namespace http_handler {
extern int default_http_get_handler(const HttpParser& http_parser,
                                    std::string& response, string& status, std::vector<std::string>& add_header);
} // end namespace http_handler

TcpConnAsync::TcpConnAsync(std::shared_ptr<ip::tcp::socket> p_socket,
                           HttpServer& server):
    ConnIf(p_socket),
    was_cancelled_(false),
    ops_cancel_mutex_(),
    ops_cancel_timer_(),
    http_server_(server),
    http_parser_(new HttpParser()),
    strand_(std::make_shared<io_service::strand>(server.io_service_)) {

    set_tcp_nodelay(true);
    set_tcp_nonblocking(true);
}

TcpConnAsync::~TcpConnAsync() {
    tzhttpd_log_debug("TcpConnAsync SOCKET RELEASED!!!");
}

void TcpConnAsync::start() override {

    set_conn_stat(ConnStat::kWorking);
    do_read_head();
}

void TcpConnAsync::stop() {
    set_conn_stat(ConnStat::kPending);
}

// Wrapping the handler with strand.wrap. This will return a new handler, that will dispatch through the strand.
// Posting or dispatching directly through the strand.

void TcpConnAsync::do_read_head() {

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
                                 std::bind(&TcpConnAsync::read_head_handler,
                                     shared_from_this(),
                                     std::placeholders::_1,
                                     std::placeholders::_2)));
    return;
}

void TcpConnAsync::read_head_handler(const boost::system::error_code& ec, size_t bytes_transferred) {

    revoke_ops_cancel_timeout();

    if (ec) {
        handle_socket_ec(ec);
        return;
    }

    SAFE_ASSERT(bytes_transferred > 0);

    std::string head_str (boost::asio::buffers_begin(request_.data()),
                          boost::asio::buffers_begin(request_.data()) + request_.size());

    request_.consume(bytes_transferred); // skip the already head

    if (!http_parser_->parse_request_header(head_str.c_str())) {
        tzhttpd_log_err( "Parse request error: %s", head_str.c_str());
        goto error_return;
    }

    // Header must already recv here, do the uri parse work,
    // And store the items in params
    if (!http_parser_->parse_request_uri()) {
        std::string uri = http_parser_->find_request_header(http_proto::header_options::request_uri);
        tzhttpd_log_err("Prase request uri failed: %s", uri.c_str());
        goto error_return;
    }

    if (http_parser_->get_method() == HTTP_METHOD::GET)
    {
        // HTTP GET handler
        SAFE_ASSERT(http_parser_->find_request_header(http_proto::header_options::content_length).empty());

        std::string real_path_info = http_parser_->find_request_header(http_proto::header_options::request_path_info);
        std::string vhost_name = StrUtil::drop_host_port(
                http_parser_->find_request_header(http_proto::header_options::host));

        std::shared_ptr<HttpReqInstance> http_req_instance
                    = std::make_shared<HttpReqInstance>(http_parser_->get_method(), shared_from_this(),
                                                        vhost_name, real_path_info,
                                                        http_parser_, "");
        Dispatcher::instance().handle_http_request(http_req_instance);

        // 再次开始读取请求，可以shared_from_this()保持住连接
        start();
        return;
    }
    else if (http_parser_->get_method() == HTTP_METHOD::POST )
    {

        size_t len = ::atoi(http_parser_->find_request_header(http_proto::header_options::content_length).c_str());
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

    }
    else
    {
        tzhttpd_log_err("Invalid or unsupport request method: %s",
                http_parser_->find_request_header(http_proto::header_options::request_method).c_str());
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

void TcpConnAsync::do_read_body() {

    if (get_conn_stat() != ConnStat::kWorking) {
        tzhttpd_log_err("Socket Status Error: %d", get_conn_stat());
        return;
    }

    if (was_ops_cancelled()) {
        return;
    }

    if (recv_bound_.length_hint_ == 0) {
        recv_bound_.length_hint_ = ::atoi(http_parser_->find_request_header(http_proto::header_options::content_length).c_str());
    }

    tzhttpd_log_debug("strand read async_read exactly... in thread %#lx", (long)pthread_self());

    size_t to_read = std::min(static_cast<size_t>(recv_bound_.length_hint_ - recv_bound_.buffer_.get_length()),
                              static_cast<size_t>(kFixedIoBufferSize));

    set_ops_cancel_timeout();
    async_read(*socket_, buffer(recv_bound_.io_block_, to_read),
                    boost::asio::transfer_at_least(to_read),
                             strand_->wrap(
                                 std::bind(&TcpConnAsync::read_body_handler,
                                     shared_from_this(),
                                     std::placeholders::_1,
                                     std::placeholders::_2)));
    return;
}


void TcpConnAsync::read_body_handler(const boost::system::error_code& ec, size_t bytes_transferred) {

    revoke_ops_cancel_timeout();

    if (ec) {
        handle_socket_ec(ec);
        return;
    }

    // 如果在读取HTTP头部的同时就将数据也读取出来了，这时候实际的
    // bytes_transferred == 0
    if (bytes_transferred > 0) {
        std::string additional(recv_bound_.io_block_, bytes_transferred);
        recv_bound_.buffer_.append_internal(additional);
    }

    if (recv_bound_.buffer_.get_length() < recv_bound_.length_hint_) {
        // need to read more, do again!
        do_read_body();
        return;
    }

    std::string real_path_info = http_parser_->find_request_header(http_proto::header_options::request_path_info);
    std::string vhost_name = StrUtil::drop_host_port(
                                      http_parser_->find_request_header(http_proto::header_options::host));

    std::string post_body;
    recv_bound_.buffer_.consume(post_body, recv_bound_.length_hint_);

    std::shared_ptr<HttpReqInstance> http_req_instance
                = std::make_shared<HttpReqInstance>(http_parser_->get_method(), shared_from_this(),
                                                    vhost_name, real_path_info,
                                                    http_parser_, post_body);


    Dispatcher::instance().handle_http_request(http_req_instance);

    // default, OK
    // go through write return;

    // If HTTP 1.0 or HTTP 1.1 without Keep-Alived, close the connection directly
    // Else, trigger the next generation read again!

    // 算了，强制一个读操作，从而可以引发其错误处理

    // 再次开始读取请求，可以shared_from_this()保持住连接
    start();

}


void TcpConnAsync::do_write() override {

    if (get_conn_stat() != ConnStat::kWorking) {
        tzhttpd_log_err("Socket Status Error: %d", get_conn_stat());
        return;
    }

    if(send_bound_.buffer_.get_length() == 0) {

        // 看是否关闭主动关闭连接
        if (!keep_continue()) {

            revoke_ops_cancel_timeout();
            ops_cancel();
            sock_shutdown_and_close(ShutdownType::kBoth);
        }

        return;
    }

    SAFE_ASSERT(send_bound_.buffer_.get_length() > 0);

    tzhttpd_log_debug("strand write async_write exactly... in thread thread %#lx", (long)pthread_self());

    size_t to_write = std::min(static_cast<size_t>(send_bound_.buffer_.get_length()),
                               static_cast<size_t>(kFixedIoBufferSize));

    send_bound_.buffer_.consume(send_bound_.io_block_, to_write);
    set_ops_cancel_timeout();
    async_write(*socket_, buffer(send_bound_.io_block_, to_write),
                    boost::asio::transfer_exactly(to_write),
                              strand_->wrap(
                                 std::bind(&TcpConnAsync::write_handler,
                                     shared_from_this(),
                                     std::placeholders::_1,
                                     std::placeholders::_2)));
    return;
}


void TcpConnAsync::write_handler(const boost::system::error_code& ec, size_t bytes_transferred) {

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


void TcpConnAsync::fill_http_for_send(const string& str, const string& status_line, const std::vector<std::string>& additional_header) {

    string content = http_proto::http_response_generate(str, status_line, keep_continue(), additional_header);
    send_bound_.buffer_.append_internal(content);

    std::string str_method = HTTP_METHOD_STRING(http_parser_->get_method());
    tzhttpd_log_info("\n =====> \"%s %s\" %s",
                     str_method.c_str(), http_parser_->get_uri().c_str(), status_line.c_str());

    return;
}


void TcpConnAsync::fill_std_http_for_send(enum http_proto::StatusCode code) {

    string http_ver = http_parser_->get_version();
    std::string status_line = generate_response_status_line(http_ver, code);
    string content = http_proto::http_std_response_generate(http_ver, status_line, keep_continue());

    send_bound_.buffer_.append_internal(content);

    std::string str_method = HTTP_METHOD_STRING(http_parser_->get_method());
    tzhttpd_log_info("\n =====> \"%s %s\" %s",
                     str_method.c_str(), http_parser_->get_uri().c_str(), status_line.c_str());

    return;
}


// http://www.boost.org/doc/libs/1_44_0/doc/html/boost_asio/reference/error__basic_errors.html
bool TcpConnAsync::handle_socket_ec(const boost::system::error_code& ec ) {

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
bool TcpConnAsync::keep_continue() {

    std::string connection =  http_parser_->find_request_header(http_proto::header_options::connection);
    if (!connection.empty()) {
        if (boost::iequals(connection, "Close")) {
            return false;
        } else if (boost::iequals(connection, "Keep-Alive")){
            return true;
        } else {
            tzhttpd_log_err("unknown connection value: %s", connection.c_str());
        }
    }

    if (http_parser_->get_version() > "1.0" ) {
        return true;
    }

    return false;
}

void TcpConnAsync::set_ops_cancel_timeout() {

    std::lock_guard<std::mutex> lock(ops_cancel_mutex_);

    if (http_server_.ops_cancel_time_out() == 0){
        SAFE_ASSERT(!ops_cancel_timer_);
        return;
    }

    ops_cancel_timer_.reset( new boost::asio::deadline_timer (http_server_.io_service_,
                                      boost::posix_time::seconds(http_server_.ops_cancel_time_out())) );
    SAFE_ASSERT(http_server_.ops_cancel_time_out());
    ops_cancel_timer_->async_wait(std::bind(&TcpConnAsync::ops_cancel_timeout_call, shared_from_this(),
                                           std::placeholders::_1));
    tzhttpd_log_debug("register ops_cancel_time_out %d sec", http_server_.ops_cancel_time_out());
}

void TcpConnAsync::revoke_ops_cancel_timeout() {

    std::lock_guard<std::mutex> lock(ops_cancel_mutex_);

    boost::system::error_code ignore_ec;
    if (ops_cancel_timer_) {
        ops_cancel_timer_->cancel(ignore_ec);
        ops_cancel_timer_.reset();
    }
}

void TcpConnAsync::ops_cancel_timeout_call(const boost::system::error_code& ec) {

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
