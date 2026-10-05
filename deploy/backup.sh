#!/bin/bash
# ============================================================
#  统一认证中心 — 数据备份脚本
#  建议 crontab：0 3 * * * /opt/uac/deploy/backup.sh
# ============================================================
set -euo pipefail

# ---------- 备份目录（以时间戳命名） ----------
BACKUP_ROOT="/opt/uac/backups"
BACKUP_DIR="${BACKUP_ROOT}/$(date +%Y%m%d_%H%M%S)"
mkdir -p "$BACKUP_DIR"

# ---------- 加载环境变量 ----------
cd /opt/uac/deploy
set -a; source .env; set +a

# ---------- 步骤 1：备份 PostgreSQL ----------
echo ">>> [1/3] 备份 PostgreSQL..."
docker exec uac-postgres pg_dump -U "${POSTGRES_USER}" "${POSTGRES_DB}" \
    | gzip > "$BACKUP_DIR/postgres.sql.gz"

# ---------- 步骤 2：备份 Redis ----------
echo ">>> [2/3] 备份 Redis（BGSAVE + 复制 RDB 文件）..."
docker exec uac-redis redis-cli -a "${REDIS_PASSWORD}" BGSAVE
sleep 2
docker cp uac-redis:/data/dump.rdb "$BACKUP_DIR/redis.rdb" 2>/dev/null || \
    cp /opt/uac/data/redis/dump.rdb "$BACKUP_DIR/redis.rdb" 2>/dev/null || true

# ---------- 步骤 3：清理 7 天前的备份 ----------
echo ">>> [3/3] 清理 7 天前的备份..."
find "$BACKUP_ROOT" -maxdepth 1 -type d -mtime +7 -exec rm -rf {} \; 2>/dev/null || true

# ---------- 结果 ----------
echo ""
echo ">>> 备份完成: $BACKUP_DIR"
ls -lh "$BACKUP_DIR"