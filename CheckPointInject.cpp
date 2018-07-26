/*-
 * Copyright (c) 2018 TAO Zhijiang<taozhijiang@gmail.com>
 *
 * Licensed under the BSD-3-Clause license, see LICENSE for full information.
 *
 */

// 空函数，业务根据需求可以增加实现代码

#include "CheckPoint.h"
#include "Log.h"

namespace tzhttpd {

// Event Report
CP_report_event_func_t checkpoint_report_event_func_impl_ = NULL;
void set_checkpoint_report_event_func(CP_report_event_func_t func) {
    checkpoint_report_event_func_impl_ = func;
}


// Log Store
CP_log_store_func_t checkpoint_log_store_func_impl_ = NULL;
void set_checkpoint_log_store_func(CP_log_store_func_t func) {
    checkpoint_log_store_func_impl_ = func;
}

} // end namespace tzhttpd
