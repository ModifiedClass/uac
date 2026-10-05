#include "jwt.h"
#include "crypto.h"

#include <QDateTime>
#include <QJsonDocument>
#include <QVariant>

namespace Jwt {

QString sign(const QJsonObject &payload, const QByteArray &secret)
{
    // JWT Header
    const QJsonObject header{{"alg", "HS256"}, {"typ", "JWT"}};

    // Base64URL 编码 header 与 payload
    const QByteArray h = Crypto::base64UrlEncode(
        QJsonDocument(header).toJson(QJsonDocument::Compact));
    const QByteArray p = Crypto::base64UrlEncode(
        QJsonDocument(payload).toJson(QJsonDocument::Compact));

    // 签名
    const QByteArray signingInput = h + '.' + p;
    const QByteArray sig = Crypto::base64UrlEncode(Crypto::hmacSha256(secret, signingInput));

    return QString::fromLatin1(signingInput + '.' + sig);
}

bool verify(const QString &token, const QByteArray &secret,
            QJsonObject *payloadOut, QString *error)
{
    const QList<QByteArray> parts = token.toLatin1().split('.');
    if (parts.size() != 3) {
        if (error) *error = QStringLiteral("令牌格式错误");
        return false;
    }

    // 1) 签名校验（先验签，避免解析不可信内容）
    const QByteArray signingInput = parts[0] + '.' + parts[1];
    const QByteArray expect = Crypto::base64UrlEncode(Crypto::hmacSha256(secret, signingInput));
    if (!Crypto::constantTimeEquals(expect, parts[2])) {
        if (error) *error = QStringLiteral("签名校验失败");
        return false;
    }

    // 2) alg 校验，防止 alg=none / 算法混淆攻击
    const QJsonObject header =
        QJsonDocument::fromJson(Crypto::base64UrlDecode(parts[0])).object();
    if (header.value("alg").toString() != QLatin1String("HS256")) {
        if (error) *error = QStringLiteral("不支持的签名算法");
        return false;
    }

    // 3) 时间声明校验
    const QJsonObject payload =
        QJsonDocument::fromJson(Crypto::base64UrlDecode(parts[1])).object();
    const qint64 now = QDateTime::currentSecsSinceEpoch();

    if (payload.contains("exp")
        && payload.value("exp").toVariant().toLongLong() < now) {
        if (error) *error = QStringLiteral("令牌已过期");
        return false;
    }
    if (payload.contains("nbf")
        && payload.value("nbf").toVariant().toLongLong() > now) {
        if (error) *error = QStringLiteral("令牌尚未生效");
        return false;
    }

    if (payloadOut) *payloadOut = payload;
    return true;
}

} // namespace Jwt