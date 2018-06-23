/*-
 * Copyright (c) 2018 TAO Zhijiang<taozhijiang@gmail.com>
 *
 * Licensed under the BSD-3-Clause license, see LICENSE for full information.
 *
 */
 
#ifndef __TZHTTPD_LOCALHEAD_H__
#define __TZHTTPD_LOCALHEAD_H__

// General GNU
#ifdef __GNUC__
#define likely(x)       __builtin_expect(!!(x), 1)
#define unlikely(x)     __builtin_expect(!!(x), 0)
#else
#define likely(x)       (x)
#define unlikely(x)     (x)
#endif

#if __cplusplus > 201103L
// _built_in_
#else
#define override
#define nullptr (void*)0
#define noexcept
#endif

#include <boost/assert.hpp>

#undef NDEBUG  // needed by handler
#ifdef NP_DEBUG
// default, expand to ::assert
#else
// custom assert print but not abort, defined at Utils.cpp
#define BOOST_ENABLE_ASSERT_HANDLER
#endif
#define SAFE_ASSERT(expr) BOOST_ASSERT(expr)


#include <iostream>
using std::cout;
using std::cerr;
using std::endl;

#include <string>
using std::string;

#include <cstdint>
#include <linux/limits.h>  //PATH_MAX

#include <memory>

#include <thread>
#include <functional>  // bind

#include <boost/asio.hpp>
using namespace boost::asio;

typedef std::shared_ptr<ip::tcp::socket>    SocketPtr;
typedef std::weak_ptr<ip::tcp::socket>      SocketWeakPtr;

typedef boost::asio::posix::stream_descriptor asio_fd;
typedef std::shared_ptr<boost::asio::posix::stream_descriptor> asio_fd_shared_ptr;

#ifndef _TZHTTPD_DEFINE_GET_POINTER_MARKER_
#define _TZHTTPD_DEFINE_GET_POINTER_MARKER_
template<class T>
T * get_pointer(std::shared_ptr<T> const& p) {
    return p.get();
}
#endif // _TZHTTPD_DEFINE_GET_POINTER_MARKER_

#endif // _TZHTTPD_LOCALHEAD_H_
