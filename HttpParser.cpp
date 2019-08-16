/*-
 * Copyright (c) 2019 TAO Zhijiang<taozhijiang@gmail.com>
 *
 * Licensed under the BSD-3-Clause license, see LICENSE for full information.
 *
 */

#include <map>
#include <sstream>
#include <iterator>
#include <string>

#include <boost/algorithm/string.hpp>

// The GNU C++ standard library supports <regex>, but not until version 4.9.0.
// (The headers were present in earlier versions, but were unusable.)
#include <boost/regex.hpp>

#include <container/PairVec.h>
#include <other/Log.h>

#include "CryptoUtil.h"
#include "HttpProto.h"

#include "HttpParser.h"

namespace tzhttpd {



std::string HttpParser::find_request_header(std::string option_name) const {

    if (option_name.empty())
        return "";

    std::map<std::string, std::string>::const_iterator it;
    for (it = request_headers_.cbegin(); it != request_headers_.cend(); ++it) {
        if (boost::iequals(option_name, it->first))
            return it->second;
    }

    return "";
}

bool HttpParser::parse_request_uri() {

    // clear it first!
    request_uri_params_.CLEAR();

    std::string uri = find_request_header(http_proto::header_options::request_uri);
    if (uri.empty()) {
        roo::log_err("Error found, head uri empty!");
        return false;
    }

    std::string::size_type item_idx = 0;
    item_idx = uri.find_first_of("?");
    if (item_idx == std::string::npos) {
        request_headers_.insert(std::make_pair(http_proto::header_options::request_path_info,
                                               CryptoUtil::url_decode(uri)));
        return true;
    }

    request_headers_.insert(std::make_pair(http_proto::header_options::request_path_info,
                                           CryptoUtil::url_decode(uri.substr(0, item_idx))));
    request_headers_.insert(std::make_pair(http_proto::header_options::request_query_str,
                                           uri.substr(item_idx + 1)));

    // do query string parse, from cgicc
    std::string name, value;
    std::string::size_type pos;
    std::string::size_type oldPos = 0;
    std::string query_str = find_request_header(http_proto::header_options::request_query_str);

    while (true) {

        // Find the '=' separating the name from its value,
        // also have to check for '&' as its a common misplaced delimiter but is a delimiter none the less
        pos = query_str.find_first_of("&=", oldPos);

        // If no '=', we're finished
        if (std::string::npos == pos)
            break;

        // Decode the name
        // pos == '&', that means whatever is in name is the only name/value
        if (query_str.at(pos) == '&') {

            const char* pszData = query_str.c_str() + oldPos;
            while (*pszData == '&') { // eat up extraneous '&'
                ++pszData;
                ++oldPos;
            }

            if (oldPos >= pos) { // its all &'s
                oldPos = ++pos;
                continue;
            }

            // this becomes an name with an empty value
            name = CryptoUtil::url_decode(query_str.substr(oldPos, pos - oldPos));
            request_uri_params_.PUSH_BACK(name, std::string(""));
            oldPos = ++pos;
            continue;
        }

        // else find the value
        name = CryptoUtil::url_decode(query_str.substr(oldPos, pos - oldPos));
        oldPos = ++pos;

        // Find the '&' or ';' separating subsequent name/value pairs
        pos = query_str.find_first_of(";&", oldPos);

        // Even if an '&' wasn't found the rest of the string is a value
        value = CryptoUtil::url_decode(query_str.substr(oldPos, pos - oldPos));

        // Store the pair
        request_uri_params_.PUSH_BACK(name, value);

        if (std::string::npos == pos)
            break;

        // Update parse position
        oldPos = ++pos;
    }

    return true;
}

std::string HttpParser::normalize_request_uri(const std::string& uri) {

    // 因为Linux文件系统是大小写敏感的，所以这里不会进行uri大小写的规则化
    const std::string src = boost::algorithm::trim_copy(uri);
    std::string result;
    result.reserve(src.size());

    for (std::string::const_iterator iter = src.begin(); iter != src.end(); ++iter) {
        if (*iter == '/') {
            while (std::distance(iter, src.end()) >= 1 && *(iter + 1) == '/')
                ++iter;
        }

        result.append(1, *iter); //store it!
    }

    return result;
}

bool HttpParser::do_parse_request(const std::string& header) {

    request_headers_.clear();
    request_headers_.insert(std::make_pair(http_proto::header_options::request_body,
                                           header.substr(header.find("\r\n\r\n") + 4)));
    std::string header_part = header.substr(0, header.find("\r\n\r\n") + 4);

    std::istringstream resp(header_part);
    std::string item;
    std::string::size_type index;

    while (std::getline(resp, item) && item != "\r") {

        // 重新编写，支持url中带有特殊字符
        if (boost::istarts_with(item, "GET ") ||
            boost::istarts_with(item, "HEAD ") ||
            boost::istarts_with(item, "POST ") ||
            boost::istarts_with(item, "PUT ") ||
            boost::istarts_with(item, "DELETE ") ||
            boost::istarts_with(item, "CONNECT ") ||
            boost::istarts_with(item, "OPTIONS ") ||
            boost::istarts_with(item, "TRACE ") ||
            boost::istarts_with(item, "MOVE ") ||
            boost::istarts_with(item, "COPY ") ||
            boost::istarts_with(item, "LINK ") ||
            boost::istarts_with(item, "UNLINK ") ||
            boost::istarts_with(item, "WRAPPED ") ||
            boost::istarts_with(item, "Extension-method ")
           ) {
            // HTTP 标准头
            boost::smatch what;
            if (boost::regex_match(item, what,
                                   boost::regex("([a-zA-Z]+)[ ]+([^ ]+)([ ]+(.*))?"))) {
                request_headers_.insert(std::make_pair(http_proto::header_options::request_method,
                                                       boost::algorithm::trim_copy(
                                                           boost::to_upper_copy(std::string(what[1])))));

                // HTTP Method
                if (boost::iequals(find_request_header(http_proto::header_options::request_method), "GET")) {
                    method_ = HTTP_METHOD::GET;
                } else if (boost::iequals(find_request_header(http_proto::header_options::request_method), "POST")) {
                    method_ = HTTP_METHOD::POST;
                } else if (boost::iequals(find_request_header(http_proto::header_options::request_method), "OPTIONS")) {
                    method_ = HTTP_METHOD::OPTIONS;
                } else {
                    method_ = HTTP_METHOD::UNDETECTED;
                }

                uri_ = normalize_request_uri(std::string(what[2]));
                request_headers_.insert(std::make_pair(http_proto::header_options::request_uri, uri_));
                request_headers_.insert(std::make_pair(http_proto::header_options::http_version,
                                                       boost::algorithm::trim_copy(std::string(what[3]))));

                version_ = boost::algorithm::trim_copy(std::string(what[3]));
            }
        } else {
            index = item.find(':', 0);
            if (index != std::string::npos) { // 直接Key-Value
                request_headers_.insert(std::make_pair(
                                            boost::algorithm::trim_copy(item.substr(0, index)),
                                            boost::algorithm::trim_copy(item.substr(index + 1))));
            } else {
                roo::log_err("unabled to handle line: %s", item.c_str());
            }
        }
    }

    return true;
}

} // end namespace tzhttpd
