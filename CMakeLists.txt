cmake_minimum_required (VERSION 2.8.11)
# set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++0x " )
# set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wconversion -Woverloaded-virtual -Wpointer-arith -Wshadow -Wwrite-strings -march=native " )

# set(CMAKE_BUILD_TYPE DEBUG)
# set(CMAKE_BUILD_TYPE RELEASE)
# set(CMAKE_CXX_FLAGS_DEBUG   "$ENV{CXXFLAGS} -O0 -g")
# set(CMAKE_CXX_FLAGS_RELEASE "$ENV{CXXFLAGS} -O2 ")
# include_directories( ../xtra_rhel6.x/include )

# 如果集成在其他项目当中，上面的配置可以继承得到而不用显式配置了

# 如果使用syslog，则将下面的定义打开；否则所有的日志都会打出在终端上
# add_definitions( -DTZHTTPD_USING_SYSLOG )

# 下面测试案例使用
# 正常情况下，tzhttpd是当作子模块使用的，所以最好不要在这个模块中添加业务逻辑代码
# add_subdirectory( cgi-handler/getdemo )
# add_subdirectory( cgi-handler/postdemo )

aux_source_directory(. DIR_LIB_SRCS)
add_library (tzhttpd STATIC ${DIR_LIB_SRCS})
