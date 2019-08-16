/*-
 * Copyright (c) 2018-2019 TAO Zhijiang<taozhijiang@gmail.com>
 *
 * Licensed under the BSD-3-Clause license, see LICENSE for full information.
 *
 */

#include <xtra_rhel.h>

#include <thread>
#include <functional>


#include <string/StrUtil.h>

#include "HttpParser.h"
#include "HttpConf.h"
#include "HttpServer.h"
#include "HttpProto.h"
#include "TcpConnAsync.h"

#include "Dispatcher.h"
#include "HttpReqInstance.h"


namespace tzhttpd {

namespace http_handler {
extern int default_http_get_handler(const HttpParser& http_parser,
                                    std::string& response, std::string& status, std::vector<std::string>& add_header);
} // end namespace http_handler

boost::atomic<int32_t> TcpConnAsync::current_concurrency_(0);

TcpConnAsync::TcpConnAsync(std::shared_ptr<boost::asio::ip::tcp::socket> socket,
                           HttpServer& server) :
    ConnIf(socket),
    was_cancelled_(false),
    ops_cancel_mutex_(),
    ops_cancel_timer_(),
    session_cancel_timer_(),
    http_server_(server),
    strand_(std::make_shared<boost::asio::io_service::strand>(server.io_service())) {

    set_tcp_nodelay(true);
    set_tcp_nonblocking(true);

    ++current_concurrency_;
}

TcpConnAsync::~TcpConnAsync() {

    --current_concurrency_;
    // roo::log_info("TcpConnAsync SOCKET RELEASED!!!");
}

void TcpConnAsync::start() {

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
        roo::log_err("Socket Status Error: %d", get_conn_stat());
        return;
    }

    if (was_ops_cancelled()) {
        return;
    }

    // roo::log_info("strand read read_until ... in thread %#lx", (long)pthread_self());

    set_session_cancel_timeout();
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

    revoke_session_cancel_timeout();

    if (ec) {
        handle_socket_ec(ec);
        return;
    }


    boost::system::error_code call_ec;
    SAFE_ASSERT(bytes_transferred > 0);

    std::string head_str(boost::asio::buffers_begin(request_.data()),
                         boost::asio::buffers_begin(request_.data()) + request_.size());

    request_.consume(bytes_transferred); // skip the already head


    auto http_parser = std::make_shared<HttpParser>();
    if (!http_parser) {
        roo::log_err("Create HttpParser object failed.");
        goto error_return;
    }

    if (!http_parser->parse_request_header(head_str.c_str())) {
        roo::log_err("Parse request error: %s", head_str.c_str());
        goto error_return;
    }

    // 保存远程客户端信息
    http_parser->remote_ = socket_->remote_endpoint(call_ec);
    if (call_ec) {
        roo::log_err("Request remote address failed.");
        goto error_return;
    }

    // Header must already recv here, do the uri parse work,
    // And store the items in params
    if (!http_parser->parse_request_uri()) {
        std::string uri = http_parser->find_request_header(http_proto::header_options::request_uri);
        roo::log_err("Prase request uri failed: %s", uri.c_str());
        goto error_return;
    }

