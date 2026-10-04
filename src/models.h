#ifndef MODELS_H
#define MODELS_H

#include <QByteArray>
#include <QString>
#include <QStringList>

struct UserInfo {
    QString      userId;
    QString      username;
    QString      name;
    QString      phone;
    QString      email;
    QStringList  roles;
    QByteArray   passwordHash;   // 格式: sha256$saltHex$hashHex（演示用）
};

struct ClientInfo {
    QString      clientId;
    QString      clientSecret;
    QStringList  redirectUris;
    QString      name;
};

struct AuthCode {
    QString code;
    QString clientId;
    QString redirectUri;
    QString userId;
    QString scope;
    qint64  expiresAt = 0;       // Unix 秒
};

struct Session {
    QString sessionId;
    QString userId;
    qint64  expiresAt = 0;       // Unix 秒
};

#endif // MODELS_H