/*-
 * Copyright (c) 2018 TAO Zhijiang<taozhijiang@gmail.com>
 *
 * Licensed under the BSD-3-Clause license, see LICENSE for full information.
 *
 */
 
#ifndef __TZHTTPD_SSL_SETUP_H__
#define __TZHTTPD_SSL_SETUP_H__

#include <openssl/ssl.h>

namespace tzhttpd {
	
bool Ssl_thread_setup();
void Ssl_thread_clean();

extern SSL_CTX* global_ssl_ctx;

} // end namespace tzhttpd

#endif //__TZHTTPD_SSL_SETUP_H__
