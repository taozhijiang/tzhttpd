/*-
 * Copyright (c) 2018 TAO Zhijiang<taozhijiang@gmail.com>
 *
 * Licensed under the BSD-3-Clause license, see LICENSE for full information.
 *
 */

#include <signal.h>
#include <iostream>
#include <sstream>
#include <thread>
#include <functional>

#include <boost/format.hpp>
#include <boost/atomic/atomic.hpp>

#include "HttpCfgHelper.h"
#include "TCPConnAsync.h"
#include "HttpHandler.h"
#include "HttpServer.h"

#include "StrUtil.h"
#include "SslSetup.h"
#include "Log.h"

namespace tzhttpd {

namespace http_handler {
// init only once at startup
extern std::string              http_server_version;
} // end namespace http_handler

static const size_t bucket_size_ = 0xFF;
static size_t bucket_hash_index_call(const std::shared_ptr<ConnType>& ptr) {
    return std::hash<ConnType *>()(ptr.get());
}


std::once_flag http_version_once;
void init_http_version(const std::string& server_version) {
    http_handler::http_server_version = server_version;
}


bool HttpConf::load_config(const libconfig::Config& cfg) {

    int listen_port = 0;
    ConfUtil::conf_value(cfg, "http.bind_addr", bind_addr_);
    ConfUtil::conf_value(cfg, "http.listen_port", listen_port);
    if (bind_addr_.empty() || listen_port <=0 ){
        tzhttpd_log_err( "invalid http.bind_addr %s & http.listen_port %d",
                         bind_addr_.c_str(), listen_port);
        return false;
    }
    listen_port_ = static_cast<unsigned short>(listen_port);

    std::string ip_list;
    ConfUtil::conf_value(cfg, "http.safe_ip", ip_list);
    if (!ip_list.empty()) {
        std::vector<std::string> ip_vec;
        std::set<std::string> ip_set;
        boost::split(ip_vec, ip_list, boost::is_any_of(";"));
        for (std::vector<std::string>::iterator it = ip_vec.begin(); it != ip_vec.cend(); ++it){
            std::string tmp = boost::trim_copy(*it);
            if (tmp.empty())
                continue;

            ip_set.insert(tmp);
        }

        std::swap(ip_set, safe_ip_);
    }
    if (!safe_ip_.empty()) {
        tzhttpd_log_alert("safe_ip not empty, totally contain %d items",
                          static_cast<int>(safe_ip_.size()));
    }

    ConfUtil::conf_value(cfg, "http.backlog_size", backlog_size_);
    if (backlog_size_ < 0) {
        tzhttpd_log_err( "invalid http.backlog_size %d.", backlog_size_);
        return false;
    }

    ConfUtil::conf_value(cfg, "http.thread_pool_size", io_thread_number_);
    if (io_thread_number_ < 0) {
        tzhttpd_log_err( "invalid http.io_thread_number %d", io_thread_number_);
        return false;
    }

    // once init
    std::string server_version;
    ConfUtil::conf_value(cfg, "http.version", server_version);
    if (!server_version.empty()) {
        std::call_once(http_version_once, init_http_version, server_version);
    }

    // other http parameters
    int value1, value2;
    ConfUtil::conf_value(cfg, "http.conn_time_out", value1, 300);
    ConfUtil::conf_value(cfg, "http.conn_time_out_linger", value2, 10);
    if (value1 < 0 || value2 < 0 || value1 < value2) {
        tzhttpd_log_err("invalid http conn_time_out %d & linger configure value %d, using default.", value1, value2);
        return false;
    }
    conn_time_out_ = value1;
    conn_time_out_linger_ = value2;

    ConfUtil::conf_value(cfg, "http.ops_cancel_time_out", value1);
    if (value1 < 0){
        tzhttpd_log_err("invalid http ops_cancel_time_out value.");
        return false;
    }
    ops_cancel_time_out_ = value1;

    bool value_b;
    ConfUtil::conf_value(cfg, "http.service_enable", value_b, true);
    ConfUtil::conf_value(cfg, "http.service_speed", value1);
    if (value1 < 0){
        tzhttpd_log_err("invalid http.service_speed value %d.", value1);
        return false;
    }
    http_service_enabled_ = value_b;
    http_service_speed_ = value1;

    tzhttpd_log_debug("HttpConf parse cfgfile %s OK!", HttpCfgHelper::instance().get_cfgfile().c_str());

    return true;
}

void HttpConf::timed_feed_token_handler(const boost::system::error_code& ec) {

    if (http_service_speed_ == 0) {
        tzhttpd_log_alert("unlock speed jail, close the timer.");
        timed_feed_token_.reset();
        return;
    }

    // 恢复token
    feed_http_service_token();

    // 再次启动定时器
    timed_feed_token_->expires_from_now(boost::posix_time::millisec(5000)); // 5sec
    timed_feed_token_->async_wait(
        std::bind(&HttpConf::timed_feed_token_handler, this, std::placeholders::_1));
}



/////////////////

HttpServer::HttpServer(const std::string& cfgfile, const std::string& instance_name) :
    instance_name_(instance_name),
    io_service_(),
    acceptor_(),
    timed_checker_(),
    conf_({}),
    conns_alive_("TcpConnAsync"),
    io_service_threads_() {

    HttpCfgHelper::instance().init(cfgfile);

}

bool HttpServer::init() {

    // incase not forget
    ::signal(SIGPIPE, SIG_IGN);

    boost::atomic<int> atomic_int;
    if (atomic_int.is_lock_free()) {
        tzhttpd_log_alert("GOOD, your system atomic is lock_free ...");
    } else {
        tzhttpd_log_err("BAD, your system atomic is not lock_free, may impact performance ...");
    }

    if (!Ssl_thread_setup()) {
        tzhttpd_log_err("Ssl_thread_setup failed!");
        return false;
    }

    libconfig::Config cfg;

    std::string cfgfile = HttpCfgHelper::instance().get_cfgfile();
    try {
        cfg.readFile(cfgfile.c_str());
    } catch(libconfig::FileIOException &fioex) {
        fprintf(stderr, "I/O error while reading file: %s.", cfgfile.c_str());
        tzhttpd_log_err( "I/O error while reading file: %s.", cfgfile.c_str());
        return false;
    } catch(libconfig::ParseException &pex) {
        fprintf(stderr, "Parse error at %d - %s", pex.getLine(), pex.getError());
        tzhttpd_log_err( "Parse error at %d - %s", pex.getLine(), pex.getError());
        return false;
    }

    // protect cfg race conditon
    std::lock_guard<std::mutex> lock(conf_.lock_);

    if (!conf_.load_config(cfg)) {
        tzhttpd_log_err("Load cfg failed!");
        return false;
    }

    ep_ = ip::tcp::endpoint(ip::address::from_string(conf_.bind_addr_), conf_.listen_port_);
    tzhttpd_log_alert("create listen endpoint for %s:%d",
                      conf_.bind_addr_.c_str(), conf_.listen_port_);

    tzhttpd_log_debug("socket/session conn time_out: %ds, linger: %ds",
                      conf_.conn_time_out_.load(), conf_.conn_time_out_linger_.load());
    conns_alive_.init(std::bind(&HttpServer::conn_destroy, this, std::placeholders::_1),
                      conf_.conn_time_out_, conf_.conn_time_out_linger_);

    tzhttpd_log_debug("socket/session conn cancel time_out: %d, enabled: %s",
                      conf_.ops_cancel_time_out_.load(),
                      conf_.ops_cancel_time_out_ > 0 ? "true" : "false");

    if (conf_.http_service_speed_) {
        conf_.timed_feed_token_.reset(new boost::asio::deadline_timer (io_service_,
                                              boost::posix_time::millisec(5000))); // 5sec
        if (!conf_.timed_feed_token_) {
            tzhttpd_log_err("Create timed_feed_token_ failed!");
            return false;
        }

        conf_.timed_feed_token_->async_wait(
            std::bind(&HttpConf::timed_feed_token_handler, &conf_, std::placeholders::_1));
    }
    tzhttpd_log_debug("http service enabled: %s, speed: %ld", conf_.http_service_enabled_ ? "true" : "false",
                      conf_.http_service_speed_.load());

    if (!io_service_threads_.init_threads(
        std::bind(&HttpServer::io_service_run, shared_from_this(), std::placeholders::_1),
        conf_.io_thread_number_)) {
        tzhttpd_log_err("HttpServer::io_service_run init task failed!");
        return false;
    }

    timed_checker_.reset(new boost::asio::deadline_timer (io_service_,
                                              boost::posix_time::millisec(5000))); // 5sec
    if (!timed_checker_) {
        tzhttpd_log_err("Create timed_checker_ failed!");
        return false;
    }
    timed_checker_->async_wait(
        std::bind(&HttpServer::timed_checker_handler, shared_from_this(), std::placeholders::_1));

    if (HttpCfgHelper::instance().register_cfg_callback(
            std::bind(&HttpServer::update_runtime_cfg, shared_from_this(), std::placeholders::_1 )) != 0) {
        tzhttpd_log_err("HttpServer register cfg callback failed!");
        return false;
    }

    // vhost_manager_ initialize
    if (!vhost_manager_.init(cfg)) {
        tzhttpd_log_err("HttpVhost initialize failed!");
        return false;
    }

    return true;
}

int HttpServer::update_runtime_cfg(const libconfig::Config& cfg) {

    tzhttpd_log_debug("HttpServer::update_runtime_cfg called ...");

    HttpConf conf {};
    if (!conf.load_config(cfg)) {
        tzhttpd_log_err("Load cfg failed!");
        return -1;
    }

    // protect cfg race conditon
    std::lock_guard<std::mutex> lock(conf_.lock_);

    tzhttpd_log_alert("Exchange safe_ip_ .");
    std::swap(conf.safe_ip_, conf_.safe_ip_);

    if (conf.ops_cancel_time_out_ != conf_.ops_cancel_time_out_) {
        tzhttpd_log_alert("=> update socket/session conn cancel time_out: from %d to %d",
                          conf_.ops_cancel_time_out_.load(), conf.ops_cancel_time_out_.load());
        conf_.ops_cancel_time_out_ = conf.ops_cancel_time_out_.load();
    }

    // 注意，一旦关闭消费，所有的URI请求都会被拒绝掉，除了internal_manage管理页面可用
    if (conf.http_service_enabled_ != conf_.http_service_enabled_) {
        tzhttpd_log_alert("=> update http_service_enabled: from %d to %d",
                          conf_.http_service_enabled_.load(), conf.http_service_enabled_.load());
        conf_.http_service_enabled_ = conf.http_service_enabled_.load();
    }

    if (conf.http_service_speed_ != conf_.http_service_speed_ ) {

        tzhttpd_log_alert("=> update http_service_speed: from %ld to %ld",
                          conf_.http_service_speed_.load(), conf.http_service_speed_.load());
        conf_.http_service_speed_ = conf.http_service_speed_.load();

        if (conf.http_service_speed_) { // 首次启用
            if (! conf_.timed_feed_token_) {
                conf_.timed_feed_token_.reset(new boost::asio::deadline_timer (io_service_,
                                                      boost::posix_time::millisec(5000))); // 5sec
                if (!conf_.timed_feed_token_) {
                    tzhttpd_log_err("create timed_feed_token_ failed!");
                    return -2;
                }

                conf_.timed_feed_token_->async_wait(
                    std::bind(&HttpConf::timed_feed_token_handler, &conf_, std::placeholders::_1));
            }
        } else { // 禁用功能

            // 禁用在handler中删除定时器就可以了，直接这里删会导致CoreDump
        }
    }

    // 当前不支持缩减线程
    if (conf.io_thread_number_ > conf_.io_thread_number_) {
        tzhttpd_log_alert("=> resize io_thread_num from %d to %d",
                          conf_.io_thread_number_, conf.io_thread_number_);
        conf_.io_thread_number_ = conf.io_thread_number_;
        if (io_service_threads_.resize_threads(conf_.io_thread_number_) != 0) {
            tzhttpd_log_err("resize io_thread_num may failed!");
            return -3;
        }
    }

    // reload cgi-handlers
    int ret_code = vhost_manager_.update_runtime_cfg(cfg);
    if (ret_code != 0) {
        tzhttpd_log_err("register cgi-handler return %d", ret_code);
    }

    tzhttpd_log_alert("HttpServer::update_runtime_cfg called return %d ...", ret_code);
    return ret_code;
}

void HttpServer::timed_checker_handler(const boost::system::error_code& ec) {

    conns_alive_.clean_up();

    // 再次启动定时器
    timed_checker_->expires_from_now(boost::posix_time::millisec(5000)); // 5sec
    timed_checker_->async_wait(
        std::bind(&HttpServer::timed_checker_handler, shared_from_this(), std::placeholders::_1));
}


// main task loop
void HttpServer::io_service_run(ThreadObjPtr ptr) {

    tzhttpd_log_info("HttpServer io_service thread %#lx is about to work... ", (long)pthread_self());

    while (true) {

        if (unlikely(ptr->status_ == ThreadStatus::kThreadTerminating)) {
            tzhttpd_log_err("thread %#lx is about to terminating...", (long)pthread_self());
            break;
        }

        // 线程启动
        if (unlikely(ptr->status_ == ThreadStatus::kThreadSuspend)) {
            ::usleep(1*1000*1000);
            continue;
        }

        boost::system::error_code ec;
        io_service_.run(ec);

        if (ec){
            tzhttpd_log_err("io_service stopped...");
            break;
        }
    }

    ptr->status_ = ThreadStatus::kThreadDead;
    tzhttpd_log_info("HttpServer io_service thread %#lx is about to terminate ... ", (long)pthread_self());

    return;
}

void HttpServer::service() {

    acceptor_.reset( new ip::tcp::acceptor(io_service_) );
    acceptor_->open(ep_.protocol());

    acceptor_->set_option(ip::tcp::acceptor::reuse_address(true));
    acceptor_->bind(ep_);
    acceptor_->listen(conf_.backlog_size_ > 0 ? conf_.backlog_size_ : socket_base::max_connections);

    do_accept();
}

void HttpServer::do_accept() {

    SocketPtr sock_ptr(new ip::tcp::socket(io_service_));
    acceptor_->async_accept(*sock_ptr,
                           std::bind(&HttpServer::accept_handler, this,
                                       std::placeholders::_1, sock_ptr));
}

void HttpServer::accept_handler(const boost::system::error_code& ec, SocketPtr sock_ptr) {

    do {

        if (ec) {
            tzhttpd_log_err("Error during accept with %d, %s", ec.value(), ec.message().c_str());
            break;
        }

        boost::system::error_code ignore_ec;
        auto remote = sock_ptr->remote_endpoint(ignore_ec);
        if (ignore_ec) {
            tzhttpd_log_err("get remote info failed:%d, %s", ignore_ec.value(), ignore_ec.message().c_str());
            break;
        }

        std::string remote_ip = remote.address().to_string(ignore_ec);
        tzhttpd_log_debug("Remote Client Info: %s:%d", remote_ip.c_str(), remote.port());

        if (!conf_.check_safe_ip(remote_ip)) {
            tzhttpd_log_err("check safe_ip failed for: %s", remote_ip.c_str());

            sock_ptr->shutdown(boost::asio::socket_base::shutdown_both, ignore_ec);
            sock_ptr->close(ignore_ec);
            break;
        }

        if (!conf_.get_http_service_token()) {
            tzhttpd_log_err("request http service token failed, enabled: %s, speed: %ld",
                    conf_.http_service_enabled_ ? "true" : "false", conf_.http_service_speed_.load());

            sock_ptr->shutdown(boost::asio::socket_base::shutdown_both, ignore_ec);
            sock_ptr->close(ignore_ec);
            break;
        }

        ConnTypePtr new_conn = std::make_shared<ConnType>(sock_ptr, *this);
        conn_add(new_conn);

        new_conn->start();

    } while (0);

    // 再次启动接收异步请求
    do_accept();
}


int HttpServer::conn_destroy(ConnTypePtr p_conn) {
    p_conn->sock_shutdown_and_close(ShutdownType::kBoth);
    return 0;
}


int HttpServer::io_service_stop_graceful() {
    tzhttpd_log_err("About to stop io_service... ");
    io_service_.stop();
    io_service_threads_.graceful_stop_threads();
    return 0;
}

int HttpServer::io_service_join() {
    io_service_threads_.join_threads();
    return 0;
}


} // end namespace tzhttpd
