// Copyright 2026.
// License: All rights reserved.

#include "behavior_page_widget.h"

#include <functional>

#include <QAbstractItemView>
#include <QCalendarWidget>
#include <QColor>
#include <QComboBox>
#include <QCoreApplication>
#include <QDateTime>
#include <QTimeZone>
#include <QDateTimeEdit>
#include <QDir>
#include <QEvent>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPalette>
#include <QShowEvent>
#include <QTextCharFormat>
#include <QTimer>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QList>
#include <QMouseEvent>
#include <QPixmap>
#include <QPushButton>
#include <QResizeEvent>
#include <QScrollArea>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QSizePolicy>
#include <QSpacerItem>
#include <QStyle>
#include <QToolButton>
#include <QVariant>
#include <QWidget>
#include <QVBoxLayout>

#include "behavior_settings_dialog.h"

namespace shore_station::behavior_page
{
namespace
{
constexpr int kCardMinWidth = 280;
constexpr int kToolbarControlHeight = 34;
constexpr quint8 kAllBehavior = 255;

class ClickFrame final : public QFrame
{
public:
    explicit ClickFrame(QWidget *parent = nullptr)
        : QFrame(parent)
    {
        setCursor(Qt::PointingHandCursor);
    }

    std::function<void()> on_click;

protected:
    void mousePressEvent(QMouseEvent *event) override
    {
        QFrame::mousePressEvent(event);
        if (event->button() == Qt::LeftButton && on_click) {
            on_click();
        }
    }
};

QString joinBehaviors(const QVector<quint8> &behaviors)
{
    QStringList texts;
    for (quint8 type : behaviors) {
        texts.append(BehaviorStore::behaviorText(type));
    }
    return texts.join(QStringLiteral(" / "));
}

QDateTime fromUtcMs(qint64 timestamp_ms)
{
    return QDateTime::fromMSecsSinceEpoch(timestamp_ms, QTimeZone::UTC).toLocalTime();
}

QPixmap loadRecordImage(const BehaviorRecord &record)
{
    if (record.local_path.isEmpty()) {
        return QPixmap();
    }
    return QPixmap(record.local_path);
}

QString cameraCaption(const QString &name, quint16 camera_id)
{
    const QString trimmed = name.trimmed();
    if (!trimmed.isEmpty()) {
        return trimmed;
    }
    if (camera_id == 0) {
        return QStringLiteral("未命名摄像头");
    }
    return QStringLiteral("摄像头 %1").arg(camera_id);
}

// 主体 QSpinBox 样式会在日历弹出时把年份步进，显示完成后再拉回编辑框里的日期
class CalendarShowGuard final : public QObject
{
public:
    CalendarShowGuard(QDateTimeEdit *edit, QCalendarWidget *calendar)
        : QObject(calendar)
        , edit_(edit)
        , calendar_(calendar)
    {
        calendar_->installEventFilter(this);
    }

protected:
    bool eventFilter(QObject *watched, QEvent *event) override
    {
        if (watched == calendar_ && event->type() == QEvent::Show && edit_ != nullptr) {
            const QDate date = edit_->date();
            const auto restore = [this, date]() {
                if (edit_ == nullptr || calendar_ == nullptr) {
                    return;
                }
                if (edit_->date() != date) {
                    const QSignalBlocker blocker(edit_);
                    edit_->setDate(date);
                }
                if (calendar_->yearShown() != date.year() || calendar_->monthShown() != date.month()) {
                    calendar_->setCurrentPage(date.year(), date.month());
                }
            };
            QTimer::singleShot(0, this, restore);
            QTimer::singleShot(50, this, restore);
        }
        return QObject::eventFilter(watched, event);
    }

private:
    QDateTimeEdit *edit_;
    QCalendarWidget *calendar_;
};
}  // namespace

BehaviorPageWidget::BehaviorPageWidget(BehaviorStore &store, QWidget *parent)
    : QWidget(parent)
    , store_(store)
    , cmb_ship_(nullptr)
    , cmb_camera_(nullptr)
    , cmb_behavior_(nullptr)
    , dt_start_(nullptr)
    , dt_end_(nullptr)
    , pbn_live_(nullptr)
    , tbn_settings_(nullptr)
    , grid_scroll_(nullptr)
    , grid_host_(nullptr)
    , grid_layout_(nullptr)
    , lb_empty_(nullptr)
    , lightbox_(nullptr)
    , lb_lightbox_image_(nullptr)
    , lb_lightbox_time_(nullptr)
    , lb_lightbox_caption_(nullptr)
    , strip_host_(nullptr)
{
    setObjectName(QStringLiteral("BehaviorPage"));
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(14, 14, 18, 16);
    root->setSpacing(12);

    auto *toolbar = new QFrame(this);
    toolbar->setObjectName(QStringLiteral("behavior_toolbar"));
    auto *toolbar_layout = new QHBoxLayout(toolbar);
    toolbar_layout->setContentsMargins(14, 8, 14, 8);
    toolbar_layout->setSpacing(8);
    auto *lb_title = new QLabel(tr("行为识别"), toolbar);
    lb_title->setObjectName(QStringLiteral("lb_title"));
    toolbar_layout->addWidget(lb_title);
    toolbar_layout->addStretch(1);

    cmb_ship_ = new QComboBox(toolbar);
    cmb_ship_->setObjectName(QStringLiteral("cmb_ship"));
    cmb_camera_ = new QComboBox(toolbar);
    cmb_camera_->setObjectName(QStringLiteral("cmb_camera"));
    cmb_behavior_ = new QComboBox(toolbar);
    cmb_behavior_->setObjectName(QStringLiteral("cmb_behavior"));
    dt_start_ = new QDateTimeEdit(toolbar);
    dt_start_->setObjectName(QStringLiteral("dt_start"));
    dt_start_->setDisplayFormat(QStringLiteral("yyyy-MM-dd HH:mm:ss"));
    dt_start_->setCalendarPopup(true);
    dt_end_ = new QDateTimeEdit(toolbar);
    dt_end_->setObjectName(QStringLiteral("dt_end"));
    dt_end_->setDisplayFormat(QStringLiteral("yyyy-MM-dd HH:mm:ss"));
    dt_end_->setCalendarPopup(true);
    applyStyle();
    styleCalendar(dt_start_);
    styleCalendar(dt_end_);
    pbn_live_ = new QPushButton(tr("至今"), toolbar);
    pbn_live_->setObjectName(QStringLiteral("pbn_live"));
    pbn_live_->setCheckable(true);
    pbn_live_->setChecked(true);
    tbn_settings_ = new QToolButton(toolbar);
    tbn_settings_->setObjectName(QStringLiteral("tbn_settings"));
    tbn_settings_->setText(tr("设置"));
    tbn_settings_->setAutoRaise(true);
    const QList<QWidget *> toolbar_controls = {cmb_ship_,
                                               cmb_camera_,
                                               cmb_behavior_,
                                               dt_start_,
                                               dt_end_,
                                               pbn_live_,
                                               tbn_settings_};
    for (QWidget *control : toolbar_controls) {
        control->setMinimumHeight(kToolbarControlHeight);
        control->setMaximumHeight(kToolbarControlHeight);
        control->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    }

    toolbar_layout->addWidget(cmb_ship_);
    toolbar_layout->addWidget(cmb_camera_);
    toolbar_layout->addWidget(cmb_behavior_);
    toolbar_layout->addWidget(dt_start_);
    toolbar_layout->addWidget(dt_end_);
    toolbar_layout->addWidget(pbn_live_);
    toolbar_layout->addWidget(tbn_settings_);
    root->addWidget(toolbar);

    grid_scroll_ = new QScrollArea(this);
    grid_scroll_->setObjectName(QStringLiteral("behavior_grid_scroll"));
    grid_scroll_->setWidgetResizable(true);
    grid_scroll_->setFrameShape(QFrame::NoFrame);
    grid_scroll_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    grid_scroll_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    grid_host_ = new QWidget(grid_scroll_);
    grid_host_->setObjectName(QStringLiteral("behavior_grid_host"));
    grid_host_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    grid_layout_ = new QGridLayout(grid_host_);
    grid_layout_->setContentsMargins(0, 0, 0, 0);
    grid_layout_->setSpacing(14);
    grid_layout_->setAlignment(Qt::AlignTop);
    lb_empty_ = new QLabel(tr("无识别帧"), grid_host_);
    lb_empty_->setObjectName(QStringLiteral("lb_empty"));
    lb_empty_->setAlignment(Qt::AlignCenter);
    grid_layout_->addWidget(lb_empty_, 0, 0);
    grid_scroll_->setWidget(grid_host_);
    grid_scroll_->viewport()->installEventFilter(this);
    root->addWidget(grid_scroll_, 1);

    lightbox_ = new QWidget(this);
    lightbox_->setObjectName(QStringLiteral("behavior_lightbox"));
    auto *lightbox_root = new QVBoxLayout(lightbox_);
    lightbox_root->setContentsMargins(24, 24, 24, 16);
    lightbox_root->setSpacing(12);
    auto *pbn_close = new QPushButton(tr("关闭"), lightbox_);
    pbn_close->setObjectName(QStringLiteral("pbn_close"));
    pbn_close->setFixedHeight(32);
    lightbox_root->addWidget(pbn_close, 0, Qt::AlignRight);
    auto *stage = new QFrame(lightbox_);
    stage->setObjectName(QStringLiteral("behavior_lightbox_stage"));
    auto *stage_layout = new QVBoxLayout(stage);
    stage_layout->setContentsMargins(0, 0, 0, 0);
    lb_lightbox_image_ = new QLabel(stage);
    lb_lightbox_image_->setObjectName(QStringLiteral("lb_lightbox_image"));
    lb_lightbox_image_->setAlignment(Qt::AlignCenter);
    lb_lightbox_image_->setMinimumHeight(240);
    lb_lightbox_time_ = new QLabel(stage);
    lb_lightbox_time_->setObjectName(QStringLiteral("lb_lightbox_time"));
    lb_lightbox_time_->setAlignment(Qt::AlignCenter);
    lb_lightbox_caption_ = new QLabel(stage);
    lb_lightbox_caption_->setObjectName(QStringLiteral("lb_lightbox_caption"));
    lb_lightbox_caption_->setWordWrap(true);
    stage_layout->addWidget(lb_lightbox_image_, 1);
    stage_layout->addWidget(lb_lightbox_time_);
    stage_layout->addWidget(lb_lightbox_caption_);
    lightbox_root->addWidget(stage, 1);
    auto *strip_scroll = new QScrollArea(lightbox_);
    strip_scroll->setObjectName(QStringLiteral("behavior_lightbox_strip"));
    strip_scroll->setWidgetResizable(true);
    strip_scroll->setFixedHeight(90);
    strip_scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    strip_scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    strip_host_ = new QWidget(strip_scroll);
    auto *strip_layout = new QHBoxLayout(strip_host_);
    strip_layout->setContentsMargins(0, 0, 0, 0);
    strip_layout->setSpacing(8);
    strip_scroll->setWidget(strip_host_);
    lightbox_root->addWidget(strip_scroll);
    lightbox_->hide();

    const QDateTime now = QDateTime::currentDateTime();
    const QTimeZone zone = QTimeZone::systemTimeZone();
    dt_start_->setDateTime(QDateTime(now.date(), QTime(0, 0, 0), zone));
    dt_end_->setDateTime(now);
    dt_end_->setEnabled(false);

    connect(cmb_ship_, &QComboBox::currentIndexChanged, this, [this]() {
        fillCameras();
        renderGrid();
    });
    connect(cmb_camera_, &QComboBox::currentIndexChanged, this, [this]() { renderGrid(); });
    connect(cmb_behavior_, &QComboBox::currentIndexChanged, this, [this]() { renderGrid(); });
    connect(dt_start_, &QDateTimeEdit::dateTimeChanged, this, [this]() {
        start_touched_ = true;
        renderGrid();
    });
    connect(dt_end_, &QDateTimeEdit::dateTimeChanged, this, [this]() { renderGrid(); });
    connect(pbn_live_, &QPushButton::clicked, this, [this]() {
        live_end_ = pbn_live_->isChecked();
        dt_end_->setEnabled(!live_end_);
        if (live_end_) {
            syncEndTime();
        }
        renderGrid();
    });
    connect(tbn_settings_, &QToolButton::clicked, this, [this]() {
        BehaviorSettingsDialog dialog(store_, this);
        dialog.exec();
    });
    connect(pbn_close, &QPushButton::clicked, this, [this]() { closeLightbox(); });
    connect(&store_, &BehaviorStore::recordsChanged, this, [this]() {
        fillFilters();
        renderGrid();
        if (lightbox_->isVisible()) {
            renderLightbox();
        }
    });

    fillFilters();
    renderGrid();

    catalog_timer_ = new QTimer(this);
    catalog_timer_->setInterval(2000);
    connect(catalog_timer_, &QTimer::timeout, this, [this]() {
        if (isVisible()) {
            refreshHostCatalog();
        }
    });
    catalog_timer_->start();
}

void BehaviorPageWidget::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    lightbox_->setGeometry(rect());
    relayoutGrid();
}

