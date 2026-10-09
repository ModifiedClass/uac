#!/bin/bash
# ============================================================
#  PostgreSQL 18 — Swarm 单副本部署
#  overlay 网络
# ============================================================
docker network create --driver overlay --attachable "uac-network"


# ============================================================
#  创建 service（单副本）
# ============================================================
docker service create \
  --name uac-postgre18 \
  --network uac-network \
  --replicas 1 \
  --constraint 'node.hostname==manager1' \
  --mount type=bind,source=/data/postgresql18,destination=/var/lib/postgresql/data \
  --mount type=bind,source=/etc/docker/postgre/simple-postgresql.conf,destination=/etc/postgresql/postgresql.conf,readonly=true \
  --mount type=bind,source=/etc/docker/postgre/simple-pg_hba.conf,destination=/etc/postgresql/pg_hba.conf,readonly=true \
  --secret source=postgres_password,target=/run/secrets/postgres_password \
  --env POSTGRES_USER=uac \
  --env POSTGRES_PASSWORD_FILE=/run/secrets/postgres_password \
  --env POSTGRES_DB=uac \
  --env PGDATA=/var/lib/postgresql/data \
  --publish published=15432,target=5432,mode=host \
  --health-cmd="pg_isready -U postgres" \
  --health-interval=15s \
  --health-timeout=10s \
  --health-retries=5 \
  --restart-condition=on-failure \
  --restart-delay=10s \
  --restart-max-attempts=5 \
  192.168.201.194:5000/postgres:18 \
  -c config_file=/etc/postgresql/postgresql.conf \
  -c hba_file=/etc/postgresql/pg_hba.conf
