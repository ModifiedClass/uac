#include "authserver.h"
#include "crypto.h"
#include "jwt.h"
#include "store_pg.h"
#include "store_redis.h"

#include <QDateTime>
#include <QFile>
#include <QHttpHeaders>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLoggingCategory>
#include <QTcpServer>
#include <QUrl>
#include <QUrlQuery>

Q_LOGGING_CATEGORY(lcAuth, "uac.auth")

/* ============================================================
 *  小工具
 * ============================================================ */

/**
 * @brief 从请求头 Cookie 中解析键值对
 */
static QHash<QString, QString> parseCookies(const QHttpServerRequest &req)
{
    QHash<QString, QString> out;
    const QByteArray raw = req.value("Cookie");
    const QList<QByteArray> parts = raw.split(';');
    for (const QByteArray &p : parts) {
        const QByteArray kv = p.trimmed();
        const int eq = kv.indexOf('=');
        if (eq <= 0) continue;
        out.insert(QString::fromUtf8(kv.left(eq)).trimmed(),
                   QString::fromUtf8(kv.mid(eq + 1)).trimmed());
    }
    return out;
}

/**
 * @brief 构造 302 重定向响应
 *        Qt 6.8+ 使用 setHeaders() 批量设置响应头
 */
static QHttpServerResponse redirectResponse(const QString &location,
                                            const QByteArray &cookie = {})
{
    QHttpServerResponse resp(QHttpServerResponse::StatusCode::Found);

    QHttpHeaders headers;
    headers.append("Location", location.toUtf8());
    if (!cookie.isEmpty())
        headers.append("Set-Cookie", cookie);

    resp.setHeaders(std::move(headers));
    return resp;
}

/// 构造 HTML 响应
static QHttpServerResponse htmlResponse(const QString &html)
{
    return QHttpServerResponse("text/html; charset=utf-8", html.toUtf8());
}

/// 构造 JSON 响应
static QHttpServerResponse jsonResponse(const QJsonObject &obj)
{
    return QHttpServerResponse(QJsonDocument(obj).toJson(QJsonDocument::Compact),
                               "application/json");
}

/// OAuth2 错误响应
static QHttpServerResponse jsonError(const QString &error, const QString &desc = {})
{
    QJsonObject o{{"error", error}};
    if (!desc.isEmpty())
        o.insert("error_description", desc);
    return jsonResponse(o);
}

/**
 * @brief 校验 redirect 参数，只允许站内相对路径，防止开放重定向
 */
static QString sanitizeRedirect(const QString &raw)
{
    if (raw.isEmpty()) return QString();
    if (!raw.startsWith(QLatin1Char('/'))) return QString();
    if (raw.startsWith(QLatin1String("//"))) return QString();
    if (raw.contains(QLatin1String("\\"))) return QString();
    return raw;
}

/* ============================================================
 *  AuthConfig
 * ============================================================ */

AuthConfig AuthConfig::fromFile(const QString &path)
{
    AuthConfig c;

    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        qWarning(lcAuth) << "无法打开配置文件" << path << "，使用内置默认值";
        return c;
    }

    const QJsonObject root = QJsonDocument::fromJson(f.readAll()).object();
    c.raw = root;

    const QJsonObject s = root.value("server").toObject();
    c.host = s.value("host").toString(c.host);
    c.port = static_cast<quint16>(s.value("port").toInt(c.port));

    const QJsonObject j = root.value("jwt").toObject();
    c.jwtIssuer      = j.value("issuer").toString(c.jwtIssuer);
    c.accessTokenTtl = j.value("accessTokenTtlSeconds").toInt(c.accessTokenTtl);

    const QJsonObject se = root.value("session").toObject();
    c.sessionTtl = se.value("ttlSeconds").toInt(c.sessionTtl);
    c.cookieName = se.value("cookieName").toString(c.cookieName);

    const QJsonObject ac = root.value("authCode").toObject();
    c.authCodeTtl = ac.value("ttlSeconds").toInt(c.authCodeTtl);

    return c;
}

/* ============================================================
 *  AuthServer
 * ============================================================ */

