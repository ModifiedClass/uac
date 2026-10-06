#!/bin/bash
# ============================================================
#  统一认证中心 — 方案 A 一键部署
#
#  架构:
#    PostgreSQL  : 1 副本（有状态，单点）
#    Redis       : 1 副本（有状态，单点）
#    Auth Center : 3 副本（无状态，负载均衡）
#    Nginx       : 3 副本（无状态，负载均衡）
#
#  用法:
#    ./deploy.sh                # 首次 / 重新部署
#    ./deploy.sh --force-init   # 强制重新初始化 PG（清空 PG 数据!）
# ============================================================
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$SCRIPT_DIR"

RED='\033[0;31m'; GREEN='\033[0;32m'; NC='\033[0m'
info()  { echo -e "${GREEN}[DEPLOY][INFO]${NC} $*"; }
error() { echo -e "${RED}[DEPLOY][ERROR]${NC} $*" >&2; exit 1; }

[ -f .env ] || error "未找到 .env 文件，请先: cp .env.example .env && vim .env"
set -a; source .env; set +a

FORCE_INIT_ARGS=()
[ "${1:-}" = "--force-init" ] && FORCE_INIT_ARGS=("--force-init")

# ---------- 分脚本检查 ----------
for s in deploy-pg.sh deploy-redis.sh deploy-auth-center.sh deploy-nginx.sh; do
  [ -f "${SCRIPT_DIR}/${s}" ] || error "缺少分脚本: ${s}"
  [ -x "${SCRIPT_DIR}/${s}" ] || chmod +x "${SCRIPT_DIR}/${s}"
done

# ============================================================
echo ""
echo "============================================================"
info "[1/4] PostgreSQL 18（单副本）"
echo "============================================================"
"${SCRIPT_DIR}/deploy-pg.sh" "${FORCE_INIT_ARGS[@]:-}"

# ============================================================
echo ""
echo "============================================================"
info "[2/4] Redis（单副本）"
echo "============================================================"
"${SCRIPT_DIR}/deploy-redis.sh"

# ============================================================
echo ""
echo "============================================================"
info "[3/4] 认证中心（3 副本）"
echo "============================================================"
"${SCRIPT_DIR}/deploy-auth-center.sh"

# ============================================================
echo ""
echo "============================================================"
info "[4/4] Nginx（3 副本）"
echo "============================================================"
"${SCRIPT_DIR}/deploy-nginx.sh"

# ============================================================
#  汇总
# ============================================================
echo ""
echo "============================================================"
echo "  统一认证中心部署完成（方案 A）"
echo "============================================================"
echo ""
echo "  访问地址:  http://${SERVER_IP}/"
echo "  登录页:    http://${SERVER_IP}/login"
echo ""
echo "  Swarm 服务总览:"
docker service ls --format "table {{.Name}}\t{{.Replicas}}\t{{.Ports}}"
echo ""
echo "  副本分布:"
for svc in "${PG_SERVICE}" "${REDIS_SERVICE}" "${AUTH_SERVICE}" "${NGINX_SERVICE}"; do
  echo "  ── ${svc} ──"
  docker service ps "${svc}" \
    --format "    {{.Name}}\t{{.Node}}\t{{.CurrentState}}" \
    --filter "desired-state=running" 2>/dev/null || true
done
echo ""
echo "  数据目录:"
echo "    PostgreSQL: ${PG_DATA_DIR}  (运行副本所在节点)"
echo "    Redis:      ${REDIS_DATA_DIR} (运行副本所在节点)"
echo ""
echo "  常用命令:"
echo "    PG 副本状态:    docker service ps ${PG_SERVICE}"
echo "    PG 日志:        docker service logs -f ${PG_SERVICE}"
echo "    Redis 日志:     docker service logs -f ${REDIS_SERVICE}"
echo "    Auth 日志:      docker service logs -f ${AUTH_SERVICE}"
echo "    Nginx 日志:     docker service logs -f ${NGINX_SERVICE}"
echo "    重新部署:       ./deploy.sh"
echo "    强制重置 PG:    ./deploy.sh --force-init"
echo "============================================================"