-- ============================================================
--  FlamingoIM v2 · 为 t_user 补齐 f_salt / f_usertype 两列
--  balloonwj@qq.com   2026-05-22
--
--  背景：v2 登录改造给 t_user 新增了 f_salt(密码盐) 与 f_usertype(用户类型)。
--      table.sql 已含这两列，但 MysqlManager 的 t_user 建表定义(STableInfo)
--      遗漏了它们，导致 chatserver 启动时 checkTable 不会自动补列；存量库的
--      t_user 因此缺列，UserManager 加载用户的 SELECT 报错：
--          errno 1054, Unknown column 'f_salt' in 'field list'
--
--  用途：仅调整表结构(ADD COLUMN)，不修改任何已有数据。
--  幂等：用 information_schema 判断列是否已存在，已存在则跳过，可重复执行。
--  兼容：不使用 "ADD COLUMN IF NOT EXISTS"(MySQL 5.7 不支持)，改用
--      information_schema + PREPARE 语句，对 MySQL 5.7/8.0 与 MariaDB 通用。
--
--  执行前：须先选中目标库(本项目为 flamingo)——
--      命令行： mysql -h<host> -u<user> -p<pwd> flamingo < 本脚本
--      或在客户端里先执行： USE flamingo;
--
--  执行后：存量账号的 f_salt 为 NULL，其登录口令校验会失败，需再执行
--      migrations/v2_login_password_migrate.sql 完成口令数据迁移；
--      f_usertype 有 DEFAULT 0，存量行自动为“普通用户”，无需数据迁移。
-- ============================================================

-- ---- 1. f_salt：密码盐值，置于 f_password 之后 ----
SET @col_exists := (
    SELECT COUNT(*) FROM information_schema.COLUMNS
    WHERE TABLE_SCHEMA = DATABASE()
      AND TABLE_NAME   = 't_user'
      AND COLUMN_NAME  = 'f_salt'
);
SET @ddl := IF(@col_exists = 0,
    "ALTER TABLE t_user ADD COLUMN f_salt varchar(32) DEFAULT NULL COMMENT '密码盐值，注册时生成（v2登录口令存储用）' AFTER f_password",
    "SELECT 'column t_user.f_salt already exists, skipped' AS info");
PREPARE stmt FROM @ddl;
EXECUTE stmt;
DEALLOCATE PREPARE stmt;

-- ---- 2. f_usertype：用户类型，置于 f_status 之后 ----
SET @col_exists := (
    SELECT COUNT(*) FROM information_schema.COLUMNS
    WHERE TABLE_SCHEMA = DATABASE()
      AND TABLE_NAME   = 't_user'
      AND COLUMN_NAME  = 'f_usertype'
);
SET @ddl := IF(@col_exists = 0,
    "ALTER TABLE t_user ADD COLUMN f_usertype int(2) NOT NULL DEFAULT 0 COMMENT '用户类型：0=普通用户 1=服务号用户' AFTER f_status",
    "SELECT 'column t_user.f_usertype already exists, skipped' AS info");
PREPARE stmt FROM @ddl;
EXECUTE stmt;
DEALLOCATE PREPARE stmt;

-- ---- 3. 校验：执行后应看到 f_salt 与 f_usertype 两行 ----
SELECT COLUMN_NAME, COLUMN_TYPE, IS_NULLABLE, COLUMN_DEFAULT, COLUMN_COMMENT
FROM information_schema.COLUMNS
WHERE TABLE_SCHEMA = DATABASE()
  AND TABLE_NAME   = 't_user'
  AND COLUMN_NAME IN ('f_salt', 'f_usertype')
ORDER BY ORDINAL_POSITION;