AuthServer::AuthServer(const AuthConfig &cfg, QObject *parent)
    : QObject(parent), m_cfg(cfg)
{
    // 从环境变量读取连接串
    const QString dbUrl    = qEnvironmentVariable("DATABASE_URL");
    const QString redisUrl = qEnvironmentVariable("REDIS_URL", "redis://127.0.0.1:6379/0");

    if (dbUrl.isEmpty()) {
        qCritical(lcAuth) << "未设置 DATABASE_URL 环境变量";
        throw std::runtime_error("DATABASE_URL is required");
    }

    // 初始化 PostgreSQL 存储
    auto pgUsers   = std::make_unique<PgUserStore>(dbUrl);
    auto pgClients = std::make_unique<PgClientStore>(dbUrl);

    // 首次启动时从 config.json 导入（幂等）
    pgUsers->importFromConfig(m_cfg.raw.value("users").toArray());
    pgClients->importFromConfig(m_cfg.raw.value("clients").toArray());

    m_users   = std::move(pgUsers);
    m_clients = std::move(pgClients);

    // 初始化 Redis 存储
    m_codes    = std::make_unique<RedisAuthCodeStore>(redisUrl);
    m_sessions = std::make_unique<RedisSessionStore>(redisUrl);

    // 加载插件
    m_plugins = std::make_unique<PluginManager>();
    m_plugins->loadFromConfig(m_cfg.raw.value("plugins").toObject());
    qInfo(lcAuth) << "已加载插件:" << m_plugins->names();
}

bool AuthServer::listen()
{
    // ============ 路由注册 ============
    m_server.route("/", QHttpServerRequest::Method::Get,
                   [this](const QHttpServerRequest &r) { return handleIndex(r); });
    m_server.route("/login", QHttpServerRequest::Method::Get,
                   [this](const QHttpServerRequest &r) { return handleLoginGet(r); });
    m_server.route("/login", QHttpServerRequest::Method::Post,
                   [this](const QHttpServerRequest &r) { return handleLoginPost(r); });
    m_server.route("/authorize", QHttpServerRequest::Method::Get,
                   [this](const QHttpServerRequest &r) { return handleAuthorize(r); });
    m_server.route("/token", QHttpServerRequest::Method::Post,
                   [this](const QHttpServerRequest &r) { return handleToken(r); });
    m_server.route("/userinfo", QHttpServerRequest::Method::Get,
                   [this](const QHttpServerRequest &r) { return handleUserinfo(r); });
    m_server.route("/logout", QHttpServerRequest::Method::Get,
                   [this](const QHttpServerRequest &r) { return handleLogout(r); });

    // ============ 监听（Qt 6.8+ 使用 bind(QTcpServer*)）============
    auto tcpServer = std::make_unique<QTcpServer>();
    if (!tcpServer->listen(QHostAddress(m_cfg.host), m_cfg.port)) {
        qCritical(lcAuth) << "TCP 监听失败:" << tcpServer->errorString();
        return false;
    }
    if (!m_server.bind(tcpServer.get())) {
        qCritical(lcAuth) << "HTTP 服务器绑定失败";
        return false;
    }
    qInfo(lcAuth) << "统一认证中心已启动: http://"
                  << m_cfg.host << ":" << tcpServer->serverPort();
    tcpServer.release();  // 所有权交给 QHttpServer
    return true;
}

/* ---------------- 会话辅助 ---------------- */

std::optional<UserInfo> AuthServer::currentUser(const QHttpServerRequest &req)
{
    const QString sid = parseCookies(req).value(m_cfg.cookieName);
    if (sid.isEmpty())
        return std::nullopt;

    auto s = m_sessions->get(sid);
    if (!s)
        return std::nullopt;

    return m_users->findById(s->userId);
}

QString AuthServer::loadTemplate(const QString &name) const
{
    QFile f(m_cfg.templatesDir + QLatin1Char('/') + name);
    if (!f.open(QIODevice::ReadOnly))
        return QString();
    return QString::fromUtf8(f.readAll());
}

