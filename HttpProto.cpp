/*-
 * Copyright (c) 2018 TAO Zhijiang<taozhijiang@gmail.com>
 *
 * Licensed under the BSD-3-Clause license, see LICENSE for full information.
 *
 */

#include <boost/date_time/posix_time/posix_time.hpp>
using namespace boost::posix_time;
using namespace boost::gregorian;

#include <sstream>
#include "HttpProto.h"


namespace tzhttpd {

namespace http_handler {
extern std::string              http_server_version;
}   // end namespace http_handler

namespace http_proto {

struct header {
  std::string name;
  std::string value;
};


/**
 * 由于最终的底层都是调用c_str()发送的，所以这里不添加额外的字符
 */
string http_response_generate(const string& content, const string& stat_str,
                              bool keepalive, const std::vector<std::string>& additional_header) {

    std::vector<header> headers(5);

    // reply fixed header
    headers[0].name = "Server";
    headers[0].value = "tzhttpd server/" + http_handler::http_server_version;
    headers[1].name = "Date";
    headers[1].value = to_simple_string(second_clock::universal_time());
    headers[2].name = "Content-Length";
    headers[2].value = std::to_string(static_cast<long long unsigned>(content.size()));

    headers[3].name = "Connection";
    if (keepalive) {
        headers[3].value = "keep-alive";   // 长连接
    } else {
        headers[3].value = "close";        // 短连接
    }

    headers[4].name = "Access-Control-Allow-Origin";
    headers[4].value = "*";

    string str = stat_str;
    str += header_crlf_str;
    for (size_t i=0; i< headers.size(); ++i) {
        str += headers[i].name;
        str += header_name_value_separator_str;
        str += headers[i].value;
        str += header_crlf_str;
    }

    for (auto iter = additional_header.begin(); iter != additional_header.end(); ++ iter) {
        str += *iter;
        str += header_crlf_str;
    }

    str += header_crlf_str;
    str += content;

    return str;
}

string http_response_generate(const char* data, size_t len, const string& stat_str,
                              bool keepalive, const std::vector<std::string>& additional_header) {

    std::string content(data, len);
    return http_response_generate(content, stat_str, keepalive, additional_header);
}


static std::string get_status_content(enum StatusCode code) {
    const auto iter = status_code_strings.find(code);
    if(iter != status_code_strings.end()) {
        return iter->second;
    }

    return "";
}

string http_std_response_generate(const std::string& http_ver, enum StatusCode stat, bool keepalive) {

    std::stringstream content_ss;
    std::string msg = get_status_content(stat);

    content_ss << "<html><head><title>"
               << msg
               << "</title></head>"
               << "<body><h1>"
               << msg
               << "</h1></body></html>";

    std::string content = content_ss.str();
    std::vector<std::string> hd {"Content-Type: text/html"};
    std::string status_line = generate_response_status_line(http_ver, http_proto::StatusCode::success_ok);

    return http_response_generate(content, status_line, keepalive, hd);
}


}  // end namespace http_proto
}  // end namespace tzhttpd

