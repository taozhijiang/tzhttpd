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
#include "HttpHandler.h"
#include "HttpServer.h"

#include "Log.h"

namespace tzhttpd {
namespace http_handler {

// init only once at startup, these are the default value
std::string              http_server_version = "1.0.0";
std::string              http_docu_root = "./docs/";
std::vector<std::string> http_docu_index = { "index.html", "index.htm" };


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

    std::string real_file_path = http_docu_root +
        "/" + http_parser.find_request_header(http_proto::header_options::request_path_info);

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
                const std::vector<std::string>& indexes = http_docu_index;
                for (std::vector<std::string>::const_iterator iter = indexes.cbegin();
                      iter != indexes.cend();
                      ++iter) {
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


int manage_http_get_handler(const HttpParser& http_parser, std::string& response, std::string& status_line) {

    const UriParamContainer& params = http_parser.get_request_uri_params();
    if (params.EMPTY() || !params.EXIST("cmd") || !params.EXIST("auth")) {
        log_err("manage page param check failed!");
        response = http_proto::content_bad_request;
        status_line = generate_response_status_line(http_parser.get_version(),
                                                    StatusCode::client_error_bad_request);
        return 0;
    }

    if (params.VALUE("auth") != "d44bfc666db304b2f72b4918c8b46f78") {
        log_err("auth check failed!");
        response = http_proto::content_forbidden;
        status_line = generate_response_status_line(http_parser.get_version(),
                                                    StatusCode::client_error_forbidden);
        return 0;
    }

    std::string cmd = params.VALUE("cmd");
    int ret = 0;
    if (cmd == "reload") {
        log_debug("do configure reconfigure ....");
        ret = HttpCfgHelper::instance().update_cfg();
    }

    if (ret == 0) {
        response = http_proto::content_ok;
        status_line = generate_response_status_line(http_parser.get_version(),
                                                    StatusCode::success_ok);
    } else {
        response = http_proto::content_error;
        status_line = generate_response_status_line(http_parser.get_version(),
                                                    StatusCode::server_error_internal_server_error);
    }

    return 0;
}


// http_cgi_handler

bool CgiWrapper::load_dl() {

    dl_ = std::make_shared<SLibLoader>(dl_path_);
    if (!dl_) {
        return false;
    }

    if (!dl_->init()) {
        log_err("init dl %s failed!", dl_->get_dl_path().c_str());
        return false;
    }



    return true;
}


//
// GET

bool CgiGetWrapper::init() {
    if (!load_dl()) {
        log_err("load dl failed!");
        return false;
    }
    if (!dl_->load_func<cgi_get_handler_t>("cgi_get_handler", &func_)) {
        log_err("Load func cgi_get_handler failed!");
        return false;
    }
    return true;
}

int CgiGetWrapper::operator()(const HttpParser& http_parser,
                              std::string& response, std::string& status_line) {
    if(!func_) {
        log_err("get func not initialized.");
        return -1;
    }

    msg_t param {};
    std::string str = http_parser.get_request_uri_params_string();
    fill_msg(&param, str.c_str(), str.size());

    int ret = -1;
    msg_t rsp {};

    try {
        ret = func_(&param, &rsp);
    } catch (...) {
        log_err("get func call exception detect.");
    }

    if (ret == 0) {
        response = std::string(rsp.data, rsp.len);
        status_line = generate_response_status_line(http_parser.get_version(), StatusCode::success_ok);
    } else {
        log_err("post func call return: %d", ret);
        response = http_proto::content_error;
        status_line = generate_response_status_line(http_parser.get_version(), StatusCode::server_error_internal_server_error);
    }

    log_debug("response: %s, status: %s", response.c_str(), status_line.c_str());
    free_msg(&param); free_msg(&rsp);
    return ret;
}

//
// POST

bool CgiPostWrapper::init() {
    if (!load_dl()) {
        log_err("load dl failed!");
        return false;
    }
    if (!dl_->load_func<cgi_post_handler_t>("cgi_post_handler", &func_)) {
        log_err("Load func cgi_post_handler failed!");
        return false;
    }
    return true;
}

int CgiPostWrapper::operator()(const HttpParser& http_parser, const std::string& post_data,
                               std::string& response, std::string& status_line) {
    if(!func_){
        log_err("get func not initialized.");
        return -1;
    }

    msg_t param {}, post{};
    std::string str = http_parser.get_request_uri_params_string();
    fill_msg(&param, str.c_str(), str.size());
    fill_msg(&post, post_data.c_str(), post_data.size());

    int ret = -1;
    msg_t rsp {};

    try {
        ret = func_(&param, &post, &rsp);
    } catch (...) {
        log_err("post func call exception detect.");
    }

    if (ret == 0) {
        response = std::string(rsp.data, rsp.len);
        status_line = generate_response_status_line(http_parser.get_version(), StatusCode::success_ok);
    } else {
        log_err("post func call return: %d", ret);
        response = http_proto::content_error;
        status_line = generate_response_status_line(http_parser.get_version(), StatusCode::server_error_internal_server_error);
    }

    log_debug("response: %s, status: %s", response.c_str(), status_line.c_str());
    free_msg(&param); free_msg(&rsp);
    return ret;
}

} // end namespace http_handler



// class HttpHandler

int HttpHandler::register_http_get_handler(std::string uri_regex, const HttpGetHandler& handler){

    std::string uri = pure_uri_path(uri_regex);
    boost::lock_guard<boost::shared_mutex> wlock(rwlock_);

    std::vector<std::pair<UriRegex, HttpGetHandler>>::iterator it;
    for (it = get_handler_.begin(); it != get_handler_.end(); ++it) {
        if (it->first.str() == uri ) {
            log_err("Handler for %s(%s) already exists, but still override it!", uri.c_str(), uri_regex.c_str());
            it->second = handler;
            return 0;
        }
    }

    UriRegex rgx {uri};
    get_handler_.push_back({rgx, handler});

    log_alert("Register GetHandler for %s(%s) OK!", uri.c_str(), uri_regex.c_str());
    return 0;
}

int HttpHandler::register_http_post_handler(std::string uri_regex, const HttpPostHandler& handler){

    std::string uri = pure_uri_path(uri_regex);
    boost::lock_guard<boost::shared_mutex> wlock(rwlock_);

    std::vector<std::pair<UriRegex, HttpPostHandler>>::iterator it;
    for (it = post_handler_.begin(); it != post_handler_.end(); ++it) {
        if (it->first.str() == uri ) {
            log_err("Handler for %s(%s) already exists, but still override it!", uri.c_str(), uri_regex.c_str());
            it->second = handler;
            return 0;
        }
    }

    UriRegex rgx {uri};
    post_handler_.push_back({rgx, handler});

    log_alert("Register PostHandler for %s(%s) OK!", uri.c_str(), uri_regex.c_str());
    return 0;
}


int HttpHandler::find_http_get_handler(std::string uri, HttpGetHandler& handler){

    uri = pure_uri_path(uri);
    boost::shared_lock<boost::shared_mutex> rlock(rwlock_);

    std::vector<std::pair<UriRegex, HttpGetHandler>>::const_iterator it;
    boost::smatch what;
    for (it = get_handler_.cbegin(); it != get_handler_.cend(); ++it) {
        if (boost::regex_match(uri, what, it->first)) {
            handler = it->second;
            return 0;
        }
    }

    return -1;
}

int HttpHandler::find_http_post_handler(std::string uri, HttpPostHandler& handler){

    uri = pure_uri_path(uri);
    boost::shared_lock<boost::shared_mutex> rlock(rwlock_);

    std::vector<std::pair<UriRegex, HttpPostHandler>>::const_iterator it;
    boost::smatch what;
    for (it = post_handler_.cbegin(); it != post_handler_.cend(); ++it) {
        if (boost::regex_match(uri, what, it->first)) {
            handler = it->second;
            return 0;
        }
    }

    return -1;
}


int HttpHandler::parse_cfg(const libconfig::Config& cfg, const std::string& key, std::map<std::string, std::string>& path_map) {

    if (!cfg.exists(key)) {
        log_err("handlers for %s not found!", key.c_str());
        return -1;
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
                log_err("skip err configure item...");
                ret_code --;
                continue;
            }

            log_alert("detect handler uri:%s, dl_path:%s", uri_path.c_str(), dl_path.c_str());
            path_map[uri_path] = dl_path;
        }
    } catch (...) {
        log_err("Parse %s error!!!", key.c_str());
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
        http_handler::CgiGetWrapper getter(iter->second);
        if (!getter.init()) {
            log_err("init get for %s @ %s failed, skip it!", iter->first.c_str(), iter->second.c_str());
            ret_code --;
            continue;
        }

        register_http_get_handler(iter->first, getter);
        log_debug("register_http_get_handler for %s @ %s OK!", iter->first.c_str(), iter->second.c_str());
    }


    key = "http.cgi_post_handlers";
    path_map.clear();
    ret_code += parse_cfg(cfg, key, path_map);
    for (auto iter = path_map.cbegin(); iter != path_map.cend(); ++ iter) {
        http_handler::CgiPostWrapper poster(iter->second);
        if (!poster.init()) {
            log_err("init post for %s @ %s failed, skip it!", iter->first.c_str(), iter->second.c_str());
            ret_code --;
            continue;
        }

        register_http_post_handler(iter->first, poster);
        log_debug("register_http_post_handler for %s @ %s OK!", iter->first.c_str(), iter->second.c_str());
    }

    return ret_code;
}



} // end namespace tzhttpd

