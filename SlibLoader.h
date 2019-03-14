/*-
 * Copyright (c) 2018-2019 TAO Zhijiang<taozhijiang@gmail.com>
 *
 * Licensed under the BSD-3-Clause license, see LICENSE for full information.
 *
 */

#ifndef __TZHTTPD_SLIB_LOADER_H__
#define __TZHTTPD_SLIB_LOADER_H__

#include <dlfcn.h>
#include <linux/limits.h>

#include "Log.h"
#include "CgiHelper.h"


namespace tzhttpd {

class SLibLoader {
public:
    SLibLoader(const std::string& dl_path):
        dl_path_(dl_path),
        dl_handle_(NULL) {
    }

    SLibLoader(const SLibLoader&) = delete;
    SLibLoader& operator=(const SLibLoader&) = delete;

    std::string get_dl_path() {
        return dl_path_;
    }

    bool init() {

        // RTLD_LAZY: Linux is not concerned about unresolved symbols until they are referenced.
        // RTLD_NOW: All unresolved symbols resolved when dlopen() is called.
        dl_handle_ = dlopen(dl_path_.c_str(), RTLD_LAZY);
        if (!dl_handle_) {
            tzhttpd_log_err("Load library %s failed: %s.", dl_path_.c_str(), dlerror());
            return false;
        }

        // Reset errors
        dlerror();
        char *err_info = NULL;

        module_init_ = (module_init_t)dlsym(dl_handle_, "module_init");
        if ((err_info = dlerror()) != NULL ) {
            tzhttpd_log_err("Load func module_init failed: %s", err_info);
            return false;
        }

        // 调用module_init函数
        int ret_code = (*module_init_)();
        if( ret_code != 0) {
            tzhttpd_log_err("call module_init failed: %d", ret_code);
            return false;
        }

        module_exit_ = (module_exit_t)dlsym(dl_handle_, "module_exit");
        if ((err_info = dlerror()) != NULL ) {
            tzhttpd_log_err("Load func module_exit failed: %s", err_info);
            return false;
        }

        tzhttpd_log_alert("module %s load ok!", dl_path_.c_str());
        return true;
    }

    // 函数指针类型
    template<typename FuncType>
    bool load_func(const std::string& func_name, FuncType* func) {

        if (!dl_handle_) {
            return false;
        }

        dlerror();
        char *err_info = NULL;

        FuncType func_t = (FuncType)dlsym(dl_handle_, func_name.c_str());
        if ((err_info = dlerror()) != NULL ) {
            tzhttpd_log_err("Load func %s failed: %s", func_name.c_str(), err_info);
            return false;
        }

        *func = func_t;
        tzhttpd_log_alert("load func %s from %s ok!", func_name.c_str(), dl_path_.c_str());
        return true;
    }

    void close() {
        if (dl_handle_) {

            if(module_exit_) {
                (*module_exit_)();
                module_exit_ = NULL;
                tzhttpd_log_alert("module_exit from %s called!", dl_path_.c_str());
            }

            dlclose(dl_handle_);
            dl_handle_ = NULL;
            tzhttpd_log_alert("dlclose from %s called!", dl_path_.c_str());
        }
    }

    ~SLibLoader() {
        close();
    }

private:

    // so 模块中需要实现的函数接口，用于模块启动和注销时候的操作
    module_init_t module_init_;
    module_exit_t module_exit_;

private:
    std::string  dl_path_;
    void*        dl_handle_;
};

} // end namespace tzhttpd

#endif // __TZHTTPD_SLIB_LOADER_H__
