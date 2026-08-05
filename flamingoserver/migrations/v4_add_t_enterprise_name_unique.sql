-- v4_add_t_enterprise_name_unique.sql
--
-- 为 t_enterprise.f_name（企业全称）增加唯一约束 uniq_enterprise_name。
-- 已存在的库需手工执行本脚本；新库由 table.sql 的建表语句、以及 MysqlManager.cpp
-- 里那份运行期建表定义直接带上，无需再跑本脚本。
-- 注意 CMysqlManager::checkTable 只会自动 ALTER ADD COLUMN 补缺失的列，
-- **不会**补索引，所以存量库只能靠本脚本升级。
--
-- 为什么企业全称必须唯一：PC 客户端注册时用户手填公司全称，服务端要靠这个名字
-- 精确定位到唯一一家企业，再把新账号挂进该企业的公司根部门（见
-- chatserversrc/RegisterService.cpp）。若允许重名，"这个人该进哪一家"无从判定。
--
-- SQLite 后端不需要执行本脚本：其索引定义在 sqliteapi/SqliteSchema.cpp 里，
-- 由 CSqliteManager::init 每次启动时以 CREATE UNIQUE INDEX IF NOT EXISTS 重放，
-- 存量库会被自动补上。
--
-- balloonwj@qq.com   2026-08-04

-- 步骤 1：先查重。本句必须返回空结果集，否则步骤 2 会报 1062 Duplicate entry。
-- 查出结果时须人工决定保留哪一家、把另一家改名或合并（企业下挂着员工与部门，
-- 不能简单删除），处理干净后再执行步骤 2。
SELECT f_name, COUNT(*) c FROM t_enterprise GROUP BY f_name HAVING c > 1;

-- 步骤 2：建立唯一约束。
-- MySQL 默认排序规则 utf8_general_ci 不区分大小写，因此 "ACME" 与 "acme" 会被
-- 判为重复 —— 这与注册时按名匹配的口径一致，是预期行为。
ALTER TABLE t_enterprise ADD UNIQUE KEY uniq_enterprise_name (f_name);
