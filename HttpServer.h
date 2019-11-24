/*-
 * Copyright (c) 2018-2019 TAO Zhijiang<taozhijiang@gmail.com>
 *
 * Licensed under the BSD-3-Clause license, see LICENSE for full information.
 *
 */

#ifndef __TZHTTPD_HTTP_SERVER_H__
#define __TZHTTPD_HTTP_SERVER_H__

#include <scaffold/Status.h>
#include <scaffold/Setting.h>

#include "HttpHandler.h"

namespace tzhttpd {

class HttpServerImpl;

class HttpServer {

    __noncopyable__(HttpServer)

public:

    //
    // ALL AVAILABLE PUBLIC API CALL HERE
    //

    /// Construct the server to listen on the specified TCP address and port
    explicit HttpServer(const std::string& cfgfile, const std::string& instance_name);
    ~HttpServer();

    // 先构造，再增加主机、handler等信息，再调用该初始化
    bool init();

    int service_start();
    int service_join();
    int service_stop_graceful();

    // Proxy to Dispatcher ...
    int add_http_vhost(
        const std::string& hostname);
    int add_http_get_handler(
        const std::string& uri_regex, const HttpGetHandler& handler,
        bool built_in = false, const std::string hostname = "");
    int add_http_post_handler(
        const std::string& uri_regex, const HttpPostHandler& handler,
        bool built_in = false, const std::string hostname = "");

    // Proxy to Global ...
    int register_http_status_callback(const std::string& name, roo::StatusCallable func);
    int register_http_runtime_callback(const std::string& name, roo::SettingUpdateCallable func);
    int update_http_runtime();

public:

    int ops_cancel_time_out() const;
    int session_cancel_time_out() const;
    boost::asio::io_service& io_service() const;

    int module_runtime(const libconfig::Config& setting);
    int module_status(std::string& module, std::string& key, std::string& value);

private:
    std::shared_ptr<HttpServerImpl> impl_;
};


} // end namespace tzhttpd

#endif //__TZHTTPD_HTTP_SERVER_H__
