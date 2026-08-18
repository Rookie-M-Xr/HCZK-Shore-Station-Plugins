// Copyright 2026.
// License: All rights reserved.

#include "hello_page_plugin.h"

#include "hello_page_widget.h"

namespace shore_station::hello_page
{
HelloPagePlugin::HelloPagePlugin()
    : context_(nullptr)
{
}

plugin_sdk::PluginDescriptor HelloPagePlugin::descriptor() const
{
    plugin_sdk::PluginDescriptor descriptor;
    descriptor.id = QStringLiteral("com.hczk.shore.hello");
    descriptor.name = QStringLiteral("Hello");
    descriptor.version = QStringLiteral("1.0.0");
    descriptor.sdk_version = plugin_sdk::sdkVersion();
    descriptor.kind = plugin_sdk::PluginKind::Application;
    descriptor.icon = QStringLiteral(":/hello_page/icon.svg");
    return descriptor;
}

plugin_sdk::PluginResult HelloPagePlugin::initialize(plugin_sdk::IPluginContext &context)
{
    context_ = &context;
    context.ui().addPage(*this);
    context.log().info(descriptor().id, QStringLiteral("已注册 Hello 页面"));
    return plugin_sdk::PluginResult::success();
}

plugin_sdk::PluginResult HelloPagePlugin::start()
{
    if (context_ != nullptr) {
        context_->log().info(descriptor().id, QStringLiteral("start"));
    }
    return plugin_sdk::PluginResult::success();
}

void HelloPagePlugin::stop() noexcept
{
    if (context_ != nullptr) {
        context_->log().info(descriptor().id, QStringLiteral("stop"));
    }
    context_ = nullptr;
}

plugin_sdk::PageDescriptor HelloPagePlugin::pageDescriptor() const
{
    plugin_sdk::PageDescriptor descriptor;
    descriptor.id = QStringLiteral("hello.main");
    descriptor.title = tr("示例");
    descriptor.icon = QStringLiteral("layers");
    return descriptor;
}

QWidget *HelloPagePlugin::createPage(QWidget *parent)
{
    return new HelloPageWidget(parent);
}
}  // namespace shore_station::hello_page
