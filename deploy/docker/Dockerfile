# uac 认证中心 —— Docker 镜像(多阶段构建)
# 说明: 直接基于 Debian 官方镜像安装 Qt6 开发包编译;
#       若所用 Debian 仓库缺少 libqt6httpserver6, 可改用 Qt 官方镜像或自行编译 Qt(见 docs/DEPLOYMENT.md)。

# ============ 构建阶段 ============
FROM debian:bookworm-slim AS build
RUN apt-get update \
 && apt-get install -y --no-install-recommends \
        build-essential \
        qt6-base-dev \
        qt6-base-dev-tools \
        libqt6httpserver6 \
 && rm -rf /var/lib/apt/lists/*
WORKDIR /src
COPY auth-center/ ./
RUN qmake6 uac.pro CONFIG+=release && make -j"$(nproc)"

# ============ 运行阶段 ============
FROM debian:bookworm-slim
RUN apt-get update \
 && apt-get install -y --no-install-recommends \
        libqt6core6 libqt6network6 libqt6httpserver6 \
        libgcc-s1 libstdc++6 \
        curl ca-certificates \
 && rm -rf /var/lib/apt/lists/* \
 && groupadd --system uac \
 && useradd --system --gid uac --home-dir /opt/auth-center --shell /usr/sbin/nologin uac
WORKDIR /opt/auth-center
COPY --from=build --chown=uac:uac /src/auth-center ./auth-center
COPY --chown=uac:uac auth-center/templates ./templates
COPY --chown=uac:uac auth-center/config.json ./config.json
USER uac
EXPOSE 27149

# 健康检查(供编排/负载均衡使用)
HEALTHCHECK --interval=30s --timeout=3s --retries=3 \
  CMD curl -fsS http://127.0.0.1:27149/healthz >/dev/null || exit 1

ENTRYPOINT ["./auth-center"]
