sudo tee /etc/docker/redis/redis-uac.conf << 'EOF'
# ============================================
# Redis 8.0 单节点优化配置
# 适用于 Docker / Docker Swarm 部署
# ============================================

# ---------- 基础配置 ----------
port 6379
bind 0.0.0.0
protected-mode yes
tcp-backlog 1024
timeout 0
tcp-keepalive 300

# 容器内不要 daemonize，日志建议输出到 stdout，由 Docker 收集
daemonize no
supervised no
# pidfile 在容器中通常不需要，注释掉
# pidfile /var/run/redis_6379.pid

loglevel notice
# 容器最佳实践：日志输出到 stdout，用 docker logs 查看
logfile ""

# ---------- 数据库 ----------
databases 16
always-show-logo no

# ---------- RDB 快照 ----------
# 保留 RDB 作为备份，AOF 开启时恢复优先使用 AOF
save 900 1
save 300 10
save 60 10000
stop-writes-on-bgsave-error yes
rdbcompression yes
rdbchecksum yes
dbfilename dump.rdb
rdb-del-sync-files no
dir /data


# ---------- 安全配置 ----------
# 密码建议通过 Docker Secret 注入，不要明文写在配置文件
# 使用启动参数：redis-server /path/redis.conf --requirepass $(cat /run/secrets/redis_password)
# requirepass your_strong_password
# masterauth your_strong_password

# ---------- 客户端与内存 ----------
maxclients 10000
# 根据容器内存限制调整，建议 maxmemory 小于容器 limit，留出约 20% 开销
maxmemory 1gb
# 纯缓存场景：allkeys-lru；持久化重要数据：noeviction 或 volatile-lru
maxmemory-policy allkeys-lru

# ---------- AOF 持久化 ----------
appendonly yes
appendfilename "appendonly.aof"
appendfsync everysec
no-appendfsync-on-rewrite no
auto-aof-rewrite-percentage 100
auto-aof-rewrite-min-size 64mb
aof-load-truncated yes
aof-use-rdb-preamble yes

# ---------- Lazy Free（异步释放内存，降低大 key 删除阻塞） ----------
lazyfree-lazy-eviction yes
lazyfree-lazy-expire yes
lazyfree-lazy-server-del yes
lazyfree-lazy-user-del yes
lazyfree-lazy-user-flush yes

# ---------- Lua 脚本 ----------
lua-time-limit 5000

# ---------- 慢日志 ----------
slowlog-log-slower-than 10000
slowlog-max-len 128

# ---------- 事件通知 ----------
notify-keyspace-events ""

# ---------- 高级配置（Redis 7/8 使用 listpack） ----------
hash-max-listpack-entries 512
hash-max-listpack-value 64
list-max-listpack-size -2
list-compress-depth 0
set-max-intset-entries 512
zset-max-listpack-entries 128
zset-max-listpack-value 64
hll-sparse-max-bytes 3000
stream-node-max-bytes 4096
stream-node-max-entries 100

activerehashing yes
client-output-buffer-limit normal 0 0 0
client-output-buffer-limit replica 256mb 64mb 60
client-output-buffer-limit pubsub 32mb 8mb 60
hz 10
dynamic-hz yes
aof-rewrite-incremental-fsync yes
rdb-save-incremental-fsync yes

EOF

printf 'czl.redis' > /tmp/redis-uac_password.txt
docker secret create redis-uac_password /tmp/redis-uac_password.txt
rm -f /tmp/redis-uac_password.txt

mkdir -p /data/redis-uac

docker service create \
  --name uac-redis \
  --network uac-network \
  --replicas 1 \
  --constraint 'node.hostname==manager1' \
  --mount type=bind,source=/etc/docker/redis/redis-uac.conf,destination=/usr/local/etc/redis/redis.conf,readonly \
  --mount type=bind,source=/data/redis-uac,destination=/data \
  --secret source=redis-uac_password,target=/run/secrets/redis_password \
  --limit-memory 1.5G \
  --publish published=16379,target=6379,mode=host \
  192.168.201.194:5000/redis:8.0 \
  redis-server /usr/local/etc/redis/redis.conf
