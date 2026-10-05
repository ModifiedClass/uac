#!/bin/bash
# ============================================================
# PostgreSQL 18 Swarm 集群部署脚本 (Debian)
#
# 首次执行: 初始化配置 + 数据目录 + 部署服务
# 后续执行: 仅重新部署服务, 保留数据库数据
#
# 用法:
#   ./deploy-postgres.sh                  # 首次初始化 / 后续重新部署
#   ./deploy-postgres.sh --force-init     # 强制重新初始化(会清空数据!)
# ============================================================

set -euo pipefail

# -------------------- 可调参数 --------------------
NODES=(
  192.168.201.189
  192.168.201.181
  192.168.201.190
  192.168.201.191
  192.168.201.193
  192.168.201.182
)

SSH_PORT=60022
SSH_USER="root"

POSTGRES_IMAGE="192.168.201.194:5000/postgres:18"
SERVICE_NAME="postgres18-cluster-uac"
NETWORK_NAME="postgres-network-uac"
SECRET_NAME="postgres_password-uac"

CONFIG_DIR="/codes/uac/docker/postgre"
DATA_DIR="/data/postgresql18-uac"
PG_PASSWORD="G7#kL9@qZ2"
REPLICAS=6

MARKER_FILE="${CONFIG_DIR}/.initialized"
# --------------------------------------------------

FORCE_INIT=0
[ "${1:-}" = "--force-init" ] && FORCE_INIT=1

log() { echo "[$(date '+%F %T')] $*"; }
die() { echo "[$(date '+%F %T')] [ERROR] $*" >&2; exit 1; }

# ---------- 依赖检查 ----------
for cmd in docker ssh scp; do
  command -v "$cmd" >/dev/null 2>&1 || die "未找到命令: $cmd"
done
docker info >/dev/null 2>&1 || die "无法连接 Docker, 请确认当前节点是 Swarm manager"

# ============================================================
# 1. 写入本地配置文件
# ============================================================
write_config_files() {
  log "写入配置文件到 ${CONFIG_DIR}"
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

  printf '%s\n' "${PG_PASSWORD}" > "${CONFIG_DIR}/postgres_password.txt"
  chmod 644 "${CONFIG_DIR}/simple-postgresql.conf" \
           "${CONFIG_DIR}/simple-pg_hba.conf"
  chmod 600 "${CONFIG_DIR}/postgres_password.txt"
}

