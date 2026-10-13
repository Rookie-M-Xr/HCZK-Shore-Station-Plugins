// Copyright 2026.
// License: All rights reserved.

#include "behavior_store.h"

#include <algorithm>
#include <memory>

#include <QCoreApplication>
#include <QDate>
#include <QDateTime>
#include <QEvent>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <QStringList>
#include <QRegularExpression>
#include <QTime>
#include <QTimeZone>
#include <QTimer>
#include <QUrl>

namespace shore_station::behavior_page
{
namespace
{
constexpr int kMinRetainDays = 1;
constexpr int kMaxRetainDays = 365;
constexpr int kDefaultRetainDays = 180;
constexpr int kDefaultDownloadIntervalSec = 10;
constexpr int kMinDownloadIntervalSec = 1;
constexpr int kMaxDownloadIntervalSec = 3600;
constexpr int kDownloadIntervalMs = kDefaultDownloadIntervalSec * 1000;
constexpr int kFirstFetchDelayMs = 1500;
constexpr int kMinBackoffMs = 2000;
constexpr int kMaxBackoffMs = 60000;
constexpr int kFetchTimeoutMs = 30000;
constexpr int kMaxFetchAttempts = 20;
constexpr qint64 kGiveUpMs = 10LL * 60LL * 1000LL;
constexpr int kMinPumpMs = 200;
constexpr qint64 kDayMs = 24LL * 60LL * 60LL * 1000LL;

int clampRetainDays(int days)
{
    return qBound(kMinRetainDays, days, kMaxRetainDays);
}

int clampDownloadIntervalSec(int seconds)
{
    return qBound(kMinDownloadIntervalSec, seconds, kMaxDownloadIntervalSec);
}

QString sanitizeFileToken(const QString &text, const QString &fallback)
{
    QString token = text.trimmed();
    token.replace(QRegularExpression(QStringLiteral(R"([\\/:*?"<>|\s]+)")), QStringLiteral("-"));
    token.replace(QRegularExpression(QStringLiteral("^-+|-+$")), QString());
    if (token.isEmpty()) {
        token = fallback;
    }
    return token.left(40);
}

QVector<quint8> uniqueSortedBehaviors(QVector<quint8> behaviors)
{
    std::sort(behaviors.begin(), behaviors.end());
    behaviors.erase(std::unique(behaviors.begin(), behaviors.end()), behaviors.end());
    return behaviors;
}

QString behaviorFileTag(const QVector<quint8> &behaviors)
{
    const QVector<quint8> unique = uniqueSortedBehaviors(behaviors);
    if (unique.isEmpty()) {
        return QStringLiteral("未标注");
    }
    QStringList parts;
    for (quint8 type : unique) {
        parts.append(sanitizeFileToken(BehaviorStore::behaviorText(type), QStringLiteral("未知")));
    }
    return parts.join(QLatin1Char('-'));
}

QString shipFolderName(const BehaviorRecord &record)
{
    QString name = sanitizeFileToken(record.ship_name, QStringLiteral("船"));
    const QString id_text = QString::number(record.ship_id);
    if (name == id_text) {
        name = QStringLiteral("船");
    }
    return QStringLiteral("%1_%2").arg(name).arg(record.ship_id);
}

QString cameraFileToken(const BehaviorRecord &record)
{
    QString name = record.camera_name.trimmed();
    const QString id_text = QString::number(record.camera_id);
    if (record.camera_id != 0 && (name == id_text || name.startsWith(id_text))) {
        name = name.mid(id_text.size()).trimmed();
    }
    const QString fallback = record.camera_id == 0 ? QStringLiteral("摄像头")
                                                   : QStringLiteral("摄像头%1").arg(record.camera_id);
    return sanitizeFileToken(name, fallback);
}

bool looksUnnamed(const BehaviorRecord &record)
{
    return record.ship_id == 0
        && (record.ship_name.isEmpty() || record.ship_name == QStringLiteral("未命名船")
            || record.ship_name == QStringLiteral("未分类"))
        && (record.camera_name.isEmpty() || record.camera_name == QStringLiteral("未命名摄像头")
            || record.camera_name == QStringLiteral("摄像头"));
}

QHash<QString, quint8> behaviorTagMap()
{
    QHash<QString, quint8> map;
    const quint8 types[] = {kBehaviorUnknown,
                            kBehaviorHelmet,
                            kBehaviorNoJacket,
                            kBehaviorPhone,
                            kBehaviorFire,
                            kBehaviorFight};
    for (quint8 type : types) {
        map.insert(sanitizeFileToken(BehaviorStore::behaviorText(type), QStringLiteral("未知")), type);
    }
    return map;
}

bool parseShipFolder(const QString &folder_name, quint32 *ship_id, QString *ship_name)
{
    const QRegularExpression match_re(QStringLiteral(R"(^(.+)_(\d+)$)"));
    const QRegularExpressionMatch match = match_re.match(folder_name);
    if (!match.hasMatch()) {
        return false;
    }
    *ship_name = match.captured(1);
    *ship_id = match.captured(2).toUInt();
    return true;
}

void parsePictureFileName(const QString &base_name, BehaviorRecord *record)
{
    if (record == nullptr) {
        return;
    }
    const QRegularExpression match_re(QStringLiteral(R"(^(.+)_(\d{8})_(\d{6})(.*)$)"));
    const QRegularExpressionMatch match = match_re.match(base_name);
    if (!match.hasMatch()) {
        return;
    }
    record->camera_name = match.captured(1);
    const QDate date = QDate::fromString(match.captured(2), QStringLiteral("yyyyMMdd"));
    const QTime time = QTime::fromString(match.captured(3), QStringLiteral("HHmmss"));
    if (date.isValid() && time.isValid()) {
        record->timestamp_ms = QDateTime(date, time, QTimeZone::systemTimeZone()).toUTC().toMSecsSinceEpoch();
    }
    QString tag = match.captured(4);
    if (tag.startsWith(QLatin1Char('_'))) {
        tag.remove(0, 1);
    }
    const QRegularExpression serial_re(QStringLiteral(R"(^(.+)_(\d+)$)"));
    const QRegularExpressionMatch serial = serial_re.match(tag);
    if (serial.hasMatch()) {
        tag = serial.captured(1);
    }
    if (tag.isEmpty()) {
        return;
    }
    const QHash<QString, quint8> tags = behaviorTagMap();
    QVector<quint8> behaviors;
    const QStringList parts = tag.split(QLatin1Char('-'), Qt::SkipEmptyParts);
    for (const QString &part : parts) {
        const auto found = tags.constFind(part);
        if (found != tags.cend()) {
            behaviors.append(found.value());
        }
    }
    if (!behaviors.isEmpty()) {
        record->behaviors = uniqueSortedBehaviors(behaviors);
    }
}

bool applyMockMetadata(BehaviorRecord *record, qint64 file_size)
{
    if (record == nullptr || file_size <= 0) {
        return false;
    }
    struct MockItem
    {
        qint64 size;
        quint32 ship_id;
        const char *ship_name;
        quint16 camera_id;
        const char *camera_name;
        const char *position;
        QVector<quint8> behaviors;
    };
    const MockItem items[] = {
        {5413330,
         1001,
         "演示船甲",
         1,
         "控制室",
         "集控室",
         QVector<quint8>{kBehaviorNoJacket, kBehaviorHelmet}},
        {133141,
         1001,
         "演示船甲",
         2,
         "驾驶室",
         "驾驶台",
         QVector<quint8>{kBehaviorHelmet}},
        {133038,
         1002,
         "演示船乙",
         1,
         "甲板",
         "左舷",
         QVector<quint8>{kBehaviorHelmet}},
        {263000,
         1002,
         "演示船乙",
         2,
         "货舱甲板",
         "货舱口",
         QVector<quint8>{kBehaviorFight}},
        {2243979,
         1001,
         "演示船甲",
         2,
         "驾驶室",
         "驾驶台",
         QVector<quint8>{kBehaviorNoJacket, kBehaviorHelmet}},
    };
    for (const MockItem &item : items) {
        if (item.size != file_size) {
            continue;
        }
        record->ship_id = item.ship_id;
        record->ship_name = QString::fromUtf8(item.ship_name);
        record->camera_id = item.camera_id;
        record->camera_name = QString::fromUtf8(item.camera_name);
        record->position = QString::fromUtf8(item.position);
        record->behaviors = item.behaviors;
        return true;
    }
    return false;
}

bool sameAbsolutePath(const QString &left, const QString &right)
{
    if (left.isEmpty() || right.isEmpty()) {
        return false;
    }
    return QFileInfo(left).absoluteFilePath() == QFileInfo(right).absoluteFilePath();
}

bool sameDemoRecord(const BehaviorRecord &left, const BehaviorRecord &right)
{
    return left.ship_id == right.ship_id && left.message_id == right.message_id
        && left.ship_name == right.ship_name && left.camera_id == right.camera_id
        && left.camera_name == right.camera_name && left.position == right.position
        && left.timestamp_ms == right.timestamp_ms && left.behaviors == right.behaviors
        && left.local_path == right.local_path && left.image_url == right.image_url;
}

QJsonObject demoRecordObject(quint32 ship_id,
                             const QString &ship_name,
                             quint32 message_id,
                             quint16 camera_id,
                             const QString &camera_name,
                             const QString &position,
                             qint64 timestamp_ms,
                             const QVector<int> &behaviors,
                             const QString &local_path)
{
    QJsonObject object;
    object.insert(QStringLiteral("ship_id"), static_cast<double>(ship_id));
    object.insert(QStringLiteral("ship_name"), ship_name);
    object.insert(QStringLiteral("message_id"), static_cast<double>(message_id));
    object.insert(QStringLiteral("camera_id"), camera_id);
    object.insert(QStringLiteral("camera_name"), camera_name);
    object.insert(QStringLiteral("position"), position);
    object.insert(QStringLiteral("timestamp_ms"), static_cast<double>(timestamp_ms));
    QJsonArray types;
    for (int type : behaviors) {
        types.append(type);
    }
    object.insert(QStringLiteral("behaviors"), types);
    object.insert(QStringLiteral("local_path"), local_path);
    return object;
}

QByteArray defaultDemoRecordsJson()
{
    QJsonArray records;
    records.append(demoRecordObject(10001,
                                    QStringLiteral("演示船甲"),
                                    1,
                                    1,
                                    QStringLiteral("控制室"),
                                    QStringLiteral("集控室"),
                                    1760160000000LL,
                                    {1, 2},
                                    QStringLiteral("picture/demo_a1.jpg")));
    records.append(demoRecordObject(10001,
                                    QStringLiteral("演示船甲"),
                                    2,
                                    2,
                                    QStringLiteral("驾驶室"),
                                    QStringLiteral("驾驶台"),
                                    1760160030000LL,
                                    {3},
                                    QStringLiteral("picture/demo_a2.jpg")));
    records.append(demoRecordObject(10002,
                                    QStringLiteral("演示船乙"),
                                    1,
                                    1,
                                    QStringLiteral("甲板"),
                                    QStringLiteral("左舷"),
                                    1760160060000LL,
                                    {5},
                                    QStringLiteral("D:/demo/fight.jpg")));
    records.append(demoRecordObject(10002,
                                    QStringLiteral("演示船乙"),
                                    2,
                                    2,
                                    QStringLiteral("货舱甲板"),
                                    QStringLiteral("货舱口"),
                                    1760160120000LL,
                                    {4},
                                    QStringLiteral("picture/demo_b2.jpg")));
    QJsonObject root;
    root.insert(QStringLiteral("_readme"),
                QStringLiteral("复制为本目录 records.json 后改字段即可演示。"
                               "local_path 可为绝对路径，或相对 picture/、data/、插件根目录。"
                               "behaviors：0 未知，1 安全帽，2 救生衣，3 玩手机，4 着火，5 打架。"
                               "同一船内 message_id 不要重复。缺图仍显示元数据。改完点刷新。"));
    root.insert(QStringLiteral("records"), records);
    return QJsonDocument(root).toJson(QJsonDocument::Indented);
}
}  // namespace

BehaviorStore::BehaviorStore(QObject *parent)
    : QObject(parent)
    , retain_days_(kDefaultRetainDays)
    , pump_cap_ms_(kDownloadIntervalMs)
    , download_timer_(new QTimer(this))
    , cleanup_timer_(new QTimer(this))
    , fetch_timeout_(new QTimer(this))
    , auto_pull_timer_(new QTimer(this))
{
    download_timer_->setInterval(kDownloadIntervalMs);
    cleanup_timer_->setInterval(static_cast<int>(kDayMs));
    fetch_timeout_->setSingleShot(true);
    fetch_timeout_->setInterval(kFetchTimeoutMs);
    auto_pull_timer_->setSingleShot(true);
    connect(download_timer_, &QTimer::timeout, this, [this]() {
        if (!fetching_) {
            fetchNext();
        }
        schedulePump();
    });
    connect(cleanup_timer_, &QTimer::timeout, this, [this]() {
        cleanupExpired();
    });
    connect(fetch_timeout_, &QTimer::timeout, this, [this]() {
        onFetchTimeout();
    });
    connect(auto_pull_timer_, &QTimer::timeout, this, [this]() {
        onAutoPull();
    });
}

BehaviorStore::~BehaviorStore()
{
    stop();
}

void BehaviorStore::setDataRoot(const QString &root)
{
    data_root_ = QFileInfo(root).absoluteFilePath();
}

void BehaviorStore::setCloudBlob(plugin_sdk::ICloudBlob *blob)
{
    cloud_blob_ = blob;
}

QString BehaviorStore::pluginRoot() const
{
    if (!data_root_.isEmpty()) {
        return data_root_;
    }
    return QCoreApplication::applicationDirPath();
}

QString BehaviorStore::defaultConfigDirectory() const
{
    return QDir(pluginRoot()).filePath(QStringLiteral("config"));
}

QString BehaviorStore::defaultDataDirectory() const
{
    return QDir(pluginRoot()).filePath(QStringLiteral("data"));
}

QString BehaviorStore::defaultPictureDirectory() const
{
    return QDir(pluginRoot()).filePath(QStringLiteral("picture"));
}

QString BehaviorStore::behaviorText(quint8 type)
{
    switch (type) {
    case kBehaviorUnknown:
        return QStringLiteral("未知");
    case kBehaviorHelmet:
        return QStringLiteral("安全帽");
    case kBehaviorNoJacket:
        return QStringLiteral("救生衣");
    case kBehaviorPhone:
        return QStringLiteral("玩手机");
    case kBehaviorFire:
        return QStringLiteral("着火");
    case kBehaviorFight:
        return QStringLiteral("打架");
    default:
        return QStringLiteral("类型 %1").arg(type);
    }
}

void BehaviorStore::start()
{
    if (started_) {
        return;
    }
    started_ = true;
    ensureDirectories();
    loadSettings();
    // 独立程序可缩短间隔，不写回设置文件
    pump_cap_ms_ = download_interval_sec_ * 1000;
    const QByteArray interval_env = qgetenv("SHORE_BEHAVIOR_DOWNLOAD_MS");
    if (!interval_env.isEmpty()) {
        const int interval_ms = interval_env.toInt();
        if (interval_ms >= 50) {
            pump_cap_ms_ = interval_ms;
        }
    }
    // 演示分支只读本地 JSON，不迁移、不删清单、不抽远程
    QFile::remove(QDir(defaultConfigDirectory()).filePath(QStringLiteral("index.json")));
    loadLocalDemoRecords();
    restartAutoPullClock();
}

void BehaviorStore::stop()
{
    download_timer_->stop();
    cleanup_timer_->stop();
    fetch_timeout_->stop();
    auto_pull_timer_->stop();
    next_auto_pull_due_ms_ = 0;
    started_ = false;
    clearInFlight();
    ++fetch_generation_;
    ++query_generation_;
    if (QCoreApplication::instance() != nullptr) {
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        QCoreApplication::processEvents();
    }
}

void BehaviorStore::ingestHostEvent(const plugin_sdk::BehaviorEvent &event, const QString &ship_name)
{
    Q_UNUSED(event);
    Q_UNUSED(ship_name);
}

void BehaviorStore::ingest(const BehaviorFrame &frame)
{
    Q_UNUSED(frame);
}

QVector<BehaviorRecord> BehaviorStore::records() const
{
    return records_;
}

void BehaviorStore::setCameraQuery(std::function<QVector<plugin_sdk::HostCamera>()> query)
{
    camera_query_ = std::move(query);
}

QVector<plugin_sdk::HostCamera> BehaviorStore::hostCameras() const
{
    if (!camera_query_) {
        return {};
    }
    return camera_query_();
}

int BehaviorStore::retainDays() const
{
    return retain_days_;
}

void BehaviorStore::setRetainDays(int days)
{
    retain_days_ = clampRetainDays(days);
    saveSettings();
    cleanupExpired();
}

QString BehaviorStore::customPicturePath() const
{
    return custom_picture_path_;
}

void BehaviorStore::setCustomPicturePath(const QString &path)
{
    custom_picture_path_ = path.trimmed();
    ensureDirectories();
    saveSettings();
}

int BehaviorStore::downloadIntervalSec() const
{
    return download_interval_sec_;
}

void BehaviorStore::setDownloadIntervalSec(int seconds)
{
    download_interval_sec_ = clampDownloadIntervalSec(seconds);
    pump_cap_ms_ = download_interval_sec_ * 1000;
    saveSettings();
    schedulePump();
    restartAutoPullClock();
}

bool BehaviorStore::autoRefresh() const
{
    return auto_refresh_;
}

void BehaviorStore::setAutoRefresh(bool enabled)
{
    if (auto_refresh_ == enabled) {
        restartAutoPullClock();
        return;
    }
    auto_refresh_ = enabled;
    saveSettings();
    restartAutoPullClock();
}

void BehaviorStore::setRemoteRefresh(std::function<void()> refresh)
{
    remote_refresh_ = std::move(refresh);
}

void BehaviorStore::setShipNameQuery(std::function<QString(quint32)> query)
{
    ship_name_query_ = std::move(query);
}

void BehaviorStore::setKnownShipsQuery(std::function<QVector<quint32>()> query)
{
    known_ships_query_ = std::move(query);
}

void BehaviorStore::setLog(std::function<void(const QString &)> info,
                           std::function<void(const QString &)> warning)
{
    log_info_ = std::move(info);
    log_warn_ = std::move(warning);
}

void BehaviorStore::logInfo(const QString &message) const
{
    if (log_info_) {
        log_info_(message);
    }
}

void BehaviorStore::logWarn(const QString &message) const
{
    if (log_warn_) {
        log_warn_(message);
    }
}

QVector<quint32> BehaviorStore::knownShipIds() const
{
    QVector<quint32> ships;
    for (const BehaviorRecord &record : records_) {
        if (record.ship_id != 0 && !ships.contains(record.ship_id)) {
            ships.append(record.ship_id);
        }
    }
    if (known_ships_query_) {
        for (const quint32 ship_id : known_ships_query_()) {
            if (ship_id != 0 && !ships.contains(ship_id)) {
                ships.append(ship_id);
            }
        }
    }
    for (const plugin_sdk::HostCamera &camera : hostCameras()) {
        if (camera.ship_id != 0 && !ships.contains(camera.ship_id)) {
            ships.append(camera.ship_id);
        }
    }
    return ships;
}

QString BehaviorStore::displayShipName(quint32 ship_id) const
{
    return shipDisplayName(ship_id, {});
}

void BehaviorStore::setQueryWindow(qint64 from_ms, qint64 to_ms, quint32 ship_id)
{
    window_from_ms_ = from_ms;
    window_to_ms_ = to_ms;
    window_ship_id_ = ship_id;
}

void BehaviorStore::setPageVisible(bool visible)
{
    page_visible_ = visible;
    if (!visible) {
        cancelPageFetches();
    }
}

void BehaviorStore::cancelPageFetches()
{
    ++fetch_generation_;
    pending_.clear();
    page_fetch_signature_.clear();
    clearInFlight();
}

QVector<quint32> BehaviorStore::queryShipIds() const
{
    QVector<quint32> ships;
    if (window_ship_id_ != 0) {
        ships.append(window_ship_id_);
        return ships;
    }
    ships = knownShipIds();
    if (ships.isEmpty()) {
        ships.append(0);
    }
    return ships;
}

QString BehaviorStore::shipDisplayName(quint32 ship_id, const QString &fallback) const
{
    if (ship_name_query_) {
        const QString name = ship_name_query_(ship_id);
        if (!name.isEmpty()) {
            return name;
        }
    }
    if (!fallback.isEmpty()) {
        return fallback;
    }
    for (const BehaviorRecord &record : records_) {
        if (record.ship_id == ship_id && !record.ship_name.trimmed().isEmpty()) {
            return record.ship_name;
        }
    }
    return QString::number(ship_id);
}

bool BehaviorStore::inCurrentWindow(const BehaviorRecord &record) const
{
    if (window_from_ms_ > 0 && record.timestamp_ms < window_from_ms_) {
        return false;
    }
    if (window_to_ms_ > 0 && record.timestamp_ms > window_to_ms_) {
        return false;
    }
    if (window_ship_id_ != 0 && record.ship_id != window_ship_id_) {
        return false;
    }
    return true;
}

BehaviorRecord BehaviorStore::recordFromInfo(const plugin_sdk::BehaviorEventInfo &info) const
{
    BehaviorRecord record;
    record.ship_id = info.ship_id;
    record.ship_name = shipDisplayName(info.ship_id, {});
    record.message_id = info.message_id;
    record.camera_id = info.camera_id;
    record.camera_name = info.camera_name;
    record.timestamp_ms = info.timestamp_ms;
    record.image_url = info.source_url;
    record.behaviors = info.behaviors;
    if (record.behaviors.isEmpty()) {
        record.behaviors = {kBehaviorUnknown};
    }
    attachCachedPath(&record);
    return record;
}

void BehaviorStore::attachCachedPath(BehaviorRecord *record) const
{
    if (record == nullptr || !record->local_path.isEmpty()) {
        return;
    }
    record->local_path = findCachedImage(*record);
}

QString BehaviorStore::findCachedImage(const BehaviorRecord &record) const
{
    const QStringList suffixes = {QStringLiteral(".jpg"),
                                  QStringLiteral(".jpeg"),
                                  QStringLiteral(".png"),
                                  QStringLiteral(".webp")};
    for (const QString &suffix : suffixes) {
        const QString path =
            QFileInfo(QDir(pictureDirectory()).filePath(imageRelativePath(record, suffix)))
                .absoluteFilePath();
        if (QFileInfo::exists(path)) {
            return path;
        }
    }
    return {};
}

void BehaviorStore::queryCurrentWindow()
{
    if (!started_) {
        return;
    }
    loadLocalDemoRecords();
}

void BehaviorStore::applyQueryResults(QVector<BehaviorRecord> events, int generation)
{
    if (generation != query_generation_) {
        return;
    }
    QHash<QString, int> live_keep;
    QVector<BehaviorRecord> merged;
    for (BehaviorRecord &event : events) {
        attachCachedPath(&event);
        const QString key = recordKey(event.ship_id, event.message_id);
        if (live_keep.contains(key)) {
            continue;
        }
        live_keep.insert(key, merged.size());
        merged.append(event);
    }
    for (const BehaviorRecord &live : records_) {
        const QString key = recordKey(live.ship_id, live.message_id);
        if (live_keep.contains(key) || !inCurrentWindow(live)) {
            continue;
        }
        live_keep.insert(key, merged.size());
        merged.append(live);
    }
    std::sort(merged.begin(), merged.end(), [](const BehaviorRecord &left, const BehaviorRecord &right) {
        return left.timestamp_ms > right.timestamp_ms;
    });
    QStringList previous_keys;
    previous_keys.reserve(records_.size());
    QHash<QString, QString> previous_paths;
    previous_paths.reserve(records_.size());
    for (const BehaviorRecord &item : records_) {
        const QString key = recordKey(item.ship_id, item.message_id);
        previous_keys.append(key);
        previous_paths.insert(key, item.local_path);
    }
    QStringList next_keys;
    next_keys.reserve(merged.size());
    for (const BehaviorRecord &item : merged) {
        next_keys.append(recordKey(item.ship_id, item.message_id));
    }
    records_ = std::move(merged);
    rebuildIndexByKey();
    if (previous_keys == next_keys) {
        for (const BehaviorRecord &item : records_) {
            const QString key = recordKey(item.ship_id, item.message_id);
            if (item.local_path != previous_paths.value(key) && !item.local_path.isEmpty()) {
                emit recordImageReady(item.ship_id, item.message_id);
            }
        }
        emit queryFinished();
        return;
    }
    logInfo(QStringLiteral("清单已更新 条数=%1").arg(records_.size()));
    emit recordsChanged();
    emit queryFinished();
}

void BehaviorStore::requestPageImages(const QVector<BehaviorRecord> &page)
{
    Q_UNUSED(page);
}

bool BehaviorStore::isFetching(quint32 ship_id, quint32 message_id) const
{
    const QString key = recordKey(ship_id, message_id);
    if (fetching_ && fetching_item_.key == key) {
        return true;
    }
    return pendingIndex(key) >= 0;
}

qint64 BehaviorStore::nextAutoPullDueMs() const
{
    if (!auto_refresh_ || !started_) {
        return 0;
    }
    return next_auto_pull_due_ms_;
}

void BehaviorStore::refreshNow()
{
    loadLocalDemoRecords();
    restartAutoPullClock();
}

void BehaviorStore::onAutoPull()
{
    if (!started_ || !auto_refresh_) {
        return;
    }
    refreshNow();
}

void BehaviorStore::restartAutoPullClock()
{
    if (!started_ || !auto_refresh_) {
        if (auto_pull_timer_ != nullptr) {
            auto_pull_timer_->stop();
        }
        next_auto_pull_due_ms_ = 0;
        emit pullScheduleChanged();
        return;
    }
    const int ms = pumpCapMs();
    next_auto_pull_due_ms_ = nowMs() + ms;
    auto_pull_timer_->start(ms);
    emit pullScheduleChanged();
}

QString BehaviorStore::pictureDirectory() const
{
    if (custom_picture_path_.isEmpty()) {
        return defaultPictureDirectory();
    }
    return custom_picture_path_;
}

QString BehaviorStore::recordKey(quint32 ship_id, quint32 message_id) const
{
    return QStringLiteral("%1_%2").arg(ship_id).arg(message_id);
}

QString BehaviorStore::settingsFilePath() const
{
    return QDir(defaultConfigDirectory()).filePath(QStringLiteral("settings.json"));
}

QString BehaviorStore::indexFilePath() const
{
    return QDir(defaultDataDirectory()).filePath(QStringLiteral("records.json"));
}

void BehaviorStore::ensureDirectories() const
{
    QDir().mkpath(defaultConfigDirectory());
    QDir().mkpath(defaultDataDirectory());
    QDir().mkpath(pictureDirectory());
}

void BehaviorStore::loadSettings()
{
    QFile file(settingsFilePath());
    if (!file.open(QIODevice::ReadOnly)) {
        return;
    }
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll());
    const QJsonObject root = document.object();
    retain_days_ = clampRetainDays(root.value(QStringLiteral("retain_days")).toInt(kDefaultRetainDays));
    custom_picture_path_ = root.value(QStringLiteral("picture_path")).toString();
    download_interval_sec_ = clampDownloadIntervalSec(
        root.value(QStringLiteral("download_interval_sec")).toInt(kDefaultDownloadIntervalSec));
    pump_cap_ms_ = download_interval_sec_ * 1000;
    auto_refresh_ = root.value(QStringLiteral("auto_refresh")).toBool(true);
}

void BehaviorStore::saveSettings() const
{
    ensureDirectories();
    QJsonObject root;
    root.insert(QStringLiteral("retain_days"), retain_days_);
    root.insert(QStringLiteral("picture_path"), custom_picture_path_);
    root.insert(QStringLiteral("download_interval_sec"), download_interval_sec_);
    root.insert(QStringLiteral("auto_refresh"), auto_refresh_);
    QFile file(settingsFilePath());
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return;
    }
    file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
}

