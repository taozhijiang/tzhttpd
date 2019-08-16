/*-
 * Copyright (c) 2018-2019 TAO Zhijiang<taozhijiang@gmail.com>
 *
 * Licensed under the BSD-3-Clause license, see LICENSE for full information.
 *
 */

#ifndef __TZHTTPD_HTTP_EXECUTOR_H__
#define __TZHTTPD_HTTP_EXECUTOR_H__


#include <xtra_rhel.h>

#include <scaffold/Setting.h>

#include <boost/thread/locks.hpp>
#include <string/UriRegex.h>

#include "Executor.h"
#include "HttpProto.h"
#include "ServiceIf.h"
#include "HttpHandler.h"


namespace tzhttpd {

class BasicAuth;

class HttpExecutor : public ServiceIf {

public:

    explicit HttpExecutor(const std::string& hostname) :
        hostname_(hostname),
        EMPTY_STRING(),
        conf_lock_(),
        conf_ptr_(),
        default_get_handler_(),
        redirect_handler_(),
        rwlock_(),
        handlers_() {
    }


    bool init();

    void handle_http_request(std::shared_ptr<HttpReqInstance> http_req_instance)override;

    std::string instance_name()override {
        return hostname_;
    }

    ExecutorConf get_executor_conf() {
        return conf_ptr_->executor_conf_;
    }


    // override
    int add_get_handler(const std::string& uri_regex, const HttpGetHandler& handler, bool built_in)override;
    int add_post_handler(const std::string& uri_regex, const HttpPostHandler& handler, bool built_in)override;

    bool exist_handler(const std::string& uri_regex, enum HTTP_METHOD method)override;

    // 对于任何uri，可以先用这个接口进行卸载，然后再使用动态配置增加接口，借此实现接口的动态更新
    int drop_handler(const std::string& uri_regex, enum HTTP_METHOD method)override;



    int module_runtime(const libconfig::Config& conf)override;
    int module_status(std::string& strModule, std::string& strKey, std::string& strValue)override;


private:

    struct CgiHandlerCfg {
        std::string url_;
        std::string dl_path_;
    };
    bool parse_http_cgis(const libconfig::Setting& setting, const std::string& key,
                         std::map<std::string, CgiHandlerCfg>& handlerCfg);

    bool load_http_cgis(const libconfig::Setting& setting);
    bool handle_virtual_host_conf(const libconfig::Setting& setting);

    // 基本按照handle_virtual_host_conf的思路进行类似处理，
    // 不过动态更新可能考虑会忽略一些配置和错误
    int handle_virtual_host_runtime_conf(const libconfig::Setting& setting);


    // 路由选择算法
    int do_find_handler(const enum HTTP_METHOD& method,
                        const std::string& uri,
                        HttpHandlerObjectPtr& handler);

private:

    std::string hostname_;
    const std::string EMPTY_STRING;

    struct HttpExecutorConf {

        // 用来返回给Executor使用的，主要是线程伸缩相关的东西
        ExecutorConf executor_conf_;

        std::string http_docu_root_;
        std::vector<std::string> http_docu_index_;

        std::string redirect_str_;
        std::string redirect_code_;
        std::string redirect_uri_;

        // 缓存策略规则
        std::map<std::string, std::string> cache_controls_;

        // 压缩控制
        std::set<std::string> compress_controls_;

        // 认证支持
        std::unique_ptr<BasicAuth> http_auth_;
    };

    std::mutex conf_lock_;
    std::shared_ptr<HttpExecutorConf> conf_ptr_;

    bool pass_basic_auth(const std::string& uri, const std::string auth_str);

    // default http get handler, important for web_server
    HttpHandlerObjectPtr default_get_handler_;
    int default_get_handler(const HttpParser& http_parser, std::string& response,
                            std::string& status_line, std::vector<std::string>& add_header);

    // 30x 重定向使用
    // http redirect part
    HttpHandlerObjectPtr redirect_handler_;
    int http_redirect_handler(const HttpParser& http_parser, const std::string& post_data,
                              std::string& response,
                              std::string& status_line, std::vector<std::string>& add_header);


    // 使用vector保存handler，保证是先注册handler具有高优先级
    boost::shared_mutex rwlock_;
    std::vector<std::pair<roo::UriRegex, HttpHandlerObjectPtr>> handlers_;

};

} // end namespace tzhttpd


#endif // __TZHTTPD_HTTP_EXECUTOR_H__

