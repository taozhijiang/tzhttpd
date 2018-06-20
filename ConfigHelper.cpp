#include "ConfigHelper.h"
#include "Log.h"

namespace tzhttpd {

ConfigHelper& ConfigHelper::instance() {
    static ConfigHelper helper;
    return helper;
}

int ConfigHelper::update_cfg() {

    if (in_process_) {
        log_err("!!! already in apply configure process, try it later!");
        return 0;
    }

    std::string cfgfile = "../tzhttpd.conf";
    libconfig::Config cfg;

    try {
        cfg.readFile(cfgfile.c_str());
    } catch(libconfig::FileIOException &fioex) {
        log_err("I/O error while reading file: %s.", cfgfile.c_str());
        return -1;
    } catch(libconfig::ParseException &pex) {
        log_err("Parse error at %d - %s", pex.getLine(), pex.getError());
        return -2;
    }

    std::lock_guard<std::mutex> lock(lock_);
    int ret = 0;
    for (std::vector<ConfigCallable>::const_iterator it = calls_.begin(); it != calls_.end(); ++it) {
            ret += (*it)(cfg); // call it!
    }

    log_alert("ConfigHelper::update_cfg totally callback return: %d", ret);
    in_process_ = false;

    return ret;
}

int ConfigHelper::register_cfg_callback(ConfigCallable func) {

    if (!func){
        return -1;
    }

    std::lock_guard<std::mutex> lock(lock_);
    calls_.push_back(func);
    return 0;
}


} // end namespace tzhttpd
