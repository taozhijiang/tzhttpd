#ifndef __TZHTTPD_HTTP_HANDLER_H__
#define __TZHTTPD_HTTP_HANDLER_H__

// 所有的http uri 路由

#include <string>

namespace tzhttpd {

class HttpParser;

typedef std::function<int (const HttpParser& http_parser, const std::string& post_data, std::string& response, std::string& status_line)> HttpPostHandler;
typedef std::function<int (const HttpParser& http_parser, std::string& response, std::string& status_line)> HttpGetHandler;

namespace http_handler {

int default_http_get_handler(const HttpParser& http_parser, std::string& response, std::string& status_line);

} // end namespace http_handler

} // end namespace tzhttpd


#endif //__TZHTTPD_HTTP_HANDLER_H__
