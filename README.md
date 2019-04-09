### tzhttpd
This is a high-performance while easy-to-be-used HTTP service framework, which can help you develop a HTTP-featured server easily and quickly.   

### Key Points of tzhttpd
1. Developed with Boost.Asio, which means high-concurrency and high-performance. My little very poor virtual machine (1C1G) can support up to 1.5K QPS, so I believe it can satisfy performance requirement for most cases in production.   
2. Just supporting HTTP basic GET/POST methods, but can feed the need of most backend application gateway development. Parameters and post body are well handled and structed. Routing handlers based on uri regex-match, easy for configuration.   
3. Connection can be keep-alived, long-connection means higher performance (about 2x more), and can get ride of TIME-WAIT disasters, and the server also can be tuned to be automatically timed out and removed.   
4. Support loading handlers through .so library, this feature simulates legacy CGI deployment conveniently. This library try its best loading and updating handler with less impact for others. And much more amazing thing is that you can just build one tzhttpd instance and copy it everywhere, and write your handlers, build them to individual so file, add them to configure files and update configuration dynamically, just like plugins.   
5. Based on Boost library and C++0x standard, so can used in legacy but widely-deploied RHEL-6.x environment, also RHEL-7.x is officially supported.   
6. Support regex-based Http Basic Authorization.   
7. Not buggy, and has stood tests in a way in production environment.   

### Possible use case
1. General Web server, KIDDING. tzhttpd does not support full HTTP protocal, so it may not behave well in this situation. But I also add the VHost, Cache Control and some other features, and my [homepage](http://taozj.net) is hosted by this, it works fine currently.   
2. Backend application gateway, YES. tzhttpd is designed and optimized for this purpose.   
3. HTTP protocal adaptor. This way tzhttpd can be use as proxy, it can accept and parse HTTP request, forward the request and transform corresponding respose as standard HTTP response.   
4. Service manage interface. You can integrate tzhttpd into your already project, then you can send customize uri to interfere your service, such as configuration update, runtime statistic output, ... Actually I am changing most of my projects using HTTP interface, before these were implemented by kill Signal.   

### Performance
![siege](siege.png?raw=true "siege")

### Internal UI
```bash
# system status
curl 'http://127.0.0.1:18430/internal/status'

# dynamic update runtime conf
curl 'http://127.0.0.1:18430/internal/updateconf'

# reload so handler
curl 'http://127.0.0.1:18430/internal/drop?uri=^/cgi-bin/getdemo.cgi$'
curl 'http://127.0.0.1:18430/internal/status'
cp libgetdemo.so ../cgi-bin
curl 'http://127.0.0.1:18430/internal/updateconf'
```

### Attention:
With rdynamic and whole-archive link options, dynamic cgi-handler (through .so) can also use main program symbols, but those symbols should be built with --fvisibility=default, or your program in so may complain for undefined symbol. Though so deploy is convenient, but should be using very carefully, please use nm -r to check all U symbols of your \*.so, and use readelf -s to check whether these symbol are DEFAULT visibility in the main program.   

### Reference project
[tzmonitor](https://github.com/taozhijiang/tzmonitor) http_face example.   
