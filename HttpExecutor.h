/*-
 * Copyright (c) 2018 TAO Zhijiang<taozhijiang@gmail.com>
 *
 * Licensed under the BSD-3-Clause license, see LICENSE for full information.
 *
 */

#ifndef __TZHTTPD_HTTP_EXECUTOR_H__
#define __TZHTTPD_HTTP_EXECUTOR_H__


#include <xtra_rhel6.h>

#include <libconfig.h++>

#include <boost/atomic/atomic.hpp>
#include <boost/thread/locks.hpp>

#include "StrUtil.h"

#include "HttpProto.h"
#include "ServiceIf.h"
#include "HttpHandler.h"
#include "BasicAuth.h"

namespace tzhttpd {


class HttpExecutor: public ServiceIf {

public:

    explicit HttpExecutor(const std::string& hostname):
        hostname_(hostname),
        http_docu_root_(),
        http_docu_index_(),
        EMPTY_STRING(),
        default_get_handler_(),
        redirect_handler_(),
        rwlock_(),
        handlers_(),
        cache_controls_(),
        compress_controls_(),
        http_auth_() {
    }


    bool init();

    void handle_http_request(std::shared_ptr<HttpReqInstance> http_req_instance);

    std::string instance_name() override {
        return hostname_;
    }

    // override
    int register_get_handler(const std::string& uri_regex, const HttpGetHandler& handler);
    int register_post_handler(const std::string& uri_regex, const HttpPostHandler& handler);

private:

    bool handle_vhost_conf(const libconfig::Setting& setting);

    // 路由选择算法
    int do_find_handler(const enum HTTP_METHOD& method,
                        const std::string& uri,
                        HttpHandlerObjectPtr& handler);


private:
    std::string hostname_;
    std::string http_docu_root_;
    std::vector<std::string> http_docu_index_;
    const std::string EMPTY_STRING;


    // default http get handler, important for web_server
    HttpHandlerObjectPtr default_get_handler_;
    int default_get_handler(const HttpParser& http_parser, std::string& response,
                            std::string& status_line, std::vector<std::string>& add_header);

    // 30x 重定向使用
    // http redirect part
    std::string redirect_str_;
    HttpHandlerObjectPtr redirect_handler_;
    int http_redirect_handler(std::string red_code, std::string red_uri,
                              const HttpParser& http_parser, const std::string& post_data,
                              std::string& response,
                              std::string& status_line, std::vector<std::string>& add_header);


    // 使用vector保存handler，保证是先注册handler具有高优先级
    boost::shared_mutex rwlock_;
    std::vector<std::pair<UriRegex, HttpHandlerObjectPtr>> handlers_;

    // 缓存策略规则
    std::map<std::string, std::string> cache_controls_;

    // 压缩控制
    std::set<std::string> compress_controls_;

    std::unique_ptr<BasicAuth> http_auth_;
};

} // tzhttpd


#endif // __TZHTTPD_HTTP_EXECUTOR_H__

