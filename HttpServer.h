#ifndef __TZHTTPD_HTTP_SERVER_H__
#define __TZHTTPD_HTTP_SERVER_H__

#include <set>
#include <map>
#include <mutex>
#include <condition_variable>
#include <functional>

#include <boost/noncopyable.hpp>

#include "EQueue.h"
#include "ThreadPool.h"
#include "AliveTimer.h"

#include "TCPConnAsync.h"
#include "HttpHandler.h"

#include "HttpParser.h"

namespace tzhttpd {

typedef TCPConnAsync ConnType;
typedef std::shared_ptr<ConnType> ConnTypePtr;
typedef std::weak_ptr<ConnType>   ConnTypeWeakPtr;

class HttpServer;
class HttpConf {

    friend class HttpServer;

private:
    std::string              docu_root_;
    std::vector<std::string> docu_index_;

    int conn_time_out_;
    int conn_time_out_linger_;

    int ops_cancel_time_out_;  // sec 会话超时自动取消ops

    std::mutex        lock_;
    bool              http_service_enabled_;  // 服务开关
    int64_t           http_service_speed_;
    volatile int64_t  http_service_token_;

    bool get_http_service_token() {
        std::lock_guard<std::mutex> lock(lock_);
        if (http_service_speed_ == 0) // 没有限流
            return true;

        if (http_service_token_ <= 0)
            return false;

        -- http_service_token_;
        return true;
    }

    void withdraw_http_service_token() {    // 支持将令牌还回去
        std::lock_guard<std::mutex> lock(lock_);
        ++ http_service_token_;
    }

    void feed_http_service_token(){
        std::lock_guard<std::mutex> lock(lock_);
        http_service_token_ = http_service_speed_;
    }
};

class HttpServer : public boost::noncopyable,
                   public std::enable_shared_from_this<HttpServer> {

    friend class TCPConnAsync;  // can not work with typedef, ugly ...

public:

    /// Construct the server to listen on the specified TCP address and port
    HttpServer(const std::string& address, unsigned short port, size_t t_size);
    bool init();
    void service();

private:
    io_service io_service_;

    // 侦听地址信息
    ip::tcp::endpoint ep_;
    std::unique_ptr<ip::tcp::acceptor> acceptor_;

    HttpConf conf_;

    void do_accept();
    void accept_handler(const boost::system::error_code& ec, SocketPtr ptr);

    std::map<std::string, HttpPostHandler> http_post_handler_;
    std::map<std::string, HttpGetHandler>  http_get_handler_;

    AliveTimer<ConnType>    conns_alive_;

public:
    int register_http_post_handler(std::string uri, HttpPostHandler handler);
    int find_http_post_handler(std::string uri, HttpPostHandler& handler);

    int register_http_get_handler(std::string uri, HttpGetHandler handler);
    int find_http_get_handler(std::string uri, HttpGetHandler& handler);

    const std::string& document_root() const {
        return conf_.docu_root_;
    }

    const std::vector<std::string>& document_index() const {
        return conf_.docu_index_;
    }

    int ops_cancel_time_out() const {
        return conf_.ops_cancel_time_out_;
    }

    int conn_add(ConnTypePtr p_conn) {
        conns_alive_.INSERT(p_conn);
        return 0;
    }

    void conn_touch(ConnTypePtr p_conn) {
        conns_alive_.TOUCH(p_conn);
    }

    void conn_drop(ConnTypePtr p_conn) {
        conns_alive_.DROP(p_conn);
    }

    void conn_drop(ConnType* ptr) {
        conns_alive_.DROP(ptr);
    }

    int conn_destroy(ConnTypePtr p_conn) {
        p_conn->sock_shutdown(ShutdownType::kShutdownBoth);
        p_conn->sock_close();
        return 0;
    }


public:
    ThreadPool io_service_threads_;
    void io_service_run(ThreadObjPtr ptr);  // main task loop
    int io_service_stop_graceful();
    int io_service_join();
};


} // end namespace tzhttpd

#endif //__TZHTTPD_HTTP_SERVER_H__
