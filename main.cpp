#include "authserver.h"

#include <QByteArray>
#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QLoggingCategory>

/**
 * @brief 程序入口
 *        - 解析命令行参数
 *        - 加载配置
 *        - 环境变量覆盖
 *        - 启动认证中心
 */
int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("uac"));
    app.setApplicationVersion(QStringLiteral("1.0.0"));

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("统一认证中心 (Unified Authentication Center)"));
    parser.addHelpOption();
    parser.addVersionOption();

    QCommandLineOption cfgOpt({QStringLiteral("c"), QStringLiteral("config")},
                              QStringLiteral("配置文件路径"), QStringLiteral("path"),
                              QStringLiteral("config.json"));
    QCommandLineOption tplOpt({QStringLiteral("t"), QStringLiteral("templates")},
                              QStringLiteral("模板目录"), QStringLiteral("dir"),
                              QStringLiteral("templates"));
    parser.addOption(cfgOpt);
    parser.addOption(tplOpt);
    parser.process(app);

    AuthConfig cfg = AuthConfig::fromFile(parser.value(cfgOpt));
    cfg.templatesDir = parser.value(tplOpt);

    // 环境变量覆盖
    if (qEnvironmentVariableIsSet("AUTH_HOST"))
        cfg.host = qEnvironmentVariable("AUTH_HOST");
    if (qEnvironmentVariableIsSet("AUTH_PORT"))
        cfg.port = static_cast<quint16>(qEnvironmentVariableIntValue("AUTH_PORT"));

    if (qEnvironmentVariableIsSet("JWT_SECRET")) {
        cfg.jwtSecret = qEnvironmentVariable("JWT_SECRET").toUtf8();
        if (cfg.jwtSecret.size() < 32)
            qWarning("[SECURITY] JWT_SECRET 长度不足 32 字节，建议使用更长的随机密钥。");
    } else {
        cfg.jwtSecret = QByteArrayLiteral("dev-only-insecure-secret-change-me-please-32bytes");
        qWarning("[SECURITY] 未设置环境变量 JWT_SECRET，正在使用内置默认密钥。"
                 "生产环境必须通过环境变量注入强随机密钥！");
    }

    try {
        AuthServer server(cfg);
        if (!server.listen())
            return 1;
        return app.exec();
    } catch (const std::exception &e) {
        qCritical() << "启动失败:" << e.what();
        return 1;
    }
}