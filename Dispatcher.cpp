/*-
 * Copyright (c) 2018-2019 TAO Zhijiang<taozhijiang@gmail.com>
 *
 * Licensed under the BSD-3-Clause license, see LICENSE for full information.
 *
 */

#include "ConfHelper.h"
#include "HttpExecutor.h"

#include "Dispatcher.h"

namespace tzhttpd {

Dispatcher& Dispatcher::instance() {
    static Dispatcher dispatcher;
    return dispatcher;
}

bool Dispatcher::init() {

    initialized_ = true;

    // 注册默认default vhost
    SAFE_ASSERT(!default_service_);

    // 创建默认虚拟主机 default virtual host
    auto default_http_impl = std::make_shared<HttpExecutor>("[default]");

    // HttpExecutor层次的初始化，包括了虚拟主机配置文件的解析
    // 同时Executor需要的配置信息通过ExecuteConf传递过来
    if (!default_http_impl|| !default_http_impl->init()) {
        tzhttpd_log_err("create and initialize HttpExecutor for host [default] failed.");
        return false;
    }

    // http executor
    default_service_.reset(new Executor(default_http_impl));
    // 进行业务层无关的初始化，比如创建工作线程组，维护请求队列等
    if (!default_service_|| !default_service_->init()) {
        tzhttpd_log_err("create and initialize host [default] executor failed.");
        return false;
    }

    default_service_->executor_start();
    tzhttpd_log_debug("start host [default] Executor: %s success",  default_service_->instance_name().c_str());


    // HttpExecutor和Executor在register_virtual_host的时候已经进行初始化(并成功)了
    // 此处开启虚拟主机的工作线程组，开始接收请求
    for (auto iter = services_.begin(); iter != services_.end(); ++iter) {
        auto executor = iter->second;
        executor->executor_start();
        tzhttpd_log_debug("start Executor for host %s success",  executor->instance_name().c_str());
    }

    // 注册配置动态配置更新接口，由此处分发到各个虚拟主机，不再每个虚拟主机自己注册
    ConfHelper::instance().register_conf_callback(
            std::bind(&Dispatcher::update_runtime_conf, this,
                      std::placeholders::_1));


    return true;
}


int Dispatcher::add_virtual_host(const std::string& hostname) {

    if (initialized_) {
        tzhttpd_log_err("Dispatcher has already been initialized, but we does not support dynamic addService");
        return -1;
    }

    if (services_.find(hostname) != services_.end()) {
        tzhttpd_log_err("already found host %s added, please check.", hostname.c_str());
        return -1;
    }

    // 此处加载HTTP的相关配置
    auto default_http_impl = std::make_shared<HttpExecutor>(hostname);
    if (!default_http_impl || !default_http_impl->init()) {
        tzhttpd_log_err("create and initialize HttpExecutor for host %s failed.", hostname.c_str());
        return -1;
    }

    auto default_http = std::make_shared<Executor>(default_http_impl);
    if (!default_http || !default_http->init()) {
        tzhttpd_log_err("create and initialize Executor for host %s failed.", hostname.c_str());
        return -1;
    }

    services_[hostname] = default_http;
    tzhttpd_log_debug("successful add service %s ", default_http->instance_name().c_str());

    return 0;
}


int Dispatcher::add_http_get_handler(const std::string& hostname, const std::string& uri_regex,
                                     const HttpGetHandler& handler, bool built_in) {

    std::shared_ptr<Executor> service;

    if (hostname.empty() || hostname == "[default]") {
        service = default_service_;
    } else {
        auto it = services_.find(hostname);
        if (it != services_.end()) {
            service = it->second;
        } else {
            tzhttpd_log_err("hostname %s not found.",  hostname.c_str());
            return -1;
        }
    }

    SAFE_ASSERT(service);

    return service->add_get_handler(uri_regex, handler, built_in);
}

int Dispatcher::add_http_post_handler(const std::string& hostname, const std::string& uri_regex,
                                      const HttpPostHandler& handler, bool built_in) {

    std::shared_ptr<Executor> service;

    if (hostname.empty() || hostname == "[default]") {
        service = default_service_;
    } else {
        auto it = services_.find(hostname);
        if (it != services_.end()) {
            service = it->second;
        } else {
            tzhttpd_log_err("hostname %s not found.",  hostname.c_str());
            return -1;
        }
    }

    SAFE_ASSERT(service);
    return service->add_post_handler(uri_regex, handler, built_in);
}

int Dispatcher::drop_http_handler(const std::string& hostname, const std::string& uri_regex, enum HTTP_METHOD method) {

    std::shared_ptr<Executor> service;

    if (hostname.empty() || hostname == "[default]") {
        service = default_service_;
    } else {
        auto it = services_.find(hostname);
        if (it != services_.end()) {
            service = it->second;
        } else {
            tzhttpd_log_err("hostname %s not found.",  hostname.c_str());
            return -1;
        }
    }

    SAFE_ASSERT(service);
    return service->drop_handler(uri_regex, method);
}


// 依次调用触发进行默认、其他虚拟主机的配置更新
int Dispatcher::update_runtime_conf(const libconfig::Config& conf) {

    int ret_sum = 0;
    int ret = 0;

    tzhttpd_log_notice("update_runtime_conf begin to handle host [default].");
    ret = default_service_->update_runtime_conf(conf);
    tzhttpd_log_notice("update_runtime_conf for host [default] return: %d", ret);
    ret_sum += ret;

    for (auto iter = services_.begin(); iter != services_.end(); ++iter) {

        auto executor = iter->second;
        ret = executor->update_runtime_conf(conf);
        tzhttpd_log_notice("update_runtime_conf for host %s return: %d",
                           executor->instance_name().c_str(), ret);
        ret_sum += ret;
    }

    return ret_sum;
}


} // end namespace tzhttpd

