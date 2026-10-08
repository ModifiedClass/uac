#pragma once
// 通用小工具: 随机 hex 生成、常量时间比较(防时序侧信道)

#include <QByteArray>
#include <QRandomGenerator>

// 生成 byteCount 字节随机数的小写 hex 字符串(使用系统 CSPRNG),
// 用于 session_id / 授权码 / 盐等安全随机数。
inline QByteArray randomHex(int byteCount)
{
    QByteArray hex;
    hex.resize(byteCount * 2);
    auto *rng = QRandomGenerator::system();
    static const char digits[] = "0123456789abcdef";
    for (int i = 0; i < byteCount; ++i) {
        const unsigned char b = static_cast<unsigned char>(rng->bounded(256));
        hex[i * 2] = digits[b >> 4];
        hex[i * 2 + 1] = digits[b & 0x0f];
    }
    return hex;
}

// 常量时间比较, 用于密钥/签名/密码哈希比较, 避免时序侧信道攻击。
inline bool constantTimeEquals(const QByteArray &a, const QByteArray &b)
{
    if (a.size() != b.size())
        return false;
    unsigned char diff = 0;
    for (int i = 0; i < a.size(); ++i)
        diff |= static_cast<unsigned char>(a[i] ^ b[i]);
    return diff == 0;
}