void BehaviorPageWidget::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    refreshHostCatalog();
    relayoutGrid();
}

bool BehaviorPageWidget::eventFilter(QObject *watched, QEvent *event)
{
    if (grid_scroll_ != nullptr && watched == grid_scroll_->viewport()
        && event->type() == QEvent::Resize) {
        relayoutGrid();
    }
    return QWidget::eventFilter(watched, event);
}

void BehaviorPageWidget::setTheme(const QString &theme)
{
    dark_theme_ = theme == QLatin1String("dark");
    applyStyle();
    styleCalendar(dt_start_);
    styleCalendar(dt_end_);
}

void BehaviorPageWidget::applyStyle()
{
    const QString theme = dark_theme_ ? QStringLiteral("dark") : QStringLiteral("light");
    QFile style(QStringLiteral(":/behavior_page/style/%1.qss").arg(theme));
    if (!style.open(QIODevice::ReadOnly)) {
        return;
    }
    const QString sheet = QString::fromUtf8(style.readAll());
    setStyleSheet(sheet);
    const int calendar_at = sheet.indexOf(QStringLiteral("QCalendarWidget"));
    calendar_style_ = calendar_at >= 0 ? sheet.mid(calendar_at) : sheet;
}

void BehaviorPageWidget::styleCalendar(QDateTimeEdit *edit) const
{
    if (edit == nullptr || edit->calendarWidget() == nullptr) {
        return;
    }
    QCalendarWidget *calendar = edit->calendarWidget();
    calendar->setGridVisible(false);
    calendar->setVerticalHeaderFormat(QCalendarWidget::NoVerticalHeader);
    calendar->setFirstDayOfWeek(Qt::Monday);
    calendar->setAttribute(Qt::WA_StyledBackground, true);
    calendar->setStyleSheet(calendar_style_);

    const QColor window = dark_theme_ ? QColor(QStringLiteral("#0b1924")) : QColor(QStringLiteral("#ffffff"));
    const QColor text = dark_theme_ ? QColor(QStringLiteral("#e5f0f5")) : QColor(QStringLiteral("#1c2e36"));
    const QColor nav = dark_theme_ ? QColor(QStringLiteral("#102433")) : QColor(QStringLiteral("#f5f8fa"));
    const QColor accent(QStringLiteral("#1578a3"));
    QPalette palette = calendar->palette();
    palette.setColor(QPalette::Window, window);
    palette.setColor(QPalette::Base, window);
    palette.setColor(QPalette::AlternateBase, nav);
    palette.setColor(QPalette::Text, text);
    palette.setColor(QPalette::WindowText, text);
    palette.setColor(QPalette::Button, nav);
    palette.setColor(QPalette::ButtonText, text);
    palette.setColor(QPalette::Highlight, accent);
    palette.setColor(QPalette::HighlightedText, Qt::white);
    calendar->setPalette(palette);
    calendar->setAutoFillBackground(true);

    QTextCharFormat weekday;
    weekday.setForeground(text);
    QTextCharFormat weekend;
    weekend.setForeground(accent);
    calendar->setWeekdayTextFormat(Qt::Monday, weekday);
    calendar->setWeekdayTextFormat(Qt::Tuesday, weekday);
    calendar->setWeekdayTextFormat(Qt::Wednesday, weekday);
    calendar->setWeekdayTextFormat(Qt::Thursday, weekday);
    calendar->setWeekdayTextFormat(Qt::Friday, weekday);
    calendar->setWeekdayTextFormat(Qt::Saturday, weekend);
    calendar->setWeekdayTextFormat(Qt::Sunday, weekend);

    const QDate date = edit->date();
    calendar->setSelectedDate(date);
    calendar->setCurrentPage(date.year(), date.month());
    if (!calendar->property("shore_calendar_ready").toBool()) {
        calendar->setProperty("shore_calendar_ready", true);
        new CalendarShowGuard(edit, calendar);
    }
    const auto views = calendar->findChildren<QAbstractItemView *>();
    for (QAbstractItemView *view : views) {
        view->setAttribute(Qt::WA_StyledBackground, true);
        view->setPalette(palette);
        if (view->viewport() != nullptr) {
            view->viewport()->setAutoFillBackground(true);
            view->viewport()->setPalette(palette);
        }
    }
}

