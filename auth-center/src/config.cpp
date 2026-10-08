#include "config.h"

#include <QDebug>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>

namespace {

// 演示阶段默认密钥: 仅当 JWT_SECRET 环境变量与配置均未提供时使用(启动时输出警告)
const char kDefaultJwtSecret[] = "dev-only-insecure-secret-change-me";

// 演示用户密码哈希: SHA256(固定盐hex + ':' + 密码) 的 hex
// 盐: a1b2c3d4e5f60718293a4b5c6d7e8f90
// admin / admin123 -> 281e66a51f43debfd0ee80c0ebb6491a60a8fd701130f5ff0aad726ba2219964
// alice / alice123 -> a6f635be8fb29ef2e5262d178140176e540f096af36e4fbae41fb8b318303366
const char kAdminHash[] = "sha256$a1b2c3d4e5f60718293a4b5c6d7e8f90$281e66a51f43debfd0ee80c0ebb6491a60a8fd701130f5ff0aad726ba2219964";
const char kAliceHash[] = "sha256$a1b2c3d4e5f60718293a4b5c6d7e8f90$a6f635be8fb29ef2e5262d178140176e540f096af36e4fbae41fb8b318303366";

AppConfig demoDefaults()
{
    AppConfig cfg;

    ConfigUser admin;
    admin.userId = QStringLiteral("u-1001");
    admin.username = QStringLiteral("admin");
    admin.name = QStringLiteral("系统管理员");
    admin.phone = QStringLiteral("13800000001");
    admin.email = QStringLiteral("admin@example.com");
    admin.roles = {QStringLiteral("admin"), QStringLiteral("user")};
    admin.passwordHash = QByteArray(kAdminHash);

    ConfigUser alice;
    alice.userId = QStringLiteral("u-1002");
    alice.username = QStringLiteral("alice");
    alice.name = QStringLiteral("Alice");
    alice.phone = QStringLiteral("13800000002");
    alice.email = QStringLiteral("alice@example.com");
    alice.roles = {QStringLiteral("user")};
    alice.passwordHash = QByteArray(kAliceHash);

    ConfigClient app;
    app.clientId = QStringLiteral("fastapi-app");
    app.clientSecret = QStringLiteral("fastapi-app-secret");
    app.name = QStringLiteral("FastAPI 示例业务");
    app.redirectUris = {
        QStringLiteral("http://your-domain.com/callback"),
        QStringLiteral("http://127.0.0.1:8000/callback"),
        QStringLiteral("http://localhost:8000/callback"),
    };

    PluginConfig rateLimit;
    rateLimit.name = QStringLiteral("rate_limit");
    rateLimit.enabled = true;
    rateLimit.config = QJsonObject{
        {QStringLiteral("login_max_failures"), 5},
        {QStringLiteral("login_window_seconds"), 300},
        {QStringLiteral("login_lock_seconds"), 900},
        {QStringLiteral("global_max_per_minute"), 600},
    };

    PluginConfig blacklist;
    blacklist.name = QStringLiteral("blacklist");
    blacklist.enabled = true;
    blacklist.config = QJsonObject{
        {QStringLiteral("blocked_ips"), QJsonArray()},
        {QStringLiteral("blocked_usernames"), QJsonArray()},
        {QStringLiteral("blocked_tokens"), QJsonArray()},
    };

    cfg.users = {admin, alice};
    cfg.clients = {app};
    cfg.plugins = {rateLimit, blacklist};
    return cfg;
}

QStringList toStringList(const QJsonValue &v)
{
    QStringList out;
    for (const auto &item : v.toArray())
        out << item.toString();
    return out;
}

} // namespace

