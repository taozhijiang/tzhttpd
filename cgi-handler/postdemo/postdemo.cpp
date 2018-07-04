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


int cgi_post_handler(const msg_t* param, const msg_t* post, msg_t* rsp, msg_t* rsp_header) {

    std::string msg = "return from postdemo with param:" + std::string(param->data);
    msg += " , and postdata:" + std::string(post->data);
    fill_msg(rsp, msg.c_str(), msg.size());

    std::string strHead = "PostHead1: value1\n PostHead2: value2  ";
    fill_msg(rsp_header, strHead.c_str(), strHead.size());

    return 0;
}


#ifdef __cplusplus
}
#endif
