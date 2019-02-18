/*-
 * Copyright (c) 2018-2019 TAO Zhijiang<taozhijiang@gmail.com>
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

#include "TcpConnAsync.h"

#include "HttpProto.h"
#include "HttpHandler.h"
#include "HttpServer.h"
#include "Timer.h"
#include "Dispatcher.h"
#include "Status.h"

#include "SslSetup.h"

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

bool HttpConf::load_conf(std::shared_ptr<libconfig::Config> conf_ptr) {
    const auto& conf = *conf_ptr;
    return load_conf(conf);
}

bool HttpConf::load_conf(const libconfig::Config& conf) {

    int listen_port = 0;
    ConfUtil::conf_value(conf, "http.bind_addr", bind_addr_);
    ConfUtil::conf_value(conf, "http.listen_port", listen_port);
    if (bind_addr_.empty() || listen_port <=0 ){
        tzhttpd_log_err( "invalid http.bind_addr %s & http.listen_port %d",
                         bind_addr_.c_str(), listen_port);
        return false;
    }
    listen_port_ = static_cast<unsigned short>(listen_port);

    std::string ip_list;
    ConfUtil::conf_value(conf, "http.safe_ip", ip_list);
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

    ConfUtil::conf_value(conf, "http.backlog_size", backlog_size_);
    if (backlog_size_ < 0) {
        tzhttpd_log_err( "invalid http.backlog_size %d.", backlog_size_);
        return false;
    }

    ConfUtil::conf_value(conf, "http.io_thread_pool_size", io_thread_number_);
    if (io_thread_number_ < 0) {
        tzhttpd_log_err( "invalid http.io_thread_number %d", io_thread_number_);
        return false;
    }

    // once init
    std::string server_version;
    ConfUtil::conf_value(conf, "http.version", server_version);
    if (!server_version.empty()) {
        std::call_once(http_version_once, init_http_version, server_version);
    }
    // other http parameters
    int value_i;

    ConfUtil::conf_value(conf, "http.ops_cancel_time_out", value_i);
    if (value_i < 0){
        tzhttpd_log_err("invalid http ops_cancel_time_out value.");
        return false;
    }
    ops_cancel_time_out_ = value_i;

    ConfUtil::conf_value(conf, "http.session_cancel_time_out", value_i);
    if (value_i < 0){
        tzhttpd_log_err("invalid http session_cancel_time_out value.");
        return false;
    }
    session_cancel_time_out_ = value_i;

    bool value_b;
    ConfUtil::conf_value(conf, "http.service_enable", value_b, true);
    ConfUtil::conf_value(conf, "http.service_speed", value_i);
    if (value_i < 0){
        tzhttpd_log_err("invalid http.service_speed value %d.", value_i);
        return false;
    }
    http_service_enabled_ = value_b;
    http_service_speed_ = value_i;

    tzhttpd_log_debug("HttpConf parse conf OK!");

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
    timed_feed_token_->expires_from_now(boost::chrono::seconds(1)); // 1sec
    timed_feed_token_->async_wait(
                std::bind(&HttpConf::timed_feed_token_handler, this, std::placeholders::_1));
}



/////////////////

HttpServer::HttpServer(const std::string& cfgfile, const std::string& instance_name) :
    instance_name_(instance_name),
    io_service_(),
    acceptor_(),
    cfgfile_(cfgfile),
    conf_({}),
    io_service_threads_() {

   bool ret = ConfHelper::instance().init(cfgfile_);
   if (!ret) {
       tzhttpd_log_err("init conf failed.");
       return;
   }

   auto conf_ptr = ConfHelper::instance().get_conf();
   int log_level = 0;
   ConfUtil::conf_value(*conf_ptr, "http.log_level", log_level);
    if (log_level <= 0 || log_level > 7) {
        tzhttpd_log_notice("invalid log_level value, reset to default 7.");
        log_level = 7;
    }

    tzhttpd_log_init(log_level);
    tzhttpd_log_notice("initialized log with level: %d", log_level);
}


int system_status_handler(const HttpParser& http_parser,
                          std::string& response, std::string& status_line, std::vector<std::string>& add_header) {

    std::string result;
    Status::instance().collect_status(result);

    response = result;
    status_line = http_proto::generate_response_status_line(http_parser.get_version(), http_proto::StatusCode::success_ok);

    return 0;
}

bool system_manage_page_init(HttpServer& server);

bool HttpServer::init() {

    (void)Status::instance();
    (void)Dispatcher::instance();

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

    if(!Timer::instance().init()) {
        tzhttpd_log_err("Timer service init failed.");
        return false;
    }

    auto conf_ptr = ConfHelper::instance().get_conf();
	if(!conf_ptr) { 
		tzhttpd_log_err("ConfHelper return null conf pointer, maybe your conf file ill!");
		return false;
	}

    // protect cfg race conditon
    std::lock_guard<std::mutex> lock(conf_.lock_);
    if (!conf_.load_conf(conf_ptr)) {
        tzhttpd_log_err("Load http conf failed!");
        return false;
    }

    ep_ = ip::tcp::endpoint(ip::address::from_string(conf_.bind_addr_), conf_.listen_port_);
    tzhttpd_log_alert("create listen endpoint for %s:%d",
                      conf_.bind_addr_.c_str(), conf_.listen_port_);

    tzhttpd_log_debug("socket/session conn cancel time_out: %d secs, enabled: %s",
                      conf_.ops_cancel_time_out_.load(),
                      conf_.ops_cancel_time_out_ > 0 ? "true" : "false");

    if (conf_.http_service_speed_) {
        conf_.timed_feed_token_.reset(new steady_timer (io_service_)); // 1sec
        if (!conf_.timed_feed_token_) {
            tzhttpd_log_err("Create timed_feed_token_ failed!");
            return false;
        }

        conf_.timed_feed_token_->expires_from_now(boost::chrono::seconds(1));
        conf_.timed_feed_token_->async_wait(
                    std::bind(&HttpConf::timed_feed_token_handler, &conf_, std::placeholders::_1));
    }
    tzhttpd_log_debug("http service enabled: %s, speed: %ld tps", conf_.http_service_enabled_ ? "true" : "false",
                      conf_.http_service_speed_.load());

    if (!io_service_threads_.init_threads(
        std::bind(&HttpServer::io_service_run, shared_from_this(), std::placeholders::_1),
        conf_.io_thread_number_)) {
        tzhttpd_log_err("HttpServer::io_service_run init task failed!");
        return false;
    }

    if (!Dispatcher::instance().init()) {
        tzhttpd_log_err("Init HttpDispatcher failed.");
        return false;
    }

    // 注册配置动态更新的回调函数
    ConfHelper::instance().register_conf_callback(
            std::bind(&HttpServer::update_runtime_conf, shared_from_this(),
                      std::placeholders::_1));

    // 系统状态展示相关的初始化
    Status::instance().register_status_callback(
            "http_server",
            std::bind(&HttpServer::module_status, shared_from_this(),
                      std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));

    if (!system_manage_page_init(*this)) {
        tzhttpd_log_err("init system manage page failed, treat as fatal.");
        return false;
    }

    return true;
}



// main task loop
void HttpServer::io_service_run(ThreadObjPtr ptr) {

    tzhttpd_log_info("HttpServer io_service thread %#lx is about to work... ", (long)pthread_self());

    while (true) {

        if (unlikely(ptr->status_ == ThreadStatus::kTerminating)) {
            tzhttpd_log_err("thread %#lx is about to terminating...", (long)pthread_self());
            break;
        }

        // 线程启动
        if (unlikely(ptr->status_ == ThreadStatus::kSuspend)) {
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

    ptr->status_ = ThreadStatus::kDead;
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

int HttpServer::add_http_vhost(const std::string& hostname) {
    return Dispatcher::instance().add_virtual_host(hostname);
}

int HttpServer::add_http_get_handler(const std::string& uri_regex, const HttpGetHandler& handler,
                                     bool built_in, const std::string hostname) {
    return Dispatcher::instance().add_http_get_handler(hostname, uri_regex, handler, built_in);
}

int HttpServer::add_http_post_handler(const std::string& uri_regex, const HttpPostHandler& handler,
                                      bool built_in, const std::string hostname) {
    return Dispatcher::instance().add_http_post_handler(hostname, uri_regex, handler, built_in);
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

        std::shared_ptr<ConnType> new_conn = std::make_shared<ConnType>(sock_ptr, *this);

        new_conn->start();

    } while (0);

    // 再次启动接收异步请求
    do_accept();
}


int HttpServer::io_service_stop_graceful() {

    tzhttpd_log_err("about to stop io_service... ");

    io_service_.stop();
    io_service_threads_.graceful_stop_threads();
    return 0;
}

int HttpServer::io_service_join() {

    tzhttpd_log_err("about to join io_service... ");

    io_service_threads_.join_threads();
    return 0;
}


int HttpServer::module_status(std::string& strModule, std::string& strKey, std::string& strValue) {

    strModule = "tzhttpd";
    strKey = "http_server";

    std::stringstream ss;

    ss << "\t" << "instance_name: " << instance_name_ << std::endl;
    ss << "\t" << "service_addr: " << conf_.bind_addr_ << "@" << conf_.listen_port_ << std::endl;
    ss << "\t" << "backlog_size: " << conf_.backlog_size_ << std::endl;
    ss << "\t" << "io_thread_pool_size: " << conf_.io_thread_number_ << std::endl;
    ss << "\t" << "safe_ips: " ;

    {
        // protect cfg race conditon
        std::lock_guard<std::mutex> lock(conf_.lock_);
        for (auto iter = conf_.safe_ip_.begin(); iter != conf_.safe_ip_.end(); ++iter) {
            ss << *iter << ", ";
        }
        ss << std::endl;
    }

    ss << "\t" << std::endl;

    ss << "\t" << "http_service_enabled: " << (conf_.http_service_enabled_  ? "true" : "false") << std::endl;
    ss << "\t" << "http_service_speed(tps): " << conf_.http_service_speed_ << std::endl;
    ss << "\t" << "session_cancel_time_out: " << conf_.session_cancel_time_out_ << std::endl;
    ss << "\t" << "ops_cancel_time_out: " << conf_.ops_cancel_time_out_ << std::endl;

    strValue = ss.str();
    return 0;
}


int HttpServer::update_runtime_conf(const libconfig::Config& cfg) {

    HttpConf conf {};
    if (!conf.load_conf(cfg)) {
        tzhttpd_log_err("load conf for HttpConf failed.");
        return -1;
    }

    if (conf_.session_cancel_time_out_ != conf.session_cancel_time_out_) {
        tzhttpd_log_notice("update session_cancel_time_out from %d to %d",
                           conf_.session_cancel_time_out_.load(), conf.session_cancel_time_out_.load());
        conf_.session_cancel_time_out_ = conf.session_cancel_time_out_.load();
    }

    if (conf_.ops_cancel_time_out_ != conf.ops_cancel_time_out_) {
        tzhttpd_log_notice("update ops_cancel_time_out from %d to %d",
                           conf_.ops_cancel_time_out_.load(),  conf.ops_cancel_time_out_.load());
        conf_.ops_cancel_time_out_ = conf.ops_cancel_time_out_.load();
    }


    tzhttpd_log_notice("swap safe_ips...");

    {
        // protect cfg race conditon
        std::lock_guard<std::mutex> lock(conf_.lock_);
        conf_.safe_ip_.swap(conf.safe_ip_);
    }

    if (conf_.http_service_speed_ != conf.http_service_speed_) {
        tzhttpd_log_notice("update http_service_speed from %ld to %ld",
                           conf_.http_service_speed_.load() , conf.http_service_speed_.load());
        conf_.http_service_speed_ = conf.http_service_speed_.load();

        // 检查定时器是否存在
        if (conf_.http_service_speed_) {

            // 直接重置定时器，无论有没有
            conf_.timed_feed_token_.reset(new steady_timer(io_service_)); // 1sec
            if (!conf_.timed_feed_token_) {
                tzhttpd_log_err("Create timed_feed_token_ failed!");
                return -1;
            }

            conf_.timed_feed_token_->expires_from_now(boost::chrono::seconds(1));
            conf_.timed_feed_token_->async_wait(
                        std::bind(&HttpConf::timed_feed_token_handler, &conf_, std::placeholders::_1));
        }
        else // speed == 0
        {
            if (conf_.timed_feed_token_) {
                boost::system::error_code ignore_ec;
                conf_.timed_feed_token_->cancel(ignore_ec);
                conf_.timed_feed_token_.reset();
            }
        }
    }

    tzhttpd_log_notice("http service enabled: %s, speed: %ld", conf_.http_service_enabled_ ? "true" : "false",
                      conf_.http_service_speed_.load());

    return 0;
}

} // end namespace tzhttpd