QString AuthServer::renderLogin(const QString &error, const QString &redirect) const
{
    QString tpl = loadTemplate(QStringLiteral("login.html"));
    if (tpl.isEmpty()) {
        // 兜底模板
        tpl = QStringLiteral(
            "<!doctype html><html><meta charset=\"utf-8\">"
            "<title>统一认证中心</title><body>"
            "{{ERROR_BLOCK}}"
            "<form method=\"post\" action=\"/login\">"
            "<input type=\"hidden\" name=\"redirect\" value=\"{{REDIRECT}}\">"
            "<p><input name=\"username\" placeholder=\"用户名\" required></p>"
            "<p><input name=\"password\" type=\"password\" placeholder=\"密码\" required></p>"
            "<p><button type=\"submit\">登录</button></p>"
            "</form></body></html>");
    }

    const QString errBlock = error.isEmpty()
                                 ? QString()
                                 : QStringLiteral("<div class=\"error\">%1</div>").arg(error.toHtmlEscaped());

    tpl.replace(QLatin1String("{{ERROR_BLOCK}}"), errBlock);
    tpl.replace(QLatin1String("{{REDIRECT}}"), redirect.toHtmlEscaped());
    return tpl;
}

/* ---------------- GET / ---------------- */

QHttpServerResponse AuthServer::handleIndex(const QHttpServerRequest &req)
{
    const auto user = currentUser(req);

    QString body;
    if (user) {
        body = QStringLiteral(
                   "<!doctype html><html lang=\"zh-CN\"><meta charset=\"utf-8\">"
                   "<title>统一认证中心</title>"
                   "<body style=\"font-family:sans-serif;max-width:640px;margin:60px auto\">"
                   "<h2>统一认证中心</h2>"
                   "<p>已登录：<b>%1</b>（%2）</p>"
                   "<p>角色：%3</p>"
                   "<p>邮箱：%4　电话：%5</p>"
                   "<p><a href=\"/logout\">退出登录</a></p>"
                   "</body></html>")
                   .arg(user->name.toHtmlEscaped(),
                        user->username.toHtmlEscaped(),
                        user->roles.join(QStringLiteral(", ")).toHtmlEscaped(),
                        user->email.toHtmlEscaped(),
                        user->phone.toHtmlEscaped());
    } else {
        body = QStringLiteral(
            "<!doctype html><html lang=\"zh-CN\"><meta charset=\"utf-8\">"
            "<title>统一认证中心</title>"
            "<body style=\"font-family:sans-serif;max-width:640px;margin:60px auto\">"
            "<h2>统一认证中心</h2><p>当前未登录</p>"
            "<p><a href=\"/login\">前往登录</a></p>"
            "</body></html>");
    }
    return htmlResponse(body);
}

/* ---------------- GET /login ---------------- */

QHttpServerResponse AuthServer::handleLoginGet(const QHttpServerRequest &req)
{
    const QUrlQuery q(req.url());
    const QString redirect = sanitizeRedirect(
        q.queryItemValue(QStringLiteral("redirect"), QUrl::FullyDecoded));
    return htmlResponse(renderLogin(QString(), redirect));
}

/* ---------------- POST /login ---------------- */

QHttpServerResponse AuthServer::handleLoginPost(const QHttpServerRequest &req)
{
    const QUrlQuery form(QString::fromUtf8(req.body()));
    const QString username = form.queryItemValue(QStringLiteral("username"), QUrl::FullyDecoded).trimmed();
    const QString password = form.queryItemValue(QStringLiteral("password"), QUrl::FullyDecoded);
    const QString redirect = sanitizeRedirect(
        form.queryItemValue(QStringLiteral("redirect"), QUrl::FullyDecoded));
    const QString ip = req.remoteAddress().toString();

    qInfo(lcAuth) << "登录尝试 user=" << username << "ip=" << ip;

    // 插件前置检查
    QString reason;
    if (!m_plugins->beforeLogin(username, ip, &reason)) {
        qWarning(lcAuth) << "登录被拦截 user=" << username << "ip=" << ip << "reason=" << reason;
        m_plugins->afterLogin(username, ip, false);
        return htmlResponse(renderLogin(reason, redirect));
    }

    // 校验凭据
    const auto user = m_users->findByUsername(username);
    const bool ok = user.has_value()
                    && Crypto::bcryptVerify(password, QString::fromUtf8(user->passwordHash));

    m_plugins->afterLogin(username, ip, ok);

    if (!ok) {
        qWarning(lcAuth) << "登录失败 user=" << username << "ip=" << ip;
        return htmlResponse(renderLogin(QStringLiteral("用户名或密码错误"), redirect));
    }

    // 创建会话
    Session s;
    s.sessionId = Crypto::randomToken(32);
    s.userId    = user->userId;
    s.expiresAt = QDateTime::currentSecsSinceEpoch() + m_cfg.sessionTtl;
    m_sessions->save(s);

    const QByteArray cookie = QStringLiteral(
                                  "%1=%2; HttpOnly; SameSite=Lax; Max-Age=%3; Path=/")
                                  .arg(m_cfg.cookieName, s.sessionId)
                                  .arg(m_cfg.sessionTtl)
                                  .toUtf8();

    qInfo(lcAuth) << "登录成功 user=" << username << "userId=" << user->userId;
    return redirectResponse(redirect.isEmpty() ? QStringLiteral("/") : redirect, cookie);
}

