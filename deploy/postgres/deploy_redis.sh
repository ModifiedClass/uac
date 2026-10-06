#!/bin/bash
# ============================================================
#  Redis — Swarm 单副本部署（AOF 持久化）
# ============================================================
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$SCRIPT_DIR"

RED='\033[0;31m'; GREEN='\033[0;32m'; YELLOW='\033[1;33m'; NC='\033[0m'
info()  { echo -e "${GREEN}[REDIS][INFO]${NC} $*"; }
warn()  { echo -e "${YELLOW}[REDIS][WARN]${NC} $*"; }
error() { echo -e "${RED}[REDIS][ERROR]${NC} $*" >&2; exit 1; }

[ -f .env ] || error "未找到 .env"
set -a; source .env; set +a

IFS=',' read -ra NODE_ARRAY <<< "${NODES}"
REPLICAS="${REDIS_REPLICAS:-1}"

CONFIG_DIR="${CONFIG_BASE_DIR}/redis"
SECRET_NAME="redis_password-uac"

[ -n "${REDIS_PASSWORD:-}" ] || error "REDIS_PASSWORD 未设置"

docker info 2>/dev/null | grep -q "Swarm: active" || error "Swarm 未激活"
docker node ls >/dev/null 2>&1 || error "当前节点不是 Swarm manager"

# ---------- 写密码文件 ----------
info "写入 redis 密码文件"
mkdir -p "${CONFIG_DIR}"
printf '%s\n' "${REDIS_PASSWORD}" > "${CONFIG_DIR}/redis_password.txt"
chmod 600 "${CONFIG_DIR}/redis_password.txt"

# ---------- 初始化所有节点数据目录 ----------
info "初始化所有节点数据目录 ${REDIS_DATA_DIR}"
for NODE_IP in "${NODE_ARRAY[@]}"; do
  info "  -> ${NODE_IP}"
  ssh -p "${SSH_PORT}" -o StrictHostKeyChecking=no "${SSH_USER}@${NODE_IP}" "
    set -e
    mkdir -p ${REDIS_DATA_DIR}
    chown 999:999 ${REDIS_DATA_DIR}
    chmod 700 ${REDIS_DATA_DIR}
  " 2>/dev/null || true
done

# ---------- 网络 ----------
if ! docker network inspect "${NETWORK_NAME}" >/dev/null 2>&1; then
  info "创建 overlay 网络 ${NETWORK_NAME}"
  docker network create --driver overlay --attachable "${NETWORK_NAME}"
fi

# ---------- 删除旧 service ----------
if docker service inspect "${REDIS_SERVICE}" >/dev/null 2>&1; then
  info "删除旧 service ${REDIS_SERVICE}"
  docker service rm "${REDIS_SERVICE}" >/dev/null
  for i in $(seq 1 60); do
    docker service inspect "${REDIS_SERVICE}" >/dev/null 2>&1 || break
    sleep 1
  done
  sleep 5
fi

# ---------- 重建 secret ----------
if docker secret inspect "${SECRET_NAME}" >/dev/null 2>&1; then
  info "删除旧 secret ${SECRET_NAME}"
  i=0
  until docker secret rm "${SECRET_NAME}" >/dev/null 2>&1; do
    i=$((i+1)); [ "${i}" -gt 30 ] && error "删除 secret 失败"; sleep 1
  done
fi
info "创建 secret ${SECRET_NAME}"
docker secret create "${SECRET_NAME}" "${CONFIG_DIR}/redis_password.txt" >/dev/null

# ---------- 创建 service ----------
info "创建 service ${REDIS_SERVICE}（replicas=${REPLICAS}）"
docker service create \
  --name "${REDIS_SERVICE}" \
  --network "${NETWORK_NAME}" \
  --replicas "${REPLICAS}" \
  --mount type=bind,source=${REDIS_DATA_DIR},destination=/data \
  --secret source=${SECRET_NAME},target=/run/secrets/redis_password \
  --publish published=6379,target=6379,mode=host \
  --health-cmd="redis-cli -a \$(cat /run/secrets/redis_password) ping | grep PONG" \
  --health-interval=15s --health-timeout=5s --health-retries=5 \
  --restart-condition=on-failure --restart-delay=10s --restart-max-attempts=5 \
  "${REDIS_IMAGE}" \
  sh -c 'exec redis-server --dir /data --appendonly yes --requirepass "$(cat /run/secrets/redis_password)"'

info "Redis service 部署完成（副本数=${REPLICAS}）"
info "查看副本状态: docker service ps ${REDIS_SERVICE}"