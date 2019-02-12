/*-
 * Copyright (c) 2018 TAO Zhijiang<taozhijiang@gmail.com>
 *
 * Licensed under the BSD-3-Clause license, see LICENSE for full information.
 *
 */


#include <ctime>
#include <boost/chrono.hpp>

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

    std::time_t now = boost::chrono::system_clock::to_time_t(boost::chrono::system_clock::now());
    std::string time_str = std::string(std::ctime(&now));
    headers[1].value = time_str.erase(time_str.find('\n')); // ctime 会在末尾增加一个 \n
//    headers[1].value = to_simple_string(second_clock::universal_time()) + " GMT";

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

string http_std_response_generate(const std::string& http_ver, const std::string& stat_str, bool keepalive) {

    std::stringstream content_ss;

    content_ss << "<html><head><title>"
               << stat_str
               << "</title></head>"
               << "<body><h1>"
               << stat_str
               << "</h1></body></html>";

    std::string content = content_ss.str();
    std::vector<std::string> hd {"Content-Type: text/html"};

    return http_response_generate(content, stat_str, keepalive, hd);
}


static const std::map<std::string, std::string> content_types = {
    { ".avi", "Content-Type: video/x-msvideo" },
    { ".bin", "Content-Type: application/octet-stream" },
    { ".bmp", "Content-Type: image/bmp" },
    { ".bz", "Content-Type: application/x-bzip" },
    { ".bz2", "Content-Type: application/x-bzip2" },
    { ".csh", "Content-Type: application/x-csh" },
    { ".css", "Content-Type: text/css" },
    { ".csv", "Content-Type: text/csv" },
    { ".doc", "Content-Type: application/msword" },
    { ".docx", "Content-Type: application/vnd.openxmlformats-officedocument.wordprocessingml.document" },
    { ".eot", "Content-Type: application/vnd.ms-fontobject" },
    { ".epub", "Content-Type: application/epub+zip" },
    { ".gif", "Content-Type: image/gif" },
    { ".htm", "Content-Type: text/html" },
    { ".html", "Content-Type: text/html" },
    { ".ico", "Content-Type: image/x-icon" },
    { ".jar", "Content-Type: application/java-archive" },
    { ".jpeg", "Content-Type: image/jpeg" },
    { ".jpg", "Content-Type: image/jpeg" },
    { ".js", "Content-Type: application/javascript" },
    { ".json", "Content-Type: application/json" },
    { ".mid", "Content-Type: audio/midi" },
    { ".midi", "Content-Type: audio/midi" },
    { ".mpeg", "Content-Type: video/mpeg" },
    { ".odp", "Content-Type: application/vnd.oasis.opendocument.presentation" },
    { ".ods", "Content-Type: application/vnd.oasis.opendocument.spreadsheet" },
    { ".odt", "Content-Type: application/vnd.oasis.opendocument.text" },
    { ".oga", "Content-Type: audio/ogg" },
    { ".ogv", "Content-Type: video/ogg" },
    { ".ogx", "Content-Type: application/ogg" },
    { ".otf", "Content-Type: font/otf" },
    { ".png", "Content-Type: image/png" },
    { ".pdf", "Content-Type: application/pdf" },
    { ".ppt", "Content-Type: application/vnd.ms-powerpoint" },
    { ".pptx", "Content-Type: application/vnd.openxmlformats-officedocument.presentationml.presentation" },
    { ".rar", "Content-Type: application/x-rar-compressed" },
    { ".rtf", "Content-Type: application/rtf" },
    { ".sh", "Content-Type: application/x-sh" },
    { ".svg", "Content-Type: image/svg+xml" },
    { ".swf", "Content-Type: application/x-shockwave-flash" },
    { ".tar", "Content-Type: application/x-tar" },
    { ".tif", "Content-Type: image/tiff" },
    { ".tiff", "Content-Type: image/tiff" },
    { ".ts", "Content-Type: application/typescript" },
    { ".ttf", "Content-Type: font/ttf" },
    { ".txt", "Content-Type: text/plain" },
    { ".vsd", "Content-Type: application/vnd.visio" },
    { ".wav", "Content-Type: audio/wav" },
    { ".woff", "Content-Type: font/woff" },
    { ".woff2", "Content-Type: font/woff2" },
    { ".xhtml", "Content-Type: application/xhtml+xml" },
    { ".xls", "Content-Type: application/vnd.ms-excel" },
    { ".xlsx", "Content-Type: application/vnd.openxmlformats-officedocument.spreadsheetml.sheet" },
    { ".xml", "Content-Type: application/xml" },
    { ".zip", "Content-Type: application/zip" },
    { ".3gp", "Content-Type: video/3gpp" },
    { ".7z", "Content-Type: application/x-7z-compressed" },
};

std::string find_content_type(const std::string& suffix) {
    auto iter = content_types.find(suffix);
    if (iter != content_types.cend()) {
        return iter->second;
    }

    return "";
}

}  // end namespace http_proto
}  // end namespace tzhttpd

