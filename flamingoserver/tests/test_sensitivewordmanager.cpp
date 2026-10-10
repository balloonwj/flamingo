/**
 *  企业级敏感词库管理器单元测试（Suite SWD），test_sensitivewordmanager.cpp
 *
 *  覆盖 chatserversrc/SensitiveWordManager：导入导出文本格式与分类名称等纯函数（SWD-T），
 *  以及在临时库上的词库增删改查、判重、上限、校验错误、企业隔离与总开关、匹配器缓存失效、
 *  拦截记录的写入查询与过期清理、批量导入、导出后再导入、多段文字检查（SWD-1～SWD-11）；
 *  删除企业时的清理与拦截记录的保留截止时刻（SWD-12、SWD-13，2d 新增）。
 *  用例清单与预期见 docs/敏感词方案.md 第 9 节 1b 与第 10.1 节。
 *
 *  临时库默认用 SQLite，设 FLAMINGO_TEST_MYSQL=1 时跑 MySQL（见 test_dbbackend.h）。
 *
 *  balloonwj@qq.com   2026-10-02
 */
#include "doctest.h"
#include "../chatserversrc/SensitiveWordManager.h"
#include "../dbapi/IDatabase.h"
#include "../dbapi/DbFactory.h"
#include "../base/Singleton.h"
#include "test_dbbackend.h"

#include <algorithm>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

using namespace chatserver;

namespace {

// 本套用例专用的临时库：SQLite 模式下的文件名、MySQL 模式下的库名（会被整个 DROP 重建）。
const char* const kSwdSqliteFile = "flamingo_sqlite_sensitivewordmanager_test.db";
const char* const kSwdMysqlDb    = "flamingo_test_sensitivewordmanager";

// 两个测试企业的ID（任取，不与其它用例冲突即可）与一个不存在的企业ID。
const int64_t kEntA       = 9001;
const int64_t kEntB       = 9002;
const int64_t kEntMissing = 9999;

// 添加词条的管理员。
const int64_t kAdminId = 7;
const char* const kAdminName = "admin";

// 本套用例统一的基准时间戳（Unix 毫秒）。
const int64_t kNow = 1700000000000LL;

// 一天的毫秒数，用来造"多少天之前"的拦截记录。
const int64_t kOneDayMs = 24LL * 3600 * 1000;

// U+FFFD 的 UTF-8 编码：4 字节字符写库前被替换成它。
const char* const kReplacementChar = "\xEF\xBF\xBD";

// 一个 4 字节 UTF-8 的表情（U+1F600），用字节写法以免受源文件编码影响。
const char* const kEmoji = "\xF0\x9F\x98\x80";

// SWD-3 往库里直接写入的词条条数：比上限少一条。
const int kNearlyFull = SENSITIVE_WORDS_PER_ENTERPRISE - 1;

// SWD-3 直接写库时每条 INSERT 语句带的行数。
const int kRowsPerInsert = 500;

// 执行一条 SQL，失败时中止用例。
void execSql(const std::string& sql)
{
    std::unique_ptr<IDatabase> pConn(DbFactory::createConnection());
    REQUIRE(pConn);
    REQUIRE(pConn->execute(sql.c_str()));
}

// 插入一个企业；t_enterprise 上没有默认值的 NOT NULL 列都要给。
void addEnterprise(int64_t enterpriseId, const std::string& name)
{
    std::ostringstream os;
    os << "INSERT INTO t_enterprise(f_enterprise_id, f_name, f_create_time) VALUES("
       << enterpriseId << ", '" << name << "', '2026-10-02 10:00:00')";
    execSql(os.str());
}

// 重建测试库、写入两个企业，并清掉管理器里这两个企业的匹配器缓存（管理器是单例，
// 用例之间共享）。
SensitiveWordManager& resetSwdDb()
{
    testdb::resetDb(kSwdSqliteFile, kSwdMysqlDb);
    addEnterprise(kEntA, "entA");
    addEnterprise(kEntB, "entB");

    SensitiveWordManager& mgr = Singleton<SensitiveWordManager>::Instance();
    REQUIRE(mgr.init());
    mgr.invalidate(kEntA);
    mgr.invalidate(kEntB);
    mgr.invalidate(kEntMissing);
    return mgr;
}

// 添加一个词条并要求成功，返回新词条ID。
int64_t addOk(SensitiveWordManager& mgr, int64_t enterpriseId, const std::string& word,
              int category, const std::string& remark, int64_t time)
{
    int64_t id = 0;
    REQUIRE(mgr.addWord(enterpriseId, word, category, remark, kAdminId, kAdminName, time, &id)
            == sensitive_word_op_ok);
    REQUIRE(id > 0);
    return id;
}

// 查某企业第一页（每页最大条数）的全部词条。
std::vector<SensitiveWordRecord> listAll(SensitiveWordManager& mgr, int64_t enterpriseId, int& total)
{
    SensitiveWordQuery q;
    q.m_enterpriseId = enterpriseId;
    q.m_pageSize = SENSITIVE_MAX_PAGE_SIZE;
    std::vector<SensitiveWordRecord> out;
    REQUIRE(mgr.listWords(q, out, total));
    return out;
}

// 检查一段文字是否命中某企业的词库。
bool hits(SensitiveWordManager& mgr, int64_t enterpriseId, const std::string& text)
{
    return mgr.checkTexts(enterpriseId, std::vector<std::string>(1, text), NULL);
}

// 造一条拦截记录。
SensitiveHitRecord makeHit(int64_t enterpriseId, int64_t userId, const std::string& userName,
                           int source, int category, int64_t time)
{
    SensitiveHitRecord r;
    r.m_enterpriseId = enterpriseId;
    r.m_userId = userId;
    r.m_userName = userName;
    r.m_source = source;
    r.m_ctype = 2;
    r.m_cid = 4000000001LL;
    r.m_targetName = u8"项目组";
    r.m_wordId = 5;
    r.m_word = u8"敏感";
    r.m_category = category;
    r.m_snippet = u8"前文敏感后文";
    r.m_createTime = time;
    return r;
}

// 重复一个字符串 n 次。
std::string Repeat(const std::string& s, int n)
{
    std::string out;
    for (int i = 0; i < n; ++i)
        out += s;
    return out;
}

// 按 \n 切分导出的文字。
std::vector<std::string> SplitLines(const std::string& text)
{
    std::vector<std::string> lines;
    std::string cur;
    for (size_t i = 0; i < text.size(); ++i)
    {
        if (text[i] == '\n')
        {
            lines.push_back(cur);
            cur.clear();
        }
        else
        {
            cur += text[i];
        }
    }
    if (!cur.empty())
        lines.push_back(cur);
    return lines;
}

} // anonymous namespace

