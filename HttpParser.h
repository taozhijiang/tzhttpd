/*-
 * Copyright (c) 2018 TAO Zhijiang<taozhijiang@gmail.com>
 *
 * Licensed under the BSD-3-Clause license, see LICENSE for full information.
 *
 */

#ifndef __TZHTTPD_HTTP_PARSER_H__
#define __TZHTTPD_HTTP_PARSER_H__

#include <map>
#include <sstream>
#include <iterator>
#include <string>

#include <boost/noncopyable.hpp>
#include <boost/algorithm/string.hpp>

// The GNU C++ standard library supports <regex>, but not until version 4.9.0.
// (The headers were present in earlier versions, but were unusable.)
#include <boost/regex.hpp>

#include "KVVec.h"
#include "Log.h"

#include "CryptoUtil.h"
#include "HttpProto.h"

namespace tzhttpd {

typedef KVVec<std::string, std::string> UriParamContainer;

enum HTTP_METHOD {
    GET = 1,
    POST = 2,
    UNKNOWN = 99,
};

class HttpParser: private boost::noncopyable {
public:
    HttpParser():
        request_headers_(),
        request_uri_params_(),
        method_(HTTP_METHOD::UNKNOWN) {
    }

    enum HTTP_METHOD get_method() const {
        return method_;
    }

    std::string get_uri() const {
        return uri_;
    }

    std::string get_version() const {
        return version_;
    }

    bool parse_request_header(const char* header_ptr) {
        if (!header_ptr || !strlen(header_ptr) || !strstr(header_ptr, "\r\n\r\n")) {
            tzhttpd_log_err( "check raw header package failed ...");
            return false;
        }

        return do_parse_request(std::string(header_ptr));
    }

    bool parse_request_header(const std::string& header) {
        return do_parse_request(header);
    }

    std::string find_request_header(std::string option_name) const {

        if (!option_name.size())
            return "";

        std::map<std::string, std::string>::const_iterator it;
        for (it = request_headers_.cbegin(); it!=request_headers_.cend(); ++it) {
            if (boost::iequals(option_name, it->first))
                return it->second;
        }

        return "";
    }

    bool parse_request_uri() {

        // clear it first!
        request_uri_params_.CLEAR();

        std::string uri = find_request_header(http_proto::header_options::request_uri);
        if (uri.empty()) {
            tzhttpd_log_err("Error found, head uri empty!");
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

        while(true) {

            // Find the '=' separating the name from its value,
            // also have to check for '&' as its a common misplaced delimiter but is a delimiter none the less
            pos = query_str.find_first_of( "&=", oldPos);

            // If no '=', we're finished
            if(std::string::npos == pos)
                break;

            // Decode the name
            // pos == '&', that means whatever is in name is the only name/value
            if( query_str.at(pos) == '&' ) {

                const char * pszData = query_str.c_str() + oldPos;
                while( *pszData == '&' ) { // eat up extraneous '&'
                    ++pszData; ++oldPos;
                }

                if( oldPos >= pos ) { // its all &'s
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

            if(std::string::npos == pos)
                break;

            // Update parse position
            oldPos = ++pos;
        }

        return true;
    }

    const UriParamContainer& get_request_uri_params() const {
        return request_uri_params_;
    }

    std::string get_request_uri_params_string() const {
        return request_uri_params_.SERIALIZE();
    }

    bool get_request_uri_param(const std::string& key, std::string& value) const {
        return request_uri_params_.FIND(key, value);
    }

    std::string char_to_hex(char c) const {

        std::string result;
        char first, second;

        first =  static_cast<char>((c & 0xF0) / 16);
        first += static_cast<char>(first > 9 ? 'A' - 10 : '0');
        second =  c & 0x0F;
        second += static_cast<char>(second > 9 ? 'A' - 10 : '0');

        result.append(1, first); result.append(1, second);
        return result;
    }

    char hex_to_char(char first, char second) const {
        int digit;

        digit = (first >= 'A' ? ((first & 0xDF) - 'A') + 10 : (first - '0'));
        digit *= 16;
        digit += (second >= 'A' ? ((second & 0xDF) - 'A') + 10 : (second - '0'));
        return static_cast<char>(digit);
    }


private:

    std::string normalize_request_uri(const std::string& uri){

        // 因为Linux文件系统是大小写敏感的，所以这里不会进行uri大小写的规则化
        const std::string src = boost::algorithm::trim_copy(uri);
        std::string result;
        result.reserve(src.size());

        for (std::string::const_iterator iter = src.begin(); iter != src.end(); ++iter) {
            if (*iter == '/') {
                while(std::distance(iter, src.end()) >= 1 && *(iter + 1) == '/')
                    ++ iter;
            }

            result.append(1, *iter); //store it!
        }

        return result;
    }

    bool do_parse_request(const std::string& header) {

        request_headers_.clear();
        request_headers_.insert(std::make_pair(http_proto::header_options::request_body,
                                               header.substr(header.find("\r\n\r\n") + 4)));
        std::string header_part = header.substr(0, header.find("\r\n\r\n") + 4);

        std::istringstream resp(header_part);
        std::string item;
        std::string::size_type index;

        while (std::getline(resp, item) && item != "\r") {
            index = item.find(':', 0);
            if(index != std::string::npos) { // 直接Key-Value
                request_headers_.insert(std::make_pair(
                        boost::algorithm::trim_copy(item.substr(0, index)),
                        boost::algorithm::trim_copy(item.substr(index + 1)) ));
            } else { // HTTP 请求行，特殊处理
                boost::smatch what;
                if (boost::regex_match(item, what,
                                       boost::regex("([a-zA-Z]+)[ ]+([^ ]+)([ ]+(.*))?"))) {
                    request_headers_.insert(std::make_pair(http_proto::header_options::request_method,
                                                       boost::algorithm::trim_copy(
                                                           boost::to_upper_copy(std::string(what[1])))));

                    // HTTP Method
                    if (boost::iequals(find_request_header(http_proto::header_options::request_method), "GET") ) {
                        method_ = HTTP_METHOD::GET;
                    } else if (boost::iequals(find_request_header(http_proto::header_options::request_method), "POST") ) {
                        method_ = HTTP_METHOD::POST;
                    } else {
                        method_ = HTTP_METHOD::UNKNOWN;
                    }

                    uri_ = normalize_request_uri(std::string(what[2]));
                    request_headers_.insert(std::make_pair(http_proto::header_options::request_uri, uri_));
                    request_headers_.insert(std::make_pair(http_proto::header_options::http_version, boost::algorithm::trim_copy(std::string(what[3]))));

                    version_ = boost::algorithm::trim_copy(std::string(what[3]));
                }
            }
        }

        return true;
    }

private:
    std::map<std::string, std::string> request_headers_;
    UriParamContainer request_uri_params_;
    enum HTTP_METHOD method_;
    std::string version_;
    std::string uri_;
};

} // end namespace tzhttpd

#endif // __TZHTTPD_HTTP_PARSER_H__
