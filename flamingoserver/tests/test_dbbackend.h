/**
 *  测试用的数据库后端切换，test_dbbackend.h
 *
 *  几套要真连数据库的用例（CVU 未读口径、CVE 会话摘要、CVB 计时）原先各自写死了
 *  SQLite。但产品支持 MySQL 与 SQLite 两种后端，而两边的差异不只是语法 —— 索引行为
 *  就完全不同（InnoDB 的二级索引自带主键值，SQLite 不带），只测一边等于没测另一边。
 *
 *  故把"建一个干净的测试库并把 CDbConfig 指过去"这件事收到这里，按环境变量选后端：
 *
 *      默认（不设任何环境变量）           跑 SQLite，行为与从前一致
 *      FLAMINGO_TEST_MYSQL=1              跑 MySQL
 *
 *  MySQL 连接参数同样取自环境变量，缺省值与 etc/chatserver.conf 里那组一致：
 *      FLAMINGO_TEST_MYSQL_HOST（默认 127.0.0.1）
 *      FLAMINGO_TEST_MYSQL_USER（默认 root）
 *      FLAMINGO_TEST_MYSQL_PWD （默认 123456）
 *
 *  <u>MySQL 模式只有 flamingo_tests_mysql 这个构建目标能用</u>：默认的 flamingo_tests
 *  是带 FLAMINGO_NO_MYSQL 编的，DbFactory 里根本没有 MySQL 分支（见 CMakeLists.txt）。
 *
 *  balloonwj@qq.com   2026-08-12
 */
#pragma once

namespace testdb {

/**
 *  本次运行是否按 MySQL 后端跑。读环境变量 FLAMINGO_TEST_MYSQL，值为 "1" 时为真。
 */
bool useMysql();

/**
 *  重建一个干净的测试库，并把全局 CDbConfig 指到它上面。
 *
 *  两种后端的建库方式刻意<u>都走产品自己的那条路</u>，这样"新建的库结构对不对"
 *  （比如某条索引是不是真的建上了）也一并被测到：
 *    · SQLite：删文件后由 CSqliteManager::init 建成完整结构库；
 *    · MySQL：DROP 掉整个库再由 CMysqlManager::init 建库建表。
 *
 *    sqliteFile：SQLite 模式下用的临时 .db 文件名（相对当前目录）。
 *    mysqlDb：   MySQL 模式下用的库名；<u>会被整个 DROP 重建</u>，务必用测试专用的名字。
 *
 *  失败时（连不上 MySQL、建库失败等）内部会以 doctest 的 REQUIRE 中止该用例。
 */
void resetDb(const char* sqliteFile, const char* mysqlDb);

/**
 *  收尾清理。SQLite 模式删掉临时库文件（连同 -wal / -shm）；MySQL 模式<u>什么都不做</u> ——
 *  库留在那儿便于事后排查，下次 resetDb 会把它整个重建。
 *    sqliteFile：与 resetDb 传入的同一个文件名。
 */
void cleanupDb(const char* sqliteFile);

} // namespace testdb
