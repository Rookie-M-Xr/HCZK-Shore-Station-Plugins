// Copyright 2026.
// License: All rights reserved.
//
// Hello 插件示例页：证明可选 DLL 可以在不重编主体的情况下加入页面栈。

#ifndef SHORE_STATION_HELLO_PAGE_HELLO_PAGE_WIDGET_H
#define SHORE_STATION_HELLO_PAGE_HELLO_PAGE_WIDGET_H

#include <QWidget>

namespace shore_station::hello_page
{
class HelloPageWidget final : public QWidget
{
    Q_OBJECT

public:
    explicit HelloPageWidget(QWidget *parent = nullptr);
};
}  // namespace shore_station::hello_page

#endif  // SHORE_STATION_HELLO_PAGE_HELLO_PAGE_WIDGET_H
