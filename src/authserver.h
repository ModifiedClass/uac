#ifndef AUTHSERVER_H
#define AUTHSERVER_H

#include "models.h"
#include "plugins.h"
#include "store.h"

#include <QByteArray>
#include <QHttpServer>
#include <QHttpServerRequest>
#include <QHttpServerResponse>
#include <QJsonObject>
#include <QObject>
#include <QStringList>

#include <memory>
#include <optional>

/**
 * @brief 认证中心配置（从 config.json 加载，环境变量可覆盖）
 */
struct AuthConfig {
    QString     host            = QStringLiteral("0.0.0.0");
    quint16     port            = 27149;
    QByteArray  jwtSecret;                          ///< 必须来自环境变量 JWT_SECRET
    QString     jwtIssuer       = QStringLiteral("uac");
    int         accessTokenTtl  = 3600;             ///< 访问令牌 TTL（秒）
    int         sessionTtl      = 86400;            ///< 会话 TTL（秒）
    int         authCodeTtl     = 300;              ///< 授权码 TTL（秒）
    QString     cookieName      = QStringLiteral("session_id");
    QString     templatesDir    = QStringLiteral("templates");
    QJsonObject raw;                                ///< 完整配置对象

    /// 从配置文件读取
    static AuthConfig fromFile(const QString &path);
};

/**
 * @brief 认证中心 HTTP 服务
 */
class AuthServer : public QObject
{
    Q_OBJECT
public:
    explicit AuthServer(const AuthConfig &cfg, QObject *parent = nullptr);

    /// 启动监听
    bool listen();

private:
    // ---- 路由处理器 ----
    QHttpServerResponse handleIndex(const QHttpServerRequest &req);
    QHttpServerResponse handleLoginGet(const QHttpServerRequest &req);
    QHttpServerResponse handleLoginPost(const QHttpServerRequest &req);
    QHttpServerResponse handleAuthorize(const QHttpServerRequest &req);
    QHttpServerResponse handleToken(const QHttpServerRequest &req);
    QHttpServerResponse handleUserinfo(const QHttpServerRequest &req);
    QHttpServerResponse handleLogout(const QHttpServerRequest &req);

    // ---- 辅助 ----
    std::optional<UserInfo> currentUser(const QHttpServerRequest &req);
    QString loadTemplate(const QString &name) const;
    QString renderLogin(const QString &error, const QString &redirect) const;

    AuthConfig m_cfg;
    QHttpServer m_server;

    std::unique_ptr<IUserStore>     m_users;
    std::unique_ptr<IClientStore>   m_clients;
    std::unique_ptr<IAuthCodeStore> m_codes;
    std::unique_ptr<ISessionStore>  m_sessions;
    std::unique_ptr<PluginManager>  m_plugins;
};

#endif // AUTHSERVER_H