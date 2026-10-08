#include "jwt.h"
#include "util.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QJsonDocument>
#include <QJsonParseError>

JwtHandler::JwtHandler(const QByteArray &secret)
    : m_secret(secret)
{
    // 由 config 层保证非空(未配置时给出演示默认值并告警), 这里仅兜底
    if (m_secret.isEmpty())
        m_secret = QByteArrayLiteral("dev-only-insecure-secret-change-me");
}

void JwtHandler::setDefaultTtlSeconds(qint64 ttl)
{
    m_defaultTtlSeconds = ttl > 0 ? ttl : 3600;
}

// RFC 2104 HMAC-SHA256
QByteArray JwtHandler::hmacSha256(const QByteArray &key, const QByteArray &message)
{
    const int blockSize = 64;
    QByteArray k = key;
    if (k.size() > blockSize)
        k = QCryptographicHash::hash(k, QCryptographicHash::Sha256);
    k = k.leftJustified(blockSize, '\0');

    QByteArray ipad(blockSize, char(0x36));
    QByteArray opad(blockSize, char(0x5c));
    for (int i = 0; i < blockSize; ++i) {
        ipad[i] = char(ipad.at(i) ^ k.at(i));
        opad[i] = char(opad.at(i) ^ k.at(i));
    }
    const QByteArray inner = QCryptographicHash::hash(ipad + message, QCryptographicHash::Sha256);
    return QCryptographicHash::hash(opad + inner, QCryptographicHash::Sha256);
}

QByteArray JwtHandler::b64UrlEncode(const QByteArray &in)
{
    return in.toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals);
}

QByteArray JwtHandler::b64UrlDecode(const QByteArray &in, bool *ok)
{
    QByteArray data = in;
    data.replace('-', '+');
    data.replace('_', '/');
    while (data.size() % 4 != 0)
        data.append('=');
    const QByteArray out = QByteArray::fromBase64(data);
    if (ok)
        *ok = !out.isEmpty() || in.isEmpty();
    return out;
}

QByteArray JwtHandler::createToken(const QJsonObject &claims, qint64 ttlSeconds) const
{
    QJsonObject payload = claims;
    const qint64 now = QDateTime::currentSecsSinceEpoch();
    if (!payload.contains(QStringLiteral("iat")))
        payload.insert(QStringLiteral("iat"), now);
    if (!payload.contains(QStringLiteral("exp")))
        payload.insert(QStringLiteral("exp"), now + (ttlSeconds > 0 ? ttlSeconds : m_defaultTtlSeconds));

    const QByteArray header = QJsonDocument(QJsonObject{
        {QStringLiteral("alg"), QStringLiteral("HS256")},
        {QStringLiteral("typ"), QStringLiteral("JWT")},
    }).toJson(QJsonDocument::Compact);
    const QByteArray payloadJson = QJsonDocument(payload).toJson(QJsonDocument::Compact);
    const QByteArray signingInput = b64UrlEncode(header) + '.' + b64UrlEncode(payloadJson);
    return signingInput + '.' + b64UrlEncode(hmacSha256(m_secret, signingInput));
}

bool JwtHandler::verify(const QByteArray &token, QJsonObject *payloadOut, QString *error) const
{
    const auto fail = [error](const QString &msg) {
        if (error)
            *error = msg;
        return false;
    };

    const auto parts = token.split('.');
    if (parts.size() != 3)
        return fail(QStringLiteral("令牌格式错误"));

    bool ok = false;
    const QByteArray headerJson = b64UrlDecode(parts.at(0), &ok);
    if (!ok)
        return fail(QStringLiteral("令牌编码错误"));
    const QByteArray payloadJson = b64UrlDecode(parts.at(1), &ok);
    if (!ok)
        return fail(QStringLiteral("令牌编码错误"));

    QJsonParseError perr{};
    const QJsonDocument headerDoc = QJsonDocument::fromJson(headerJson, &perr);
    if (!headerDoc.isObject())
        return fail(QStringLiteral("令牌头部无效"));
    if (headerDoc.object().value(QStringLiteral("alg")).toString() != QLatin1String("HS256"))
        return fail(QStringLiteral("不支持的签名算法"));

    // 签名校验(常量时间比较)
    const QByteArray expectedSig = hmacSha256(m_secret, parts.at(0) + '.' + parts.at(1));
    const QByteArray providedSig = b64UrlDecode(parts.at(2), &ok);
    if (!ok || !constantTimeEquals(providedSig, expectedSig))
        return fail(QStringLiteral("签名校验失败"));

    const QJsonDocument payloadDoc = QJsonDocument::fromJson(payloadJson, &perr);
    if (!payloadDoc.isObject())
        return fail(QStringLiteral("令牌载荷无效"));
    const QJsonObject payload = payloadDoc.object();

    // 有效期校验
    const qint64 now = QDateTime::currentSecsSinceEpoch();
    if (payload.contains(QStringLiteral("exp"))
        && payload.value(QStringLiteral("exp")).toVariant().toLongLong() <= now)
        return fail(QStringLiteral("令牌已过期"));
    if (payload.contains(QStringLiteral("nbf"))
        && payload.value(QStringLiteral("nbf")).toVariant().toLongLong() > now)
        return fail(QStringLiteral("令牌尚未生效"));

    if (payloadOut)
        *payloadOut = payload;
    return true;
}
