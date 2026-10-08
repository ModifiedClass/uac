-- ============================================================
-- uac 统一认证中心 数据库初始化脚本 (PostgreSQL 14+)
--
-- 测试环境不使用数据库(内存存储); 生产环境使用本脚本初始化:
--   用户/客户端 -> PostgreSQL, 授权码/会话 -> Redis(见部署文档)。
-- 用法: psql -U postgres -d uac -f init.sql
-- ============================================================

-- 1. 扩展: pgcrypto 提供 bcrypt (crypt/gen_salt); argon2 可另装 argon2 扩展或应用层实现
CREATE EXTENSION IF NOT EXISTS pgcrypto;

-- 2. 表结构
CREATE TABLE IF NOT EXISTS users (
    user_id       VARCHAR(64)  PRIMARY KEY,
    username      VARCHAR(64)  NOT NULL UNIQUE,
    password_hash TEXT         NOT NULL,              -- bcrypt: $2a$... (生产禁止 sha256 示例格式)
    name          VARCHAR(128) NOT NULL DEFAULT '',
    phone         VARCHAR(32)  NOT NULL DEFAULT '',
    email         VARCHAR(128) NOT NULL DEFAULT '',
    roles         TEXT[]       NOT NULL DEFAULT '{}', -- 角色列表, 如 ARRAY['admin','user']
    created_at    TIMESTAMPTZ  NOT NULL DEFAULT now(),
    updated_at    TIMESTAMPTZ  NOT NULL DEFAULT now()
);
COMMENT ON TABLE users IS '用户表(密码仅存哈希, 禁止明文)';

CREATE TABLE IF NOT EXISTS clients (
    client_id     VARCHAR(64)  PRIMARY KEY,
    client_secret VARCHAR(128) NOT NULL,              -- 生产建议存哈希或加密字段
    name          VARCHAR(128) NOT NULL DEFAULT '',
    redirect_uris TEXT[]       NOT NULL DEFAULT '{}', -- 回调地址白名单(授权端点校验)
    created_at    TIMESTAMPTZ  NOT NULL DEFAULT now()
);
COMMENT ON TABLE clients IS '接入业务系统(客户端)注册表';

CREATE TABLE IF NOT EXISTS auth_codes (
    code         VARCHAR(128) PRIMARY KEY,
    client_id    VARCHAR(64)  NOT NULL REFERENCES clients(client_id),
    redirect_uri TEXT         NOT NULL,               -- 签发时绑定, 兑换时必须一致
    user_id      VARCHAR(64)  NOT NULL REFERENCES users(user_id),
    scope        TEXT         NOT NULL DEFAULT '',
    expires_at   BIGINT       NOT NULL                -- unix 秒; 授权码 5 分钟(可配置)
);
COMMENT ON TABLE auth_codes IS '授权码(一次性使用: 兑换成功后立即 DELETE)';
CREATE INDEX IF NOT EXISTS idx_auth_codes_expires ON auth_codes (expires_at);

CREATE TABLE IF NOT EXISTS sessions (
    session_id  VARCHAR(128) PRIMARY KEY,
    user_id     VARCHAR(64)  NOT NULL REFERENCES users(user_id),
    expires_at  BIGINT       NOT NULL                 -- unix 秒; 会话 24 小时(可配置)
);
COMMENT ON TABLE sessions IS '登录会话(生产可外置到 Redis 支持多副本)';
CREATE INDEX IF NOT EXISTS idx_sessions_expires ON sessions (expires_at);

-- 3. 初始化数据
-- 密码使用 pgcrypto 的 bcrypt 哈希(cost=10), 演示口令: admin123 / alice123
INSERT INTO users (user_id, username, password_hash, name, phone, email, roles) VALUES
  ('u-1001', 'admin', crypt('admin123', gen_salt('bf', 10)), '系统管理员', '13800000001', 'admin@example.com', ARRAY['admin','user']),
  ('u-1002', 'alice', crypt('alice123', gen_salt('bf', 10)), 'Alice',        '13800000002', 'alice@example.com', ARRAY['user'])
ON CONFLICT (user_id) DO NOTHING;

INSERT INTO clients (client_id, client_secret, name, redirect_uris) VALUES
  ('fastapi-app', 'fastapi-app-secret', 'FastAPI 示例业务',
   ARRAY['http://your-domain.com/callback','http://127.0.0.1:8000/callback','http://localhost:8000/callback'])
ON CONFLICT (client_id) DO NOTHING;

-- 4. 定时清理过期数据(建议 pg_cron 每分钟执行; 或由应用层定时任务完成)
-- CREATE EXTENSION IF NOT EXISTS pg_cron;
-- SELECT cron.schedule('uac-purge-auth-codes', '* * * * *',
--   $$DELETE FROM auth_codes WHERE expires_at <= extract(epoch from now())$$);
-- SELECT cron.schedule('uac-purge-sessions', '* * * * *',
--   $$DELETE FROM sessions WHERE expires_at <= extract(epoch from now())$$);

-- 5. 常用查询示例
-- 校验密码: SELECT (password_hash = crypt('admin123', password_hash)) AS ok FROM users WHERE username='admin';
