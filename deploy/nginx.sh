#!/bin/bash
# ============================================================
#  Nginx — Swarm 多副本部署（ingress 模式）
#
#  配置文件来源：脚本同目录下的 conf.d/*.conf
#  （不生成 default.conf，完全由你维护）
#
#  首次执行: 初始化配置 + 数据目录 + 创建 service
#  后续执行: 仅重新部署 service，保留数据
#
#  用法:
#    ./deploy-nginx.sh                # 首次 / 重新部署
#    ./deploy-nginx.sh --force-init   # 强制重新初始化（清空日志!）
# ============================================================
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$SCRIPT_DIR"

RED='\033[0;31m'; GREEN='\033[0;32m'; YELLOW='\033[1;33m'; NC='\033[0m'
info()  { echo -e "${GREEN}[NGINX][INFO]${NC} $*"; }
warn()  { echo -e "${YELLOW}[NGINX][WARN]${NC} $*"; }
error() { echo -e "${RED}[NGINX][ERROR]${NC} $*" >&2; exit 1; }

[ -f .env ] || error "未找到 .env"
set -a; source .env; set +a

IFS=',' read -ra NODE_ARRAY <<< "${NODES}"
REPLICAS="${NGINX_REPLICAS:-3}"

# ---------- 本地配置文件（源） ----------
LOCAL_CONF_DIR="${SCRIPT_DIR}/conf.d"
LOCAL_NGINX_CONF="${SCRIPT_DIR}/nginx.conf"      # 可选：本地主配置，若存在则优先使用
LOCAL_DEFAULT_CONF="${LOCAL_CONF_DIR}/default.conf"

