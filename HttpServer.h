/*-
 * Copyright (c) 2018-2019 TAO Zhijiang<taozhijiang@gmail.com>
 *
 * Licensed under the BSD-3-Clause license, see LICENSE for full information.
 *
 */

#ifndef __TZHTTPD_HTTP_SERVER_H__
#define __TZHTTPD_HTTP_SERVER_H__

#include <xtra_asio.h>

#include <libconfig.h++>

#include <set>
#include <map>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <algorithm>

#include <boost/noncopyable.hpp>
#include <boost/atomic/atomic.hpp>

#include "Log.h"
#include "StrUtil.h"
#include "EQueue.h"
#include "ThreadPool.h"

#include "Status.h"
#include "ConfHelper.h"

#include "HttpParser.h"
#include "HttpHandler.h"

namespace tzhttpd {

class TcpConnAsync;

typedef TcpConnAsync ConnType;
typedef std::shared_ptr<ConnType> ConnTypePtr;

class HttpServer;
class HttpConf {

    friend class HttpServer;

private:
    bool load_conf(std::shared_ptr<libconfig::Config> conf_ptr);
    bool load_conf(const libconfig::Config& conf);

private:
    std::string bind_addr_;
    unsigned short listen_port_;
    std::set<std::string> safe_ip_;

    int backlog_size_;
    int io_thread_number_;

    // 加载、更新配置的时候保护竞争状态
    // 这里保护主要是非atomic的原子结构
    std::mutex             lock_;

    boost::atomic<int>     session_cancel_time_out_;    // session间隔会话时长
    boost::atomic<int>     ops_cancel_time_out_;        // ops操作超时时长

    boost::atomic<bool>    http_service_enabled_;   // 服务开关
    boost::atomic<int64_t> http_service_speed_;

    boost::atomic<int64_t> http_service_token_;

    bool check_safe_ip(const std::string& ip) {
        std::lock_guard<std::mutex> lock(lock_);
        return ( safe_ip_.empty() || (safe_ip_.find(ip) != safe_ip_.cend()) );
    }

    bool get_http_service_token() {

        // 注意：
        // 如果关闭这个选项，则整个服务都不可用了(包括管理页面)
        // 此时如果需要变更除非重启服务，或者采用非web方式(比如发送命令)来恢复配置

        if (!http_service_enabled_) {
            tzhttpd_log_alert("http_service not enabled ...");
            return false;
        }

        // 下面就不使用锁来保证严格的一致性了，因为非关键参数，过多的锁会影响性能
        if (http_service_speed_ == 0) // 没有限流
            return true;

        if (http_service_token_ <= 0) {
            tzhttpd_log_alert("http_service not speed over ...");
            return false;
        }

        -- http_service_token_;
        return true;
    }

    void withdraw_http_service_token() {    // 支持将令牌还回去
        ++ http_service_token_;
    }

    void feed_http_service_token(){
        http_service_token_ = http_service_speed_.load();
    }

    std::shared_ptr<steady_timer> timed_feed_token_;
    void timed_feed_token_handler(const boost::system::error_code& ec);

};  // end class HttpConf



class HttpServer : public boost::noncopyable,
                   public std::enable_shared_from_this<HttpServer> {

    friend class TcpConnAsync;  // can not work with typedef, ugly ...

public:

    // PUBLIC API CALL HERE

    /// Construct the server to listen on the specified TCP address and port
    explicit HttpServer(const std::string& cfgfile, const std::string& instance_name);
    bool init();

    void service();

    int add_http_vhost(const std::string& hostname);

    int add_http_get_handler(const std::string& uri_regex, const HttpGetHandler& handler,
                             bool built_in = false, const std::string hostname = "");
    int add_http_post_handler(const std::string& uri_regex, const HttpPostHandler& handler,
                              bool built_in = false, const std::string hostname = "");

    int register_module_status(const std::string& strKey, StatusCallable func) {
        return Status::instance().register_status_callback(strKey, func);
    }

    int update_http_runtime_conf() {
        return ConfHelper::instance().update_runtime_conf();
    }

private:
    const std::string instance_name_;
    io_service io_service_;

    // 侦听地址信息
    ip::tcp::endpoint ep_;
    std::unique_ptr<ip::tcp::acceptor> acceptor_;

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
    ThreadPool io_service_threads_;
    void io_service_run(ThreadObjPtr ptr);  // main task loop
    int io_service_stop_graceful();
    int io_service_join();

public:
    int update_runtime_conf(const libconfig::Config& conf);
    int module_status(std::string& strModule, std::string& strKey, std::string& strValue);
};


} // end namespace tzhttpd

#endif //__TZHTTPD_HTTP_SERVER_H__