    if (http_parser->get_method() == HTTP_METHOD::GET ||
        http_parser->get_method() == HTTP_METHOD::OPTIONS) {
        // HTTP GET handler
        SAFE_ASSERT(http_parser->find_request_header(http_proto::header_options::content_length).empty());

        std::string real_path_info = http_parser->find_request_header(http_proto::header_options::request_path_info);
        std::string vhost_name = roo::StrUtil::drop_host_port(
            http_parser->find_request_header(http_proto::header_options::host));

        std::shared_ptr<HttpReqInstance> http_req_instance
            = std::make_shared<HttpReqInstance>(http_parser->get_method(), shared_from_this(),
                                                vhost_name, real_path_info,
                                                http_parser, "");
        Dispatcher::instance().handle_http_request(http_req_instance);

        // 再次开始读取请求，可以shared_from_this()保持住连接
        //
        // 我们不支持pipeline流水线机制，所以根据HTTP的鸟性，及时是长连接，客户端的下
        // 一次请求过来，也是等到服务端发送完请求后才会有数据
        //
        start();
        return;
    } else if (http_parser->get_method() == HTTP_METHOD::POST) {

        size_t len = ::atoi(http_parser->find_request_header(http_proto::header_options::content_length).c_str());
        recv_bound_.length_hint_ = len;  // 登记需要读取的长度
        size_t additional_size = request_.size(); // net additional body size

        SAFE_ASSERT(additional_size <= len);

        // first async_read_until may read more additional data, if so
        // then move additional data possible
        if (additional_size) {

            std::string additional(boost::asio::buffers_begin(request_.data()),
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

            do_read_body(http_parser);
        } else {
            // call the process callback directly
            read_body_handler(http_parser, ec, 0);   // already updated r_size_
        }

        return;

    } else {
        roo::log_err("Invalid or unsupport request method: %s",
                     http_parser->find_request_header(http_proto::header_options::request_method).c_str());
        fill_std_http_for_send(http_parser, http_proto::StatusCode::client_error_bad_request);
        goto write_return;
    }

 error_return:
    fill_std_http_for_send(http_parser, http_proto::StatusCode::server_error_internal_server_error);
    request_.consume(request_.size());

 write_return:

    // If HTTP 1.0 or HTTP 1.1 without Keep-Alived, close the connection directly
    // Else, trigger the next generation read again!

    // Bug:
    // 因为异步写也会触发定时器的设置和取消，所以这里的读定时器会被误取消导致永久阻塞
    // 解决的方式：
    // (1)将超时器分开设置
    //
    // 在 do_write()中启动读操作不合理，见do_write()的注释
    // 在这个路径返回的，基本都是异常情况导致的错误返回，此时根据情况看是否发起请求
    // 读操作必须在写操作之前，否则可能会导致引用计数消失

    do_write(http_parser);

    if (keep_continue(http_parser)) {
        start();
    }
}

void TcpConnAsync::do_read_body(std::shared_ptr<HttpParser> http_parser) {

    if (get_conn_stat() != ConnStat::kWorking) {
        roo::log_err("Socket Status Error: %d", get_conn_stat());
        return;
    }

    if (was_ops_cancelled()) {
        return;
    }

    if (recv_bound_.length_hint_ == 0) {
        recv_bound_.length_hint_ = ::atoi(http_parser->find_request_header(http_proto::header_options::content_length).c_str());
    }

    size_t to_read = std::min(static_cast<size_t>(recv_bound_.length_hint_ - recv_bound_.buffer_.get_length()),
                              static_cast<size_t>(kFixedIoBufferSize));

    // roo::log_info("strand read async_read exactly(%lu)... in thread %#lx",
    //              to_read, (long)pthread_self());

    set_ops_cancel_timeout();
    async_read(*socket_, boost::asio::buffer(recv_bound_.io_block_, to_read),
               boost::asio::transfer_at_least(to_read),
               strand_->wrap(
                   std::bind(&TcpConnAsync::read_body_handler,
                             shared_from_this(),
                             http_parser,
                             std::placeholders::_1,
                             std::placeholders::_2)));
    return;
}


void TcpConnAsync::read_body_handler(std::shared_ptr<HttpParser> http_parser,
                                     const boost::system::error_code& ec, size_t bytes_transferred) {

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
        do_read_body(http_parser);
        return;
    }

    std::string real_path_info = http_parser->find_request_header(http_proto::header_options::request_path_info);
    std::string vhost_name = roo::StrUtil::drop_host_port(
        http_parser->find_request_header(http_proto::header_options::host));

    std::string post_body;
    recv_bound_.buffer_.consume(post_body, recv_bound_.length_hint_);

    std::shared_ptr<HttpReqInstance> http_req_instance
        = std::make_shared<HttpReqInstance>(http_parser->get_method(), shared_from_this(),
                                            vhost_name, real_path_info,
                                            http_parser, post_body);


    Dispatcher::instance().handle_http_request(http_req_instance);

    // default, OK
    // go through write return;

    // If HTTP 1.0 or HTTP 1.1 without Keep-Alived, close the connection directly
    // Else, trigger the next generation read again!

    // 算了，强制一个读操作，从而可以引发其错误处理

    // 这里是POST方法，而GET方法在读完头部后就直接再次发起读操作了
    // 再次开始读取请求，可以shared_from_this()保持住连接
    start();
}