void BehaviorPageWidget::refreshHostCatalog()
{
    QStringList parts;
    for (const plugin_sdk::HostCamera &camera : store_.hostCameras()) {
        parts.append(QStringLiteral("%1:%2:%3").arg(camera.ship_id).arg(camera.camera_id).arg(camera.camera_name));
    }
    const QString signature = parts.join(QLatin1Char('|'));
    if (signature == host_camera_signature_) {
        return;
    }
    host_camera_signature_ = signature;
    fillFilters();
    renderGrid();
}

void BehaviorPageWidget::revealStoredPictures()
{
    if (range_initialized_ || start_touched_ || store_.records().isEmpty()) {
        return;
    }
    if (cmb_ship_->currentIndex() > 0 || cmb_camera_->currentIndex() > 0
        || cmb_behavior_->currentData().toInt() != static_cast<int>(kAllBehavior)) {
        return;
    }
    if (!visibleRecords().isEmpty()) {
        range_initialized_ = true;
        return;
    }
    qint64 earliest = 0;
    for (const BehaviorRecord &record : store_.records()) {
        if (record.timestamp_ms <= 0) {
            continue;
        }
        if (earliest == 0 || record.timestamp_ms < earliest) {
            earliest = record.timestamp_ms;
        }
    }
    range_initialized_ = true;
    if (earliest <= 0) {
        return;
    }
    const QTimeZone zone = QTimeZone::systemTimeZone();
    const QDateTime local = QDateTime::fromMSecsSinceEpoch(earliest, zone);
    const QDateTime start(local.date(), QTime(0, 0, 0), zone);
    if (dt_start_->dateTime() <= start) {
        return;
    }
    const QSignalBlocker blocker(dt_start_);
    dt_start_->setDateTime(start);
}

