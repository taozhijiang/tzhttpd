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


int cgi_handler(const msg_t* req, msg_t* resp) {
    std::string msg = "return from demo so, with orign:" + std::string(req->data);
    fill_msg(resp, msg.c_str(), msg.size());
    return 0;
}


#ifdef __cplusplus
}
#endif
