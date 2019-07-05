#!/bin/bash

echo "build normal httpsrv ..."
g++ -std=c++0x -g -O0 -DNP_DEBUG -Wall  main.cpp setup.cpp \
    -I../ -I../../roo \
    -I../ -I../../xtra_rhelz.x/include -I../ -I../../xtra_rhelz.x/include/google \
    -L../build/ -L../../xtra_rhelz.x/libs/ -L../../xtra_rhelz.x/libs/google/ \
    -L../../xtra_rhelz.x/libs/boost/ \
    -ltzhttpd libRoo.a \
    -lboost_system -lboost_thread -lboost_chrono -lboost_regex \
    -lpthread -lrt -rdynamic -ldl -lconfig++ -lssl -lcryptopp -lcrypto \
    -o httpsrv

echo "build fast_client..."
g++ -std=c++0x -g -O0 -DNP_DEBUG -Wall fast_client.cpp \
    -I../ -I../../roo \
    -I../ -I../../xtra_rhelz.x/include -I../../xtra_rhelz.x/include/curl_7.53.1 \
    -L../../xtra_rhelz.x/libs \
    -L../../xtra_rhelz.x/libs/curl_7.53.1 -L../../xtra_rhelz.x/libs/boost/ \
    -lboost_system -lssl -lcryptopp -lcrypto -lcurl -lssh2 -lrt \
    -o fast_client
