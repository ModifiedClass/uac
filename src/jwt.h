#ifndef JWT_H
#define JWT_H

#include <QByteArray>
#include <QJsonObject>
#include <QString>

/**
 * @brief JWT 签发与验签（HS256）
 */
namespace Jwt {

/**
 * @brief 使用 HS256 签发 JWT
 * @param payload Payload JSON
 * @param secret  对称密钥
 * @return 三段式 JWT 字符串
 */
QString sign(const QJsonObject &payload, const QByteArray &secret);

/**
 * @brief 校验 JWT 签名与时间声明
 * @param token      待校验的 JWT
 * @param secret     对称密钥
 * @param payloadOut 输出 Payload（可选）
 * @param error      输出错误原因（可选）
 * @return 校验通过返回 true
 */
bool verify(const QString &token,
            const QByteArray &secret,
            QJsonObject *payloadOut = nullptr,
            QString *error = nullptr);

} // namespace Jwt

#endif // JWT_H