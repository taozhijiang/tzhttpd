cmake_minimum_required (VERSION 2.8.11)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11 " )
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Woverloaded-virtual -Wpointer-arith -Wwrite-strings -Werror=format -march=native " )


# 精简日志
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -D__FILENAME__=\"$(notdir $<)\" " )

# set(CMAKE_BUILD_TYPE DEBUG)
# set(CMAKE_BUILD_TYPE RELEASE)
# set(CMAKE_CXX_FLAGS_DEBUG   "$ENV{CXXFLAGS} -O0 -g")
# set(CMAKE_CXX_FLAGS_RELEASE "$ENV{CXXFLAGS} -O2 ")
add_definitions(-DNP_DEBUG)

set (INCLUDE_DIRS
    ${PROJECT_SOURCE_DIR}/include/
    ${PROJECT_SOURCE_DIR}/source/
    ${PROJECT_SOURCE_DIR}/../xtra_rhel7.x/self/include/
    ${PROJECT_SOURCE_DIR}/../xtra_rhel7.x/google_prefix/include
    ${PROJECT_SOURCE_DIR}/../xtra_rhel7.x/boost_prefix/include
    ${PROJECT_SOURCE_DIR}/../roo
)
include_directories( ${INCLUDE_DIRS} )

# 如果集成在其他项目当中，上面的配置可以继承得到而不用显式配置了


# 日志请参照example中的使用方法，设置log_store_func函数即可
# 如果不设置，那么默认日志打印在终端上面

# 下面测试案例使用
# 正常情况下，tzhttpd是当作子模块使用的，所以最好不要在这个模块中添加业务逻辑代码
# add_subdirectory( cgi-handler/getdemo )
# add_subdirectory( cgi-handler/postdemo )

aux_source_directory(. DIR_LIB_SRCS)
add_library (tzhttpd STATIC ${DIR_LIB_SRCS})
