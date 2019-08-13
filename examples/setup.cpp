#include <signal.h>

#include <iostream>
#include <ctime>
#include <cstdio>


#include <syslog.h>
#include <boost/format.hpp>
#include <linux/limits.h>

#include <other/Log.h>

#include "Global.h"

// API for main

void init_signal_handle();
void usage();
int create_process_pid();


static void interrupted_callback(int signal) {
    roo::log_warning("Signal %d received ...", signal);
    switch (signal) {
        case SIGHUP:
            roo::log_warning("SIGHUP recv, do update_run_conf... ");
            tzhttpd::Global::instance().setting_ptr()->update_runtime_setting();
            break;

        case SIGUSR1:
            roo::log_warning("SIGUSR recv, do module_status ... ");
            {
                std::string output;
                tzhttpd::Global::instance().status_ptr()->collect_status(output);
                std::cout << output << std::endl;
                roo::log_warning("%s", output.c_str());
            }
            break;

        default:
            roo::log_err("Unhandled signal: %d", signal);
            break;
    }
}

void init_signal_handle() {

    ::signal(SIGPIPE, SIG_IGN);
    ::signal(SIGUSR1, interrupted_callback);
    ::signal(SIGHUP,  interrupted_callback);

    return;
}

extern char* program_invocation_short_name;
void usage() {
    std::stringstream ss;

    ss << program_invocation_short_name << ":" << std::endl;
    ss << "\t -c cfgFile  specify config file, default " << program_invocation_short_name << ".conf. " << std::endl;
    ss << "\t -d          daemonize service." << std::endl;
    ss << "\t -v          print version info." << std::endl;
    ss << std::endl;

    std::cout << ss.str();
}


// /var/run/[program_invocation_short_name].pid --> root permission
int create_process_pid() {

    char pid_msg[24];
    char pid_file[PATH_MAX];

    snprintf(pid_file, PATH_MAX, "./%s.pid", program_invocation_short_name);
    FILE* fp = fopen(pid_file, "w+");

    if (!fp) {
        roo::log_err("Create pid file %s failed!", pid_file);
        return -1;
    }

    pid_t pid = ::getpid();
    snprintf(pid_msg, sizeof(pid_msg), "%d\n", pid);
    fwrite(pid_msg, sizeof(char), strlen(pid_msg), fp);

    fclose(fp);
    return 0;
}
