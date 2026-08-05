-- ============================================================
--  FlamingoIM v2 消息收发改造 · 废弃旧 t_chatmsg 表
--  balloonwj@qq.com   2026-05-21
--
--  用途：删除 v1 的旧 t_chatmsg 表（结构为 f_senderid/f_targetid/
--      f_msgcontent BLOB），以便 chatserver 下次启动时由
--      CMysqlManager::checkTable 按 v2 新结构重新建表。
--
--  v2 新结构（见 table.sql / MysqlManager.cpp 第 3 块）：
--      f_msgid / f_clientmsgid / f_ctype / f_from / f_to /
--      f_msgtype / f_content / f_recalled / f_create_time
--
--  注意：
--   1. v1 与 v2 协议不兼容，旧聊天记录无法直接迁移到新结构，
--      本脚本直接丢弃旧数据。若需保留旧记录，请先自行备份 t_chatmsg。
--   2. 执行本脚本后，启动 chatserver 即会自动重建空的 v2 t_chatmsg。
--   3. 幂等：使用 DROP TABLE IF EXISTS，可安全重复执行。
-- ============================================================

DROP TABLE IF EXISTS t_chatmsg;
