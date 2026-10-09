// Copyright 2026.
// License: All rights reserved.

#include "behavior_store.h"

#include <algorithm>

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

QJsonArray behaviorsToJson(const QVector<quint8> &behaviors)
{
    QJsonArray array;
    for (quint8 type : behaviors) {
        array.append(static_cast<int>(type));
    }
    return array;
}

QVector<quint8> behaviorsFromJson(const QJsonArray &array)
{
    QVector<quint8> behaviors;
    behaviors.reserve(array.size());
    for (const QJsonValue &value : array) {
        behaviors.append(static_cast<quint8>(value.toInt()));
    }
    return behaviors;
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
}  // namespace

BehaviorStore::BehaviorStore(QObject *parent)
    : QObject(parent)
    , retain_days_(kDefaultRetainDays)
    , pump_cap_ms_(kDownloadIntervalMs)
    , download_timer_(new QTimer(this))
    , cleanup_timer_(new QTimer(this))
    , fetch_timeout_(new QTimer(this))
{
    download_timer_->setInterval(kDownloadIntervalMs);
    cleanup_timer_->setInterval(static_cast<int>(kDayMs));
    fetch_timeout_->setSingleShot(true);
    fetch_timeout_->setInterval(kFetchTimeoutMs);
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
    // 旧版本把报文记在配置目录，那不是设置，直接丢掉
    QFile::remove(QDir(defaultConfigDirectory()).filePath(QStringLiteral("index.json")));
    loadIndex();
    const bool imported = importLoosePictures();
    const bool classified = classifyMockPictures();
    const bool relocated = relocatePicturesIntoShipFolders();
    if (imported || classified || relocated) {
        rebuildIndexByKey();
        saveIndex();
        emit recordsChanged();
    }
    enqueuePending();
    cleanupExpired();
    download_timer_->start();
    cleanup_timer_->start();
    schedulePump();
    fetchNext();
}

void BehaviorStore::stop()
{
    download_timer_->stop();
    cleanup_timer_->stop();
    fetch_timeout_->stop();
    started_ = false;
    clearInFlight();
    ++fetch_generation_;
    if (QCoreApplication::instance() != nullptr) {
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        QCoreApplication::processEvents();
    }
}

void BehaviorStore::ingest(const BehaviorFrame &frame)
{
    const QString key = recordKey(frame.ship_id, frame.message_id);
    if (index_by_key_.contains(key)) {
        return;
    }

    BehaviorRecord record;
    record.ship_id = frame.ship_id;
    record.ship_name = frame.ship_name;
    record.message_id = frame.message_id;
    record.camera_id = frame.camera_id;
    record.camera_name = frame.camera_name;
    record.position = frame.position;
    record.timestamp_ms = frame.timestamp_ms;
    record.image_url = frame.image_url;
    record.behaviors = frame.behaviors;
    records_.prepend(record);
    index_by_key_.insert(key, 0);
    for (int index = 1; index < records_.size(); ++index) {
        const BehaviorRecord &item = records_.at(index);
        index_by_key_.insert(recordKey(item.ship_id, item.message_id), index);
    }
    enqueueIfNeeded(key, true);
    saveIndex();
    emit recordsChanged();
    if (started_) {
        schedulePump();
        fetchNext();
    }
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
}

void BehaviorStore::saveSettings() const
{
    ensureDirectories();
    QJsonObject root;
    root.insert(QStringLiteral("retain_days"), retain_days_);
    root.insert(QStringLiteral("picture_path"), custom_picture_path_);
    root.insert(QStringLiteral("download_interval_sec"), download_interval_sec_);
    QFile file(settingsFilePath());
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return;
    }
    file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
}

void BehaviorStore::loadIndex()
{
    records_.clear();
    index_by_key_.clear();
    QFile file(indexFilePath());
    if (!file.open(QIODevice::ReadOnly)) {
        return;
    }
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll());
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
        record.behaviors = behaviorsFromJson(object.value(QStringLiteral("behaviors")).toArray());
        record.local_path = object.value(QStringLiteral("local_path")).toString();
        records_.append(record);
        index_by_key_.insert(recordKey(record.ship_id, record.message_id), records_.size() - 1);
    }
}

void BehaviorStore::saveIndex() const
{
    ensureDirectories();
    QJsonArray array;
    for (const BehaviorRecord &record : records_) {
        QJsonObject object;
        object.insert(QStringLiteral("ship_id"), static_cast<double>(record.ship_id));
        object.insert(QStringLiteral("ship_name"), record.ship_name);
        object.insert(QStringLiteral("message_id"), static_cast<double>(record.message_id));
        object.insert(QStringLiteral("camera_id"), static_cast<int>(record.camera_id));
        object.insert(QStringLiteral("camera_name"), record.camera_name);
        object.insert(QStringLiteral("position"), record.position);
        object.insert(QStringLiteral("timestamp_ms"), static_cast<double>(record.timestamp_ms));
        object.insert(QStringLiteral("image_url"), record.image_url);
        object.insert(QStringLiteral("behaviors"), behaviorsToJson(record.behaviors));
        object.insert(QStringLiteral("local_path"), record.local_path);
        array.append(object);
    }
    QJsonObject root;
    root.insert(QStringLiteral("records"), array);
    QFile file(indexFilePath());
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return;
    }
    file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
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

void BehaviorStore::enqueuePending()
{
    pending_.clear();
    for (const BehaviorRecord &record : records_) {
        if (record.local_path.isEmpty() || !QFileInfo::exists(record.local_path)) {
            enqueueIfNeeded(recordKey(record.ship_id, record.message_id), false);
        }
    }
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
        if (result.status == plugin_sdk::CloudBlobStatus::Failed) {
            schedulePump();
            fetchNext();
            return;
        }
        requeueNotReady(std::move(item));
        schedulePump();
        fetchNext();
        return;
    }

    ensureDirectories();
    BehaviorRecord &record = records_[index];
    const QString suffix = fileSuffixForUrl(record.image_url);
    const QString path = resolveImagePath(record, suffix, record.local_path);
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
    saveIndex();
    emit recordsChanged();
    fetchNext();
}

void BehaviorStore::cleanupExpired()
{
    const qint64 expire_before =
        QDateTime::currentMSecsSinceEpoch() - static_cast<qint64>(retain_days_) * kDayMs;
    QVector<BehaviorRecord> kept;
    kept.reserve(records_.size());
    bool changed = false;
    for (const BehaviorRecord &record : records_) {
        if (record.timestamp_ms > 0 && record.timestamp_ms < expire_before) {
            if (!record.local_path.isEmpty()) {
                const QString directory = QFileInfo(record.local_path).absolutePath();
                QFile::remove(record.local_path);
                removeEmptyPictureDir(directory);
            }
            const QString key = recordKey(record.ship_id, record.message_id);
            const int pending_at = pendingIndex(key);
            if (pending_at >= 0) {
                pending_.removeAt(pending_at);
            }
            changed = true;
            continue;
        }
        kept.append(record);
    }
    if (!changed) {
        return;
    }
    records_ = kept;
    rebuildIndexByKey();
    saveIndex();
    emit recordsChanged();
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
    const QString stamp = local.isValid() ? local.toString(QStringLiteral("yyyyMMdd_HHmmss"))
                                           : QStringLiteral("00000000_000000");
    const QString file_name = QStringLiteral("%1_%2_%3%4")
                                  .arg(cameraFileToken(record), stamp, behaviorFileTag(record.behaviors), suffix);
    return QDir(shipFolderName(record)).filePath(file_name);
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