// =====================================================================
// SWD-T：纯函数
// =====================================================================

TEST_CASE("SWD-T 分类名称、导入行解析、导出行格式与 4 字节字符替换")
{
    //分类名称与取值互查
    CHECK(SensitiveWordManager::CategoryName(sensitive_category_illegal) == std::string(u8"违法违规"));
    CHECK(SensitiveWordManager::CategoryName(sensitive_category_vulgar) == std::string(u8"色情低俗"));
    CHECK(SensitiveWordManager::CategoryName(sensitive_category_abuse) == std::string(u8"攻击辱骂"));
    CHECK(SensitiveWordManager::CategoryName(sensitive_category_secret) == std::string(u8"商业机密"));
    CHECK(SensitiveWordManager::CategoryName(sensitive_category_ad) == std::string(u8"广告引流"));
    CHECK(SensitiveWordManager::CategoryName(sensitive_category_other) == std::string(u8"其他"));
    CHECK(SensitiveWordManager::CategoryName(99) == std::string(u8"其他"));
    CHECK(SensitiveWordManager::ParseCategoryName(u8"违法违规") == sensitive_category_illegal);
    CHECK(SensitiveWordManager::ParseCategoryName(u8" 广告引流 ") == sensitive_category_ad);
    CHECK(SensitiveWordManager::ParseCategoryName(u8"不存在的分类") == 0);

    //导入行：一列、两列、三列，分类名写错时取默认分类，各列去掉首尾空白
    std::string word;
    int category = 0;
    std::string remark;
    REQUIRE(SensitiveWordManager::ParseImportLine(u8"词一", sensitive_category_other, word, category, remark));
    CHECK(word == std::string(u8"词一"));
    CHECK(category == sensitive_category_other);
    CHECK(remark.empty());

    REQUIRE(SensitiveWordManager::ParseImportLine(u8"词二\t违法违规", sensitive_category_other,
                                                  word, category, remark));
    CHECK(word == std::string(u8"词二"));
    CHECK(category == sensitive_category_illegal);

    REQUIRE(SensitiveWordManager::ParseImportLine(u8"  词三  \t 不存在的分类 \t 备注三 \r",
                                                  sensitive_category_ad, word, category, remark));
    CHECK(word == std::string(u8"词三"));
    CHECK(category == sensitive_category_ad);
    CHECK(remark == std::string(u8"备注三"));

    //整行只有空白：返回 false，调用方忽略该行
    CHECK_FALSE(SensitiveWordManager::ParseImportLine("", sensitive_category_other, word, category, remark));
    CHECK_FALSE(SensitiveWordManager::ParseImportLine("  \t ", sensitive_category_other, word, category, remark));

    //词条列为空但该行不是空行：返回 true、词条为空，由调用方按"为空"跳过
    REQUIRE(SensitiveWordManager::ParseImportLine(u8"\t违法违规", sensitive_category_other,
                                                  word, category, remark));
    CHECK(word.empty());

    //导出行：词条与备注里的制表符、回车、换行替换为空格
    CHECK(SensitiveWordManager::FormatExportLine(u8"词", sensitive_category_illegal, "a\tb\r\nc")
          == std::string(u8"词\t违法违规\ta b  c"));
    CHECK(SensitiveWordManager::FormatExportLine(u8"词", sensitive_category_other, "")
          == std::string(u8"词\t其他\t"));

    //4 字节字符替换为 U+FFFD，其余原样
    CHECK(SensitiveWordManager::ReplaceFourByteChars(std::string("a") + kEmoji + u8"中")
          == std::string("a") + kReplacementChar + u8"中");
}

