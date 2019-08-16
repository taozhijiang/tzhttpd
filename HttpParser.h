/*-
 * Copyright (c) 2018-2019 TAO Zhijiang<taozhijiang@gmail.com>
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
#include <xtra_rhel.h>

#include <boost/asio.hpp>

#include <container/PairVec.h>
#include <other/Log.h>

#include "CryptoUtil.h"
#include "HttpProto.h"

namespace tzhttpd {

typedef roo::PairVec<std::string, std::string> UriParamContainer;

class HttpParser {

    __noncopyable__(HttpParser)

public:
    HttpParser() :
        request_headers_(),
        request_uri_params_(),
        method_(HTTP_METHOD::UNDETECTED) {
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
            roo::log_err("check raw header package failed ...");
            return false;
        }

        return do_parse_request(std::string(header_ptr));
    }

    bool parse_request_header(const std::string& header) {
        return do_parse_request(header);
    }

    std::string find_request_header(std::string option_name) const;
    bool parse_request_uri();

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

        first = static_cast<char>((c & 0xF0) / 16);
        first += static_cast<char>(first > 9 ? 'A' - 10 : '0');
        second = c & 0x0F;
        second += static_cast<char>(second > 9 ? 'A' - 10 : '0');

        result.append(1, first);
        result.append(1, second);
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
    std::string normalize_request_uri(const std::string& uri);
    bool do_parse_request(const std::string& header);

private:

    std::map<std::string, std::string> request_headers_;
    UriParamContainer request_uri_params_;

    enum HTTP_METHOD method_;
    std::string version_;
    std::string uri_;

public:
    boost::asio::ip::tcp::endpoint remote_;
};

} // end namespace tzhttpd

#endif // __TZHTTPD_HTTP_PARSER_H__
