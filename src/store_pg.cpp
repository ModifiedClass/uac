#include "store_pg.h"
#include "crypto.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QLoggingCategory>
#include <QUrl>

Q_LOGGING_CATEGORY(lcPg, "uac.store.pg")

/**
 * @brief 解析 PostgreSQL 连接串
 *        postgresql://user:pass@host:port/dbname
 */
static void parseConnStr(const QString &connStr, QString *host, int *port,
                         QString *db, QString *user, QString *pass)
{
    QUrl url(connStr);
    *host = url.host();
    *port = url.port(5432);
    *db   = url.path().mid(1);
    *user = url.userName();
    *pass = url.password();
}

/* ============================================================
 *  PgUserStore
 * ============================================================ */

PgUserStore::PgUserStore(const QString &connStr)
{
    QString host, db, user, pass;
    int port;
    parseConnStr(connStr, &host, &port, &db, &user, &pass);

    m_db = QSqlDatabase::addDatabase("QPSQL", "pg_users");
    m_db.setHostName(host);
    m_db.setPort(port);
    m_db.setDatabaseName(db);
    m_db.setUserName(user);
    m_db.setPassword(pass);

    if (!m_db.open()) {
        qCritical(lcPg) << "PostgreSQL 连接失败:" << m_db.lastError().text();
        throw std::runtime_error("PostgreSQL connection failed");
    }
    qInfo(lcPg) << "PostgreSQL 已连接:" << host << port << db;

    ensureTable();
}

PgUserStore::~PgUserStore()
{
    if (m_db.isOpen())
        m_db.close();
    QSqlDatabase::removeDatabase("pg_users");
}

bool PgUserStore::ensureTable()
{
    QSqlQuery q(m_db);
    const QString ddl = R"(
        CREATE TABLE IF NOT EXISTS users (
            user_id       VARCHAR(64) PRIMARY KEY,
            username      VARCHAR(64) UNIQUE NOT NULL,
            password_hash VARCHAR(255) NOT NULL,
            name          VARCHAR(128) NOT NULL,
            phone         VARCHAR(32),
            email         VARCHAR(128),
            roles         TEXT[] NOT NULL DEFAULT '{}',
            created_at    TIMESTAMPTZ NOT NULL DEFAULT NOW()
        )
    )";
    if (!q.exec(ddl)) {
        qCritical(lcPg) << "建表失败:" << q.lastError().text();
        return false;
    }
    return true;
}

void PgUserStore::add(const UserInfo &u)
{
    QMutexLocker lock(&m_mutex);
    QSqlQuery q(m_db);
    q.prepare("INSERT INTO users (user_id, username, password_hash, name, phone, email, roles) "
              "VALUES (:uid, :uname, :phash, :name, :phone, :email, :roles) "
              "ON CONFLICT (user_id) DO UPDATE SET "
              "username=:uname, password_hash=:phash, name=:name, "
              "phone=:phone, email=:email, roles=:roles");
    q.bindValue(":uid",   u.userId);
    q.bindValue(":uname", u.username);
    q.bindValue(":phash", QString::fromUtf8(u.passwordHash));
    q.bindValue(":name",  u.name);
    q.bindValue(":phone", u.phone);
    q.bindValue(":email", u.email);
    q.bindValue(":roles", u.roles.join(QLatin1Char(',')));
    if (!q.exec())
        qWarning(lcPg) << "插入用户失败:" << q.lastError().text();
}

std::optional<UserInfo> PgUserStore::findByUsername(const QString &username)
{
    QMutexLocker lock(&m_mutex);
    QSqlQuery q(m_db);
    q.prepare("SELECT user_id, username, password_hash, name, phone, email, roles "
              "FROM users WHERE username = :u");
    q.bindValue(":u", username);
    if (!q.exec() || !q.next())
        return std::nullopt;

    UserInfo u;
    u.userId       = q.value("user_id").toString();
    u.username     = q.value("username").toString();
    u.passwordHash = q.value("password_hash").toString().toUtf8();
    u.name         = q.value("name").toString();
    u.phone        = q.value("phone").toString();
    u.email        = q.value("email").toString();
    u.roles        = q.value("roles").toString().split(QLatin1Char(','), Qt::SkipEmptyParts);
    return u;
}

std::optional<UserInfo> PgUserStore::findById(const QString &userId)
{
    QMutexLocker lock(&m_mutex);
    QSqlQuery q(m_db);
    q.prepare("SELECT user_id, username, password_hash, name, phone, email, roles "
              "FROM users WHERE user_id = :id");
    q.bindValue(":id", userId);
    if (!q.exec() || !q.next())
        return std::nullopt;

    UserInfo u;
    u.userId       = q.value("user_id").toString();
    u.username     = q.value("username").toString();
    u.passwordHash = q.value("password_hash").toString().toUtf8();
    u.name         = q.value("name").toString();
    u.phone        = q.value("phone").toString();
    u.email        = q.value("email").toString();
    u.roles        = q.value("roles").toString().split(QLatin1Char(','), Qt::SkipEmptyParts);
    return u;
}

