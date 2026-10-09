# 行为识别插件，与岸基分开编译。
# 默认：独立调试程序。岸基加载的 DLL：qmake 附加 CONFIG+=shore_plugin

PLUGIN_STORAGE_NAME = behavior_page
SHORE_STANDALONE_DIR = $$clean_path($$PWD/run)
SHORE_PLUGIN_DIR = $$clean_path($$PWD/../../bin/plugins)

# ============= 插件编译配置，单独调试请注释 =============
# CONFIG+=shore_plugin
# ======================================================

include($$PWD/../../qmake/plugin_app.pri)

QT += core gui widgets

include($$PWD/../../qmake/build.pri)
include($$PROJECT_ROOT/src/plugin_sdk/plugin_sdk_impl.pri)

INCLUDEPATH += $$PWD/include

HEADERS += \
    $$PWD/include/behavior_page_plugin.h \
    $$PWD/include/behavior_page_widget.h \
    $$PWD/include/behavior_settings_dialog.h \
    $$PWD/include/behavior_store.h \
    $$PWD/include/behavior_types.h \
    $$PWD/include/behavior_codec.h

SOURCES += \
    $$PWD/src/behavior_page_plugin.cpp \
    $$PWD/src/behavior_page_widget.cpp \
    $$PWD/src/behavior_settings_dialog.cpp \
    $$PWD/src/behavior_store.cpp \
    $$PWD/src/behavior_codec.cpp

RESOURCES += \
    $$PWD/behavior_page.qrc

DISTFILES += \
    $$PWD/include/behavior_page.json \
    $$PWD/style/light.qss \
    $$PWD/style/dark.qss

!contains(CONFIG, shore_plugin) {
    SOURCES += \
        $$PWD/src/main.cpp
}
