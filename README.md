### TZHTTPD
This is a high-performance while easy-to-be-used HTTP service framework, which can help you develop a HTTP-featured server quickly.   

### Key Points of TZHTTPD
1. Developed with Boost.Asio, which means high-concurrency with high-performance. Very poor virtual machine can support up to 1.5K QPS, so I believe it can satisfy performance requirements in most cases.    
2. Just supporting HTTP GET/POST methods, but feed the need of most backend application gateway development. Parameters and post body are well handled and structed. Routing handlers based on uri regex-match.    
3. Connection can be keep-alived, and automatically timed out and be removed.   
4. Support loading handlers through .so library, this feature simulates legacy CGI deployment conveniently. This library try its best loading and updating handler with less impact for others. And more amazing thing is that you can just build one tzhttpd instance and copy it everywhere, and write your handlers and build them to individual so, add them to configure files, just like plugins.   
5. Based on Boost library and C++0x standard, so can used in legacy but widely-deploied RHEL-6.x environment.   
6. Support regex-based Http Basic Authorization support.
7. Not buggy, and has stood tests in a way.

### Possible usage
1. General Web server, KIDDING. TZHTTPD does not support full HTTP protocal, so it may not behave well in this situation. But I also add the VHost, Cache Control features, and my [homepage](http://taozj.net) is hosted by this, it works fine.   
2. Backend application gateway, YES. TZHTTPD is designed and optimized for this purpose.   
3. HTTP protocal adaptor. This way TZHTTPD can be use as proxy, it accept and parse HTTP request, forward the request and transform corresponding respose as standard HTTP response.   
4. Service manage interface. You can integrate TZHTTPD into your already project, then you can send customize uri to interfere your service, such as configuration update, runtime statistic output, ...   

### Performance
![siege](siege.png?raw=true "siege")

### Manage URI
```bash
# dynamic reload cfg
curl 'http://127.0.0.1:18430/internal_manage?cmd=reload'

# disable & enable handler
curl 'http://127.0.0.1:18430/internal_manage?cmd=switch_handler&method=get&path=^/cgi-bin/getdemo.cgi$&enable=off'
curl 'http://127.0.0.1:18430/internal_manage?cmd=switch_handler&method=get&path=^/cgi-bin/getdemo.cgi$&enable=on'

# steps to dynamic update handler from so, without impact other service
curl 'http://127.0.0.1:18430/internal_manage?cmd=switch_handler&method=get&path=^/cgi-bin/getdemo.cgi$&enable=off'
cp libgetdemo.so ../cgi-bin 
curl 'http://127.0.0.1:18430/internal_manage?cmd=update_handler&method=get&path=^/cgi-bin/getdemo.cgi$&enable=on'
```

### NOTE
As a common sense, you should try your best to make your uri not blocking or consume too long time, otherwise your server will support only little throughput. Sometime these may be inevitable, I will try to fix this problem later if I can.

### Reference project   
[tzmonitor](https://github.com/taozhijiang/tzmonitor).   

