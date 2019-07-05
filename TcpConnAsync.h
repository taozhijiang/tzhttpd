/*-
 * Copyright (c) 2018-2019 TAO Zhijiang<taozhijiang@gmail.com>
 *
 * Licensed under the BSD-3-Clause license, see LICENSE for full information.
 *
 */

#ifndef __TZHTTPD_TCP_CONN_ASYNC_H__
#define __TZHTTPD_TCP_CONN_ASYNC_H__

#include <xtra_rhel.h>

#include <boost/asio.hpp>
#include <boost/atomic/atomic.hpp>

#include "ConnIf.h"
#include "HttpParser.h"

#include <boost/chrono.hpp>
#include <boost/asio/steady_timer.hpp>
using boost::asio::steady_timer;

namespace tzhttpd {

class TcpConnAsync;
class HttpReqInstance;

class HttpServer;
class TcpConnAsync : public ConnIf,
    public std::enable_shared_from_this<TcpConnAsync> {

    __noncopyable__(TcpConnAsync)
    friend class HttpReqInstance;

public:

    // 当前并发连接数目
    static boost::atomic<int32_t> current_concurrency_;

    /// Construct a connection with the given socket.
    TcpConnAsync(std::shared_ptr<boost::asio::ip::tcp::socket> p_socket, HttpServer& server);
    virtual ~TcpConnAsync();

    virtual void start();
    void stop();

    // http://www.boost.org/doc/libs/1_44_0/doc/html/boost_asio/reference/error__basic_errors.html
    bool handle_socket_ec(const boost::system::error_code& ec);

private:

    virtual bool do_read()override { SAFE_ASSERT(false);
        return false;}
    virtual void read_handler(const boost::system::error_code& ec, std::size_t bytes_transferred)override {
        SAFE_ASSERT(false);
    }

    virtual bool do_write()override { SAFE_ASSERT(false);
        return false;}
    virtual void write_handler(const boost::system::error_code& ec, std::size_t bytes_transferred)override {
        SAFE_ASSERT(false);
    }

    void do_read_head();
    void read_head_handler(const boost::system::error_code& ec, std::size_t bytes_transferred);

    void do_read_body(std::shared_ptr<HttpParser> http_parser);
    void read_body_handler(std::shared_ptr<HttpParser> http_parser,
                           const boost::system::error_code& ec, std::size_t bytes_transferred);

    bool do_write(std::shared_ptr<HttpParser> http_parser);
    // std::bind无法使用重载函数，所以这里另起函数名
    void self_write_handler(std::shared_ptr<HttpParser> http_parser,
                            const boost::system::error_code& ec, std::size_t bytes_transferred);

    void set_session_cancel_timeout();
    void revoke_session_cancel_timeout();
    void set_ops_cancel_timeout();
    void revoke_ops_cancel_timeout();
    bool was_ops_cancelled() {
        std::lock_guard<std::mutex> lock(ops_cancel_mutex_);
        return was_cancelled_;
    }

    bool ops_cancel() {
        std::lock_guard<std::mutex> lock(ops_cancel_mutex_);
        sock_cancel();
        set_conn_stat(ConnStat::kError);
        was_cancelled_ = true;
        return was_cancelled_;
    }
    void ops_cancel_timeout_call(const boost::system::error_code& ec);

    // 是否Connection长连接
    bool keep_continue(const std::shared_ptr<HttpParser>& http_parser);

    void fill_http_for_send(std::shared_ptr<HttpParser> http_parser,
                            const char* data, size_t len, const std::string& status) {
        SAFE_ASSERT(data && len);
        std::string msg(data, len);
        fill_http_for_send(http_parser, msg, status, { });
    }

    void fill_http_for_send(std::shared_ptr<HttpParser> http_parser,
                            const std::string& str, const std::string& status) {
        fill_http_for_send(http_parser, str, status, { });
    }

    void fill_http_for_send(std::shared_ptr<HttpParser> http_parser,
                            const char* data, size_t len, const std::string& status,
                            const std::vector<std::string>& additional_header) {
        SAFE_ASSERT(data && len);
        std::string msg(data, len);
        return fill_http_for_send(http_parser, msg, status, additional_header);
    }

    void fill_http_for_send(std::shared_ptr<HttpParser> http_parser,
                            const std::string& str, const std::string& status,
                            const std::vector<std::string>& additional_header);

    // 标准的HTTP响应头和响应体
    void fill_std_http_for_send(std::shared_ptr<HttpParser> http_parser,
                                enum http_proto::StatusCode code);

private:

    // 用于读取HTTP的头部使用
    boost::asio::streambuf request_;   // client request_ read

    IOBound recv_bound_;
    IOBound send_bound_;

    bool was_cancelled_;
    std::mutex ops_cancel_mutex_;

    // IO操作的最大时长
    std::unique_ptr<steady_timer> ops_cancel_timer_;

    // 会话间隔的最大时长
    std::unique_ptr<steady_timer> session_cancel_timer_;

private:

    HttpServer& http_server_;

    // Of course, the handlers may still execute concurrently with other handlers that
    // were not dispatched through an boost::asio::strand, or were dispatched through
    // a different boost::asio::strand object.

    // Where there is a single chain of asynchronous operations associated with a
    // connection (e.g. in a half duplex protocol implementation like HTTP) there
    // is no possibility of concurrent execution of the handlers. This is an implicit strand.

    // Strand to ensure the connection's handlers are not called concurrently. ???
    std::shared_ptr<boost::asio::io_service::strand> strand_;

};

} // end namespace tzhttpd

#endif //__TZHTTPD_TCP_CONN_ASYNC_H__