QString BehaviorPageWidget::recordKey(quint32 ship_id, quint32 message_id) const
{
    return QStringLiteral("%1_%2").arg(ship_id).arg(message_id);
}

QDateTime BehaviorPageWidget::rangeStart() const
{
    return dt_start_->dateTime();
}

QDateTime BehaviorPageWidget::rangeEnd() const
{
    if (live_end_) {
        return QDateTime::currentDateTime();
    }
    return dt_end_->dateTime();
}

void BehaviorPageWidget::syncEndTime()
{
    if (!live_end_) {
        return;
    }
    const QSignalBlocker blocker(dt_end_);
    dt_end_->setDateTime(QDateTime::currentDateTime());
}

QVector<BehaviorRecord> BehaviorPageWidget::visibleRecords() const
{
    const quint32 ship_id = cmb_ship_->currentData().toUInt();
    const QString camera_key = cmb_camera_->currentData().toString();
    const int behavior = cmb_behavior_->currentData().toInt();
    const qint64 start_ms = rangeStart().toUTC().toMSecsSinceEpoch();
    const qint64 end_ms = rangeEnd().toUTC().toMSecsSinceEpoch();
    QVector<BehaviorRecord> shown;
    for (const BehaviorRecord &record : store_.records()) {
        if (record.timestamp_ms < start_ms || record.timestamp_ms > end_ms) {
            continue;
        }
        if (cmb_ship_->currentIndex() > 0 && record.ship_id != ship_id) {
            continue;
        }
        if (!camera_key.isEmpty()
            && recordKey(record.ship_id, record.camera_id) != camera_key) {
            continue;
        }
        if (behavior != kAllBehavior) {
            bool hit = false;
            for (quint8 type : record.behaviors) {
                if (type == static_cast<quint8>(behavior)) {
                    hit = true;
                    break;
                }
            }
            if (!hit) {
                continue;
            }
        }
        shown.append(record);
    }
    return shown;
}

