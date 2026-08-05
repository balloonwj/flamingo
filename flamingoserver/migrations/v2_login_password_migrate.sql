-- ============================================================
--  FlamingoIM v2 登录改造 · 存量账号密码迁移脚本
--  balloonwj@qq.com   2026-05-21
--
--  用途：把 t_user 中的旧版【明文密码】转换为 v2 的【加盐哈希】格式。
--      v2 口令存储格式： f_password = MD5( SHA256(明文密码) + f_salt )
--      迁移直接利用 MySQL 内置 SHA2() / MD5() 在库内完成，无需取出明文。
--
--  前提：t_user 已含 f_salt、f_usertype 两列
--       （随 table.sql 更新由 MysqlManager.checkTable 自动 ALTER 添加；
--         若未自动添加，可手动执行 table.sql 中的对应 ALTER 语句）。
--
--  幂等：仅处理 f_salt 为空的行；迁移完成后所有行 f_salt 均非空，
--       再次执行影响 0 行，可安全重跑。
--
--  注意：
--   1. 迁移行的盐值由 f_user_id + f_register_time 派生（非真随机），
--      这是为换取“单语句、可重跑”所做的取舍——每个用户盐值各不相同，
--      仍可抵御跨用户彩虹表。新注册账号（cmd_register）一律用真随机盐。
--      若对存量账号有更高安全要求，建议迁移后强制其修改密码。
--   2. t_user 为 utf8 表，SHA2() 对 utf8 字节求值；客户端必须对明文密码的
--      UTF-8 字节做 SHA-256，两端结果才能一致。
--   3. f_usertype 有 DEFAULT 0，存量行自动为“普通用户”，无需数据迁移。
-- ============================================================

UPDATE t_user
SET
    -- 旧明文 → v2 口令： MD5( SHA256(明文) + 派生盐 )
    f_password = MD5(CONCAT(
                     SHA2(f_password, 256),
                     SUBSTRING(MD5(CONCAT('flmg2#', f_user_id, '#', f_register_time)), 1, 24)
                 )),
    -- 写入与上面完全一致的派生盐表达式，保证 f_salt 与 f_password 用的盐一致
    f_salt     = SUBSTRING(MD5(CONCAT('flmg2#', f_user_id, '#', f_register_time)), 1, 24)
WHERE f_salt IS NULL OR f_salt = '';