bool AppConfigLoader::load(const QString &path, AppConfig *out, QString *error)
{
    AppConfig cfg = demoDefaults();

    QFile file(path);
    if (file.open(QIODevice::ReadOnly)) {
        QJsonParseError perr{};
        const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &perr);
        if (perr.error != QJsonParseError::NoError || !doc.isObject()) {
            if (error)
                *error = QStringLiteral("%1: JSON 解析失败: %2").arg(path, perr.errorString());
            return false;
        }
        const QJsonObject root = doc.object();

        // server
        const QJsonObject server = root.value(QStringLiteral("server")).toObject();
        if (!server.isEmpty()) {
            cfg.host = server.value(QStringLiteral("host")).toString(cfg.host);
            cfg.port = server.value(QStringLiteral("port")).toInt(cfg.port);
        }

        // security
        const QJsonObject sec = root.value(QStringLiteral("security")).toObject();
        if (!sec.isEmpty()) {
            const QString secret = sec.value(QStringLiteral("jwt_secret")).toString();
            if (!secret.isEmpty())
                cfg.jwtSecret = secret.toUtf8();
            if (sec.contains(QStringLiteral("session_ttl_seconds")))
                cfg.sessionTtlSeconds = sec.value(QStringLiteral("session_ttl_seconds")).toVariant().toLongLong();
            if (sec.contains(QStringLiteral("auth_code_ttl_seconds")))
                cfg.authCodeTtlSeconds = sec.value(QStringLiteral("auth_code_ttl_seconds")).toVariant().toLongLong();
            if (sec.contains(QStringLiteral("access_token_ttl_seconds")))
                cfg.accessTokenTtlSeconds = sec.value(QStringLiteral("access_token_ttl_seconds")).toVariant().toLongLong();
            const QString tpl = sec.value(QStringLiteral("template_dir")).toString();
            if (!tpl.isEmpty())
                cfg.templateDir = tpl;
        }

        // plugins(4.7 插件化: 按配置装配防抖限流/黑名单等安全插件)
        const QJsonValue plugins = root.value(QStringLiteral("plugins"));
        if (plugins.isArray() && !plugins.toArray().isEmpty()) {
            cfg.plugins.clear();
            for (const auto &pv : plugins.toArray()) {
                const QJsonObject po = pv.toObject();
                PluginConfig pc;
                pc.name = po.value(QStringLiteral("name")).toString();
                pc.enabled = po.value(QStringLiteral("enabled")).toBool(true);
                pc.config = po.value(QStringLiteral("config")).toObject();
                if (!pc.name.isEmpty())
                    cfg.plugins.append(pc);
            }
        }

        // users
        const QJsonValue users = root.value(QStringLiteral("users"));
        if (users.isArray() && !users.toArray().isEmpty()) {
            cfg.users.clear();
            for (const auto &uv : users.toArray()) {
                const QJsonObject uo = uv.toObject();
                ConfigUser u;
                u.userId = uo.value(QStringLiteral("user_id")).toString();
                u.username = uo.value(QStringLiteral("username")).toString();
                u.name = uo.value(QStringLiteral("name")).toString();
                u.phone = uo.value(QStringLiteral("phone")).toString();
                u.email = uo.value(QStringLiteral("email")).toString();
                u.roles = toStringList(uo.value(QStringLiteral("roles")));
                u.passwordHash = uo.value(QStringLiteral("password_hash")).toString().toUtf8();
                u.passwordPlain = uo.value(QStringLiteral("password")).toString(); // 引导字段, 生产禁止
                if (!u.userId.isEmpty() && !u.username.isEmpty())
                    cfg.users.append(u);
            }
        }

        // clients
        const QJsonValue clients = root.value(QStringLiteral("clients"));
        if (clients.isArray() && !clients.toArray().isEmpty()) {
            cfg.clients.clear();
            for (const auto &cv : clients.toArray()) {
                const QJsonObject co = cv.toObject();
                ConfigClient c;
                c.clientId = co.value(QStringLiteral("client_id")).toString();
                c.clientSecret = co.value(QStringLiteral("client_secret")).toString();
                c.name = co.value(QStringLiteral("name")).toString();
                c.redirectUris = toStringList(co.value(QStringLiteral("redirect_uris")));
                if (!c.clientId.isEmpty())
                    cfg.clients.append(c);
            }
        }
    } else {
        qWarning().noquote() << "[config] 无法打开配置文件" << path << "(" << file.errorString()
                             << "), 使用内置演示配置(用户 admin/alice, 客户端 fastapi-app)";
    }

    // ---- 环境变量覆盖(优先级最高) ----
    if (const QByteArray host = qgetenv("UAC_HOST"); !host.isEmpty())
        cfg.host = QString::fromLatin1(host);

    bool ok = false;
    const int portEnv = qEnvironmentVariableIntValue("UAC_PORT", &ok);
    if (ok && portEnv > 0 && portEnv < 65536)
        cfg.port = portEnv;

    if (const QByteArray secret = qgetenv("JWT_SECRET"); !secret.isEmpty()) {
        cfg.jwtSecret = secret;
        cfg.jwtSecretFromEnv = true;
    }

    ok = false;
    const qint64 sessionTtl = qEnvironmentVariableIntValue("UAC_SESSION_TTL", &ok);
    if (ok && sessionTtl > 0)
        cfg.sessionTtlSeconds = sessionTtl;

    ok = false;
    const qint64 codeTtl = qEnvironmentVariableIntValue("UAC_CODE_TTL", &ok);
    if (ok && codeTtl > 0)
        cfg.authCodeTtlSeconds = codeTtl;

    ok = false;
    const qint64 tokenTtl = qEnvironmentVariableIntValue("UAC_TOKEN_TTL", &ok);
    if (ok && tokenTtl > 0)
        cfg.accessTokenTtlSeconds = tokenTtl;

    // JWT 密钥兜底与告警
    if (cfg.jwtSecret.isEmpty()) {
        cfg.jwtSecret = QByteArray(kDefaultJwtSecret);
        qWarning().noquote()
            << "[security] 未设置 JWT_SECRET 环境变量且配置中无 jwt_secret, 使用内置演示密钥!"
            << "生产环境必须通过环境变量注入强随机密钥(如 openssl rand -hex 32 生成)";
    } else if (!cfg.jwtSecretFromEnv) {
        qInfo().noquote() << "[security] JWT 密钥来自配置文件; 生产环境建议通过 JWT_SECRET 环境变量注入";
    }

    *out = cfg;
    return true;
}
