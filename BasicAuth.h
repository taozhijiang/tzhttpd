/*-
 * Copyright (c) 2018-2019 TAO Zhijiang<taozhijiang@gmail.com>
 *
 * Licensed under the BSD-3-Clause license, see LICENSE for full information.
 *
 */

#ifndef __TZHTTPD_BASIC_AUTH_H__
#define __TZHTTPD_BASIC_AUTH_H__

#include <xtra_rhel6.h>

#include <set>
#include <boost/algorithm/string.hpp>
#include <boost/thread/locks.hpp>

#include "CryptoUtil.h"
#include "StrUtil.h"
#include "Log.h"

namespace tzhttpd {

// 每个Virtual Host持有一个这个认证结构，主要用户Http BasicAuth鉴权

typedef std::vector<std::pair<UriRegex, std::set<std::string>>> BasicAuthContain;

class BasicAuth {

public:
    BasicAuth():
        basic_auths_(new BasicAuthContain()) {
    }

    // strict == true，如果遇到错误的配置将会报错终止解析
    bool init(const libconfig::Setting& setting, bool strict = false){

        if (!setting.exists("basic_auth")) {
            tzhttpd_log_err("conf does not contains basic_auth part.");
            return true;
        }

        std::shared_ptr<BasicAuthContain> basic_auths_load(new BasicAuthContain());

        const libconfig::Setting& basic_auth = setting["basic_auth"];
        for(int i = 0; i < basic_auth.getLength(); ++i) {
            const libconfig::Setting& basic_auths_item = basic_auth[i];
            if (!basic_auths_item.exists("uri") || !basic_auths_item.exists("auth")) {
                tzhttpd_log_err("required uri and auth not found.");
                continue;
            }
            std::string auth_uri_regex;
            basic_auths_item.lookupValue("uri", auth_uri_regex);
            auth_uri_regex = StrUtil::pure_uri_path(auth_uri_regex);

            std::set<std::string> auth_set{};
            const libconfig::Setting& auth = basic_auths_item["auth"];
            for (int j = 0; j < auth.getLength(); ++j) {

                const libconfig::Setting& auth_acct = auth[j];
                std::string auth_user;
                std::string auth_passwd;

                auth_acct.lookupValue("user", auth_user);
                auth_acct.lookupValue("passwd", auth_passwd);

                if (auth_user.empty() || auth_passwd.empty()) {
                    if (strict) {
                        tzhttpd_log_err("basic_auth err account item %s, strict error return.", auth_user.c_str());
                        return false;
                    } else {
                        tzhttpd_log_err("basic_auth skip err account item %s, skip this.", auth_user.c_str());
                        continue;
                    }
                }

                std::string auth_str = auth_user + ":" + auth_passwd;
                std::string auth_base = CryptoUtil::base64_encode(auth_str);

                auth_set.insert(auth_base);
                tzhttpd_log_debug("basic_auth detected valid item for user %s ", auth_user.c_str());
            }

            if (auth_set.empty()) {
                tzhttpd_log_notice("empty ruleset for %s, we will allow all access for this uri.",
                                    auth_uri_regex.c_str());
            }

            UriRegex rgx {auth_uri_regex};
            basic_auths_load->push_back({rgx, auth_set});
            tzhttpd_log_debug("success add %d auth items for uri %s.",
                              static_cast<int>(auth_set.size()), auth_uri_regex.c_str());
        }

        tzhttpd_log_debug("total valid auth rules count: %d detected.",
                          static_cast<int>(basic_auths_load->size()));

        {
            std::lock_guard<std::mutex> lock(lock_);
            basic_auths_.swap(basic_auths_load);
        }

        return true;
    }


public:
    bool check_auth(const std::string& uri, const std::string& auth_str){

        std::string auth_code {};

        // 获取Http Header Auth字段
        {
            std::vector<std::string> vec{};
            boost::split(vec, auth_str, boost::is_any_of(" \t\n"));
            if (vec.size() == 2 && strcasestr(vec[0].c_str(), "Basic")) {
                auth_code = boost::algorithm::trim_copy(vec[1]);
            }
        }

        std::string pure_uri = StrUtil::pure_uri_path(uri);

        std::shared_ptr<BasicAuthContain> auth_rule {};
        {
            std::lock_guard<std::mutex> lock(lock_);
            auth_rule = basic_auths_;
        }

        // 在配置文件中按照优先级的顺序向下检索，如果发现请求URI匹配了正则表达式
        // 如果检索到了账号，表示授权成功，返回true
        // 否则拒绝本次请求，不再尝试后续表达式匹配
        std::vector<std::pair<UriRegex, std::set<std::string>>>::const_iterator it;
        boost::smatch what;
        for (it = auth_rule->cbegin(); it != auth_rule->cend(); ++it) {
            if (boost::regex_match(pure_uri, what, it->first)) {

                // empty auth, we will allow all access
                if (it->second.empty()) {
                    return true;
                }

                // normal rule check
                if (it->second.find(auth_code) == it->second.end()) {
                    tzhttpd_log_err("reject access to %s with auth_str: %s", uri.c_str(), auth_str.c_str());
                    return false;
                }
                else
                {
                    return true;
                }
            }
        }

        return true;
    }

private:
    std::mutex lock_;
    std::shared_ptr<BasicAuthContain> basic_auths_;
};


} // end namespace tzhttpd


#endif //__TZHTTPD_BASIC_AUTH_H__
