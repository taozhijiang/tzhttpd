/*-
 * Copyright (c) 2018-2019 TAO Zhijiang<taozhijiang@gmail.com>
 *
 * Licensed under the BSD-3-Clause license, see LICENSE for full information.
 *
 */

#ifndef __TZHTTPD_DISPATCHER_H__
#define __TZHTTPD_DISPATCHER_H__

#include <xtra_rhel.h>

#include <cinttypes>
#include <memory>

#include <map>
#include <mutex>

#include "HttpHandler.h"

namespace tzhttpd {

class HttpReqInstance;
class Executor;

class Dispatcher {

    __noncopyable__(Dispatcher)

public:
    static Dispatcher& instance();

    bool init();

    void handle_http_request(std::shared_ptr<HttpReqInstance> http_req_instance);

    // 注册虚拟主机
    int add_virtual_host(const std::string& hostname);

    // 外部注册http handler的接口
    int add_http_get_handler(const std::string& hostname, const std::string& uri_regex,
                             const HttpGetHandler& handler, bool built_in);
    int add_http_post_handler(const std::string& hostname, const std::string& uri_regex,
                              const HttpPostHandler& handler, bool built_in);

    int drop_http_handler(const std::string& hostname, const std::string& uri_regex, enum HTTP_METHOD method);

    int module_runtime(const libconfig::Config& conf);

private:

    Dispatcher() :
        initialized_(false),
        services_({ }) {
    }

    ~Dispatcher() = default;

    bool initialized_;

    // 系统在启动的时候进行注册初始化，然后再提供服务，所以
    // 这边就不使用锁结构进行保护了，防止影响性能
    std::map<std::string, std::shared_ptr<Executor>> services_;

    // 默认的http虚拟主机
    std::shared_ptr<Executor> default_service_;
};

} // end namespace tzhttpd


#endif // __TZHTTPD_SERVICE_DISPATCHER_H__
