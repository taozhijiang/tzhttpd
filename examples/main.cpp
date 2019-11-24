/*-
 * Copyright (c) 2019 TAO Zhijiang<taozhijiang@gmail.com>
 *
 * Licensed under the BSD-3-Clause license, see LICENSE for full information.
 *
 */

#include <unistd.h>
#include <sstream>

#include <cstdio>
#include <libconfig.h++>

#include "HttpParser.h"
#include "HttpServer.h"

void init_signal_handle();
void usage();
int create_process_pid();

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


extern char* program_invocation_short_name;

char cfgFile[PATH_MAX] = "httpsrv.conf";
bool daemonize = false;


static int module_status(std::string& module, std::string& name, std::string& val) {

    module = "httpsrv";
    name = "main";

    val = "conf_file: " + std::string(cfgFile);

    return 0;
}

int main(int argc, char* argv[]) {

    int opt_g = 0;
    while ((opt_g = getopt(argc, argv, "c:dhv")) != -1) {
        switch (opt_g) {
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


    auto setting = std::make_shared<roo::Setting>();
    if (!setting || !setting->init(cfgFile)) {
        fprintf(stderr, "Create and init roo::Setting with cfg %s failed.", cfgFile);
        return false;
    }

    auto setting_ptr = setting->get_setting();
    if (!setting_ptr) {
        fprintf(stderr, "roo::Setting return null pointer, maybe your conf file ill???");
        return false;
    }

    int log_level = 0;
    setting_ptr->lookupValue("log_level", log_level);
    if (log_level <= 0 || log_level > 7) {
        fprintf(stderr, "Invalid log_level %d, reset to default 7(DEBUG).", log_level);
        log_level = 7;
    }

    std::string log_path;
    setting_ptr->lookupValue("log_path", log_path);
    if (log_path.empty())
        log_path = "./log";

    roo::log_init(log_level, "", log_path, LOG_LOCAL6);
    roo::log_warning("Initialized roo::Log with level %d, path %s.", log_level, log_path.c_str());


    std::shared_ptr<tzhttpd::HttpServer> http_server_ptr;

    // daemonize should before any thread creation...
    if (daemonize) {
        roo::log_warning("we will daemonize this service...");

        bool chdir = false; // leave the current working directory in case
                            // the user has specified relative paths for
        // the config file, etc
        bool close = true;  // close stdin, stdout, stderr
        if (::daemon(!chdir, !close) != 0) {
            roo::log_err("call to daemon() failed: %s", strerror(errno));
            ::exit(EXIT_FAILURE);
        }
    }

    // 信号处理
    init_signal_handle();

    http_server_ptr.reset(new tzhttpd::HttpServer(cfgFile, "example_main"));
    if (!http_server_ptr) {
        roo::log_err("create HttpServer failed!");
        ::exit(EXIT_FAILURE);
    }

    // must called before http_server init
    http_server_ptr->add_http_vhost("example2.com");
    http_server_ptr->add_http_vhost("www.example2.com");
    http_server_ptr->add_http_vhost("www.example3.com");

    if (!http_server_ptr->init()) {
        roo::log_err("init HttpServer failed!");
        ::exit(EXIT_FAILURE);
    }

    http_server_ptr->add_http_get_handler("^/test$", tzhttpd::get_test_handler);
    http_server_ptr->register_http_status_callback("httpsrv", module_status);

    http_server_ptr->service_start();

    // wait to terminate
    http_server_ptr->service_join();

    return 0;
}

namespace boost {

void assertion_failed(char const* expr, char const* function, char const* file, long line) {
    fprintf(stderr, "BAD!!! expr `%s` assert failed at %s(%ld): %s", expr, file, line, function);
    roo::log_err("BAD!!! expr `%s` assert failed at %s(%ld): %s", expr, file, line, function);
}

} // end boost
