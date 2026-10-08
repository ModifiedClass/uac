// uac 统一认证中心 —— 程序入口
//
// 配置加载顺序(优先级从低到高):
//   1. config.json(缺失时使用内置演示配置)
//   2. 环境变量: UAC_HOST / UAC_PORT / JWT_SECRET / UAC_SESSION_TTL / UAC_CODE_TTL / UAC_TOKEN_TTL
//
// 生产环境 JWT_SECRET 必须通过环境变量注入, 禁止硬编码。

#include <QCoreApplication>

#include "authserver.h"
#include "config.h"

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("uac"));
    QCoreApplication::setApplicationVersion(QStringLiteral("1.0.0"));
    QCoreApplication::setOrganizationName(QStringLiteral("uac"));

    // 结构化日志格式(输出到 stdout/stderr, systemd 下自动进入 journal)
    qSetMessagePattern(QStringLiteral("[%{time yyyy-MM-dd hh:mm:ss.zzz}] [%{type}] %{message}"));

    const QString configPath = qEnvironmentVariable("UAC_CONFIG", "config.json");

    AppConfig cfg;
    QString err;
    if (!AppConfigLoader::load(configPath, &cfg, &err)) {
        qCritical().noquote() << "[startup] 配置加载失败:" << err;
        return 1;
    }

    AuthServer server;
    if (!server.init(cfg)) {
        qCritical().noquote() << "[startup] 初始化失败";
        return 1;
    }
    if (!server.start()) {
        qCritical().noquote() << "[startup] 监听失败";
        return 1;
    }

    qInfo().noquote() << "[startup] uac 认证中心启动完成, 监听"
                      << cfg.host << ":" << cfg.port
                      << "(仅本地回环, 对外由 Nginx 反代)";
    return app.exec();
}
