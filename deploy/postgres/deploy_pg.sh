#!/bin/bash
# ============================================================
#  PostgreSQL 18 — Swarm 单副本部署
#
#  首次执行: 初始化配置 + 数据目录 + 创建 service
#  后续执行: 仅重新部署 service，保留数据
#
#  用法:
#    ./deploy-pg.sh                # 首次 / 重新部署
#    ./deploy-pg.sh --force-init   # 强制重新初始化（清空数据!）
# ============================================================
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$SCRIPT_DIR"

RED='\033[0;31m'; GREEN='\033[0;32m'; YELLOW='\033[1;33m'; NC='\033[0m'
info()  { echo -e "${GREEN}[PG][INFO]${NC} $*"; }
warn()  { echo -e "${YELLOW}[PG][WARN]${NC} $*"; }
error() { echo -e "${RED}[PG][ERROR]${NC} $*" >&2; exit 1; }

[ -f .env ] || error "未找到 .env"
set -a; source .env; set +a

IFS=',' read -ra NODE_ARRAY <<< "${NODES}"
REPLICAS="${PG_REPLICAS:-1}"

CONFIG_DIR="${CONFIG_BASE_DIR}/postgre"
SECRET_NAME="postgres_password-uac"
MARKER_FILE="${CONFIG_DIR}/.initialized"

FORCE_INIT=0
[ "${1:-}" = "--force-init" ] && FORCE_INIT=1

# ---------- 依赖检查 ----------
for cmd in docker ssh scp; do
  command -v "$cmd" >/dev/null 2>&1 || error "未找到命令: $cmd"
done
docker info 2>/dev/null | grep -q "Swarm: active" || error "Swarm 未激活"
docker node ls >/dev/null 2>&1 || error "当前节点不是 Swarm manager"

[ -n "${POSTGRES_USER:-}" ]     || error "POSTGRES_USER 未设置"
[ -n "${POSTGRES_PASSWORD:-}" ] || error "POSTGRES_PASSWORD 未设置"
[ -n "${POSTGRES_DB:-}" ]       || error "POSTGRES_DB 未设置"

# ============================================================
# 1. 写入 PG 配置文件
# ============================================================
write_config_files() {
  info "写入 PostgreSQL 配置文件到 ${CONFIG_DIR}"
  mkdir -p "${CONFIG_DIR}"

  cat > "${CONFIG_DIR}/simple-postgresql.conf" << 'EOF'
listen_addresses = '*'
port = 5432
max_connections = 200
shared_buffers = 2GB
effective_cache_size = 6GB
work_mem = 8MB
maintenance_work_mem = 256MB
wal_buffers = 16MB

wal_level = replica
max_wal_senders = 10
wal_keep_size = 1GB
min_wal_size = 512MB
max_wal_size = 4GB

checkpoint_timeout = 15min
checkpoint_completion_target = 0.9
checkpoint_flush_after = 512kB

random_page_cost = 1.1
effective_io_concurrency = 200
default_statistics_target = 100

max_worker_processes = 8
max_parallel_workers = 8
max_parallel_workers_per_gather = 2

timezone = 'Asia/Shanghai'
log_timezone = 'Asia/Shanghai'

log_statement = 'none'
log_duration = off
log_lock_waits = on
log_min_duration_statement = 1000
log_checkpoints = on
log_connections = on
log_disconnections = on
log_line_prefix = '%m [%p] %q%u@%d '
log_filename = 'postgresql-%Y-%m-%d_%H%M%S.log'
log_rotation_age = 1d
log_rotation_size = 100MB

dynamic_shared_memory_type = posix
track_io_timing = on
jit = on
EOF

  cat > "${CONFIG_DIR}/simple-pg_hba.conf" << 'EOF'
# TYPE  DATABASE        USER            ADDRESS                 METHOD
local   all             all                                     trust
host    all             all             127.0.0.1/32            trust
host    all             all             ::1/128                 trust
host    all             all             0.0.0.0/0               md5
EOF

  printf '%s\n' "${POSTGRES_PASSWORD}" > "${CONFIG_DIR}/postgres_password.txt"
  chmod 644 "${CONFIG_DIR}/simple-postgresql.conf" "${CONFIG_DIR}/simple-pg_hba.conf"
  chmod 600 "${CONFIG_DIR}/postgres_password.txt"
}

