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

#include <boost/asio.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/format.hpp>
#include <boost/algorithm/string.hpp>

#include <system/ConstructException.h>
#include <crypto/SslSetup.h>
#include <concurrency/ThreadPool.h>

#include "TcpConnAsync.h"

#include "HttpProto.h"
#include "HttpParser.h"
#include "HttpHandler.h"
#include "Dispatcher.h"

#include "Global.h"


#include "HttpConf.h"
#include "HttpServer.h"

using namespace boost::asio;

namespace tzhttpd {

typedef TcpConnAsync ConnType;

// HTTP_SERVER的版本信息
namespace http_handler {
extern std::string http_server_version;
}

static void init_http_version(const std::string& server_version) {
    http_handler::http_server_version = server_version;
}

std::once_flag http_version_once;
void init_http_version_once(const std::string& server_version) {
    if (!server_version.empty())
        std::call_once(http_version_once, init_http_version, server_version);
}

// Hash使用
static const size_t bucket_size_ = 0xFF;
static size_t bucket_hash_index_call(const std::shared_ptr<ConnType>& ptr) {
    return std::hash<ConnType*>()(ptr.get());
}


//////
class HttpServerImpl {

    __noncopyable__(HttpServerImpl)

    friend class TcpConnAsync;  // can not work with typedef, ugly ...
    friend class HttpServer;

public:

    //
    // ALL AVAILABLE PUBLIC API CALL HERE
    //

    /// Construct the server to listen on the specified TCP address and port
    HttpServerImpl(const std::string& cfgfile, const std::string& instance_name,
                   HttpServer& server);
    ~HttpServerImpl() = default;

    bool init();
    int service_start();
    int service_stop_graceful();
    int service_join();

private:
    const std::string instance_name_;
    HttpServer& super_server_;

    // boost::asio
    io_service io_service_;
    ip::tcp::endpoint ep_; // 侦听地址信息
    std::unique_ptr<ip::tcp::acceptor> acceptor_;

    const std::string cfgfile_;
    std::shared_ptr<HttpConf> conf_ptr_;

    void do_accept();
    void accept_handler(const boost::system::error_code& ec,
                        std::shared_ptr<boost::asio::ip::tcp::socket> ptr);

public:

    roo::ThreadPool io_service_threads_;
    void io_service_run(roo::ThreadObjPtr ptr);  // main task loop

public:
    int module_runtime(const libconfig::Config& setting);
    int module_status(std::string& module, std::string& key, std::string& value);
};


HttpServerImpl::HttpServerImpl(const std::string& cfgfile, const std::string& instance_name,
                               HttpServer& server) :
    instance_name_(instance_name),
    super_server_(server),
    io_service_(),
    ep_(),
    acceptor_(),
    cfgfile_(cfgfile),
    conf_ptr_(std::make_shared<HttpConf>()) {

    (void)Global::instance();
    (void)Dispatcher::instance();

    if (!conf_ptr_)
        throw roo::ConstructException("Create HttpConf failed.");

    if (!Global::instance().init(cfgfile_))
        throw roo::ConstructException("Init Global instance failed.");
}


extern
bool system_manage_page_init();

bool HttpServerImpl::init() {

    auto setting_ptr = Global::instance().setting_ptr()->get_setting();
    if (!setting_ptr) {
        roo::log_err("Setting return null pointer, maybe your conf file ill???");
        return false;
    }

    // protect cfg race conditon, just in case
    __auto_lock__(conf_ptr_->lock_);
    if (!conf_ptr_->load_setting(setting_ptr)) {
        roo::log_err("Load http conf failed!");
        return false;
    }

    ep_ = ip::tcp::endpoint(ip::address::from_string(conf_ptr_->bind_addr_), conf_ptr_->bind_port_);
    roo::log_warning("create listen endpoint for %s:%d",
                conf_ptr_->bind_addr_.c_str(), conf_ptr_->bind_port_);

    roo::log_info("socket/session conn cancel time_out: %d secs, enabled: %s",
             conf_ptr_->ops_cancel_time_out_,
             conf_ptr_->ops_cancel_time_out_ > 0 ? "true" : "false");

    if (conf_ptr_->service_speed_) {
        conf_ptr_->timed_feed_token_.reset(new steady_timer(io_service_)); // 1sec
        if (!conf_ptr_->timed_feed_token_) {
            roo::log_err("Create timed_feed_token_ failed!");
            return false;
        }

        conf_ptr_->timed_feed_token_->expires_from_now(boost::chrono::seconds(1));
        conf_ptr_->timed_feed_token_->async_wait(
            std::bind(&HttpConf::timed_feed_token_handler, conf_ptr_, std::placeholders::_1));
    }
    roo::log_info("http service enabled: %s, speed: %d tps",
             conf_ptr_->service_enabled_ ? "true" : "false",
             conf_ptr_->service_speed_);

    if (!io_service_threads_.init_threads(
            std::bind(&HttpServerImpl::io_service_run, this, std::placeholders::_1),
            conf_ptr_->io_thread_number_)) {
        roo::log_err("HttpServer::io_service_run init task failed!");
        return false;
    }

    // 执行任务分发使用
    if (!Dispatcher::instance().init()) {
        roo::log_err("Init HttpDispatcher failed.");
        return false;
    }

    // 注册配置动态更新的回调函数
    Global::instance().setting_ptr()->attach_runtime_callback(
        "tzhttpd-HttpServer",
        std::bind(&HttpServerImpl::module_runtime, this,
                  std::placeholders::_1));

    // 系统状态展示相关的初始化
    Global::instance().status_ptr()->attach_status_callback(
        "tzhttpd-HttpServer",
        std::bind(&HttpServerImpl::module_status, this,
                  std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));

    if (!system_manage_page_init()) {
        roo::log_err("init system manage page failed, treat as fatal.");
        return false;
    }

    roo::log_info("HttpServer initialized successfully!");
    return true;
}



// main task loop
void HttpServerImpl::io_service_run(roo::ThreadObjPtr ptr) {

    roo::log_warning("HttpServerImpl IoService %#lx work... ", (long)pthread_self());

    while (true) {

        if (unlikely(ptr->status_ == roo::ThreadStatus::kTerminating)) {
            roo::log_err("HttpServerImpl IoService %#lx terminating...", (long)pthread_self());
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
            roo::log_err("HttpServerImpl io_service %#lx stopped...", (long)pthread_self());
            break;
        }
    }

    ptr->status_ = roo::ThreadStatus::kDead;
    roo::log_warning("HttpServerImpl IoService %#lx terminated ... ", (long)pthread_self());

    return;
}

int HttpServerImpl::service_start() {

    io_service_threads_.start_threads();

    acceptor_.reset(new ip::tcp::acceptor(io_service_));
    acceptor_->open(ep_.protocol());

    acceptor_->set_option(ip::tcp::acceptor::reuse_address(true));
    acceptor_->bind(ep_);
    acceptor_->listen(conf_ptr_->backlog_size_ > 0 ? conf_ptr_->backlog_size_ : socket_base::max_connections);

    do_accept();

    return 0;
}

int HttpServerImpl::service_stop_graceful() {

    roo::log_err("about to stop io_service... ");

    io_service_.stop();
    io_service_threads_.graceful_stop_threads();
    return 0;
}

int HttpServerImpl::service_join() {

    roo::log_err("about to join io_service... ");

    io_service_threads_.join_threads();
    return 0;
}



void HttpServerImpl::do_accept() {

    auto sock_ptr = std::make_shared<ip::tcp::socket>(io_service_);
    if (!sock_ptr) {
        roo::log_err("create new socket for acceptor failed!");
        return;
    }

    acceptor_->async_accept(*sock_ptr,
                            std::bind(&HttpServerImpl::accept_handler, this,
                                      std::placeholders::_1, sock_ptr));
}

void HttpServerImpl::accept_handler(const boost::system::error_code& ec,
                                    std::shared_ptr<boost::asio::ip::tcp::socket> sock_ptr) {

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

        // 远程访问客户端的地址信息
        std::string remote_ip = remote.address().to_string(ignore_ec);
        roo::log_info("Remote Client Info: %s:%d", remote_ip.c_str(), remote.port());

        if (!conf_ptr_->check_safe_ip(remote_ip)) {
            roo::log_err("check safe_ip failed for: %s", remote_ip.c_str());

            sock_ptr->shutdown(boost::asio::socket_base::shutdown_both, ignore_ec);
            sock_ptr->close(ignore_ec);
            break;
        }

        if (!conf_ptr_->get_http_service_token()) {
            roo::log_err("request http service token failed, enabled: %s, speed: %d",
                    conf_ptr_->service_enabled_ ? "true" : "false", conf_ptr_->service_speed_);

            sock_ptr->shutdown(boost::asio::socket_base::shutdown_both, ignore_ec);
            sock_ptr->close(ignore_ec);
            break;
        }

        if (conf_ptr_->service_concurrency_ != 0 &&
            conf_ptr_->service_concurrency_ < TcpConnAsync::current_concurrency_) {
            roo::log_err("service_concurrency_ error, limit: %d, current: %d",
                    conf_ptr_->service_concurrency_, TcpConnAsync::current_concurrency_.load());
            sock_ptr->shutdown(boost::asio::socket_base::shutdown_both, ignore_ec);
            sock_ptr->close(ignore_ec);
            break;
        }

        std::shared_ptr<ConnType> new_conn = std::make_shared<ConnType>(sock_ptr, super_server_);

        new_conn->start();

    } while (0);