/* ---------------- GET /authorize ---------------- */

QHttpServerResponse AuthServer::handleAuthorize(const QHttpServerRequest &req)
{
    const QUrlQuery q(req.url());
    const QString clientId    = q.queryItemValue(QStringLiteral("client_id"));
    const QString redirectUri = q.queryItemValue(QStringLiteral("redirect_uri"), QUrl::FullyDecoded);
    const QString respType    = q.queryItemValue(QStringLiteral("response_type"));
    const QString scope       = q.queryItemValue(QStringLiteral("scope"));
    const QString state       = q.queryItemValue(QStringLiteral("state"));

    // 1) 校验 client_id
    const auto client = m_clients->findById(clientId);
    if (!client)
        return jsonError(QStringLiteral("invalid_client"), QStringLiteral("未知的 client_id"));

    // 2) 校验 redirect_uri 白名单（精确匹配）
    if (!client->redirectUris.contains(redirectUri))
        return jsonError(QStringLiteral("invalid_request"),
                         QStringLiteral("redirect_uri 未在客户端白名单中"));

    // 3) 仅支持 authorization_code
    if (respType != QLatin1String("code"))
        return jsonError(QStringLiteral("unsupported_response_type"),
                         QStringLiteral("仅支持 response_type=code"));

    // 4) 检查会话
    const auto user = currentUser(req);
    if (!user) {
        QString target = req.url().path();
        if (req.url().hasQuery())
            target += QLatin1Char('?') + req.url().query();

        const QString loginUrl = QStringLiteral("/login?redirect=")
                                 + QString::fromLatin1(QUrl::toPercentEncoding(target));
        qInfo(lcAuth) << "未登录，重定向到登录页";
        return redirectResponse(loginUrl);
    }

    // 5) 生成一次性授权码
    AuthCode ac;
    ac.code        = Crypto::randomToken(32);
    ac.clientId    = clientId;
    ac.redirectUri = redirectUri;
    ac.userId      = user->userId;
    ac.scope       = scope;
    ac.expiresAt   = QDateTime::currentSecsSinceEpoch() + m_cfg.authCodeTtl;
    m_codes->save(ac);

    QUrl cb(redirectUri);
    QUrlQuery cq(cb.query());
    cq.addQueryItem(QStringLiteral("code"), ac.code);
    if (!state.isEmpty())
        cq.addQueryItem(QStringLiteral("state"), state);
    cb.setQuery(cq);

    qInfo(lcAuth) << "签发授权码 client=" << clientId << "user=" << user->username;
    return redirectResponse(cb.toString());
}

/* ---------------- POST /token ---------------- */