void BehaviorStore::ensureDemoRecordsFile() const
{
    const QString path = indexFilePath();
    if (QFileInfo::exists(path)) {
        return;
    }
    QDir().mkpath(defaultDataDirectory());
    const QString example = QDir(defaultDataDirectory()).filePath(QStringLiteral("records.json.example"));
    if (QFileInfo::exists(example) && QFile::copy(example, path)) {
        logInfo(QStringLiteral("已从 records.json.example 写出演示清单"));
        return;
    }
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        logWarn(QStringLiteral("无法写出 records.json"));
        return;
    }
    file.write(defaultDemoRecordsJson());
    logInfo(QStringLiteral("已写出默认演示清单"));
}

QString BehaviorStore::resolveDemoLocalPath(const QString &raw) const
{
    const QString trimmed = QDir::fromNativeSeparators(raw.trimmed());
    if (trimmed.isEmpty()) {
        return {};
    }
    const QFileInfo given(trimmed);
    if (given.isAbsolute()) {
        return given.absoluteFilePath();
    }
    const QString from_picture =
        QFileInfo(QDir(pictureDirectory()).filePath(trimmed)).absoluteFilePath();
    if (QFileInfo::exists(from_picture)) {
        return from_picture;
    }
    const QString from_data =
        QFileInfo(QDir(defaultDataDirectory()).filePath(trimmed)).absoluteFilePath();
    if (QFileInfo::exists(from_data)) {
        return from_data;
    }
    const QString from_root = QFileInfo(QDir(pluginRoot()).filePath(trimmed)).absoluteFilePath();
    if (QFileInfo::exists(from_root)) {
        return from_root;
    }
    if (trimmed.startsWith(QLatin1String("picture/")) || trimmed.startsWith(QLatin1String("data/"))) {
        return from_root;
    }
    return from_picture;
}

