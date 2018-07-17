/*-
 * Copyright (c) 2018 TAO Zhijiang<taozhijiang@gmail.com>
 *
 * Licensed under the BSD-3-Clause license, see LICENSE for full information.
 *
 */

#include "HttpVhost.h"

namespace tzhttpd {

bool HttpVhost::init(const libconfig::Config& cfg) {

    try {
        const libconfig::Setting &http_vhosts = cfg.lookup("http.vhosts");

        for(int i = 0; i < http_vhosts.getLength(); ++i) {

            const libconfig::Setting& http_vhost = http_vhosts[i];
            std::shared_ptr<HttpHandler> handler {};
            if(!handle_vhost_cfg(http_vhost, handler) || !handler) {
                tzhttpd_log_err("parse http_vhost failed for idx: %d", i);
                return false;
            }

            std::string vhost_name = handler->get_vhost_name();
            if (vhost_name.empty()) {
                tzhttpd_log_err("vhost_name empty, param error...");
                return false;
            }

            if (vhost_name == "[default]") {
                if (default_vhost_) {
                    tzhttpd_log_err("[default] vhost already exist, configured multi times?");
                    return false;
                }

                default_vhost_ = handler;
                tzhttpd_log_alert("Success added vhost: %s ...", vhost_name.c_str());
                continue;
            }

            if (vhosts_.find(vhost_name) != vhosts_.end()) {
                tzhttpd_log_err("%s vhost already exist, configured multi times?", vhost_name.c_str());
                return false;
            }

            // store it
            vhosts_[vhost_name] = handler;
            tzhttpd_log_alert("Success added vhost: %s ...", vhost_name.c_str());

        }
    } catch (std::exception& e) {
        tzhttpd_log_err("Parse cfg key http.vhosts error with %s!!!", e.what());
        return false;

    } catch (...) {
        tzhttpd_log_err("Parse cfg key http.vhosts error!!!");
        return false;
    }

    if (!default_vhost_) {
        tzhttpd_log_err("[default] vhost not configured! ");
        return false;
    }

    tzhttpd_log_alert("Already configured(registered) vhost count: %d", static_cast<int>(vhosts_.size()));
    int i = 0;
    for (auto iter = vhosts_.cbegin(); iter != vhosts_.cend(); ++iter) {
        tzhttpd_log_alert("%d: %s", ++i, iter->first.c_str());
    }

    return true;
}

bool HttpVhost::handle_vhost_cfg(const libconfig::Setting& setting, std::shared_ptr<HttpHandler>& handler) {

    if (!setting.exists("server_name") || !setting.exists("docu_root") ||
        !setting.exists("docu_index")) {
        tzhttpd_log_err("required param not found for vhost.");
        return false;
    }

    std::string server_name = setting["server_name"];
    std::string docu_root = setting["docu_root"];
    std::string str_docu_index = setting["docu_index"];

    std::vector<std::string> docu_index {};
    {
        std::vector<std::string> vec {};
        boost::split(vec, str_docu_index, boost::is_any_of(";"));
        for (auto iter = vec.begin(); iter != vec.cend(); ++ iter){
            std::string tmp = boost::trim_copy(*iter);
            if (tmp.empty())
                continue;

            docu_index.push_back(tmp);
        }
        if (docu_index.empty()) {
            tzhttpd_log_err("empty valid docu_index found, previous: %s", str_docu_index.c_str());
        }
    }

    if (server_name.empty() || docu_root.empty() || docu_index.empty()) {
        tzhttpd_log_err("required param not found for vhost.");
        return false;
    }

    std::shared_ptr<HttpHandler> phandler = std::make_shared<HttpHandler>(server_name, docu_root, docu_index);
    if (!phandler || !phandler->init(setting)) {
        tzhttpd_log_err("create handler object for %s failed...", server_name.c_str());
        return false;
    }

    int ret_code = phandler->update_run_cfg(setting);
    if (ret_code != 0) {
        tzhttpd_log_err("update run cfg failed with %d ...", ret_code);
    }

    handler = phandler;
    return true;
}


int HttpVhost::update_run_cfg(const libconfig::Config& cfg) {

    return -1;
}


} // end namespace tzhttpd
