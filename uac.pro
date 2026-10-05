# ============================================================
#  统一认证中心 — qmake 工程文件
#  目标平台：Debian 13 / FreeBSD 15
#  Qt 版本：Qt 6.12（Core、Network、HttpServer、Sql）
# ============================================================

# ---------------- 使用的 Qt 模块 ----------------
QT       += core network httpserver sql

# ---------------- 明确排除的 Qt 模块 ----------------
QT       -= gui widgets qml quick quickcontrols2
QT       -= xml concurrent dbus testlib printsupport svg multimedia
QT       -= websockets websocketsquick bluetooth nfc positioning
QT       -= location sensors serialport serialbus charts datavis3d
QT       -= 3dcore 3drender 3dinput 3dlogic 3danimation 3dextras
QT       -= webenginecore webengine webenginewidgets webchannel
QT       -= texttospeech multimediawidgets multimediaquick
QT       -= remoteobjects scxml statemachine

# ---------------- 工程配置 ----------------
CONFIG   += c++17 console
CONFIG   -= app_bundle
CONFIG   -= debug_and_release

TEMPLATE  = app
TARGET    = auth-center
DESTDIR   = $$PWD/bin

# ---------------- 外部库（仅 Redis C 客户端） ----------------
unix:!macx {
    LIBS += -lhiredis
    INCLUDEPATH += /usr/include/hiredis
}

# ---------------- 源文件 ----------------
SOURCES += \
    main.cpp \
    src/bcrypt.cpp \
    src/bcrypt.cpp \
    src/crypto.cpp \
    src/jwt.cpp \
    src/store.cpp \
    src/store_pg.cpp \
    src/store_pg.cpp \
    src/store_redis.cpp \
    src/store_redis.cpp \
    src/plugins.cpp \
    src/authserver.cpp

HEADERS += \
    src/bcrypt.h \
    src/models.h \
    src/bcrypt.h \
    src/crypto.h \
    src/jwt.h \
    src/store.h \
    src/store_pg.h \
    src/store_pg.h \
    src/store_redis.h \
    src/store_redis.h \
    src/plugins.h \
    src/authserver.h

INCLUDEPATH += src

DISTFILES += \
    README.md \
    config.json \
    deploy/Dockerfile \
    deploy/backup.sh \
    deploy/deploy.sh \
    deploy/docker-compose.yml \
    deploy/.env.example \
    deploy/install-docker.sh \
    deploy/postgres/deploy_pg.sh \
    deploy/postgres/init.sql \
    deploy/nginx/conf.d/default.conf \
    templates/login.html

# ---------------- 编译选项 ----------------
QMAKE_CXXFLAGS += -std=c++17
QMAKE_CXXFLAGS_RELEASE += -O2 -Wall -Wextra
QMAKE_LDFLAGS += -Wl,--as-needed