#!/bin/bash

echo "build normal httpsrv ..."
g++ -std=c++0x -g -O0 -DNP_DEBUG -Wall  main.cpp setup.cpp \
    -I../ -I../../xtra_rhelz.x/include \
    -L../build/ -L../../xtra_rhelz.x/libs/ -L../../xtra_rhelz.x/libs/boost/ \
    -ltzhttpd -lboost_system -lboost_thread -lboost_chrono -lboost_regex -lpthread -lrt -rdynamic -ldl -lconfig++ -lssl -lcryptopp -lcrypto \
    -o httpsrv

echo "build httpsrv with tzmonitor support ..."
g++ -std=c++0x -g -O0 -DNP_DEBUG -Wall main_with_report.cpp setup.cpp \
    -I../ -I../../xtra_rhelz.x/include \
    -L./tzmonitor -L../build/ -L../../xtra_rhelz.x/libs/ -L../../xtra_rhelz.x/libs/boost/ -L../../xtra_rhelz.x/libs/google/protobuf-2.5.0/ \
    -ltzhttpd -lMonitorClient -lboost_system -lboost_thread -lboost_chrono -lboost_regex -lpthread -lrt -rdynamic -ldl -lconfig++ -lssl -lcryptopp -lcrypto -lcurl -lprotoc -lprotobuf \
    -o httpsrv_with_report