# ============================================================
# 2. 分发配置到所有节点（单副本也分发，方便故障后切换节点）
# ============================================================
distribute_config() {
  info "分发 PostgreSQL 配置到所有节点..."
  for NODE_IP in "${NODE_ARRAY[@]}"; do
    info "  -> ${NODE_IP}"
    ssh -p "${SSH_PORT}" -o StrictHostKeyChecking=no "${SSH_USER}@${NODE_IP}" \
        "mkdir -p ${CONFIG_DIR}" 2>/dev/null || true

    scp -P "${SSH_PORT}" -o StrictHostKeyChecking=no -q \
        "${CONFIG_DIR}/simple-postgresql.conf" \
        "${CONFIG_DIR}/simple-pg_hba.conf" \
        "${CONFIG_DIR}/postgres_password.txt" \
        "${SSH_USER}@${NODE_IP}:${CONFIG_DIR}/"

    ssh -p "${SSH_PORT}" -o StrictHostKeyChecking=no "${SSH_USER}@${NODE_IP}" \
        "chmod 644 ${CONFIG_DIR}/simple-postgresql.conf ${CONFIG_DIR}/simple-pg_hba.conf; \
         chmod 600 ${CONFIG_DIR}/postgres_password.txt" 2>/dev/null || true
  done
}

