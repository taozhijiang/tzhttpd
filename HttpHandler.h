#ifndef __TZHTTPD_HTTP_HANDLER_H__
#define __TZHTTPD_HTTP_HANDLER_H__

// 所有的http uri 路由
#include <vector>
#include <string>

#include <boost/regex.hpp>
#include <boost/thread/locks.hpp>
#include <boost/thread/shared_mutex.hpp>


namespace tzhttpd {

class HttpParser;

typedef std::function<int (const HttpParser& http_parser, const std::string& post_data, \
                           std::string& response, std::string& status_line)> HttpPostHandler;
typedef std::function<int (const HttpParser& http_parser, \
                           std::string& response, std::string& status_line)> HttpGetHandler;

class UriRegex: public boost::regex {
public:
    explicit UriRegex(const std::string& regexStr) :
        boost::regex(regexStr), str_(regexStr) {
    }

    std::string str() {
        return str_;
    }

private:
    std::string str_;
};


class HttpHandler {

public:

    int register_http_get_handler(std::string uri_regex, HttpGetHandler handler);
    int register_http_post_handler(std::string uri_regex, HttpPostHandler handler);

    int find_http_get_handler(std::string uri, HttpGetHandler& handler);
    int find_http_post_handler(std::string uri, HttpPostHandler& handler);

private:

    boost::shared_mutex rwlock_;

    // 使用vector保存handler，保证是先注册handler具有高优先级
    std::vector<std::pair<UriRegex, HttpPostHandler>> post_handler_;
    std::vector<std::pair<UriRegex, HttpGetHandler>>  get_handler_;

};

namespace http_handler {

int default_http_get_handler(const HttpParser& http_parser, std::string& response, std::string& status_line);

} // end namespace http_handler

} // end namespace tzhttpd


#endif //__TZHTTPD_HTTP_HANDLER_H__
