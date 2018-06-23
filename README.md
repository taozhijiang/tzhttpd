
This is a high-performance and easy-to-use HTTP service framework, which can help you develope a HTTP-based server quickly with just a few steps.    

The key-points of this library includes:    
1. Developed with Boost.Asio, which means high-concurrency and high-performance. Very poor virtual machine can support upto 1.5K QPS, so I believe it can satisfy performance requirements in most cases.    
2. Though just support HTTP GET/POST methods, which feed the need of most backend server developments, and parameters and post body are well handled. Routing handlers based on uri regex-match.    
3. We support loading handlers through .so dynamicly, this feature simulates legacy CGI deployment.    
4. Based on Boost library and C++0x standard, so can used in legacy but widely-deploied RHEL-6.x environment.   
5. Not buggy, and has stood tests in a way.    


