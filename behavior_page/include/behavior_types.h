// Copyright 2026.
// License: All rights reserved.
//
// 行为识别载荷在插件内的值类型。不要放进 plugin_sdk。

#ifndef SHORE_STATION_BEHAVIOR_PAGE_BEHAVIOR_TYPES_H
#define SHORE_STATION_BEHAVIOR_PAGE_BEHAVIOR_TYPES_H

#include <QString>
#include <QVector>
#include <QtGlobal>

namespace shore_station::behavior_page
{
inline constexpr quint8 kBehaviorUnknown = 0x00;
inline constexpr quint8 kBehaviorHelmet = 0x01;
inline constexpr quint8 kBehaviorNoJacket = 0x02;
inline constexpr quint8 kBehaviorPhone = 0x03;
inline constexpr quint8 kBehaviorFire = 0x04;
inline constexpr quint8 kBehaviorFight = 0x05;

// 已废弃：产品路径是主体 0xCA00 + GetImage。保留仅避免旧调试数据编译失败
inline constexpr quint16 kHczkCmdBehaviorResult = 0x2B01;

// MAX_IMAGE_URL_LEN 协议未给出，按定长 256 解析
inline constexpr int kMaxImageUrlLen = 256;

struct BehaviorFrame
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
};
}  // namespace shore_station::behavior_page

#endif  // SHORE_STATION_BEHAVIOR_PAGE_BEHAVIOR_TYPES_H
