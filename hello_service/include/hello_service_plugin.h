// Copyright 2026.
// License: All rights reserved.
//
// 示例服务插件：无独立页面，在插件中心通过「配置」修改参数。

#ifndef SHORE_STATION_HELLO_SERVICE_HELLO_SERVICE_PLUGIN_H
#define SHORE_STATION_HELLO_SERVICE_HELLO_SERVICE_PLUGIN_H

#include <QObject>
#include <QString>

#include "i_config_provider.h"
#include "i_plugin_context.h"
#include "i_shore_plugin.h"

namespace shore_station::hello_service
{
class HelloServicePlugin final : public QObject,
                                 public plugin_sdk::IShorePlugin,
                                 public plugin_sdk::IConfigProvider
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID IShorePlugin_iid FILE "hello_service.json")
    Q_INTERFACES(shore_station::plugin_sdk::IShorePlugin)

public:
    HelloServicePlugin();

    plugin_sdk::PluginDescriptor descriptor() const override;
    plugin_sdk::PluginResult initialize(plugin_sdk::IPluginContext &context) override;
    plugin_sdk::PluginResult start() override;
    void stop() noexcept override;
    bool configure(QWidget *parent) override;

private:
    // 宿主上下文，stop 后置空
    plugin_sdk::IPluginContext *context_;
    // 示例配置项，由配置对话框修改
    QString remark_;
};
}  // namespace shore_station::hello_service

#endif  // SHORE_STATION_HELLO_SERVICE_HELLO_SERVICE_PLUGIN_H