void BehaviorStore::loadLocalDemoRecords()
{
    ensureDirectories();
    ensureDemoRecordsFile();

    QFile file(indexFilePath());
    if (!file.open(QIODevice::ReadOnly)) {
        logWarn(QStringLiteral("未找到 records.json"));
        if (!records_.isEmpty()) {
            records_.clear();
            rebuildIndexByKey();
            emit recordsChanged();
        }
        emit queryFinished();
        return;
    }
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll());
    file.close();
    if (!document.isObject()) {
        logWarn(QStringLiteral("records.json 无法解析，保留当前清单"));
        emit queryFinished();
        return;
    }

    QVector<BehaviorRecord> loaded;
    QSet<QString> seen;
    const QJsonArray array = document.object().value(QStringLiteral("records")).toArray();
    for (const QJsonValue &value : array) {
        const QJsonObject object = value.toObject();
        BehaviorRecord record;
        record.ship_id = static_cast<quint32>(object.value(QStringLiteral("ship_id")).toDouble());
        record.ship_name = object.value(QStringLiteral("ship_name")).toString();
        record.message_id = static_cast<quint32>(object.value(QStringLiteral("message_id")).toDouble());
        record.camera_id = static_cast<quint16>(object.value(QStringLiteral("camera_id")).toInt());
        record.camera_name = object.value(QStringLiteral("camera_name")).toString();
        record.position = object.value(QStringLiteral("position")).toString();
        record.timestamp_ms = static_cast<qint64>(object.value(QStringLiteral("timestamp_ms")).toDouble());
        record.image_url = object.value(QStringLiteral("image_url")).toString();
        const QJsonArray behaviors = object.value(QStringLiteral("behaviors")).toArray();
        for (const QJsonValue &item : behaviors) {
            record.behaviors.append(static_cast<quint8>(item.toInt()));
        }
        if (record.behaviors.isEmpty()) {
            record.behaviors = {kBehaviorUnknown};
        }
        record.local_path = resolveDemoLocalPath(object.value(QStringLiteral("local_path")).toString());
        if (record.ship_id == 0 || record.message_id == 0) {
            continue;
        }
        const QString key = recordKey(record.ship_id, record.message_id);
        if (seen.contains(key)) {
            logWarn(QStringLiteral("跳过重复记录 船=%1 消息=%2").arg(record.ship_id).arg(record.message_id));
            continue;
        }
        seen.insert(key);
        loaded.append(record);
    }
    std::sort(loaded.begin(), loaded.end(), [](const BehaviorRecord &left, const BehaviorRecord &right) {
        return left.timestamp_ms > right.timestamp_ms;
    });

    bool changed = loaded.size() != records_.size();
    if (!changed) {
        for (int index = 0; index < loaded.size(); ++index) {
            if (!sameDemoRecord(loaded.at(index), records_.at(index))) {
                changed = true;
                break;
            }
        }
    }
    if (!changed) {
        emit queryFinished();
        return;
    }
    records_ = std::move(loaded);
    rebuildIndexByKey();
    logInfo(QStringLiteral("已载入本地演示清单 条数=%1").arg(records_.size()));
    emit recordsChanged();
    emit queryFinished();
}