void BehaviorPageWidget::fillFilters()
{
    const quint32 previous_ship = cmb_ship_->currentData().toUInt();
    const bool had_behavior = cmb_behavior_->count() > 0;
    const int previous_behavior =
        had_behavior ? cmb_behavior_->currentData().toInt() : static_cast<int>(kAllBehavior);
    const QSignalBlocker ship_blocker(cmb_ship_);
    const QSignalBlocker behavior_blocker(cmb_behavior_);
    cmb_ship_->clear();
    cmb_ship_->addItem(tr("全部船舶"), 0);
    QVector<quint32> ships;
    QVector<quint8> extra_types;
    for (const plugin_sdk::HostCamera &camera : store_.hostCameras()) {
        if (camera.ship_id == 0 || ships.contains(camera.ship_id)) {
            continue;
        }
        ships.append(camera.ship_id);
        cmb_ship_->addItem(camera.ship_name.isEmpty() ? QString::number(camera.ship_id) : camera.ship_name,
                           camera.ship_id);
    }
    for (const BehaviorRecord &record : store_.records()) {
        if (record.ship_id != 0 && !ships.contains(record.ship_id)) {
            ships.append(record.ship_id);
            cmb_ship_->addItem(record.ship_name.isEmpty() ? QString::number(record.ship_id) : record.ship_name,
                               record.ship_id);
        }
        for (quint8 type : record.behaviors) {
            if (type > kBehaviorFight && !extra_types.contains(type)) {
                extra_types.append(type);
            }
        }
    }
    for (int index = 0; index < cmb_ship_->count(); ++index) {
        if (cmb_ship_->itemData(index).toUInt() == previous_ship) {
            cmb_ship_->setCurrentIndex(index);
            break;
        }
    }

    cmb_behavior_->clear();
    cmb_behavior_->addItem(tr("所有行为"), kAllBehavior);
    cmb_behavior_->addItem(BehaviorStore::behaviorText(kBehaviorUnknown), kBehaviorUnknown);
    cmb_behavior_->addItem(BehaviorStore::behaviorText(kBehaviorHelmet), kBehaviorHelmet);
    cmb_behavior_->addItem(BehaviorStore::behaviorText(kBehaviorNoJacket), kBehaviorNoJacket);
    cmb_behavior_->addItem(BehaviorStore::behaviorText(kBehaviorPhone), kBehaviorPhone);
    cmb_behavior_->addItem(BehaviorStore::behaviorText(kBehaviorFire), kBehaviorFire);
    cmb_behavior_->addItem(BehaviorStore::behaviorText(kBehaviorFight), kBehaviorFight);
    for (quint8 type : extra_types) {
        cmb_behavior_->addItem(BehaviorStore::behaviorText(type), type);
    }
    for (int index = 0; index < cmb_behavior_->count(); ++index) {
        if (cmb_behavior_->itemData(index).toInt() == previous_behavior) {
            cmb_behavior_->setCurrentIndex(index);
            break;
        }
    }
    fillCameras();
}

