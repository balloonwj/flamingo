/**
 *  SQLite 方言文本翻译的单元测试（Suite SQT），test_sqlite_translator.cpp
 *  balloonwj@qq.com   2026-07-31
 *
 *  覆盖两件纯逻辑的事：INSERT IGNORE INTO 的改写，以及 SQLite 规则的字符串转义。
 *  两者都不依赖 sqlite3 句柄，属于纯函数测试。
 */
#include <string>
#include <vector>

#include "doctest.h"
#include "../sqliteapi/SqliteSqlTranslator.h"

//测试里反复用到的转义辅助：按最坏情况（全是单引号）分配缓冲区，
//调用 EscapeSqlString 后把结果连同长度一起返回，便于校验含 '\0' 的数据。
static std::string escapeToString(const std::string& src, int32_t& outWritten)
{
    std::vector<char> buffer(src.size() * 2 + 1, '\0');
    outWritten = sqliteapi::EscapeSqlString(&buffer[0], src.c_str(),
                                            static_cast<uint32_t>(src.size()));
    return std::string(&buffer[0], static_cast<size_t>(outWritten));
}

TEST_CASE("SQT1-SQT4 INSERT IGNORE 的正常改写")
{
    //SQT1 最常见的形态，与业务代码里 13 处写法一致
    CHECK(sqliteapi::TranslateInsertIgnore(
              "INSERT IGNORE INTO t_group_member(f_groupid) VALUES(1)")
          == "INSERT OR IGNORE INTO t_group_member(f_groupid) VALUES(1)");

    //SQT2 关键字大小写不敏感
    CHECK(sqliteapi::TranslateInsertIgnore("insert ignore into t_x VALUES(1)")
          == "INSERT OR IGNORE INTO t_x VALUES(1)");
    CHECK(sqliteapi::TranslateInsertIgnore("Insert Ignore Into t_x VALUES(1)")
          == "INSERT OR IGNORE INTO t_x VALUES(1)");

    //SQT3 关键字之间允许多个空白与换行，改写后统一成单个空格
    CHECK(sqliteapi::TranslateInsertIgnore("INSERT   IGNORE\n\tINTO t_x VALUES(1)")
          == "INSERT OR IGNORE INTO t_x VALUES(1)");

    //SQT4 语句前的空白原样保留，不影响改写
    CHECK(sqliteapi::TranslateInsertIgnore("  INSERT IGNORE INTO t_x VALUES(1)")
          == "  INSERT OR IGNORE INTO t_x VALUES(1)");
}

TEST_CASE("SQT5 不该改写的语句原样返回")
{
    //普通 INSERT INTO 不含 IGNORE，不改写
    CHECK(sqliteapi::TranslateInsertIgnore("INSERT INTO t_x VALUES(1)")
          == "INSERT INTO t_x VALUES(1)");

    //SELECT / UPDATE / DELETE 不受影响
    CHECK(sqliteapi::TranslateInsertIgnore("SELECT f_a FROM t_x WHERE f_b = 1")
          == "SELECT f_a FROM t_x WHERE f_b = 1");
    CHECK(sqliteapi::TranslateInsertIgnore("UPDATE t_x SET f_a = 1")
          == "UPDATE t_x SET f_a = 1");

    //空串
    CHECK(sqliteapi::TranslateInsertIgnore("") == "");

    //只有 INSERT、后面不跟 IGNORE INTO 的残缺片段不改写
    CHECK(sqliteapi::TranslateInsertIgnore("INSERT IGNORE t_x") == "INSERT IGNORE t_x");
    CHECK(sqliteapi::TranslateInsertIgnore("INSERT") == "INSERT");
}