void BehaviorStore::migrateLegacyPictures()
{
    QFile file(indexFilePath());
    if (!file.open(QIODevice::ReadOnly)) {
        return;
    }
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll());
    file.close();
    const QJsonArray array = document.object().value(QStringLiteral("records")).toArray();
    for (const QJsonValue &value : array) {
        const QJsonObject object = value.toObject();
        BehaviorRecord record;
        record.ship_id = static_cast<quint32>(object.value(QStringLiteral("ship_id")).toDouble());
        record.message_id = static_cast<quint32>(object.value(QStringLiteral("message_id")).toDouble());
        record.timestamp_ms = static_cast<qint64>(object.value(QStringLiteral("timestamp_ms")).toDouble());
        record.image_url = object.value(QStringLiteral("image_url")).toString();
        const QString old_path = object.value(QStringLiteral("local_path")).toString();
        if (record.message_id == 0 || old_path.isEmpty() || !QFileInfo::exists(old_path)) {
            continue;
        }
        QString suffix = QFileInfo(old_path).suffix();
        if (suffix.isEmpty()) {
            suffix = fileSuffixForUrl(record.image_url);
        } else {
            suffix = QLatin1Char('.') + suffix.toLower();
        }
        const QString dest =
            QFileInfo(QDir(pictureDirectory()).filePath(imageRelativePath(record, suffix)))
                .absoluteFilePath();
        if (sameAbsolutePath(old_path, dest) || QFileInfo::exists(dest)) {
            continue;
        }
        QDir().mkpath(QFileInfo(dest).absolutePath());
        QFile::copy(old_path, dest);
    }
    QFile::remove(indexFilePath());
}

