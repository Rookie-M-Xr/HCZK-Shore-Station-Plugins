// Copyright 2026.
// License: All rights reserved.

#include "hello_page_widget.h"

#include <QFont>
#include <QFrame>
#include <QLabel>
#include <QVBoxLayout>

namespace shore_station::hello_page
{
HelloPageWidget::HelloPageWidget(QWidget *parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("hello_page_widget"));

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(32, 32, 32, 32);
    layout->setSpacing(12);

    auto *title = new QLabel(tr("Hello 插件"), this);
    title->setObjectName(QStringLiteral("hello_title_label"));
    QFont title_font = title->font();
    title_font.setPointSize(18);
    title_font.setBold(true);
    title->setFont(title_font);

    auto *body = new QLabel(this);
    body->setWordWrap(true);
    body->setText(tr("这是一个独立 DLL 页面。"));

    auto *card = new QFrame(this);
    card->setObjectName(QStringLiteral("hello_info_frame"));
    auto *card_layout = new QVBoxLayout(card);
    card_layout->addWidget(title);
    card_layout->addWidget(body);

    layout->addWidget(card);
    layout->addStretch(1);
}
}  // namespace shore_station::hello_page
