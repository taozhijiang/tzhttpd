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

// init only once at startup, these are the default value
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
                    status_line = generate_response_status_line(http_parser.get_version(), StatusCode::success_ok);
                }
            }
            break;

        default:
            break;
    }

    return 0;
}


// http_handler

bool CgiGetWrapper::init() {

    dl_ = std::make_shared<SLibLoader>(dl_path_);
    if (!dl_) {
        return false;
    }

    if (!dl_->init()) {
        log_err("init dl %s failed!", dl_->get_dl_path().c_str());
        return false;
    }

    if (!dl_->load_func<cgi_handler_t>("cgi_handler", &func_)) {
        log_err("Load func cig_handler failed!");
        return false;
    }

    return true;
}


int CgiGetWrapper::operator()(const HttpParser& http_parser,
               std::string& response, std::string& status_line) {
    if(func_){
        msg_t req {};
        msg_t resp{};

        std::string hello = "hello, from http server";
        fill_msg(&req, hello.c_str(), hello.size());
        int ret = func_(&req, &resp);
        response = std::string(resp.data);
        status_line = generate_response_status_line(http_parser.get_version(), StatusCode::success_ok);

        free_msg(&req); free_msg(&resp);
        return ret;
    }
    return -1;
}



} // end namespace http_handler



// class HttpHandler

int HttpHandler::register_http_get_handler(std::string uri_regex, HttpGetHandler handler){

    std::string uri = boost::algorithm::trim_copy(boost::to_lower_copy(uri_regex));
    while (uri[uri.size()-1] == '/' && uri.size() > 1)  // 全部的小写字母，去除尾部'/'
        uri = uri.substr(0, uri.size()-1);


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

int HttpHandler::register_http_post_handler(std::string uri_regex, HttpPostHandler handler){

    std::string uri = boost::algorithm::trim_copy(boost::to_lower_copy(uri_regex));
    while (uri[uri.size()-1] == '/' && uri.size() > 1)  // 全部的小写字母，去除尾部'/'
        uri = uri.substr(0, uri.size()-1);


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

    uri = boost::algorithm::trim_copy(boost::to_lower_copy(uri));
    while (uri[uri.size()-1] == '/' && uri.size() > 1)  // 全部的小写字母，去除尾部
        uri = uri.substr(0, uri.size()-1);

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

    uri = boost::algorithm::trim_copy(boost::to_lower_copy(uri));
    while (uri[uri.size()-1] == '/' && uri.size() > 1)  // 全部的小写字母，去除尾部
        uri = uri.substr(0, uri.size()-1);

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

} // end namespace tzhttpd

