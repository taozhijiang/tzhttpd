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

#include "HttpServer.h"
#include "HttpProto.h"
#include "HttpCfgHelper.h"

#include "Log.h"

namespace tzhttpd {

using namespace tzhttpd::http_proto;

int HttpServer::manage_http_get_handler(const HttpParser& http_parser, std::string& response,
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
        ret = HttpCfgHelper::instance().update_cfg();
    } else if (cmd == "disable_handler") {
        // 禁用 uri
        std::string uri_regex = params.VALUE("path");

    } else if (cmd == "enable_handler") {
        // 启用 uri
        std::string uri_regex = params.VALUE("path");

    } else if (cmd == "update_handler") {
        // 更新 non-build_in uri
        // 谨慎，为防止coredump需要检查引用计数
        std::string uri_regex = params.VALUE("path");

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
