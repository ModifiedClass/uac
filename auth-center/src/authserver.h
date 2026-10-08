#pragma once
// 路由与业务逻辑: 首页 / 登录 / 授权码 / 令牌 / 用户信息 / 登出。
// 模块划分: JWT(jwt.*) / 密码哈希(passwordhash.*) / 存储(storage.*) / 配置(config.*) / 插件(plugins.*)

#include <QByteArray>
#include <QHttpServer>
#include <QHttpServerRequest>
#include <QHttpServerResponse>
#include <QJsonObject>
#include <QObject>
#include <QTimer>
#include <memory>
#include <optional>
#include <vector>

#include "config.h"
#include "jwt.h"
#include "plugins.h"
#include "storage.h"

class AuthServer : public QObject
{
    Q_OBJECT
public:
    explicit AuthServer(QObject *parent = nullptr);

    // 装配存储/插件/模板并注册全部路由
    bool init(const AppConfig &cfg);
    // 监听(默认 127.0.0.1:27149, 仅本地回环)
    bool start();

private:
    QHttpServerResponse handleHome(const QHttpServerRequest &req);
    QHttpServerResponse handleLoginPage(const QHttpServerRequest &req);
    QHttpServerResponse handleLoginSubmit(const QHttpServerRequest &req);
    QHttpServerResponse handleAuthorize(const QHttpServerRequest &req);
    QHttpServerResponse handleToken(const QHttpServerRequest &req);
    QHttpServerResponse handleUserInfo(const QHttpServerRequest &req);
    QHttpServerResponse handleLogout(const QHttpServerRequest &req);

    // 插件链: 返回非空表示被拦截
    std::optional<QHttpServerResponse> runPlugins(const RequestContext &ctx);

    // 工具函数
    QString clientIp(const QHttpServerRequest &req) const;
    QByteArray cookieValue(const QHttpServerRequest &req, const QByteArray &name) const;
    std::optional<UserInfo> currentUser(const QHttpServerRequest &req) const;
    QByteArray renderLoginPage(const QString &error, const QString &username,
                               const QString &redirect) const;
    QByteArray renderHomePage(const std::optional<UserInfo> &user) const;

    QHttpServerResponse redirect(const QByteArray &location, const QByteArray &setCookie = {}) const;
    QHttpServerResponse json(const QJsonObject &obj, QHttpServerResponse::StatusCode status) const;
    QHttpServerResponse oauthError(const QString &errorCode, const QString &description,
                                   int httpStatus, bool basicAuth = false) const;

    AppConfig m_cfg;
    QHttpServer m_server;
    QTimer m_cleanupTimer;

    // 存储(接口注入, 生产替换为 PostgreSQL/Redis 实现)
    std::unique_ptr<UserStore> m_users;
    std::unique_ptr<ClientStore> m_clients;
    std::unique_ptr<AuthCodeStore> m_authCodes;
    std::unique_ptr<SessionStore> m_sessions;
    std::unique_ptr<JwtHandler> m_jwt;

    // 插件链(按 config.json 装配)
    std::vector<std::unique_ptr<AuthPlugin>> m_plugins;
    RateLimitPlugin *m_rateLimit = nullptr;
    BlacklistPlugin *m_blacklist = nullptr;

    QByteArray m_loginTemplate;
};
