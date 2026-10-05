#ifndef MODELS_H
#define MODELS_H

#include <QByteArray>
#include <QString>
#include <QStringList>

/**
 * @brief 用户信息模型
 *        对应 PostgreSQL 表 users
 */
struct UserInfo {
    QString     userId;         ///< 用户唯一 ID
    QString     username;       ///< 登录用户名
    QString     name;           ///< 显示名称
    QString     phone;          ///< 手机号
    QString     email;          ///< 邮箱
    QStringList roles;          ///< 角色列表
    QByteArray  passwordHash;   ///< bcrypt 密码哈希
};

/**
 * @brief OAuth2 客户端模型
 *        对应 PostgreSQL 表 clients
 */
struct ClientInfo {
    QString     clientId;       ///< 客户端 ID
    QString     clientSecret;   ///< 客户端密钥
    QStringList redirectUris;   ///< 允许的回调地址白名单
    QString     name;           ///< 客户端名称
};

/**
 * @brief OAuth2 授权码模型
 *        存储在 Redis 中，带 TTL 自动过期
 */
struct AuthCode {
    QString code;               ///< 授权码
    QString clientId;           ///< 关联的客户端 ID
    QString redirectUri;        ///< 关联的回调地址
    QString userId;             ///< 关联的用户 ID
    QString scope;              ///< 授权范围
    qint64  expiresAt = 0;      ///< 过期时间戳（Unix 秒）
};

/**
 * @brief 用户会话模型
 *        存储在 Redis 中，带 TTL 自动过期
 */
struct Session {
    QString sessionId;          ///< 会话 ID
    QString userId;             ///< 关联的用户 ID
    qint64  expiresAt = 0;      ///< 过期时间戳（Unix 秒）
};

#endif // MODELS_H