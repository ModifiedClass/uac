#!/bin/bash
# ============================================================
#  认证中心 — 构建 + 推送 + Swarm 多副本（无状态）
# ============================================================
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$SCRIPT_DIR"

RED='\033[0;31m'; GREEN='\033[0;32m'; NC='\033[0m'
info()  { echo -e "${GREEN}[AUTH][INFO]${NC} $*"; }
error() { echo -e "${RED}[AUTH][ERROR]${NC} $*" >&2; exit 1; }

[ -f .auth.env ] || error "未找到 .auth.env"
set -a; source .auth.env; set +a

IFS=',' read -ra NODE_ARRAY <<< "${NODES}"
REPLICAS="${AUTH_REPLICAS:-3}"

[ -n "${JWT_SECRET:-}" ] && [ ${#JWT_SECRET} -ge 32 ] || error "JWT_SECRET 未设置或长度不足 32"
[ -n "${POSTGRES_PASSWORD:-}" ] || error "POSTGRES_PASSWORD 未设置"
[ -n "${REDIS_PASSWORD:-}" ]    || error "REDIS_PASSWORD 未设置"

# ---------- 构建镜像 ----------
AUTH_SRC_DIR="${SCRIPT_DIR}/auth-center"
[ -d "${AUTH_SRC_DIR}" ] || error "未找到 auth-center 源码目录: ${AUTH_SRC_DIR}"

info "构建镜像 ${AUTH_IMAGE}"
docker build -t "${AUTH_IMAGE}" "${AUTH_SRC_DIR}"

info "推送镜像到 ${REGISTRY}"
docker push "${AUTH_IMAGE}"

# ---------- 生成 JWT secret ----------
JWT_DIR="${CONFIG_BASE_DIR}/auth"
mkdir -p "${JWT_DIR}"
printf '%s\n' "${JWT_SECRET}" > "${JWT_DIR}/jwt_secret.txt"
chmod 600 "${JWT_DIR}/jwt_secret.txt"

if docker secret inspect "jwt_secret-uac" >/dev/null 2>&1; then
  info "删除旧 secret jwt_secret-uac"
  i=0
  until docker secret rm "jwt_secret-uac" >/dev/null 2>&1; do
    i=$((i+1)); [ "${i}" -gt 30 ] && error "删除 secret 失败"; sleep 1
  done
fi
docker secret create "jwt_secret-uac" "${JWT_DIR}/jwt_secret.txt" >/dev/null

# ---------- 网络检查 ----------
docker network inspect "${NETWORK_NAME}" >/dev/null 2>&1 || error "overlay 网络 ${NETWORK_NAME} 不存在"

# ---------- 删除旧 service ----------
if docker service inspect "${AUTH_SERVICE}" >/dev/null 2>&1; then
  info "删除旧 service ${AUTH_SERVICE}"
  docker service rm "${AUTH_SERVICE}" >/dev/null
  for i in $(seq 1 60); do
    docker service inspect "${AUTH_SERVICE}" >/dev/null 2>&1 || break
    sleep 1
  done
  sleep 5
fi

# ---------- 创建 service ----------
info "创建 service ${AUTH_SERVICE}（replicas=${AUTH_REPLICAS}）"
docker service create \
  --name "${AUTH_SERVICE}" \
  --network "${NETWORK_NAME}" \
  --replicas "${AUTH_REPLICAS}" \
  --with-registry-auth \
  --secret source=jwt_secret-uac,target=/run/secrets/jwt_secret \
  --secret source=postgres_password-uac,target=/run/secrets/pg_password \
  --secret source=redis_password-uac,target=/run/secrets/redis_password \
  --env JWT_SECRET_FILE=/run/secrets/jwt_secret \
  --env DB_HOST="${PG_SERVICE}" \
  --env DB_PORT=5432 \
  --env DB_USER="${POSTGRES_USER}" \
  --env DB_NAME="${POSTGRES_DB}" \
  --env DB_PASSWORD_FILE=/run/secrets/pg_password \
  --env REDIS_HOST="${REDIS_SERVICE}" \
  --env REDIS_PORT=6379 \
  --env REDIS_PASSWORD_FILE=/run/secrets/redis_password \
  --publish published=8080,target=8080,mode=ingress \
  --update-parallelism 1 \
  --update-delay 10s \
  --restart-condition=on-failure --restart-delay=10s --restart-max-attempts=5 \
  "${AUTH_IMAGE}"

info "Auth service 部署完成（副本数=${AUTH_REPLICAS}）"
info "查看副本状态: docker service ps ${AUTH_SERVICE}"