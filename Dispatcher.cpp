#include "Dispatcher.h"

namespace tzhttpd {

Dispatcher& Dispatcher::instance() {
    static Dispatcher dispatcher;
    return dispatcher;
}


int Dispatcher::register_virtual_host(const std::string& hostname, std::shared_ptr<ServiceIf> service) {

    if (initialized_) {
        tzhttpd_log_err("Dispatcher has already been initialized, does not support dynamic registerService");
        return -1;
    }

    auto http_service = std::make_shared<Executor>(service);
    if (!http_service->init()) {
        tzhttpd_log_err("service %s init failed.", service->instance_name().c_str());
        return -2;
    }

    services_[hostname] = http_service;
    tzhttpd_log_debug("successful register service %s ", service->instance_name().c_str());

    return true;
}


int Dispatcher::register_http_get_handler(const std::string& hostname, const HttpGetHandler& handler) {

    std::shared_ptr<ServiceIf> service;
    auto it = services_.find(hostname);
    if (it != services_.end()) {
        service = it->second;
    }

    if (!service) {
        service = default_service_;
    }

    return service->register_get_handler(handler);
}

int Dispatcher::register_http_post_handler(const std::string& hostname, const HttpPostHandler& handler) {

    std::shared_ptr<ServiceIf> service;
    auto it = services_.find(hostname);
    if (it != services_.end()) {
        service = it->second;
    }

    if (!service) {
        service = default_service_;
    }

    return service->register_post_handler(handler);
}


} // tzrpc

