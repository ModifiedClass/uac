#include "plugins.h"

#include <QDateTime>
#include <QHash>
#include <QJsonArray>
#include <QMutex>
#include <QMutexLocker>
#include <QSet>

/* ============================================================
 *  内置插件 1：黑名单 + 自动封禁
 * ============================================================ */
class BlacklistPlugin : public IAuthPlugin {
public:
    QString name() const override { return QStringLiteral("blacklist"); }

    bool init(const QJsonObject &cfg) override
    {
        for (const auto &v : cfg.value("ips").toArray())
            m_staticIps.insert(v.toString());
        for (const auto &v : cfg.value("usernames").toArray())
            m_staticUsers.insert(v.toString());
        m_threshold  = cfg.value("autoBanThreshold").toInt(5);
        m_banSeconds = cfg.value("autoBanSeconds").toInt(300);
        return true;
    }

    bool beforeLogin(const QString &username, const QString &ip, QString *reason) override
    {
        QMutexLocker lock(&m_mutex);
        purgeLocked();

        if (m_staticIps.contains(ip) || m_staticUsers.contains(username)
            || m_autoBannedIps.contains(ip)) {
            if (reason) *reason = QStringLiteral("该来源已被列入黑名单，请稍后再试");
            return false;
        }
        return true;
    }

    void afterLogin(const QString &username, const QString &ip, bool success) override
    {
        if (success) return;
        QMutexLocker lock(&m_mutex);
        const QString key = ip + QLatin1Char('|') + username;
        const int n = ++m_fails[key];
        if (n >= m_threshold) {
            m_autoBannedIps.insert(ip, QDateTime::currentSecsSinceEpoch() + m_banSeconds);
            m_fails.remove(key);
        }
    }

private:
    void purgeLocked()
    {
        const qint64 now = QDateTime::currentSecsSinceEpoch();
        for (auto it = m_autoBannedIps.begin(); it != m_autoBannedIps.end(); ) {
            if (it.value() < now) it = m_autoBannedIps.erase(it);
            else ++it;
        }
    }

    QMutex m_mutex;
    QSet<QString> m_staticIps;
    QSet<QString> m_staticUsers;
    QHash<QString, int>    m_fails;
    QHash<QString, qint64> m_autoBannedIps;
    int m_threshold  = 5;
    int m_banSeconds = 300;
};

/* ============================================================
 *  内置插件 2：防抖（同一 IP+用户 短时间内重复提交直接拒绝）
 * ============================================================ */
class DebouncePlugin : public IAuthPlugin {
public:
    QString name() const override { return QStringLiteral("debounce"); }

    bool init(const QJsonObject &cfg) override
    {
        m_windowMs = cfg.value("windowMs").toInt(1500);
        return true;
    }

    bool beforeLogin(const QString &username, const QString &ip, QString *reason) override
    {
        QMutexLocker lock(&m_mutex);
        const QString key = ip + QLatin1Char('|') + username;
        const qint64 now  = QDateTime::currentMSecsSinceEpoch();
        const qint64 last = m_lastAttempt.value(key, 0);

        if (now - last < m_windowMs) {
            if (reason) *reason = QStringLiteral("请求过于频繁，请稍后再试");
            return false;
        }
        m_lastAttempt.insert(key, now);

        // 简单清理
        if (m_lastAttempt.size() > 10000) {
            for (auto it = m_lastAttempt.begin(); it != m_lastAttempt.end(); ) {
                if (now - it.value() > m_windowMs * 60) it = m_lastAttempt.erase(it);
                else ++it;
            }
        }
        return true;
    }

private:
    QMutex m_mutex;
    QHash<QString, qint64> m_lastAttempt;
    int m_windowMs = 1500;
};

/* ---------------- PluginManager ---------------- */

void PluginManager::loadFromConfig(const QJsonObject &plugins)
{
    const QJsonObject bl = plugins.value("blacklist").toObject();
    if (bl.value("enabled").toBool(true)) {
        auto p = std::make_unique<BlacklistPlugin>();
        p->init(bl);
        add(std::move(p));
    }

    const QJsonObject db = plugins.value("debounce").toObject();
    if (db.value("enabled").toBool(true)) {
        auto p = std::make_unique<DebouncePlugin>();
        p->init(db);
        add(std::move(p));
    }
}

bool PluginManager::beforeLogin(const QString &username, const QString &ip, QString *reason)
{
    for (auto &p : m_plugins)
        if (!p->beforeLogin(username, ip, reason))
            return false;
    return true;
}

void PluginManager::afterLogin(const QString &username, const QString &ip, bool success)
{
    for (auto &p : m_plugins)
        p->afterLogin(username, ip, success);
}

bool PluginManager::beforeToken(const QString &clientId, const QString &ip, QString *reason)
{
    for (auto &p : m_plugins)
        if (!p->beforeToken(clientId, ip, reason))
            return false;
    return true;
}

QStringList PluginManager::names() const
{
    QStringList out;
    for (const auto &p : m_plugins)
        out << p->name();
    return out;
}