qint64 BehaviorStore::nowMs() const
{
    return QDateTime::currentMSecsSinceEpoch();
}

int BehaviorStore::pumpCapMs() const
{
    return qMax(kMinPumpMs, pump_cap_ms_);
}

int BehaviorStore::pendingIndex(const QString &key) const
{
    for (int index = 0; index < pending_.size(); ++index) {
        if (pending_.at(index).key == key) {
            return index;
        }
    }
    return -1;
}

int BehaviorStore::duePendingIndex() const
{
    const qint64 now = nowMs();
    for (int index = 0; index < pending_.size(); ++index) {
        if (pending_.at(index).next_at_ms <= now) {
            return index;
        }
    }
    return -1;
}

void BehaviorStore::enqueueIfNeeded(const QString &key, bool delay_first)
{
    if (key.isEmpty() || pendingIndex(key) >= 0) {
        return;
    }
    if (fetching_ && fetching_item_.key == key) {
        return;
    }
    const qint64 now = nowMs();
    PendingFetch item;
    item.key = key;
    item.first_at_ms = now;
    item.next_at_ms = delay_first ? now + kFirstFetchDelayMs : now;
    item.attempts = 0;
    pending_.append(item);
}

void BehaviorStore::clearInFlight()
{
    fetching_ = false;
    fetching_item_ = PendingFetch{};
    fetch_timeout_->stop();
}

