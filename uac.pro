# ============================================================================
#  统一认证中心 (Unified Authentication Center) — qmake 工程文件
#  目标平台: Debian 11/12/13 / FreeBSD 15
#  Qt 版本 : Qt 6.12 (Core、Network、HttpServer)
# ============================================================================

QT       += core network httpserver
QT       -= gui widgets qml quick quickcontrols2
QT       -= sql xml concurrent dbus testlib printsupport svg multimedia
QT       -= websockets websocketsquick bluetooth nfc positioning
QT       -= location sensors serialport serialbus charts datavis3d
QT       -= 3dcore 3drender 3dinput 3dlogic 3danimation 3dextras
QT       -= webenginecore webengine webenginewidgets webchannel
QT       -= texttospeech multimediawidgets multimediaquick
QT       -= remoteobjects scxml statemachine

CONFIG   += c++17 console
CONFIG   -= app_bundle
CONFIG   -= debug_and_release

TEMPLATE  = app
TARGET    = auth-center
DESTDIR   = $$PWD/bin

# ----------------------------------------------------------------------------
#  源文件 / 头文件
# ----------------------------------------------------------------------------
SOURCES += \
    main.cpp \
    src/crypto.cpp \
    src/jwt.cpp \
    src/store.cpp \
    src/plugins.cpp \
    src/authserver.cpp

HEADERS += \
    src/models.h \
    src/crypto.h \
    src/jwt.h \
    src/store.h \
    src/plugins.h \
    src/authserver.h

DISTFILES += \
    config.json \
    templates/login.html

# ----------------------------------------------------------------------------
#  编译选项
# ----------------------------------------------------------------------------
QMAKE_CXXFLAGS_RELEASE += -O2 -Wall -Wextra -Wpedantic
QMAKE_CXXFLAGS_RELEASE -= -O0 -g
QMAKE_CXXFLAGS_DEBUG   += -O0 -g3 -Wall -Wextra

# C++17 严格模式
QMAKE_CXXFLAGS += -std=c++17

# 关闭 Qt 的隐式转换/废弃 API 警告（可按需开启）
DEFINES += QT_DEPRECATED_WARNINGS
DEFINES += QT_DISABLE_DEPRECATED_BEFORE=0x060000
DEFINES += QT_NO_FOREACH

# ----------------------------------------------------------------------------
#  平台相关
# ----------------------------------------------------------------------------
unix:!macx {
    # Debian / FreeBSD 通用
    QMAKE_LFLAGS += -Wl,--as-needed

    # FreeBSD 特有
    freebsd-* {
        QMAKE_CXXFLAGS += -I/usr/local/include
        QMAKE_LFLAGS   += -L/usr/local/lib
    }

    # Linux 特有：启用 PIE 与 RELRO（安全加固）
    linux-* {
        QMAKE_LFLAGS += -Wl,-z,relro,-z,now
    }
}

# ----------------------------------------------------------------------------
#  部署（可选：编译后自动复制资源到 DESTDIR）
# ----------------------------------------------------------------------------
!isEmpty(DESTDIR) {
    QMAKE_POST_LINK += $$QMAKE_COPY $$shell_path($$PWD/config.json) $$shell_path($$DESTDIR) $$escape_expand(\\n\\t)
    QMAKE_POST_LINK += $$QMAKE_MKDIR $$shell_path($$DESTDIR/templates) $$escape_expand(\\n\\t)
    QMAKE_POST_LINK += $$QMAKE_COPY $$shell_path($$PWD/templates/login.html) $$shell_path($$DESTDIR/templates) $$escape_expand(\\n\\t)
}