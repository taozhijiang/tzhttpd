/*-
 * Copyright (c) 2018-2019 TAO Zhijiang<taozhijiang@gmail.com>
 *
 * Licensed under the BSD-3-Clause license, see LICENSE for full information.
 *
 */

#ifndef __TZHTTPD_HTTP_SERVER_H__
#define __TZHTTPD_HTTP_SERVER_H__

#include <boost/asio/steady_timer.hpp>
using boost::asio::steady_timer;


#include <boost/asio.hpp>

#include <libconfig.h++>

#include <set>
#include <map>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <algorithm>

#include <other/Log.h>
#include <container/EQueue.h>
#include <concurrency/ThreadPool.h>

#include <scaffold/Status.h>
#include <scaffold/Setting.h>

#include "HttpHandler.h"

namespace tzhttpd {

class TcpConnAsync;

typedef TcpConnAsync ConnType;
typedef std::shared_ptr<ConnType> ConnTypePtr;

class HttpServer;
class HttpConf {

    friend class HttpServer;

    bool        service_enabled_;   // 服务开关
    int32_t     service_speed_;

    int32_t     service_token_;

    int32_t     service_concurrency_;       // 最大连接并发控制

    int32_t     session_cancel_time_out_;    // session间隔会话时长
    int32_t     ops_cancel_time_out_;        // ops操作超时时长

    // 加载、更新配置的时候保护竞争状态
    // 这里保护主要是非atomic的原子结构
    std::mutex             lock_;
    std::set<std::string>  safe_ip_;

    std::string    bind_addr_;
    int32_t        bind_port_;

    int32_t        backlog_size_;
    int32_t        io_thread_number_;


    bool load_conf(std::shared_ptr<libconfig::Config> conf_ptr);
    bool load_conf(const libconfig::Config& conf);


    bool check_safe_ip(const std::string& ip) {
        std::lock_guard<std::mutex> lock(lock_);
        return (safe_ip_.empty() || (safe_ip_.find(ip) != safe_ip_.cend()));
    }

    bool get_http_service_token() {

        // 注意：
        // 如果关闭这个选项，则整个服务都不可用了(包括管理页面)
        // 此时如果需要变更除非重启服务，或者采用非web方式(比如发送命令)来恢复配置

        if (!service_enabled_) {
            roo::log_warning("http_service not enabled ...");
            return false;
        }

        // 下面就不使用锁来保证严格的一致性了，因为非关键参数，过多的锁会影响性能
        if (service_speed_ == 0) // 没有限流
            return true;

        if (service_token_ <= 0) {
            roo::log_warning("http_service not speed over ...");
            return false;
        }

        --service_token_;
        return true;
    }

    void withdraw_http_service_token() {    // 支持将令牌还回去
        ++service_token_;
    }

    void feed_http_service_token() {
        service_token_ = service_speed_;
    }

    std::shared_ptr<steady_timer> timed_feed_token_;
    void timed_feed_token_handler(const boost::system::error_code& ec);

    // 默认初始化良好的数据
    HttpConf() :
        service_enabled_(true),
        service_speed_(0),
        service_token_(0),
        service_concurrency_(0),
        session_cancel_time_out_(0),
        ops_cancel_time_out_(0),
        lock_(),
        safe_ip_({ }),
        bind_addr_(),
        bind_port_(0),
        backlog_size_(0),
        io_thread_number_(0) {
    }

} __attribute__((aligned(4)));  // end class HttpConf


typedef std::shared_ptr<boost::asio::ip::tcp::socket>    SocketPtr;


class HttpServer : public std::enable_shared_from_this<HttpServer> {

    friend class TcpConnAsync;  // can not work with typedef, ugly ...

public:

    // PUBLIC API CALL HERE

    /// Construct the server to listen on the specified TCP address and port
    explicit HttpServer(const std::string& cfgfile, const std::string& instance_name);
    ~HttpServer() { };

    HttpServer(const HttpServer&) = delete;
    HttpServer& operator=(const HttpServer&) = delete;

    bool init();

    void service();

    int add_http_vhost(const std::string& hostname);

    int add_http_get_handler(const std::string& uri_regex, const HttpGetHandler& handler,
                             bool built_in = false, const std::string hostname = "");
    int add_http_post_handler(const std::string& uri_regex, const HttpPostHandler& handler,
                              bool built_in = false, const std::string hostname = "");

    int register_http_status_callback(const std::string& name, roo::StatusCallable func);
    int register_http_runtime_callback(const std::string& name, roo::SettingUpdateCallable func);
    int update_http_runtime_conf();

private:
    const std::string instance_name_;
    boost::asio::io_service io_service_;

    // 侦听地址信息
    boost::asio::ip::tcp::endpoint ep_;
    std::unique_ptr<boost::asio::ip::tcp::acceptor> acceptor_;

    const std::string cfgfile_;
    HttpConf conf_;

    void do_accept();
    void accept_handler(const boost::system::error_code& ec, SocketPtr ptr);

public:

    int ops_cancel_time_out() const {
        return conf_.ops_cancel_time_out_;
    }

    int session_cancel_time_out() const {
        return conf_.session_cancel_time_out_;
    }

public:
    roo::ThreadPool io_service_threads_;
    void io_service_run(roo::ThreadObjPtr ptr);  // main task loop
    int io_service_stop_graceful();
    int io_service_join();

public:
    int module_runtime(const libconfig::Config& conf);
    int module_status(std::string& strModule, std::string& strKey, std::string& strValue);
};


} // end namespace tzhttpd

#endif //__TZHTTPD_HTTP_SERVER_H__
