// Copyright 2026.
// License: All rights reserved.
//
// 行为识别插件设置：保存天数、拉取间隔与图片目录。

#ifndef SHORE_STATION_BEHAVIOR_PAGE_BEHAVIOR_SETTINGS_DIALOG_H
#define SHORE_STATION_BEHAVIOR_PAGE_BEHAVIOR_SETTINGS_DIALOG_H

#include <QDialog>

class QLineEdit;
class QSpinBox;

namespace shore_station::behavior_page
{
class BehaviorStore;

class BehaviorSettingsDialog final : public QDialog
{
    Q_OBJECT

public:
    /***************************************************************
     * @brief 构造设置对话框
     * @param store  本地存储，不拥有
     **************************************************************/
    explicit BehaviorSettingsDialog(BehaviorStore &store, QWidget *parent = nullptr);

private:
    void apply();

    BehaviorStore &store_;
    QSpinBox *spb_retain_days_;
    QSpinBox *spb_download_interval_;
    QLineEdit *le_picture_path_;
};
}  // namespace shore_station::behavior_page

#endif  // SHORE_STATION_BEHAVIOR_PAGE_BEHAVIOR_SETTINGS_DIALOG_H