void BehaviorStore::schedulePump()
{
    if (!started_ || download_timer_ == nullptr) {
        return;
    }
    int delay = pumpCapMs();
    if (!pending_.isEmpty()) {
        qint64 earliest = pending_.constFirst().next_at_ms;
        for (const PendingFetch &item : pending_) {
            earliest = qMin(earliest, item.next_at_ms);
        }
        delay = static_cast<int>(qBound(qint64(kMinPumpMs), earliest - nowMs(), qint64(pumpCapMs())));
    }
    download_timer_->setInterval(delay);
    if (!download_timer_->isActive()) {
        download_timer_->start();
    }
}

void BehaviorStore::requeueNotReady(PendingFetch item)
{
    ++item.attempts;
    const qint64 now = nowMs();
    const qint64 lived = now - item.first_at_ms;
    if (item.attempts >= kMaxFetchAttempts || lived >= kGiveUpMs) {
        return;
    }
    int backoff = kMinBackoffMs;
    for (int step = 1; step < item.attempts && backoff < kMaxBackoffMs; ++step) {
        backoff = qMin(kMaxBackoffMs, backoff * 2);
    }
    item.next_at_ms = now + backoff;
    pending_.append(item);
}

void BehaviorStore::onFetchTimeout()
{
    if (!fetching_) {
        return;
    }
    PendingFetch item = fetching_item_;
    ++fetch_generation_;
    clearInFlight();
    if (!started_) {
        return;
    }
    requeueNotReady(std::move(item));
    schedulePump();
    fetchNext();
}

