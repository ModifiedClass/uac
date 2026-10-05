#include "crypto.h"
#include "bcrypt.h"

#include <QCryptographicHash>
#include <QMessageAuthenticationCode>
#include <QRandomGenerator>
#include <QStringList>

namespace {

/// 生成密码学安全随机字节
QByteArray randomBytesImpl(int n)
{
    QByteArray out(n, Qt::Uninitialized);
    QRandomGenerator *gen = QRandomGenerator::system();
    for (int i = 0; i < n; ++i)
        out[i] = static_cast<char>(gen->bounded(256));
    return out;
}

} // namespace

namespace Crypto {

QByteArray randomBytes(int n) { return randomBytesImpl(n); }

QString randomToken(int nbytes)
{
    return QString::fromLatin1(base64UrlEncode(randomBytesImpl(nbytes)));
}

QByteArray sha256(const QByteArray &data)
{
    return QCryptographicHash::hash(data, QCryptographicHash::Sha256);
}

QString sha256Hex(const QByteArray &data)
{
    return QString::fromLatin1(sha256(data).toHex());
}

QByteArray hmacSha256(const QByteArray &key, const QByteArray &data)
{
    QMessageAuthenticationCode mac(QCryptographicHash::Sha256, key);
    mac.addData(data);
    return mac.result();
}

bool constantTimeEquals(const QByteArray &a, const QByteArray &b)
{
    if (a.size() != b.size())
        return false;
    unsigned char diff = 0;
    for (int i = 0; i < a.size(); ++i)
        diff |= static_cast<unsigned char>(a[i] ^ b[i]);
    return diff == 0;
}

QString bcryptHash(const QString &password, int cost)
{
    return BCrypt::hash(password, cost);
}

bool bcryptVerify(const QString &password, const QString &stored)
{
    return BCrypt::verify(password, stored);
}

QString sha256HashPassword(const QString &password, const QByteArray &salt)
{
    const QByteArray s = salt.isEmpty() ? randomBytesImpl(16) : salt;
    const QByteArray h = sha256(s + password.toUtf8());
    return QStringLiteral("sha256$%1$%2")
        .arg(QString::fromLatin1(s.toHex()), QString::fromLatin1(h.toHex()));
}

bool sha256VerifyPassword(const QString &password, const QString &stored)
{
    const QStringList parts = stored.split(QLatin1Char('$'));
    if (parts.size() != 3 || parts.at(0) != QLatin1String("sha256"))
        return false;
    const QByteArray salt   = QByteArray::fromHex(parts.at(1).toLatin1());
    const QByteArray expect = QByteArray::fromHex(parts.at(2).toLatin1());
    return constantTimeEquals(sha256(salt + password.toUtf8()), expect);
}

QByteArray base64UrlEncode(const QByteArray &data)
{
    return data.toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals);
}

QByteArray base64UrlDecode(const QByteArray &data)
{
    QByteArray in = data;
    while (in.size() % 4)
        in.append('=');
    return QByteArray::fromBase64(in, QByteArray::Base64UrlEncoding);
}

} // namespace Crypto