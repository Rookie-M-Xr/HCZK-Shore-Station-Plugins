// Copyright 2026.
// License: All rights reserved.
//
// 行为识别宫格页：筛选、卡片与点击放大。

#ifndef SHORE_STATION_BEHAVIOR_PAGE_BEHAVIOR_PAGE_WIDGET_H
#define SHORE_STATION_BEHAVIOR_PAGE_BEHAVIOR_PAGE_WIDGET_H

#include <QWidget>

#include "behavior_store.h"

class QCheckBox;
class QComboBox;
class QDateTimeEdit;
class QEvent;
class QFrame;
class QHideEvent;
class QShowEvent;
class QTimer;
class QGridLayout;
class QLabel;
class QLineEdit;
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
    void hideEvent(QHideEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    QVector<BehaviorRecord> visibleRecords() const;
    /***************************************************************
     * @brief 当前页要画的记录，最多 18 条，新的在前
     **************************************************************/
    QVector<BehaviorRecord> pageRecords() const;
    /***************************************************************
     * @brief 按筛选结果把页码限制在合法范围
     **************************************************************/
    void clampPage();
    void fillFilters();
    void fillCameras();
    void renderGrid();
    /***************************************************************
     * @brief 刷新页码条；有记录时始终占位，避免宫格被挤占
     **************************************************************/
    void updatePager();
    void goPrevPage();
    void goNextPage();
    /***************************************************************
     * @brief 按输入框页码跳转，超出范围时落到首尾页
     **************************************************************/
    void goJumpPage();
    /***************************************************************
     * @brief 按存储倒计时更新红色剩余秒数
     **************************************************************/
    void updateCountdown();
    /***************************************************************
     * @brief 勾选变化写回插件配置
     **************************************************************/
    void onAutoRefreshToggled(bool checked);
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
    /***************************************************************
     * @brief 只换这一张卡的图，不拆整页
     **************************************************************/
    void updateCardImage(quint32 ship_id, quint32 message_id);
    /***************************************************************
     * @brief 灯箱只换这一张图，不拆底部条
     **************************************************************/
    void updateLightboxImage(quint32 ship_id, quint32 message_id);
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
     * @brief 把工具栏时间窗与船舶写入存储
     **************************************************************/
    void applyQueryWindow();
    /***************************************************************
     * @brief 重查当前窗清单；刷新时同时抽空 Inbox
     **************************************************************/
    void reloadWindow(bool flush_inbox);

    BehaviorStore &store_;
    bool live_end_ = true;
    quint32 opened_ship_id_ = 0;
    quint32 opened_message_id_ = 0;
    QComboBox *cmb_ship_;
    QComboBox *cmb_camera_;
    QComboBox *cmb_behavior_;
    QPushButton *pbn_refresh_;
    QCheckBox *ckb_auto_refresh_;
    QLabel *lb_countdown_;
    QDateTimeEdit *dt_start_;
    QDateTimeEdit *dt_end_;
    QPushButton *pbn_live_;
    QToolButton *tbn_settings_;
    QScrollArea *grid_scroll_;
    QWidget *grid_host_;
    QGridLayout *grid_layout_;
    QLabel *lb_empty_;
    // 超过一页时显示
    QFrame *pager_;
    QPushButton *pbn_prev_;
    QLabel *lb_page_;
    QPushButton *pbn_next_;
    QLineEdit *le_page_;
    QPushButton *pbn_jump_;
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
    // 当前页，0 为最新一页
    int page_index_ = 0;
    // 刷新自动拉取倒计时显示
    QTimer *countdown_timer_ = nullptr;
    // 行为页首次可见后再查清单
    bool window_loaded_ = false;
};
}  // namespace shore_station::behavior_page

#endif  // SHORE_STATION_BEHAVIOR_PAGE_BEHAVIOR_PAGE_WIDGET_H