bool TcpConnAsync::do_write(std::shared_ptr<HttpParser> http_parser) {

    if (get_conn_stat() != ConnStat::kWorking) {
        roo::log_err("Socket Status Error: %d", get_conn_stat());
        return false;
    }

    if (send_bound_.buffer_.get_length() == 0) {

        // 看是否关闭主动关闭连接
        // 因为在读取请求的时候默认就当作长连接发起再次读了，所以如果这里检测到是
        // 短连接，就采取主动关闭操作，否则就让之前的长连接假设继续生效
        if (!keep_continue(http_parser)) {

            revoke_ops_cancel_timeout();
            ops_cancel();
            sock_shutdown_and_close(ShutdownType::kBoth);
        }

        return true;
    }

    SAFE_ASSERT(send_bound_.buffer_.get_length() > 0);

    size_t to_write = std::min(static_cast<size_t>(send_bound_.buffer_.get_length()),
                               static_cast<size_t>(kFixedIoBufferSize));

    // roo::log_info("strand write async_write exactly (%lu)... in thread thread %#lx",
    //              to_write, (long)pthread_self());

    send_bound_.buffer_.consume(send_bound_.io_block_, to_write);

    set_ops_cancel_timeout();
    async_write(*socket_, boost::asio::buffer(send_bound_.io_block_, to_write),
                boost::asio::transfer_exactly(to_write),
                strand_->wrap(
                    std::bind(&TcpConnAsync::self_write_handler,
                              shared_from_this(),
                              http_parser,
                              std::placeholders::_1,
                              std::placeholders::_2)));
    return true;
}


void TcpConnAsync::self_write_handler(std::shared_ptr<HttpParser> http_parser,
                                      const boost::system::error_code& ec, size_t bytes_transferred) {

    revoke_ops_cancel_timeout();

    if (ec) {
        handle_socket_ec(ec);
        return;
    }

    SAFE_ASSERT(bytes_transferred > 0);

    //
    // 再次触发写，如果为空就直接返回
    // 函数中会检查，如果内容为空，就直接返回不执行写操作


    // Bug 这里会有竞争条件 !!!
    // 当使用http客户端长连接的时候，下面的do_write()如果没有数据会触发
    // keep_continue()调用，而该实现是遍历http_parser的head来实现的，
    // 但是此时可能在write_handler调用之前或之中就触发了客户端新的head解析，导致
    // 该调用访问http_parser会产生问题

    do_write(http_parser);
}


void TcpConnAsync::fill_http_for_send(std::shared_ptr<HttpParser> http_parser,
                                      const std::string& str, const std::string& status_line,
                                      const std::vector<std::string>& additional_header) {

    bool keep_next = false;
    std::string str_uri = "UNDETECTED_URI";
    std::string str_method = "UNDETECTED_METHOD";

    if (http_parser) {
        keep_next = keep_continue(http_parser);
        str_uri = http_parser->get_uri();
        str_method = HTTP_METHOD_STRING(http_parser->get_method());
    }

    std::string content = http_proto::http_response_generate(str, status_line, keep_next, additional_header);
    send_bound_.buffer_.append_internal(content);

    roo::log_warning("\n =====> \"%s %s\" %s",
                     str_method.c_str(), str_uri.c_str(), status_line.c_str());

    return;
}


void TcpConnAsync::fill_std_http_for_send(std::shared_ptr<HttpParser> http_parser,
                                          enum http_proto::StatusCode code) {

    bool keep_next = false;
    std::string http_ver = "HTTP/1.1";
    std::string str_uri = "UNDETECTED_URI";
    std::string str_method = "UNDETECTED_METHOD";

    if (http_parser) {
        keep_next = keep_continue(http_parser);
        http_ver = http_parser->get_version();
        str_uri = http_parser->get_uri();
        str_method = HTTP_METHOD_STRING(http_parser->get_method());
    }


    std::string status_line = generate_response_status_line(http_ver, code);
    std::string content =
        http_proto::http_std_response_generate(http_ver, status_line, code, keep_next);

    send_bound_.buffer_.append_internal(content);

    roo::log_warning("\n =====> \"%s %s\" %s",
                     str_method.c_str(), str_uri.c_str(), status_line.c_str());

    return;
}


