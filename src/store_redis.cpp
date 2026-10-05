#include "store_redis.h"
#include "crypto.h"

#include <QDateTime>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLoggingCategory>
#include <QUrl>

#include <hiredis/hiredis.h>

Q_LOGGING_CATEGORY(lcRedis, "uac.store.redis")

/**
 * @brief 解析 Redis URL：redis://[:password@]host:port[/db]
 */
static bool parseRedisUrl(const QString &redisUrl, QString *host, int *port,
                          QString *password, int *db)
{
    QUrl url(redisUrl);
    if (!url.isValid()) return false;

    *host     = url.host();
    *port     = url.port(6379);
    *password = url.password();
    *db       = 0;

    const QString path = url.path();
    if (!path.isEmpty() && path != QLatin1String("/")) {
        bool ok = false;
        const int d = path.mid(1).toInt(&ok);
        if (ok) *db = d;
    }
    return true;
}

/**
 * @brief 建立 Redis 连接并选择 DB / AUTH
 */
static redisContext *connectRedis(const QString &redisUrl, int *outDb)
{
    QString host, password;
    int port, db;
    if (!parseRedisUrl(redisUrl, &host, &port, &password, &db)) {
        qCritical(lcRedis) << "无效的 Redis URL:" << redisUrl;
        throw std::runtime_error("Invalid Redis URL");
    }
    *outDb = db;

    redisContext *ctx = redisConnect(host.toUtf8().constData(), port);
    if (!ctx || ctx->err) {
        qCritical(lcRedis) << "Redis 连接失败:" << (ctx ? ctx->errstr : "alloc error");
        throw std::runtime_error("Redis connection failed");
    }

    if (!password.isEmpty()) {
        redisReply *reply = static_cast<redisReply *>(
            redisCommand(ctx, "AUTH %s", password.toUtf8().constData()));
        if (!reply || reply->type == REDIS_REPLY_ERROR) {
            qCritical(lcRedis) << "Redis AUTH 失败";
            freeReplyObject(reply);
            throw std::runtime_error("Redis AUTH failed");
        }
        freeReplyObject(reply);
    }

    if (db != 0) {
        redisReply *reply = static_cast<redisReply *>(
            redisCommand(ctx, "SELECT %d", db));
        freeReplyObject(reply);
    }

    qInfo(lcRedis) << "Redis 已连接:" << host << port << "db=" << db;
    return ctx;
}

/* ============================================================
 *  RedisAuthCodeStore
 * ============================================================ */

RedisAuthCodeStore::RedisAuthCodeStore(const QString &redisUrl)
{
    m_ctx = connectRedis(redisUrl, &m_dbIndex);
}

RedisAuthCodeStore::~RedisAuthCodeStore()
{
    if (m_ctx) redisFree(m_ctx);
}

QByteArray RedisAuthCodeStore::serialize(const AuthCode &c) const
{
    QJsonObject o{
        {"code",        c.code},
        {"clientId",    c.clientId},
        {"redirectUri", c.redirectUri},
        {"userId",      c.userId},
        {"scope",       c.scope},
        {"expiresAt",   static_cast<qint64>(c.expiresAt)},
        };
    return QJsonDocument(o).toJson(QJsonDocument::Compact);
}

std::optional<AuthCode> RedisAuthCodeStore::deserialize(const QByteArray &data) const
{
    const QJsonObject o = QJsonDocument::fromJson(data).object();
    if (o.isEmpty()) return std::nullopt;

    AuthCode c;
    c.code        = o.value("code").toString();
    c.clientId    = o.value("clientId").toString();
    c.redirectUri = o.value("redirectUri").toString();
    c.userId      = o.value("userId").toString();
    c.scope       = o.value("scope").toString();
    c.expiresAt   = o.value("expiresAt").toVariant().toLongLong();
    return c;
}