# ============================================================
# 3. 初始化数据目录（首次）
# ============================================================
init_data_dirs() {
  info "初始化所有节点数据目录 ${PG_DATA_DIR} ..."
  for NODE_IP in "${NODE_ARRAY[@]}"; do
    info "  -> ${NODE_IP}"
    ssh -p "${SSH_PORT}" -o StrictHostKeyChecking=no "${SSH_USER}@${NODE_IP}" "
      set -e
      if [ ! -d '${PG_DATA_DIR}' ] || [ -z \"\$(ls -A ${PG_DATA_DIR} 2>/dev/null)\" ]; then
        mkdir -p ${PG_DATA_DIR}
        chown 999:999 ${PG_DATA_DIR}
        chmod 700 ${PG_DATA_DIR}
        echo '    已初始化数据目录'
      else
        echo '    目录非空, 保留现有数据'
      fi
    "
  done
}

# ============================================================
# 4. 清空数据目录（--force-init）
# ============================================================
wipe_data_dirs() {
  warn "!!! 清空所有节点数据目录（--force-init） !!!"
  for NODE_IP in "${NODE_ARRAY[@]}"; do
    info "  -> ${NODE_IP}"
    ssh -p "${SSH_PORT}" -o StrictHostKeyChecking=no "${SSH_USER}@${NODE_IP}" "
      rm -rf ${PG_DATA_DIR}/*
      mkdir -p ${PG_DATA_DIR}
      chown 999:999 ${PG_DATA_DIR}
      chmod 700 ${PG_DATA_DIR}
    "
  done
}

# ============================================================
# 5. overlay 网络
# ============================================================
ensure_network() {
  if docker network inspect "${NETWORK_NAME}" >/dev/null 2>&1; then
    info "overlay 网络 ${NETWORK_NAME} 已存在"
  else
    info "创建 overlay attachable 网络 ${NETWORK_NAME}"
    docker network create --driver overlay --attachable "${NETWORK_NAME}"
  fi
}

# ============================================================
# 6. 删除旧 service
# ============================================================
remove_service() {
  if docker service inspect "${PG_SERVICE}" >/dev/null 2>&1; then
    info "删除旧 service ${PG_SERVICE}"
    docker service rm "${PG_SERVICE}" >/dev/null
    for i in $(seq 1 60); do
      docker service inspect "${PG_SERVICE}" >/dev/null 2>&1 || break
      sleep 1
    done
    info "等待容器释放 secret 资源..."
    sleep 5
  else
    info "service ${PG_SERVICE} 不存在, 跳过删除"
  fi
}

# ============================================================
# 7. 创建/更新 secret
# ============================================================
ensure_secret() {
  if docker secret inspect "${SECRET_NAME}" >/dev/null 2>&1; then
    info "删除旧 secret ${SECRET_NAME}"
    local i=0
    until docker secret rm "${SECRET_NAME}" >/dev/null 2>&1; do
      i=$((i+1))
      [ "${i}" -gt 30 ] && error "删除 secret 失败（可能仍被容器占用）"
      sleep 1
    done
  fi
  info "创建 secret ${SECRET_NAME}"
  docker secret create "${SECRET_NAME}" "${CONFIG_DIR}/postgres_password.txt" >/dev/null
}

# ============================================================
# 8. 创建 service（单副本）
# ============================================================
create_pg_service() {
  info "创建 service ${PG_SERVICE}（replicas=${REPLICAS}）"
  docker service create \
    --name "${PG_SERVICE}" \
    --network "${NETWORK_NAME}" \
    --replicas "${REPLICAS}" \
    --constraint 'node.hostname==manager1' \
    --mount type=bind,source=${PG_DATA_DIR},destination=/var/lib/postgresql/data \
    --mount type=bind,source=${CONFIG_DIR}/simple-postgresql.conf,destination=/etc/postgresql/postgresql.conf,readonly=true \
    --mount type=bind,source=${CONFIG_DIR}/simple-pg_hba.conf,destination=/etc/postgresql/pg_hba.conf,readonly=true \
    --secret source=${SECRET_NAME},target=/run/secrets/postgres_password \
    --env POSTGRES_USER="${POSTGRES_USER}" \
    --env POSTGRES_PASSWORD_FILE=/run/secrets/postgres_password \
    --env POSTGRES_DB="${POSTGRES_DB}" \
    --env PGDATA=/var/lib/postgresql/data \
    --publish published=5432,target=5432,mode=host \
    --health-cmd="pg_isready -U ${POSTGRES_USER}" \
    --health-interval=15s \
    --health-timeout=10s \
    --health-retries=5 \
    --restart-condition=on-failure \
    --restart-delay=10s \
    --restart-max-attempts=5 \
    "${POSTGRES_IMAGE}" \
    -c config_file=/etc/postgresql/postgresql.conf \
    -c hba_file=/etc/postgresql/pg_hba.conf
}

# ============================================================
# 主流程
# ============================================================
main() {
  local need_init=0

  if [ "${FORCE_INIT}" -eq 1 ]; then
    need_init=1
  elif [ ! -f "${MARKER_FILE}" ] && ! docker service inspect "${PG_SERVICE}" >/dev/null 2>&1; then
    need_init=1
  fi

  if [ "${need_init}" -eq 1 ]; then
    info "========== 首次初始化 PG =========="
    write_config_files
    distribute_config
    ensure_network
    if [ "${FORCE_INIT}" -eq 1 ]; then
      wipe_data_dirs
    fi
    init_data_dirs
    mkdir -p "$(dirname "${MARKER_FILE}")"
    touch "${MARKER_FILE}"
    info "初始化完成, 标记: ${MARKER_FILE}"
  else
    info "========== 重新部署 PG（保留数据） =========="
    ensure_network
  fi

  remove_service
  ensure_secret
  create_pg_service

  echo
  info "PG service 部署完成（副本数=${REPLICAS}）"
  info "查看副本状态: docker service ps ${PG_SERVICE}"
  info "查看服务日志: docker service logs -f ${PG_SERVICE}"
  info "连接数据库  : psql -h <节点IP> -U ${POSTGRES_USER} -d ${POSTGRES_DB}"
}

main "$@"