// Copyright 2026.
// License: All rights reserved.
//
// 按定长 BehaviorResult 解析 HCZK 数据域。

#ifndef SHORE_STATION_BEHAVIOR_PAGE_BEHAVIOR_CODEC_H
#define SHORE_STATION_BEHAVIOR_PAGE_BEHAVIOR_CODEC_H

#include <QByteArray>
#include <QString>

#include "behavior_types.h"

namespace shore_station::behavior_page
{
/***************************************************************
 * @brief 按 1 字节对齐解析 BehaviorResult
 * @param payload  主体切好的数据域
 * @param out  解析结果，船号与船名由调用方再填
 **************************************************************/
bool parseBehaviorPayload(const QByteArray &payload, BehaviorFrame *out, QString *error);
}  // namespace shore_station::behavior_page

#endif  // SHORE_STATION_BEHAVIOR_PAGE_BEHAVIOR_CODEC_H
