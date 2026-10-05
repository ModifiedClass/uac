#ifndef BCRYPT_H
#define BCRYPT_H

#include <QByteArray>
#include <QString>

/**
 * @brief bcrypt 密码哈希 — 项目内自包含实现
 *
 * 算法：Blowfish + EksBlowfishSetup（昂贵的密钥调度）
 * 输出：$2b$<cost>$<22 字符 salt><31 字符 hash>（共 60 字节）
 *
 * 参考：Provos & Mazières, "A Future-Adaptable Password Scheme", USENIX 1999
 * 不依赖 Openwall crypt_blowfish 或任何外部加密库。
 */
namespace BCrypt {

/**
 * @brief 生成 bcrypt 哈希（自动生成随机盐）
 * @param password 明文密码（UTF-8，超过 72 字节将被截断）
 * @param cost     迭代成本 4-31，默认 12（即 2^12 = 4096 轮）
 * @return 60 字节的 bcrypt 串，失败返回空串
 */
QString hash(const QString &password, int cost = 12);

/**
 * @brief 校验密码
 * @param password 待校验的明文密码
 * @param stored   已存储的 bcrypt 哈希
 * @return 匹配返回 true
 */
bool verify(const QString &password, const QString &stored);

/**
 * @brief 使用指定盐生成哈希（用于测试或验证流程）
 * @param password 明文密码
 * @param salt16   16 字节盐值
 * @param cost     迭代成本
 * @return 60 字节的 bcrypt 串
 */
QString hashWithSalt(const QString &password, const QByteArray &salt16, int cost);

} // namespace BCrypt

#endif // BCRYPT_H