    // 再次启动接收异步请求
    do_accept();
}


int HttpServerImpl::module_status(std::string& module, std::string& key, std::string& value) {

    module = "tzhttpd";
    key = "http_server";

    std::stringstream ss;

    ss << "\t" << "instance_name: " << instance_name_ << std::endl;
    ss << "\t" << "service_addr: " << conf_ptr_->bind_addr_ << "@" << conf_ptr_->bind_port_ << std::endl;
    ss << "\t" << "backlog_size: " << conf_ptr_->backlog_size_ << std::endl;
    ss << "\t" << "io_thread_pool_size: " << conf_ptr_->io_thread_number_ << std::endl;
    ss << "\t" << "safe_ips: ";

    {
        // protect cfg race conditon
        __auto_lock__(conf_ptr_->lock_);
        for (auto iter = conf_ptr_->safe_ip_.begin(); iter != conf_ptr_->safe_ip_.end(); ++iter) {
            ss << *iter << ", ";
        }
        ss << std::endl;
    }

    ss << "\t" << std::endl;

    ss << "\t" << "service_enabled: " << (conf_ptr_->service_enabled_  ? "true" : "false") << std::endl;
    ss << "\t" << "service_speed(tps): " << conf_ptr_->service_speed_ << std::endl;
    ss << "\t" << "service_concurrency: " << conf_ptr_->service_concurrency_ << std::endl;
    ss << "\t" << "session_cancel_time_out: " << conf_ptr_->session_cancel_time_out_ << std::endl;
    ss << "\t" << "ops_cancel_time_out: " << conf_ptr_->ops_cancel_time_out_ << std::endl;

    value = ss.str();
    return 0;
}


