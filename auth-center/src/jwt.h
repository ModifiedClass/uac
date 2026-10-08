#pragma once
// JWT (HS256) 签发与校验。
//
// 示例阶段: HMAC-SHA256 手写实现(本文件), 满足验收标准。
// 生产环境: 建议替换为 jwt-cpp(header-only, 支持 RS256), 认证中心持私钥签发,
//            业务系统用公钥验签。见 docs/PRODUCTION-CHECKLIST.md。

#include <QByteArray>
#include <QJsonObject>

class JwtHandler
{
public:
    explicit JwtHandler(const QByteArray &secret);

    // 默认访问令牌有效期(秒), 由配置 access_token_ttl_seconds 设置
    void setDefaultTtlSeconds(qint64 ttl);

    // 签发令牌: 自动补齐 iat/exp(exp 使用 ttlSeconds, 传 -1 时用默认值)
    QByteArray createToken(const QJsonObject &claims, qint64 ttlSeconds = -1) const;

    // 校验令牌: 结构 -> header/alg -> 签名 -> exp/nbf。
    // 成功时 payloadOut 输出载荷; 失败时 error 说明原因(不泄露内部细节)。
    bool verify(const QByteArray &token, QJsonObject *payloadOut = nullptr, QString *error = nullptr) const;

    // ---- 基础算法(static, 供测试与复用) ----
    static QByteArray hmacSha256(const QByteArray &key, const QByteArray &message);
    static QByteArray b64UrlEncode(const QByteArray &in);
    static QByteArray b64UrlDecode(const QByteArray &in, bool *ok = nullptr);

private:
    QByteArray m_secret;
    qint64 m_defaultTtlSeconds = 3600;
};