QHttpServerResponse AuthServer::handleToken(const QHttpServerRequest &req)
{
    const QUrlQuery form(QString::fromUtf8(req.body()));
    const QString grantType    = form.queryItemValue(QStringLiteral("grant_type"));
    const QString code         = form.queryItemValue(QStringLiteral("code"));
    const QString redirectUri  = form.queryItemValue(QStringLiteral("redirect_uri"), QUrl::FullyDecoded);
    const QString clientId     = form.queryItemValue(QStringLiteral("client_id"));
    const QString clientSecret = form.queryItemValue(QStringLiteral("client_secret"));
    const QString ip           = req.remoteAddress().toString();

    if (grantType != QLatin1String("authorization_code"))
        return jsonError(QStringLiteral("unsupported_grant_type"),
                         QStringLiteral("仅支持 authorization_code"));

    QString reason;
    if (!m_plugins->beforeToken(clientId, ip, &reason))
        return jsonError(QStringLiteral("access_denied"), reason);

    // 1) 校验客户端凭据
    const auto client = m_clients->findById(clientId);
    if (!client)
        return jsonError(QStringLiteral("invalid_client"), QStringLiteral("未知客户端"));

    if (!Crypto::constantTimeEquals(client->clientSecret.toUtf8(), clientSecret.toUtf8())) {
        qWarning(lcAuth) << "client_secret 校验失败 client=" << clientId << "ip=" << ip;
        return jsonError(QStringLiteral("invalid_client"), QStringLiteral("client_secret 不正确"));
    }

    // 2) 校验授权码（一次性）
    const auto ac = m_codes->consume(code);
    if (!ac)
        return jsonError(QStringLiteral("invalid_grant"), QStringLiteral("授权码不存在或已被使用"));
    if (ac->expiresAt < QDateTime::currentSecsSinceEpoch())
        return jsonError(QStringLiteral("invalid_grant"), QStringLiteral("授权码已过期"));
    if (ac->clientId != clientId)
        return jsonError(QStringLiteral("invalid_grant"), QStringLiteral("授权码与客户端不匹配"));
    if (!redirectUri.isEmpty() && ac->redirectUri != redirectUri)
        return jsonError(QStringLiteral("invalid_grant"), QStringLiteral("redirect_uri 不匹配"));

    const auto user = m_users->findById(ac->userId);
    if (!user)
        return jsonError(QStringLiteral("invalid_grant"), QStringLiteral("用户不存在"));

    // 3) 签发 JWT
    const qint64 now = QDateTime::currentSecsSinceEpoch();
    const QJsonObject payload{
        {"iss",       m_cfg.jwtIssuer},
        {"sub",       user->userId},
        {"name",      user->name},
        {"phone",     user->phone},
        {"email",     user->email},
        {"roles",     QJsonArray::fromStringList(user->roles)},
        {"client_id", clientId},
        {"scope",     ac->scope},
        {"iat",       now},
        {"exp",       now + m_cfg.accessTokenTtl},
        };
    const QString token = Jwt::sign(payload, m_cfg.jwtSecret);

    const QJsonObject resp{
        {"access_token", token},
        {"token_type",   QStringLiteral("Bearer")},
        {"expires_in",   m_cfg.accessTokenTtl},
        {"scope",        ac->scope},
        };

    qInfo(lcAuth) << "签发访问令牌 client=" << clientId << "sub=" << user->userId;
    return jsonResponse(resp);
}

/* ---------------- GET /userinfo ---------------- */

QHttpServerResponse AuthServer::handleUserinfo(const QHttpServerRequest &req)
{
    const QByteArray auth = req.value("Authorization");
    if (!auth.startsWith("Bearer "))
        return QHttpServerResponse(QHttpServerResponse::StatusCode::Unauthorized);

    const QString token = QString::fromUtf8(auth.mid(7)).trimmed();

    QJsonObject payload;
    QString err;
    if (!Jwt::verify(token, m_cfg.jwtSecret, &payload, &err)) {
        qWarning(lcAuth) << "userinfo 令牌校验失败:" << err;
        return QHttpServerResponse(QHttpServerResponse::StatusCode::Unauthorized);
    }

    const QJsonObject out{
        {"sub",       payload.value("sub")},
        {"name",      payload.value("name")},
        {"phone",     payload.value("phone")},
        {"email",     payload.value("email")},
        {"roles",     payload.value("roles")},
        {"client_id", payload.value("client_id")},
        };
    return jsonResponse(out);
}

/* ---------------- GET /logout ---------------- */

QHttpServerResponse AuthServer::handleLogout(const QHttpServerRequest &req)
{
    const QString sid = parseCookies(req).value(m_cfg.cookieName);
    if (!sid.isEmpty()) {
        m_sessions->remove(sid);
        qInfo(lcAuth) << "用户登出 session 已清除";
    }

    const QByteArray cookie = QStringLiteral(
                                  "%1=; HttpOnly; SameSite=Lax; Max-Age=0; Path=/")
                                  .arg(m_cfg.cookieName).toUtf8();

    return redirectResponse(QStringLiteral("/"), cookie);
}