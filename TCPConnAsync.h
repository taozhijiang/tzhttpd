#ifndef __TZHTTPD_TCP_CONN_ASYNC_H__
#define __TZHTTPD_TCP_CONN_ASYNC_H__

#include <boost/date_time/posix_time/posix_time.hpp>
using namespace boost::posix_time;
using namespace boost::gregorian;

#include <boost/noncopyable.hpp>

#include "ConnIf.h"
#include "HttpParser.h"

namespace tzhttpd {

class TCPConnAsync;
typedef std::shared_ptr<TCPConnAsync> NetConnPtr;
typedef std::weak_ptr<TCPConnAsync>   NetConnWeakPtr;


class HttpServer;
class TCPConnAsync: public ConnIf, public boost::noncopyable,
                    public std::enable_shared_from_this<TCPConnAsync> {

public:

    /// Construct a connection with the given socket.
    TCPConnAsync(std::shared_ptr<ip::tcp::socket> p_socket, HttpServer& server);
    virtual ~TCPConnAsync();

    virtual void start();
    void stop();

    // http://www.boost.org/doc/libs/1_44_0/doc/html/boost_asio/reference/error__basic_errors.html
    bool handle_socket_ec(const boost::system::error_code& ec);

private:

    virtual void do_read() override { SAFE_ASSERT(false); }
    virtual void read_handler(const boost::system::error_code& ec, std::size_t bytes_transferred) override {
        SAFE_ASSERT(false);
    }

    virtual void do_write();
    virtual void write_handler(const boost::system::error_code &ec, std::size_t bytes_transferred);

    void do_read_head();
    void read_head_handler(const boost::system::error_code &ec, std::size_t bytes_transferred);

    void do_read_body();
    void read_body_handler(const boost::system::error_code &ec, std::size_t bytes_transferred);

    void set_ops_cancel_timeout();
    void revoke_ops_cancel_timeout();
    bool was_ops_cancelled() {
        std::lock_guard<std::mutex> lock(ops_cancel_mutex_);
        return was_cancelled_;
    }

    bool ops_cancel() {
        std::lock_guard<std::mutex> lock(ops_cancel_mutex_);
        sock_cancel();
        set_conn_stat(ConnStat::kConnError);
        was_cancelled_ = true;
        return was_cancelled_;
    }
    void ops_cancel_timeout_call(const boost::system::error_code& ec);

    // 是否Connection长连接
    bool keep_continue();

    void fill_http_for_send(const char* data, size_t len, const string& status);
    void fill_http_for_send(const string& str, const string& status);

    // 标准的HTTP响应头和响应体
    void fill_std_http_for_send(enum http_proto::StatusCode code);

private:

    // 用于读取HTTP的头部使用
    boost::asio::streambuf request_;   // client request_ read

    bool was_cancelled_;
    std::mutex ops_cancel_mutex_;
    std::unique_ptr<boost::asio::deadline_timer> ops_cancel_timer_;

private:

    HttpServer& http_server_;
    HttpParser http_parser_;

    // Of course, the handlers may still execute concurrently with other handlers that
    // were not dispatched through an boost::asio::strand, or were dispatched through
    // a different boost::asio::strand object.

    // Where there is a single chain of asynchronous operations associated with a
    // connection (e.g. in a half duplex protocol implementation like HTTP) there
    // is no possibility of concurrent execution of the handlers. This is an implicit strand.

    // Strand to ensure the connection's handlers are not called concurrently. ???
    std::shared_ptr<io_service::strand> strand_;

private:
    // 读写的有效负载记录
    size_t r_size_; // 读取开始写的位置

    size_t w_size_; // 有效负载的末尾
    size_t w_pos_;  // 写可能会一次不能完全发送，这里保存已写的位置

    std::shared_ptr<std::vector<char> > p_buffer_;
    std::shared_ptr<std::vector<char> > p_write_;

    bool send_buff_empty() {
        return (w_size_ == 0 || (w_pos_ >= w_size_) );
    }
};

} // end namespace tzhttpd

#endif //__TZHTTPD_TCP_CONN_ASYNC_H__
