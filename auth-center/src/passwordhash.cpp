#include "passwordhash.h"
#include "util.h"

#include <QCryptographicHash>
#include <QtCore>

namespace PasswordHasher {

QByteArray randomSaltHex()
{
    return randomHex(16);
}

QByteArray hashPassword(const QString &password)
{
    // ★★ 生产环境必须替换为 bcrypt / argon2(慢哈希), SHA-256 仅用于示例演示 ★★
    const QByteArray salt = randomSaltHex();
    const QByteArray digest = QCryptographicHash::hash(salt + ':' + password.toUtf8(),
                                                        QCryptographicHash::Sha256);
    return QByteArrayLiteral("sha256$") + salt + '$' + digest.toHex();
}

bool verifyPassword(const QString &password, const QByteArray &storedHash)
{
    const auto parts = storedHash.split('$');
    if (parts.size() == 3 && parts.at(0) == "sha256") {
        const QByteArray digest = QCryptographicHash::hash(parts.at(1) + ':' + password.toUtf8(),
                                                           QCryptographicHash::Sha256);
        return constantTimeEquals(digest.toHex(), parts.at(2));
    }
    // 生产: 此处应校验 bcrypt/argon2 格式(如 libsodium argon2id_verify / crypt_checkpass)
    return false;
}

// ---------------- 生产接口占位实现 ----------------
// ★★★ 上生产前必须用真正的 bcrypt/argon2 替换以下两个函数 ★★★
// 推荐: argon2(libsodium) 或 bcrypt(OpenBSD)。替换后无需改动调用方。
QByteArray bcryptHash(const QString &password)
{
    return hashPassword(password);
}

bool bcryptVerify(const QString &password, const QByteArray &hash)
{
    return verifyPassword(password, hash);
}

bool isBcryptFormat(const QByteArray &storedHash)
{
    return storedHash.startsWith("$2a$") || storedHash.startsWith("$2b$")
           || storedHash.startsWith("$argon2");
}

} // namespace PasswordHasher
