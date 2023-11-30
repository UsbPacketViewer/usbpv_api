#-------------------------------------------------
#
# Project created by QtCreator 2020-05-20T10:14:12
#
#-------------------------------------------------

QT -= qt
QT -= gui core

CONFIG += c++11 console
CONFIG -= app_bundle

TARGET = test_usbpv_lib_s

# CONFIG += staticlib

# The following define makes your compiler emit warnings if you use
# any feature of Qt which has been marked as deprecated (the exact warnings
# depend on your compiler). Please consult the documentation of the
# deprecated API in order to know how to port your code away from it.
DEFINES += QT_DEPRECATED_WARNINGS

# You can also make your code fail to compile if you use deprecated APIs.
# In order to do so, uncomment the following line.
# You can also select to disable deprecated APIs only up to a certain version of Qt.
#DEFINES += QT_DISABLE_DEPRECATED_BEFORE=0x060000    # disables all the APIs deprecated before Qt 6.0.0


SOURCES +=  usbpv_s.cpp usbpv_util.cpp test_usbpv_s.cpp
HEADERS += usbpv_s.h

# -------------------------------------------------
# sources for libusb
# -------------------------------------------------
win32:DEFINES += _WIN32_WINNT=0x0500 FACILITY_SETUPAPI=15 _UNICODE

INCLUDEPATH += ./libusb-1.0.23 ./libusb-1.0.23/libusb
HEADERS += ./libusb-1.0.23/libusb/libusbi.h \
           ./libusb-1.0.23/libusb/libusb.h \
           ./libusb-1.0.23/libusb/version.h \
           ./libusb-1.0.23/libusb/version_nano.h \
           ./libusb-1.0.23/libusb/os/poll_windows.h \
           ./libusb-1.0.23/libusb/os/threads_windows.h \
           ./libusb-1.0.23/libusb/os/windows_common.h \
           ./libusb-1.0.23/libusb/os/windows_nt_common.h \
           ./libusb-1.0.23/libusb/os/windows_winusb.h

SOURCES += ./libusb-1.0.23/libusb/core.c \
           ./libusb-1.0.23/libusb/descriptor.c \
           ./libusb-1.0.23/libusb/hotplug.c \
           ./libusb-1.0.23/libusb/io.c \
           ./libusb-1.0.23/libusb/strerror.c \
           ./libusb-1.0.23/libusb/sync.c
win32{
SOURCES += ./libusb-1.0.23/libusb/os/poll_windows.c \
           ./libusb-1.0.23/libusb/os/threads_windows.c \
           ./libusb-1.0.23/libusb/os/windows_nt_common.c \
           ./libusb-1.0.23/libusb/os/windows_winusb.c \
           ./libusb-1.0.23/libusb/os/windows_usbdk.c
}

unix{
SOURCES += ./libusb-1.0.23/libusb/os/poll_posix.c \
           ./libusb-1.0.23/libusb/os/threads_posix.c \
           ./libusb-1.0.23/libusb/os/linux_usbfs.c \
           ./libusb-1.0.23/libusb/os/linux_udev.c
LIBS+=-ludev
}
