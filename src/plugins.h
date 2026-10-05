#ifndef PLUGINS_H
#define PLUGINS_H

#include <QJsonObject>
#include <QString>
#include <QStringList>

#include <memory>
#include <vector>

/**
 * @brief 认证中心插件接口
 *        可用于：黑名单、防抖(限流)、风控、审计、MFA 二次校验等
 */
class IAuthPlugin {
public:
    virtual ~IAuthPlugin() = default;

    virtual QString name() const = 0;
    virtual bool init(const QJsonObject &cfg) { Q_UNUSED(cfg); return true; }

    /// 返回 false 则拒绝本次登录
    virtual bool beforeLogin(const QString &username, const QString &ip, QString *reason)
    { Q_UNUSED(username); Q_UNUSED(ip); Q_UNUSED(reason); return true; }

    /// 登录完成后的通知（无论成功失败）
    virtual void afterLogin(const QString &username, const QString &ip, bool success)
    { Q_UNUSED(username); Q_UNUSED(ip); Q_UNUSED(success); }

    /// 返回 false 则拒绝本次 token 请求
    virtual bool beforeToken(const QString &clientId, const QString &ip, QString *reason)
    { Q_UNUSED(clientId); Q_UNUSED(ip); Q_UNUSED(reason); return true; }
};

class PluginManager {
public:
    void add(std::unique_ptr<IAuthPlugin> p) { m_plugins.emplace_back(std::move(p)); }

    /// 根据 config.json 的 "plugins" 节点加载内置插件
    void loadFromConfig(const QJsonObject &plugins);

    bool beforeLogin(const QString &username, const QString &ip, QString *reason);
    void afterLogin(const QString &username, const QString &ip, bool success);
    bool beforeToken(const QString &clientId, const QString &ip, QString *reason);

    QStringList names() const;

private:
    std::vector<std::unique_ptr<IAuthPlugin>> m_plugins;
};

#endif // PLUGINS_H