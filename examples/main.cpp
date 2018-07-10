#include <cstdio>
#include <libconfig.h++>

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

}

int main(int argc, char* argv[]) {

    std::string cfgfile = "../tzhttpd.conf";
    libconfig::Config cfg;
    std::shared_ptr<tzhttpd::HttpServer> http_server_ptr;

    http_server_ptr.reset(new tzhttpd::HttpServer(cfgfile, "example_main"));
    if (!http_server_ptr || !http_server_ptr->init()) {
        fprintf(stderr, "Init HttpServer failed!");
        return false;
    }

    http_server_ptr->register_http_get_handler("^/test$", tzhttpd::get_test_handler, true);

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