# ============================================================
# 2. 分发配置文件到所有节点
# ============================================================
distribute_config() {
  log "分发配置文件到所有节点..."
  for NODE_IP in "${NODES[@]}"; do
    log "  -> ${NODE_IP}"
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
# 3. 初始化数据目录(仅首次运行)
# ============================================================
init_data_dirs() {
  log "初始化所有节点数据目录 ${DATA_DIR} ..."
  for NODE_IP in "${NODES[@]}"; do
    log "  -> ${NODE_IP}"
    ssh -p "${SSH_PORT}" -o StrictHostKeyChecking=no "${SSH_USER}@${NODE_IP}" "
      set -e
      if [ ! -d '${DATA_DIR}' ] || [ -z \"\$(ls -A ${DATA_DIR} 2>/dev/null)\" ]; then
        mkdir -p ${DATA_DIR}
        chown 999:999 ${DATA_DIR}
        chmod 700 ${DATA_DIR}
        echo '    已初始化数据目录'
      else
        echo '    目录非空, 保留现有数据'
      fi
    "
  done
}

# ============================================================
# 4. 清空所有节点数据目录(仅 --force-init 时调用)
# ============================================================
wipe_data_dirs() {
  log "!!! 清空所有节点数据目录 (强制初始化模式) !!!"
  for NODE_IP in "${NODES[@]}"; do
    log "  -> ${NODE_IP}"
    ssh -p "${SSH_PORT}" -o StrictHostKeyChecking=no "${SSH_USER}@${NODE_IP}" "
      rm -rf ${DATA_DIR}/*
      mkdir -p ${DATA_DIR}
      chown 999:999 ${DATA_DIR}
      chmod 700 ${DATA_DIR}
    "
  done
}

# ============================================================
# 5. 确保 overlay 网络存在
# ============================================================
ensure_network() {
  if docker network inspect "${NETWORK_NAME}" >/dev/null 2>&1; then
    log "网络 ${NETWORK_NAME} 已存在"
  else
    log "创建 overlay 网络 ${NETWORK_NAME}"
    docker network create --driver overlay --attachable "${NETWORK_NAME}"
  fi
}

# ============================================================
# 6. 删除旧服务(保留数据目录)
# ============================================================
remove_service() {
  if docker service inspect "${SERVICE_NAME}" >/dev/null 2>&1; then
    log "删除旧服务 ${SERVICE_NAME}"
    docker service rm "${SERVICE_NAME}" >/dev/null
    # 等待服务彻底消失
    for i in $(seq 1 60); do
      docker service inspect "${SERVICE_NAME}" >/dev/null 2>&1 || break
      sleep 1
    done
    log "等待容器释放 secret 资源..."
    sleep 5
  else
    log "服务 ${SERVICE_NAME} 不存在, 跳过删除"
  fi
}

# ============================================================
# 7. 创建/更新 secret
# ============================================================
ensure_secret() {
  if [ -f "${CONFIG_DIR}/postgres_password.txt" ]; then
    if docker secret inspect "${SECRET_NAME}" >/dev/null 2>&1; then
      log "删除旧 secret ${SECRET_NAME}"
      local i=0
      until docker secret rm "${SECRET_NAME}" >/dev/null 2>&1; do
        i=$((i+1))
        [ "${i}" -gt 30 ] && die "删除 secret 失败(可能仍被容器占用)"
        sleep 1
      done
    fi
    log "创建 secret ${SECRET_NAME}"
    docker secret create "${SECRET_NAME}" "${CONFIG_DIR}/postgres_password.txt" >/dev/null
  else
    docker secret inspect "${SECRET_NAME}" >/dev/null 2>&1 \
      || die "缺少密码文件且 secret 不存在: ${CONFIG_DIR}/postgres_password.txt"
    log "保留现有 secret ${SECRET_NAME}"
  fi
}

# ============================================================
# 8. 创建服务
# ============================================================
create_service() {
  log "创建服务 ${SERVICE_NAME}"
  docker service create \
    --name "${SERVICE_NAME}" \
    --network "${NETWORK_NAME}" \
    --replicas "${REPLICAS}" \
    --mount type=bind,source=${DATA_DIR},destination=/var/lib/postgresql/data \
    --mount type=bind,source=${CONFIG_DIR}/simple-postgresql.conf,destination=/etc/postgresql/postgresql.conf,readonly=true \
    --mount type=bind,source=${CONFIG_DIR}/simple-pg_hba.conf,destination=/etc/postgresql/pg_hba.conf,readonly=true \
    --secret source=${SECRET_NAME},target=/run/secrets/postgres_password \
    --env POSTGRES_USER=postgres \
    --env POSTGRES_PASSWORD_FILE=/run/secrets/postgres_password \
    --env POSTGRES_DB=postgres \
    --env PGDATA=/var/lib/postgresql/data \
    --publish published=5432,target=5432,mode=host \
    --health-cmd="pg_isready -U postgres" \
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
  elif [ ! -f "${MARKER_FILE}" ] && ! docker service inspect "${SERVICE_NAME}" >/dev/null 2>&1; then
    # 既没有标记文件, 也没有现存服务 => 判定为首次运行
    need_init=1
  fi

  if [ "${need_init}" -eq 1 ]; then
    log "========== 首次初始化模式 =========="
    write_config_files
    distribute_config
    ensure_network
    if [ "${FORCE_INIT}" -eq 1 ]; then
      wipe_data_dirs
    fi
    init_data_dirs
    touch "${MARKER_FILE}"
    log "初始化完成, 标记文件: ${MARKER_FILE}"
  else
    log "========== 重新部署模式 (保留数据库数据) =========="
    if [ ! -f "${CONFIG_DIR}/postgres_password.txt" ]; then
      if ! docker secret inspect "${SECRET_NAME}" >/dev/null 2>&1; then
        die "缺少密码文件 ${CONFIG_DIR}/postgres_password.txt, 且 secret 不存在"
      fi
      log "警告: 密码文件缺失, 将复用现有 secret"
    fi
  fi

  remove_service
  ensure_secret
  create_service

  echo
  log "========== 部署完成 =========="
  log "查看服务状态: docker service ps ${SERVICE_NAME}"
  log "查看服务日志: docker service logs -f ${SERVICE_NAME}"
  log "连接数据库  : psql -h <节点IP> -U postgres -d postgres"
}

main "$@"

# 使用说明
# 首次部署
# chmod +x deploy-postgres.sh
# ./deploy-postgres.sh

# 修改配置 / 升级镜像后重新部署（保留数据）：
# ./deploy-postgres.sh

# 需要彻底重置（会清空数据）：
# ./deploy-postgres.sh --force-init