/*-
 * Copyright (c) 2018 TAO Zhijiang<taozhijiang@gmail.com>
 *
 * Licensed under the BSD-3-Clause license, see LICENSE for full information.
 *
 */

#include "HttpServer.h"

#include "Status.h"

namespace tzhttpd {

// internal/status
static int system_status_handler(const HttpParser& http_parser,
                                 std::string& response, std::string& status_line, std::vector<std::string>& add_header);

bool system_manage_page_init(HttpServer& server) {

    if (server.register_http_get_handler("^/internal/status$", system_status_handler) != 0) {
        tzhttpd_log_err("register system status module failed, treat as fatal.");
        return false;
    }
    return true;
}



static int system_status_handler(const HttpParser& http_parser,
                                 std::string& response, std::string& status_line, std::vector<std::string>& add_header) {

    std::string result;
    Status::instance().collect_status(result);

    response = result;
    status_line = http_proto::generate_response_status_line(http_parser.get_version(), http_proto::StatusCode::success_ok);

    return 0;
}


} // end namespace tzhttpd
