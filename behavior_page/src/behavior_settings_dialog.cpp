// Copyright 2026.
// License: All rights reserved.

#include "behavior_settings_dialog.h"

#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

#include "behavior_store.h"

namespace shore_station::behavior_page
{
BehaviorSettingsDialog::BehaviorSettingsDialog(BehaviorStore &store, QWidget *parent)
    : QDialog(parent)
    , store_(store)
    , spb_retain_days_(nullptr)
    , spb_download_interval_(nullptr)
    , le_picture_path_(nullptr)
{
    setWindowTitle(tr("行为识别"));
    setModal(true);

    auto *root = new QVBoxLayout(this);
    auto *form = new QFormLayout();
    spb_retain_days_ = new QSpinBox(this);
    spb_retain_days_->setObjectName(QStringLiteral("spb_retain_days"));
    spb_retain_days_->setRange(1, 365);
    spb_retain_days_->setValue(store_.retainDays());
    form->addRow(tr("保存天数"), spb_retain_days_);

    spb_download_interval_ = new QSpinBox(this);
    spb_download_interval_->setObjectName(QStringLiteral("spb_download_interval"));
    spb_download_interval_->setRange(1, 3600);
    spb_download_interval_->setSuffix(tr(" 秒"));
    spb_download_interval_->setValue(store_.downloadIntervalSec());
    form->addRow(tr("拉取间隔"), spb_download_interval_);

    auto *path_row = new QWidget(this);
    auto *path_layout = new QHBoxLayout(path_row);
    path_layout->setContentsMargins(0, 0, 0, 0);
    le_picture_path_ = new QLineEdit(path_row);
    le_picture_path_->setObjectName(QStringLiteral("le_picture_path"));
    le_picture_path_->setText(store_.customPicturePath());
    le_picture_path_->setPlaceholderText(store_.defaultPictureDirectory());
    auto *pbn_browse = new QPushButton(tr("浏览"), path_row);
    pbn_browse->setObjectName(QStringLiteral("pbn_browse"));
    path_layout->addWidget(le_picture_path_, 1);
    path_layout->addWidget(pbn_browse);
    form->addRow(tr("图片目录"), path_row);
    root->addLayout(form);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    root->addWidget(buttons);
    connect(pbn_browse, &QPushButton::clicked, this, [this]() {
        const QString selected = QFileDialog::getExistingDirectory(this,
                                                                   tr("图片目录"),
                                                                   le_picture_path_->text().isEmpty()
                                                                       ? store_.defaultPictureDirectory()
                                                                       : le_picture_path_->text());
        if (!selected.isEmpty()) {
            le_picture_path_->setText(QDir::toNativeSeparators(selected));
        }
    });
    connect(buttons, &QDialogButtonBox::accepted, this, [this]() {
        apply();
        accept();
    });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

void BehaviorSettingsDialog::apply()
{
    store_.setRetainDays(spb_retain_days_->value());
    store_.setDownloadIntervalSec(spb_download_interval_->value());
    store_.setCustomPicturePath(le_picture_path_->text());
}
}  // namespace shore_station::behavior_page
