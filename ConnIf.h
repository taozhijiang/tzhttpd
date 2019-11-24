/*-
 * Copyright (c) 2018-2019 TAO Zhijiang<taozhijiang@gmail.com>
 *
 * Licensed under the BSD-3-Clause license, see LICENSE for full information.
 *
 */

#ifndef __TZHTTPD_CONN_IF_H__
#define __TZHTTPD_CONN_IF_H__

#include <mutex>
#include "Buffer.h"

#include <boost/system/error_code.hpp>
#include <boost/asio.hpp>

namespace tzhttpd {

enum class ConnStat : uint8_t {
    kWorking = 1,
    kPending = 2,
    kError   = 3,
    kClosed  = 4,
};

enum class ShutdownType : uint8_t {
    kSend = 1,
    kRecv = 2,
    kBoth = 3,
};

class ConnIf {

public:

    /// Construct a connection with the given socket.
    explicit ConnIf(std::shared_ptr<boost::asio::ip::tcp::socket> sock) :
        conn_stat_(ConnStat::kPending),
        socket_(sock) {
        set_tcp_nonblocking(false);
    }

    virtual ~ConnIf() = default;

public:

    // read write 接口也可能回用到同步操作中去，所以返回bool
    virtual bool do_read() = 0;
    virtual void read_handler(const boost::system::error_code& ec, std::size_t bytes_transferred) = 0;

    virtual bool do_write() = 0;
    virtual void write_handler(const boost::system::error_code& ec, std::size_t bytes_transferred) = 0;


    // some general tiny settings function

    bool set_tcp_nonblocking(bool set_value) {

        boost::system::error_code ignore_ec;

        //boost::asio::socket_base::non_blocking_io command(set_value);
        socket_->non_blocking(set_value, ignore_ec);

        return true;
    }

    bool set_tcp_nodelay(bool set_value) {

        boost::system::error_code ignore_ec;

        boost::asio::ip::tcp::no_delay nodelay(set_value);
        socket_->set_option(nodelay, ignore_ec);
        boost::asio::ip::tcp::no_delay option;
        socket_->get_option(option, ignore_ec);

        return (option.value() == set_value);
    }

    bool set_tcp_keepalive(bool set_value) {

        boost::system::error_code ignore_ec;

        boost::asio::socket_base::keep_alive keepalive(set_value);
        socket_->set_option(keepalive, ignore_ec);
        boost::asio::socket_base::keep_alive option;
        socket_->get_option(option, ignore_ec);

        return (option.value() == set_value);
    }

    void sock_shutdown_and_close(enum ShutdownType s) {

        std::lock_guard<std::mutex> lock(conn_mutex_);
        if (conn_stat_ == ConnStat::kClosed)
            return;

        boost::system::error_code ignore_ec;
        if (s == ShutdownType::kSend) {
            socket_->shutdown(boost::asio::socket_base::shutdown_send, ignore_ec);
        } else if (s == ShutdownType::kRecv) {
            socket_->shutdown(boost::asio::socket_base::shutdown_receive, ignore_ec);
        } else if (s == ShutdownType::kBoth) {
            socket_->shutdown(boost::asio::socket_base::shutdown_both, ignore_ec);
        }

        socket_->close(ignore_ec);
        conn_stat_ = ConnStat::kClosed;
    }

    void sock_cancel() {

        std::lock_guard<std::mutex> lock(conn_mutex_);

        boost::system::error_code ignore_ec;
        socket_->cancel(ignore_ec);
    }

    void sock_close() {

        std::lock_guard<std::mutex> lock(conn_mutex_);
        if (conn_stat_ == ConnStat::kClosed)
            return;

        boost::system::error_code ignore_ec;
        socket_->close(ignore_ec);
        conn_stat_ = ConnStat::kClosed;
    }

    enum ConnStat get_conn_stat() const { return conn_stat_; }
    void set_conn_stat(enum ConnStat stat) { conn_stat_ = stat; }

private:
    std::mutex conn_mutex_;
    enum ConnStat conn_stat_;

protected:
    std::shared_ptr<boost::asio::ip::tcp::socket> socket_;
};

// 固定的发送、接收缓冲区大小
const static uint32_t kFixedIoBufferSize = 2048;

struct IOBound {
    IOBound() :
        io_block_({ }),
        length_hint_({ 0 }),
        buffer_() {
    }

    char io_block_[kFixedIoBufferSize];     // 读写操作的固定缓存
    size_t length_hint_;
    Buffer buffer_;                         // 已经传输字节
};
} // end namespace tzhttpd

#endif //__TZHTTPD_CONN_IF_H__
