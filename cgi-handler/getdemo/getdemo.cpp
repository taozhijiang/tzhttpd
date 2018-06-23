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


int cgi_get_handler(const msg_t* param, msg_t* rsp) {
    std::string msg = "return from getdemo with param:" + std::string(param->data);
    fill_msg(rsp, msg.c_str(), msg.size());
    return 0;
}


#ifdef __cplusplus
}
#endif
