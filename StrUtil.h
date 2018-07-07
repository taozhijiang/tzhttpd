/*-
 * Copyright (c) 2018 TAO Zhijiang<taozhijiang@gmail.com>
 *
 * Licensed under the BSD-3-Clause license, see LICENSE for full information.
 *
 */

#ifndef __TZHTTPD_STR_UTIL_H__
#define __TZHTTPD_STR_UTIL_H__

#include <string>

#include <boost/lexical_cast.hpp>

#include "LocalHead.h"

// 类静态函数可以直接将函数定义丢在头文件中

namespace tzhttpd {

struct StrUtil {

    static size_t trim_whitespace(std::string& str) {

        size_t index = 0;
        size_t orig = str.size();

        // trim left whitespace
        for (index = 0; index < str.size() && isspace(str[index]); ++index)
            /* do nothing*/;
        str.erase(0, index);

        // trim right whitespace
        for (index = str.size(); index > 0 && isspace(str[index - 1]); --index)
            /* do nothing*/;
        str.erase(index);

        return orig - str.size();
    }


    template <typename T>
    static std::string convert_to_string(const T& arg) {
        try {
            return boost::lexical_cast<std::string>(arg);
        }
        catch(boost::bad_lexical_cast& e) {
            return "";
        }
    }


};


} // end namespace tzhttpd

#endif // __TZHTTPD_STR_UTIL_H__