// =====================================================================
// SWD-1～SWD-11：临时库
// =====================================================================

TEST_CASE("SWD-1 增删改查：列表最新在前、关键字筛选、分类筛选与分页")
{
    SensitiveWordManager& mgr = resetSwdDb();
    const int64_t idA = addOk(mgr, kEntA, u8"敏感", sensitive_category_illegal, "r1", kNow);
    const int64_t idB = addOk(mgr, kEntA, u8"违禁", sensitive_category_vulgar, "", kNow + 1);
    addOk(mgr, kEntA, u8"广告", sensitive_category_ad, "", kNow + 2);

    int total = 0;
    std::vector<SensitiveWordRecord> all = listAll(mgr, kEntA, total);
    REQUIRE(total == 3);
    REQUIRE(all.size() == 3);
    CHECK(all[0].m_word == std::string(u8"广告"));
    CHECK(all[2].m_id == idA);
    CHECK(all[2].m_word == std::string(u8"敏感"));
    CHECK(all[2].m_category == sensitive_category_illegal);
    CHECK(all[2].m_remark == "r1");
    CHECK(all[2].m_creatorId == kAdminId);
    CHECK(all[2].m_creatorName == kAdminName);
    CHECK(all[2].m_createTime == kNow);
    CHECK(all[2].m_enterpriseId == kEntA);

    //修改原文、分类与备注
    CHECK(mgr.updateWord(kEntA, idB, u8"违禁品", sensitive_category_abuse, "r2", kNow + 10)
          == sensitive_word_op_ok);
    all = listAll(mgr, kEntA, total);
    REQUIRE(all.size() == 3);
    CHECK(all[1].m_word == std::string(u8"违禁品"));
    CHECK(all[1].m_category == sensitive_category_abuse);
    CHECK(all[1].m_remark == "r2");
    CHECK(all[1].m_updateTime == kNow + 10);

    //删除
    int deleted = 0;
    REQUIRE(mgr.deleteWords(kEntA, std::vector<int64_t>(1, idA), deleted));
    CHECK(deleted == 1);
    all = listAll(mgr, kEntA, total);
    CHECK(total == 2);

    //关键字与分类筛选
    SensitiveWordQuery q;
    q.m_enterpriseId = kEntA;
    q.m_keyword = u8"违禁";
    std::vector<SensitiveWordRecord> out;
    REQUIRE(mgr.listWords(q, out, total));
    CHECK(total == 1);
    REQUIRE(out.size() == 1);
    CHECK(out[0].m_id == idB);

    q.m_keyword.clear();
    q.m_category = sensitive_category_ad;
    REQUIRE(mgr.listWords(q, out, total));
    CHECK(total == 1);
    REQUIRE(out.size() == 1);
    CHECK(out[0].m_word == std::string(u8"广告"));

    //分页：共 3 条，每页 2 条，第 2 页只剩最早的那一条
    addOk(mgr, kEntA, u8"第三条", sensitive_category_other, "", kNow + 3);
    q.m_category = 0;
    q.m_pageSize = 2;
    q.m_page = 2;
    REQUIRE(mgr.listWords(q, out, total));
    CHECK(total == 3);
    REQUIRE(out.size() == 1);
    CHECK(out[0].m_id == idB);

    int count = 0;
    REQUIRE(mgr.countWords(kEntA, count));
    CHECK(count == 3);
    testdb::cleanupDb(kSwdSqliteFile);
}