void RedisAuthCodeStore::save(const AuthCode &c)
{
    QMutexLocker lock(&m_mutex);
    const QByteArray key = ("uac:code:" + c.code).toUtf8();
    const QByteArray val = serialize(c);

    redisReply *reply = static_cast<redisReply *>(
        redisCommand(m_ctx, "SETEX %s %lld %b",
                     key.constData(),
                     static_cast<long long>(c.expiresAt - QDateTime::currentSecsSinceEpoch()),
                     val.constData(), static_cast<size_t>(val.size())));
    if (!reply || reply->type == REDIS_REPLY_ERROR)
        qWarning(lcRedis) << "Redis SETEX 失败";
    freeReplyObject(reply);
}

std::optional<AuthCode> RedisAuthCodeStore::consume(const QString &code)
{
    QMutexLocker lock(&m_mutex);
    const QByteArray key = ("uac:code:" + code).toUtf8();

    // Lua 脚本：GET + DEL 原子操作
    const char *script =
        "local v = redis.call('GET', KEYS[1]) "
        "if v then redis.call('DEL', KEYS[1]) end "
        "return v";

    redisReply *reply = static_cast<redisReply *>(
        redisCommand(m_ctx, "EVAL %s 1 %s", script, key.constData()));

    std::optional<AuthCode> result;
    if (reply && reply->type == REDIS_REPLY_STRING) {
        result = deserialize(QByteArray(reply->str, static_cast<int>(reply->len)));
    }
    freeReplyObject(reply);

    if (result && result->expiresAt < QDateTime::currentSecsSinceEpoch())
        return std::nullopt;
    return result;
}

void RedisAuthCodeStore::purgeExpired()
{
    // Redis TTL 自动过期，无需手动清理
}

/* ============================================================
 *  RedisSessionStore
 * ============================================================ */

RedisSessionStore::RedisSessionStore(const QString &redisUrl)
{
    m_ctx = connectRedis(redisUrl, &m_dbIndex);
}

RedisSessionStore::~RedisSessionStore()
{
    if (m_ctx) redisFree(m_ctx);
}

void RedisSessionStore::save(const Session &s)
{
    QMutexLocker lock(&m_mutex);
    const QByteArray key = ("uac:session:" + s.sessionId).toUtf8();

    QJsonObject o{
        {"sessionId", s.sessionId},
        {"userId",    s.userId},
        {"expiresAt", static_cast<qint64>(s.expiresAt)},
        };
    const QByteArray val = QJsonDocument(o).toJson(QJsonDocument::Compact);

    redisReply *reply = static_cast<redisReply *>(
        redisCommand(m_ctx, "SETEX %s %lld %b",
                     key.constData(),
                     static_cast<long long>(s.expiresAt - QDateTime::currentSecsSinceEpoch()),
                     val.constData(), static_cast<size_t>(val.size())));
    if (!reply || reply->type == REDIS_REPLY_ERROR)
        qWarning(lcRedis) << "Redis SETEX 失败";
    freeReplyObject(reply);
}

std::optional<Session> RedisSessionStore::get(const QString &sessionId)
{
    QMutexLocker lock(&m_mutex);
    const QByteArray key = ("uac:session:" + sessionId).toUtf8();

    redisReply *reply = static_cast<redisReply *>(
        redisCommand(m_ctx, "GET %s", key.constData()));

    std::optional<Session> result;
    if (reply && reply->type == REDIS_REPLY_STRING) {
        const QJsonObject o =
            QJsonDocument::fromJson(QByteArray(reply->str, static_cast<int>(reply->len))).object();
        if (!o.isEmpty()) {
            Session s;
            s.sessionId = o.value("sessionId").toString();
            s.userId    = o.value("userId").toString();
            s.expiresAt = o.value("expiresAt").toVariant().toLongLong();
            if (s.expiresAt > QDateTime::currentSecsSinceEpoch())
                result = s;
        }
    }
    freeReplyObject(reply);
    return result;
}

void RedisSessionStore::remove(const QString &sessionId)
{
    QMutexLocker lock(&m_mutex);
    const QByteArray key = ("uac:session:" + sessionId).toUtf8();
    redisReply *reply = static_cast<redisReply *>(
        redisCommand(m_ctx, "DEL %s", key.constData()));
    freeReplyObject(reply);
}

void RedisSessionStore::purgeExpired()
{
    // Redis TTL 自动过期
}