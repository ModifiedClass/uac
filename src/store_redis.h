#ifndef STORE_REDIS_H
#define STORE_REDIS_H

#include "store.h"
#include <QMutex>
#include <hiredis/hiredis.h>

/**
 * @brief Redis 授权码存储实现
 *        使用 SETEX 设置 TTL，GET+DEL 原子消费
 */
class RedisAuthCodeStore : public IAuthCodeStore
{
public:
    explicit RedisAuthCodeStore(const QString &redisUrl);
    ~RedisAuthCodeStore() override;

    void save(const AuthCode &c) override;
    std::optional<AuthCode> consume(const QString &code) override;
    void purgeExpired() override;   // Redis TTL 自动处理

private:
    redisContext *m_ctx = nullptr;
    QMutex        m_mutex;
    int           m_dbIndex = 0;

    QByteArray serialize(const AuthCode &c) const;
    std::optional<AuthCode> deserialize(const QByteArray &data) const;
};

/**
 * @brief Redis 会话存储实现
 */
class RedisSessionStore : public ISessionStore
{
public:
    explicit RedisSessionStore(const QString &redisUrl);
    ~RedisSessionStore() override;

    void save(const Session &s) override;
    std::optional<Session> get(const QString &sessionId) override;
    void remove(const QString &sessionId) override;
    void purgeExpired() override;

private:
    redisContext *m_ctx = nullptr;
    QMutex        m_mutex;
    int           m_dbIndex = 0;
};

#endif // STORE_REDIS_H