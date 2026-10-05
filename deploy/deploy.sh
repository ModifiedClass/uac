#!/bin/bash
# ============================================================
#  统一认证中心 — 生产环境一键部署
#  服务器：Debian 13.4  192.168.2.1
#  执行：./deploy.sh
# ============================================================
set -euo pipefail

# ---------- 常量 ----------
DEPLOY_DIR="/opt/uac"
COMPOSE_DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$COMPOSE_DIR"

# ---------- 颜色 ----------
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'

info()  { echo -e "${GREEN}[INFO]${NC} $*"; }
warn()  { echo -e "${YELLOW}[WARN]${NC} $*"; }
error() { echo -e "${RED}[ERROR]${NC} $*"; exit 1; }

# ============================================================
#  步骤 1：检查 .env
# ============================================================
info "[1/8] 检查环境变量文件..."
if [ ! -f .env ]; then
    error "未找到 .env 文件，请先执行: cp .env.example .env && vim .env"
fi
set -a; source .env; set +a

# ============================================================
#  步骤 2：校验关键变量
# ============================================================
info "[2/8] 校验关键变量..."
if [ -z "${JWT_SECRET:-}" ] || [ "${#JWT_SECRET}" -lt 32 ]; then
    error "JWT_SECRET 未设置或长度不足 32 字节"
fi
if [ -z "${POSTGRES_PASSWORD:-}" ]; then
    error "POSTGRES_PASSWORD 未设置"
fi
if [ -z "${REDIS_PASSWORD:-}" ]; then
    error "REDIS_PASSWORD 未设置"
fi

# ============================================================
#  步骤 3：检查 Docker
# ============================================================
info "[3/8] 检查 Docker 环境..."
command -v docker &>/dev/null || error "未安装 Docker，请先执行: sudo ./install-docker.sh"
docker compose version &>/dev/null || error "未安装 docker compose 插件"
systemctl is-active --quiet docker || error "Docker 服务未运行，请执行: sudo systemctl start docker"

# ============================================================
#  步骤 4：创建数据持久化目录（宿主机外部存储）
# ============================================================
info "[4/8] 创建数据持久化目录..."
sudo mkdir -p /opt/uac/data/postgres
sudo mkdir -p /opt/uac/data/redis
sudo mkdir -p /opt/uac/logs/nginx

# PostgreSQL 容器内 UID=999，Redis UID=999，nginx UID=101
sudo chown -R 999:999 /opt/uac/data/postgres
sudo chown -R 999:999 /opt/uac/data/redis
sudo chown -R 101:101 /opt/uac/logs/nginx

info "  数据目录："
echo "    PostgreSQL: /opt/uac/data/postgres"
echo "    Redis:      /opt/uac/data/redis"
echo "    日志:       /opt/uac/logs"

# ============================================================
#  步骤 5：停止旧容器
# ============================================================
info "[5/8] 停止旧容器..."
docker compose down --remove-orphans 2>/dev/null || true

# ============================================================
#  步骤 6：构建认证中心镜像
# ============================================================
info "[6/8] 构建认证中心镜像（Release 模式）..."
docker compose build --no-cache auth-center

# ============================================================
#  步骤 7：启动全部服务
# ============================================================
info "[7/8] 启动全部服务..."
docker compose up -d

# 等待服务就绪
info "  等待服务就绪（8 秒）..."
sleep 8

# 检查容器状态
for svc in uac-postgres uac-redis uac-auth-center uac-nginx; do
    if docker ps --format '{{.Names}}' | grep -q "^${svc}$"; then
        info "  ✓ ${svc} 运行中"
    else
        error "  ✗ ${svc} 启动失败，请执行: docker compose logs ${svc}"
    fi
done

# ============================================================
#  步骤 8：健康检查
# ============================================================
info "[8/8] 执行健康检查..."

# Nginx 反向代理检查
if curl -sf http://127.0.0.1:80/ > /dev/null 2>&1; then
    info "  ✓ Nginx 反向代理正常"
else
    warn "  ✗ Nginx 健康检查失败，请检查日志"
fi

# PostgreSQL 检查
if docker exec uac-postgres pg_isready -U "${POSTGRES_USER}" -d "${POSTGRES_DB}" &>/dev/null; then
    info "  ✓ PostgreSQL 正常"
else
    warn "  ✗ PostgreSQL 健康检查失败"
fi

# Redis 检查
if docker exec uac-redis redis-cli -a "${REDIS_PASSWORD}" ping 2>/dev/null | grep -q PONG; then
    info "  ✓ Redis 正常"
else
    warn "  ✗ Redis 健康检查失败"
fi

# ============================================================
#  输出结果
# ============================================================
echo ""
echo "============================================================"
echo "  统一认证中心部署完成"
echo "============================================================"
echo ""
echo "  访问地址:  http://192.168.2.1/"
echo "  登录页:    http://192.168.2.1/login"
echo ""
echo "  容器状态:"
docker compose ps --format "table {{.Name}}\t{{.Status}}\t{{.Ports}}"
echo ""
echo "  数据目录:"
echo "    PostgreSQL: /opt/uac/data/postgres"
echo "    Redis:      /opt/uac/data/redis"
echo ""
echo "  常用命令:"
echo "    查看日志:  docker compose logs -f"
echo "    重启服务:  docker compose restart"
echo "    停止服务:  docker compose down"
echo "    重新部署:  ./deploy.sh"
echo "============================================================"