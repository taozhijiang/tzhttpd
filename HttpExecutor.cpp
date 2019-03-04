/*-
 * Copyright (c) 2018-2019 TAO Zhijiang<taozhijiang@gmail.com>
 *
 * Licensed under the BSD-3-Clause license, see LICENSE for full information.
 *
 */

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#include <fstream>
#include <sstream>
#include <boost/algorithm/string.hpp>

#include "ConfHelper.h"
#include "HttpParser.h"
#include "HttpExecutor.h"
#include "HttpReqInstance.h"
#include "BasicAuth.h"

#include "CgiHelper.h"
#include "CgiWrapper.h"
#include "SlibLoader.h"

#include "CryptoUtil.h"

#include "Log.h"

namespace tzhttpd {
namespace http_handler {
// init only once at startup, these are the default value
std::string              http_server_version = "2.1.0";
} // end namespace http_handler


using namespace http_proto;

// 默认静态的Handler，从文件系统读取文件病发送
static bool check_and_sendfile(const HttpParser& http_parser, std::string regular_file_path,
                               std::string& response, std::string& status_line) {

    // check dest is directory or regular?
    struct stat sb;
    if (stat(regular_file_path.c_str(), &sb) == -1) {
        tzhttpd_log_err("Stat file error: %s", regular_file_path.c_str());
        response = http_proto::content_error;
        status_line = generate_response_status_line(http_parser.get_version(),
                StatusCode::server_error_internal_server_error);
        return false;
    }

    if (sb.st_size > 100*1024*1024 /*100M*/) {
        tzhttpd_log_err("Too big file size: %ld", sb.st_size);
        response = http_proto::content_bad_request;
        status_line = generate_response_status_line(http_parser.get_version(),
                StatusCode::client_error_bad_request);
        return false;
    }

    std::ifstream fin(regular_file_path);
    fin.seekg(0);
    std::stringstream buffer;
    buffer << fin.rdbuf();
    response = buffer.str();
    status_line = generate_response_status_line(http_parser.get_version(),
            StatusCode::success_ok);

    return true;
}



int HttpExecutor::default_get_handler(const HttpParser& http_parser, std::string& response,
                                      std::string& status_line, std::vector<std::string>& add_header) {

    const UriParamContainer& params = http_parser.get_request_uri_params();
    if (!params.EMPTY()) {
        tzhttpd_log_err("Default handler just for static file transmit, we can not handler uri parameters...");
    }

    std::shared_ptr<HttpExecutorConf> conf_ptr;
    {
        std::unique_lock<std::mutex> lock(conf_lock_);
        conf_ptr = conf_ptr_;
    }

    std::string real_file_path = conf_ptr_->http_docu_root_ +
            "/" + http_parser.find_request_header(http_proto::header_options::request_path_info);

    // check dest exist?
    if (::access(real_file_path.c_str(), R_OK) != 0) {
        tzhttpd_log_err("File not found: %s", real_file_path.c_str());
        response = http_proto::content_not_found;
        status_line = generate_response_status_line(http_parser.get_version(),
                StatusCode::client_error_not_found);
        return -1;
    }

    // check dest is directory or regular?
    struct stat sb;
    if (stat(real_file_path.c_str(), &sb) == -1) {
        tzhttpd_log_err("Stat file error: %s", real_file_path.c_str());
        response = http_proto::content_error;
        status_line = generate_response_status_line(http_parser.get_version(),
                StatusCode::server_error_internal_server_error);
        return -1;
    }

    bool OK = false;
    std::string did_file_full_path {};

    switch (sb.st_mode & S_IFMT) {
        case S_IFREG:
            if(check_and_sendfile(http_parser, real_file_path, response, status_line)) {
                did_file_full_path = real_file_path;
                OK = true;
            }
            break;

        case S_IFDIR:
            {
                const std::vector<std::string>& indexes = conf_ptr->http_docu_index_;
                for (std::vector<std::string>::const_iterator iter = indexes.cbegin();
                      iter != indexes.cend();
                      ++iter) {
                    std::string file_path = real_file_path + "/" + *iter;
                    tzhttpd_log_info("Trying: %s", file_path.c_str());
                    if (check_and_sendfile(http_parser, file_path, response, status_line)) {
                        did_file_full_path = file_path;
                        OK = true;
                        break;
                    }
                }

                if (!OK) {
                    // default, 404
                    response = http_proto::content_not_found;
                    status_line = generate_response_status_line(http_parser.get_version(),
                            StatusCode::client_error_not_found);
                }
            }
            break;

        default:
            break;
    }

    // do handler helper
    if (OK && !did_file_full_path.empty()) {

        boost::to_lower(did_file_full_path);

        // 取出扩展名
        std::string::size_type pos = did_file_full_path.rfind(".");
        std::string suffix {};
        if (pos != std::string::npos && (did_file_full_path.size() - pos) < 6) {
            suffix = did_file_full_path.substr(pos);
        }

        if (suffix.empty()) {
            return 0;
        }

        // handle with specified suffix, for cache and content_type
        auto iter = conf_ptr->cache_controls_.find(suffix);
        if (iter != conf_ptr->cache_controls_.end()) {
            add_header.push_back(iter->second);
            tzhttpd_log_debug("Adding cache header for %s(%s) -> %s",
                              did_file_full_path.c_str(), iter->first.c_str(), iter->second.c_str());
        }

        std::string content_type = http_proto::find_content_type(suffix);
        if (!content_type.empty()) {
            add_header.push_back(content_type);
            tzhttpd_log_debug("Adding content_type header for %s(%s) -> %s",
                              did_file_full_path.c_str(), suffix.c_str(), content_type.c_str());
        }

        // compress type
        // content already in response, compress it if possible
        const auto cz_iter = conf_ptr->compress_controls_.find(suffix);
        if (cz_iter != conf_ptr->compress_controls_.cend()) {
            std::string encoding = http_parser.find_request_header(http_proto::header_options::accept_encoding);
            if (!encoding.empty()) {
                tzhttpd_log_debug("Accept Encoding: %s", encoding.c_str());
                if (encoding.find("deflate") != std::string::npos) {

                    std::string compressed {};

                    if (CryptoUtil::Deflator(response, compressed) == 0) {
                        tzhttpd_log_debug("compress %s size from %d to %d", did_file_full_path.c_str(),
                                          static_cast<int>(response.size()), static_cast<int>(compressed.size()));
                        response.swap(compressed);
                        add_header.push_back("Content-Encoding: deflate");
                    } else {
                        tzhttpd_log_err("cryptopp deflate encoding failed.");
                    }

                } else if (encoding.find("gzip") != std::string::npos) {

                    std::string compressed {};

                    if (CryptoUtil::Gzip(response, compressed) == 0) {
                        tzhttpd_log_debug("compress %s size from %d to %d", did_file_full_path.c_str(),
                                          static_cast<int>(response.size()), static_cast<int>(compressed.size()));
                        response.swap(compressed);
                        add_header.push_back("Content-Encoding: gzip");
                    } else {
                        tzhttpd_log_err("cryptopp gzip encoding failed.");
                    }

                } else {
                    tzhttpd_log_err("unregistered compress type: %s", encoding.c_str());
                }
            }
        }
    }

    return 0;
}


bool HttpExecutor::init() override {

    auto conf_ptr = ConfHelper::instance().get_conf();
	if(!conf_ptr) {
		tzhttpd_log_err("ConfHelper not initialized? return conf_ptr empty!!!");
		return false;
	}

	bool init_success = false;

    try
    {
        const libconfig::Setting& http_vhosts = conf_ptr->lookup("http.vhosts");
        for(int i = 0; i < http_vhosts.getLength(); ++i) {

            const libconfig::Setting& vhost = http_vhosts[i];
            std::string server_name;
            ConfUtil::conf_value(vhost, "server_name", server_name);
            if (server_name.empty() ) {
                tzhttpd_log_err("check virtual host conf, required server_name not found, skip this one.");
                continue;
            }

            // tzhttpd_log_debug("detected server_name: %s", server_name.c_str());

            // 发现是匹配的，则找到对应虚拟主机的配置文件了
            if (server_name == hostname_) {
                if (!handle_virtual_host_conf(vhost)) {
                    tzhttpd_log_err("handle detail conf for %s failed.", server_name.c_str());
                    return false;
                }

                tzhttpd_log_debug("handle detail conf for host %s success!", server_name.c_str());
                // OK
                init_success = true;
                break;
            }
        }

    } catch (const libconfig::SettingNotFoundException &nfex) {
        tzhttpd_log_err("http.vhosts not found!");
    } catch (std::exception& e) {
        tzhttpd_log_err("execptions catched for %s",  e.what());
    }

	if(!init_success) {
		tzhttpd_log_err("host %s init failed, may not configure for it?", hostname_.c_str());
	}
    return init_success;
}

bool HttpExecutor::parse_http_cgis(const libconfig::Setting& setting, const std::string& key,
                                    std::map<std::string, CgiHandlerCfg>& handlerCfg) {

    if (!setting.exists(key)) {
        tzhttpd_log_notice("vhost:%s handlers for %s not found!",
                           hostname_.c_str(), key.c_str());
        return true;
    }

    handlerCfg.clear();
    const libconfig::Setting &http_cgi_handlers = setting[key];

    for(int i = 0; i < http_cgi_handlers.getLength(); ++i) {

        const libconfig::Setting& handler = http_cgi_handlers[i];
        std::string uri_path {};
        std::string dl_path {};

        ConfUtil::conf_value(handler, "uri", uri_path);
        ConfUtil::conf_value(handler, "dl_path", dl_path);

        if(uri_path.empty() || dl_path.empty()) {
            tzhttpd_log_err("vhost:%s skip err configure item %s:%s...",
                            hostname_.c_str(), uri_path.c_str(), dl_path.c_str());
            continue;
        }

        tzhttpd_log_debug("vhost:%s detect handler uri:%s, dl_path:%s",
                          hostname_.c_str(), uri_path.c_str(), dl_path.c_str());

        CgiHandlerCfg cfg {};
        cfg.url_ = uri_path;
        cfg.dl_path_ = dl_path;

        handlerCfg[uri_path] = cfg;
    }

    return true;
}



bool HttpExecutor::load_http_cgis(const libconfig::Setting& setting) {

    std::string key;
    std::map<std::string, CgiHandlerCfg> cgimap {};

    key = "cgi_get_handlers";
    parse_http_cgis(setting, key, cgimap);

    for (auto iter = cgimap.cbegin(); iter != cgimap.cend(); ++ iter) {

        // we will not override handler directly, consider using
        // /internal_manage manipulate
        if (exist_handler(iter->first, HTTP_METHOD::GET)) {
            tzhttpd_log_alert("[vhost:%s] HttpGet for %s already exists, skip it.",
                              hostname_.c_str(), iter->first.c_str());
            continue;
        }

        http_handler::CgiGetWrapper getter(iter->second.dl_path_);
        if (!getter.init()) {
            tzhttpd_log_err("[vhost:%s] init get for %s @ %s failed, skip it!",
                            hostname_.c_str(), iter->first.c_str(),
                            iter->second.dl_path_.c_str());
            continue;
        }

        add_get_handler(iter->first, getter, false);
    }

    key = "cgi_post_handlers";
    parse_http_cgis(setting, key, cgimap);

    for (auto iter = cgimap.cbegin(); iter != cgimap.cend(); ++ iter) {

        // we will not override handler directly, consider using
        // /internal_manage manipulate
        if (exist_handler(iter->first, HTTP_METHOD::POST)) {
            tzhttpd_log_alert("[vhost:%s] HttpPost for %s already exists, skip it.",
                              hostname_.c_str(), iter->first.c_str());
            continue;
        }

        http_handler::CgiPostWrapper poster(iter->second.dl_path_);
        if (!poster.init()) {
            tzhttpd_log_err("[vhost:%s] init post for %s @ %s failed, skip it!",
                            hostname_.c_str(), iter->first.c_str(),
                            iter->second.dl_path_.c_str());
            continue;
        }

        add_post_handler(iter->first, poster, false);
    }

    return true;
}

// 系统启动时候初始化，持有整个锁进行
bool HttpExecutor::handle_virtual_host_conf(const libconfig::Setting& setting) {

    std::unique_lock<std::mutex> lock(conf_lock_);

    if (!conf_ptr_) {
        conf_ptr_.reset(new HttpExecutorConf());
        if (!conf_ptr_) {
            tzhttpd_log_err("create HttpExecutorConf instance failed.");
            return false;
        }
    }

    std::string server_name;
    std::string redirect_str;
    std::string docu_root_str;
    std::string docu_index_str;

    ConfUtil::conf_value(setting, "server_name", server_name);
    ConfUtil::conf_value(setting, "redirect", redirect_str);
    ConfUtil::conf_value(setting, "docu_root", docu_root_str);
    ConfUtil::conf_value(setting, "docu_index", docu_index_str);

    ConfUtil::conf_value(setting, "exec_thread_pool_size", conf_ptr_->executor_conf_.exec_thread_number_);
    ConfUtil::conf_value(setting, "exec_thread_pool_size_hard", conf_ptr_->executor_conf_.exec_thread_number_hard_);
    ConfUtil::conf_value(setting, "exec_thread_pool_step_queue_size", conf_ptr_->executor_conf_.exec_thread_step_queue_size_);


    if (!redirect_str.empty()) {

        conf_ptr_->redirect_str_ = redirect_str;

        auto pos = redirect_str.find('~');
        if (pos == std::string::npos) {
            tzhttpd_log_err("error redirect config: %s", redirect_str.c_str());
            return false;
        }

        std::string code = boost::trim_copy(redirect_str.substr(0, pos));
        std::string uri  = boost::trim_copy(redirect_str.substr(pos+1));

        if (code != "301" && code != "302") {
            tzhttpd_log_err("error redirect config: %s", redirect_str.c_str());
            return false;
        }

        conf_ptr_->redirect_code_ = code;
        conf_ptr_->redirect_uri_ = uri;

        HttpGetHandler get_func =
                std::bind(&HttpExecutor::http_redirect_handler, this,
                          std::placeholders::_1, EMPTY_STRING,
                          std::placeholders::_2,
                          std::placeholders::_3, std::placeholders::_4 );
        HttpPostHandler post_func =
                std::bind(&HttpExecutor::http_redirect_handler, this,
                          std::placeholders::_1, std::placeholders::_2,
                          std::placeholders::_3, std::placeholders::_4,
                          std::placeholders::_5 );


        redirect_handler_.reset(new HttpHandlerObject("[redirect]", get_func, post_func, true));
        if (!redirect_handler_ || !redirect_handler_->http_get_handler_ || !redirect_handler_->http_post_handler_) {
            tzhttpd_log_err("Create redirect handler for %s failed!", hostname_.c_str());
            return false;
        }

        // configured redirect, pass following configure
        tzhttpd_log_alert("redirect %s configure ok for host %s",
                          conf_ptr_->redirect_str_.c_str(), hostname_.c_str());

        // redirect 虚拟主机只需要这个配置就可以了
        return true;


    } else if (!docu_root_str.empty() && !docu_index_str.empty()) {


        std::vector<std::string> docu_index{};
        {
            std::vector<std::string> vec {};
            boost::split(vec, docu_index_str, boost::is_any_of(";"));
            for (auto iter = vec.begin(); iter != vec.cend(); ++ iter){
                std::string tmp = boost::trim_copy(*iter);
                if (tmp.empty())
                    continue;

                docu_index.push_back(tmp);
            }
            if (docu_index.empty()) { // not fatal
                tzhttpd_log_err("empty valid docu_index found, previous: %s", docu_index_str.c_str());
            }
        }


        conf_ptr_->http_docu_root_ = docu_root_str;
        conf_ptr_->http_docu_index_ = docu_index;

        tzhttpd_log_debug("docu_root: %s, index items: %lu",
                          conf_ptr_->http_docu_root_.c_str(), conf_ptr_->http_docu_index_.size());

        // fall throught following configure

    } else {

        tzhttpd_log_err("required at lease document setting or redirect setting for %s.", hostname_.c_str());
        return false;

    }


    // Cgi配置处理
    load_http_cgis(setting);


    // 默认的Get Handler，主要用于静态web服务器使用

    // 注册默认的 static filesystem handler
    HttpGetHandler func = std::bind(&HttpExecutor::default_get_handler, this,
                          std::placeholders::_1, std::placeholders::_2,
                          std::placeholders::_3, std::placeholders::_4);
    default_get_handler_.reset(new HttpHandlerObject(EMPTY_STRING, func, true));
    if (!default_get_handler_) {
        tzhttpd_log_err("init default http get handler failed.");
        return false;
    }


    if (setting.exists("cache_control")) {
        const libconfig::Setting &http_cache_control = setting["cache_control"];
        for(int i = 0; i < http_cache_control.getLength(); ++i) {
            const libconfig::Setting& ctrl_item = http_cache_control[i];
            std::string suffix {};
            std::string ctrl_head {};

            ConfUtil::conf_value(ctrl_item, "suffix", suffix);
            ConfUtil::conf_value(ctrl_item, "header", ctrl_head);
            if(suffix.empty() || ctrl_head.empty()) {
                tzhttpd_log_err("skip err cache ctrl configure item ...");
                continue;
            }

            // parse
            {
                std::vector<std::string> suffixes {};
                boost::split(suffixes, suffix, boost::is_any_of(";"));
                for (auto iter = suffixes.begin(); iter != suffixes.cend(); ++ iter){
                    std::string tmp = boost::trim_copy(*iter);
                    if (tmp.empty())
                        continue;

                    conf_ptr_->cache_controls_[tmp] = ctrl_head;
                }
            }
        }

        // total display
        tzhttpd_log_debug("total %d cache ctrl for vhost %s",
                          static_cast<int>(conf_ptr_->cache_controls_.size()), hostname_.c_str());
        for (auto iter = conf_ptr_->cache_controls_.begin(); iter != conf_ptr_->cache_controls_.end(); ++iter) {
            tzhttpd_log_debug("%s => %s", iter->first.c_str(), iter->second.c_str());
        }
    }

    // basic_auth
    if (setting.exists("basic_auth")) {
        conf_ptr_->http_auth_.reset(new BasicAuth());
        if (!conf_ptr_->http_auth_ || !conf_ptr_->http_auth_->init(setting, true)) {
            tzhttpd_log_err("init basic_auth for vhost %s failed.", hostname_.c_str());
            return false;
        }
    }

    if (setting.exists("compress_control")) {

        std::string suffix {};
        ConfUtil::conf_value(setting, "compress_control", suffix);

        std::vector<std::string> suffixes {};
        boost::split(suffixes, suffix, boost::is_any_of(";"));
        for (auto iter = suffixes.begin(); iter != suffixes.cend(); ++ iter){
            std::string tmp = boost::trim_copy(*iter);
            if (tmp.empty())
                continue;

            conf_ptr_->compress_controls_.insert(tmp);
        }

        tzhttpd_log_debug("total %d compress ctrl for vhost %s",
                          static_cast<int>(conf_ptr_->compress_controls_.size()), hostname_.c_str());
    }

    return true;
}

bool HttpExecutor::exist_handler(const std::string& uri_regex, enum HTTP_METHOD method) {

    std::string uri = StrUtil::pure_uri_path(uri_regex);
    boost::shared_lock<boost::shared_mutex> rlock(rwlock_);

    std::vector<std::pair<UriRegex, HttpHandlerObjectPtr>>::iterator it;
    for (it = handlers_.begin(); it != handlers_.end(); ++it) {

        if (it->first.str() == uri) {

            if (method == HTTP_METHOD::GET && it->second->http_get_handler_) {
                return true;
            }
            else if (method == HTTP_METHOD::POST && it->second->http_post_handler_) {
                return true;
            }
            else if (method == HTTP_METHOD::ALL && (it->second->http_get_handler_ || it->second->http_post_handler_)) {
                return true;
            }

            tzhttpd_log_err("Confused request: %s, handler method: GET %s, POST %s", uri_regex.c_str(),
                            it->second->http_get_handler_ ? "YES" : "NO",
                            it->second->http_post_handler_ ? "YES" : "NO" );
            return false;
        }
    }

    return false;
}


int HttpExecutor::drop_handler(const std::string& uri_regex, enum HTTP_METHOD method) {

    std::string uri = StrUtil::pure_uri_path(uri_regex);
    boost::lock_guard<boost::shared_mutex> wlock(rwlock_);

    std::vector<std::pair<UriRegex, HttpHandlerObjectPtr>>::iterator it;
    for (it = handlers_.begin(); it != handlers_.end(); ++it) {
        if (it->first.str() == uri) {
            if (it->second->built_in_) {
                tzhttpd_log_err("can not drop built_in hander ");
                return -1;
            }

            // 删除的时候先检测剩余的handler是否存在，如果存在就直接删除当前method的
            // 否则就fall through删除整个object
            if (method == HTTP_METHOD::GET) {
                tzhttpd_log_notice("drop get handler for host %s, uri: %s", hostname_.c_str(),  uri.c_str());
                if (it->second->http_post_handler_) {
                    it->second->http_get_handler_ = HttpGetHandler();  // empty
                    SAFE_ASSERT(it->second->http_get_handler_);
                    return 0;
                }
            }
            else if (method == HTTP_METHOD::POST) {
                tzhttpd_log_notice("drop post handler for host %s, uri: %s", hostname_.c_str(),  uri.c_str());
                if (it->second->http_get_handler_) {
                    it->second->http_post_handler_ = HttpPostHandler();  // empty
                    SAFE_ASSERT(it->second->http_post_handler_);
                    return 0;
                }
            }

            tzhttpd_log_notice("remove whole handler object for host %s, uri: %s!", hostname_.c_str(),  uri.c_str());
            handlers_.erase(it);

            return 0;
        }
    }

    tzhttpd_log_notice("handler for host %s, uri: %s not found!", hostname_.c_str(),  uri.c_str());
    return -1;
}



int HttpExecutor::add_get_handler(const std::string& uri_regex, const HttpGetHandler& handler, bool built_in) override {

    std::string uri = StrUtil::pure_uri_path(uri_regex);
    boost::lock_guard<boost::shared_mutex> wlock(rwlock_);

    std::vector<std::pair<UriRegex, HttpHandlerObjectPtr>>::iterator it;
    for (it = handlers_.begin(); it != handlers_.end(); ++it) {
        if (it->first.str() == uri ) {
            tzhttpd_log_debug("hostname:%s GetHandler for %s(%s) already exists, update it!",
                              hostname_.c_str(), uri.c_str(), uri_regex.c_str());
            it->second->update_get_handler(handler);
            return 0;
        }
    }

    tzhttpd_log_debug("hostname:%s GetHandler for %s(%s) does not exists, create it!",
                      hostname_.c_str(), uri.c_str(), uri_regex.c_str());
    UriRegex rgx {uri};
    auto phandler_obj = std::make_shared<HttpHandlerObject>(uri, handler, built_in);
    if (!phandler_obj) {
        tzhttpd_log_err("hostname:%s Create Handler object for %s(%s) failed.",
                        hostname_.c_str(), uri.c_str(), uri_regex.c_str());
        return -1;
    }

    handlers_.push_back({ rgx, phandler_obj });

    tzhttpd_log_notice("hostname:%s register_http_get_handler for %s(%s) OK!",
                      hostname_.c_str(), uri.c_str(), uri_regex.c_str());
    return 0;
}


int HttpExecutor::add_post_handler(const std::string& uri_regex, const HttpPostHandler& handler, bool built_in) override {

    std::string uri = StrUtil::pure_uri_path(uri_regex);
    boost::lock_guard<boost::shared_mutex> wlock(rwlock_);

    std::vector<std::pair<UriRegex, HttpHandlerObjectPtr>>::iterator it;
    for (it = handlers_.begin(); it != handlers_.end(); ++it) {
        if (it->first.str() == uri ) {
            tzhttpd_log_debug("hostname:%s PostHandler for %s(%s) already exists, update it!",
                              hostname_.c_str(), uri.c_str(), uri_regex.c_str());
            it->second->update_post_handler(handler);
            return 0;
        }
    }

    tzhttpd_log_debug("hostname:%s PostHandler for %s(%s) does not exists, create it!",
                      hostname_.c_str(), uri.c_str(), uri_regex.c_str());
    UriRegex rgx {uri};
    auto phandler_obj = std::make_shared<HttpHandlerObject>(uri, handler, built_in);
    if (!phandler_obj) {
        tzhttpd_log_err("hostname:%s Create Handler object for %s(%s) failed.",
                        hostname_.c_str(), uri.c_str(), uri_regex.c_str());
        return -1;
    }

    handlers_.push_back({ rgx, phandler_obj });

    tzhttpd_log_notice("hostname:%s register_http_post_handler for %s(%s) OK!",
                       hostname_.c_str(), uri.c_str(), uri_regex.c_str());
    return 0;
}

bool HttpExecutor::pass_basic_auth(const std::string& uri, const std::string auth_str) {

    std::shared_ptr<HttpExecutorConf> conf_ptr;
    {
        std::unique_lock<std::mutex> lock(conf_lock_);
        conf_ptr = conf_ptr_;
    }

    if (!conf_ptr->http_auth_) {
        return true;
    }

    return conf_ptr->http_auth_->check_auth(uri, auth_str);
}


int HttpExecutor::do_find_handler(const enum HTTP_METHOD& method,
                                  const std::string& uri,
                                  HttpHandlerObjectPtr& handler) {

    if (redirect_handler_) {
        tzhttpd_log_debug("redirect handler found, will do redirect.");
        handler = redirect_handler_;
        return 0;
    }

    std::string n_uri = StrUtil::pure_uri_path(uri);

    boost::shared_lock<boost::shared_mutex> rlock(rwlock_);

    std::vector<std::pair<UriRegex, HttpHandlerObjectPtr>>::const_iterator it;
    boost::smatch what;
    for (it = handlers_.cbegin(); it != handlers_.cend(); ++it) {
        if (boost::regex_match(uri, what, it->first)) {
            if (method == HTTP_METHOD::GET && it->second->http_get_handler_) {
                handler = it->second;
                return 0;
            } else if (method == HTTP_METHOD::POST && it->second->http_post_handler_) {
                handler = it->second;
                return 0;
            } else {
                tzhttpd_log_err("uri: %s matched, but no suitable handler for method: %s",
                                uri.c_str(), HTTP_METHOD_STRING(method).c_str());
                return -1;
            }
        }
    }

    if (method == HTTP_METHOD::GET) {
        tzhttpd_log_debug("[hostname:%s] http get default handler (filesystem) for %s ",
                          hostname_.c_str(), uri.c_str());
        handler = default_get_handler_;
        return 0;
    }

    return -2;
}

int HttpExecutor::http_redirect_handler(
                          const HttpParser& http_parser, const std::string& post_data,
                          std::string& response,
                          std::string& status_line, std::vector<std::string>& add_header) {

    std::shared_ptr<HttpExecutorConf> conf_ptr;
    {
        std::unique_lock<std::mutex> lock(conf_lock_);
        conf_ptr = conf_ptr_;
    }

    if (conf_ptr->redirect_code_ == "301") {
        status_line = generate_response_status_line(http_parser.get_version(),
                StatusCode::redirection_moved_permanently);
        response = http_proto::content_301;
        add_header.push_back("Location: " + conf_ptr->redirect_uri_);
    } else if(conf_ptr->redirect_code_ == "302"){
        status_line = generate_response_status_line(http_parser.get_version(),
                StatusCode::redirection_found);
        response = http_proto::content_302;
        add_header.push_back("Location: " + conf_ptr->redirect_uri_);
    } else {
        tzhttpd_log_err("unknown red_code: %s", conf_ptr->redirect_code_.c_str());
        status_line = generate_response_status_line(http_parser.get_version(),
                StatusCode::server_error_internal_server_error);
        response = http_proto::content_error;
    }

    return 0;
}


void HttpExecutor::handle_http_request(std::shared_ptr<HttpReqInstance> http_req_instance) override {

    HttpHandlerObjectPtr handler_object {};

    if (do_find_handler(http_req_instance->method_,  http_req_instance->uri_, handler_object) != 0) {
        tzhttpd_log_err("find handler for %s, %s failed.",
                        HTTP_METHOD_STRING(http_req_instance->method_).c_str(),
                        http_req_instance->uri_.c_str());
        http_req_instance->http_std_response(http_proto::StatusCode::client_error_not_found);
        return;
    }

    SAFE_ASSERT(handler_object);

    // AUTH CHECK
    if(!pass_basic_auth(http_req_instance->uri_,
                        http_req_instance->http_parser_->find_request_header(http_proto::header_options::auth))) {
         tzhttpd_log_err("basic_auth for %s failed ...", http_req_instance->uri_.c_str());
         http_req_instance->http_std_response(http_proto::StatusCode::client_error_unauthorized);
         return;
    }

    if (http_req_instance->method_ == HTTP_METHOD::GET)
    {
        HttpGetHandler handler = handler_object->http_get_handler_;
        if (!handler) {
            http_req_instance->http_std_response(http_proto::StatusCode::server_error_internal_server_error);
            return;
        }

        std::string response_str;
        std::string status_str;
        std::vector<std::string> headers;
        int code = handler(*http_req_instance->http_parser_, response_str, status_str, headers);
        if (code == 0) {
            handler_object->success_count_ ++;
        } else {
            handler_object->fail_count_ ++;
        }

        // status_line 为必须返回参数，如果没有就按照调用结果返回标准内容
        if (status_str.empty()) {
            if (code == 0)
                http_req_instance->http_std_response(http_proto::StatusCode::success_ok);
            else
                http_req_instance->http_std_response(http_proto::StatusCode::server_error_internal_server_error);
        } else {
            http_req_instance->http_response(response_str, status_str, headers);
        }
    }
    else if (http_req_instance->method_ == HTTP_METHOD::POST)
    {
        HttpPostHandler handler = handler_object->http_post_handler_;
        if (!handler) {
            http_req_instance->http_std_response(http_proto::StatusCode::server_error_internal_server_error);
            return;
        }

        std::string response_str;
        std::string status_str;
        std::vector<std::string> headers;
        int code = handler(*http_req_instance->http_parser_, http_req_instance->data_,
                           response_str, status_str, headers);
        if (code == 0) {
            handler_object->success_count_ ++;
        } else {
            handler_object->fail_count_ ++;
        }

        // status_line 为必须返回参数，如果没有就按照调用结果返回标准内容
        if (status_str.empty()) {
            if (code == 0)
                http_req_instance->http_std_response(http_proto::StatusCode::success_ok);
            else
                http_req_instance->http_std_response(http_proto::StatusCode::server_error_internal_server_error);
        } else {
            http_req_instance->http_response(response_str, status_str, headers);
        }
    }
    else
    {
        tzhttpd_log_err("what? %s", HTTP_METHOD_STRING(http_req_instance->method_).c_str());
        http_req_instance->http_std_response(http_proto::StatusCode::server_error_internal_server_error);
    }

}


int HttpExecutor::module_status(std::string& strModule, std::string& strKey, std::string& strValue) {

    std::shared_ptr<HttpExecutorConf> conf_ptr;
    {
        std::unique_lock<std::mutex> lock(conf_lock_);
        conf_ptr = conf_ptr_;
    }

    std::stringstream ss;

    ss << "\t" << "virtual_hostname: " << hostname_ << std::endl;

    if (!conf_ptr->redirect_str_.empty()) {
        ss << "\t" << "30x redirect: " << conf_ptr->redirect_str_ << std::endl;
        strValue = ss.str();
        return 0;
    }

    ss << "\t" << "document_root: " << conf_ptr->http_docu_root_ << std::endl;
    ss << "\t" << "document_index: ";
    for (auto iter = conf_ptr->http_docu_index_.begin(); iter != conf_ptr->http_docu_index_.end(); ++iter) {
        ss << *iter << ", ";
    }
    ss << std::endl;

    ss << "\t" << "register_handler: " << std::endl;
    for (auto iter = handlers_.begin(); iter != handlers_.end(); ++iter) {
        auto handlerObj = iter->second;
        ss << "\t\t" << "path: " << handlerObj->path_ ;
        ss         << ", method: " << ( handlerObj->http_get_handler_ ? "GET " : "");
        ss                       << ( handlerObj->http_post_handler_ ? "POST " : "");
        ss << std::endl;
    }

    ss << "\t" << std::endl;
    if (!conf_ptr->cache_controls_.empty()) {
        ss << "\t" << "cache_control: " << std::endl;
        for (auto iter = conf_ptr->cache_controls_.begin(); iter != conf_ptr->cache_controls_.end(); ++iter) {
            ss << "\t\t" << iter->first << " : "  << iter->second << std::endl;
        }
    }

    if (!conf_ptr->compress_controls_.empty()) {
        ss << "\t" << "compress_control: ";
        for (auto iter = conf_ptr->compress_controls_.begin(); iter != conf_ptr->compress_controls_.end(); ++iter) {
            ss << *iter << ", ";
        }
        ss << std::endl;
    }

    strValue = ss.str();
    return 0;
}


// 做一些可选的配置动态更新
// 1. thread_pool的动态更新
// 2. cgis
// 3. basic_auth
// 4. compress_control
// 5. cache_control

int HttpExecutor::handle_virtual_host_runtime_conf(const libconfig::Setting& setting) {

    std::shared_ptr<HttpExecutorConf> conf_ptr = std::make_shared<HttpExecutorConf>();
    if (!conf_ptr) {
        tzhttpd_log_err("create HttpExecutorConf instance failed.");
        return -1;
    }

    std::string redirect_str;
    std::string docu_root_str;
    std::string docu_index_str;

    ConfUtil::conf_value(setting, "redirect", redirect_str);
    ConfUtil::conf_value(setting, "docu_root", docu_root_str);
    ConfUtil::conf_value(setting, "docu_index", docu_index_str);

    ConfUtil::conf_value(setting, "exec_thread_pool_size", conf_ptr->executor_conf_.exec_thread_number_);
    ConfUtil::conf_value(setting, "exec_thread_pool_size_hard", conf_ptr->executor_conf_.exec_thread_number_hard_);
    ConfUtil::conf_value(setting, "exec_thread_pool_step_queue_size", conf_ptr->executor_conf_.exec_thread_step_queue_size_);

    // 检查ExecutorConf参数合法性
    if (conf_ptr->executor_conf_.exec_thread_number_hard_ < conf_ptr->executor_conf_.exec_thread_number_) {
        conf_ptr->executor_conf_.exec_thread_number_hard_ = conf_ptr->executor_conf_.exec_thread_number_;
    }

    if (conf_ptr->executor_conf_.exec_thread_number_ <= 0 ||
        conf_ptr->executor_conf_.exec_thread_number_ > 100 ||
        conf_ptr->executor_conf_.exec_thread_number_hard_ > 100 ||
        conf_ptr->executor_conf_.exec_thread_number_hard_ < conf_ptr->executor_conf_.exec_thread_number_ )
    {
        tzhttpd_log_err("invalid exec_thread_pool_size setting: %d, %d",
                        conf_ptr->executor_conf_.exec_thread_number_, conf_ptr->executor_conf_.exec_thread_number_hard_);
        return -1;
    }

    if (conf_ptr->executor_conf_.exec_thread_step_queue_size_ < 0) {
        tzhttpd_log_err("invalid exec_thread_step_queue_size setting: %d",
                        conf_ptr->executor_conf_.exec_thread_step_queue_size_);
        return -1;
    }


    if (!redirect_str.empty()) {

        if (!redirect_handler_) {
            tzhttpd_log_err("redirect_str not empty, but redirect_handler not set!");
            return -1;
        }

        auto pos = redirect_str.find('~');
        if (pos == std::string::npos) {
            tzhttpd_log_err("error redirect config: %s", redirect_str.c_str());
            return -1;
        }

        std::string code = boost::trim_copy(redirect_str.substr(0, pos));
        std::string uri  = boost::trim_copy(redirect_str.substr(pos+1));

        if (code != "301" && code != "302") {
            tzhttpd_log_err("error redirect config: %s", redirect_str.c_str());
            return -1;
        }

        conf_ptr->redirect_str_ = redirect_str;
        conf_ptr->redirect_code_ = code;
        conf_ptr->redirect_uri_ = uri;

        {
            // do swap here
            std::unique_lock<std::mutex> lock(conf_lock_);
            conf_ptr_.swap(conf_ptr);
        }

        // redirect 虚拟主机只需要这个配置就可以了
        return 0;

    } else if (!docu_root_str.empty() && !docu_index_str.empty()) {


        std::vector<std::string> docu_index{};
        {
            std::vector<std::string> vec {};
            boost::split(vec, docu_index_str, boost::is_any_of(";"));
            for (auto iter = vec.begin(); iter != vec.cend(); ++ iter){
                std::string tmp = boost::trim_copy(*iter);
                if (tmp.empty())
                    continue;

                docu_index.push_back(tmp);
            }
            if (docu_index.empty()) { // not fatal
                tzhttpd_log_err("empty valid docu_index found, previous: %s", docu_index_str.c_str());
            }
        }


        conf_ptr->http_docu_root_ = docu_root_str;
        conf_ptr->http_docu_index_ = docu_index;

        tzhttpd_log_debug("docu_root: %s, index items: %lu",
                          conf_ptr->http_docu_root_.c_str(),  conf_ptr->http_docu_index_.size());

        // fall throught following configure

    } else {

        tzhttpd_log_err("required at lease document setting or redirect setting for %s.", hostname_.c_str());
        return -1;
    }


    // Cgi配置处理，目前默认的处理方式是已经有的handler先不覆盖，所以
    // 可以动态增加handler，但是不能动态修改，这个后续优化之
    load_http_cgis(setting);


    if (setting.exists("cache_control")) {
        const libconfig::Setting &http_cache_control = setting["cache_control"];
        for(int i = 0; i < http_cache_control.getLength(); ++i) {
            const libconfig::Setting& ctrl_item = http_cache_control[i];
            std::string suffix {};
            std::string ctrl_head {};

            ConfUtil::conf_value(ctrl_item, "suffix", suffix);
            ConfUtil::conf_value(ctrl_item, "header", ctrl_head);
            if(suffix.empty() || ctrl_head.empty()) {
                tzhttpd_log_err("skip err cache ctrl configure item ...");
                continue;
            }

            // parse
            {
                std::vector<std::string> suffixes {};
                boost::split(suffixes, suffix, boost::is_any_of(";"));
                for (auto iter = suffixes.begin(); iter != suffixes.cend(); ++ iter){
                    std::string tmp = boost::trim_copy(*iter);
                    if (tmp.empty())
                        continue;

                    conf_ptr->cache_controls_[tmp] = ctrl_head;
                }
            }
        }

        // total display
        tzhttpd_log_debug("total %d cache ctrl for vhost %s",
                          static_cast<int>(conf_ptr->cache_controls_.size()), hostname_.c_str());
        for (auto iter = conf_ptr->cache_controls_.begin(); iter != conf_ptr->cache_controls_.end(); ++iter) {
            tzhttpd_log_debug("%s => %s", iter->first.c_str(), iter->second.c_str());
        }
    }

    // basic_auth
    if (setting.exists("basic_auth")) {
        conf_ptr->http_auth_.reset(new BasicAuth());
        if (!conf_ptr->http_auth_ || !conf_ptr->http_auth_->init(setting, true)) {
            tzhttpd_log_err("init basic_auth for vhost %s failed.", hostname_.c_str());
            return -1;
        }
    }

    if (setting.exists("compress_control")) {

        std::string suffix {};
        ConfUtil::conf_value(setting, "compress_control", suffix);

        std::vector<std::string> suffixes {};
        boost::split(suffixes, suffix, boost::is_any_of(";"));
        for (auto iter = suffixes.begin(); iter != suffixes.cend(); ++ iter){
            std::string tmp = boost::trim_copy(*iter);
            if (tmp.empty())
                continue;

            conf_ptr->compress_controls_.insert(tmp);
        }

        tzhttpd_log_debug("total %d compress ctrl for vhost %s",
                          static_cast<int>(conf_ptr->compress_controls_.size()), hostname_.c_str());
    }

    {
        // do swap here
        std::unique_lock<std::mutex> lock(conf_lock_);
        conf_ptr_.swap(conf_ptr);
    }

    return 0;

}

int HttpExecutor::module_runtime(const libconfig::Config& conf) {

    try
    {
        const libconfig::Setting &http_vhosts = conf.lookup("http.vhosts");

        for(int i = 0; i < http_vhosts.getLength(); ++i) {

            const libconfig::Setting& vhost = http_vhosts[i];

            std::string server_name;
            ConfUtil::conf_value(vhost, "server_name", server_name);

            // 发现是匹配的，则找到对应虚拟主机的配置文件了
            if (server_name == hostname_) {
                tzhttpd_log_notice("about to handle_virtual_host_runtime_conf update for host %s", hostname_.c_str());
                return handle_virtual_host_runtime_conf(vhost);
            }
        }
    } catch (const libconfig::SettingNotFoundException &nfex) {
        tzhttpd_log_err("http.vhosts not found!");
    } catch (std::exception& e) {
        tzhttpd_log_err("execptions catched for %s",  e.what());
    }

    tzhttpd_log_err("conf for host %s not found!!!!", hostname_.c_str());
    return -1;
}


} // end namespace tzhttpd