// http://www.boost.org/doc/libs/1_44_0/doc/html/boost_asio/reference/error__basic_errors.html
bool TcpConnAsync::handle_socket_ec(const boost::system::error_code& ec) {

    boost::system::error_code ignore_ec;
    bool close_socket = false;

    if (ec == boost::asio::error::eof ||
        ec == boost::asio::error::connection_reset ||
        ec == boost::asio::error::timed_out ||
        ec == boost::asio::error::bad_descriptor) {
        roo::log_err("error_code: {%d} %s", ec.value(), ec.message().c_str());
        close_socket = true;
    } else if (ec == boost::asio::error::operation_aborted) {
        // like itimeout trigger
        roo::log_err("error_code: {%d} %s", ec.value(), ec.message().c_str());
    } else {
        roo::log_err("Undetected error %d, %s ...", ec.value(), ec.message().c_str());
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
bool TcpConnAsync::keep_continue(const std::shared_ptr<HttpParser>& http_parser) {

    if (!http_parser)
        return false;

    std::string connection =  http_parser->find_request_header(http_proto::header_options::connection);
    if (!connection.empty()) {
        if (boost::iequals(connection, "Close")) {
            return false;
        } else if (boost::iequals(connection, "Keep-Alive")) {
            return true;
        } else {
            roo::log_err("unknown connection value: %s", connection.c_str());
        }
    }

    if (http_parser->get_version() > "1.0") {
        return true;
    }

    return false;
}

void TcpConnAsync::set_session_cancel_timeout() {

    std::lock_guard<std::mutex> lock(ops_cancel_mutex_);

    if (http_server_.session_cancel_time_out() == 0) {
        SAFE_ASSERT(!session_cancel_timer_);
        return;
    }

    // cancel the already timer first if any
    boost::system::error_code ignore_ec;
    if (session_cancel_timer_) {
        session_cancel_timer_->cancel(ignore_ec);
    } else {
        session_cancel_timer_.reset(new steady_timer(http_server_.io_service()));
    }

    SAFE_ASSERT(http_server_.session_cancel_time_out());
    session_cancel_timer_->expires_from_now(seconds(http_server_.session_cancel_time_out()));
    session_cancel_timer_->async_wait(std::bind(&TcpConnAsync::ops_cancel_timeout_call, shared_from_this(),
                                                std::placeholders::_1));
    roo::log_info("register session_cancel_time_out %d sec", http_server_.session_cancel_time_out());
}

void TcpConnAsync::revoke_session_cancel_timeout() {

    std::lock_guard<std::mutex> lock(ops_cancel_mutex_);

    boost::system::error_code ignore_ec;
    if (session_cancel_timer_) {
        session_cancel_timer_->cancel(ignore_ec);
    }
}

void TcpConnAsync::set_ops_cancel_timeout() {

    std::lock_guard<std::mutex> lock(ops_cancel_mutex_);

    if (http_server_.ops_cancel_time_out() == 0) {
        SAFE_ASSERT(!ops_cancel_timer_);
        return;
    }

    // cancel the already timer first if any
    boost::system::error_code ignore_ec;
    if (ops_cancel_timer_) {
        ops_cancel_timer_->cancel(ignore_ec);
    } else {
        ops_cancel_timer_.reset(new steady_timer(http_server_.io_service()));
    }

    SAFE_ASSERT(http_server_.ops_cancel_time_out());
    ops_cancel_timer_->expires_from_now(seconds(http_server_.ops_cancel_time_out()));
    ops_cancel_timer_->async_wait(std::bind(&TcpConnAsync::ops_cancel_timeout_call, shared_from_this(),
                                            std::placeholders::_1));
    roo::log_info("register ops_cancel_time_out %d sec", http_server_.ops_cancel_time_out());
}

void TcpConnAsync::revoke_ops_cancel_timeout() {

    std::lock_guard<std::mutex> lock(ops_cancel_mutex_);

    boost::system::error_code ignore_ec;
    if (ops_cancel_timer_) {
        ops_cancel_timer_->cancel(ignore_ec);
    }
}

void TcpConnAsync::ops_cancel_timeout_call(const boost::system::error_code& ec) {

    if (ec == 0) {
        roo::log_warning("ops_cancel_timeout_call called with timeout: %d", http_server_.ops_cancel_time_out());
        ops_cancel();
        sock_shutdown_and_close(ShutdownType::kBoth);
    } else if (ec == boost::asio::error::operation_aborted) {
        // normal cancel
    } else {
        roo::log_err("unknown and won't handle error_code: {%d} %s", ec.value(), ec.message().c_str());
    }
}


} // end namespace tzhttpd
