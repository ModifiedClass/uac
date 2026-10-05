#ifndef STORE_H
#define STORE_H

#include "models.h"

#include <QHash>
#include <QMutex>
#include <QMutexLocker>
#include <optional>

/* ============================================================
 *  存储抽象接口
 *  生产环境：PostgreSQL（用户/客户端） + Redis（授权码/会话）
 * ============================================================ */

class IUserStore {
public:
    virtual ~IUserStore() = default;
    virtual void add(const UserInfo &u) = 0;
    virtual std::optional<UserInfo> findByUsername(const QString &username) = 0;
    virtual std::optional<UserInfo> findById(const QString &userId) = 0;
};

class IClientStore {
public:
    virtual ~IClientStore() = default;
    virtual void add(const ClientInfo &c) = 0;
    virtual std::optional<ClientInfo> findById(const QString &clientId) = 0;
};

class IAuthCodeStore {
public:
    virtual ~IAuthCodeStore() = default;
    virtual void save(const AuthCode &c) = 0;
    /// 取出并立即删除（一次性语义）
    virtual std::optional<AuthCode> consume(const QString &code) = 0;
    virtual void purgeExpired() = 0;
};

class ISessionStore {
public:
    virtual ~ISessionStore() = default;
    virtual void save(const Session &s) = 0;
    virtual std::optional<Session> get(const QString &sessionId) = 0;
    virtual void remove(const QString &sessionId) = 0;
    virtual void purgeExpired() = 0;
};

/* ===================== 内存实现（仅演示） ===================== */

class InMemoryUserStore : public IUserStore {
public:
    void add(const UserInfo &u) override;
    std::optional<UserInfo> findByUsername(const QString &username) override;
    std::optional<UserInfo> findById(const QString &userId) override;
private:
    QMutex m_mutex;
    QHash<QString, UserInfo> m_byName;
    QHash<QString, UserInfo> m_byId;
};

class InMemoryClientStore : public IClientStore {
public:
    void add(const ClientInfo &c) override;
    std::optional<ClientInfo> findById(const QString &clientId) override;
private:
    QMutex m_mutex;
    QHash<QString, ClientInfo> m_map;
};

class InMemoryAuthCodeStore : public IAuthCodeStore {
public:
    void save(const AuthCode &c) override;
    std::optional<AuthCode> consume(const QString &code) override;
    void purgeExpired() override;
private:
    QMutex m_mutex;
    QHash<QString, AuthCode> m_map;
};

class InMemorySessionStore : public ISessionStore {
public:
    void save(const Session &s) override;
    std::optional<Session> get(const QString &sessionId) override;
    void remove(const QString &sessionId) override;
    void purgeExpired() override;
private:
    QMutex m_mutex;
    QHash<QString, Session> m_map;
};

#endif // STORE_H