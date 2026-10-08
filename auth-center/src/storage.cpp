#include "storage.h"
#include "util.h"

#include <QDateTime>
#include <QMutexLocker>

namespace {
qint64 nowSecs()
{
    return QDateTime::currentSecsSinceEpoch();
}
} // namespace

// ---------------- MemoryUserStore ----------------

bool MemoryUserStore::add(const UserInfo &user)
{
    if (user.userId.isEmpty() || user.username.isEmpty())
        return false;
    QMutexLocker locker(&m_mutex);
    if (m_byName.contains(user.username) || m_byId.contains(user.userId))
        return false;
    m_byName.insert(user.username, user);
    m_byId.insert(user.userId, user);
    return true;
}

std::optional<UserInfo> MemoryUserStore::findByUsername(const QString &username) const
{
    QMutexLocker locker(&m_mutex);
    const auto it = m_byName.constFind(username);
    if (it == m_byName.constEnd())
        return std::nullopt;
    return it.value();
}

std::optional<UserInfo> MemoryUserStore::findById(const QString &userId) const
{
    QMutexLocker locker(&m_mutex);
    const auto it = m_byId.constFind(userId);
    if (it == m_byId.constEnd())
        return std::nullopt;
    return it.value();
}

QList<UserInfo> MemoryUserStore::all() const
{
    QMutexLocker locker(&m_mutex);
    return m_byId.values();
}

// ---------------- MemoryClientStore ----------------

bool MemoryClientStore::add(const ClientInfo &client)
{
    if (client.clientId.isEmpty())
        return false;
    QMutexLocker locker(&m_mutex);
    if (m_byId.contains(client.clientId))
        return false;
    m_byId.insert(client.clientId, client);
    return true;
}

std::optional<ClientInfo> MemoryClientStore::findById(const QString &clientId) const
{
    QMutexLocker locker(&m_mutex);
    const auto it = m_byId.constFind(clientId);
    if (it == m_byId.constEnd())
        return std::nullopt;
    return it.value();
}

// ---------------- MemoryAuthCodeStore ----------------

void MemoryAuthCodeStore::setTtlSeconds(qint64 ttl)
{
    QMutexLocker locker(&m_mutex);
    m_ttl = ttl > 0 ? ttl : 300;
}

QString MemoryAuthCodeStore::issue(const QString &clientId, const QString &redirectUri,
                                   const QString &userId, const QString &scope)
{
    QMutexLocker locker(&m_mutex);
    QString code;
    do {
        code = QString::fromLatin1(randomHex(32)); // 256bit 随机授权码, 防猜测
    } while (m_map.contains(code));
    m_map.insert(code, AuthCode{code, clientId, redirectUri, userId, scope, nowSecs() + m_ttl});
    return code;
}

std::optional<AuthCode> MemoryAuthCodeStore::consume(const QString &code)
{
    QMutexLocker locker(&m_mutex);
    const auto it = m_map.find(code);
    if (it == m_map.end())
        return std::nullopt;
    if (it->expiresAt <= nowSecs()) { // 过期即销毁
        m_map.erase(it);
        return std::nullopt;
    }
    const AuthCode out = it.value();
    m_map.erase(it); // 一次性使用
    return out;
}

void MemoryAuthCodeStore::purgeExpired()
{
    QMutexLocker locker(&m_mutex);
    const qint64 now = nowSecs();
    for (auto it = m_map.begin(); it != m_map.end();) {
        if (it->expiresAt <= now)
            it = m_map.erase(it);
        else
            ++it;
    }
}

// ---------------- MemorySessionStore ----------------

void MemorySessionStore::setTtlSeconds(qint64 ttl)
{
    QMutexLocker locker(&m_mutex);
    m_ttl = ttl > 0 ? ttl : 86400;
}

QString MemorySessionStore::create(const QString &userId)
{
    QMutexLocker locker(&m_mutex);
    QString id;
    do {
        id = QString::fromLatin1(randomHex(32)); // 256bit 随机会话 ID, 防会话固定/猜测
    } while (m_map.contains(id));
    m_map.insert(id, Session{id, userId, nowSecs() + m_ttl});
    return id;
}

std::optional<Session> MemorySessionStore::find(const QString &sessionId)
{
    QMutexLocker locker(&m_mutex);
    const auto it = m_map.find(sessionId);
    if (it == m_map.end())
        return std::nullopt;
    if (it->expiresAt <= nowSecs()) {
        m_map.erase(it);
        return std::nullopt;
    }
    return it.value();
}

bool MemorySessionStore::remove(const QString &sessionId)
{
    QMutexLocker locker(&m_mutex);
    return m_map.remove(sessionId) > 0;
}

void MemorySessionStore::purgeExpired()
{
    QMutexLocker locker(&m_mutex);
    const qint64 now = nowSecs();
    for (auto it = m_map.begin(); it != m_map.end();) {
        if (it->expiresAt <= now)
            it = m_map.erase(it);
        else
            ++it;
    }
}
