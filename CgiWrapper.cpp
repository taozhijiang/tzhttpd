/*-
 * Copyright (c) 2018-2019 TAO Zhijiang<taozhijiang@gmail.com>
 *
 * Licensed under the BSD-3-Clause license, see LICENSE for full information.
 *
 */

#include "HttpParser.h"
#include "HttpProto.h"

#include "CgiHelper.h"
#include "SlibLoader.h"
#include "CgiWrapper.h"

namespace tzhttpd {

namespace http_handler {

using namespace tzhttpd::http_proto;

bool CgiWrapper::load_dl() {

    dl_ = std::make_shared<SLibLoader>(dl_path_);
    if (!dl_) {
        return false;
    }

    if (!dl_->init()) {
        tzhttpd_log_err("init dl %s failed!", dl_->get_dl_path().c_str());
        return false;
    }

    return true;
}


//
// GET

bool CgiGetWrapper::init() {
    if (!load_dl()) {
        tzhttpd_log_err("load dl failed!");
        return false;
    }
    if (!dl_->load_func<cgi_get_handler_t>("cgi_get_handler", &func_)) {
        tzhttpd_log_err("Load cgi_get_handler func for %s failed.", dl_path_.c_str());
        return false;
    }
    return true;
}

int CgiGetWrapper::operator()(const HttpParser& http_parser,
                              std::string& response, std::string& status_line,
                              std::vector<std::string>& add_header) {
    if(!func_) {
        tzhttpd_log_err("get func not initialized.");
        return -1;
    }

    msg_t param {};
    std::string param_str = http_parser.get_request_uri_params_string();
    fill_msg(&param, param_str.c_str(), param_str.size());

    int ret = -1;
    msg_t rsp {};
    msg_t rsp_header {};

    try {
        ret = func_(&param, &rsp, &rsp_header);
    } catch (const std::exception& e) {
        tzhttpd_log_err("post func call std::exception detect: %s.", e.what());
    } catch (...) {
        tzhttpd_log_err("get func call exception detect.");
    }

    if (ret == 0) {
        response = std::string(rsp.data, rsp.len);
        status_line = generate_response_status_line(http_parser.get_version(),
                StatusCode::success_ok);
    } else {
        tzhttpd_log_err("post func call return: %d", ret);
        response = http_proto::content_error;
        status_line = generate_response_status_line(http_parser.get_version(),
                StatusCode::server_error_internal_server_error);
    }

    std::string header(rsp_header.data, rsp_header.len);
    if (!header.empty()) {
        std::vector<std::string> vec{};
        boost::split(vec, header, boost::is_any_of("\n"));
        for (auto iter = vec.begin(); iter != vec.cend(); ++iter){
            std::string str = boost::trim_copy(*iter);
            if (!str.empty()) {
                add_header.push_back(str);
            }
        }
    }

    tzhttpd_log_debug("param: %s,\n"
                      "response: %s, status: %s, add_header: %s",
                      param_str.c_str(),
                      response.c_str(), status_line.c_str(), header.c_str());

    free_msg(&param);
    free_msg(&rsp); free_msg(&rsp_header);
    return ret;
}

//
// POST

bool CgiPostWrapper::init() {
    if (!load_dl()) {
        tzhttpd_log_err("load dl failed!");
        return false;
    }
    if (!dl_->load_func<cgi_post_handler_t>("cgi_post_handler", &func_)) {
        tzhttpd_log_err("Load cgi_post_handler func for %s failed.", dl_path_.c_str());
        return false;
    }
    return true;
}

int CgiPostWrapper::operator()(const HttpParser& http_parser, const std::string& post_data,
                               std::string& response, std::string& status_line,
                               std::vector<std::string>& add_header ) {
    if(!func_){
        tzhttpd_log_err("get func not initialized.");
        return -1;
    }

    msg_t param {}, post{};
    std::string param_str = http_parser.get_request_uri_params_string();
    fill_msg(&param, param_str.c_str(), param_str.size());
    fill_msg(&post, post_data.c_str(), post_data.size());

    int ret = -1;
    msg_t rsp {};
    msg_t rsp_header {};

    try {
        ret = func_(&param, &post, &rsp, &rsp_header);
    } catch (const std::exception& e) {
        tzhttpd_log_err("post func call std::exception detect: %s.", e.what());
    } catch (...) {
        tzhttpd_log_err("post func call exception detect.");
    }

    if (ret == 0) {
        response = std::string(rsp.data, rsp.len);
        status_line = generate_response_status_line(http_parser.get_version(),
                StatusCode::success_ok);
    } else {
        tzhttpd_log_err("post func call return: %d", ret);
        response = http_proto::content_error;
        status_line = generate_response_status_line(http_parser.get_version(),
                StatusCode::server_error_internal_server_error);
    }

    std::string header(rsp_header.data, rsp_header.len);
    if (!header.empty()) {
        std::vector<std::string> vec{};
        boost::split(vec, header, boost::is_any_of("\n"));
        for (auto iter = vec.begin(); iter != vec.cend(); ++iter){
            std::string str = boost::trim_copy(*iter);
            if (!str.empty()) {
                add_header.push_back(str);
            }
        }
    }

    tzhttpd_log_debug("param: %s, post: %s,\n"
                      "response: %s, status: %s, add_header: %s",
                      param_str.c_str(), post_data.c_str(),
                      response.c_str(), status_line.c_str(), header.c_str());

    free_msg(&param); free_msg(&post);
    free_msg(&rsp); free_msg(&rsp_header);
    return ret;
    return 0;
}


} // end namespace http_handler
} // end namespace tzhttpd

