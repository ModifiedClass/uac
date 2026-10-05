#!/bin/bash
# ============================================================
#  Debian 13 (Trixie) — Docker CE 安装脚本
#  执行：sudo ./install-docker.sh
# ============================================================
set -e

echo "=========================================="
echo "  Debian 13 Docker CE 安装"
echo "=========================================="

# ---------- 步骤 1：清理旧版本 ----------
echo ">>> [1/7] 清理旧版本 Docker..."
apt-get remove -y docker docker-engine docker.io containerd runc 2>/dev/null || true

# ---------- 步骤 2：安装依赖 ----------
echo ">>> [2/7] 安装 apt 依赖..."
apt-get update
apt-get install -y ca-certificates curl gnupg lsb-release

# ---------- 步骤 3：添加 Docker GPG 密钥 ----------
echo ">>> [3/7] 添加 Docker GPG 密钥..."
install -m 0755 -d /etc/apt/keyrings
curl -fsSL https://download.docker.com/linux/debian/gpg | \
    gpg --dearmor -o /etc/apt/keyrings/docker.gpg
chmod a+r /etc/apt/keyrings/docker.gpg

# ---------- 步骤 4：添加 Docker 仓库 ----------
echo ">>> [4/7] 添加 Docker APT 仓库..."
echo \
  "deb [arch=$(dpkg --print-architecture) signed-by=/etc/apt/keyrings/docker.gpg] \
  https://download.docker.com/linux/debian \
  $(. /etc/os-release && echo "$VERSION_CODENAME") stable" | \
  tee /etc/apt/sources.list.d/docker.list > /dev/null

# ---------- 步骤 5：安装 Docker 及 Compose 插件 ----------
echo ">>> [5/7] 安装 Docker CE 及 Compose 插件..."
apt-get update
apt-get install -y \
    docker-ce \
    docker-ce-cli \
    containerd.io \
    docker-buildx-plugin \
    docker-compose-plugin

# ---------- 步骤 6：启动并设置开机自启 ----------
echo ">>> [6/7] 启动 Docker 并设置开机自启..."
systemctl start docker
systemctl enable docker

# ---------- 步骤 7：将当前用户加入 docker 组 ----------
echo ">>> [7/7] 将当前用户加入 docker 组..."
if [ -n "${SUDO_USER}" ]; then
    usermod -aG docker "${SUDO_USER}"
fi

echo ""
echo "=========================================="
echo "  Docker 安装完成"
echo "=========================================="
docker --version
docker compose version
echo ""
echo "请执行 'newgrp docker' 或重新登录以使用非 sudo 方式操作 Docker。"