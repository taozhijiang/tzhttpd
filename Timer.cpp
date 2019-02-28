/*-
 * Copyright (c) 2018-2019 TAO Zhijiang<taozhijiang@gmail.com>
 *
 * Licensed under the BSD-3-Clause license, see LICENSE for full information.
 *
 */

#include "Timer.h"

namespace tzhttpd {

Timer& Timer::instance() {
    static Timer handler;
    return handler;
}


} // end namespace tzhttpd

