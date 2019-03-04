#include <unistd.h>
#include <sstream>

#include <syslog.h>
#include <cstdio>
#include <libconfig.h++>

#include "CheckPoint.h"
#include "Log.h"
#include "HttpServer.h"

namespace tzhttpd {

using namespace http_proto;

int get_test_handler(const HttpParser& http_parser,
                     std::string& response, std::string& status_line, std::vector<std::string>& add_header) {
    response = "test uri called...";
    add_header.push_back("TestHead1: value1");
    add_header.push_back("TestHead2: value2");
    status_line = generate_response_status_line(http_parser.get_version(), StatusCode::success_ok);
    return 0;
}

namespace http_handler {
extern std::string http_server_version;
}

} // end namespace tzhttpd


extern char * program_invocation_short_name;
static void usage() {
    std::stringstream ss;

    ss << program_invocation_short_name << ":" << std::endl;
    ss << "\t -c cfgFile  specify config file, default httpsrv.conf. " << std::endl;
    ss << "\t -d          daemonize service." << std::endl;
    ss << "\t -v          print version info." << std::endl;
    ss << std::endl;

    std::cout << ss.str();
}

char cfgFile[PATH_MAX] = "httpsrv.conf";
bool daemonize = false;


static void interrupted_callback(int signal){
    tzhttpd::tzhttpd_log_alert("Signal %d received ...", signal);
    switch(signal) {
        case SIGHUP:
            tzhttpd::tzhttpd_log_notice("SIGHUP recv, do update_run_conf... ");
            tzhttpd::ConfHelper::instance().update_runtime_conf();
            break;

        case SIGUSR1:
            tzhttpd::tzhttpd_log_notice("SIGUSR recv, do module_status ... ");
            {
                std::string output;
                tzhttpd::Status::instance().collect_status(output);
                std::cout << output << std::endl;
                tzhttpd::tzhttpd_log_notice("%s", output.c_str());
            }
            break;

        default:
            tzhttpd::tzhttpd_log_err("Unhandled signal: %d", signal);
            break;
    }
}

static void init_signal_handle(){

    ::signal(SIGPIPE, SIG_IGN);
    ::signal(SIGUSR1, interrupted_callback);
    ::signal(SIGHUP,  interrupted_callback);

    return;
}

static int module_status(std::string& strModule, std::string& strKey, std::string& strValue) {

    strModule = "httpsrv";
    strKey = "main";

    strValue = "conf_file: " + std::string(cfgFile);

    return 0;
}

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
    tzhttpd::set_checkpoint_log_store_func(syslog);
    // setup in default DEBUG level, then reinialize when conf prased
    tzhttpd::tzhttpd_log_init(7);
    tzhttpd::tzhttpd_log_debug("first stage log init with default DEBUG finished.");

    // daemonize should before any thread creation...
    if (daemonize) {
        tzhttpd::tzhttpd_log_notice("we will daemonize this service...");

        bool chdir = false; // leave the current working directory in case
                            // the user has specified relative paths for
                            // the config file, etc
        bool close = true;  // close stdin, stdout, stderr
        if (::daemon(!chdir, !close) != 0) {
            tzhttpd::tzhttpd_log_err("call to daemon() failed: %s", strerror(errno));
            ::exit(EXIT_FAILURE);
        }
    }

    // 信号处理
    init_signal_handle();

    http_server_ptr.reset(new tzhttpd::HttpServer(cfgFile, "example_main"));
    if (!http_server_ptr ) {
        tzhttpd::tzhttpd_log_err("create HttpServer failed!");
        ::exit(EXIT_FAILURE);
    }

    // must called before http_server init
    http_server_ptr->add_http_vhost("example2.com");
    http_server_ptr->add_http_vhost("www.example2.com");
    http_server_ptr->add_http_vhost("www.example3.com");

    if(!http_server_ptr->init()){
        tzhttpd::tzhttpd_log_err("init HttpServer failed!");
        ::exit(EXIT_FAILURE);
    }

    http_server_ptr->add_http_get_handler("^/test$", tzhttpd::get_test_handler);
    http_server_ptr->register_http_status_callback("httpsrv", module_status);

    http_server_ptr->io_service_threads_.start_threads();
    http_server_ptr->service();

    http_server_ptr->io_service_threads_.join_threads();

    return 0;
}

namespace boost {

void assertion_failed(char const * expr, char const * function, char const * file, long line) {
    fprintf(stderr, "BAD!!! expr `%s` assert failed at %s(%ld): %s", expr, file, line, function);
    tzhttpd::tzhttpd_log_err("BAD!!! expr `%s` assert failed at %s(%ld): %s", expr, file, line, function);
}

} // end boost
