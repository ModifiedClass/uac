#pragma once
// 配置加载: config.json + 环境变量覆盖。
//
// 环境变量(优先级最高):
//   UAC_CONFIG        配置文件路径(默认 ./config.json)
//   UAC_HOST          监听地址(默认 127.0.0.1)
//   UAC_PORT          监听端口(默认 27149)
//   JWT_SECRET        JWT HS256 签名密钥(★生产必须注入, 禁止硬编码★)
//   UAC_SESSION_TTL   会话有效期秒(默认 86400)
//   UAC_CODE_TTL      授权码有效期秒(默认 300)
//   UAC_TOKEN_TTL     访问令牌有效期秒(默认 3600)

#include <QByteArray>
#include <QJsonObject>
#include <QString>
#include <QStringList>

struct ConfigUser {
    QString userId;
    QString username;
    QString name;
    QString phone;
    QString email;
    QStringList roles;
    QByteArray passwordHash; // "sha256$盐$摘要" 或 bcrypt/argon2 格式
    QString passwordPlain;   // 首次引导字段(配置写明文时启动时哈希), ★生产禁止使用★
};

struct ConfigClient {
    QString clientId;
    QString clientSecret;
    QStringList redirectUris;
    QString name;
};

struct PluginConfig {
    QString name;
    bool enabled = true;
    QJsonObject config;
};

struct AppConfig {
    QString host = QStringLiteral("127.0.0.1");
    int port = 27149;
    QByteArray jwtSecret;
    bool jwtSecretFromEnv = false;
    qint64 sessionTtlSeconds = 86400;     // 会话 24 小时(可配置)
    qint64 authCodeTtlSeconds = 300;      // 授权码 5 分钟(可配置)
    qint64 accessTokenTtlSeconds = 3600;  // 访问令牌 1 小时(可配置)
    QString templateDir = QStringLiteral("templates");
    QList<PluginConfig> plugins;
    QList<ConfigUser> users;
    QList<ConfigClient> clients;
};

class AppConfigLoader
{
public:
    // 优先读取 path; 文件不存在时使用内置演示配置(输出警告)。
    // 之后应用环境变量覆盖, 并解析 JWT 密钥(环境变量 > 配置文件 > 演示默认值+告警)。
    static bool load(const QString &path, AppConfig *out, QString *error);
};
