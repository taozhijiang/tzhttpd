#include <string>

#include "../../CgiHelper.h"

#ifdef __cplusplus
extern "C" {
#endif


int module_init() {
    return 0;
}

int module_exit() {
    return 0;
}


int cgi_post_handler(const msg_t* param, const msg_t* post, msg_t* rsp) {
    std::string msg = "return from postdemo with param:" + std::string(param->data);
	msg += " , and postdata:" + std::string(post->data);
    fill_msg(rsp, msg.c_str(), msg.size());
    return 0;
}


#ifdef __cplusplus
}
#endif
