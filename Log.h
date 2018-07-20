/*-
 * Copyright (c) 2018 TAO Zhijiang<taozhijiang@gmail.com>
 *
 * Licensed under the BSD-3-Clause license, see LICENSE for full information.
 *
 */

#ifndef __TZHTTPD_LOG_H__
#define __TZHTTPD_LOG_H__

#include <pthread.h>
#include <stdio.h>
#include <stdarg.h>
#include <cstring>
#include <cstddef>

#include <algorithm>

#include <boost/current_function.hpp>

// LOG_EMERG   0   system is unusable
// LOG_ALERT   1   action must be taken immediately
// LOG_CRIT    2   critical conditions
// LOG_ERR     3   error conditions
// LOG_WARNING 4   warning conditions
// LOG_NOTICE  5   normal, but significant, condition
// LOG_INFO    6   informational message
// LOG_DEBUG   7   debug-level message

namespace tzhttpd {



// man 3 syslog
#include <syslog.h>

bool tzhttpd_log_init(int log_level);
void tzhttpd_log_close();
void log_api(int priority, const char *file, int line, const char *func, const char *msg, ...)
    __attribute__((format(printf, 5, 6)));

#define tzhttpd_log_emerg(...)   log_api( LOG_EMERG, __FILE__, __LINE__, __FUNCTION__, __VA_ARGS__)
#define tzhttpd_log_alert(...)   log_api( LOG_ALERT, __FILE__, __LINE__, __FUNCTION__, __VA_ARGS__)
#define tzhttpd_log_crit(...)    log_api( LOG_CRIT, __FILE__, __LINE__, __FUNCTION__, __VA_ARGS__)
#define tzhttpd_log_err(...)     log_api( LOG_ERR, __FILE__, __LINE__, __FUNCTION__, __VA_ARGS__)
#define tzhttpd_log_warning(...) log_api( LOG_WARNING, __FILE__, __LINE__, __FUNCTION__, __VA_ARGS__)
#define tzhttpd_log_notice(...)  log_api( LOG_NOTICE, __FILE__, __LINE__, __FUNCTION__, __VA_ARGS__)
#define tzhttpd_log_info(...)    log_api( LOG_INFO, __FILE__, __LINE__, __FUNCTION__, __VA_ARGS__)
#define tzhttpd_log_debug(...)   log_api( LOG_DEBUG, __FILE__, __LINE__, __FUNCTION__, __VA_ARGS__)


} // end namespace tzhttpd


#endif // __TZHTTPD_LOG_H__
