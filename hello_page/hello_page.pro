QT += core gui widgets

TEMPLATE = lib
CONFIG += plugin
TARGET = hello_page

include($$PWD/../../qmake/build.pri)
include($$PROJECT_ROOT/src/plugin_sdk/plugin_sdk.pri)

INCLUDEPATH += $$PWD/include

HEADERS += \
    $$PWD/include/hello_page_plugin.h \
    $$PWD/include/hello_page_widget.h

SOURCES += \
    $$PWD/src/hello_page_plugin.cpp \
    $$PWD/src/hello_page_widget.cpp

RESOURCES += \
    $$PWD/hello_page.qrc

DISTFILES += \
    $$PWD/include/hello_page.json
