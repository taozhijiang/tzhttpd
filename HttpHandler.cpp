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

#include "HttpCfgHelper.h"
#include "HttpProto.h"
#include "HttpParser.h"
#include "HttpHandler.h"
#include "HttpServer.h"

#include "Log.h"

namespace tzhttpd {
namespace http_handler {

// init only once at startup, these are the default value
std::string              http_server_version = "1.1.3";
std::string              http_docu_root = "./docs/";
std::vector<std::string> http_docu_index = { "index.html", "index.htm" };

std::shared_ptr<HttpGetHandlerObject> default_http_get_phandler_obj =
    std::make_shared<HttpGetHandlerObject>(default_http_get_handler, true);




using namespace http_proto;

static bool check_and_sendfile(const HttpParser& http_parser, std::string regular_file_path,
                               std::string& response, std::string& status_line) {

    // check dest is directory or regular?
    struct stat sb;
    if (stat(regular_file_path.c_str(), &sb) == -1) {
        tzhttpd_log_err("Stat file error: %s", regular_file_path.c_str());
        response = http_proto::content_error;
        status_line = generate_response_status_line(http_parser.get_version(), StatusCode::server_error_internal_server_error);
        return false;
    }

    if (sb.st_size > 100*1024*1024 /*100M*/) {
        tzhttpd_log_err("Too big file size: %ld", sb.st_size);
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


int default_http_get_handler(const HttpParser& http_parser, std::string& response,
                             std::string& status_line, std::vector<std::string>& add_header) {

    const UriParamContainer& params = http_parser.get_request_uri_params();
    if (!params.EMPTY()) {
        tzhttpd_log_err("Default handler just for static file transmit, we can not handler uri parameters...");
    }

    std::string real_file_path = http_docu_root +
        "/" + http_parser.find_request_header(http_proto::header_options::request_path_info);

    // check dest exist?
    if (::access(real_file_path.c_str(), R_OK) != 0) {
        tzhttpd_log_err("File not found: %s", real_file_path.c_str());
        response = http_proto::content_not_found;
        status_line = generate_response_status_line(http_parser.get_version(), StatusCode::client_error_not_found);
        return -1;
    }

    // check dest is directory or regular?
    struct stat sb;
    if (stat(real_file_path.c_str(), &sb) == -1) {
        tzhttpd_log_err("Stat file error: %s", real_file_path.c_str());
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
                const std::vector<std::string>& indexes = http_docu_index;
                for (std::vector<std::string>::const_iterator iter = indexes.cbegin();
                      iter != indexes.cend();
                      ++iter) {
                    std::string file_path = real_file_path + "/" + *iter;
                    tzhttpd_log_info("Trying: %s", file_path.c_str());
                    if (check_and_sendfile(http_parser, file_path, response, status_line)) {
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

    return 0;
}


} // end namespace http_handler



// class HttpHandler

int HttpHandler::register_http_get_handler(const std::string& uri_r, const HttpGetHandler& handler,
                                           bool built_in, bool working){

    std::string uri = pure_uri_path(uri_r);
    boost::lock_guard<boost::shared_mutex> wlock(rwlock_);

    std::vector<std::pair<UriRegex, HttpGetHandlerObjectPtr>>::iterator it;
    for (it = get_handler_.begin(); it != get_handler_.end(); ++it) {
        if (it->first.str() == uri ) {
            tzhttpd_log_err("Handler for %s(%s) already exists, we will skip it!", uri.c_str(), uri_r.c_str());
            return -1;
        }
    }

    UriRegex rgx {uri};
    HttpGetHandlerObjectPtr phandler_obj = std::make_shared<HttpGetHandlerObject>(handler, built_in, working);
    if (!phandler_obj) {
        tzhttpd_log_err("Create get handler object for %s failed.", uri.c_str());
        return -2;
    }
    get_handler_.push_back({rgx, phandler_obj});

    tzhttpd_log_alert("Register GetHandler for %s(%s) OK!", uri.c_str(), uri_r.c_str());
    return 0;
}

int HttpHandler::register_http_post_handler(const std::string& uri_r, const HttpPostHandler& handler,
                                            bool built_in, bool working){

    std::string uri = pure_uri_path(uri_r);
    boost::lock_guard<boost::shared_mutex> wlock(rwlock_);

    std::vector<std::pair<UriRegex, HttpPostHandlerObjectPtr>>::iterator it;
    for (it = post_handler_.begin(); it != post_handler_.end(); ++it) {
        if (it->first.str() == uri ) {
            tzhttpd_log_err("Handler for %s(%s) already exists, we will skip it!", uri.c_str(), uri_r.c_str());
            return -1;
        }
    }

    UriRegex rgx {uri};
    HttpPostHandlerObjectPtr phandler_obj = std::make_shared<HttpPostHandlerObject>(handler, built_in, working);
    if (!phandler_obj) {
        tzhttpd_log_err("Create post handler object for %s failed.", uri.c_str());
        return -2;
    }
    post_handler_.push_back({ rgx, phandler_obj });

    tzhttpd_log_alert("Register PostHandler for %s(%s) OK!", uri.c_str(), uri_r.c_str());
    return 0;
}


int HttpHandler::find_http_get_handler(std::string uri, HttpGetHandlerObjectPtr& phandler_obj){

    uri = pure_uri_path(uri);
    boost::shared_lock<boost::shared_mutex> rlock(rwlock_);

    std::vector<std::pair<UriRegex, HttpGetHandlerObjectPtr>>::const_iterator it;
    boost::smatch what;
    for (it = get_handler_.cbegin(); it != get_handler_.cend(); ++it) {
        if (boost::regex_match(uri, what, it->first)) {
            phandler_obj = it->second;
            return 0;
        }
    }

    return -1;
}

int HttpHandler::find_http_post_handler(std::string uri, HttpPostHandlerObjectPtr& phandler_obj){

    uri = pure_uri_path(uri);
    boost::shared_lock<boost::shared_mutex> rlock(rwlock_);

    std::vector<std::pair<UriRegex, HttpPostHandlerObjectPtr>>::const_iterator it;
    boost::smatch what;
    for (it = post_handler_.cbegin(); it != post_handler_.cend(); ++it) {
        if (boost::regex_match(uri, what, it->first)) {
            phandler_obj = it->second;
            return 0;
        }
    }

    return -1;
}


int HttpHandler::parse_cfg(const libconfig::Config& cfg, const std::string& key,
                           std::map<std::string, std::string>& path_map) {

    if (!cfg.exists(key)) {
        tzhttpd_log_notice("handlers for %s not found!", key.c_str());
        return 0;
    }

    path_map.clear();
    int ret_code = 0;
    try {

        const libconfig::Setting &http_cgi_handlers = cfg.lookup(key);
        for(int i = 0; i < http_cgi_handlers.getLength(); ++i) {

            const libconfig::Setting& handler = http_cgi_handlers[i];
            std::string uri_path;
            std::string dl_path;

            if(!handler.lookupValue("uri", uri_path) || !handler.lookupValue("dl_path", dl_path)) {
                tzhttpd_log_err("skip err configure item...");
                ret_code --;
                continue;
            }

            tzhttpd_log_alert("detect handler uri:%s, dl_path:%s", uri_path.c_str(), dl_path.c_str());
            path_map[uri_path] = dl_path;
        }
    } catch (...) {
        tzhttpd_log_err("Parse %s error!!!", key.c_str());
        ret_code --;
    }

    return ret_code;
}

int HttpHandler::update_run_cfg(const libconfig::Config& cfg) {

    int ret_code = 0;
    std::string key;
    std::map<std::string, std::string> path_map {};

    key = "http.cgi_get_handlers";
    path_map.clear();
    ret_code += parse_cfg(cfg, key, path_map);
    for (auto iter = path_map.cbegin(); iter != path_map.cend(); ++ iter) {

        // we will not override handler directly, consider using /manage
        if (check_exist_http_get_handler(iter->first)) {
            tzhttpd_log_alert("HttpGet for %s already exists, skip it.", iter->first.c_str());
            continue;
        }

        http_handler::CgiGetWrapper getter(iter->second);
        if (!getter.init()) {
            tzhttpd_log_err("init get for %s @ %s failed, skip it!", iter->first.c_str(), iter->second.c_str());
            ret_code --;
            continue;
        }

        register_http_get_handler(iter->first, getter, false);
        tzhttpd_log_debug("register_http_get_handler for %s @ %s OK!", iter->first.c_str(), iter->second.c_str());
    }


    key = "http.cgi_post_handlers";
    path_map.clear();
    ret_code += parse_cfg(cfg, key, path_map);
    for (auto iter = path_map.cbegin(); iter != path_map.cend(); ++ iter) {

        // we will not override handler directly, consider using /manage
        if (check_exist_http_post_handler(iter->first)) {
            tzhttpd_log_alert("HttpPost for %s already exists, skip it.", iter->first.c_str());
            continue;
        }

        http_handler::CgiPostWrapper poster(iter->second);
        if (!poster.init()) {
            tzhttpd_log_err("init post for %s @ %s failed, skip it!", iter->first.c_str(), iter->second.c_str());
            ret_code --;
            continue;
        }

        register_http_post_handler(iter->first, poster, false);
        tzhttpd_log_debug("register_http_post_handler for %s @ %s OK!", iter->first.c_str(), iter->second.c_str());
    }

    return ret_code;
}



} // end namespace tzhttpd

