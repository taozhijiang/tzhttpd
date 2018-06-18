cmake_minimum_required (VERSION 2.8.11)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++0x " )
# set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wconversion -Woverloaded-virtual -Wpointer-arith -Wshadow -Wwrite-strings -march=native " )

# if what to see debug in verbose mode
# cmake -DCMAKE_VERBOSE_MAKEFILE=true ../

set(CMAKE_BUILD_TYPE DEBUG)
#set(CMAKE_BUILD_TYPE RELEASE)
set(CMAKE_CXX_FLAGS_DEBUG   "$ENV{CXXFLAGS} -O0 -g")
set(CMAKE_CXX_FLAGS_RELEASE "$ENV{CXXFLAGS} -O2 ")


aux_source_directory(. DIR_LIB_SRCS)
add_library (tzhttpd STATIC ${DIR_LIB_SRCS})


include_directories(
           ../xtra_rhel6.x/include )

install (TARGETS tzhttpd
            RUNTIME DESTINATION bin
            LIBRARY DESTINATION lib
            ARCHIVE DESTINATION lib/static )
