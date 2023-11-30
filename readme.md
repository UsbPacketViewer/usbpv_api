# Source code SDK for USB Packet Viewer

### usbpv_lib_s.pro

编译库的Qt工程文件, Qt project to build library

### usbpv_lib_s_test.pro

编译测试程序的Qt工程文件, Qt project to build test application

### Makefile

编译测试程序的Makefile,默认使用环境变量中的gcc，如果要使用其它工具链，修改Makefile中的TOOLCHAIN_PREFIX变量值。

Makefile to build test application, Makefile is auto generate by qmake. Before make, setup the TOOLCHAIN_PREFIX in Makefile