TEST_CASE("SWD-2 判重按归一化后的形式，且只在本企业内判")
{
    SensitiveWordManager& mgr = resetSwdDb();
    addOk(mgr, kEntA, "ABC", sensitive_category_other, "", kNow);
    CHECK(mgr.addWord(kEntA, "a b c", sensitive_category_other, "", kAdminId, kAdminName, kNow, NULL)
          == sensitive_word_op_exist);
    addOk(mgr, kEntB, "abc", sensitive_category_other, "", kNow);

    //把另一条改成与已有词条重复：拒绝；只改备注、原文不变：可以
    const int64_t idXyz = addOk(mgr, kEntA, "XYZ", sensitive_category_other, "", kNow + 1);
    CHECK(mgr.updateWord(kEntA, idXyz, "abc", sensitive_category_other, "", kNow + 2)
          == sensitive_word_op_exist);
    CHECK(mgr.updateWord(kEntA, idXyz, "XYZ", sensitive_category_other, "note", kNow + 2)
          == sensitive_word_op_ok);
    testdb::cleanupDb(kSwdSqliteFile);
}

TEST_CASE("SWD-3 每个企业最多 10000 条：添加与导入都受上限约束")
{
    SensitiveWordManager& mgr = resetSwdDb();

    //直接往库里写 9999 条（一个事务、每条 INSERT 带多行），比逐条调用 addWord 快得多
    {
        std::unique_ptr<IDatabase> pConn(DbFactory::createConnection());
        REQUIRE(pConn);
        REQUIRE(pConn->beginTransaction());
        int written = 0;
        while (written < kNearlyFull)
        {
            std::ostringstream os;
            os << "INSERT INTO t_sensitive_word(f_enterprise_id, f_word, f_word_norm, f_category, "
                  "f_create_time, f_update_time) VALUES";
            const int batchEnd = std::min(written + kRowsPerInsert, kNearlyFull);
            for (int i = written; i < batchEnd; ++i)
            {
                if (i > written)
                    os << ",";
                os << "(" << kEntA << ", 'w" << i << "', 'w" << i << "', "
                   << sensitive_category_other << ", " << kNow << ", " << kNow << ")";
            }
            REQUIRE(pConn->execute(os.str().c_str()));
            written = batchEnd;
        }
        REQUIRE(pConn->commit());
    }

    int count = 0;
    REQUIRE(mgr.countWords(kEntA, count));
    REQUIRE(count == kNearlyFull);

    addOk(mgr, kEntA, "extra1", sensitive_category_other, "", kNow);
    CHECK(mgr.addWord(kEntA, "extra2", sensitive_category_other, "", kAdminId, kAdminName, kNow, NULL)
          == sensitive_word_op_full);

    //腾出一个名额后导入两行：第一行加入，第二行因上限被跳过
    int deleted = 0;
    REQUIRE(mgr.deleteWords(kEntA, std::vector<int64_t>(1, 1), deleted));
    REQUIRE(deleted == 1);
    std::vector<std::string> lines;
    lines.push_back("imp1");
    lines.push_back("imp2");
    int added = 0;
    std::vector<SensitiveImportSkip> skipped;
    REQUIRE(mgr.importWords(kEntA, lines, sensitive_category_other, kAdminId, kAdminName, kNow,
                            added, skipped));
    CHECK(added == 1);
    REQUIRE(skipped.size() == 1);
    CHECK(skipped[0].m_line == 2);
    CHECK(skipped[0].m_word == "imp2");
    CHECK(skipped[0].m_reason == sensitive_word_op_full);
    testdb::cleanupDb(kSwdSqliteFile);
}

