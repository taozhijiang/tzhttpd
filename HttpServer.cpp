#include <iostream>
#include <sstream>

#include <boost/format.hpp>
#include <thread>
#include <functional>

#include "HttpHandler.h"
#include "HttpServer.h"

#include "Log.h"

namespace tzhttpd {

static const size_t bucket_size_ = 0xFF;
static size_t bucket_hash_index_call(const std::shared_ptr<ConnType>& ptr) {
    return std::hash<ConnType *>()(ptr.get());
}

HttpServer::HttpServer(const std::string& address, unsigned short port, size_t t_size) :
    io_service_(),
    ep_(ip::tcp::endpoint(ip::address::from_string(address), port)),
    acceptor_(),
    conf_({}),
    conns_alive_("TcpConnAsync"),
    io_service_threads_(static_cast<uint8_t>(t_size)) {

}

bool HttpServer::init() {

    #if 0
    if (!get_config_value("http.docu_root", conf_.docu_root_)) {
        log_err("get http.docu_root failed!");
        return false;
    }

    std::string docu_index;
    if (!get_config_value("http.docu_index", docu_index)) {
        log_err("get http.docu_index failed!");
        return false;
    }
    std::vector<std::string> vec {};
    boost::split(vec, docu_index, boost::is_any_of(";"));
    for (auto iter = vec.begin(); iter != vec.cend(); ++ iter){
        std::string tmp = boost::trim_copy(*iter);
        if (tmp.empty())
            continue;

        conf_.docu_index_.push_back(tmp);
    }
    if (conf_.docu_index_.empty()) {
        log_err("empty valid docu_index found, previous: %s", docu_index.c_str());
        return false;
    }

    if (!get_config_value("http.conn_time_out", conf_.conn_time_out_) ||
        !get_config_value("http.conn_time_out_linger", conf_.conn_time_out_linger_)) {
        log_err("get http conn_time_out & linger configure value error, using default.");
        conf_.conn_time_out_ = 5 * 60;
        conf_.conn_time_out_linger_ = 10;
    }

    log_debug("socket/session conn time_out: %ds, linger: %ds", conf_.conn_time_out_, conf_.conn_time_out_linger_);
    conns_alive_.init(std::bind(&HttpServer::conn_destroy, this, std::placeholders::_1),
                                  conf_.conn_time_out_, conf_.conn_time_out_linger_);

    if (!get_config_value("http.ops_cancel_time_out", conf_.ops_cancel_time_out_)){
        log_err("get http ops_cancel_time_out configure value error, using default.");
        conf_.ops_cancel_time_out_ = 0;
    }
    if (conf_.ops_cancel_time_out_ < 0) {
        conf_.ops_cancel_time_out_ = 0;
    }
    log_debug("socket/session conn cancel time_out: %d, enabled: %s", conf_.ops_cancel_time_out_,
              conf_.ops_cancel_time_out_ > 0 ? "true" : "false");

    if (!get_config_value("http.service_enable", conf_.http_service_enabled_) ||
        !get_config_value("http.service_speed", conf_.http_service_speed_)){
        log_err("get http service enable/speed configure value error, using default.");
        conf_.http_service_enabled_ = true;
        conf_.http_service_speed_ = 0;
    }
    if (conf_.http_service_speed_ < 0) {
        conf_.http_service_speed_ = 0;
    }

    if (conf_.http_service_speed_ &&
        (helper::register_timer_task( std::bind(&HttpConf::feed_http_service_token, &conf_), 5*1000, true, true) == 0) )
    {
        log_err("register http token feed task failed!");
        return false;
    }
    log_debug("http service enabled: %s, speed: %ld", conf_.http_service_enabled_ ? "true" : "false",
              conf_.http_service_speed_);


    if (!io_service_threads_.init_threads(std::bind(&HttpServer::io_service_run, shared_from_this(), std::placeholders::_1))) {
        log_err("HttpServer::io_service_run init task failed!");
        return false;
    }


    // customize route uri handler
    register_http_get_handler("/", http_handler::index_http_get_handler);
    register_http_get_handler("/stat", http_handler::event_stat_http_get_handler);

    register_http_get_handler("/ev_query", http_handler::get_ev_query_handler);
    register_http_post_handler("/ev_submit", http_handler::post_ev_submit_handler);

    register_http_get_handler("/test", http_handler::get_test_handler);
    register_http_post_handler("/test", http_handler::post_test_handler);

    if (helper::register_timer_task( std::bind(&AliveTimer<ConnType>::clean_up, &conns_alive_), 5*1000, true, false) == 0) {
        log_err("Register alive conn purge task failed!");
        return false;
    }
#endif

    return true;
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
            log_err("request http service token failed, enabled: %s, speed: %ld", conf_.http_service_enabled_ ? "true" : "false", conf_.http_service_speed_);

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

int HttpServer::register_http_post_handler(std::string uri, HttpPostHandler handler){

    uri = boost::algorithm::trim_copy(boost::to_lower_copy(uri));
    while (uri[uri.size()-1] == '/' && uri.size() > 1)  // 全部的大写字母，去除尾部 /
        uri = uri.substr(0, uri.size()-1);

    std::map<std::string, HttpPostHandler>::const_iterator it;
    for (it = http_post_handler_.cbegin(); it!=http_post_handler_.cend(); ++it) {
        if (boost::iequals(uri, it->first))
            log_err("Handler for %s already exists, override it!", uri.c_str());
    }

    http_post_handler_[uri] = handler;
    return 0;
}


int HttpServer::find_http_post_handler(std::string uri, HttpPostHandler& handler){

    uri = boost::algorithm::trim_copy(boost::to_lower_copy(uri));
    while (uri[uri.size()-1] == '/' && uri.size() > 1)  // 全部的小写字母，去除尾部 /
        uri = uri.substr(0, uri.size()-1);

    std::map<std::string, HttpPostHandler>::const_iterator it;
    for (it = http_post_handler_.cbegin(); it!=http_post_handler_.cend(); ++it) {
        if (boost::iequals(uri, it->first)){
            handler = it->second;
            return 0;
        }
    }

    return -1;
}

int HttpServer::register_http_get_handler(std::string uri, HttpGetHandler handler){

    uri = boost::algorithm::trim_copy(boost::to_lower_copy(uri));
    while (uri[uri.size()-1] == '/' && uri.size() > 1)  // 全部的大写字母，去除尾部 /
        uri = uri.substr(0, uri.size()-1);

    std::map<std::string, HttpGetHandler>::const_iterator it;
    for (it = http_get_handler_.cbegin(); it!=http_get_handler_.cend(); ++it) {
        if (boost::iequals(uri, it->first))
            log_err("Handler for %s already exists, override it!", uri.c_str());
    }

    http_get_handler_[uri] = handler;
    return 0;
}


int HttpServer::find_http_get_handler(std::string uri, HttpGetHandler& handler){

    uri = boost::algorithm::trim_copy(boost::to_lower_copy(uri));
    while (uri[uri.size()-1] == '/' && uri.size() > 1)  // 全部的小写字母，去除尾部 /
        uri = uri.substr(0, uri.size()-1);

    std::map<std::string, HttpGetHandler>::const_iterator it;
    for (it = http_get_handler_.cbegin(); it!=http_get_handler_.cend(); ++it) {
        if (boost::iequals(uri, it->first)){
            handler = it->second;
            return 0;
        }
    }

    return -1;
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