void BehaviorPageWidget::fillCameras()
{
    const QString previous = cmb_camera_->currentData().toString();
    const quint32 ship_id = cmb_ship_->currentData().toUInt();
    const QSignalBlocker blocker(cmb_camera_);
    cmb_camera_->clear();
    cmb_camera_->addItem(tr("全部摄像头"), QString());
    QStringList seen;
    for (const plugin_sdk::HostCamera &camera : store_.hostCameras()) {
        if (cmb_ship_->currentIndex() > 0 && camera.ship_id != ship_id) {
            continue;
        }
        const QString key = recordKey(camera.ship_id, camera.camera_id);
        if (seen.contains(key)) {
            continue;
        }
        seen.append(key);
        const QString caption = cameraCaption(camera.camera_name, camera.camera_id);
        const QString label = cmb_ship_->currentIndex() > 0
            ? caption
            : camera.ship_name + QStringLiteral(" / ") + caption;
        cmb_camera_->addItem(label, key);
    }
    for (const BehaviorRecord &record : store_.records()) {
        if (cmb_ship_->currentIndex() > 0 && record.ship_id != ship_id) {
            continue;
        }
        const QString key = recordKey(record.ship_id, record.camera_id);
        if (seen.contains(key)) {
            continue;
        }
        seen.append(key);
        const QString caption = cameraCaption(record.camera_name, record.camera_id);
        const QString label = cmb_ship_->currentIndex() > 0
            ? caption
            : record.ship_name + QStringLiteral(" / ") + caption;
        cmb_camera_->addItem(label, key);
    }
    for (int index = 0; index < cmb_camera_->count(); ++index) {
        if (cmb_camera_->itemData(index).toString() == previous) {
            cmb_camera_->setCurrentIndex(index);
            break;
        }
    }
}

void BehaviorPageWidget::renderGrid()
{
    if (live_end_) {
        syncEndTime();
    }
    revealStoredPictures();
    const QVector<BehaviorRecord> shown = visibleRecords();
    cards_.clear();
    while (QLayoutItem *item = grid_layout_->takeAt(0)) {
        if (item->widget() != nullptr && item->widget() != lb_empty_) {
            item->widget()->deleteLater();
        }
        delete item;
    }
    if (shown.isEmpty()) {
        lb_empty_->show();
        grid_layout_->addWidget(lb_empty_, 0, 0);
        return;
    }
    lb_empty_->hide();
    for (const BehaviorRecord &record : shown) {
        cards_.append(createCard(record));
    }
    last_grid_width_ = -1;
    last_columns_ = 0;
    relayoutGrid();
}