void BehaviorStore::fetchNext()
{
    if (fetching_ || !started_ || cloud_blob_ == nullptr) {
        return;
    }
    const int due = duePendingIndex();
    if (due < 0) {
        schedulePump();
        return;
    }
    const PendingFetch item = pending_.takeAt(due);
    const int index = index_by_key_.value(item.key, -1);
    if (index < 0 || index >= records_.size()) {
        fetchNext();
        return;
    }
    BehaviorRecord &record = records_[index];
    if (!record.local_path.isEmpty() && QFileInfo::exists(record.local_path)) {
        fetchNext();
        return;
    }
    if (record.image_url.isEmpty() && record.message_id == 0) {
        requeueNotReady(item);
        schedulePump();
        fetchNext();
        return;
    }

    fetching_ = true;
    fetching_item_ = item;
    fetch_timeout_->start();
    const int generation = fetch_generation_;
    logInfo(QStringLiteral("开始取图 船=%1 消息=%2").arg(record.ship_id).arg(record.message_id));
    plugin_sdk::CloudBlobKey blob_key;
    blob_key.ship_id = record.ship_id;
    blob_key.message_id = record.message_id;
    blob_key.path = record.image_url;
    cloud_blob_->fetch(blob_key, [this, item, generation](plugin_sdk::CloudBlobResult result) {
        finishFetch(item, result, generation);
    });
}

void BehaviorStore::finishFetch(PendingFetch item,
                                const plugin_sdk::CloudBlobResult &result,
                                int generation)
{
    if (generation != fetch_generation_) {
        return;
    }
    clearInFlight();
    if (!started_) {
        return;
    }
    const int index = index_by_key_.value(item.key, -1);
    if (index < 0 || index >= records_.size()) {
        fetchNext();
        return;
    }

    if (result.status == plugin_sdk::CloudBlobStatus::NotReady
        || result.bytes.isEmpty()
        || result.status != plugin_sdk::CloudBlobStatus::Ok) {
        const BehaviorRecord &record = records_[index];
        if (result.status == plugin_sdk::CloudBlobStatus::Failed) {
            logWarn(QStringLiteral("取图失败 船=%1 消息=%2 %3")
                        .arg(record.ship_id)
                        .arg(record.message_id)
                        .arg(result.error));
            schedulePump();
            fetchNext();
            return;
        }
        logInfo(QStringLiteral("取图未就绪 船=%1 消息=%2 %3")
                    .arg(record.ship_id)
                    .arg(record.message_id)
                    .arg(result.error));
        requeueNotReady(std::move(item));
        schedulePump();
        fetchNext();
        return;
    }

    ensureDirectories();
    BehaviorRecord &record = records_[index];
    const QString suffix = fileSuffixForUrl(record.image_url);
    const QString path =
        QFileInfo(QDir(pictureDirectory()).filePath(imageRelativePath(record, suffix)))
            .absoluteFilePath();
    QDir().mkpath(QFileInfo(path).absolutePath());
    const QString temp_path = path + QStringLiteral(".tmp");
    QFile file(temp_path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        requeueNotReady(std::move(item));
        schedulePump();
        fetchNext();
        return;
    }
    file.write(result.bytes);
    file.close();
    QFile::remove(path);
    if (!QFile::rename(temp_path, path)) {
        QFile::remove(temp_path);
        requeueNotReady(std::move(item));
        schedulePump();
        fetchNext();
        return;
    }

    record.local_path = path;
    logInfo(QStringLiteral("取图完成 船=%1 消息=%2 字节=%3")
                .arg(record.ship_id)
                .arg(record.message_id)
                .arg(result.bytes.size()));
    emit recordImageReady(record.ship_id, record.message_id);
    fetchNext();
}

void BehaviorStore::cleanupExpired()
{
    // 演示分支保留 JSON 记录，不按保存天数裁剪
}

void BehaviorStore::rebuildIndexByKey()
{
    index_by_key_.clear();
    for (int index = 0; index < records_.size(); ++index) {
        const BehaviorRecord &item = records_.at(index);
        index_by_key_.insert(recordKey(item.ship_id, item.message_id), index);
    }
}

QString BehaviorStore::imageRelativePath(const BehaviorRecord &record, const QString &suffix) const
{
    const QDateTime local =
        QDateTime::fromMSecsSinceEpoch(record.timestamp_ms, QTimeZone::UTC).toLocalTime();
    const QString day = local.isValid() ? local.toString(QStringLiteral("yyyyMMdd"))
                                        : QStringLiteral("00000000");
    return QStringLiteral("%1/%2/%3%4")
        .arg(record.ship_id)
        .arg(day)
        .arg(record.message_id)
        .arg(suffix);
}

