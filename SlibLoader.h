#ifndef __TZHTTPD_SLIB_LOADER_H__
#define __TZHTTPD_SLIB_LOADER_H__

#include <dlfcn.h>
#include <linux/limits.h>

#include <boost/noncopyable.hpp>

#include "Log.h"
#include "CgiHelper.h"


namespace tzhttpd {

class SLibLoader: private boost::noncopyable {
public:
    SLibLoader(const std::string& dl_path):
        dl_path_(dl_path),
        dl_handle_(NULL) {
    }

    std::string get_dl_path() {
        return dl_path_;
    }

    bool init() {

        // RTLD_LAZY: Linux is not concerned about unresolved symbols until they are referenced.
        // RTLD_NOW: All unresolved symbols resolved when dlopen() is called.
        dl_handle_ = dlopen(dl_path_.c_str(), RTLD_LAZY);
        if (!dl_handle_) {
            log_err("Load library %s failed: %s.", dl_path_.c_str(), dlerror());
            return false;
        }

        // Reset errors
        dlerror();
        char *err_info = NULL;

        module_init_ = (module_init_t)dlsym(dl_handle_, "module_init");
        if ((err_info = dlerror()) != NULL ) {
            log_err("Load func module_init failed: %s", err_info);
            return false;
        }

        module_exit_ = (module_exit_t)dlsym(dl_handle_, "module_exit");
        if ((err_info = dlerror()) != NULL ) {
            log_err("Load func module_exit failed: %s", err_info);
            return false;
        }

        log_alert("module %s load ok!", dl_path_.c_str());
        return true;
    }

    // 函数指针类型
    template<typename funcT>
    bool load_func(const std::string& func_name, funcT* func) {

        if (!dl_handle_) {
            return false;
        }

        dlerror();
        char *err_info = NULL;

        funcT func_t = (funcT)dlsym(dl_handle_, func_name.c_str());
        if ((err_info = dlerror()) != NULL ) {
            log_err("Load func %s failed: %s", func_name.c_str(), err_info);
            return false;
        }

        *func = func_t;
        log_alert("load func %s from %s ok!", func_name.c_str(), dl_path_.c_str());
        return true;
    }

    void close() {
        if (dl_handle_) {
            dlclose(dl_handle_);
            dl_handle_ = NULL;
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
