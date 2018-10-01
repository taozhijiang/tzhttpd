/*-
 * Copyright (c) 2018 TAO Zhijiang<taozhijiang@gmail.com>
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

#include "StrUtil.h"
#include "EQueue.h"
#include "ThreadPool.h"
#include "AliveTimer.h"

#include "HttpVhost.h"
#include "HttpParser.h"

namespace tzhttpd {

class TCPConnAsync;

typedef TCPConnAsync ConnType;
typedef std::shared_ptr<ConnType> ConnTypePtr;
typedef std::weak_ptr<ConnType>   ConnTypeWeakPtr;

class HttpServer;
class HttpConf {

    friend class HttpServer;

private:
    bool load_config(const libconfig::Config& cfg);

private:
    std::string bind_addr_;
    unsigned short listen_port_;
    std::set<std::string> safe_ip_;

    int backlog_size_;
    int io_thread_number_;

    // 加载、更新配置的时候保护竞争状态
    std::mutex          lock_;

    boost::atomic<int>     conn_time_out_;
    boost::atomic<int>     conn_time_out_linger_;

    boost::atomic<int>     ops_cancel_time_out_;    // sec 会话超时自动取消ops

    boost::atomic<bool>    http_service_enabled_;   // 服务开关
    boost::atomic<int64_t> http_service_speed_;

    boost::atomic<int64_t> http_service_token_;

    bool check_safe_ip(const std::string& ip) {
        std::lock_guard<std::mutex> lock(lock_);
        return ( safe_ip_.empty() || (safe_ip_.find(ip) != safe_ip_.cend()) );
    }

    bool get_http_service_token() {

        // if (!http_service_enabled_) {
        //    tzhttpd_log_alert("http_service not enabled ...");
        //    return false;
        // }

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

    std::shared_ptr<boost::asio::deadline_timer> timed_feed_token_;
    void timed_feed_token_handler(const boost::system::error_code& ec);

};  // end class HttpConf



class HttpServer : public boost::noncopyable,
                   public std::enable_shared_from_this<HttpServer> {

    friend class TCPConnAsync;  // can not work with typedef, ugly ...

public:

    /// Construct the server to listen on the specified TCP address and port
    explicit HttpServer(const std::string& cfgfile, const std::string& instance_name);
    bool init();

    int update_runtime_cfg(const libconfig::Config& cfg);
    void service();

private:
    const std::string instance_name_;
    io_service io_service_;

    // 侦听地址信息
    ip::tcp::endpoint ep_;
    std::unique_ptr<ip::tcp::acceptor> acceptor_;

    std::shared_ptr<boost::asio::deadline_timer> timed_checker_;
    void timed_checker_handler(const boost::system::error_code& ec);

    HttpConf conf_;
    HttpVhost vhost_manager_;

    void do_accept();
    void accept_handler(const boost::system::error_code& ec, SocketPtr ptr);

    AliveTimer<ConnType>    conns_alive_;

public:

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

    int conn_destroy(ConnTypePtr p_conn);


    // 用户态的接口，所以这里的basic_auth 是 user:passwd形式，没有进行base64转码
    int register_http_get_handler(std::string uri_regex, const HttpGetHandler& handler, bool built_in,
                                  const std::set<std::string>& basic_auth_raw = std::set<std::string>()) {
        return register_http_get_handler("", uri_regex, handler,
                                         built_in, basic_auth_raw);
    }
    int register_http_post_handler(std::string uri_regex, const HttpPostHandler& handler, bool built_in,
                                   const std::set<std::string>& basic_auth_raw = std::set<std::string>()) {
        return register_http_post_handler("", uri_regex, handler,
                                          built_in, basic_auth_raw);
    }

    int register_http_get_handler(std::string vhost,
                                  std::string uri_regex, const HttpGetHandler& handler, bool built_in,
                                  const std::set<std::string>& basic_auth_raw = std::set<std::string>()) {

        std::set<std::string> basic_auth {};
        if (!basic_auth_raw.empty()) {
            for (auto iter = basic_auth_raw.begin(); iter != basic_auth_raw.end(); ++iter) {
                std::string auth_item = CryptoUtil::base64_encode(*iter);
                if (!auth_item.empty())
                    basic_auth.insert(auth_item);
            }
        }

        return vhost_manager_.register_http_get_handler(vhost, uri_regex, handler,
                                                        built_in, basic_auth);
    }

    int register_http_post_handler(std::string vhost,
                                   std::string uri_regex, const HttpPostHandler& handler, bool built_in,
                                   const std::set<std::string>& basic_auth_raw = std::set<std::string>()) {

        std::set<std::string> basic_auth {};
        if (!basic_auth_raw.empty()) {
            for (auto iter = basic_auth_raw.begin(); iter != basic_auth_raw.end(); ++iter) {
                std::string auth_item = CryptoUtil::base64_encode(*iter);
                if (!auth_item.empty())
                    basic_auth.insert(auth_item);
            }
        }

        return vhost_manager_.register_http_post_handler(vhost, uri_regex, handler,
                                                         built_in, basic_auth);
    }

    int find_http_get_handler(std::string vhost, std::string uri, HttpGetHandlerObjectPtr& phandler_obj) {
        do {
            if (!conf_.http_service_enabled_) {

                // 服务关闭，但是还是允许特定页面的访问
                uri = StrUtil::pure_uri_path(uri);
                if (boost::iequals(uri, "/internal_manage")) {
                    break; // fall through handler fetch
                }

                tzhttpd_log_err("http_service_enabled_ == false, reject request GET %s ... ", uri.c_str());
                return -1;
            }
        } while (0);

        return vhost_manager_.find_http_get_handler(vhost, uri, phandler_obj);
    }

    int find_http_post_handler(std::string vhost, std::string uri, HttpPostHandlerObjectPtr& phandler_obj) {
        if (!conf_.http_service_enabled_) {
            tzhttpd_log_err("http_service_enabled_ == false, reject request POST %s ... ", uri.c_str());
            return -1;
        }

        return vhost_manager_.find_http_post_handler(vhost, uri, phandler_obj);
    }


public:
    ThreadPool io_service_threads_;
    void io_service_run(ThreadObjPtr ptr);  // main task loop
    int io_service_stop_graceful();
    int io_service_join();
};


} // end namespace tzhttpd

#endif //__TZHTTPD_HTTP_SERVER_H__
