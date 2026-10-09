// Copyright 2026.
// License: All rights reserved.

#include "behavior_codec.h"

#include <cstring>

#include <QtEndian>

namespace shore_station::behavior_page
{
namespace
{
constexpr int kCameraNameBytes = 32;
constexpr int kPositionBytes = 16;
constexpr int kFixedPrefixBytes =
    4 + 2 + kCameraNameBytes + kPositionBytes + 8 + kMaxImageUrlLen + 1;

QString readFixedUtf8(const char *data, int size)
{
    int length = 0;
    while (length < size && data[length] != '\0') {
        ++length;
    }
    return QString::fromUtf8(data, length);
}

template <typename T>
T readLittle(const char *data)
{
    T value{};
    std::memcpy(&value, data, sizeof(T));
    return qFromLittleEndian(value);
}
}  // namespace

bool parseBehaviorPayload(const QByteArray &payload, BehaviorFrame *out, QString *error)
{
    if (out == nullptr) {
        return false;
    }
    if (payload.size() < kFixedPrefixBytes) {
        if (error != nullptr) {
            *error = QStringLiteral("行为识别载荷过短");
        }
        return false;
    }

    const char *data = payload.constData();
    int offset = 0;
    BehaviorFrame frame;
    frame.message_id = readLittle<quint32>(data + offset);
    offset += 4;
    frame.camera_id = readLittle<quint16>(data + offset);
    offset += 2;
    frame.camera_name = readFixedUtf8(data + offset, kCameraNameBytes);
    offset += kCameraNameBytes;
    frame.position = readFixedUtf8(data + offset, kPositionBytes);
    offset += kPositionBytes;
    frame.timestamp_ms = static_cast<qint64>(readLittle<quint64>(data + offset));
    offset += 8;
    frame.image_url = readFixedUtf8(data + offset, kMaxImageUrlLen);
    offset += kMaxImageUrlLen;
    const auto behavior_num = static_cast<quint8>(data[offset]);
    ++offset;
    if (payload.size() - offset < behavior_num) {
        if (error != nullptr) {
            *error = QStringLiteral("行为列表长度不足");
        }
        return false;
    }
    frame.behaviors.reserve(behavior_num);
    for (quint8 index = 0; index < behavior_num; ++index) {
        frame.behaviors.append(static_cast<quint8>(data[offset]));
        ++offset;
    }
    if (frame.behaviors.isEmpty()) {
        if (error != nullptr) {
            *error = QStringLiteral("没有行为类型");
        }
        return false;
    }
    *out = frame;
    return true;
}
}  // namespace shore_station::behavior_page
