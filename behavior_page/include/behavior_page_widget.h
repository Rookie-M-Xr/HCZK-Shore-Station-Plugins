// Copyright 2026.
// License: All rights reserved.
//
// 行为识别宫格页：筛选、卡片与点击放大。

#ifndef SHORE_STATION_BEHAVIOR_PAGE_BEHAVIOR_PAGE_WIDGET_H
#define SHORE_STATION_BEHAVIOR_PAGE_BEHAVIOR_PAGE_WIDGET_H

#include <QWidget>

#include "behavior_store.h"

class QComboBox;
class QDateTimeEdit;
class QEvent;
class QFrame;
class QShowEvent;
class QTimer;
class QGridLayout;
class QLabel;
class QPushButton;
class QScrollArea;
class QToolButton;

namespace shore_station::behavior_page
{
class BehaviorPageWidget final : public QWidget
{
    Q_OBJECT

public:
    /***************************************************************
     * @brief 构造宫格页面
     * @param store  本地存储，不拥有
     **************************************************************/
    explicit BehaviorPageWidget(BehaviorStore &store, QWidget *parent = nullptr);

    /***************************************************************
     * @brief 按主体通知的皮肤加载本插件样式
     **************************************************************/
    void setTheme(const QString &theme);

protected:
    void resizeEvent(QResizeEvent *event) override;
    void showEvent(QShowEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    QVector<BehaviorRecord> visibleRecords() const;
    void fillFilters();
    void fillCameras();
    void renderGrid();
    /***************************************************************
     * @brief 按滚动区实际宽度计算列数并拉伸卡片
     **************************************************************/
    void relayoutGrid();
    /***************************************************************
     * @brief 宫格可用宽度；视口尚未布局时回退到窗口宽度
     **************************************************************/
    int gridAvailableWidth() const;
    /***************************************************************
     * @brief 按列宽缩放卡片配图
     **************************************************************/
    void applyCardShotSize(QFrame *card, int column_width) const;
    QFrame *createCard(const BehaviorRecord &record);
    void openLightbox(quint32 ship_id, quint32 message_id);
    void closeLightbox();
    void renderLightbox();
    QString recordKey(quint32 ship_id, quint32 message_id) const;
    QDateTime rangeStart() const;
    QDateTime rangeEnd() const;
    void syncEndTime();
    void applyStyle();
    void styleCalendar(QDateTimeEdit *edit) const;
    /***************************************************************
     * @brief 主体摄像头目录变化时刷新船舶和摄像头筛选
     **************************************************************/
    void refreshHostCatalog();
    /***************************************************************
     * @brief 默认时间范围里没有记录时，把起点收到最早一张图
     **************************************************************/
    void revealStoredPictures();

    BehaviorStore &store_;
    bool live_end_ = true;
    quint32 opened_ship_id_ = 0;
    quint32 opened_message_id_ = 0;
    QComboBox *cmb_ship_;
    QComboBox *cmb_camera_;
    QComboBox *cmb_behavior_;
    QDateTimeEdit *dt_start_;
    QDateTimeEdit *dt_end_;
    QPushButton *pbn_live_;
    QToolButton *tbn_settings_;
    QScrollArea *grid_scroll_;
    QWidget *grid_host_;
    QGridLayout *grid_layout_;
    QLabel *lb_empty_;
    QWidget *lightbox_;
    QLabel *lb_lightbox_image_;
    QLabel *lb_lightbox_time_;
    QLabel *lb_lightbox_caption_;
    QWidget *strip_host_;
    QVector<QFrame *> cards_;
    // 上次参与列数计算的可用宽度，避免滚动条显隐来回抖动
    int last_grid_width_ = -1;
    // 上次排成的列数
    int last_columns_ = 0;
    // 定时查看主体摄像头目录是否变化
    QTimer *catalog_timer_ = nullptr;
    // 上次刷新时的摄像头目录签名
    QString host_camera_signature_;
    // 日历弹层使用的样式表
    QString calendar_style_;
    // 当前是否为深色主题，用于日历调色板
    bool dark_theme_ = false;
    // 用户改过起始时间后，不再自动放宽范围
    bool start_touched_ = false;
    // 已经按库存图片对齐过默认起点
    bool range_initialized_ = false;
};
}  // namespace shore_station::behavior_page

#endif  // SHORE_STATION_BEHAVIOR_PAGE_BEHAVIOR_PAGE_WIDGET_H
