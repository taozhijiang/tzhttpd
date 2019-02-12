#include "Dispatcher.h"
#include "HttpExecutor.h"

namespace tzhttpd {

Dispatcher& Dispatcher::instance() {
    static Dispatcher dispatcher;
    return dispatcher;
}

bool Dispatcher::init() {

    initialized_ = true;

    // 注册默认default vhost
    SAFE_ASSERT(!default_service_);

    // 创建io_service工作线程
    io_service_thread_ = boost::thread(std::bind(&Dispatcher::io_service_run, this));

    // 创建 default virtual host
    // http impl
    auto default_http_impl = std::make_shared<HttpExecutor>("[default]");
    if (!default_http_impl|| !default_http_impl->init()) {
        tzhttpd_log_err("create default http_impl failed.");
        return false;
    }

    // http executor
    default_service_.reset(new Executor(default_http_impl));
    Executor* executor = dynamic_cast<Executor *>(default_service_.get());

    SAFE_ASSERT(executor);
    if (!executor || !executor->init()) {
        tzhttpd_log_err("init default virtual host executor failed.");
        return false;
    }

    executor->executor_start();
    tzhttpd_log_debug("start default virtual host executor: %s success",  executor->instance_name().c_str());
    //

    for (auto iter = services_.begin(); iter != services_.end(); ++iter) {
        Executor* executor = dynamic_cast<Executor *>(iter->second.get());
        SAFE_ASSERT(executor);

        executor->executor_start();
        tzhttpd_log_debug("start virtual host executor for %s success",  executor->instance_name().c_str());
    }

    return true;
}


int Dispatcher::register_virtual_host(const std::string& hostname) {

    if (initialized_) {
        tzhttpd_log_err("Dispatcher has already been initialized, does not support dynamic registerService");
        return -1;
    }

    if (services_.find(hostname) != services_.end()) {
        tzhttpd_log_err("already found vhost %s, please check.", hostname.c_str());
        return -1;
    }

    // http impl
    auto default_http_impl = std::make_shared<HttpExecutor>(hostname);
    if (!default_http_impl || !default_http_impl->init()) {
        tzhttpd_log_err("create http_impl for %s failed.", hostname.c_str());
        return -1;
    }

    auto default_http = std::make_shared<Executor>(default_http_impl);
    if (!default_http || !default_http->init()) {
        tzhttpd_log_err("create http_executor for %s failed.", hostname.c_str());
        return -1;
    }

    services_[hostname] = default_http;
    tzhttpd_log_debug("successful register service %s ", default_http->instance_name().c_str());

    return 0;
}


int Dispatcher::register_http_get_handler(const std::string& hostname, const std::string& uri_regex,
                                          const HttpGetHandler& handler) {

    std::shared_ptr<ServiceIf> service;
    auto it = services_.find(hostname);
    if (it != services_.end()) {
        service = it->second;
    }

    if (!service) {
        tzhttpd_log_notice("hostname %s not found, register handler to default",  hostname.c_str());
        service = default_service_;
    }

    return service->register_get_handler(uri_regex, handler);
}

int Dispatcher::register_http_post_handler(const std::string& hostname, const std::string& uri_regex,
                                           const HttpPostHandler& handler) {

    std::shared_ptr<ServiceIf> service;
    auto it = services_.find(hostname);
    if (it != services_.end()) {
        service = it->second;
    }

    if (!service) {
        tzhttpd_log_notice("hostname %s not found, register handler to default",  hostname.c_str());
        service = default_service_;
    }

    return service->register_post_handler(uri_regex, handler);
}


} // tzrpc

