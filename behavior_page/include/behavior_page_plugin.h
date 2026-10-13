// Copyright 2026.
// License: All rights reserved.
//
// 行为识别应用插件：宫格页面，接收主体推送的识别结果与图像。

#ifndef SHORE_STATION_BEHAVIOR_PAGE_BEHAVIOR_PAGE_PLUGIN_H
#define SHORE_STATION_BEHAVIOR_PAGE_BEHAVIOR_PAGE_PLUGIN_H

#include <QPointer>

#include "i_behavior_sink.h"
#include "i_page_provider.h"
#include "shore_plugin_base.h"

namespace shore_station::behavior_page
{
class BehaviorPageWidget;
class BehaviorStore;

class BehaviorPagePlugin final : public plugin_sdk::ShorePluginBase,
                                 public plugin_sdk::IPageProvider,
                                 public plugin_sdk::IBehaviorSink
{
    Q_OBJECT
#ifdef SHORE_AS_PLUGIN
    Q_PLUGIN_METADATA(IID IShorePlugin_iid FILE "behavior_page.json")
#endif
    Q_INTERFACES(shore_station::plugin_sdk::IShorePlugin)

public:
    BehaviorPagePlugin();
    ~BehaviorPagePlugin() override;

    plugin_sdk::PluginDescriptor descriptor() const override;
    plugin_sdk::PluginResult initialize(plugin_sdk::IPluginContext &context) override;
    plugin_sdk::PluginResult start() override;
    void stop() noexcept override;

    plugin_sdk::PageDescriptor pageDescriptor() const override;
    QWidget *createPage(QWidget *parent) override;
    void onBehaviorEvent(const plugin_sdk::BehaviorEvent &event) override;
    void onThemeChanged(const QString &theme);

private:
    /***************************************************************
     * @brief 把主体的船号、船名、取图和日志接到存储
     **************************************************************/
    void bindHost(plugin_sdk::IPluginContext &context);

    BehaviorStore *store_;
    QPointer<BehaviorPageWidget> page_;
};
}  // namespace shore_station::behavior_page

#endif  // SHORE_STATION_BEHAVIOR_PAGE_BEHAVIOR_PAGE_PLUGIN_H
