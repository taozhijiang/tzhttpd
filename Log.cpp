/*-
 * Copyright (c) 2018 TAO Zhijiang<taozhijiang@gmail.com>
 *
 * Licensed under the BSD-3-Clause license, see LICENSE for full information.
 *
 */
 
#include <vector>
#include <boost/algorithm/string.hpp>

#include "Log.h"

namespace tzhttpd {

#ifdef TZHTTPD_USING_SYSLOG

Log& Log::instance() {
    static Log helper;
    return helper;
}

// We believe outer project some where already initialized this.
// Because it is just library.
bool Log::init(int log_level) {
//    openlog(program_invocation_short_name, LOG_PID , LOG_LOCAL6);
//    setlogmask (LOG_UPTO (log_level));
    return true;
}

void Log::log_api(int priority, const char *file, int line, const char *func, const char *msg, ...) {

    char buf[MAX_LOG_BUF_SIZE + 2] = {0, };
    int n = snprintf(buf, MAX_LOG_BUF_SIZE, "[%s:%d][%s][%#lx] -- ", file, line, func, (long)pthread_self());

    va_list arg_ptr;
    va_start(arg_ptr, msg);
    vsnprintf(buf + n, MAX_LOG_BUF_SIZE - n, msg, arg_ptr);
    va_end(arg_ptr);

    // 如果消息中夹杂着换行，在rsyslog中处理(尤其是转发的时候)会比较麻烦
    // 如果原本发送，则接收端是#开头的编码字符，如果转移，根据协议换行意味着消息的结束，消息会丢失
    // 这种情况下，将消息拆分然后分别发送

    n = static_cast<int>(strlen(buf));
    if (likely(std::find(buf, buf + n, '\n') == (buf + n))) {
        buf[n] = '\n';   // 兼容老的pbi_tzhttpd_log_service
        ::syslog(priority, "%s", buf);
        return;
    }

    // 拆分消息
    std::vector<std::string> messages;
    boost::split(messages, buf, boost::is_any_of("\r\n"));
    for (std::vector<string>::iterator it = messages.begin(); it != messages.end(); ++it){
        if (!it->empty()) {
            std::string message = (*it) + "\n";
            ::syslog(priority, "%s", message.c_str());
        }
    }
}

#else // TZHTTPD_USING_SYSLOG

void log_api(const char* priority, const char *file, int line, const char *func, const char *msg, ...) {

    char buf[MAX_LOG_BUF_SIZE + 2] = {0, };
    int n = snprintf(buf, MAX_LOG_BUF_SIZE, "[%s:%d][%s][%#lx]<%s> -- ", file, line, func, (long)pthread_self(), priority);

    va_list arg_ptr;
    va_start(arg_ptr, msg);
    vsnprintf(buf + n, MAX_LOG_BUF_SIZE - n, msg, arg_ptr);
    va_end(arg_ptr);

    n = static_cast<int>(strlen(buf));
    if (std::find(buf, buf + n, '\n') == (buf + n)) {
        buf[n] = '\n';   // 兼容老的pbi_tzhttpd_log_service
        fprintf(stderr, "%s", buf);
        return;
    }
}

#endif // TZHTTPD_USING_SYSLOG


} // end namespace tzhttpd