int HttpServerImpl::module_runtime(const libconfig::Config& setting) {

    std::shared_ptr<HttpConf> conf_ptr = std::make_shared<HttpConf>();
    if (!conf_ptr || !conf_ptr->load_setting(setting)) {
        roo::log_err("create or load conf for HttpConf failed.");
        return -1;
    }

    if (conf_ptr_->session_cancel_time_out_ != conf_ptr->session_cancel_time_out_) {
        roo::log_warning("update session_cancel_time_out from %d to %d",
                    conf_ptr_->session_cancel_time_out_, conf_ptr->session_cancel_time_out_);
        conf_ptr_->session_cancel_time_out_ = conf_ptr->session_cancel_time_out_;
    }

    if (conf_ptr_->ops_cancel_time_out_ != conf_ptr->ops_cancel_time_out_) {
        roo::log_warning("update ops_cancel_time_out from %d to %d",
                    conf_ptr_->ops_cancel_time_out_, conf_ptr->ops_cancel_time_out_);
        conf_ptr_->ops_cancel_time_out_ = conf_ptr->ops_cancel_time_out_;
    }


    roo::log_warning("swap safe_ips...");

    {
        // protect cfg race conditon
        __auto_lock__(conf_ptr_->lock_);
        conf_ptr_->safe_ip_.swap(conf_ptr->safe_ip_);
    }

    if (conf_ptr_->service_speed_ != conf_ptr->service_speed_) {
        roo::log_warning("update http_service_speed from %d to %d",
                    conf_ptr_->service_speed_, conf_ptr->service_speed_);
        conf_ptr_->service_speed_ = conf_ptr->service_speed_;

        // 检查定时器是否存在
        if (conf_ptr_->service_speed_) {

            // 直接重置定时器，无论有没有
            conf_ptr_->timed_feed_token_.reset(new steady_timer(io_service_)); // 1sec
            if (!conf_ptr_->timed_feed_token_) {
                roo::log_err("Create timed_feed_token_ failed!");
                return -1;
            }

            conf_ptr_->timed_feed_token_->expires_from_now(boost::chrono::seconds(1));
            conf_ptr_->timed_feed_token_->async_wait(
                std::bind(&HttpConf::timed_feed_token_handler, conf_ptr_, std::placeholders::_1));
        } else { // speed == 0
            if (conf_ptr_->timed_feed_token_) {
                boost::system::error_code ignore_ec;
                conf_ptr_->timed_feed_token_->cancel(ignore_ec);
                conf_ptr_->timed_feed_token_.reset();
            }
        }
    }

    if (conf_ptr_->service_concurrency_ != conf_ptr->service_concurrency_) {
        roo::log_err("update service_concurrency from %d to %d.",
                conf_ptr_->service_concurrency_, conf_ptr->service_concurrency_);
        conf_ptr_->service_concurrency_ = conf_ptr->service_concurrency_;
    }

    roo::log_warning("http service enabled: %s, speed: %d", conf_ptr_->service_enabled_ ? "true" : "false",
                conf_ptr_->service_speed_);

    return 0;
}




////
// HttpServer Call Forward

HttpServer::HttpServer(const std::string& cfgfile, const std::string& instance_name) {

    ::signal(SIGPIPE, SIG_IGN);
    roo::Ssl_thread_setup();

    impl_ = make_unique<HttpServerImpl>(cfgfile, instance_name, *this);
    if (!impl_)
        throw roo::ConstructException("Create HttpServerImpl failed.");
}

HttpServer::~HttpServer() {
    roo::Ssl_thread_clean();
}


bool HttpServer::init() {
    return impl_->init();
}

int HttpServer::service_start() {
    return impl_->service_start();
}

int HttpServer::service_stop_graceful() {
    return impl_->service_stop_graceful();
}

int HttpServer::service_join() {
    return impl_->service_join();
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

int HttpServer::register_http_status_callback(const std::string& name, roo::StatusCallable func) {
    return Global::instance().status_ptr()->attach_status_callback(name, func);
}
int HttpServer::register_http_runtime_callback(const std::string& name, roo::SettingUpdateCallable func) {
    return Global::instance().setting_ptr()->attach_runtime_callback(name, func);
}
int HttpServer::update_http_runtime() {
    return Global::instance().setting_ptr()->update_runtime_setting();
}


int HttpServer::ops_cancel_time_out() const {
    return impl_->conf_ptr_->ops_cancel_time_out_;
}
int HttpServer::session_cancel_time_out() const {
    return impl_->conf_ptr_->session_cancel_time_out_;
}

boost::asio::io_service& HttpServer::io_service() const {
    return impl_->io_service_;
}

int HttpServer::module_runtime(const libconfig::Config& setting) {
    return impl_->module_runtime(setting);
}
int HttpServer::module_status(std::string& module, std::string& key, std::string& value) {
    return impl_->module_status(module, key, value);
}



} // end namespace tzhttpd
