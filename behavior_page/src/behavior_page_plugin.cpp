// Copyright 2026.
// License: All rights reserved.

#include "behavior_page_plugin.h"

#include <QVector>
#include <QWidget>

#include "behavior_page_widget.h"
#include "behavior_store.h"
#include "i_plugin_context.h"

namespace shore_station::behavior_page
{
BehaviorPagePlugin::BehaviorPagePlugin()
    : ShorePluginBase(QStringLiteral("behavior_page"))
    , store_(nullptr)
{
}

BehaviorPagePlugin::~BehaviorPagePlugin()
{
    stop();
    if (store_ != nullptr && store_->parent() == this) {
        delete store_;
    }
    store_ = nullptr;
}

plugin_sdk::PluginDescriptor BehaviorPagePlugin::descriptor() const
{
    plugin_sdk::PluginDescriptor descriptor;
    descriptor.id = QStringLiteral("com.hczk.shore.behavior");
    descriptor.name = QStringLiteral("行为识别");
    descriptor.version = QStringLiteral("1.0.0");
    descriptor.sdk_version = plugin_sdk::sdkVersion();
    descriptor.kind = plugin_sdk::PluginKind::Application;
    descriptor.icon = QStringLiteral(":/behavior_page/icon.svg");
    return descriptor;
}

plugin_sdk::PluginResult BehaviorPagePlugin::initialize(plugin_sdk::IPluginContext &context)
{
    const plugin_sdk::PluginResult base = ShorePluginBase::initialize(context);
    if (!base.ok) {
        return base;
    }
    if (store_ == nullptr) {
        store_ = new BehaviorStore(this);
    }
    store_->setDataRoot(pluginDataRoot());
    store_->setCloudBlob(&context.cloudBlob());
    store_->setCameraQuery([this]() {
        if (this->context() == nullptr) {
            return QVector<plugin_sdk::HostCamera>{};
        }
        return this->context()->cameras();
    });
    context.ui().addPage(*this);
    context.log().info(descriptor().id, QStringLiteral("已注册行为识别页面"));
    return plugin_sdk::PluginResult::success();
}

plugin_sdk::PluginResult BehaviorPagePlugin::start()
{
    const plugin_sdk::PluginResult base = ShorePluginBase::start();
    if (!base.ok) {
        return base;
    }
    if (store_ == nullptr) {
        return plugin_sdk::PluginResult::failure(QStringLiteral("插件未初始化"));
    }
    store_->start();
    context()->behaviorFeed().subscribe(*this);
    context()->log().info(descriptor().id, QStringLiteral("start"));
    return plugin_sdk::PluginResult::success();
}

void BehaviorPagePlugin::stop() noexcept
{
    if (context() != nullptr) {
        context()->behaviorFeed().unsubscribe(*this);
        context()->log().info(descriptor().id, QStringLiteral("stop"));
    }
    if (store_ != nullptr) {
        store_->setCameraQuery({});
        store_->setCloudBlob(nullptr);
        store_->stop();
    }
    ShorePluginBase::stop();
}

plugin_sdk::PageDescriptor BehaviorPagePlugin::pageDescriptor() const
{
    plugin_sdk::PageDescriptor descriptor;
    descriptor.id = QStringLiteral("behavior.main");
    descriptor.title = tr("行为识别");
    descriptor.icon = QStringLiteral("behavior_nav");
    return descriptor;
}

QWidget *BehaviorPagePlugin::createPage(QWidget *parent)
{
    if (store_ == nullptr) {
        store_ = new BehaviorStore(this);
        store_->setDataRoot(pluginDataRoot());
        if (context() != nullptr) {
            store_->setCloudBlob(&context()->cloudBlob());
        }
    }
    auto *page = new BehaviorPageWidget(*store_, parent);
    if (context() != nullptr) {
        page->setTheme(context()->theme());
    }
    page_ = page;
    return page;
}

void BehaviorPagePlugin::onBehaviorEvent(const plugin_sdk::BehaviorEvent &event)
{
    if (store_ == nullptr) {
        return;
    }
    store_->ingestHostEvent(event, context() != nullptr ? context()->shipName(event.ship_id)
                                                        : QString::number(event.ship_id));
}

void BehaviorPagePlugin::onThemeChanged(const QString &theme)
{
    if (page_ != nullptr) {
        page_->setTheme(theme);
    }
}
}  // namespace shore_station::behavior_page
