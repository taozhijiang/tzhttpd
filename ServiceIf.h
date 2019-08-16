/*-
 * Copyright (c) 2018-2019 TAO Zhijiang<taozhijiang@gmail.com>
 *
 * Licensed under the BSD-3-Clause license, see LICENSE for full information.
 *
 */

#ifndef __TZHTTPD_SERVICE_IF_H__
#define __TZHTTPD_SERVICE_IF_H__

#include <string>
#include <scaffold/Setting.h>

#include "HttpHandler.h"

// real http vhost should implement this interface class

namespace tzhttpd {

class HttpReqInstance;


class ServiceIf {

    __noncopyable__(ServiceIf)

public:
    ServiceIf() = default;
    ~ServiceIf() = default;

    // 根据opCode分发rpc请求的处理
    virtual void handle_http_request(std::shared_ptr<HttpReqInstance> http_req_instance) = 0;
    virtual std::string instance_name() = 0;

    //
    virtual int add_get_handler(const std::string& uri, const HttpGetHandler& handler, bool built_in) = 0;
    virtual int add_post_handler(const std::string& uri, const HttpPostHandler& handler, bool built_in) = 0;

    virtual bool exist_handler(const std::string& uri_regex, enum HTTP_METHOD method) = 0;
    virtual int drop_handler(const std::string& uri_regex, enum HTTP_METHOD method) = 0;


    // 收集模块的状态信息
    virtual int module_status(std::string& module, std::string& key, std::string& value) = 0;
    virtual int module_runtime(const libconfig::Config& cfg) = 0;
};

} // end namespace tzhttpd

#endif // __TZHTTPD_SERVICE_IF_H__