TEST_CASE("SQT6-SQT7 字符串字面量里的关键字不被误伤")
{
    //SQT6 字面量内部恰好出现关键字序列（例如往表里存一段 SQL 文本），必须原样保留
    CHECK(sqliteapi::TranslateInsertIgnore(
              "INSERT IGNORE INTO t_log(f_sql) VALUES('INSERT IGNORE INTO t_y')")
          == "INSERT OR IGNORE INTO t_log(f_sql) VALUES('INSERT IGNORE INTO t_y')");

    //整条语句只有字面量里含关键字时，一个字都不该改
    CHECK(sqliteapi::TranslateInsertIgnore("SELECT 'INSERT IGNORE INTO t_y' AS f_a")
          == "SELECT 'INSERT IGNORE INTO t_y' AS f_a");

    //SQT7 字面量内部连续两个单引号是转义的单引号，不算字面量结束，
    //     后面的关键字仍在字面量内，不能被改写
    CHECK(sqliteapi::TranslateInsertIgnore(
              "SELECT 'it''s INSERT IGNORE INTO here' AS f_a")
          == "SELECT 'it''s INSERT IGNORE INTO here' AS f_a");

    //字面量正常闭合后，其后的关键字应当被改写
    CHECK(sqliteapi::TranslateInsertIgnore(
              "SELECT 'a''b'; INSERT IGNORE INTO t_x VALUES(1)")
          == "SELECT 'a''b'; INSERT OR IGNORE INTO t_x VALUES(1)");
}

TEST_CASE("SQT8 标识符边界不被误伤")
{
    //左边界：INSERT 前面粘着别的标识符字符
    CHECK(sqliteapi::TranslateInsertIgnore("MYINSERT IGNORE INTO t_x")
          == "MYINSERT IGNORE INTO t_x");

    //右边界：INTO 后面粘着别的标识符字符
    CHECK(sqliteapi::TranslateInsertIgnore("INSERT IGNORE INTOX t_x")
          == "INSERT IGNORE INTOX t_x");
}

TEST_CASE("SQT9 一条语句里多处出现时全部改写")
{
    CHECK(sqliteapi::TranslateInsertIgnore(
              "INSERT IGNORE INTO t_a VALUES(1); INSERT IGNORE INTO t_b VALUES(2)")
          == "INSERT OR IGNORE INTO t_a VALUES(1); INSERT OR IGNORE INTO t_b VALUES(2)");
}

TEST_CASE("SQT10-SQT12 EscapeSqlString 的转义规则")
{
    int32_t written = 0;

    //SQT10 单引号翻倍
    CHECK(escapeToString("it's", written) == "it''s");
    CHECK(written == 5);

    //连续多个单引号逐个翻倍
    CHECK(escapeToString("''", written) == "''''");

    //SQT11 反斜杠**不**转义 —— 这是与 MySQL 最容易写错的差异点
    CHECK(escapeToString("C:\\path\\file", written) == "C:\\path\\file");
    CHECK(escapeToString("\\'", written) == "\\''");

    //SQT12 无需转义的普通串与空串原样返回
    CHECK(escapeToString("plain text 123", written) == "plain text 123");
    CHECK(written == 14);
    CHECK(escapeToString("", written) == "");
    CHECK(written == 0);
}

TEST_CASE("SQT13-SQT15 EscapeSqlString 的边界情况")
{
    int32_t written = 0;

    //SQT13 中文等多字节内容不被破坏（本文件为 UTF-8，字面量即 UTF-8 字节序列）
    std::string chinese = "群名称'测试";
    std::string escaped = escapeToString(chinese, written);
    CHECK(escaped == "群名称''测试");
    //只多出一个字节，就是翻倍出来的那个单引号
    CHECK(static_cast<size_t>(written) == chinese.size() + 1);

    //SQT14 返回值必须等于实际写入的字节数
    std::vector<char> buffer(64, '\xCC');
    const char* src = "a'b'c";
    written = sqliteapi::EscapeSqlString(&buffer[0], src, 5);
    CHECK(written == 7);
    CHECK(std::string(&buffer[0], 7) == "a''b''c");
    //结尾必须补上 '\0'
    CHECK(buffer[7] == '\0');

    //SQT15 源串含 '\0' 时按 uSize 指定的长度处理，不能当成 C 字符串提前截断
    char withNul[6] = { 'a', '\0', 'b', '\'', 'c', '\0' };
    std::vector<char> buffer2(32, '\0');
    written = sqliteapi::EscapeSqlString(&buffer2[0], withNul, 5);
    CHECK(written == 6);
    CHECK(std::string(&buffer2[0], 6) == std::string("a\0b''c", 6));
}

TEST_CASE("SQT16 EscapeSqlString 的空指针防护")
{
    char buffer[8] = { 0 };

    CHECK(sqliteapi::EscapeSqlString(NULL, "abc", 3) == 0);
    CHECK(sqliteapi::EscapeSqlString(buffer, NULL, 3) == 0);
}
