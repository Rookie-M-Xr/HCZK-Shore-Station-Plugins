// Copyright 2026.
// License: All rights reserved.

#include "hello_service_plugin.h"

#include <QDialog>
#include <QDialogButtonBox>
#include <QLabel>
#include <QLineEdit>
#include <QVBoxLayout>

namespace shore_station::hello_service
{
HelloServicePlugin::HelloServicePlugin()
    : context_(nullptr)
    , remark_(tr("默认备注"))
{
}

plugin_sdk::PluginDescriptor HelloServicePlugin::descriptor() const
{
    plugin_sdk::PluginDescriptor descriptor;
    descriptor.id = QStringLiteral("com.hczk.shore.hello_service");
    descriptor.name = QStringLiteral("示例服务");
    descriptor.version = QStringLiteral("1.0.0");
    descriptor.sdk_version = plugin_sdk::sdkVersion();
    descriptor.kind = plugin_sdk::PluginKind::Service;
    return descriptor;
}

plugin_sdk::PluginResult HelloServicePlugin::initialize(plugin_sdk::IPluginContext &context)
{
    context_ = &context;
    context.log().info(descriptor().id, QStringLiteral("已注册示例服务（无独立页面）"));
    return plugin_sdk::PluginResult::success();
}

plugin_sdk::PluginResult HelloServicePlugin::start()
{
    if (context_ != nullptr) {
        context_->log().info(descriptor().id, QStringLiteral("start"));
    }
    return plugin_sdk::PluginResult::success();
}

void HelloServicePlugin::stop() noexcept
{
    if (context_ != nullptr) {
        context_->log().info(descriptor().id, QStringLiteral("stop"));
    }
    context_ = nullptr;
}

bool HelloServicePlugin::configure(QWidget *parent)
{
    QDialog dialog(parent);
    dialog.setWindowTitle(tr("示例服务配置"));
    dialog.setModal(true);

    auto *layout = new QVBoxLayout(&dialog);
    auto *hint = new QLabel(tr("备注（由插件自己保存，此处仅作示例）"), &dialog);
    auto *edit = new QLineEdit(remark_, &dialog);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    layout->addWidget(hint);
    layout->addWidget(edit);
    layout->addWidget(buttons);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    if (dialog.exec() != QDialog::Accepted) {
        return false;
    }
    const QString next = edit->text().trimmed();
    if (next == remark_) {
        return false;
    }
    remark_ = next;
    if (context_ != nullptr) {
        context_->log().info(descriptor().id, QStringLiteral("remark updated"));
    }
    return true;
}
}  // namespace shore_station::hello_service
