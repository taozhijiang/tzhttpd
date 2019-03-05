#include <unistd.h>
#include <sstream>

#include <syslog.h>
#include <cstdio>
#include <libconfig.h++>

#include "HttpServer.h"
#include "CheckPoint.h"
#include "Log.h"
#include "tzmonitor/MonitorClient.h"

void init_signal_handle();
void usage();
int create_process_pid();

namespace tzhttpd {

using namespace http_proto;

namespace http_handler {
extern std::string http_server_version;
}
} // end namespace tzhttpd

std::shared_ptr<tzmonitor_client::MonitorClient> monitor_ptr_ {};


int tzmonitor_report(const std::string& name, int32_t value, const std::string& tag) {
    if (monitor_ptr_) {
        monitor_ptr_->report_event(name, value, tag);
        return 0;
    }

    return -1;
}

extern char * program_invocation_short_name;

char cfgFile[PATH_MAX] = "httpsrv.conf";
bool daemonize = false;

int main(int argc, char* argv[]) {

    int opt_g = 0;
    while( (opt_g = getopt(argc, argv, "c:dhv")) != -1 ) {
        switch(opt_g)
        {
            case 'c':
                memset(cfgFile, 0, sizeof(cfgFile));
                strncpy(cfgFile, optarg, PATH_MAX);
                break;
            case 'd':
                daemonize = true;
                break;
            case 'v':
                std::cout << program_invocation_short_name << ": "
                    << tzhttpd::http_handler::http_server_version << std::endl;
                break;
            case 'h':
            default:
                usage();
                ::exit(EXIT_SUCCESS);
        }
    }


    libconfig::Config cfg;
    std::shared_ptr<tzhttpd::HttpServer> http_server_ptr;

    // default syslog
    tzhttpd::set_checkpoint_log_store_func(::syslog);
    tzhttpd::tzhttpd_log_init(7);
    tzhttpd::tzhttpd_log_debug("first stage log init with default DEBUG finished.");

    // daemonize should before any thread creation...
    if (daemonize) {
        std::cout << "We will daemonize this service..." << std::endl;
        tzhttpd::tzhttpd_log_notice("We will daemonize this service...");

        bool chdir = false; // leave the current working directory in case
                            // the user has specified relative paths for
                            // the config file, etc
        bool close = true;  // close stdin, stdout, stderr
        if (::daemon(!chdir, !close) != 0) {
            tzhttpd::tzhttpd_log_err("Call to daemon() failed: %s", strerror(errno));
            std::cout << "Call to daemon() failed: " << strerror(errno) << std::endl;
            ::exit(EXIT_FAILURE);
        }
    }

        // 信号处理
    init_signal_handle();

    http_server_ptr.reset(new tzhttpd::HttpServer(cfgFile, "tzhttpd_with_report"));
    if (!http_server_ptr ) {
        tzhttpd::tzhttpd_log_err("create HttpServer failed!");
        ::exit(EXIT_FAILURE);
    }

    if(!http_server_ptr->init()){
        tzhttpd::tzhttpd_log_err("init HttpServer failed!");
        ::exit(EXIT_FAILURE);
    }


    monitor_ptr_ = std::make_shared<tzmonitor_client::MonitorClient>();
    if(!monitor_ptr_ || !monitor_ptr_->init(cfgFile, ::syslog)) {
        fprintf(stderr, "init tzmonitor client failed!");
        return -1;
    }

    if (monitor_ptr_->ping()) {
        tzhttpd::tzhttpd_log_err("client call ping failed.");
        return false;
    }

    http_server_ptr->register_http_runtime_callback(
            "tzhttpd_with_report",
            std::bind(&tzmonitor_client::MonitorClient::module_runtime, monitor_ptr_,
                      std::placeholders::_1));

    http_server_ptr->register_http_status_callback(
            "tzhttpd_with_report",
            std::bind(&tzmonitor_client::MonitorClient::module_status, monitor_ptr_,
                      std::placeholders::_1, std::placeholders::_2,
                      std::placeholders::_3));

    // 内嵌的上报CP
    tzhttpd::checkpoint_report_event_func_impl_ = tzmonitor_report;


    http_server_ptr->io_service_threads_.start_threads();
    http_server_ptr->service();

    http_server_ptr->io_service_threads_.join_threads();

    return 0;
}

namespace boost {

void assertion_failed(char const * expr, char const * function, char const * file, long line) {
    fprintf(stderr, "BAD!!! expr `%s` assert failed at %s(%ld): %s", expr, file, line, function);
}

} // end boost
