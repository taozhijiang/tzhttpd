/*-
 * Copyright (c) 2018-2019 TAO Zhijiang<taozhijiang@gmail.com>
 *
 * Licensed under the BSD-3-Clause license, see LICENSE for full information.
 *
 */

#include <xtra_rhel.h>

#include <signal.h>
#include <iostream>
#include <sstream>
#include <thread>
#include <functional>

#include <boost/format.hpp>

#include <system/ConstructException.h>
#include <crypto/SslSetup.h>


#include "TcpConnAsync.h"

#include "HttpProto.h"
#include "HttpParser.h"
#include "HttpHandler.h"
#include "HttpServer.h"
#include "Dispatcher.h"

#include "Global.h"

using namespace boost::asio;

namespace tzhttpd {

namespace http_handler {
// init only once at startup
extern std::string              http_server_version;
} // end namespace http_handler

static const size_t bucket_size_ = 0xFF;
static size_t bucket_hash_index_call(const std::shared_ptr<ConnType>& ptr) {
    return std::hash<ConnType*>()(ptr.get());
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

    conf.lookupValue("http.bind_addr", bind_addr_);
    conf.lookupValue("http.bind_port", bind_port_);
    if (bind_addr_.empty() || bind_port_ <= 0) {
        roo::log_err("invalid http.bind_addr %s & http.bind_port %d",
                     bind_addr_.c_str(), bind_port_);
        return false;
    }

    std::string ip_list;
    conf.lookupValue("http.safe_ip", ip_list);
    if (!ip_list.empty()) {
        std::vector<std::string> ip_vec;
        std::set<std::string> ip_set;
        boost::split(ip_vec, ip_list, boost::is_any_of(";"));
        for (std::vector<std::string>::iterator it = ip_vec.begin(); it != ip_vec.cend(); ++it) {
            std::string tmp = boost::trim_copy(*it);
            if (tmp.empty())
                continue;

            ip_set.insert(tmp);
        }

        std::swap(ip_set, safe_ip_);
    }
    if (!safe_ip_.empty()) {
        roo::log_warning("safe_ip not empty, totally contain %d items",
                         static_cast<int>(safe_ip_.size()));
    }

    conf.lookupValue("http.backlog_size", backlog_size_);
    if (backlog_size_ < 0) {
        roo::log_err("invalid http.backlog_size %d.", backlog_size_);
        return false;
    }

    conf.lookupValue("http.io_thread_pool_size", io_thread_number_);
    if (io_thread_number_ < 0) {
        roo::log_err("invalid http.io_thread_number %d", io_thread_number_);
        return false;
    }

    // once init
    std::string server_version;
    conf.lookupValue("http.version", server_version);
    if (!server_version.empty()) {
        std::call_once(http_version_once, init_http_version, server_version);
    }

    conf.lookupValue("http.ops_cancel_time_out", ops_cancel_time_out_);
    if (ops_cancel_time_out_ < 0) {
        roo::log_err("invalid http ops_cancel_time_out: %d", ops_cancel_time_out_);
        return false;
    }

    conf.lookupValue("http.session_cancel_time_out", session_cancel_time_out_);
    if (session_cancel_time_out_ < 0) {
        roo::log_err("invalid http session_cancel_time_out: %d", session_cancel_time_out_);
        return false;
    }

    conf.lookupValue("http.service_enable", service_enabled_);
    conf.lookupValue("http.service_speed", service_speed_);
    if (service_speed_ < 0) {
        roo::log_err("invalid http.service_speed: %d.", service_speed_);
        return false;
    }

    conf.lookupValue("http.service_concurrency", service_concurrency_);
    if (service_concurrency_ < 0) {
        roo::log_err("invalid http.service_concurrency: %d.", service_concurrency_);
        return false;
    }

    roo::log_info("HttpConf parse conf OK!");
    return true;
}

void HttpConf::timed_feed_token_handler(const boost::system::error_code& ec) {

    if (service_speed_ == 0) {
        roo::log_warning("unlock speed jail, close the timer.");
        timed_feed_token_.reset();
        return;
    }

    // 恢复token
    feed_http_service_token();

    // 再次启动定时器
    timed_feed_token_->expires_from_now(seconds(1)); // 1sec
    timed_feed_token_->async_wait(
        std::bind(&HttpConf::timed_feed_token_handler, this, std::placeholders::_1));
}



/////////////////

HttpServer::HttpServer(const std::string& cfgfile, const std::string& instance_name) :
    instance_name_(instance_name),
    io_service_(),
    acceptor_(),
    cfgfile_(cfgfile),
    conf_() {

    (void)Global::instance();
    (void)Dispatcher::instance();

    if (!Global::instance().init(cfgfile_))
        throw roo::ConstructException("Init Global instance failed.");
}


extern bool system_manage_page_init(HttpServer& server);

bool HttpServer::init() {

    // incase not forget
    ::signal(SIGPIPE, SIG_IGN);

    if (!roo::Ssl_thread_setup()) {
        roo::log_err("Ssl_thread_setup failed!");
        return false;
    }

    auto setting_ptr = Global::instance().setting_ptr_->get_setting();
    if (!setting_ptr) {
        roo::log_err("roo::Setting return null pointer, maybe your conf file ill???");
        return false;
    }

    // protect cfg race conditon
    std::lock_guard<std::mutex> lock(conf_.lock_);
    if (!conf_.load_conf(setting_ptr)) {
        roo::log_err("Load http conf failed!");
        return false;
    }

    ep_ = ip::tcp::endpoint(ip::address::from_string(conf_.bind_addr_), conf_.bind_port_);
    roo::log_warning("create listen endpoint for %s:%d",
                     conf_.bind_addr_.c_str(), conf_.bind_port_);

    roo::log_info("socket/session conn cancel time_out: %d secs, enabled: %s",
                  conf_.ops_cancel_time_out_,
                  conf_.ops_cancel_time_out_ > 0 ? "true" : "false");

    if (conf_.service_speed_) {
        conf_.timed_feed_token_.reset(new steady_timer(io_service_)); // 1sec
        if (!conf_.timed_feed_token_) {
            roo::log_err("Create timed_feed_token_ failed!");
            return false;
        }

        conf_.timed_feed_token_->expires_from_now(seconds(1));
        conf_.timed_feed_token_->async_wait(
            std::bind(&HttpConf::timed_feed_token_handler, &conf_, std::placeholders::_1));
    }
    roo::log_info("http service enabled: %s, speed: %d tps", conf_.service_enabled_ ? "true" : "false",
                  conf_.service_speed_);

    if (!io_service_threads_.init_threads(
            std::bind(&HttpServer::io_service_run, shared_from_this(), std::placeholders::_1),
            conf_.io_thread_number_)) {
        roo::log_err("HttpServer::io_service_run init task failed!");
        return false;
    }

    if (!Dispatcher::instance().init()) {
        roo::log_err("Init HttpDispatcher failed.");
        return false;
    }

    // 注册配置动态更新的回调函数
    Global::instance().setting_ptr_->attach_runtime_callback(
        "tzhttpd-HttpServer",
        std::bind(&HttpServer::module_runtime, shared_from_this(),
                  std::placeholders::_1));

    // 系统状态展示相关的初始化
    Global::instance().status_ptr_->attach_status_callback(
        "http_server",
        std::bind(&HttpServer::module_status, shared_from_this(),
                  std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));

    if (!system_manage_page_init(*this)) {
        roo::log_err("init system manage page failed, treat as fatal.");
        return false;
    }

    return true;
}



// main task loop
void HttpServer::io_service_run(roo::ThreadObjPtr ptr) {

    roo::log_warning("HttpServer io_service thread %#lx is about to work... ", (long)pthread_self());

    while (true) {

        if (unlikely(ptr->status_ == roo::ThreadStatus::kTerminating)) {
            roo::log_err("thread %#lx is about to terminating...", (long)pthread_self());
            break;
        }

        // 线程启动
        if (unlikely(ptr->status_ == roo::ThreadStatus::kSuspend)) {
            ::usleep(1 * 1000 * 1000);
            continue;
        }

        boost::system::error_code ec;
        io_service_.run(ec);

        if (ec) {
            roo::log_err("io_service stopped...");
            break;
        }
    }

    ptr->status_ = roo::ThreadStatus::kDead;
    roo::log_warning("HttpServer io_service thread %#lx is about to terminate ... ", (long)pthread_self());

    return;
}

void HttpServer::service() {

    acceptor_.reset(new ip::tcp::acceptor(io_service_));
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
            roo::log_err("Error during accept with %d, %s", ec.value(), ec.message().c_str());
            break;
        }

