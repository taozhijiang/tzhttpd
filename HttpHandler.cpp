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
std::string              http_server_version = "1.3.2";
} // end namespace http_handler


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


int HttpHandler::default_http_get_handler(const HttpParser& http_parser, std::string& response,
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
        status_line = generate_response_status_line(http_parser.get_version(), StatusCode::client_error_not_found);
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
    }

    return 0;
}

int HttpHandler::http_redirect_handler(std::string red_code, std::string red_uri,
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


int HttpHandler::register_http_get_handler(const std::string& uri_r, const HttpGetHandler& handler,
                                           bool built_in, bool working){

    std::string uri = StrUtil::pure_uri_path(uri_r);
    boost::lock_guard<boost::shared_mutex> wlock(rwlock_);

    std::vector<std::pair<UriRegex, HttpGetHandlerObjectPtr>>::iterator it;
    for (it = get_handler_.begin(); it != get_handler_.end(); ++it) {
        if (it->first.str() == uri ) {
            tzhttpd_log_err("[vhost:%s] Handler for %s(%s) already exists, we will skip it!",
                            vhost_name_.c_str(), uri.c_str(), uri_r.c_str());
            return -1;
        }
    }

    UriRegex rgx {uri};
    HttpGetHandlerObjectPtr phandler_obj =
        std::make_shared<HttpGetHandlerObject>(uri, handler, *this, built_in, working);
    if (!phandler_obj) {
        tzhttpd_log_err("[vhost:%s] create get handler object for %s failed.",
                        vhost_name_.c_str(), uri.c_str());
        return -2;
    }
    get_handler_.push_back({rgx, phandler_obj});

    tzhttpd_log_alert("[vhost:%s] register_http_get_handler for %s(%s) OK!",
                      vhost_name_.c_str(), uri.c_str(), uri_r.c_str());
    return 0;
}

int HttpHandler::register_http_post_handler(const std::string& uri_r, const HttpPostHandler& handler,
                                            bool built_in, bool working){

    std::string uri = StrUtil::pure_uri_path(uri_r);
    boost::lock_guard<boost::shared_mutex> wlock(rwlock_);

    std::vector<std::pair<UriRegex, HttpPostHandlerObjectPtr>>::iterator it;
    for (it = post_handler_.begin(); it != post_handler_.end(); ++it) {
        if (it->first.str() == uri ) {
            tzhttpd_log_err("[vhost:%s] Handler for %s(%s) already exists, we will skip it!",
                            vhost_name_.c_str(), uri.c_str(), uri_r.c_str());
            return -1;
        }
    }

    UriRegex rgx {uri};
    HttpPostHandlerObjectPtr phandler_obj =
        std::make_shared<HttpPostHandlerObject>(uri, handler, *this, built_in, working);
    if (!phandler_obj) {
        tzhttpd_log_err("[vhost:%s] Create post handler object for %s failed.",
                        vhost_name_.c_str(), uri.c_str());
        return -2;
    }
    post_handler_.push_back({ rgx, phandler_obj });

    tzhttpd_log_alert("[vhost:%s] register_http_post_handler for %s(%s) OK!",
                      vhost_name_.c_str(), uri.c_str(), uri_r.c_str());
    return 0;
}


int HttpHandler::find_http_get_handler(std::string uri, HttpGetHandlerObjectPtr& phandler_obj){

    if (http_redirect_get_phandler_obj_) {
        phandler_obj = http_redirect_get_phandler_obj_;
        return 0;
    }

    uri = StrUtil::pure_uri_path(uri);
    boost::shared_lock<boost::shared_mutex> rlock(rwlock_);

    std::vector<std::pair<UriRegex, HttpGetHandlerObjectPtr>>::const_iterator it;
    boost::smatch what;
    for (it = get_handler_.cbegin(); it != get_handler_.cend(); ++it) {
        if (boost::regex_match(uri, what, it->first)) {
            phandler_obj = it->second;
            return 0;
        }
    }

    tzhttpd_log_debug("[vhost:%s] http get handler for %s not found, using default...",
                      vhost_name_.c_str(),uri.c_str());
    phandler_obj = default_http_get_phandler_obj_;
    return 0;
}

int HttpHandler::find_http_post_handler(std::string uri, HttpPostHandlerObjectPtr& phandler_obj){

    if (http_redirect_get_phandler_obj_) {
        phandler_obj = http_redirect_post_phandler_obj_;
        return 0;
    }

    uri = StrUtil::pure_uri_path(uri);
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


int HttpHandler::do_parse_handler(const libconfig::Setting& setting, const std::string& key,
                                  std::map<std::string, HandlerCfg>& handleCfg) {

    if (!setting.exists(key)) {
        tzhttpd_log_notice("[vhost:%s] handlers for %s not found!",
                           vhost_name_.c_str(),key.c_str());
        return 0;
    }

    handleCfg.clear();
    int ret_code = 0;
    try {

        const libconfig::Setting &http_cgi_handlers = setting[key];
        for(int i = 0; i < http_cgi_handlers.getLength(); ++i) {

            const libconfig::Setting& handler = http_cgi_handlers[i];
            std::string uri_path {};
            std::string dl_path {};

            ConfUtil::conf_value(handler, "uri", uri_path);
            ConfUtil::conf_value(handler, "dl_path", dl_path);

            if(uri_path.empty() || dl_path.empty()) {
                tzhttpd_log_err("[vhost:%s] skip err configure item %s:%s...",
                                vhost_name_.c_str(), uri_path.c_str(), dl_path.c_str());
                ret_code --;
                continue;
            }

            tzhttpd_log_debug("[vhost:%s] detect handler uri:%s, dl_path:%s",
                              vhost_name_.c_str(), uri_path.c_str(), dl_path.c_str());

            HandlerCfg cfg;
            cfg.url_ = uri_path;
            cfg.dl_path_ = dl_path;

            handleCfg[uri_path] = cfg;
        }
    } catch (...) {
        tzhttpd_log_err("[vhost:%s] Parse %s error!!!",
                        vhost_name_.c_str(), key.c_str());
        ret_code --;
    }

    return ret_code;
}

int HttpHandler::update_runtime_cfg(const libconfig::Setting& setting) {

    int ret_code = 0;
    std::string key;
    std::map<std::string, HandlerCfg> path_map {};

    key = "cgi_get_handlers";
    path_map.clear();
    ret_code += do_parse_handler(setting, key, path_map);
    for (auto iter = path_map.cbegin(); iter != path_map.cend(); ++ iter) {

        // we will not override handler directly, consider using
        // /internal_manage manipulate
        if (check_exist_http_get_handler(iter->first)) {
            tzhttpd_log_alert("[vhost:%s] HttpGet for %s already exists, skip it.",
                              vhost_name_.c_str(), iter->first.c_str());
            continue;
        }

        http_handler::CgiGetWrapper getter(iter->second.dl_path_);
        if (!getter.init()) {
            tzhttpd_log_err("[vhost:%s] init get for %s @ %s failed, skip it!",
                            vhost_name_.c_str(), iter->first.c_str(),
                            iter->second.dl_path_.c_str());
            ret_code --;
            continue;
        }

        register_http_get_handler(iter->first, getter, false);
    }


    key = "cgi_post_handlers";
    path_map.clear();
    ret_code += do_parse_handler(setting, key, path_map);
    for (auto iter = path_map.cbegin(); iter != path_map.cend(); ++ iter) {

        // we will not override handler directly, consider using
        // /internal_manage manipulate
        if (check_exist_http_post_handler(iter->first)) {
            tzhttpd_log_alert("[vhost:%s] HttpPost for %s already exists, skip it.",
                              vhost_name_.c_str(), iter->first.c_str());
            continue;
        }

        http_handler::CgiPostWrapper poster(iter->second.dl_path_);
        if (!poster.init()) {
            tzhttpd_log_err("[vhost:%s] init post for %s @ %s failed, skip it!",
                            vhost_name_.c_str(), iter->first.c_str(),
                            iter->second.dl_path_.c_str());
            ret_code --;
            continue;
        }

        register_http_post_handler(iter->first, poster, false);
    }

    return ret_code;
}



} // end namespace tzhttpd

