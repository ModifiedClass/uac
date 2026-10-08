#pragma once
// 存储抽象层(用户/客户端/授权码/会话)。
//
// 示例/测试环境: 内存实现(Memory*Store, 本文件)。
// 生产环境: 用户/客户端 -> PostgreSQL(见 db/init.sql);
//           授权码/会话 -> Redis(支持多副本部署)。
// 替换方式: 实现同名接口并在 AuthServer::init() 中替换实例, 路由逻辑零改动。

#include <QByteArray>
#include <QHash>
#include <QMutex>
#include <QString>
#include <QStringList>
#include <optional>

// ---------------- 数据模型(与设计文档 6.x 对应) ----------------

struct UserInfo {
    QString userId;          // 用户唯一 ID
    QString username;        // 登录名
    QString name;            // 显示名称
    QString phone;           // 手机号码
    QString email;           // 邮箱
    QStringList roles;       // 角色列表
    QByteArray passwordHash; // 密码哈希(禁止明文)
};

struct ClientInfo {
    QString clientId;        // 客户端 ID
    QString clientSecret;    // 客户端密钥(生产建议存储哈希)
    QStringList redirectUris;// 允许的回调地址白名单
    QString name;            // 客户端名称
};

struct AuthCode {
    QString code;
    QString clientId;        // 签发时绑定的客户端
    QString redirectUri;     // 签发时绑定的回调地址
    QString userId;          // 授权用户
    QString scope;           // 授权范围
    qint64 expiresAt = 0;    // 过期时间戳(unix 秒, 默认 5 分钟)
};

struct Session {
    QString sessionId;
    QString userId;
    qint64 expiresAt = 0;    // 过期时间戳(unix 秒, 默认 24 小时)
};

// ---------------- 接口 ----------------

class UserStore
{
public:
    virtual ~UserStore() = default;
    virtual bool add(const UserInfo &user) = 0;
    virtual std::optional<UserInfo> findByUsername(const QString &username) const = 0;
    virtual std::optional<UserInfo> findById(const QString &userId) const = 0;
    virtual QList<UserInfo> all() const = 0;
};

class ClientStore
{
public:
    virtual ~ClientStore() = default;
    virtual bool add(const ClientInfo &client) = 0;
    virtual std::optional<ClientInfo> findById(const QString &clientId) const = 0;
};

class AuthCodeStore
{
public:
    virtual ~AuthCodeStore() = default;
    virtual void setTtlSeconds(qint64 ttl) = 0;
    virtual QString issue(const QString &clientId, const QString &redirectUri,
                          const QString &userId, const QString &scope) = 0;
    // 一次性使用: 取出即删除(并发下同一 code 只会被消费一次)
    virtual std::optional<AuthCode> consume(const QString &code) = 0;
    virtual void purgeExpired() = 0;
};

class SessionStore
{
public:
    virtual ~SessionStore() = default;
    virtual void setTtlSeconds(qint64 ttl) = 0;
    virtual QString create(const QString &userId) = 0;
    virtual std::optional<Session> find(const QString &sessionId) = 0;
    virtual bool remove(const QString &sessionId) = 0;
    virtual void purgeExpired() = 0;
};

// ---------------- 内存实现(示例/测试环境) ----------------

class MemoryUserStore final : public UserStore
{
public:
    bool add(const UserInfo &user) override;
    std::optional<UserInfo> findByUsername(const QString &username) const override;
    std::optional<UserInfo> findById(const QString &userId) const override;
    QList<UserInfo> all() const override;

private:
    mutable QMutex m_mutex;
    QHash<QString, UserInfo> m_byName;
    QHash<QString, UserInfo> m_byId;
};

class MemoryClientStore final : public ClientStore
{
public:
    bool add(const ClientInfo &client) override;
    std::optional<ClientInfo> findById(const QString &clientId) const override;

private:
    mutable QMutex m_mutex;
    QHash<QString, ClientInfo> m_byId;
};

class MemoryAuthCodeStore final : public AuthCodeStore
{
public:
    void setTtlSeconds(qint64 ttl) override;
    QString issue(const QString &clientId, const QString &redirectUri,
                  const QString &userId, const QString &scope) override;
    std::optional<AuthCode> consume(const QString &code) override;
    void purgeExpired() override;

private:
    mutable QMutex m_mutex;
    QHash<QString, AuthCode> m_map;
    qint64 m_ttl = 300;
};

class MemorySessionStore final : public SessionStore
{
public:
    void setTtlSeconds(qint64 ttl) override;
    QString create(const QString &userId) override;
    std::optional<Session> find(const QString &sessionId) override;
    bool remove(const QString &sessionId) override;
    void purgeExpired() override;

private:
    mutable QMutex m_mutex;
    QHash<QString, Session> m_map;
    qint64 m_ttl = 86400;
};
