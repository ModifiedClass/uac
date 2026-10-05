# ============================================================
#  阶段 1：编译 Qt 应用
# ============================================================
FROM stateoftheartio/qt6:6.12-gcc-aqt AS builder

WORKDIR /src

# 安装 hiredis 开发库（bcrypt 已内置于项目）
RUN apt-get update && apt-get install -y --no-install-recommends \
    libhiredis-dev \
    && rm -rf /var/lib/apt/lists/*

# 拷贝源码
COPY src/ src/
COPY main.cpp uac.pro config.json ./
COPY templates/ templates/

# 编译（Release 模式）
RUN qmake6 uac.pro CONFIG+=release && \
    make -j$(nproc)

# ============================================================
#  阶段 2：运行时镜像
# ============================================================
FROM debian:13-slim

# 运行时依赖
RUN apt-get update && apt-get install -y --no-install-recommends \
    libssl3 \
    libdouble-conversion3 \
    libpcre2-16-0 \
    libzstd1 \
    libhiredis1.1.0 \
    libpq5 \
    ca-certificates \
    && rm -rf /var/lib/apt/lists/*

# 拷贝 Qt 运行库
COPY --from=builder /opt/qt/6.12.0/gcc_64/lib /opt/qt/lib
COPY --from=builder /opt/qt/6.12.0/gcc_64/plugins /opt/qt/plugins

# 拷贝可执行文件与资源
COPY --from=builder /src/bin/auth-center /app/auth-center
COPY --from=builder /src/config.json /app/config.json
COPY --from=builder /src/templates /app/templates

WORKDIR /app

ENV LD_LIBRARY_PATH=/opt/qt/lib
ENV QT_PLUGIN_PATH=/opt/qt/plugins
ENV QT_QPA_PLATFORM=offscreen

EXPOSE 27149

CMD ["./auth-center", "-c", "config.json", "-t", "templates"]