/*-
 * Copyright (c) 2018-2019 TAO Zhijiang<taozhijiang@gmail.com>
 *
 * Licensed under the BSD-3-Clause license, see LICENSE for full information.
 *
 */

#include <pthread.h>
#include <string>
#include <openssl/err.h>

#include "SslSetup.h"
#include "Log.h"

namespace tzhttpd {


/* This array will store all of the mutexes available to OpenSSL. */
static pthread_mutex_t* mutex_buf = NULL;
// 静态锁的数目
static long*            mutex_count = 0;
// 全局只需要一个SSL_CTX，一旦设置之后就不应该修改它，
// 所有的SSL都在这个ctx上面创建
SSL_CTX* global_ssl_ctx = NULL;

static void pthreads_locking_callback(int mode, int type, char *file,
         int line) {
#if 0
    tzhttpd_log_err("thread=%4d mode=%s lock=%s %s:%d\n",
        CRYPTO_thread_id(),
        (mode&CRYPTO_LOCK)?"l":"u",
        (type&CRYPTO_READ)?"r":"w",file,line);
#endif
    if (mode & CRYPTO_LOCK){
        pthread_mutex_lock(&(mutex_buf[type]));
        mutex_count[type]++;
    } else {
        pthread_mutex_unlock(&(mutex_buf[type]));
    }
}

#if 0
static void handle_error(const char *file, int lineno, const char *msg) {
    tzhttpd_log_err("** SSL error %s:%d %s\n", file, lineno, msg);
    ERR_print_errors_fp(stderr);
}
#endif

static unsigned long pthreads_thread_id(void) {
    unsigned long ret;

    ret=(unsigned long)pthread_self();
    return(ret);
}

static SSL_CTX* ssl_setup_client_ctx() {
    if (global_ssl_ctx) {
        return global_ssl_ctx;
    }

    // 兼容SSLv3 & TLS_v1
    global_ssl_ctx = SSL_CTX_new(SSLv23_method());

    return global_ssl_ctx;
}

bool Ssl_thread_setup() {

    mutex_buf = (pthread_mutex_t *)OPENSSL_malloc(CRYPTO_num_locks() * sizeof(pthread_mutex_t));
    mutex_count = (long *)OPENSSL_malloc(CRYPTO_num_locks() * sizeof(long));

    if (!mutex_buf || !mutex_count) {
        tzhttpd_log_err("Alloc Ssl thread resource failed!");
        return false;
    }

    for (int i=0; i<CRYPTO_num_locks(); ++i) {
        mutex_count[i] = 0;
        pthread_mutex_init(&(mutex_buf[i]), NULL);
    }

    CRYPTO_set_id_callback((unsigned long (*)())pthreads_thread_id);
    CRYPTO_set_locking_callback((void (*)(int, int, const char*, int))pthreads_locking_callback);

    // SSL common routine setup
    if (!SSL_library_init()) {
        tzhttpd_log_err("Load SSL library failed!");
        return false;
    }

    SSL_load_error_strings();

    if (!ssl_setup_client_ctx()) {
        tzhttpd_log_err("Create global SSL_CTX failed!");
        return false;
    }

    // 屏蔽不安全的SSLv2协议
    SSL_CTX_set_options(global_ssl_ctx, SSL_OP_ALL|SSL_OP_NO_SSLv2);

    // The flag SSL_MODE_AUTO_RETRY will cause read/write operations
    // to only return after the handshake and successful completion
    SSL_CTX_set_options(global_ssl_ctx, SSL_MODE_AUTO_RETRY);


    tzhttpd_log_alert("SSL env setup successful!");
    return true;
}

void Ssl_thread_clean() {

    if (!mutex_buf) {
        tzhttpd_log_err("Ssl already cleaned up??");
        return;
    }

    if (global_ssl_ctx) {
        SSL_CTX_free(global_ssl_ctx);
    }

    CRYPTO_set_id_callback(NULL);
    CRYPTO_set_locking_callback(NULL);

    for (int i=0; i<CRYPTO_num_locks(); ++i)
    {
        pthread_mutex_destroy(&(mutex_buf[i]));
        tzhttpd_log_err("%8ld:%s\n", mutex_count[i],
            CRYPTO_get_lock_name(i));
    }

    OPENSSL_free(mutex_buf);
    OPENSSL_free(mutex_count);

    mutex_buf = NULL;
    mutex_count = 0;

    tzhttpd_log_alert("SSL env cleanup successful!");

    return;
}

} // end namespace tzhttpd
