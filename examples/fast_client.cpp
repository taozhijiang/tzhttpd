/*-
 * Copyright (c) 2019 TAO Zhijiang<taozhijiang@gmail.com>
 *
 * Licensed under the BSD-3-Clause license, see LICENSE for full information.
 *
 */
#include <unistd.h>

#include <string>
#include <sstream>
#include <iostream>
#include <syslog.h>

#include "HttpUtil.h"

volatile bool start = false;
volatile bool stop  = false;

time_t            start_time = 0;
volatile uint64_t count = 0;

extern char* program_invocation_short_name;
static void usage() {
    std::stringstream ss;

    ss << program_invocation_short_name << " [thread_num] url " << std::endl;
    ss << std::endl;

    std::cerr << ss.str();
}

int random_int() {
    return (random() % 320) + 23;
}

std::string get_url;

std::string post_url;
std::string post_dat;

void* perf_run(void* x_void_ptr) {

    // 客户端长连接

    auto client = std::make_shared<HttpUtil::HttpClient>();
    int  code = 0;

    while (!start)
        ::usleep(1);

    while (!stop) {

        if (!get_url.empty()) {
            code = client->GetByHttp(get_url);
        } else if (!post_url.empty() && !post_dat.empty()) {
            code = client->PostByHttp(post_url, post_dat);
        } else {
            std::cerr << "err.... " << std::endl;
            break;
        }

        if (code != 0) {
            std::cerr << "err for code " << code << std::endl;
            break;
        }

        // increment success case
        count++;
    }

    return NULL;
}

int main(int argc, char* argv[]) {


    int thread_num = 0;
    if (argc < 3 || (thread_num = ::atoi(argv[1])) <= 0) {
        usage();
        return 0;
    }

    if (argc == 3) {
        get_url = std::string(argv[2]);
        std::cerr << "we will get for " << get_url << std::endl;
    } else if (argc >= 4) {
        post_url = std::string(argv[2]);
        post_dat = std::string(argv[3]);
        std::cerr << "we will post for " << post_url << ", dat " << post_dat << std::endl;
    }


    std::vector<pthread_t> tids(thread_num,  0);
    for (size_t i = 0; i < tids.size(); ++i) {
        pthread_create(&tids[i], NULL, perf_run, NULL);
        std::cerr << "starting thread with id: " << tids[i] << std::endl;
    }

    ::sleep(3);
    std::cerr << "begin to test, press any to stop." << std::endl;
    start_time = ::time(NULL);
    start = true;

    int ch = getchar();
    (void)ch;
    stop = true;
    time_t stop_time = ::time(NULL);

    uint64_t count_per_sec = count / (stop_time - start_time);
    fprintf(stderr, "total count %ld, time: %ld, perf: %ld tps\n", count, stop_time - start_time, count_per_sec);

    for (size_t i = 0; i < tids.size(); ++i) {
        pthread_join(tids[i], NULL);
        std::cerr << "joining " << tids[i] << std::endl;
    }

    std::cerr << "done" << std::endl;

    return 0;
}

