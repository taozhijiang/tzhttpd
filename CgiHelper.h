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
    msg->data = (char *)calloc(len + 1, 1);
    if (!msg->data) {
        return -1;
    }

    memcpy(msg->data, data, len);
    return 0;
}

// caller alloc req,  caller free req
// callee alloc resp, caller free resp
typedef int (*cgi_handler_t)(const msg_t* req, msg_t* resp);

typedef int (*module_init_t)();
typedef int (*module_exit_t)();

#ifdef __cplusplus
} // end extern "C"
#endif

#endif // __TZHTTPD_SO_HELPER_H__
