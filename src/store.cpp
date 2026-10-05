#include "store.h"
#include <QDateTime>

/* ---------------- InMemoryUserStore ---------------- */

void InMemoryUserStore::add(const UserInfo &u)
{
    QMutexLocker lock(&m_mutex);
    m_byName.insert(u.username, u);
    m_byId.insert(u.userId, u);
}

std::optional<UserInfo> InMemoryUserStore::findByUsername(const QString &username)
{
    QMutexLocker lock(&m_mutex);
    auto it = m_byName.constFind(username);
    if (it == m_byName.cend()) return std::nullopt;
    return *it;
}

std::optional<UserInfo> InMemoryUserStore::findById(const QString &userId)
{
    QMutexLocker lock(&m_mutex);
    auto it = m_byId.constFind(userId);
    if (it == m_byId.cend()) return std::nullopt;
    return *it;
}

/* ---------------- InMemoryClientStore ---------------- */

void InMemoryClientStore::add(const ClientInfo &c)
{
    QMutexLocker lock(&m_mutex);
    m_map.insert(c.clientId, c);
}

std::optional<ClientInfo> InMemoryClientStore::findById(const QString &clientId)
{
    QMutexLocker lock(&m_mutex);
    auto it = m_map.constFind(clientId);
    if (it == m_map.cend()) return std::nullopt;
    return *it;
}

/* ---------------- InMemoryAuthCodeStore ---------------- */

void InMemoryAuthCodeStore::save(const AuthCode &c)
{
    QMutexLocker lock(&m_mutex);
    m_map.insert(c.code, c);
}

std::optional<AuthCode> InMemoryAuthCodeStore::consume(const QString &code)
{
    QMutexLocker lock(&m_mutex);
    auto it = m_map.find(code);
    if (it == m_map.end()) return std::nullopt;
    const AuthCode c = *it;
    m_map.erase(it);            // 一次性：取出即删除
    return c;
}

void InMemoryAuthCodeStore::purgeExpired()
{
    QMutexLocker lock(&m_mutex);
    const qint64 now = QDateTime::currentSecsSinceEpoch();
    for (auto it = m_map.begin(); it != m_map.end(); ) {
        if (it->expiresAt < now) it = m_map.erase(it);
        else ++it;
    }
}

/* ---------------- InMemorySessionStore ---------------- */

void InMemorySessionStore::save(const Session &s)
{
    QMutexLocker lock(&m_mutex);
    m_map.insert(s.sessionId, s);
}

std::optional<Session> InMemorySessionStore::get(const QString &sessionId)
{
    QMutexLocker lock(&m_mutex);
    auto it = m_map.constFind(sessionId);
    if (it == m_map.cend()) return std::nullopt;
    if (it->expiresAt < QDateTime::currentSecsSinceEpoch()) {
        m_map.remove(sessionId);
        return std::nullopt;
    }
    return *it;
}

void InMemorySessionStore::remove(const QString &sessionId)
{
    QMutexLocker lock(&m_mutex);
    m_map.remove(sessionId);
}

void InMemorySessionStore::purgeExpired()
{
    QMutexLocker lock(&m_mutex);
    const qint64 now = QDateTime::currentSecsSinceEpoch();
    for (auto it = m_map.begin(); it != m_map.end(); ) {
        if (it->expiresAt < now) it = m_map.erase(it);
        else ++it;
    }
}