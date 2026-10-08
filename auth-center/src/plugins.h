#pragma once
// 插件机制(需求 4.7): 以插件方式通过配置实现防抖、黑名单、高性能、高可用、可扩展。
//
// 装配方式: config.json 的 "plugins" 数组, 每项 {"name": "...", "enabled": true, "config": {...}}。
// 扩展方式: 实现 AuthPlugin 接口 -> 在 Plugins::registerBuiltins() 注册工厂 -> config.json 添加条目。
// 内置插件:
//   rate_limit  防抖: 登录失败 N 次锁定 M 秒 + 每 IP 每分钟请求数上限(滑动窗口)
//   blacklist   黑名单: 按配置拦截 IP / 用户名 / 令牌

#include <QByteArray>
#include <QHash>
#include <QJsonObject>
#include <QList>
#include <QMutex>
#include <QSet>
#include <QString>
#include <functional>
#include <memory>
#include <optional>

class QHttpServerRequest;
class QHttpServerResponse;

// 插件看到的请求上下文(不包含原始请求对象, 便于测试与复用)
struct RequestContext {
    QString ip;             // 客户端 IP(优先 X-Forwarded-For 首项, 由 Nginx 注入)
    QString path;
    QString method;
    QString username;       // 登录尝试的用户名(仅 POST /login 时有值)
    QByteArray bearerToken; // Authorization: Bearer 后的令牌(如有)
};

class AuthPlugin
{
public:
    virtual ~AuthPlugin() = default;
    virtual QString name() const = 0;
    // 返回 std::nullopt 表示放行; 返回响应对象表示拦截(该响应直接返回客户端)
    virtual std::optional<QHttpServerResponse> onRequest(const RequestContext &ctx) = 0;
};

// 防抖/限流插件
class RateLimitPlugin : public AuthPlugin
{
public:
    explicit RateLimitPlugin(const QJsonObject &cfg);

    QString name() const override { return QStringLiteral("rate_limit"); }
    std::optional<QHttpServerResponse> onRequest(const RequestContext &ctx) override;

    void recordLoginFailure(const QString &ip);
    void recordLoginSuccess(const QString &ip);

private:
    struct IpState {
        QList<qint64> failures;   // 窗口内登录失败时间戳
        QList<qint64> requests;   // 每分钟窗口内请求时间戳
        qint64 lockedUntil = 0;   // 锁定截止时间(unix 秒)
    };

    mutable QMutex m_mutex;
    QHash<QString, IpState> m_states;

    int m_loginMaxFailures = 5;   // 窗口内最大失败次数
    qint64 m_loginWindowSeconds = 300; // 失败计数窗口(秒)
    qint64 m_loginLockSeconds = 900;   // 锁定时长(秒)
    int m_globalMaxPerMinute = 600;    // 每 IP 每分钟最大请求数
};

// 黑名单插件
class BlacklistPlugin : public AuthPlugin
{
public:
    explicit BlacklistPlugin(const QJsonObject &cfg);

    QString name() const override { return QStringLiteral("blacklist"); }
    std::optional<QHttpServerResponse> onRequest(const RequestContext &ctx) override;

    bool isIpBlocked(const QString &ip) const;
    bool isUsernameBlocked(const QString &username) const;
    bool isTokenBlocked(const QByteArray &token) const;

private:
    QSet<QString> m_blockedIps;
    QSet<QString> m_blockedUsernames;
    QSet<QByteArray> m_blockedTokens;
};

// 插件注册表(内置插件工厂在此注册; 扩展插件照此办理)
namespace Plugins {
using Factory = std::function<std::unique_ptr<AuthPlugin>(const QJsonObject &cfg)>;
QHash<QString, Factory> &registry();
void registerBuiltins();
} // namespace Plugins
