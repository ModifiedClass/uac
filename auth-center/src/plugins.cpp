#include "plugins.h"

#include <QDateTime>
#include <QHttpServerResponse>
#include <QJsonArray>
#include <QJsonDocument>
#include <QMutexLocker>

namespace {

qint64 nowSecs()
{
    return QDateTime::currentSecsSinceEpoch();
}

QHttpServerResponse blockResponse(int status, const QString &message, int retryAfterSeconds = -1)
{
    QJsonObject body{
        {QStringLiteral("error"), status == 429 ? QStringLiteral("too_many_requests")
                                                : QStringLiteral("forbidden")},
        {QStringLiteral("message"), message},
    };
    QHttpServerResponse resp(QStringLiteral("application/json"),
                             QJsonDocument(body).toJson(QJsonDocument::Compact),
                             QHttpServerResponse::StatusCode(status));
    if (retryAfterSeconds > 0)
        resp.setHeader("Retry-After", QByteArray::number(retryAfterSeconds));
    return resp;
}

} // namespace

// ---------------- RateLimitPlugin ----------------

RateLimitPlugin::RateLimitPlugin(const QJsonObject &cfg)
{
    m_loginMaxFailures = cfg.value(QStringLiteral("login_max_failures")).toInt(m_loginMaxFailures);
    m_loginWindowSeconds = cfg.value(QStringLiteral("login_window_seconds")).toVariant().toLongLong();
    m_loginLockSeconds = cfg.value(QStringLiteral("login_lock_seconds")).toVariant().toLongLong();
    m_globalMaxPerMinute = cfg.value(QStringLiteral("global_max_per_minute")).toInt(m_globalMaxPerMinute);
    if (m_loginWindowSeconds <= 0)
        m_loginWindowSeconds = 300;
    if (m_loginLockSeconds <= 0)
        m_loginLockSeconds = 900;
}

std::optional<QHttpServerResponse> RateLimitPlugin::onRequest(const RequestContext &ctx)
{
    const qint64 now = nowSecs();
    QMutexLocker locker(&m_mutex);
    IpState &st = m_states[ctx.ip];

    // 登录防抖: 锁定期间直接拒绝登录请求(带 Retry-After)
    if (ctx.path == QLatin1String("/login") && ctx.method == QLatin1String("POST")
        && st.lockedUntil > now) {
        return blockResponse(429, QStringLiteral("登录失败次数过多, 请稍后再试"),
                             int(st.lockedUntil - now));
    }

    // 全局限速: 每 IP 每分钟滑动窗口
    while (!st.requests.isEmpty() && st.requests.first() < now - 60)
        st.requests.removeFirst();
    st.requests.append(now);
    if (st.requests.size() > m_globalMaxPerMinute)
        return blockResponse(429, QStringLiteral("请求过于频繁"), 60);

    return std::nullopt;
}

void RateLimitPlugin::recordLoginFailure(const QString &ip)
{
    const qint64 now = nowSecs();
    QMutexLocker locker(&m_mutex);
    IpState &st = m_states[ip];
    while (!st.failures.isEmpty() && st.failures.first() < now - m_loginWindowSeconds)
        st.failures.removeFirst();
    st.failures.append(now);
    if (st.failures.size() >= m_loginMaxFailures) {
        st.lockedUntil = now + m_loginLockSeconds;
        st.failures.clear();
    }
}

void RateLimitPlugin::recordLoginSuccess(const QString &ip)
{
    QMutexLocker locker(&m_mutex);
    IpState &st = m_states[ip];
    st.failures.clear();
    st.lockedUntil = 0;
}

// ---------------- BlacklistPlugin ----------------

BlacklistPlugin::BlacklistPlugin(const QJsonObject &cfg)
{
    const auto toSet = [](const QJsonValue &v) {
        QSet<QString> out;
        for (const auto &item : v.toArray())
            out.insert(item.toString());
        return out;
    };
    m_blockedIps = toSet(cfg.value(QStringLiteral("blocked_ips")));
    m_blockedUsernames = toSet(cfg.value(QStringLiteral("blocked_usernames")));
    for (const auto &item : cfg.value(QStringLiteral("blocked_tokens")).toArray())
        m_blockedTokens.insert(item.toString().toUtf8());
}

std::optional<QHttpServerResponse> BlacklistPlugin::onRequest(const RequestContext &ctx)
{
    if (isIpBlocked(ctx.ip))
        return blockResponse(403, QStringLiteral("IP 已被列入黑名单"));
    if (!ctx.username.isEmpty() && isUsernameBlocked(ctx.username))
        return blockResponse(403, QStringLiteral("账号已被列入黑名单"));
    if (!ctx.bearerToken.isEmpty() && isTokenBlocked(ctx.bearerToken))
        return blockResponse(401, QStringLiteral("令牌已被列入黑名单"));
    return std::nullopt;
}

bool BlacklistPlugin::isIpBlocked(const QString &ip) const
{
    return m_blockedIps.contains(ip);
}

bool BlacklistPlugin::isUsernameBlocked(const QString &username) const
{
    return m_blockedUsernames.contains(username);
}

bool BlacklistPlugin::isTokenBlocked(const QByteArray &token) const
{
    return m_blockedTokens.contains(token);
}

// ---------------- 插件注册表 ----------------

namespace Plugins {

QHash<QString, Factory> &registry()
{
    static QHash<QString, Factory> reg;
    return reg;
}

void registerBuiltins()
{
    static bool done = false;
    if (done)
        return;
    registry().insert(QStringLiteral("rate_limit"), [](const QJsonObject &cfg) {
        return std::make_unique<RateLimitPlugin>(cfg);
    });
    registry().insert(QStringLiteral("blacklist"), [](const QJsonObject &cfg) {
        return std::make_unique<BlacklistPlugin>(cfg);
    });
    done = true;
}

} // namespace Plugins
