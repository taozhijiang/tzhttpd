#!/bin/bash

echo "build normal httpsrv ..."
g++ -std=c++0x -g -O0 -DNP_DEBUG -Wall  main.cpp -I../ -I../../xtra_rhel6.x/include -L../build/ -L../../xtra_rhel6.x/libs/ -L../../xtra_rhel6.x/libs/boost/ -ltzhttpd -lboost_system -lboost_thread -lboost_date_time -lboost_regex -lpthread -lrt -rdynamic -ldl -lconfig++ -lssl -lcryptopp -o httpsrv

echo "build httpsrv with tzmonitor support ..."
 g++ -std=c++0x -g -O0 -DNP_DEBUG -Wall main2.cpp -I../ -I../../xtra_rhel6.x/include -L./tzmonitor -L../build/ -L../../xtra_rhel6.x/libs/ -L../../xtra_rhel6.x/libs/boost/ -ltzhttpd -ltzmonitor_client -ljsoncpp -lthrifting -lthriftz -lthrift -lboost_system -lboost_thread -lboost_date_time -lboost_regex -lpthread -lrt -rdynamic -ldl -lconfig++ -lssl -lcryptopp -lcurl -lssh2 -o httpsrv2
