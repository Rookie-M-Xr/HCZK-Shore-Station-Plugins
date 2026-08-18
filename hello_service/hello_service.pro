QT += core gui widgets

TEMPLATE = lib
CONFIG += plugin
TARGET = hello_service

include($$PWD/../../qmake/build.pri)
include($$PROJECT_ROOT/src/plugin_sdk/plugin_sdk.pri)

INCLUDEPATH += $$PWD/include

HEADERS += \
    $$PWD/include/hello_service_plugin.h

SOURCES += \
    $$PWD/src/hello_service_plugin.cpp

DISTFILES += \
    $$PWD/include/hello_service.json
