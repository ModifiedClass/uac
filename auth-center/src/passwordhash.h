#pragma once
// 密码哈希。
//
// ★ 重要安全说明:
//   示例实现使用 SHA-256 + 盐。SHA-256 是快速哈希, 不具备抗暴力破解的慢哈希特性,
//   ★★ 生产环境必须替换为 bcrypt 或 argon2 ★★(见 bcryptHash/bcryptVerify 与文档)。
//
// 示例存储格式: "sha256$<盐hex>$<摘要hex>", 摘要 = SHA256(盐hex字节 + ':' + 密码UTF-8)
// 生产存储格式: bcrypt 的 "$2a$..."/"$2b$..." 或 argon2 的 "$argon2id$..."

#include <QByteArray>
#include <QString>

namespace PasswordHasher {

// 生成随机盐(16 字节 -> 32 位 hex)
QByteArray randomSaltHex();

// 示例哈希: SHA-256 + 随机盐
QByteArray hashPassword(const QString &password);

// 示例校验: 解析存储格式并重新计算比较(常量时间)
bool verifyPassword(const QString &password, const QByteArray &storedHash);

// ---- 生产接口(★必须替换实现★) ----
// 生产环境应使用 bcrypt/argon2 库(如 libsodium、OpenBSD bcrypt、phc-winner-argon2)
// 实现以下两个函数, 并在 verifyPassword 中兼容校验 "sha256$"(示例) 与 "$2a$/$2b$/$argon2"(生产) 两种格式。
QByteArray bcryptHash(const QString &password);                     // 示例实现 = hashPassword
bool bcryptVerify(const QString &password, const QByteArray &hash); // 示例实现 = verifyPassword
bool isBcryptFormat(const QByteArray &storedHash);                  // 检测 "$2a$"/"$2b$"/"$argon2" 前缀

} // namespace PasswordHasher