[ -d "${LOCAL_CONF_DIR}" ]            || error "本地配置目录不存在: ${LOCAL_CONF_DIR}"
[ -f "${LOCAL_DEFAULT_CONF}" ]        || error "未找到本地配置: ${LOCAL_DEFAULT_CONF}"
shopt -s nullglob
LOCAL_CONF_FILES=( "${LOCAL_CONF_DIR}"/*.conf )
shopt -u nullglob
[ "${#LOCAL_CONF_FILES[@]}" -gt 0 ]   || error "${LOCAL_CONF_DIR} 下没有 *.conf 文件"

# ---------- 远端/集群资源 ----------
CONFIG_DIR="${CONFIG_BASE_DIR}/nginx"
CONF_D_DIR="${CONFIG_DIR}/conf.d"
HTML_DIR="${NGINX_HTML_DIR:-${CONFIG_DIR}/html}"
LOG_DIR="${NGINX_LOG_DIR:-${CONFIG_DIR}/logs}"
MARKER_FILE="${CONFIG_DIR}/.initialized"

NGINX_SERVICE="${NGINX_SERVICE:-nginx-uac}"
NGINX_HTTP_PORT="${NGINX_HTTP_PORT:-80}"
NGINX_HTTPS_PORT="${NGINX_HTTPS_PORT:-443}"

FORCE_INIT=0
[ "${1:-}" = "--force-init" ] && FORCE_INIT=1

# ---------- 依赖检查 ----------
for cmd in docker ssh scp; do
  command -v "$cmd" >/dev/null 2>&1 || error "未找到命令: $cmd"
done
docker info 2>/dev/null | grep -q "Swarm: active" || error "Swarm 未激活"
docker node ls >/dev/null 2>&1 || error "当前节点不是 Swarm manager"

[ -n "${NGINX_IMAGE:-}" ]    || error "NGINX_IMAGE 未设置"
[ -n "${NETWORK_NAME:-}" ]   || error "NETWORK_NAME 未设置"

# ---------- 私有仓库镜像检查 ----------
if ! docker image inspect "${NGINX_IMAGE}" >/dev/null 2>&1; then
  warn "本地不存在镜像 ${NGINX_IMAGE}，尝试从私有仓库拉取..."
  docker pull "${NGINX_IMAGE}" || \
    error "拉取失败。请先执行: docker login ${NGINX_IMAGE%%/*}"
fi

# ============================================================
# 1. 组装配置（本地文件 -> ${CONFIG_DIR}）
# ============================================================
write_config_files() {
  info "从 ${LOCAL_CONF_DIR} 收集配置文件到 ${CONFIG_DIR}"
  mkdir -p "${CONF_D_DIR}" "${HTML_DIR}" "${LOG_DIR}"

  # 主配置：本地有就用本地的，否则用内置默认
  if [ -f "${LOCAL_NGINX_CONF}" ]; then
    info "  使用本地主配置: ${LOCAL_NGINX_CONF}"
    cp -f "${LOCAL_NGINX_CONF}" "${CONFIG_DIR}/nginx.conf"
  else
    info "  未找到本地 nginx.conf，使用内置默认"
    cat > "${CONFIG_DIR}/nginx.conf" << 'EOF'
user  nginx;
worker_processes  auto;

error_log  /var/log/nginx/error.log warn;
pid        /var/run/nginx.pid;

events {
    worker_connections  10240;
    multi_accept        on;
    use                 epoll;
}

http {
    include       /etc/nginx/mime.types;
    default_type  application/octet-stream;

    log_format  main  '$remote_addr - $remote_user [$time_local] "$request" '
                      '$status $body_bytes_sent "$http_referer" '
                      '"$http_user_agent" "$http_x_forwarded_for"';

    access_log  /var/log/nginx/access.log  main;

    sendfile        on;
    tcp_nopush      on;
    tcp_nodelay     on;
    keepalive_timeout  65;
    types_hash_max_size 2048;
    server_tokens   off;

    client_max_body_size 100m;

    gzip  on;
    gzip_disable "msie6";
    gzip_vary on;
    gzip_proxied any;
    gzip_comp_level 6;
    gzip_types text/plain text/css text/xml application/json
               application/javascript application/xml application/xml+rss
               text/javascript image/svg+xml;

    include /etc/nginx/conf.d/*.conf;
}
EOF
  fi

  # conf.d/*.conf 全部拷贝（先清掉旧的，避免残留）
  rm -f "${CONF_D_DIR}"/*.conf
  for f in "${LOCAL_CONF_FILES[@]}"; do
    info "  + $(basename "$f")"
    cp -f "$f" "${CONF_D_DIR}/"
  done

  chmod 644 "${CONFIG_DIR}/nginx.conf" "${CONF_D_DIR}"/*.conf

  # 本地做一次语法校验，尽量把错误拦在源头
  validate_conf
}

# ============================================================
# 1.1 语法校验（可选，需要本地有 docker）
# ============================================================
validate_conf() {
  info "本地 nginx -t 语法校验..."
  if docker run --rm \
      -v "${CONFIG_DIR}/nginx.conf:/etc/nginx/nginx.conf:ro" \
      -v "${CONF_D_DIR}:/etc/nginx/conf.d:ro" \
      "${NGINX_IMAGE}" nginx -t 2>&1 | sed 's/^/    /'; then
    info "语法校验通过"
  else
    error "nginx -t 校验失败，请检查 ${LOCAL_CONF_DIR} 下的配置"
  fi
}

# ============================================================
# 2. 分发配置到所有节点（每个副本可能落在任意节点）
# ============================================================
distribute_config() {
  info "分发 Nginx 配置到所有节点..."
  for NODE_IP in "${NODE_ARRAY[@]}"; do
    info "  -> ${NODE_IP}"
    ssh -p "${SSH_PORT}" -o StrictHostKeyChecking=no "${SSH_USER}@${NODE_IP}" \
        "mkdir -p ${CONFIG_DIR} ${CONF_D_DIR} ${HTML_DIR} ${LOG_DIR}" 2>/dev/null || true

    # 主配置
    scp -P "${SSH_PORT}" -o StrictHostKeyChecking=no -q \
        "${CONFIG_DIR}/nginx.conf" \
        "${SSH_USER}@${NODE_IP}:${CONFIG_DIR}/nginx.conf"

    # conf.d 全量
    scp -P "${SSH_PORT}" -o StrictHostKeyChecking=no -q \
        "${CONF_D_DIR}"/*.conf \
        "${SSH_USER}@${NODE_IP}:${CONF_D_DIR}/"

    ssh -p "${SSH_PORT}" -o StrictHostKeyChecking=no "${SSH_USER}@${NODE_IP}" "
        set -e
        chmod 644 ${CONFIG_DIR}/nginx.conf ${CONF_D_DIR}/*.conf
        chown -R 101:101 ${LOG_DIR} 2>/dev/null || true
        chmod 755 ${LOG_DIR}
    " 2>/dev/null || true
  done
}

# ============================================================
# 3. 初始化数据目录（首次）
# ============================================================
init_data_dirs() {
  info "初始化所有节点数据目录..."
  for NODE_IP in "${NODE_ARRAY[@]}"; do
    info "  -> ${NODE_IP}"
    ssh -p "${SSH_PORT}" -o StrictHostKeyChecking=no "${SSH_USER}@${NODE_IP}" "
      set -e
      mkdir -p ${HTML_DIR} ${LOG_DIR}
      chown -R 101:101 ${LOG_DIR} 2>/dev/null || true
      chmod 755 ${HTML_DIR} ${LOG_DIR}
      echo '    数据目录已就绪'
    "
  done
}

# ============================================================
# 4. 清空日志（--force-init）
# ============================================================
wipe_data_dirs() {
  warn "!!! 清空所有节点日志目录（--force-init） !!!"
  for NODE_IP in "${NODE_ARRAY[@]}"; do
    info "  -> ${NODE_IP}"
    ssh -p "${SSH_PORT}" -o StrictHostKeyChecking=no "${SSH_USER}@${NODE_IP}" "
      rm -rf ${LOG_DIR}/* 2>/dev/null || true
      mkdir -p ${LOG_DIR}
      chown -R 101:101 ${LOG_DIR} 2>/dev/null || true
      chmod 755 ${LOG_DIR}
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
# 6. 检查上游服务（auth-center）是否接入同一网络
# ============================================================
check_upstream() {
  local upstream_svc="auth-center"
  if ! docker service inspect "${upstream_svc}" >/dev/null 2>&1; then
    warn "未找到 service '${upstream_svc}'，Nginx 运行时可能返回 502"
    warn "  请先部署 ${upstream_svc}，并确保其接入网络 '${NETWORK_NAME}'"
    return
  fi
  # 简单检查它是否在同一网络
  if docker service inspect "${upstream_svc}" \
      --format '{{range .Spec.TaskTemplate.Networks}}{{.Target}}{{end}}' \
      | grep -q "$(docker network inspect "${NETWORK_NAME}" --format '{{.Id}}' 2>/dev/null | cut -c1-12)" ; then
    info "上游 service '${upstream_svc}' 已接入 ${NETWORK_NAME}"
  else
    warn "上游 service '${upstream_svc}' 可能未接入 ${NETWORK_NAME}，请确认"
  fi
}

# ============================================================
# 7. 删除旧 service
# ============================================================
remove_service() {
  if docker service inspect "${NGINX_SERVICE}" >/dev/null 2>&1; then
    info "删除旧 service ${NGINX_SERVICE}"
    docker service rm "${NGINX_SERVICE}" >/dev/null
    for i in $(seq 1 60); do
      docker service inspect "${NGINX_SERVICE}" >/dev/null 2>&1 || break
      sleep 1
    done
    info "等待容器释放资源..."
    sleep 3
  else
    info "service ${NGINX_SERVICE} 不存在, 跳过删除"
  fi
}

# ============================================================
# 8. 创建 service（多副本，ingress 模式）
# ============================================================
create_nginx_service() {
  info "创建 service ${NGINX_SERVICE}（replicas=${REPLICAS}, ingress 模式）"
  docker service create \
    --name "${NGINX_SERVICE}" \
    --network "${NETWORK_NAME}" \
    --replicas "${REPLICAS}" \
    --with-registry-auth \
    --mount type=bind,source=${CONFIG_DIR}/nginx.conf,destination=/etc/nginx/nginx.conf,readonly=true \
    --mount type=bind,source=${CONF_D_DIR},destination=/etc/nginx/conf.d,readonly=true \
    --mount type=bind,source=${HTML_DIR},destination=/usr/share/nginx/html \
    --mount type=bind,source=${LOG_DIR},destination=/var/log/nginx \
    --publish published=${NGINX_HTTP_PORT},target=80,mode=ingress \
    --health-cmd="pgrep nginx >/dev/null || exit 1" \
    --health-interval=15s \
    --health-timeout=5s \
    --health-retries=3 \
    --restart-condition=on-failure \
    --restart-delay=10s \
    --restart-max-attempts=5 \
    "${NGINX_IMAGE}"
}

# ============================================================
# 主流程
# ============================================================
main() {
  local need_init=0

  if [ "${FORCE_INIT}" -eq 1 ]; then
    need_init=1
  elif [ ! -f "${MARKER_FILE}" ] && ! docker service inspect "${NGINX_SERVICE}" >/dev/null 2>&1; then
    need_init=1
  fi

  ensure_network
  check_upstream

  if [ "${need_init}" -eq 1 ]; then
    info "========== 首次初始化 Nginx =========="
    write_config_files
    if [ "${FORCE_INIT}" -eq 1 ]; then
      wipe_data_dirs
    fi
    init_data_dirs
    distribute_config
    mkdir -p "$(dirname "${MARKER_FILE}")"
    touch "${MARKER_FILE}"
    info "初始化完成, 标记: ${MARKER_FILE}"
  else
    info "========== 重新部署 Nginx（保留数据） =========="
    write_config_files
    distribute_config
  fi

  remove_service
  create_nginx_service

  echo
  info "Nginx service 部署完成（副本数=${REPLICAS}）"
  info "查看副本状态: docker service ps ${NGINX_SERVICE}"
  info "查看服务日志: docker service logs -f ${NGINX_SERVICE}"
  info "访问地址    : http://${SERVER_IP}:${NGINX_HTTP_PORT}/"
  info "健康探测    : curl -H 'Host: auth.your-domain.com' http://${SERVER_IP}/"
}

main "$@"