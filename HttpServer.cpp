/*-
 * Copyright (c) 2018 TAO Zhijiang<taozhijiang@gmail.com>
 *
 * Licensed under the BSD-3-Clause license, see LICENSE for full information.
 *
 */

#include <iostream>
#include <sstream>
#include <thread>
#include <functional>

#include <boost/format.hpp>

#include "HttpCfgHelper.h"
#include "TCPConnAsync.h"
#include "HttpHandler.h"
#include "HttpServer.h"

#include "SslSetup.h"
#include "Log.h"

namespace tzhttpd {

namespace http_handler {
// init only once at startup
extern std::string              http_server_version;
extern std::string              http_docu_root;
extern std::vector<std::string> http_docu_index;
} // end namespace http_handler

static const size_t bucket_size_ = 0xFF;
static size_t bucket_hash_index_call(const std::shared_ptr<ConnType>& ptr) {
    return std::hash<ConnType *>()(ptr.get());
}


// init http_doc_root/index, once init can guarantee no change it anymore,
// work with them without protection
std::once_flag http_docu_once;
void init_http_docu(const std::string& server_version, const std::string& docu_root, const std::vector<std::string>& docu_index) {
    log_alert("Updating http docu root->%s, index_size: %d", docu_root.c_str(),
              static_cast<int>(docu_index.size()));

    http_handler::http_server_version = server_version;
    http_handler::http_docu_root = docu_root;
    http_handler::http_docu_index = docu_index;
}


bool HttpConf::load_config(const libconfig::Config& cfg) {

    int listen_port = 0;
    if (!cfg.lookupValue("http.bind_addr", bind_addr_) || !cfg.lookupValue("http.listen_port", listen_port) ){
        log_err( "get http.bind_addr & http.listen_port error");
        return false;
    }
    listen_port_ = static_cast<unsigned short>(listen_port);

    if (!cfg.lookupValue("http.thread_pool_size", io_thread_number_)) {
        io_thread_number_ = 8;
        fprintf(stderr, "Using default thread_pool size: 8");
    }

    std::string server_version;
    if (!cfg.lookupValue("http.version", server_version)) {
        log_err("get http.version failed!");
    }

    std::string docu_root;
    if (!cfg.lookupValue("http.docu_root", docu_root)) {
        log_err("get http.docu_root failed!");
    }

    std::string str_docu_index;
    std::vector<std::string> docu_index {};
    if (!cfg.lookupValue("http.docu_index", str_docu_index)) {
        log_err("get http.docu_index failed!");
    } else  {
        std::vector<std::string> vec {};
        boost::split(vec, str_docu_index, boost::is_any_of(";"));
        for (auto iter = vec.begin(); iter != vec.cend(); ++ iter){
            std::string tmp = boost::trim_copy(*iter);
            if (tmp.empty())
                continue;

            docu_index.push_back(tmp);
        }
        if (docu_index.empty()) {
            log_err("empty valid docu_index found, previous: %s", str_docu_index.c_str());
        }
    }

    // once init
    if (!server_version.empty() && !docu_root.empty() && !docu_index.empty()) {
        std::call_once(http_docu_once, init_http_docu, server_version, docu_root, docu_index);
    }

    // other http parameters

    if (!cfg.lookupValue("http.conn_time_out", conn_time_out_) ||
        !cfg.lookupValue("http.conn_time_out_linger", conn_time_out_linger_)) {
        log_err("get http conn_time_out & linger configure value error, using default.");
        conn_time_out_ = 5 * 60;
        conn_time_out_linger_ = 10;
    }

    if (!cfg.lookupValue("http.ops_cancel_time_out", ops_cancel_time_out_)){
        log_err("get http ops_cancel_time_out configure value error, using default.");
        ops_cancel_time_out_ = 0;
    }
    if (ops_cancel_time_out_ < 0) {
        ops_cancel_time_out_ = 0;
    }

    if (!cfg.lookupValue("http.service_enable", http_service_enabled_) ||
        !cfg.lookupValue("http.service_speed", http_service_speed_)){
        log_err("get http service enable/speed configure value error, using default.");
        http_service_enabled_ = true;
        http_service_speed_ = 0;
    }
    if (http_service_speed_ < 0) {
        http_service_speed_ = 0;
    }

    log_debug("HttpConf parse cfgfile %s OK!", HttpCfgHelper::instance().get_cfgfile().c_str());

    return true;
}

void HttpConf::timed_feed_token_handler(const boost::system::error_code& ec) {

    if (http_service_speed_ == 0) {
        log_alert("unlock speed jail, close the timer.");
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

    if (!Ssl_thread_setup) {
        log_err("Ssl_thread_setup failed!");
        return false;
    }

    libconfig::Config cfg;

    std::string cfgfile = HttpCfgHelper::instance().get_cfgfile();
    try {
        cfg.readFile(cfgfile.c_str());
    } catch(libconfig::FileIOException &fioex) {
        fprintf(stderr, "I/O error while reading file: %s.", cfgfile.c_str());
        return false;
    } catch(libconfig::ParseException &pex) {
        fprintf(stderr, "Parse error at %d - %s", pex.getLine(), pex.getError());
        return false;
    }

    if (!conf_.load_config(cfg)) {
        log_err("Load cfg failed!");
        return false;
    }

    ep_ = ip::tcp::endpoint(ip::address::from_string(conf_.bind_addr_), conf_.listen_port_);
    log_alert("create listen endpoint for %s:%d", conf_.bind_addr_.c_str(), conf_.listen_port_);

    log_debug("socket/session conn time_out: %ds, linger: %ds",
              conf_.conn_time_out_, conf_.conn_time_out_linger_);
    conns_alive_.init(std::bind(&HttpServer::conn_destroy, this, std::placeholders::_1),
                              conf_.conn_time_out_, conf_.conn_time_out_linger_);

    log_debug("socket/session conn cancel time_out: %d, enabled: %s", conf_.ops_cancel_time_out_,
              conf_.ops_cancel_time_out_ > 0 ? "true" : "false");

    if (conf_.http_service_speed_) {
        conf_.timed_feed_token_.reset(new boost::asio::deadline_timer (io_service_,
                                              boost::posix_time::millisec(5000))); // 5sec
        if (!conf_.timed_feed_token_) {
            log_err("Create timed_feed_token_ failed!");
            return false;
        }

        conf_.timed_feed_token_->async_wait(
            std::bind(&HttpConf::timed_feed_token_handler, &conf_, std::placeholders::_1));
    }
    log_debug("http service enabled: %s, speed: %ld", conf_.http_service_enabled_ ? "true" : "false",
              conf_.http_service_speed_);

    if (!io_service_threads_.init_threads(
        std::bind(&HttpServer::io_service_run, shared_from_this(), std::placeholders::_1),
        conf_.io_thread_number_)) {
        log_err("HttpServer::io_service_run init task failed!");
        return false;
    }

    timed_checker_.reset(new boost::asio::deadline_timer (io_service_,
                                              boost::posix_time::millisec(5000))); // 5sec
    if (!timed_checker_) {
        log_err("Create timed_checker_ failed!");
        return false;
    }
    timed_checker_->async_wait(
        std::bind(&HttpServer::timed_checker_handler, shared_from_this(), std::placeholders::_1));

    if (HttpCfgHelper::instance().register_cfg_callback(
            std::bind(&HttpServer::update_run_cfg, shared_from_this(), std::placeholders::_1 )) != 0) {
        log_err("HttpServer register cfg callback failed!");
        return false;
    }

    // 固定的管理页面地址
    if (register_http_get_handler("/manage", http_handler::manage_http_get_handler) != 0) {
        log_err("HttpServer register manage page failed!");
        return false;
    }

    // load cgi_handlers
    int ret_code = handler_.update_run_cfg(cfg);
    if (ret_code != 0) {
        log_err("register cgi-handler return %d", ret_code);
    }

    return true;
}

int HttpServer::update_run_cfg(const libconfig::Config& cfg) {

    HttpConf conf {};
    if (!conf.load_config(cfg)) {
        log_err("Load cfg failed!");
        return -1;
    }

    if (conf.ops_cancel_time_out_ != conf_.ops_cancel_time_out_) {
        log_alert("===> update socket/session conn cancel time_out: from %d to %d",
                  conf_.ops_cancel_time_out_, conf.ops_cancel_time_out_);
        conf_.ops_cancel_time_out_ = conf.ops_cancel_time_out_;
    }

    // 注意，一旦关闭消费，所有的URI请求都会被拒绝掉，除了manage管理页面可用
    if (conf.http_service_enabled_ != conf_.http_service_enabled_) {
        log_alert("===> update http_service_enabled: from %d to %d",
                  conf_.http_service_enabled_, conf.http_service_enabled_);
        conf_.http_service_enabled_ = conf.http_service_enabled_;
    }

    if (conf.http_service_speed_ != conf_.http_service_speed_ ) {

        log_alert("===> update http_service_speed: from %d to %d",
                  conf_.http_service_speed_, conf.http_service_speed_);
        conf_.http_service_speed_ = conf.http_service_speed_;

        if (conf.http_service_speed_) { // 首次启用
            if (! conf_.timed_feed_token_) {
                conf_.timed_feed_token_.reset(new boost::asio::deadline_timer (io_service_,
                                                      boost::posix_time::millisec(5000))); // 5sec
                if (!conf_.timed_feed_token_) {
                    log_err("create timed_feed_token_ failed!");
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
        log_alert("===> resize io_thread_num from %d to %d", conf_.io_thread_number_, conf.io_thread_number_);
        conf_.io_thread_number_ = conf.io_thread_number_;
        if (io_service_threads_.resize_threads(conf_.io_thread_number_) != 0) {
            log_err("resize io_thread_num may failed!");
            return -3;
        }
    }

    // reload cgi-handlers
    int ret_code = handler_.update_run_cfg(cfg);
    if (ret_code != 0) {
        log_err("register cgi-handler return %d", ret_code);
    }

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

    log_info("HttpServer io_service thread %#lx is about to work... ", (long)pthread_self());

    while (true) {

        if (unlikely(ptr->status_ == ThreadStatus::kThreadTerminating)) {
            log_err("thread %#lx is about to terminating...", (long)pthread_self());
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
            log_err("io_service stopped...");
            break;
        }
    }

    ptr->status_ = ThreadStatus::kThreadDead;
    log_info("HttpServer io_service thread %#lx is about to terminate ... ", (long)pthread_self());

    return;
}

void HttpServer::service() {

    acceptor_.reset( new ip::tcp::acceptor(io_service_) );
    acceptor_->open(ep_.protocol());

    acceptor_->set_option(ip::tcp::acceptor::reuse_address(true));
    acceptor_->bind(ep_);
    acceptor_->listen();

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
            log_err("Error during accept with %d, %s", ec, ec.message().c_str());
            break;
        }

        boost::system::error_code ignore_ec;
        std::stringstream output;
        auto remote = sock_ptr->remote_endpoint(ignore_ec);
        if (ignore_ec) {
            log_err("get remote info failed:%d, %s", ignore_ec, ignore_ec.message().c_str());
            break;
        }

        output << "Client Info-> " << remote.address() << ":" << remote.port();
        log_debug(output.str().c_str());

        if (!conf_.get_http_service_token()) {
            log_err("request http service token failed, enabled: %s, speed: %ld",
                    conf_.http_service_enabled_ ? "true" : "false", conf_.http_service_speed_);

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
    p_conn->sock_shutdown(ShutdownType::kShutdownBoth);
    p_conn->sock_close();
    return 0;
}


int HttpServer::io_service_stop_graceful() {
    log_err("About to stop io_service... ");
    io_service_.stop();
    io_service_threads_.graceful_stop_threads();
    return 0;
}

int HttpServer::io_service_join() {
    io_service_threads_.join_threads();
    return 0;
}

} // end namespace tzhttpd
