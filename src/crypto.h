#ifndef CRYPTO_H
#define CRYPTO_H

#include <QByteArray>
#include <QString>

/**
 * @brief 通用密码学工具
 *        - 随机数
 *        - SHA-256 / HMAC-SHA256
 *        - 恒定时间比较
 *        - bcrypt 转发接口
 *        - Base64URL 编解码
 */
namespace Crypto {

/// 生成 n 字节的密码学安全随机数
QByteArray randomBytes(int n);

/// 生成 nbytes 字节随机数的 Base64URL 字符串（用作 token）
QString randomToken(int nbytes = 32);

/// SHA-256 摘要
QByteArray sha256(const QByteArray &data);

/// SHA-256 摘要的十六进制字符串
QString sha256Hex(const QByteArray &data);

/// HMAC-SHA256
QByteArray hmacSha256(const QByteArray &key, const QByteArray &data);

/// 恒定时间比较，避免时序侧信道攻击
bool constantTimeEquals(const QByteArray &a, const QByteArray &b);

/* ---- bcrypt 转发接口（具体实现在 src/bcrypt.cpp） ---- */
QString bcryptHash(const QString &password, int cost = 12);
bool    bcryptVerify(const QString &password, const QString &stored);

/* ---- 演示用 SHA-256（生产环境不应使用） ---- */
QString sha256HashPassword(const QString &password, const QByteArray &salt = {});
bool    sha256VerifyPassword(const QString &password, const QString &stored);

/// Base64URL 编码（无填充）
QByteArray base64UrlEncode(const QByteArray &data);

/// Base64URL 解码
QByteArray base64UrlDecode(const QByteArray &data);

} // namespace Crypto

#endif // CRYPTO_H