int BehaviorPageWidget::gridAvailableWidth() const
{
    int available = 0;
    if (grid_scroll_ != nullptr && grid_scroll_->viewport() != nullptr) {
        available = grid_scroll_->viewport()->width();
    }
    if (available <= 0 && grid_scroll_ != nullptr) {
        available = grid_scroll_->width();
    }
    if (available <= 0) {
        available = width();
        if (layout() != nullptr) {
            const QMargins margins = layout()->contentsMargins();
            available -= margins.left() + margins.right();
        }
    }
    return qMax(0, available);
}

void BehaviorPageWidget::applyCardShotSize(QFrame *card, int column_width) const
{
    if (card == nullptr || column_width <= 0) {
        return;
    }
    auto *shot = card->findChild<QLabel *>(QStringLiteral("behavior_card_shot"));
    if (shot == nullptr) {
        return;
    }
    const QPixmap source = shot->property("source").value<QPixmap>();
    if (source.isNull()) {
        return;
    }
    const int inner_width = qMax(1, column_width);
    const int inner_height = qMax(150, inner_width * 9 / 16);
    shot->setPixmap(
        source.scaled(QSize(inner_width, inner_height), Qt::KeepAspectRatio, Qt::SmoothTransformation));
    auto *badge = shot->findChild<QLabel *>(QStringLiteral("behavior_card_badge"));
    if (badge != nullptr) {
        badge->move(8, 8);
        badge->raise();
    }
}

void BehaviorPageWidget::relayoutGrid()
{
    if (cards_.isEmpty() || grid_layout_ == nullptr) {
        return;
    }
    const int available = gridAvailableWidth();
    if (available <= 0) {
        return;
    }
    const int spacing = grid_layout_->spacing();
    const int columns = qMax(1, (available + spacing) / (kCardMinWidth + spacing));
    if (available == last_grid_width_ && columns == last_columns_
        && grid_layout_->count() == cards_.size()) {
        return;
    }
    last_grid_width_ = available;
    last_columns_ = columns;

    while (QLayoutItem *item = grid_layout_->takeAt(0)) {
        delete item;
    }
    const int old_columns = grid_layout_->columnCount();
    for (int column = 0; column < qMax(old_columns, columns); ++column) {
        grid_layout_->setColumnStretch(column, column < columns ? 1 : 0);
        grid_layout_->setColumnMinimumWidth(column, 0);
    }
    const int column_width = qMax(1, (available - spacing * (columns - 1)) / columns);
    int index = 0;
    for (QFrame *card : cards_) {
        card->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
        applyCardShotSize(card, column_width);
        grid_layout_->addWidget(card, index / columns, index % columns);
        ++index;
    }
}

QFrame *BehaviorPageWidget::createCard(const BehaviorRecord &record)
{
    auto *card = new ClickFrame(grid_host_);
    card->setObjectName(QStringLiteral("behavior_card"));
    card->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    auto *layout = new QVBoxLayout(card);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    auto *shot = new QLabel(card);
    shot->setObjectName(QStringLiteral("behavior_card_shot"));
    shot->setMinimumHeight(150);
    shot->setAlignment(Qt::AlignCenter);
    shot->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    const QPixmap image = loadRecordImage(record);
    shot->setProperty("source", QVariant::fromValue(image));
    if (image.isNull()) {
        shot->setText(tr("下载中"));
    } else {
        shot->setPixmap(
            image.scaled(QSize(kCardMinWidth, 180), Qt::KeepAspectRatio, Qt::SmoothTransformation));
    }
    auto *badge = new QLabel(joinBehaviors(record.behaviors), shot);
    badge->setObjectName(QStringLiteral("behavior_card_badge"));
    badge->move(8, 8);
    badge->adjustSize();
    auto *meta = new QFrame(card);
    meta->setObjectName(QStringLiteral("behavior_card_meta"));
    auto *meta_layout = new QVBoxLayout(meta);
    meta_layout->setContentsMargins(10, 10, 12, 12);
    meta_layout->setSpacing(4);
    auto *lb_ship = new QLabel(record.ship_name, meta);
    lb_ship->setObjectName(QStringLiteral("lb_ship"));
    auto *lb_camera = new QLabel(record.camera_name, meta);
    lb_camera->setObjectName(QStringLiteral("lb_camera"));
    auto *lb_position = new QLabel(record.position, meta);
    lb_position->setObjectName(QStringLiteral("lb_position"));
    auto *lb_behavior = new QLabel(joinBehaviors(record.behaviors), meta);
    lb_behavior->setObjectName(QStringLiteral("lb_behavior"));
    auto *lb_time = new QLabel(fromUtcMs(record.timestamp_ms).toString(QStringLiteral("yyyy-MM-dd HH:mm:ss")),
                               meta);
    lb_time->setObjectName(QStringLiteral("lb_time"));
    meta_layout->addWidget(lb_ship);
    meta_layout->addWidget(lb_camera);
    meta_layout->addWidget(lb_position);
    meta_layout->addWidget(lb_behavior);
    meta_layout->addWidget(lb_time);
    layout->addWidget(shot);
    layout->addWidget(meta);
    const quint32 ship_id = record.ship_id;
    const quint32 message_id = record.message_id;
    card->on_click = [this, ship_id, message_id]() {
        openLightbox(ship_id, message_id);
    };
    return card;
}

