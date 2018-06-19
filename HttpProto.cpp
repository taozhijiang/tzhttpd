#include <boost/date_time/posix_time/posix_time.hpp>
using namespace boost::posix_time;
using namespace boost::gregorian;

#include <sstream>
#include "HttpProto.h"


namespace tzhttpd {
    
extern std::string TZ_VERSION;

namespace http_proto {

struct header {
  std::string name;
  std::string value;
};


/**
 * 由于最终的底层都是调用c_str()发送的，所以这里不添加额外的字符
 */
string http_response_generate(const string& content, const string& stat_str) {

    std::vector<header> headers(7);

    // reply fixed header
    headers[0].name = "Server";
    headers[0].value = "TZhttpd Server/" + TZ_VERSION;
    headers[1].name = "Date";
    headers[1].value = to_simple_string(second_clock::universal_time());
    headers[2].name = "Content-Length";
    headers[2].value = std::to_string(static_cast<long long unsigned>(content.size()));
    headers[3].name = "Content-Type";
    headers[3].value = "text/html";
    headers[4].name = "Connection";
    headers[4].value = "keep-alive";  // 长连接
    // headers[4].value = "close";  // 短连接
    headers[5].name = "Cache-Control";
    headers[5].value = "no-cache";
    headers[6].name = "Access-Control-Allow-Origin";
    headers[6].value = "*";

    string str = stat_str;
    for (size_t i=0; i< headers.size(); ++i) {
        str += headers[i].name;
        str += header_name_value_separator_str;
        str += headers[i].value;
        str += header_crlf_str;
    }

    str += header_crlf_str;
    str += content;

    return str;
}

string http_response_generate(const char* data, size_t len, const string& stat_str) {

    std::vector<header> headers(6);

    // reply fixed header
    headers[0].name = "Server";
    headers[0].value = "TZhttpd Server/" + TZ_VERSION;
    headers[1].name = "Date";
    headers[1].value = to_simple_string(second_clock::universal_time());
    headers[2].name = "Content-Length";
    headers[2].value = std::to_string(static_cast<long long int>(len));
    headers[3].name = "Content-Type";
    headers[3].value = "text/html";
    headers[4].name = "Connection";
    headers[4].value = "keep-alive";
    headers[5].name = "Access-Control-Allow-Origin";
    headers[5].value = "*";

    string str = stat_str;
    for (size_t i=0; i< headers.size(); ++i) {
        str += headers[i].name;
        str += header_name_value_separator_str;
        str += headers[i].value;
        str += header_crlf_str;
    }

    str += header_crlf_str;
    str += data;

    return str;
}


static std::string get_status_content(enum StatusCode code) {
    const auto iter = status_code_strings.find(code);
    if(iter != status_code_strings.end()) {
        return iter->second;
    }

    return "";
}

string http_std_response_generate(const std::string& http_ver, enum StatusCode stat) {

    std::stringstream content_ss;
    std::string msg = get_status_content(stat);

    content_ss << "<html><head><title>"
               << msg
               << "</title></head>"
               << "<body><h1>"
               << msg
               << "</h1></body></html>";

    std::string content = content_ss.str();
    std::string status_line = generate_response_status_line(http_ver, http_proto::StatusCode::success_ok);

    return http_response_generate(content, status_line);
}


int http_url_decode(const std::string& in, std::string& out) {

    out.clear();
    out.reserve(in.size());

    for (std::size_t i = 0; i < in.size(); ++i) {
        if (in[i] == '%') {
            if (i + 3 <= in.size()) {

                int value = 0;
                std::istringstream is(in.substr(i + 1, 2));

                if (is >> std::hex >> value) {
                    out += static_cast<char>(value);
                    i += 2;
                } else {
                    return -1;
                }
            } else {
                return -1;
            }
        } else if (in[i] == '+'){
            out += ' ';
        } else {
            out += in[i];
        }
    }

    return 0;
}

}  // end namespace http_proto
}  // end namespace tzhttpd