TEST_CASE("SWD-4 校验错误：为空、超长、词条不存在或不属于该企业")
{
    SensitiveWordManager& mgr = resetSwdDb();
    CHECK(mgr.addWord(kEntA, "!!!", sensitive_category_other, "", kAdminId, kAdminName, kNow, NULL)
          == sensitive_word_op_empty);
    CHECK(mgr.addWord(kEntA, Repeat(u8"字", SENSITIVE_WORD_MAX_CHARS + 1), sensitive_category_other, "",
                      kAdminId, kAdminName, kNow, NULL) == sensitive_word_op_too_long);

    const int64_t kMissingId = 999999;
    CHECK(mgr.updateWord(kEntA, kMissingId, "abc", sensitive_category_other, "", kNow)
          == sensitive_word_op_not_exist);

    //B 企业的词条：A 企业改不了、删不掉
    const int64_t idOfB = addOk(mgr, kEntB, u8"别家的词", sensitive_category_other, "", kNow);
    CHECK(mgr.updateWord(kEntA, idOfB, "abc", sensitive_category_other, "", kNow)
          == sensitive_word_op_not_exist);
    int deleted = -1;
    REQUIRE(mgr.deleteWords(kEntA, std::vector<int64_t>(1, idOfB), deleted));
    CHECK(deleted == 0);
    int total = 0;
    listAll(mgr, kEntB, total);
    CHECK(total == 1);
    testdb::cleanupDb(kSwdSqliteFile);
}

TEST_CASE("SWD-5 企业隔离与总开关")
{
    SensitiveWordManager& mgr = resetSwdDb();
    addOk(mgr, kEntA, u8"敏感", sensitive_category_other, "", kNow);

    CHECK(hits(mgr, kEntA, u8"含敏感词"));
    CHECK_FALSE(hits(mgr, kEntB, u8"含敏感词"));
    CHECK_FALSE(hits(mgr, kEntMissing, u8"含敏感词"));

    //新企业的总开关默认打开
    bool enabled = false;
    REQUIRE(mgr.getEnabled(kEntA, enabled));
    CHECK(enabled);

    REQUIRE(mgr.setEnabled(kEntA, false));
    REQUIRE(mgr.getEnabled(kEntA, enabled));
    CHECK_FALSE(enabled);
    CHECK_FALSE(hits(mgr, kEntA, u8"含敏感词"));

    REQUIRE(mgr.setEnabled(kEntA, true));
    CHECK(hits(mgr, kEntA, u8"含敏感词"));

    //企业不存在：读写开关都失败
    CHECK_FALSE(mgr.getEnabled(kEntMissing, enabled));
    CHECK_FALSE(mgr.setEnabled(kEntMissing, true));
    testdb::cleanupDb(kSwdSqliteFile);
}

TEST_CASE("SWD-6 任何一次写入之后，检查结果立即反映最新词库")
{
    SensitiveWordManager& mgr = resetSwdDb();
    //先检查一次，让匹配器缓存装载上（此时词库为空）
    CHECK_FALSE(hits(mgr, kEntA, u8"含敏感词"));

    const int64_t id = addOk(mgr, kEntA, u8"敏感", sensitive_category_other, "", kNow);
    CHECK(hits(mgr, kEntA, u8"含敏感词"));

    REQUIRE(mgr.updateWord(kEntA, id, u8"违禁", sensitive_category_other, "", kNow + 1)
            == sensitive_word_op_ok);
    CHECK_FALSE(hits(mgr, kEntA, u8"含敏感词"));
    CHECK(hits(mgr, kEntA, u8"违禁品"));

    int deleted = 0;
    REQUIRE(mgr.deleteWords(kEntA, std::vector<int64_t>(1, id), deleted));
    CHECK_FALSE(hits(mgr, kEntA, u8"违禁品"));

    int added = 0;
    std::vector<SensitiveImportSkip> skipped;
    REQUIRE(mgr.importWords(kEntA, std::vector<std::string>(1, u8"敏感"), sensitive_category_other,
                            kAdminId, kAdminName, kNow + 2, added, skipped));
    CHECK(added == 1);
    CHECK(hits(mgr, kEntA, u8"含敏感词"));
    testdb::cleanupDb(kSwdSqliteFile);
}