void BehaviorPageWidget::openLightbox(quint32 ship_id, quint32 message_id)
{
    opened_ship_id_ = ship_id;
    opened_message_id_ = message_id;
    lightbox_->setGeometry(rect());
    lightbox_->show();
    lightbox_->raise();
    renderLightbox();
}

void BehaviorPageWidget::closeLightbox()
{
    lightbox_->hide();
    opened_ship_id_ = 0;
    opened_message_id_ = 0;
}

void BehaviorPageWidget::renderLightbox()
{
    const QVector<BehaviorRecord> shown = visibleRecords();
    const BehaviorRecord *current = nullptr;
    for (const BehaviorRecord &record : shown) {
        if (record.ship_id == opened_ship_id_ && record.message_id == opened_message_id_) {
            current = &record;
            break;
        }
    }
    if (current == nullptr) {
        closeLightbox();
        return;
    }

    const QPixmap image = loadRecordImage(*current);
    if (image.isNull()) {
        lb_lightbox_image_->setPixmap(QPixmap());
        lb_lightbox_image_->setText(tr("下载中"));
    } else {
        lb_lightbox_image_->setText(QString());
        lb_lightbox_image_->setPixmap(
            image.scaled(lb_lightbox_image_->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
    }
    lb_lightbox_time_->setText(
        fromUtcMs(current->timestamp_ms).toString(QStringLiteral("yyyy年MM月dd日 HH:mm:ss")));
    QStringList caption;
    caption << current->ship_name << current->camera_name << current->position
            << joinBehaviors(current->behaviors);
    caption.removeAll(QString());
    lb_lightbox_caption_->setText(caption.join(QStringLiteral("  ")));

    QLayout *strip_layout = strip_host_->layout();
    while (QLayoutItem *item = strip_layout->takeAt(0)) {
        if (item->widget() != nullptr) {
            item->widget()->deleteLater();
        }
        delete item;
    }
    for (const BehaviorRecord &record : shown) {
        auto *thumb = new ClickFrame(strip_host_);
        thumb->setObjectName(QStringLiteral("behavior_strip_card"));
        thumb->setFixedSize(132, 74);
        if (record.ship_id == opened_ship_id_ && record.message_id == opened_message_id_) {
            thumb->setProperty("on", true);
            thumb->style()->unpolish(thumb);
            thumb->style()->polish(thumb);
        }
        auto *thumb_layout = new QVBoxLayout(thumb);
        thumb_layout->setContentsMargins(0, 0, 0, 0);
        auto *image_label = new QLabel(thumb);
        image_label->setAlignment(Qt::AlignCenter);
        const QPixmap thumb_image = loadRecordImage(record);
        if (thumb_image.isNull()) {
            image_label->setText(tr("下载中"));
        } else {
            image_label->setPixmap(
                thumb_image.scaled(QSize(132, 74), Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation));
        }
        thumb_layout->addWidget(image_label);
        const quint32 ship_id = record.ship_id;
        const quint32 message_id = record.message_id;
        thumb->on_click = [this, ship_id, message_id]() {
            openLightbox(ship_id, message_id);
        };
        strip_layout->addWidget(thumb);
    }
    strip_layout->addItem(new QSpacerItem(0, 0, QSizePolicy::Expanding, QSizePolicy::Minimum));
}
}  // namespace shore_station::behavior_page
