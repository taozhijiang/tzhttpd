
#ifndef __TZHTTPD_HTTP_CONF_H__
#define __TZHTTPD_HTTP_CONF_H__

#include <set>
#include <mutex>

#include <boost/asio.hpp>
#include <boost/asio/steady_timer.hpp>

#include <scaffold/Setting.h>
#include <other/Log.h>

namespace tzhttpd {
    
    
extern void init_http_version_once(const std::string& server_version);

class HttpServer;
class HttpServerImpl;

class HttpConf {

    friend class HttpServer;
    friend class HttpServerImpl;
    
    bool        service_enabled_;            // 服务开关
    int32_t     service_speed_;

    int32_t     service_token_;

    int32_t     service_concurrency_;        // 最大连接并发控制

    int32_t     session_cancel_time_out_;    // session间隔会话时长
    int32_t     ops_cancel_time_out_;        // ops操作超时时长

    // 加载、更新配置的时候保护竞争状态
    // 这里保护主要是非atomic的原子结构
    std::mutex             lock_;
    std::set<std::string>  safe_ip_;

    std::string    bind_addr_;
    int32_t        bind_port_;

    int32_t        backlog_size_;
    int32_t        io_thread_number_;



    bool load_setting(std::shared_ptr<libconfig::Config> setting_ptr) {
        const auto& setting = *setting_ptr;
        return load_setting(setting);
    }

    bool load_setting(const libconfig::Config& setting) {

        setting.lookupValue("http.bind_addr", bind_addr_);
        setting.lookupValue("http.bind_port", bind_port_);
        if (bind_addr_.empty() || bind_port_ <= 0) {
            roo::log_err("invalid http.bind_addr %s & http.bind_port %d found!",
                    bind_addr_.c_str(), bind_port_);
            return false;
        }


        // IP访问白名单
        std::string ip_list;
        setting.lookupValue("http.safe_ip", ip_list);
        if (!ip_list.empty()) {
            std::vector<std::string> ip_vec;
            std::set<std::string> ip_set;
            boost::split(ip_vec, ip_list, boost::is_any_of(";"));
            for (auto it = ip_vec.begin(); it != ip_vec.cend(); ++it) {
                std::string tmp = boost::trim_copy(*it);
                if (tmp.empty())
                    continue;

                ip_set.insert(tmp);
            }

            std::swap(ip_set, safe_ip_);
        }
        if (!safe_ip_.empty()) {
            roo::log_warning("please notice safe_ip not empty, totally contain %d items",
                        static_cast<int>(safe_ip_.size()));
        }

        setting.lookupValue("http.backlog_size", backlog_size_);
        if (backlog_size_ < 0) {
            roo::log_err("invalid http.backlog_size %d found.", backlog_size_);
            return false;
        }

        setting.lookupValue("http.io_thread_pool_size", io_thread_number_);
        if (io_thread_number_ < 0) {
            roo::log_err("invalid http.io_thread_number %d", io_thread_number_);
            return false;
        }

        // once init，可以保证只被调用一次
        std::string server_version;
        setting.lookupValue("http.version", server_version);
        init_http_version_once(server_version);

        setting.lookupValue("http.ops_cancel_time_out", ops_cancel_time_out_);
        if (ops_cancel_time_out_ < 0) {
            roo::log_err("invalid http ops_cancel_time_out: %d", ops_cancel_time_out_);
            return false;
        }

        setting.lookupValue("http.session_cancel_time_out", session_cancel_time_out_);
        if (session_cancel_time_out_ < 0) {
            roo::log_err("invalid http session_cancel_time_out: %d", session_cancel_time_out_);
            return false;
        }

        setting.lookupValue("http.service_enable", service_enabled_);
        setting.lookupValue("http.service_speed", service_speed_);
        if (service_speed_ < 0) {
            roo::log_err("invalid http.service_speed: %d.", service_speed_);
            return false;
        }

        setting.lookupValue("http.service_concurrency", service_concurrency_);
        if (service_concurrency_ < 0) {
            roo::log_err("invalid http.service_concurrency: %d.", service_concurrency_);
            return false;
        }

        roo::log_info("HttpConf parse settings successfully!");
        return true;
    }



    // 如果通过检查，不在受限列表中，就返回true放行
    bool check_safe_ip(const std::string& ip) {
        std::lock_guard<std::mutex> lock(lock_);
        return (safe_ip_.empty() || (safe_ip_.find(ip) != safe_ip_.cend()));
    }

    // 限流模式使用
    bool get_http_service_token() {

        // 注意：
        // 如果关闭这个选项，则整个服务都不可用了(包括管理页面)
        // 此时如果需要变更除非重启整个服务，或者采用非web方式(比如通过发送命令)来恢复配置

        if (!service_enabled_) {
            roo::log_warning("http_service not enabled ...");
            return false;
        }

        // 下面就不使用锁来保证严格的一致性了，因为非关键参数，过多的锁会影响性能
        if (service_speed_ == 0) // 没有限流
            return true;

        if (service_token_ <= 0) {
            roo::log_warning("http_service not speed over ...");
            return false;
        }

        --service_token_;
        return true;
    }

    void withdraw_http_service_token() {    // 支持将令牌还回去
        ++service_token_;
    }

    // 采用定时器来喂狗
    void feed_http_service_token() {
        service_token_ = service_speed_;
    }

    std::shared_ptr<boost::asio::steady_timer> timed_feed_token_;
    void timed_feed_token_handler(const boost::system::error_code& ec) {

        if (service_speed_ == 0) {
            roo::log_warning("unlock speed jail, close the timer.");
            timed_feed_token_.reset();
            return;
        }

        // 恢复token
        feed_http_service_token();

        // 再次启动定时器
        timed_feed_token_->expires_from_now(boost::chrono::seconds(1)); // 1sec
        timed_feed_token_->async_wait(
            std::bind(&HttpConf::timed_feed_token_handler, this, std::placeholders::_1));
    }

public:

    // 构造函数和析构函数不能用friend改变访问性
    // 默认初始化，生成良好行为的数据
    HttpConf() :
        service_enabled_(true),
        service_speed_(0),
        service_token_(0),
        service_concurrency_(0),
        session_cancel_time_out_(0),
        ops_cancel_time_out_(0),
        lock_(),
        safe_ip_({ }),
        bind_addr_(),
        bind_port_(0),
        backlog_size_(0),
        io_thread_number_(0) {
    }

    ~HttpConf() = default;

} __attribute__((aligned(4)));  // end class HttpConf


} // end namespace tzhttpd

#endif //__TZHTTPD_HTTP_CONF_H__
