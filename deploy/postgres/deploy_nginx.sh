#!/bin/bash
# ============================================================
#  Nginx — Swarm 多副本（无状态）
# ============================================================
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$SCRIPT_DIR"

RED='\033[0;31m'; GREEN='\033[0;32m'; NC='\033[0m'
info()  { echo -e "${GREEN}[NGINX][INFO]${NC} $*"; }
error() { echo -e "${RED}[NGINX][ERROR]${NC} $*" >&2; exit 1; }

[ -f .env ] || error "未找到 .env"
set -a; source .env; set +a

IFS=',' read -ra NODE_ARRAY <<< "${NODES}"
REPLICAS="${NGINX_REPLICAS:-3}"

NGINX_CONF_DIR="${CONFIG_BASE_DIR}/nginx"
NGINX_CONF="${NGINX_CONF_DIR}/nginx.conf"

[ -f "${NGINX_CONF}" ] || error "未找到 nginx.conf: ${NGINX_CONF}"

# ---------- 分发配置到所有节点 ----------
info "分发 nginx.conf 到所有节点"
for NODE_IP in "${NODE_ARRAY[@]}"; do
  info "  -> ${NODE_IP}"
  ssh -p "${SSH_PORT}" -o StrictHostKeyChecking=no "${SSH_USER}@${NODE_IP}" \
      "mkdir -p ${NGINX_CONF_DIR}" 2>/dev/null || true
  scp -P "${SSH_PORT}" -o StrictHostKeyChecking=no -q \
      "${NGINX_CONF}" "${SSH_USER}@${NODE_IP}:${NGINX_CONF}"
done

# ---------- 网络检查 ----------
docker network inspect "${NETWORK_NAME}" >/dev/null 2>&1 || error "overlay 网络 ${NETWORK_NAME} 不存在"

# ---------- 删除旧 service ----------
if docker service inspect "${NGINX_SERVICE}" >/dev/null 2>&1; then
  info "删除旧 service ${NGINX_SERVICE}"
  docker service rm "${NGINX_SERVICE}" >/dev/null
  for i in $(seq 1 60); do
    docker service inspect "${NGINX_SERVICE}" >/dev/null 2>&1 || break
    sleep 1
  done
fi

# ---------- 创建 service ----------
info "创建 service ${NGINX_SERVICE}（replicas=${REPLICAS}）"
docker service create \
  --name "${NGINX_SERVICE}" \
  --network "${NETWORK_NAME}" \
  --replicas "${REPLICAS}" \
  --with-registry-auth \
  --mount type=bind,source=${NGINX_CONF},destination=/etc/nginx/nginx.conf,readonly=true \
  --publish published=80,target=80,mode=ingress \
  --health-cmd="wget -q -O /dev/null http://127.0.0.1/ || exit 1" \
  --health-interval=15s --health-timeout=5s --health-retries=3 \
  --restart-condition=on-failure --restart-delay=10s --restart-max-attempts=5 \
  "${NGINX_IMAGE}"

info "Nginx service 部署完成（副本数=${REPLICAS}）"