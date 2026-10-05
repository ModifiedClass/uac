#ifndef STORE_PG_H
#define STORE_PG_H

#include "store.h"
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSqlError>
#include <QMutex>

/**
 * @brief PostgreSQL 用户存储实现
 *        连接串格式：postgresql://user:pass@host:port/dbname
 */
class PgUserStore : public IUserStore
{
public:
    explicit PgUserStore(const QString &connStr);
    ~PgUserStore() override;

    void add(const UserInfo &u) override;
    std::optional<UserInfo> findByUsername(const QString &username) override;
    std::optional<UserInfo> findById(const QString &userId) override;

    /// 首次启动时从 config.json 导入用户（幂等）
    void importFromConfig(const QJsonArray &users);

private:
    bool ensureTable();

    QSqlDatabase m_db;
    QMutex       m_mutex;
};

/**
 * @brief PostgreSQL 客户端存储实现
 */
class PgClientStore : public IClientStore
{
public:
    explicit PgClientStore(const QString &connStr);
    ~PgClientStore() override;

    void add(const ClientInfo &c) override;
    std::optional<ClientInfo> findById(const QString &clientId) override;

    void importFromConfig(const QJsonArray &clients);

private:
    bool ensureTable();

    QSqlDatabase m_db;
    QMutex       m_mutex;
};

#endif // STORE_PG_H