/*-
 * Copyright (c) 2018 TAO Zhijiang<taozhijiang@gmail.com>
 *
 * Licensed under the BSD-3-Clause license, see LICENSE for full information.
 *
 */

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#include <fstream>
#include <sstream>
#include <boost/algorithm/string.hpp>

#include "HttpParser.h"
#include "HttpExecutor.h"
#include "HttpReqInstance.h"

#include "CryptoUtil.h"

#include "Log.h"

namespace tzhttpd {
namespace http_handler {
// init only once at startup, these are the default value
std::string              http_server_version = "1.3.4";
} // end namespace http_handler


using namespace http_proto;

// 默认静态的Handler，从文件系统读取文件病发送
static bool check_and_sendfile(const HttpParser& http_parser, std::string regular_file_path,
                               std::string& response, std::string& status_line) {

    // check dest is directory or regular?
    struct stat sb;
    if (stat(regular_file_path.c_str(), &sb) == -1) {
        tzhttpd_log_err("Stat file error: %s", regular_file_path.c_str());
        response = http_proto::content_error;
        status_line = generate_response_status_line(http_parser.get_version(),
                StatusCode::server_error_internal_server_error);
        return false;
    }

    if (sb.st_size > 100*1024*1024 /*100M*/) {
        tzhttpd_log_err("Too big file size: %ld", sb.st_size);
        response = http_proto::content_bad_request;
        status_line = generate_response_status_line(http_parser.get_version(),
                StatusCode::client_error_bad_request);
        return false;
    }

    std::ifstream fin(regular_file_path);
    fin.seekg(0);
    std::stringstream buffer;
    buffer << fin.rdbuf();
    response = buffer.str();
    status_line = generate_response_status_line(http_parser.get_version(),
            StatusCode::success_ok);

    return true;
}



int HttpExecutor::default_get_handler(const HttpParser& http_parser, std::string& response,
                                      std::string& status_line, std::vector<std::string>& add_header) {

    const UriParamContainer& params = http_parser.get_request_uri_params();
    if (!params.EMPTY()) {
        tzhttpd_log_err("Default handler just for static file transmit, we can not handler uri parameters...");
    }

    std::string real_file_path = http_docu_root_ +
            "/" + http_parser.find_request_header(http_proto::header_options::request_path_info);

    // check dest exist?
    if (::access(real_file_path.c_str(), R_OK) != 0) {
        tzhttpd_log_err("File not found: %s", real_file_path.c_str());
        response = http_proto::content_not_found;
        status_line = generate_response_status_line(http_parser.get_version(),
                StatusCode::client_error_not_found);
        return -1;
    }

    // check dest is directory or regular?
    struct stat sb;
    if (stat(real_file_path.c_str(), &sb) == -1) {
        tzhttpd_log_err("Stat file error: %s", real_file_path.c_str());
        response = http_proto::content_error;
        status_line = generate_response_status_line(http_parser.get_version(),
                StatusCode::server_error_internal_server_error);
        return -1;
    }

    bool OK = false;
    std::string did_file_full_path {};

    switch (sb.st_mode & S_IFMT) {
        case S_IFREG:
            if(check_and_sendfile(http_parser, real_file_path, response, status_line)) {
                did_file_full_path = real_file_path;
                OK = true;
            }
            break;

        case S_IFDIR:
            {
                const std::vector<std::string>& indexes = http_docu_index_;
                for (std::vector<std::string>::const_iterator iter = indexes.cbegin();
                      iter != indexes.cend();
                      ++iter) {
                    std::string file_path = real_file_path + "/" + *iter;
                    tzhttpd_log_info("Trying: %s", file_path.c_str());
                    if (check_and_sendfile(http_parser, file_path, response, status_line)) {
                        did_file_full_path = file_path;
                        OK = true;
                        break;
                    }
                }

                if (!OK) {
                    // default, 404
                    response = http_proto::content_not_found;
                    status_line = generate_response_status_line(http_parser.get_version(),
                            StatusCode::client_error_not_found);
                }
            }
            break;

        default:
            break;
    }

#if 0
    // do handler helper
    if (OK && !did_file_full_path.empty()) {

        boost::to_lower(did_file_full_path);

        // 取出扩展名
        std::string::size_type pos = did_file_full_path.rfind(".");
        std::string suffix {};
        if (pos != std::string::npos && (did_file_full_path.size() - pos) < 6) {
            suffix = did_file_full_path.substr(pos);
        }

        if (suffix.empty()) {
            return 0;
        }

        // handle with specified suffix, for cache and content_type
        auto iter = cache_controls_.find(suffix);
        if (iter != cache_controls_.end()) {
            add_header.push_back(iter->second);
            tzhttpd_log_debug("Adding cache header for %s(%s) -> %s",
                              did_file_full_path.c_str(), iter->first.c_str(), iter->second.c_str());
        }

        std::string content_type = http_proto::find_content_type(suffix);
        if (!content_type.empty()) {
            add_header.push_back(content_type);
            tzhttpd_log_debug("Adding content_type header for %s(%s) -> %s",
                              did_file_full_path.c_str(), suffix.c_str(), content_type.c_str());
        }

        // compress type
        // content already in response, compress it if possible
        const auto cz_iter = compress_controls_.find(suffix);
        if (cz_iter != compress_controls_.cend()) {
            std::string encoding = http_parser.find_request_header(http_proto::header_options::accept_encoding);
            if (!encoding.empty()) {
                tzhttpd_log_debug("Accept Encoding: %s", encoding.c_str());
                if (encoding.find("deflate") != std::string::npos) {

                    std::string compressed {};

                    if (CryptoUtil::Deflator(response, compressed) == 0) {
                        tzhttpd_log_debug("compress %s size from %d to %d", did_file_full_path.c_str(),
                                          static_cast<int>(response.size()), static_cast<int>(compressed.size()));
                        response.swap(compressed);
                        add_header.push_back("Content-Encoding: deflate");
                    } else {
                        tzhttpd_log_err("cryptopp deflate encoding failed.");
                    }

                } else if (encoding.find("gzip") != std::string::npos) {

                    std::string compressed {};

                    if (CryptoUtil::Gzip(response, compressed) == 0) {
                        tzhttpd_log_debug("compress %s size from %d to %d", did_file_full_path.c_str(),
                                          static_cast<int>(response.size()), static_cast<int>(compressed.size()));
                        response.swap(compressed);
                        add_header.push_back("Content-Encoding: gzip");
                    } else {
                        tzhttpd_log_err("cryptopp gzip encoding failed.");
                    }

                } else {
                    tzhttpd_log_err("unregistered compress type: %s", encoding.c_str());
                }
            }
        }
    }

#endif

    return 0;
}




int HttpExecutor::do_find_handler(const enum HTTP_METHOD& method,
                                  const std::string& uri,
                                  HttpHandlerObjectPtr& handler) {

    std::string n_uri = StrUtil::pure_uri_path(uri);

    boost::shared_lock<boost::shared_mutex> rlock(rwlock_);

    std::vector<std::pair<UriRegex, HttpHandlerObjectPtr>>::const_iterator it;
    boost::smatch what;
    for (it = handlers_.cbegin(); it != handlers_.cend(); ++it) {
        if (boost::regex_match(uri, what, it->first)) {
            if (method == HTTP_METHOD::GET && it->second->http_get_handler_) {
                handler = it->second;
                return 0;
            } else if (method == HTTP_METHOD::POST && it->second->http_post_handler_) {
                handler = it->second;
                return 0;
            } else {
                tzhttpd_log_err("uri: %s matched, but no suitable handler for method:",
                                uri.c_str(), HTTP_METHOD_STRING(method).c_str());
                return -1;
            }
        }
    }

    if (method == HTTP_METHOD::GET) {
        tzhttpd_log_debug("[hostname:%s] http get default handler (filesystem) for %s ",
                          hostname_.c_str(), uri.c_str());
        handler = default_get_handler_;
        return 0;
    }

    return -2;
}

int HttpExecutor::http_redirect_handler(std::string red_code, std::string red_uri,
                          const HttpParser& http_parser, const std::string& post_data,
                          std::string& response,
                          std::string& status_line, std::vector<std::string>& add_header) {

    if (red_code == "301") {
        status_line = generate_response_status_line(http_parser.get_version(),
                StatusCode::redirection_moved_permanently);
        response = http_proto::content_301;
        add_header.push_back("Location: " + red_uri);
    } else if(red_code == "302"){
        status_line = generate_response_status_line(http_parser.get_version(),
                StatusCode::redirection_found);
        response = http_proto::content_302;
        add_header.push_back("Location: " + red_uri);
    } else {
        tzhttpd_log_err("unknown red_code: %s", red_code.c_str());
        status_line = generate_response_status_line(http_parser.get_version(),
                StatusCode::server_error_internal_server_error);
        response = http_proto::content_error;
    }

    return 0;
}


void HttpExecutor::handle_http_request(std::shared_ptr<HttpReqInstance> http_req_instance) override {

    HttpHandlerObjectPtr handler_object {};

    if (do_find_handler(http_req_instance->method_,  http_req_instance->uri_, handler_object) != 0) {
        tzhttpd_log_err("find handler for %s, %s failed.",
                        HTTP_METHOD_STRING(http_req_instance->method_).c_str(),
                        http_req_instance->uri_.c_str());
        http_req_instance->http_std_response(http_proto::StatusCode::client_error_not_found);
        return;
    }

    SAFE_ASSERT(handler_object);

    if (http_req_instance->method_ == HTTP_METHOD::GET) {
        HttpGetHandler handler = handler_object->http_get_handler_;
        if (!handler) {
            http_req_instance->http_std_response(http_proto::StatusCode::server_error_internal_server_error);
            return;
        }

        std::string response_str;
        std::string status_str;
        std::vector<std::string> headers;
        int code = handler(*http_req_instance->http_parser_, response_str, status_str, headers);
        if (code == 0) {
            handler_object->success_count_ ++;
        } else {
            handler_object->fail_count_ ++;
        }

        // status_line 为必须返回参数，如果没有就按照调用结果返回标准内容
        if (status_str.empty()) {
            if (code == 0)
                http_req_instance->http_std_response(http_proto::StatusCode::success_ok);
            else
                http_req_instance->http_std_response(http_proto::StatusCode::server_error_internal_server_error);
        } else {
            http_req_instance->http_response(response_str, status_str, headers);
        }
    }

}


} // end namespace tzhttpd

