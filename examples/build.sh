#!/bin/bash

echo "build normal httpsrv ..."
g++ -std=c++11 -g -O0 -DNP_DEBUG -Wall  main.cpp setup.cpp \
    -I../ -I../../roo \
    -I../ -I../../xtra_rhel7.x/self/include -I../ -I../../xtra_rhel7.x/google_prefix/include \
    -I../../xtra_rhel7.x/boost_prefix/include/ \
    -L../build/ -L../../xtra_rhel7.x/self/lib/ -L../../xtra_rhel7.x/google_prefix/lib/ \
    -L../../xtra_rhel7.x/boost_prefix/lib/ \
    -ltzhttpd libRoo.a \
    -lboost_system -lboost_thread -lboost_chrono -lboost_regex \
    -lpthread -lrt -rdynamic -ldl -lconfig++ -lssl -lcryptopp -lcrypto \
    -lglogb \
    -o httpsrv

echo "build fast_client..."
g++ -std=c++0x -g -O0 -DNP_DEBUG -Wall fast_client.cpp \
    -I../ -I../../roo \
    -I../ -I../../xtra_rhel7.x/self/include -I../../xtra_rhel7.x/self/include/curl-7.53.1 \
    -I../ -I../../xtra_rhel7.x/boost_prefix/include \
    -L../../xtra_rhel7.x/self/lib \
    -L../../xtra_rhel7.x/self/lib/curl-7.53.1 -L../../xtra_rhel7.x/boost_prefix/lib/ \
    -lboost_system -lssl -lcryptopp -lcrypto -lcurl -lssh2 -lrt -lz -lpthread \
    -o fast_client
