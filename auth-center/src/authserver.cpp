// authserver.cpp —— 路由与业务逻辑(登录、授权码、令牌、用户信息、登出)
//
// 安全要点:
//   - 密码仅哈希存储/比较, 日志不输出密码、JWT 密钥、令牌等敏感信息
//   - redirect 参数仅允许站内相对路径, 防开放重定向
//   - redirect_uri 精确匹配白名单, 防授权码劫持
//   - 授权码一次性使用; 会话 Cookie: HttpOnly + SameSite=Lax + Path=/
#include "authserver.h"

#include "passwordhash.h"
#include "util.h"

#include <QDateTime>
#include <QFile>
#include <QHostAddress>
#include <QHttpServerResponder>
#include <QJsonArray>
#include <QJsonDocument>
#include <QList>
#include <QUrl>
#include <QUrlQuery>

namespace {

// 登录页内置模板(当 templates/login.html 缺失时兜底)
const char kBuiltinLoginTemplate[] = R"HTML(
<!DOCTYPE html>
<html lang="zh-CN">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>统一认证中心 · 登录</title>
<style>
body{font-family:"Segoe UI","PingFang SC","Microsoft YaHei",sans-serif;margin:0;min-height:100vh;display:flex;align-items:center;justify-content:center;background:#f4f6f9;color:#1f2937}
.card{width:360px;background:#fff;border:1px solid #e2e8f0;border-radius:12px;padding:32px;box-shadow:0 4px 16px rgba(0,0,0,.06)}
h1{font-size:20px;margin:0 0 4px}
.sub{color:#6b7280;font-size:13px;margin:0 0 24px}
label{display:block;font-size:14px;margin-bottom:16px}
input[type=text],input[type=password]{width:100%;margin-top:6px;padding:9px 12px;font-size:14px;border:1px solid #e2e8f0;border-radius:8px}
button{width:100%;padding:10px;font-size:15px;color:#fff;background:#2563eb;border:0;border-radius:8px;cursor:pointer;margin-top:8px}
.error{background:#fef2f2;color:#dc2626;border:1px solid #fecaca;border-radius:8px;padding:10px 12px;font-size:14px;margin-bottom:16px}
.hint{color:#9ca3af;font-size:12px;text-align:center;margin:20px 0 0}
</style>
</head>
<body>
<form class="card" method="post" action="/login">
<h1>统一认证中心</h1>
<p class="sub">Unified Authentication Center (OAuth2 + JWT)</p>
<div class="error" {{error_style}}>{{error}}</div>
<label>用户名<input type="text" name="username" value="{{username}}" autocomplete="username" required autofocus></label>
<label>密码<input type="password" name="password" autocomplete="current-password" required></label>
<input type="hidden" name="redirect" value="{{redirect}}">
<button type="submit">登 录</button>
<p class="hint">示例账号：admin / admin123</p>
</form>
</body>
</html>
)HTML";

const char kHomeGuest[] = R"HTML(
<!DOCTYPE html>
<html lang="zh-CN">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>uac 统一认证中心</title>
<style>
body{font-family:"Segoe UI","PingFang SC","Microsoft YaHei",sans-serif;max-width:720px;margin:60px auto;padding:0 24px;color:#1f2937;background:#f4f6f9}
.card{background:#fff;border:1px solid #e2e8f0;border-radius:12px;padding:32px;box-shadow:0 4px 16px rgba(0,0,0,.06)}
h1{font-size:22px;margin:0 0 12px}
a{color:#2563eb}
.hint{color:#9ca3af;font-size:13px;margin-top:24px}
</style>
</head>
<body>
<div class="card">
<h1>统一认证中心</h1>
<p>您尚未登录。</p>
<p><a href="/login">前往登录</a></p>
<p class="hint">OAuth2 端点: /authorize(授权码) · /token(令牌) · /userinfo(用户信息) · /logout(登出)</p>
</div>
</body>
</html>
)HTML";

const char kHomeLoggedIn[] = R"HTML(
<!DOCTYPE html>
<html lang="zh-CN">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>uac 统一认证中心</title>
<style>
body{font-family:"Segoe UI","PingFang SC","Microsoft YaHei",sans-serif;max-width:720px;margin:60px auto;padding:0 24px;color:#1f2937;background:#f4f6f9}
.card{background:#fff;border:1px solid #e2e8f0;border-radius:12px;padding:32px;box-shadow:0 4px 16px rgba(0,0,0,.06)}
h1{font-size:22px;margin:0 0 12px}
a{color:#2563eb;display:inline-block;margin:6px 16px 6px 0}
.hint{color:#9ca3af;font-size:13px;margin-top:24px}
</style>
</head>
<body>
<div class="card">
<h1>统一认证中心</h1>
<p>当前用户: <strong>{{name}}</strong> (角色: {{roles}})</p>
<p>用户ID: {{uid}} | 邮箱: {{email}}</p>
<p><a href="{{demo_authorize}}">发起授权码流程(演示, 跳转业务系统 /callback)</a></p>
<p><a href="/logout">退出登录</a></p>
<p class="hint">JWT 访问令牌通过 POST /token 获取; 用户信息通过 GET /userinfo(Bearer 令牌)获取</p>
</div>
</body>
</html>
)HTML";

QString htmlEscape(QString s)
{
    s.replace(QLatin1Char('&'), QStringLiteral("&amp;"));
    s.replace(QLatin1Char('<'), QStringLiteral("&lt;"));
    s.replace(QLatin1Char('>'), QStringLiteral("&gt;"));
    s.replace(QLatin1Char('"'), QStringLiteral("&quot;"));
    s.replace(QLatin1Char('\''), QStringLiteral("&#39;"));
    return s;
}

} // namespace

AuthServer::AuthServer(QObject *parent)
    : QObject(parent)
{
    connect(&m_cleanupTimer, &QTimer::timeout, this, [this]() {
        m_sessions->purgeExpired();
        m_authCodes->purgeExpired();
    });
}

bool AuthServer::init(const AppConfig &cfg)
{
    m_cfg = cfg;

    // ---------------- 存储装配(示例: 内存; 生产: PostgreSQL/Redis) ----------------
    auto users = std::make_unique<MemoryUserStore>();
    for (const auto &cu : cfg.users) {
        UserInfo u;
        u.userId = cu.userId;
        u.username = cu.username;
        u.name = cu.name;
        u.phone = cu.phone;
        u.email = cu.email;
        u.roles = cu.roles;
        if (!cu.passwordHash.isEmpty()) {
            u.passwordHash = cu.passwordHash;
        } else if (!cu.passwordPlain.isEmpty()) {
            // 引导模式: 配置中写了明文密码, 启动时哈希。★生产禁止, 请直接提供 password_hash★
            u.passwordHash = PasswordHasher::hashPassword(cu.passwordPlain);
            qWarning().noquote()
                << "[config] 用户" << cu.username
                << "在配置中使用明文密码引导, 已在启动时哈希; 请立即从配置中移除明文字段!";
        } else {
            qWarning().noquote() << "[config] 用户" << cu.username
                                 << "缺少 password_hash, 已跳过";
            continue;
        }
        if (!users->add(u))
            qWarning().noquote() << "[config] 用户" << cu.username << "重复, 已跳过";
    }
    m_users = std::move(users);

    auto clients = std::make_unique<MemoryClientStore>();
    for (const auto &cc : cfg.clients) {
        if (!clients->add(ClientInfo{cc.clientId, cc.clientSecret, cc.redirectUris, cc.name}))
            qWarning().noquote() << "[config] 客户端" << cc.clientId << "重复, 已跳过";
    }
    m_clients = std::move(clients);

    auto codes = std::make_unique<MemoryAuthCodeStore>();
    codes->setTtlSeconds(cfg.authCodeTtlSeconds);
    m_authCodes = std::move(codes);

    auto sessions = std::make_unique<MemorySessionStore>();
    sessions->setTtlSeconds(cfg.sessionTtlSeconds);
    m_sessions = std::move(sessions);

    m_jwt = std::make_unique<JwtHandler>(cfg.jwtSecret);
    m_jwt->setDefaultTtlSeconds(cfg.accessTokenTtlSeconds);

    // ---------------- 插件装配(需求 4.7: 按配置启用) ----------------
    Plugins::registerBuiltins();
    for (const auto &pc : cfg.plugins) {
        if (!pc.enabled) {
            qInfo().noquote() << "[plugins] 跳过(disabled):" << pc.name;
            continue;
        }
        const auto it = Plugins::registry().constFind(pc.name);
        if (it == Plugins::registry().constEnd()) {
            qWarning().noquote() << "[plugins] 未知插件, 已忽略:" << pc.name;
            continue;
        }
        auto plugin = it.value()(pc.config);
        if (auto *rl = dynamic_cast<RateLimitPlugin *>(plugin.get()))
            m_rateLimit = rl;
        if (auto *bl = dynamic_cast<BlacklistPlugin *>(plugin.get()))
            m_blacklist = bl;
        m_plugins.push_back(std::move(plugin));
        qInfo().noquote() << "[plugins] 已启用插件:" << pc.name;
    }

    // ---------------- 模板 ----------------
    QFile tpl(m_cfg.templateDir + QStringLiteral("/login.html"));
    if (tpl.open(QIODevice::ReadOnly)) {
        m_loginTemplate = tpl.readAll();
        qInfo().noquote() << "[template] 登录模板:" << tpl.fileName();
    } else {
        qWarning().noquote() << "[template] 无法读取" << tpl.fileName()
                             << "(" << tpl.errorString() << "), 使用内置模板";
        m_loginTemplate = QByteArray(kBuiltinLoginTemplate);
    }

    // ---------------- 路由注册 ----------------
    m_server.route("/", QHttpServerRequest::Method::Get,
                   [this](const QHttpServerRequest &req) { return handleHome(req); });
    m_server.route("/login", QHttpServerRequest::Method::Get,
                   [this](const QHttpServerRequest &req) { return handleLoginPage(req); });
    m_server.route("/login", QHttpServerRequest::Method::Post,
                   [this](const QHttpServerRequest &req) { return handleLoginSubmit(req); });
    m_server.route("/authorize", QHttpServerRequest::Method::Get,
                   [this](const QHttpServerRequest &req) { return handleAuthorize(req); });
    m_server.route("/token", QHttpServerRequest::Method::Post,
                   [this](const QHttpServerRequest &req) { return handleToken(req); });
    m_server.route("/userinfo", QHttpServerRequest::Method::Get,
                   [this](const QHttpServerRequest &req) { return handleUserInfo(req); });
    m_server.route("/logout", QHttpServerRequest::Method::Get,
                   [this](const QHttpServerRequest &req) { return handleLogout(req); });
    m_server.route("/healthz", QHttpServerRequest::Method::Get,
                   [](const QHttpServerRequest &) {
                       return QHttpServerResponse(QStringLiteral("application/json"),
                                                  QByteArrayLiteral("{\"status\":\"ok\"}"),
                                                  QHttpServerResponse::StatusCode::Ok);
                   });

    m_server.setMissingHandler([](const QHttpServerRequest &req, QHttpServerResponder &&responder) {
        Q_UNUSED(req);
        responder.write(QHttpServerResponse(QStringLiteral("application/json"),
                                            QByteArrayLiteral("{\"error\":\"not_found\"}"),
                                            QHttpServerResponse::StatusCode::NotFound));
    });

    // 统一安全响应头(与 Nginx 侧安全头形成双层防护)
    m_server.afterRequest([](QHttpServerResponse &&resp) {
        resp.setHeader("X-Content-Type-Options", "nosniff");
        resp.setHeader("X-Frame-Options", "DENY");
        resp.setHeader("Referrer-Policy", "no-referrer");
        resp.setHeader("Cache-Control", "no-store");
        return std::move(resp);
    });

    m_cleanupTimer.start(60 * 1000); // 每分钟清理过期会话/授权码
    return true;
}

bool AuthServer::start()
{
    const QHostAddress host(m_cfg.host.isEmpty() ? QStringLiteral("127.0.0.1") : m_cfg.host);
    // 注: Qt >= 6.7 listen() 返回 bool(实际端口用 serverPort());
    //     Qt 6.4/6.5 listen() 返回 quint16(0 表示失败), 本行两种 API 均可编译。
    if (!m_server.listen(host, static_cast<quint16>(m_cfg.port))) {
        qCritical().noquote() << "[startup] 监听失败:" << m_cfg.host << ":" << m_cfg.port;
        return false;
    }
    qInfo().noquote() << "[startup] 监听成功:" << m_cfg.host << ":" << m_cfg.port
                      << "(仅本地回环, 对外由 Nginx 反向代理)";
    return true;
}

// ---------------- 工具函数 ----------------

QString AuthServer::clientIp(const QHttpServerRequest &req) const
{
    // 部署在 Nginx 之后: 优先 X-Forwarded-For 首项(最靠近真实客户端)。
    // 注意: 服务仅监听本地回环, 仅信任本机 Nginx 注入的转发头; 若直接对外必须移除本逻辑。
    const QByteArray xff = req.headers().value("X-Forwarded-For");
    if (!xff.isEmpty()) {
        const auto parts = xff.split(',');
        if (!parts.isEmpty()) {
            const QString ip = QString::fromLatin1(parts.first().trimmed());
            if (!ip.isEmpty())
                return ip;
        }
    }
    const QByteArray xrip = req.headers().value("X-Real-IP");
    if (!xrip.isEmpty())
        return QString::fromLatin1(xrip);
    return req.remoteAddress().toString();
}

QByteArray AuthServer::cookieValue(const QHttpServerRequest &req, const QByteArray &name) const
{
    const QByteArray header = req.headers().value("Cookie");
    for (const QByteArray &part : header.split(';')) {
        const QByteArray kv = part.trimmed();
        const int eq = kv.indexOf('=');
        if (eq > 0 && kv.left(eq) == name)
            return kv.mid(eq + 1);
    }
    return {};
}

std::optional<UserInfo> AuthServer::currentUser(const QHttpServerRequest &req) const
{
    const QByteArray sid = cookieValue(req, "session_id");
    if (sid.isEmpty())
        return std::nullopt;
    const auto session = m_sessions->find(QString::fromLatin1(sid));
    if (!session)
        return std::nullopt;
    return m_users->findById(session->userId);
}

QByteArray AuthServer::renderLoginPage(const QString &error, const QString &username,
                                       const QString &redirect) const
{
    QByteArray out = m_loginTemplate;
    // 注意顺序: 先替换 {{error_style}} 再替换 {{error}}(避免子串误替换)
    out.replace("{{error_style}}",
                error.isEmpty() ? "style=\"display:none\"" : "");
    out.replace("{{error}}", htmlEscape(error).toUtf8());
    out.replace("{{username}}", htmlEscape(username).toUtf8());
    out.replace("{{redirect}}", htmlEscape(redirect).toUtf8());
    return out;
}

QByteArray AuthServer::renderHomePage(const std::optional<UserInfo> &user) const
{
    if (!user)
        return QByteArray(kHomeGuest);

    QByteArray html(kHomeLoggedIn);
    html.replace("{{name}}", htmlEscape(user->username).toUtf8());
    html.replace("{{roles}}", htmlEscape(user->roles.join(QStringLiteral(", "))).toUtf8());
    html.replace("{{uid}}", htmlEscape(user->userId).toUtf8());
    html.replace("{{email}}", htmlEscape(user->email).toUtf8());

    QByteArray demo = "/authorize?client_id=fastapi-app&response_type=code&scope=openid+profile&state=demo&redirect_uri=";
    if (const auto client = m_clients->findById(QStringLiteral("fastapi-app"));
        client && !client->redirectUris.isEmpty())
        demo += QUrl::toPercentEncoding(client->redirectUris.first());
    html.replace("{{demo_authorize}}", demo);
    return html;
}

QHttpServerResponse AuthServer::redirect(const QByteArray &location, const QByteArray &setCookie) const
{
    QHttpServerResponse resp(QStringLiteral("text/html"), QByteArray(),
                             QHttpServerResponse::StatusCode::Found);
    resp.setHeader("Location", location);
    if (!setCookie.isEmpty())
        resp.setHeader("Set-Cookie", setCookie);
    return resp;
}

QHttpServerResponse AuthServer::json(const QJsonObject &obj, QHttpServerResponse::StatusCode status) const
{
    return QHttpServerResponse(QStringLiteral("application/json"),
                               QJsonDocument(obj).toJson(QJsonDocument::Compact), status);
}

QHttpServerResponse AuthServer::oauthError(const QString &errorCode, const QString &description,
                                           int httpStatus, bool basicAuth) const
{
    QJsonObject body{
        {QStringLiteral("error"), errorCode},
        {QStringLiteral("error_description"), description},
    };
    QHttpServerResponse resp = json(body, QHttpServerResponse::StatusCode(httpStatus));
    if (basicAuth)
        resp.setHeader("WWW-Authenticate", "Basic realm=\"uac\"");
    return resp;
}

std::optional<QHttpServerResponse> AuthServer::runPlugins(const RequestContext &ctx)
{
    for (const auto &plugin : m_plugins) {
        auto resp = plugin->onRequest(ctx);
        if (resp) {
            qWarning().noquote() << "[plugins] 请求被拦截 plugin=" << plugin->name()
                                 << " path=" << ctx.path << " ip=" << ctx.ip;
            return resp;
        }
    }
    return std::nullopt;
}

// ---------------- 首页 ----------------

QHttpServerResponse AuthServer::handleHome(const QHttpServerRequest &req)
{
    RequestContext ctx;
    ctx.ip = clientIp(req);
    ctx.path = QStringLiteral("/");
    ctx.method = QStringLiteral("GET");
    if (auto blocked = runPlugins(ctx))
        return std::move(*blocked);

    return QHttpServerResponse(QStringLiteral("text/html"), renderHomePage(currentUser(req)),
                               QHttpServerResponse::StatusCode::Ok);
}

// ---------------- 登录 ----------------

QHttpServerResponse AuthServer::handleLoginPage(const QHttpServerRequest &req)
{
    RequestContext ctx;
    ctx.ip = clientIp(req);
    ctx.path = QStringLiteral("/login");
    ctx.method = QStringLiteral("GET");
    if (auto blocked = runPlugins(ctx))
        return std::move(*blocked);

    // 已登录用户直接回首页
    if (currentUser(req))
        return redirect("/");

    QString redirectTarget = req.query().queryItemValue(QStringLiteral("redirect"));
    if (redirectTarget.isEmpty() || !redirectTarget.startsWith(QLatin1Char('/'))
        || redirectTarget.startsWith(QStringLiteral("//"))) {
        redirectTarget = QStringLiteral("/"); // 防开放重定向: 仅允许站内相对路径
    }
    return QHttpServerResponse(QStringLiteral("text/html"),
                               renderLoginPage(QString(), QString(), redirectTarget),
                               QHttpServerResponse::StatusCode::Ok);
}

QHttpServerResponse AuthServer::handleLoginSubmit(const QHttpServerRequest &req)
{
    QUrlQuery form;
    form.setQuery(QString::fromUtf8(req.body()));

    const QString username = form.queryItemValue(QStringLiteral("username")).trimmed();
    const QString password = form.queryItemValue(QStringLiteral("password"));
    QString redirectTarget = form.queryItemValue(QStringLiteral("redirect"));
    if (redirectTarget.isEmpty() || !redirectTarget.startsWith(QLatin1Char('/'))
        || redirectTarget.startsWith(QStringLiteral("//"))) {
        redirectTarget = QStringLiteral("/"); // 防开放重定向
    }

    const QString ip = clientIp(req);
    RequestContext ctx;
    ctx.ip = ip;
    ctx.path = QStringLiteral("/login");
    ctx.method = QStringLiteral("POST");
    ctx.username = username;
    if (auto blocked = runPlugins(ctx))
        return std::move(*blocked);

    const auto user = m_users->findByUsername(username);
    const bool passwordOk = user && PasswordHasher::verifyPassword(password, user->passwordHash);

    if (!passwordOk) {
        if (m_rateLimit)
            m_rateLimit->recordLoginFailure(ip);
        // 注意: 日志不输出密码
        qWarning().noquote() << "[auth] 登录失败 username=" << username << " ip=" << ip;
        return QHttpServerResponse(QStringLiteral("text/html"),
                                   renderLoginPage(QStringLiteral("用户名或密码错误"), username, redirectTarget),
                                   QHttpServerResponse::StatusCode::Unauthorized);
    }

    if (m_rateLimit)
        m_rateLimit->recordLoginSuccess(ip);

    // 创建会话并下发 HttpOnly Cookie(24 小时可配置)
    const QString sessionId = m_sessions->create(user->userId);
    qInfo().noquote() << "[auth] 登录成功 user=" << user->userId
                      << " username=" << user->username << " ip=" << ip;

    const QByteArray cookie = "session_id=" + sessionId.toLatin1()
                              + "; HttpOnly; SameSite=Lax; Max-Age="
                              + QByteArray::number(m_cfg.sessionTtlSeconds) + "; Path=/";
    return redirect(redirectTarget.toUtf8(), cookie);
}

// ---------------- OAuth2 授权端点(授权码模式) ----------------

QHttpServerResponse AuthServer::handleAuthorize(const QHttpServerRequest &req)
{
    const QUrlQuery query = req.query();
    const QString clientId = query.queryItemValue(QStringLiteral("client_id"));
    const QString redirectUri = query.queryItemValue(QStringLiteral("redirect_uri"));
    const QString responseType = query.queryItemValue(QStringLiteral("response_type"));
    const QString scope = query.queryItemValue(QStringLiteral("scope"));
    const QString state = query.queryItemValue(QStringLiteral("state"));

    RequestContext ctx;
    ctx.ip = clientIp(req);
    ctx.path = QStringLiteral("/authorize");
    ctx.method = QStringLiteral("GET");
    if (auto blocked = runPlugins(ctx))
        return std::move(*blocked);

    // 1. 校验 client_id
    const auto client = m_clients->findById(clientId);
    if (!client) {
        qWarning().noquote() << "[authorize] 未知 client_id=" << clientId << " ip=" << ctx.ip;
        return QHttpServerResponse(QStringLiteral("text/html"),
                                   QByteArray("<h1>400</h1><p>未知的 client_id</p>"),
                                   QHttpServerResponse::StatusCode::BadRequest);
    }
    // 2. 校验 redirect_uri 白名单(精确匹配)
    if (!client->redirectUris.contains(redirectUri)) {
        qWarning().noquote() << "[authorize] redirect_uri 不在白名单:" << redirectUri
                             << " client=" << clientId;
        return QHttpServerResponse(QStringLiteral("text/html"),
                                   QByteArray("<h1>400</h1><p>redirect_uri 不在客户端白名单中</p>"),
                                   QHttpServerResponse::StatusCode::BadRequest);
    }
    // 3. 仅支持授权码模式
    if (responseType != QLatin1String("code")) {
        QUrl cb(redirectUri);
        QUrlQuery cbq(cb);
        cbq.addQueryItem(QStringLiteral("error"), QStringLiteral("unsupported_response_type"));
        cbq.addQueryItem(QStringLiteral("error_description"), QStringLiteral("仅支持 response_type=code"));
        if (!state.isEmpty())
            cbq.addQueryItem(QStringLiteral("state"), state);
        cb.setQuery(cbq);
        return redirect(cb.toEncoded());
    }
    // 4. 会话检查: 未登录 -> 登录页(携带当前 URL, 登录后跳回)
    const auto user = currentUser(req);
    if (!user) {
        const QByteArray currentUrl = req.url().toEncoded(); // 如 /authorize?client_id=...
        return redirect("/login?redirect=" + QUrl::toPercentEncoding(QString::fromLatin1(currentUrl)));
    }
    // 5. 签发授权码(一次性, 5 分钟可配置)
    const QString code = m_authCodes->issue(clientId, redirectUri, user->userId, scope);
    qInfo().noquote() << "[authorize] 签发授权码 client=" << clientId
                      << " user=" << user->userId << " scope=" << scope;

    QUrl cb(redirectUri);
    QUrlQuery cbq(cb);
    cbq.addQueryItem(QStringLiteral("code"), code);
    if (!state.isEmpty())
        cbq.addQueryItem(QStringLiteral("state"), state);
    cb.setQuery(cbq);
    return redirect(cb.toEncoded());
}

// ---------------- OAuth2 令牌端点 ----------------

QHttpServerResponse AuthServer::handleToken(const QHttpServerRequest &req)
{
    RequestContext ctx;
    ctx.ip = clientIp(req);
    ctx.path = QStringLiteral("/token");
    ctx.method = QStringLiteral("POST");
    if (auto blocked = runPlugins(ctx))
        return std::move(*blocked);

    // RFC 6749 §4.1.3: 必须使用 application/x-www-form-urlencoded
    const QByteArray contentType = req.headers().value("Content-Type");
    if (!contentType.toLower().startsWith("application/x-www-form-urlencoded")) {
        return oauthError(QStringLiteral("invalid_request"),
                          QStringLiteral("Content-Type 必须为 application/x-www-form-urlencoded"), 400);
    }

    QUrlQuery form;
    form.setQuery(QString::fromUtf8(req.body()));

    const QString grantType = form.queryItemValue(QStringLiteral("grant_type"));
    if (grantType != QLatin1String("authorization_code")) {
        return oauthError(QStringLiteral("unsupported_grant_type"),
                          QStringLiteral("仅支持 grant_type=authorization_code"), 400);
    }

    const QString clientId = form.queryItemValue(QStringLiteral("client_id"));
    const QString clientSecret = form.queryItemValue(QStringLiteral("client_secret"));
    const QString code = form.queryItemValue(QStringLiteral("code"));
    const QString redirectUri = form.queryItemValue(QStringLiteral("redirect_uri"));

    // 1. 校验客户端凭据(常量时间比较)
    const auto client = m_clients->findById(clientId);
    if (!client || !constantTimeEquals(client->clientSecret.toUtf8(), clientSecret.toUtf8())) {
        qWarning().noquote() << "[token] 客户端凭据校验失败 client=" << clientId << " ip=" << ctx.ip;
        return oauthError(QStringLiteral("invalid_client"),
                          QStringLiteral("client_id 或 client_secret 错误"), 401, true);
    }
    // 2. 校验并消费授权码(一次性)
    const auto issued = m_authCodes->consume(code);
    if (!issued)
        return oauthError(QStringLiteral("invalid_grant"), QStringLiteral("授权码无效或已被使用"), 400);
    if (issued->clientId != clientId)
        return oauthError(QStringLiteral("invalid_grant"), QStringLiteral("授权码与客户端不匹配"), 400);
    if (issued->redirectUri != redirectUri)
        return oauthError(QStringLiteral("invalid_grant"), QStringLiteral("redirect_uri 与签发时不一致"), 400);
    if (issued->expiresAt <= QDateTime::currentSecsSinceEpoch())
        return oauthError(QStringLiteral("invalid_grant"), QStringLiteral("授权码已过期"), 400);

    const auto user = m_users->findById(issued->userId);
    if (!user)
        return oauthError(QStringLiteral("invalid_grant"), QStringLiteral("授权用户不存在"), 400);

    // 3. 签发 JWT 访问令牌(1 小时可配置)
    QJsonObject claims{
        {QStringLiteral("sub"), user->userId},
        {QStringLiteral("name"), user->username},
        {QStringLiteral("phone"), user->phone},
        {QStringLiteral("email"), user->email},
        {QStringLiteral("roles"), QJsonArray::fromStringList(user->roles)},
        {QStringLiteral("client_id"), clientId},
        {QStringLiteral("scope"), issued->scope},
    };
    const QByteArray accessToken = m_jwt->createToken(claims, m_cfg.accessTokenTtlSeconds);

    qInfo().noquote() << "[token] 签发访问令牌 client=" << clientId << " user=" << user->userId
                      << " expires_in=" << m_cfg.accessTokenTtlSeconds;

    return json(QJsonObject{
        {QStringLiteral("access_token"), QString::fromLatin1(accessToken)},
        {QStringLiteral("token_type"), QStringLiteral("Bearer")},
        {QStringLiteral("expires_in"), m_cfg.accessTokenTtlSeconds},
        {QStringLiteral("scope"), issued->scope},
    }, QHttpServerResponse::StatusCode::Ok);
}

// ---------------- 用户信息端点 ----------------

QHttpServerResponse AuthServer::handleUserInfo(const QHttpServerRequest &req)
{
    const QByteArray authz = req.headers().value("Authorization").trimmed();
    if (!authz.startsWith("Bearer ")) {
        QHttpServerResponse resp = json(
            QJsonObject{{QStringLiteral("error"), QStringLiteral("invalid_token")},
                        {QStringLiteral("message"), QStringLiteral("缺少 Bearer 令牌")}},
            QHttpServerResponse::StatusCode::Unauthorized);
        resp.setHeader("WWW-Authenticate", "Bearer error=\"invalid_token\"");
        return resp;
    }
    const QByteArray token = authz.mid(7).trimmed();

    RequestContext ctx;
    ctx.ip = clientIp(req);
    ctx.path = QStringLiteral("/userinfo");
    ctx.method = QStringLiteral("GET");
    ctx.bearerToken = token;
    if (auto blocked = runPlugins(ctx))
        return std::move(*blocked);

    QJsonObject payload;
    QString err;
    if (!m_jwt->verify(token, &payload, &err)) {
        // 注意: 日志不输出令牌内容
        qWarning().noquote() << "[userinfo] 令牌校验失败 ip=" << ctx.ip << " reason=" << err;
        QHttpServerResponse resp = json(
            QJsonObject{{QStringLiteral("error"), QStringLiteral("invalid_token")},
                        {QStringLiteral("message"), err}},
            QHttpServerResponse::StatusCode::Unauthorized);
        resp.setHeader("WWW-Authenticate", "Bearer error=\"invalid_token\"");
        return resp;
    }

    return json(QJsonObject{
        {QStringLiteral("sub"), payload.value(QStringLiteral("sub"))},
        {QStringLiteral("name"), payload.value(QStringLiteral("name"))},
        {QStringLiteral("phone"), payload.value(QStringLiteral("phone"))},
        {QStringLiteral("email"), payload.value(QStringLiteral("email"))},
        {QStringLiteral("roles"), payload.value(QStringLiteral("roles"))},
        {QStringLiteral("client_id"), payload.value(QStringLiteral("client_id"))},
        {QStringLiteral("scope"), payload.value(QStringLiteral("scope"))},
    }, QHttpServerResponse::StatusCode::Ok);
}

// ---------------- 登出 ----------------

QHttpServerResponse AuthServer::handleLogout(const QHttpServerRequest &req)
{
    RequestContext ctx;
    ctx.ip = clientIp(req);
    ctx.path = QStringLiteral("/logout");
    ctx.method = QStringLiteral("GET");
    if (auto blocked = runPlugins(ctx))
        return std::move(*blocked);

    const QByteArray sid = cookieValue(req, "session_id");
    if (!sid.isEmpty()) {
        m_sessions->remove(QString::fromLatin1(sid));
        // 日志只输出会话 ID 前缀, 不输出完整值
        qInfo().noquote() << "[auth] 用户登出 session=" << QString::fromLatin1(sid).left(8)
                          << "... ip=" << ctx.ip;
    }
    return redirect("/", "session_id=; HttpOnly; SameSite=Lax; Max-Age=0; Path=/");
}
