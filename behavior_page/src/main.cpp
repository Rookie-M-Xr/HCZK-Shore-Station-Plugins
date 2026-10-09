// Copyright 2026.
// License: All rights reserved.
//
// 行为识别插件的独立入口。直接运行本程序调试界面，不加载岸基主体。

#include <QApplication>
#include <QDebug>
#include <QFile>
#include <QFileInfo>
#include <QFont>
#include <QMainWindow>
#include <QTimer>
#include <QUrl>
#include <functional>

#include "behavior_page_plugin.h"
#include "i_appearance_feed.h"
#include "i_appearance_sink.h"
#include "i_cloud_blob.h"
#include "i_hczk_feed.h"
#include "i_hczk_sink.h"
#include "i_log_service.h"
#include "i_plugin_context.h"
#include "i_ui_registry.h"

namespace
{
class StandaloneLog final : public shore_station::plugin_sdk::ILogService
{
public:
    void info(const QString &plugin_id, const QString &message) override
    {
        qInfo().noquote() << plugin_id << message;
    }

    void warning(const QString &plugin_id, const QString &message) override
    {
        qWarning().noquote() << plugin_id << message;
    }

    void error(const QString &plugin_id, const QString &message) override
    {
        qCritical().noquote() << plugin_id << message;
    }
};

class StandaloneUi final : public shore_station::plugin_sdk::IUiRegistry
{
public:
    void addPage(shore_station::plugin_sdk::IPageProvider &) override
    {
    }
};

class StandaloneBlob final : public shore_station::plugin_sdk::ICloudBlob
{
public:
    void fetch(const shore_station::plugin_sdk::CloudBlobKey &key,
               std::function<void(shore_station::plugin_sdk::CloudBlobResult)> done) override
    {
        if (!done) {
            return;
        }
        shore_station::plugin_sdk::CloudBlobResult result;
        QString path = key.path;
        const QUrl url(key.path);
        if (url.isLocalFile()) {
            path = url.toLocalFile();
        }
        if (!path.isEmpty() && QFileInfo::exists(path)) {
            QFile file(path);
            if (file.open(QIODevice::ReadOnly)) {
                result.bytes = file.readAll();
                result.status = result.bytes.isEmpty()
                    ? shore_station::plugin_sdk::CloudBlobStatus::NotReady
                    : shore_station::plugin_sdk::CloudBlobStatus::Ok;
                if (result.bytes.isEmpty()) {
                    result.error = QStringLiteral("文件尚无内容");
                }
            } else {
                result.status = shore_station::plugin_sdk::CloudBlobStatus::Failed;
                result.error = QStringLiteral("无法读取本地图片");
            }
        } else {
            result.status = shore_station::plugin_sdk::CloudBlobStatus::NotReady;
            result.error = QStringLiteral("文件尚未就绪");
        }
        QTimer::singleShot(0, [done = std::move(done), result = std::move(result)]() {
            done(result);
        });
    }
};

class StandaloneContext final : public shore_station::plugin_sdk::IPluginContext,
                                public shore_station::plugin_sdk::IHczkFeed,
                                public shore_station::plugin_sdk::IAppearanceFeed
{
public:
    shore_station::plugin_sdk::ILogService &log() override
    {
        return log_;
    }

    shore_station::plugin_sdk::IUiRegistry &ui() override
    {
        return ui_;
    }

    shore_station::plugin_sdk::IHczkFeed &hczk() override
    {
        return *this;
    }

    shore_station::plugin_sdk::IAppearanceFeed &appearance() override
    {
        return *this;
    }

    shore_station::plugin_sdk::ICloudBlob &cloudBlob() override
    {
        return blob_;
    }

    QVector<shore_station::plugin_sdk::HostCamera> cameras() const override
    {
        return {};
    }

    QString shipName(quint32 ship_id) const override
    {
        return QString::number(ship_id);
    }

    QString theme() const override
    {
        return QStringLiteral("light");
    }

    QString language() const override
    {
        return QStringLiteral("zh_CN");
    }

    void subscribe(shore_station::plugin_sdk::IHczkSink &sink) override
    {
        hczk_sink_ = &sink;
    }

    void unsubscribe(shore_station::plugin_sdk::IHczkSink &sink) override
    {
        if (hczk_sink_ == &sink) {
            hczk_sink_ = nullptr;
        }
    }

    void subscribe(shore_station::plugin_sdk::IAppearanceSink &sink) override
    {
        appearance_sink_ = &sink;
    }

    void unsubscribe(shore_station::plugin_sdk::IAppearanceSink &sink) override
    {
        if (appearance_sink_ == &sink) {
            appearance_sink_ = nullptr;
        }
    }

private:
    StandaloneLog log_;
    StandaloneUi ui_;
    StandaloneBlob blob_;
    shore_station::plugin_sdk::IHczkSink *hczk_sink_ = nullptr;
    shore_station::plugin_sdk::IAppearanceSink *appearance_sink_ = nullptr;
};
}  // namespace

int main(int argc, char *argv[])
{
    QApplication application(argc, argv);
    application.setApplicationName(QStringLiteral("behavior_page"));
    application.setFont(QFont(QStringLiteral("Microsoft YaHei"), 10));

    int exit_code = 1;
    {
        StandaloneContext context;
        shore_station::behavior_page::BehaviorPagePlugin plugin;
        const shore_station::plugin_sdk::PluginResult initialized = plugin.initialize(context);
        if (!initialized.ok) {
            qCritical().noquote() << initialized.message;
            return 1;
        }
        const shore_station::plugin_sdk::PluginResult started = plugin.start();
        if (!started.ok) {
            qCritical().noquote() << started.message;
            plugin.stop();
            return 1;
        }

        QMainWindow window;
        window.setWindowTitle(plugin.pageDescriptor().title);
        window.setCentralWidget(plugin.createPage(&window));
        window.resize(1280, 800);
        window.show();
        exit_code = application.exec();
        plugin.stop();
    }
    return exit_code;
}