TEST_CASE("SWD-7 拦截记录：字段齐全、4 字节字符被替换、各项筛选与分页")
{
    SensitiveWordManager& mgr = resetSwdDb();
    SensitiveHitRecord first = makeHit(kEntA, 101, std::string(u8"张三") + kEmoji,
                                       sensitive_source_text, sensitive_category_illegal, kNow);
    first.m_snippet = std::string(u8"前文敏感后文") + kEmoji;
    REQUIRE(mgr.writeHit(first));
    REQUIRE(mgr.writeHit(makeHit(kEntA, 102, u8"李四", sensitive_source_profile,
                                 sensitive_category_vulgar, kNow + 1000)));
    REQUIRE(mgr.writeHit(makeHit(kEntB, 103, u8"王五", sensitive_source_text,
                                 sensitive_category_illegal, kNow + 2000)));

    SensitiveHitQuery q;
    q.m_enterpriseId = kEntA;
    std::vector<SensitiveHitRecord> out;
    int total = 0;
    REQUIRE(mgr.listHits(q, out, total));
    CHECK(total == 2);
    REQUIRE(out.size() == 2);
    //最新在前
    CHECK(out[0].m_userId == 102);
    const SensitiveHitRecord& r = out[1];
    CHECK(r.m_id > 0);
    CHECK(r.m_enterpriseId == kEntA);
    CHECK(r.m_userId == 101);
    CHECK(r.m_userName == std::string(u8"张三") + kReplacementChar);
    CHECK(r.m_source == sensitive_source_text);
    CHECK(r.m_ctype == 2);
    CHECK(r.m_cid == 4000000001LL);
    CHECK(r.m_targetName == std::string(u8"项目组"));
    CHECK(r.m_wordId == 5);
    CHECK(r.m_word == std::string(u8"敏感"));
    CHECK(r.m_category == sensitive_category_illegal);
    CHECK(r.m_snippet == std::string(u8"前文敏感后文") + kReplacementChar);
    CHECK(r.m_createTime == kNow);

    //按提交者名称
    q.m_keyword = u8"张";
    REQUIRE(mgr.listHits(q, out, total));
    CHECK(total == 1);
    REQUIRE(out.size() == 1);
    CHECK(out[0].m_userId == 101);

    //按来源
    q.m_keyword.clear();
    q.m_source = sensitive_source_profile;
    REQUIRE(mgr.listHits(q, out, total));
    CHECK(total == 1);

    //按分类
    q.m_source = 0;
    q.m_category = sensitive_category_illegal;
    REQUIRE(mgr.listHits(q, out, total));
    CHECK(total == 1);
    REQUIRE(out.size() == 1);
    CHECK(out[0].m_userId == 101);

    //按时间范围
    q.m_category = 0;
    q.m_beginTime = kNow + 500;
    q.m_endTime = kNow + 1500;
    REQUIRE(mgr.listHits(q, out, total));
    CHECK(total == 1);
    REQUIRE(out.size() == 1);
    CHECK(out[0].m_userId == 102);

    //分页：每页 1 条，第 2 页是较早的那条
    q.m_beginTime = 0;
    q.m_endTime = 0;
    q.m_pageSize = 1;
    q.m_page = 2;
    REQUIRE(mgr.listHits(q, out, total));
    CHECK(total == 2);
    REQUIRE(out.size() == 1);
    CHECK(out[0].m_userId == 101);
    testdb::cleanupDb(kSwdSqliteFile);
}

