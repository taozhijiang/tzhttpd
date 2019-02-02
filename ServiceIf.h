#ifndef __TZHTTPD_SERVICE_IF_H__
#define __TZHTTPD_SERVICE_IF_H__

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
    virtual int register_get_handler(const std::string& uri, const HttpGetHandler& handler) = 0;
    virtual int register_post_handler(const std::string& uri, const HttpPostHandler& handler) = 0;
};

} // end tzhttpd

#endif // __TZHTTPD_SERVICE_IF_H__
