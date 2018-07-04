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


int cgi_get_handler(const msg_t* param, msg_t* rsp, msg_t* rsp_header) {

    std::string msg = "return from getdemo with param:" + std::string(param->data);
    fill_msg(rsp, msg.c_str(), msg.size());

    std::string strHead = "GetHead1: value1\n GetHead2: value2\n";
    fill_msg(rsp_header, strHead.c_str(), strHead.size());

    return 0;
}


#ifdef __cplusplus
}
#endif
