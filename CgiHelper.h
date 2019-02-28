/*-
 * Copyright (c) 2018-2019 TAO Zhijiang<taozhijiang@gmail.com>
 *
 * Licensed under the BSD-3-Clause license, see LICENSE for full information.
 *
 */

#ifndef __TZHTTPD_CGI_HELPER_H__
#define __TZHTTPD_CGI_HELPER_H__

// pure c interface with so

#include <cstdlib>
#include <cstring>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    char*  data;
    size_t len;
} msg_t;

static inline
int free_msg(msg_t* msg) {

    free(msg->data);
    msg->data = NULL;
    msg->len  = 0;

    return 0;
}

static inline
int fill_msg(msg_t* msg, const char* data, size_t len) {

    free_msg(msg);
    msg->data = (char *)malloc(len);
    if (!msg->data) {
        return -1;
    }

    memcpy(msg->data, data, len);
    msg->len = len;
    return 0;
}

// caller alloc req,  caller free req
// callee alloc resp, caller free resp
typedef int (*cgi_get_handler_t)(const msg_t* params, msg_t* resp, msg_t* resp_header);
typedef int (*cgi_post_handler_t)(const msg_t* params, const msg_t* postdata, msg_t* resp, msg_t* resp_header);

typedef int (*module_init_t)();
typedef int (*module_exit_t)();

#ifdef __cplusplus
} // end extern "C"
#endif

#endif // __TZHTTPD_CGI_HELPER_H__
