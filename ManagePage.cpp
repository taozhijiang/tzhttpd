/*-
 * Copyright (c) 2018-2019 TAO Zhijiang<taozhijiang@gmail.com>
 *
 * Licensed under the BSD-3-Clause license, see LICENSE for full information.
 *
 */

#include "HttpParser.h"
#include "HttpProto.h"
#include "HttpServer.h"

#include "Dispatcher.h"
#include "Global.h"

namespace tzhttpd {

using namespace http_proto;

// internal/status
static int system_status_handler(const HttpParser& http_parser,
                                 std::string& response, std::string& status_line, std::vector<std::string>& add_header);
// internal/updateconf
static int system_updateconf_handler(const HttpParser& http_parser,
                                     std::string& response, std::string& status_line, std::vector<std::string>& add_header);

// internal/drop?hostname=aaa&uri=bbb&method=GET/POST/ALL
static int system_drop_handler(const HttpParser& http_parser,
                               std::string& response, std::string& status_line, std::vector<std::string>& add_header);

bool system_manage_page_init(HttpServer& server) {

    if (server.add_http_get_handler("^/internal/status$", system_status_handler, true) != 0) {
        roo::log_err("register system status module failed, treat as fatal.");
        return false;
    }

    if (server.add_http_get_handler("^/internal/updateconf$", system_updateconf_handler, true) != 0) {
        roo::log_err("register system update runtime conf module failed, treat as fatal.");
        return false;
    }

    if (server.add_http_get_handler("^/internal/drop$", system_drop_handler, true) != 0) {
        roo::log_err("register system handler control failed, treat as fatal.");
        return false;
    }

    return true;
}



static int system_updateconf_handler(const HttpParser& http_parser,
                                     std::string& response, std::string& status_line, std::vector<std::string>& add_header) {

    int ret = Global::instance().setting_ptr_->update_runtime_setting();

    string http_ver = http_parser.get_version();
    if (ret == 0) {
        status_line = generate_response_status_line(http_ver, StatusCode::success_ok);
        response = content_ok;
    } else {
        status_line = generate_response_status_line(http_ver, StatusCode::server_error_internal_server_error);
        response = content_error;
    }

    return 0;
}

static int system_status_handler(const HttpParser& http_parser,
                                 std::string& response, std::string& status_line, std::vector<std::string>& add_header) {

    std::string result;
    Global::instance().status_ptr_->collect_status(result);

    response = result;
    status_line = http_proto::generate_response_status_line(http_parser.get_version(), http_proto::StatusCode::success_ok);

    return 0;
}

static int system_drop_handler(const HttpParser& http_parser,
                               std::string& response, std::string& status_line, std::vector<std::string>& add_header) {

    const UriParamContainer& params = http_parser.get_request_uri_params();

    std::string hostname = params.VALUE("hostname");
    std::string uri      = params.VALUE("uri");
    std::string method   = params.VALUE("method");
    if (uri.empty() || (!method.empty() && method != "GET" && method != "POST" && method != "ALL")) {
        roo::log_err("param check failed!");
        response = content_bad_request;
        status_line = generate_response_status_line(http_parser.get_version(),
                                                    StatusCode::client_error_bad_request);
        return 0;
    }

    string http_ver = http_parser.get_version();
    enum HTTP_METHOD h_method = HTTP_METHOD::ALL;
    if (method == "GET") {
        h_method = HTTP_METHOD::GET;
    } else if (method == "POST") {
        h_method = HTTP_METHOD::POST;
    }

    int ret = Dispatcher::instance().drop_http_handler(hostname, uri, h_method);

    if (ret == 0) {
        status_line = generate_response_status_line(http_ver, StatusCode::success_ok);
        response = content_ok;
    } else {
        status_line = generate_response_status_line(http_ver, StatusCode::server_error_internal_server_error);
        response = content_error;
    }

    return 0;
}

} // end namespace tzhttpd
