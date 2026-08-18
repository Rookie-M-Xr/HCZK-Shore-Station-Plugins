// Copyright 2026.
// License: All rights reserved.
//
// Hello 示例插件：实现 IShorePlugin + IPageProvider，向主体注册一个页面。

#ifndef SHORE_STATION_HELLO_PAGE_HELLO_PAGE_PLUGIN_H
#define SHORE_STATION_HELLO_PAGE_HELLO_PAGE_PLUGIN_H

#include <QObject>

#include "i_page_provider.h"
#include "i_plugin_context.h"
#include "i_shore_plugin.h"

namespace shore_station::hello_page
{
class HelloPagePlugin final : public QObject,
                              public plugin_sdk::IShorePlugin,
                              public plugin_sdk::IPageProvider
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID IShorePlugin_iid FILE "hello_page.json")
    Q_INTERFACES(shore_station::plugin_sdk::IShorePlugin)

public:
    HelloPagePlugin();

    plugin_sdk::PluginDescriptor descriptor() const override;
    plugin_sdk::PluginResult initialize(plugin_sdk::IPluginContext &context) override;
    plugin_sdk::PluginResult start() override;
    void stop() noexcept override;

    plugin_sdk::PageDescriptor pageDescriptor() const override;
    QWidget *createPage(QWidget *parent) override;

private:
    plugin_sdk::IPluginContext *context_;
};
}  // namespace shore_station::hello_page

#endif  // SHORE_STATION_HELLO_PAGE_HELLO_PAGE_PLUGIN_H