QString BehaviorStore::resolveImagePath(const BehaviorRecord &record,
                                        const QString &suffix,
                                        const QString &current_path) const
{
    const QString relative = QDir::fromNativeSeparators(imageRelativePath(record, suffix));
    const QFileInfo relative_info(relative);
    QString folder = relative_info.path();
    if (folder == QLatin1String(".")) {
        folder.clear();
    }
    const QString directory = folder.isEmpty()
        ? QFileInfo(pictureDirectory()).absoluteFilePath()
        : QDir(pictureDirectory()).filePath(folder);
    const QString base = relative_info.completeBaseName();
    QString name = relative_info.fileName();
    int serial = 2;
    while (true) {
        const QString path = QDir(directory).filePath(name);
        if (!QFileInfo::exists(path) || sameAbsolutePath(path, current_path)) {
            return QFileInfo(path).absoluteFilePath();
        }
        name = QStringLiteral("%1_%2%3").arg(base).arg(serial).arg(suffix);
        ++serial;
    }
}

void BehaviorStore::removeEmptyPictureDir(const QString &directory) const
{
    const QString root = QDir::cleanPath(QFileInfo(pictureDirectory()).absoluteFilePath());
    const QString current = QDir::cleanPath(QFileInfo(directory).absoluteFilePath());
    if (current.isEmpty() || current == root || !current.startsWith(root + QLatin1Char('/'))) {
        return;
    }
    QDir().rmdir(current);
}

bool BehaviorStore::classifyMockPictures()
{
    bool changed = false;
    for (BehaviorRecord &record : records_) {
        if (record.local_path.isEmpty() || !looksUnnamed(record)) {
            continue;
        }
        const QFileInfo info(record.local_path);
        if (!info.exists()) {
            continue;
        }
        const BehaviorRecord previous = record;
        if (!applyMockMetadata(&record, info.size())) {
            continue;
        }
        if (previous.ship_id != record.ship_id || previous.ship_name != record.ship_name
            || previous.camera_id != record.camera_id || previous.camera_name != record.camera_name
            || previous.position != record.position || previous.behaviors != record.behaviors) {
            changed = true;
        }
    }
    return changed;
}

bool BehaviorStore::relocatePicturesIntoShipFolders()
{
    const QString root = QFileInfo(pictureDirectory()).absoluteFilePath();
    bool changed = false;
    for (BehaviorRecord &record : records_) {
        if (record.local_path.isEmpty()) {
            continue;
        }
        const QFileInfo current(record.local_path);
        if (!current.exists()) {
            continue;
        }
        const QString suffix = QStringLiteral(".") + current.suffix().toLower();
        const QString dest = resolveImagePath(record, suffix, record.local_path);
        if (sameAbsolutePath(dest, record.local_path)) {
            continue;
        }
        QDir().mkpath(QFileInfo(dest).absolutePath());
        if (!QFile::rename(record.local_path, dest)) {
            continue;
        }
        const QString old_dir = current.absolutePath();
        record.local_path = dest;
        changed = true;
        if (old_dir != root) {
            removeEmptyPictureDir(old_dir);
        }
    }
    return changed;
}

bool BehaviorStore::importLoosePictures()
{
    QDir directory(pictureDirectory());
    if (!directory.exists()) {
        return false;
    }
    const QString root = QFileInfo(pictureDirectory()).absoluteFilePath();
    QSet<QString> known;
    for (const BehaviorRecord &record : records_) {
        if (!record.local_path.isEmpty()) {
            known.insert(QFileInfo(record.local_path).absoluteFilePath());
        }
    }
    quint32 next_message = 1;
    for (const BehaviorRecord &record : records_) {
        if (record.message_id >= next_message) {
            next_message = record.message_id + 1;
        }
    }
    const QStringList filters = {QStringLiteral("*.jpg"),
                                 QStringLiteral("*.jpeg"),
                                 QStringLiteral("*.png"),
                                 QStringLiteral("*.webp"),
                                 QStringLiteral("*.bmp")};
    QDirIterator iterator(root, filters, QDir::Files, QDirIterator::Subdirectories);
    bool changed = false;
    while (iterator.hasNext()) {
        iterator.next();
        const QFileInfo info = iterator.fileInfo();
        const QString absolute = info.absoluteFilePath();
        if (known.contains(absolute)) {
            continue;
        }
        BehaviorRecord record;
        record.timestamp_ms = info.lastModified().toUTC().toMSecsSinceEpoch();
        record.message_id = next_message++;
        record.local_path = absolute;
        const QString parent = QDir::cleanPath(info.absolutePath());
        if (parent != QDir::cleanPath(root)) {
            quint32 ship_id = 0;
            QString ship_name;
            if (parseShipFolder(QFileInfo(parent).fileName(), &ship_id, &ship_name)) {
                record.ship_id = ship_id;
                record.ship_name = ship_name;
            } else {
                record.ship_name = QFileInfo(parent).fileName();
            }
        }
        parsePictureFileName(info.completeBaseName(), &record);
        if (looksUnnamed(record)) {
            applyMockMetadata(&record, info.size());
        }
        if (looksUnnamed(record)) {
            record.ship_name = QStringLiteral("未分类");
            record.camera_name = QStringLiteral("摄像头");
            record.behaviors = {kBehaviorUnknown};
        }
        if (record.camera_name.isEmpty()) {
            record.camera_name = QStringLiteral("摄像头");
        }
        if (record.behaviors.isEmpty()) {
            record.behaviors = {kBehaviorUnknown};
        }
        records_.prepend(record);
        known.insert(absolute);
        changed = true;
    }
    return changed;
}

QString BehaviorStore::fileSuffixForUrl(const QString &url) const
{
    QString path = url;
    const QUrl parsed(url);
    if (parsed.isLocalFile()) {
        path = parsed.toLocalFile();
    } else if (parsed.scheme() == QLatin1String("http") || parsed.scheme() == QLatin1String("https")) {
        path = parsed.path();
    }
    const QString suffix = QFileInfo(path).suffix().toLower();
    if (suffix == QLatin1String("png") || suffix == QLatin1String("jpg")
        || suffix == QLatin1String("jpeg") || suffix == QLatin1String("webp")
        || suffix == QLatin1String("bmp")) {
        return QStringLiteral(".") + suffix;
    }
    return QStringLiteral(".jpg");
}
}  // namespace shore_station::behavior_page