TEST_CASE("SWD-8 过期清理只删截止时间之前的拦截记录")
{
    SensitiveWordManager& mgr = resetSwdDb();
    const int64_t kRetentionMs = SENSITIVE_HIT_RETENTION_DAYS * kOneDayMs;
    REQUIRE(mgr.writeHit(makeHit(kEntA, 1, "old", sensitive_source_text, sensitive_category_other,
                                 kNow - 200 * kOneDayMs)));
    REQUIRE(mgr.writeHit(makeHit(kEntB, 2, "mid", sensitive_source_text, sensitive_category_other,
                                 kNow - 100 * kOneDayMs)));
    REQUIRE(mgr.writeHit(makeHit(kEntA, 3, "new", sensitive_source_text, sensitive_category_other, kNow)));

    int deleted = -1;
    REQUIRE(mgr.purgeHitsBefore(kNow - kRetentionMs, deleted));
    CHECK(deleted == 1);

    SensitiveHitQuery q;
    q.m_enterpriseId = kEntA;
    std::vector<SensitiveHitRecord> out;
    int total = 0;
    REQUIRE(mgr.listHits(q, out, total));
    CHECK(total == 1);
    q.m_enterpriseId = kEntB;
    REQUIRE(mgr.listHits(q, out, total));
    CHECK(total == 1);
    testdb::cleanupDb(kSwdSqliteFile);
}

TEST_CASE("SWD-9 批量导入：空行忽略，其余不合格的行逐行记下行号与原因")
{
    SensitiveWordManager& mgr = resetSwdDb();
    addOk(mgr, kEntA, u8"已存在", sensitive_category_other, "", kNow);

    std::vector<std::string> lines;
    lines.push_back("");                                         //1 空行，忽略
    lines.push_back("   ");                                      //2 空白行，忽略
    lines.push_back(u8"词一");                                   //3 加入
    lines.push_back(u8"词二\t违法违规");                         //4 加入，分类为违法违规
    lines.push_back(u8"词三\t不存在的分类\t备注三");             //5 加入，分类取默认
    lines.push_back("!!!");                                      //6 为空
    lines.push_back(Repeat(u8"字", SENSITIVE_WORD_MAX_CHARS + 1)); //7 超长
    lines.push_back(u8"词一");                                   //8 与第 3 行重复
    lines.push_back(u8"已存在");                                 //9 与库里已有的重复

    int added = 0;
    std::vector<SensitiveImportSkip> skipped;
    REQUIRE(mgr.importWords(kEntA, lines, sensitive_category_ad, kAdminId, kAdminName, kNow + 1,
                            added, skipped));
    CHECK(added == 3);
    REQUIRE(skipped.size() == 4);
    CHECK(skipped[0].m_line == 6);
    CHECK(skipped[0].m_reason == sensitive_word_op_empty);
    CHECK(skipped[1].m_line == 7);
    CHECK(skipped[1].m_reason == sensitive_word_op_too_long);
    CHECK(skipped[2].m_line == 8);
    CHECK(skipped[2].m_word == std::string(u8"词一"));
    CHECK(skipped[2].m_reason == sensitive_word_op_exist);
    CHECK(skipped[3].m_line == 9);
    CHECK(skipped[3].m_reason == sensitive_word_op_exist);

    SensitiveWordQuery q;
    q.m_enterpriseId = kEntA;
    q.m_keyword = u8"词二";
    std::vector<SensitiveWordRecord> out;
    int total = 0;
    REQUIRE(mgr.listWords(q, out, total));
    REQUIRE(out.size() == 1);
    CHECK(out[0].m_category == sensitive_category_illegal);

    q.m_keyword = u8"词三";
    REQUIRE(mgr.listWords(q, out, total));
    REQUIRE(out.size() == 1);
    CHECK(out[0].m_category == sensitive_category_ad);
    CHECK(out[0].m_remark == std::string(u8"备注三"));
    testdb::cleanupDb(kSwdSqliteFile);
}

TEST_CASE("SWD-10 导出的文字可以原样导入到另一个企业")
{
    SensitiveWordManager& mgr = resetSwdDb();
    addOk(mgr, kEntA, u8"敏感", sensitive_category_illegal, u8"备注一", kNow);
    addOk(mgr, kEntA, u8"违禁", sensitive_category_vulgar, "", kNow + 1);
    addOk(mgr, kEntA, u8"广告", sensitive_category_ad, "a\tb", kNow + 2);

    std::string text;
    REQUIRE(mgr.exportWords(kEntA, text));
    const std::vector<std::string> lines = SplitLines(text);
    REQUIRE(lines.size() == 3);

    int added = 0;
    std::vector<SensitiveImportSkip> skipped;
    REQUIRE(mgr.importWords(kEntB, lines, sensitive_category_other, kAdminId, kAdminName, kNow + 3,
                            added, skipped));
    CHECK(added == 3);
    CHECK(skipped.empty());

    int totalA = 0;
    int totalB = 0;
    std::vector<SensitiveWordRecord> a = listAll(mgr, kEntA, totalA);
    std::vector<SensitiveWordRecord> b = listAll(mgr, kEntB, totalB);
    REQUIRE(a.size() == b.size());
    for (size_t i = 0; i < a.size(); ++i)
    {
        INFO("row " << i);
        CHECK(a[i].m_word == b[i].m_word);
        CHECK(a[i].m_category == b[i].m_category);
    }
    //制表符在导出时变成了空格
    CHECK(b[0].m_remark == "a b");
    CHECK(b[2].m_remark == std::string(u8"备注一"));
    testdb::cleanupDb(kSwdSqliteFile);
}

