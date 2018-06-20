#ifndef __TZHTTPD_CONFIG_HELPER__
#define __TZHTTPD_CONFIG_HELPER__

#include <mutex>
#include <vector>
#include <functional>

#include <libconfig.h++>

#include <boost/noncopyable.hpp>


// 值拷贝

namespace tzhttpd {

typedef std::function<int (const libconfig::Config& cfg)> ConfigCallable;

class ConfigHelper: public boost::noncopyable {
public:
    static ConfigHelper& instance();

    bool init();

    int  update_cfg();
    int  register_cfg_callback(ConfigCallable func);

private:
    ConfigHelper(){
        in_process_ = false;
    }

    ~ConfigHelper(){}

private:
    bool in_process_;
    std::mutex lock_;
    std::vector<ConfigCallable> calls_;
};

} // end namespace tzhttpd

#endif // __TZHTTPD_CONFIG_HELPER__
