/*-
 * Copyright (c) 2018-2019 TAO Zhijiang<taozhijiang@gmail.com>
 *
 * Licensed under the BSD-3-Clause license, see LICENSE for full information.
 *
 */

#ifndef __TZHTTPD_CGI_WRAPPER_H__
#define __TZHTTPD_CGI_WRAPPER_H__

// 所有的http uri 路由

#include <libconfig.h++>

#include <vector>
#include <string>
#include <memory>

#include <boost/algorithm/string.hpp>
#include <boost/regex.hpp>
#include <boost/atomic/atomic.hpp>

#include "CgiHelper.h"
#include "SlibLoader.h"

namespace tzhttpd {

class HttpParser;

namespace http_handler {


class CgiWrapper {
public:
    explicit CgiWrapper(const std::string& dl_path):
        dl_path_(dl_path),
        dl_({}) {
    }

    bool load_dl();

protected:
    std::string dl_path_;
    std::shared_ptr<SLibLoader> dl_;
};


class CgiGetWrapper: public CgiWrapper {

public:
    explicit CgiGetWrapper(const std::string& dl_path):
        CgiWrapper(dl_path) {
    }

    bool init();

    int operator()(const HttpParser& http_parser,
                   std::string& response, std::string& status_line,
                   std::vector<std::string>& add_header);

private:
    cgi_get_handler_t func_;
};



class CgiPostWrapper: public CgiWrapper {

public:

    explicit CgiPostWrapper(const std::string& dl_path):
        CgiWrapper(dl_path) {
    }

    bool init();

    int operator()(const HttpParser& http_parser, const std::string& post_data,
                   std::string& response, std::string& status_line,
                   std::vector<std::string>& add_header) ;

private:
    cgi_post_handler_t func_;
};


} // end namespace http_handler
} // end namespace tzhttpd


#endif //__TZHTTPD_CGI_WRAPPER_H__