void PgUserStore::importFromConfig(const QJsonArray &users)
{
    for (const auto &v : users) {
        const QJsonObject o = v.toObject();
        UserInfo u;
        u.userId   = o.value("userId").toString();
        u.username = o.value("username").toString();
        u.name     = o.value("name").toString(u.username);
        u.phone    = o.value("phone").toString();
        u.email    = o.value("email").toString();
        for (const auto &r : o.value("roles").toArray())
            u.roles << r.toString();

        const QString stored = o.value("passwordHash").toString();
        if (!stored.isEmpty()) {
            u.passwordHash = stored.toUtf8();
        } else {
            const QString plain = o.value("password").toString();
            // 生产环境 bcrypt cost=12
            u.passwordHash = Crypto::bcryptHash(plain, 12).toUtf8();
        }
        add(u);
    }
    qInfo(lcPg) << "已从配置导入" << users.size() << "个用户";
}

/* ============================================================
 *  PgClientStore
 * ============================================================ */

PgClientStore::PgClientStore(const QString &connStr)
{
    QString host, db, user, pass;
    int port;
    parseConnStr(connStr, &host, &port, &db, &user, &pass);

    m_db = QSqlDatabase::addDatabase("QPSQL", "pg_clients");
    m_db.setHostName(host);
    m_db.setPort(port);
    m_db.setDatabaseName(db);
    m_db.setUserName(user);
    m_db.setPassword(pass);

    if (!m_db.open()) {
        qCritical(lcPg) << "PostgreSQL 连接失败:" << m_db.lastError().text();
        throw std::runtime_error("PostgreSQL connection failed");
    }
    ensureTable();
}

PgClientStore::~PgClientStore()
{
    if (m_db.isOpen())
        m_db.close();
    QSqlDatabase::removeDatabase("pg_clients");
}

bool PgClientStore::ensureTable()
{
    QSqlQuery q(m_db);
    const QString ddl = R"(
        CREATE TABLE IF NOT EXISTS clients (
            client_id     VARCHAR(64) PRIMARY KEY,
            client_secret VARCHAR(255) NOT NULL,
            name          VARCHAR(128),
            redirect_uris TEXT[] NOT NULL DEFAULT '{}',
            created_at    TIMESTAMPTZ NOT NULL DEFAULT NOW()
        )
    )";
    if (!q.exec(ddl)) {
        qCritical(lcPg) << "建表失败:" << q.lastError().text();
        return false;
    }
    return true;
}

void PgClientStore::add(const ClientInfo &c)
{
    QMutexLocker lock(&m_mutex);
    QSqlQuery q(m_db);
    q.prepare("INSERT INTO clients (client_id, client_secret, name, redirect_uris) "
              "VALUES (:cid, :secret, :name, :uris) "
              "ON CONFLICT (client_id) DO UPDATE SET "
              "client_secret=:secret, name=:name, redirect_uris=:uris");
    q.bindValue(":cid",    c.clientId);
    q.bindValue(":secret", c.clientSecret);
    q.bindValue(":name",   c.name);
    q.bindValue(":uris",   c.redirectUris.join(QLatin1Char(',')));
    if (!q.exec())
        qWarning(lcPg) << "插入客户端失败:" << q.lastError().text();
}

std::optional<ClientInfo> PgClientStore::findById(const QString &clientId)
{
    QMutexLocker lock(&m_mutex);
    QSqlQuery q(m_db);
    q.prepare("SELECT client_id, client_secret, name, redirect_uris "
              "FROM clients WHERE client_id = :cid");
    q.bindValue(":cid", clientId);
    if (!q.exec() || !q.next())
        return std::nullopt;

    ClientInfo c;
    c.clientId     = q.value("client_id").toString();
    c.clientSecret = q.value("client_secret").toString();
    c.name         = q.value("name").toString();
    c.redirectUris = q.value("redirect_uris").toString().split(QLatin1Char(','), Qt::SkipEmptyParts);
    return c;
}

void PgClientStore::importFromConfig(const QJsonArray &clients)
{
    for (const auto &v : clients) {
        const QJsonObject o = v.toObject();
        ClientInfo c;
        c.clientId     = o.value("clientId").toString();
        c.clientSecret = o.value("clientSecret").toString();
        c.name         = o.value("name").toString();
        for (const auto &r : o.value("redirectUris").toArray())
            c.redirectUris << r.toString();
        add(c);
    }
    qInfo(lcPg) << "已从配置导入" << clients.size() << "个客户端";
}