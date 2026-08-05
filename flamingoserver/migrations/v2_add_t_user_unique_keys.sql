-- v2_add_t_user_unique_keys.sql
--
-- 为 t_user 增加两个唯一约束（已存在的库需手工执行本脚本；新库由
-- table.sql 的建表语句直接带上，无需再跑本脚本）：
--   1) uk_f_username        —— 登录账号 f_username 全局唯一
--      （f_username 是登录凭据，ChatSession::onLogin 经
--       UserManager::getUserInfoByUsername 按它匹配，必须全局唯一）。
--   2) uk_ent_employee_no   —— 工号 (f_enterprise_id, f_employee_no) 企业内
--      唯一（不同企业可各自复用同一工号，故按企业分组而非全局唯一）。
--
-- balloonwj@qq.com   2026-05-22

-- 步骤 1：把工号空串归一为 NULL。
-- MySQL 的 UNIQUE 约束把多个 NULL 视为互不相同、却把多个空串 '' 视为冲突；
-- 不少历史数据的 f_employee_no 是空串，须先转 NULL，uk_ent_employee_no 才能建上。
UPDATE t_user SET f_employee_no = NULL WHERE f_employee_no = '';

-- 步骤 2：登录账号全局唯一。
-- 若本句报 1062 Duplicate entry，说明已有重复 f_username（数据错误），
-- 须先用下面的查重语句定位并清理后再执行：
--   SELECT f_username, COUNT(*) c FROM t_user GROUP BY f_username HAVING c > 1;
ALTER TABLE t_user ADD UNIQUE KEY uk_f_username (f_username);

-- 步骤 3：工号企业内唯一。
-- 若报 1062 Duplicate entry，先按企业查重并清理：
--   SELECT f_enterprise_id, f_employee_no, COUNT(*) c FROM t_user
--   WHERE f_employee_no IS NOT NULL
--   GROUP BY f_enterprise_id, f_employee_no HAVING c > 1;
ALTER TABLE t_user ADD UNIQUE KEY uk_ent_employee_no (f_enterprise_id, f_employee_no);