TEST_CASE("SWD-11 多段文字检查：给出命中的段号、词条、分类与内容摘要")
{
    SensitiveWordManager& mgr = resetSwdDb();
    const int64_t id = addOk(mgr, kEntA, u8"敏感", sensitive_category_illegal, "", kNow);

    std::vector<std::string> texts;
    texts.push_back(u8"正常文字");
    texts.push_back("");
    texts.push_back(u8"前文敏感后文");
    SensitiveCheckResult r;
    REQUIRE(mgr.checkTexts(kEntA, texts, &r));
    CHECK(r.m_textIndex == 2);
    CHECK(r.m_wordId == id);
    CHECK(r.m_word == std::string(u8"敏感"));
    CHECK(r.m_category == sensitive_category_illegal);
    CHECK(r.m_snippet == std::string(u8"前文敏感后文"));

    texts.pop_back();
    CHECK_FALSE(mgr.checkTexts(kEntA, texts, &r));
    testdb::cleanupDb(kSwdSqliteFile);
}

TEST_CASE("SWD-12 删除企业时清理：只删该企业的词库与拦截记录，并丢弃其匹配器缓存")
{
    SensitiveWordManager& mgr = resetSwdDb();
    addOk(mgr, kEntA, u8"甲词一", sensitive_category_other, "", kNow);
    addOk(mgr, kEntA, u8"甲词二", sensitive_category_other, "", kNow);
    addOk(mgr, kEntB, u8"乙词一", sensitive_category_other, "", kNow);
    addOk(mgr, kEntB, u8"乙词二", sensitive_category_other, "", kNow);
    for (int i = 0; i < 2; ++i)
    {
        REQUIRE(mgr.writeHit(makeHit(kEntA, 1, "a", sensitive_source_text, sensitive_category_other, kNow)));
        REQUIRE(mgr.writeHit(makeHit(kEntB, 2, "b", sensitive_source_text, sensitive_category_other, kNow)));
    }
    //先检查一次，让 A 的匹配器进入缓存；删除之后若缓存没有丢弃，这里装载的词库还会继续命中
    REQUIRE(hits(mgr, kEntA, u8"含有甲词一的文字"));

    REQUIRE(mgr.deleteEnterpriseData(kEntA));

    int total = -1;
    listAll(mgr, kEntA, total);
    CHECK(total == 0);
    listAll(mgr, kEntB, total);
    CHECK(total == 2);

    SensitiveHitQuery q;
    std::vector<SensitiveHitRecord> out;
    q.m_enterpriseId = kEntA;
    REQUIRE(mgr.listHits(q, out, total));
    CHECK(total == 0);
    q.m_enterpriseId = kEntB;
    REQUIRE(mgr.listHits(q, out, total));
    CHECK(total == 2);

    CHECK_FALSE(hits(mgr, kEntA, u8"含有甲词一的文字"));
    CHECK(hits(mgr, kEntB, u8"含有乙词一的文字"));

    //没有任何数据的企业：同样返回成功
    CHECK(mgr.deleteEnterpriseData(kEntMissing));
    testdb::cleanupDb(kSwdSqliteFile);
}

TEST_CASE("SWD-13 拦截记录的保留截止时刻为当前时刻往前 180 天")
{
    CHECK(SensitiveWordManager::HitRetentionCutoff(kNow) == kNow - SENSITIVE_HIT_RETENTION_DAYS * kOneDayMs);
    //180 天是方案第 2.9 节与用户确认的保留期，常量被误改时这一条会失败
    CHECK(SensitiveWordManager::HitRetentionCutoff(kNow) == kNow - 180 * kOneDayMs);
}
