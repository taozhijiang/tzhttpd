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

#include "HttpVhost.h"
#include "HttpParser.h"
#include "HttpProto.h"
#include "HttpCfgHelper.h"

#include "Log.h"

namespace tzhttpd {

using namespace tzhttpd::http_proto;

int HttpVhost::internal_manage_http_get_handler(const HttpParser& http_parser, std::string& response,
                                                std::string& status_line, std::vector<std::string>& add_header) {

    const UriParamContainer& params = http_parser.get_request_uri_params();
    if (params.EMPTY() || !params.EXIST("cmd") || !params.EXIST("auth")) {
        tzhttpd_log_err("manage page param check failed!");
        response = http_proto::content_bad_request;
        status_line = generate_response_status_line(http_parser.get_version(),
                                                    StatusCode::client_error_bad_request);
        return 0;
    }

    if (params.VALUE("auth") != "d44bfc666db304b2f72b4918c8b46f78") {
        tzhttpd_log_err("auth check failed!");
        response = http_proto::content_forbidden;
        status_line = generate_response_status_line(http_parser.get_version(),
                                                    StatusCode::client_error_forbidden);
        return 0;
    }

    std::string cmd = params.VALUE("cmd");
    int ret = 0;

    if (cmd == "reload") {

        // 配置文件动态更新
        tzhttpd_log_debug("do configure reconfigure ....");
        ret = HttpCfgHelper::instance().update_runtime_cfg();

    } else if (cmd == "switch_handler") {

        // 开关 uri
        std::string vhost = params.VALUE("vhost");
        std::string uri_r = params.VALUE("path");
        std::string method = params.VALUE("method");
        std::string enable = params.VALUE("enable");

        bool on = boost::iequals(enable, "off") ? false : true;

        if (boost::iequals(method, "GET")) {
            ret = switch_http_get_handler(vhost, uri_r, on);
        } else if (boost::iequals(method, "POST")) {
            ret = switch_http_post_handler(vhost, uri_r, on);
        } else {
            tzhttpd_log_err("Unknown method %s for switch_handler with uri: %s",
                            method.c_str(), uri_r.c_str());
            ret = -2;
        }

    } else if (cmd == "update_handler") {

        //
        // curl 'http://172.16.10.137:18430/internal_manage?cmd=switch_handler&method=get&path=^/cgi-bin/getdemo.cgi$&enable=off&auth=d44bfc666db304b2f72b4918c8b46f78'
        // cp libgetdemo.so ../cgi-bin
        // curl 'http://172.16.10.137:18430/internal_manage?cmd=update_handler&method=get&path=^/cgi-bin/getdemo.cgi$&enable=on&auth=d44bfc666db304b2f72b4918c8b46f78'
        // curl 'http://172.16.10.137:18430/cgi-bin/getdemo.cgi'

        // 更新 non-build_in uri
        // 谨慎，为防止coredump需要检查引用计数
        std::string vhost = params.VALUE("vhost");
        std::string uri_r = params.VALUE("path");
        std::string method = params.VALUE("method");
        std::string enable = params.VALUE("enable");

        bool on = boost::iequals(enable, "off") ? false : true;

        if (boost::iequals(method, "GET")) {
            ret = update_http_get_handler(vhost, uri_r, on);
        } else if (boost::iequals(method, "POST")) {
            ret = update_http_post_handler(vhost, uri_r, on);
        } else {
            tzhttpd_log_err("Unknown method %s for update_handler with uri: %s",
                            method.c_str(), uri_r.c_str());
            ret = -2;
        }


    } else {

        tzhttpd_log_err("Unrecognized cmd: %s", cmd.c_str());
        ret = -1;
    }

    if (ret == 0) {
        response = http_proto::content_ok;
        status_line = generate_response_status_line(http_parser.get_version(),
                                                    StatusCode::success_ok);
    } else {

        // 如果没有配置错误返回信息，使用下面的默认错误响应
        if (response.empty() || status_line.empty()) {
            response = http_proto::content_error;
            status_line = generate_response_status_line(http_parser.get_version(),
                                                        StatusCode::server_error_internal_server_error);
        }
    }

    return 0;
}


} // end namespace tzhttpd
