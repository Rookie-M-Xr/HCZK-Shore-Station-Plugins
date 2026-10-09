// Copyright 2026.
// License: All rights reserved.
//
// 行为识别本地索引、向主体取图与按天数清理。

#ifndef SHORE_STATION_BEHAVIOR_PAGE_BEHAVIOR_STORE_H
#define SHORE_STATION_BEHAVIOR_PAGE_BEHAVIOR_STORE_H

#include <QHash>
#include <QObject>
#include <QString>
#include <QVector>
#include <QtGlobal>
#include <functional>

#include "behavior_types.h"
#include "host_camera.h"
#include "i_cloud_blob.h"

class QTimer;

namespace shore_station::behavior_page
{
struct BehaviorRecord
{
    quint32 ship_id = 0;
    QString ship_name;
    quint32 message_id = 0;
    quint16 camera_id = 0;
    QString camera_name;
    QString position;
    qint64 timestamp_ms = 0;
    QString image_url;
    QVector<quint8> behaviors;
    QString local_path;
};

class BehaviorStore final : public QObject
{
    Q_OBJECT

public:
    explicit BehaviorStore(QObject *parent = nullptr);
    ~BehaviorStore() override;

    /***************************************************************
     * @brief 写入数据根目录，config / data / picture 都在其下
     **************************************************************/
    void setDataRoot(const QString &root);

    /***************************************************************
     * @brief 设置取图入口，不拥有
     **************************************************************/
    void setCloudBlob(plugin_sdk::ICloudBlob *blob);

    /***************************************************************
     * @brief 开始定时取图与每日清理
     **************************************************************/
    void start();

    /***************************************************************
     * @brief 停止定时器，正在进行的取图回调作废
     **************************************************************/
    void stop();

    /***************************************************************
     * @brief 写入一条识别记录；重复的船号与消息号忽略
     **************************************************************/
    void ingest(const BehaviorFrame &frame);

    /***************************************************************
     * @brief 设置主体摄像头查询；页面筛选时调用
     **************************************************************/
    void setCameraQuery(std::function<QVector<plugin_sdk::HostCamera>()> query);

    /***************************************************************
     * @brief 主体当前摄像头；未设置查询时为空
     **************************************************************/
    QVector<plugin_sdk::HostCamera> hostCameras() const;

    /***************************************************************
     * @brief 当前全部记录，新的在前
     **************************************************************/
    QVector<BehaviorRecord> records() const;

    /***************************************************************
     * @brief 保存天数，范围 1–365
     **************************************************************/
    int retainDays() const;

    /***************************************************************
     * @brief 写入保存天数并立即清理
     **************************************************************/
    void setRetainDays(int days);

    /***************************************************************
     * @brief 用户指定的图片目录；空表示使用默认 picture
     **************************************************************/
    QString customPicturePath() const;

    /***************************************************************
     * @brief 设置图片目录；空则回到默认路径
     **************************************************************/
    void setCustomPicturePath(const QString &path);

    /***************************************************************
     * @brief 自动拉取间隔，单位秒，范围 1–3600
     **************************************************************/
    int downloadIntervalSec() const;

    /***************************************************************
     * @brief 写入自动拉取间隔并立即生效
     **************************************************************/
    void setDownloadIntervalSec(int seconds);

    /***************************************************************
     * @brief 当前实际写入新图片的目录
     **************************************************************/
    QString pictureDirectory() const;

    /***************************************************************
     * @brief 插件设置目录，只放 settings.json
     **************************************************************/
    QString defaultConfigDirectory() const;

    /***************************************************************
     * @brief 识别记录目录，与配置分开
     **************************************************************/
    QString defaultDataDirectory() const;

    /***************************************************************
     * @brief 默认图片目录，位于插件目录下 picture
     **************************************************************/
    QString defaultPictureDirectory() const;

    /***************************************************************
     * @brief 行为类型码对应文案；未知码带上数字
     **************************************************************/
    static QString behaviorText(quint8 type);

signals:
    void recordsChanged();

private:
    QString pluginRoot() const;
    QString recordKey(quint32 ship_id, quint32 message_id) const;
    QString settingsFilePath() const;
    QString indexFilePath() const;
    void ensureDirectories() const;
    void loadSettings();
    void saveSettings() const;
    void loadIndex();
    void saveIndex() const;
    struct PendingFetch
    {
        QString key;
        qint64 first_at_ms = 0;
        qint64 next_at_ms = 0;
        int attempts = 0;
    };

    /***************************************************************
     * @brief 把本地还没有图片的记录放进待取队列
     **************************************************************/
    void enqueuePending();
    /***************************************************************
     * @brief 待取队列里没有这条时加入；delay_first 为首次入队稍等再取
     **************************************************************/
    void enqueueIfNeeded(const QString &key, bool delay_first);
    int pendingIndex(const QString &key) const;
    /***************************************************************
     * @brief 第一条已经到重试时间的待取记录下标；没有则 -1
     **************************************************************/
    int duePendingIndex() const;
    /***************************************************************
     * @brief 空闲时取下一条已到期的记录；同一条正在取时不再开第二次
     **************************************************************/
    void fetchNext();
    void finishFetch(PendingFetch item, const plugin_sdk::CloudBlobResult &result, int generation);
    /***************************************************************
     * @brief 云上还没有这张图：加次数、算下次时间，放到队列后面
     **************************************************************/
    void requeueNotReady(PendingFetch item);
    void clearInFlight();
    void onFetchTimeout();
    /***************************************************************
     * @brief 按队列里最早的下次可取时间叫醒；不超过用户设置的拉取间隔
     **************************************************************/
    void schedulePump();
    int pumpCapMs() const;
    qint64 nowMs() const;
    void cleanupExpired();
    QString fileSuffixForUrl(const QString &url) const;
    void rebuildIndexByKey();
    bool importLoosePictures();
    bool classifyMockPictures();
    bool relocatePicturesIntoShipFolders();
    QString imageRelativePath(const BehaviorRecord &record, const QString &suffix) const;
    QString resolveImagePath(const BehaviorRecord &record,
                             const QString &suffix,
                             const QString &current_path) const;
    void removeEmptyPictureDir(const QString &directory) const;

    QVector<BehaviorRecord> records_;
    QHash<QString, int> index_by_key_;
    QVector<PendingFetch> pending_;
    QString custom_picture_path_;
    QString data_root_;
    int retain_days_ = 180;
    int download_interval_sec_ = 10;
    // 空闲叫醒队列的最长间隔，可由独立调试环境变量缩短
    int pump_cap_ms_ = 10000;
    bool started_ = false;
    bool fetching_ = false;
    int fetch_generation_ = 0;
    // 当前正在取的那条记录
    PendingFetch fetching_item_;
    QTimer *download_timer_;
    QTimer *cleanup_timer_;
    // 单次取图超时，避免一直占着「正在取」
    QTimer *fetch_timeout_;
    plugin_sdk::ICloudBlob *cloud_blob_ = nullptr;
    std::function<QVector<plugin_sdk::HostCamera>()> camera_query_;
};
}  // namespace shore_station::behavior_page

#endif  // SHORE_STATION_BEHAVIOR_PAGE_BEHAVIOR_STORE_H
