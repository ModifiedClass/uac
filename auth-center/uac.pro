# uac.pro —— uac 统一认证中心 (Qt C++ / QHttpServer / OAuth2 授权码模式 + JWT)
# 构建(Release):
#   qmake6 uac.pro CONFIG+=release
#   make -j$(nproc)
# 产物: ./auth-center

QT += core network httpserver
QT -= gui

CONFIG += c++17
CONFIG -= app_bundle

TEMPLATE = app
TARGET = auth-center

DEFINES += QT_DEPRECATED_WARNINGS
QMAKE_CXXFLAGS += -Wall -Wextra

SOURCES += \
    main.cpp \
    src/authserver.cpp \
    src/config.cpp \
    src/jwt.cpp \
    src/passwordhash.cpp \
    src/plugins.cpp \
    src/storage.cpp

HEADERS += \
    src/authserver.h \
    src/config.h \
    src/jwt.h \
    src/passwordhash.h \
    src/plugins.h \
    src/storage.h \
    src/util.h

DISTFILES += \
    config.json \
    templates/login.html

# 安装规则(可选): make install 安装到 /opt/auth-center
target.path = /opt/auth-center
cfg.files = config.json
cfg.path = /opt/auth-center
tpl.files = templates/login.html
tpl.path = /opt/auth-center/templates
INSTALLS += target cfg tpl