        boost::system::error_code ignore_ec;
        auto remote = sock_ptr->remote_endpoint(ignore_ec);
        if (ignore_ec) {
            roo::log_err("get remote info failed:%d, %s", ignore_ec.value(), ignore_ec.message().c_str());
            break;
        }

        std::string remote_ip = remote.address().to_string(ignore_ec);
        roo::log_info("Remote Client Info: %s:%d", remote_ip.c_str(), remote.port());

        if (!conf_.check_safe_ip(remote_ip)) {
            roo::log_err("check safe_ip failed for: %s", remote_ip.c_str());

            sock_ptr->shutdown(boost::asio::socket_base::shutdown_both, ignore_ec);
            sock_ptr->close(ignore_ec);
            break;
        }

        if (!conf_.get_http_service_token()) {
            roo::log_err("request http service token failed, enabled: %s, speed: %d",
                         conf_.service_enabled_ ? "true" : "false", conf_.service_speed_);

            sock_ptr->shutdown(boost::asio::socket_base::shutdown_both, ignore_ec);
            sock_ptr->close(ignore_ec);
            break;
        }

        if (conf_.service_concurrency_ != 0 &&
            conf_.service_concurrency_ < TcpConnAsync::current_concurrency_) {
            roo::log_err("service_concurrency_ error, limit: %d, current: %d",
                         conf_.service_concurrency_, TcpConnAsync::current_concurrency_.load());
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

    roo::log_err("about to stop io_service... ");

    io_service_.stop();
    io_service_threads_.graceful_stop_threads();
    return 0;
}

int HttpServer::io_service_join() {

    roo::log_err("about to join io_service... ");

    io_service_threads_.join_threads();
    return 0;
}


int HttpServer::register_http_status_callback(const std::string& name, roo::StatusCallable func) {
    return Global::instance().status_ptr_->attach_status_callback(name, func);
}

int HttpServer::register_http_runtime_callback(const std::string& name, roo::SettingUpdateCallable func) {
    return Global::instance().setting_ptr_->attach_runtime_callback(name, func);
}

int HttpServer::update_http_runtime_conf() {
    return Global::instance().setting_ptr_->update_runtime_setting();
}


int HttpServer::module_status(std::string& strModule, std::string& strKey, std::string& strValue) {

    strModule = "tzhttpd";
    strKey = "http_server";

    std::stringstream ss;

    ss << "\t" << "instance_name: " << instance_name_ << std::endl;
    ss << "\t" << "service_addr: " << conf_.bind_addr_ << "@" << conf_.bind_port_ << std::endl;
    ss << "\t" << "backlog_size: " << conf_.backlog_size_ << std::endl;
    ss << "\t" << "io_thread_pool_size: " << conf_.io_thread_number_ << std::endl;
    ss << "\t" << "safe_ips: ";

    {
        // protect cfg race conditon
        std::lock_guard<std::mutex> lock(conf_.lock_);
        for (auto iter = conf_.safe_ip_.begin(); iter != conf_.safe_ip_.end(); ++iter) {
            ss << *iter << ", ";
        }
        ss << std::endl;
    }

    ss << "\t" << std::endl;

    ss << "\t" << "service_enabled: " << (conf_.service_enabled_  ? "true" : "false") << std::endl;
    ss << "\t" << "service_speed(tps): " << conf_.service_speed_ << std::endl;
    ss << "\t" << "service_concurrency: " << conf_.service_concurrency_ << std::endl;
    ss << "\t" << "session_cancel_time_out: " << conf_.session_cancel_time_out_ << std::endl;
    ss << "\t" << "ops_cancel_time_out: " << conf_.ops_cancel_time_out_ << std::endl;

    strValue = ss.str();
    return 0;
}


int HttpServer::module_runtime(const libconfig::Config& cfg) {

    HttpConf conf{};
    if (!conf.load_conf(cfg)) {
        roo::log_err("load conf for HttpConf failed.");
        return -1;
    }

    if (conf_.session_cancel_time_out_ != conf.session_cancel_time_out_) {
        roo::log_warning("update session_cancel_time_out from %d to %d",
                         conf_.session_cancel_time_out_, conf.session_cancel_time_out_);
        conf_.session_cancel_time_out_ = conf.session_cancel_time_out_;
    }

    if (conf_.ops_cancel_time_out_ != conf.ops_cancel_time_out_) {
        roo::log_warning("update ops_cancel_time_out from %d to %d",
                         conf_.ops_cancel_time_out_, conf.ops_cancel_time_out_);
        conf_.ops_cancel_time_out_ = conf.ops_cancel_time_out_;
    }


    roo::log_warning("swap safe_ips...");

    {
        // protect cfg race conditon
        std::lock_guard<std::mutex> lock(conf_.lock_);
        conf_.safe_ip_.swap(conf.safe_ip_);
    }

    if (conf_.service_speed_ != conf.service_speed_) {
        roo::log_warning("update http_service_speed from %d to %d",
                         conf_.service_speed_, conf.service_speed_);
        conf_.service_speed_ = conf.service_speed_;

        // 检查定时器是否存在
        if (conf_.service_speed_) {

            // 直接重置定时器，无论有没有
            conf_.timed_feed_token_.reset(new steady_timer(io_service_)); // 1sec
            if (!conf_.timed_feed_token_) {
                roo::log_err("Create timed_feed_token_ failed!");
                return -1;
            }

            conf_.timed_feed_token_->expires_from_now(seconds(1));
            conf_.timed_feed_token_->async_wait(
                std::bind(&HttpConf::timed_feed_token_handler, &conf_, std::placeholders::_1));
        } else { // speed == 0
            if (conf_.timed_feed_token_) {
                boost::system::error_code ignore_ec;
                conf_.timed_feed_token_->cancel(ignore_ec);
                conf_.timed_feed_token_.reset();
            }
        }
    }

    if (conf_.service_concurrency_ != conf.service_concurrency_) {
        roo::log_err("update service_concurrency from %d to %d.",
                     conf_.service_concurrency_, conf.service_concurrency_);
        conf_.service_concurrency_ = conf.service_concurrency_;
    }

    roo::log_warning("http service enabled: %s, speed: %d", conf_.service_enabled_ ? "true" : "false",
                     conf_.service_speed_);

    return 0;
}

} // end namespace tzhttpd
