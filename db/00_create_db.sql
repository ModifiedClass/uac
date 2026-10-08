-- uac 统一认证中心 —— 建库脚本(单独执行, 不能放在事务块内)
-- 用法: psql -U postgres -f 00_create_db.sql

CREATE DATABASE uac
  WITH ENCODING 'UTF8'
       TEMPLATE template0
       LC_COLLATE 'C'
       LC_CTYPE 'C';
