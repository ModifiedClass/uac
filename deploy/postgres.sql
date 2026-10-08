-- ============================================================
--  统一认证中心数据库初始化
--  表由应用启动时自动创建（CREATE TABLE IF NOT EXISTS）
--  此处仅预建索引
-- ============================================================

DO $$
BEGIN
    IF EXISTS (SELECT FROM pg_tables WHERE tablename = 'users') THEN
        CREATE INDEX IF NOT EXISTS idx_users_username ON users(username);
        CREATE INDEX IF NOT EXISTS idx_users_email    ON users(email);
    END IF;
    IF EXISTS (SELECT FROM pg_tables WHERE tablename = 'clients') THEN
        CREATE INDEX IF NOT EXISTS idx_clients_client_id ON clients(client_id);
    END IF;
END $$;