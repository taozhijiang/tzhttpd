#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#include <fstream>
#include <sstream>
#include <boost/algorithm/string.hpp>

#include "HttpProto.h"
#include "HttpHandler.h"
#include "HttpServer.h"

#include "Log.h"

namespace tzhttpd {
namespace http_handler {

using namespace http_proto;

static bool check_and_sendfile(const HttpParser& http_parser, std::string regular_file_path,
                                   std::string& response, std::string& status_line) {

    // check dest is directory or regular?
    struct stat sb;
    if (stat(regular_file_path.c_str(), &sb) == -1) {
        log_err("Stat file error: %s", regular_file_path.c_str());
        response = http_proto::content_error;
        status_line = generate_response_status_line(http_parser.get_version(), StatusCode::server_error_internal_server_error);
        return false;
    }

    if (sb.st_size > 100*1024*1024 /*100M*/) {
        log_err("Too big file size: %ld", sb.st_size);
        response = http_proto::content_bad_request;
        status_line = generate_response_status_line(http_parser.get_version(), StatusCode::client_error_bad_request);
        return false;
    }

    std::ifstream fin(regular_file_path);
    fin.seekg(0);
    std::stringstream buffer;
    buffer << fin.rdbuf();
    response = buffer.str();
    status_line = generate_response_status_line(http_parser.get_version(), StatusCode::success_ok);

    return true;
}


int default_http_get_handler(const HttpParser& http_parser, std::string& response, std::string& status_line) {

    const UriParamContainer& params = http_parser.get_request_uri_params();
    if (!params.EMPTY()) {
        log_err("Default handler just for static file transmit, we can not handler uri parameters...");
    }

//    std::string real_file_path = helper::request_http_docu_root() + "/" + http_parser.find_request_header(http_proto::header_options::request_path_info);
    std::string real_file_path = "/var/www/" + http_parser.find_request_header(http_proto::header_options::request_path_info);

    // check dest exist?
    if (::access(real_file_path.c_str(), R_OK) != 0) {
        log_err("File not found: %s", real_file_path.c_str());
        response = http_proto::content_not_found;
        status_line = generate_response_status_line(http_parser.get_version(), StatusCode::client_error_not_found);
        return -1;
    }

    // check dest is directory or regular?
    struct stat sb;
    if (stat(real_file_path.c_str(), &sb) == -1) {
        log_err("Stat file error: %s", real_file_path.c_str());
        response = http_proto::content_error;
        status_line = generate_response_status_line(http_parser.get_version(), StatusCode::server_error_internal_server_error);
        return -1;
    }

    switch (sb.st_mode & S_IFMT) {
        case S_IFREG:
            check_and_sendfile(http_parser, real_file_path, response, status_line);
            break;

        case S_IFDIR:
            {
                bool OK = false;
                // const std::vector<std::string> &indexes = helper::request_http_docu_index();
                const std::vector<std::string> indexes = {"index.html", "index.htm"};
                for (std::vector<std::string>::const_iterator iter = indexes.cbegin(); iter != indexes.cend(); ++iter) {
                    std::string file_path = real_file_path + "/" + *iter;
                    log_info("Trying: %s", file_path.c_str());
                    if (check_and_sendfile(http_parser, file_path, response, status_line)) {
                        OK = true;
                        break;
                    }
                }

                if (!OK) {
                    // default, 404
                    response = http_proto::content_not_found;
                    status_line = generate_response_status_line(http_parser.get_version(), StatusCode::success_ok);
                }
            }
            break;

        default:
            break;
    }

    return 0;
}


} // end namespace http_handler
} // end namespace tzhttpd

