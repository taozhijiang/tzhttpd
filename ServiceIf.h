#ifndef __TZHTTPD_SERVICE_IF_H__
#define __TZHTTPD_SERVICE_IF_H__

#include <libconfig.h++>
#include <boost/noncopyable.hpp>

#include <string>
#include "HttpHandler.h"

// real http vhost should implement this interface class

namespace tzhttpd {

class HttpReqInstance;


class ServiceIf {

public:
    ServiceIf() { }
    ~ServiceIf() { }

    // 根据opCode分发rpc请求的处理
    virtual void handle_http_request(std::shared_ptr<HttpReqInstance> http_req_instance) = 0;
    virtual std::string instance_name() = 0;

    //
    virtual int add_get_handler(const std::string& uri, const HttpGetHandler& handler, bool built_in) = 0;
    virtual int add_post_handler(const std::string& uri, const HttpPostHandler& handler, bool built_in) = 0;

    virtual bool exist_handler(const std::string& uri_regex, enum HTTP_METHOD method) = 0;
    virtual int drop_handler(const std::string& uri_regex, enum HTTP_METHOD method) = 0;


    // 收集模块的状态信息
    virtual int module_status(std::string& strModule, std::string& strKey, std::string& strValue) = 0;
    virtual int update_runtime_conf(const libconfig::Config& cfg) = 0;
};

} // end tzhttpd

#endif // __TZHTTPD_SERVICE_IF_H__
