/**
 *  联系人标签存储管理器单元测试（Suite CTM），test_contacttagmanager.cpp
 *
 *  覆盖 ContactTagManager 的几条关键行为：
 *    - 归一规则本身（去首尾空白、丢空串、忽略大小写去重且保留首次出现者的大小写）；
 *    - 整份覆盖：写入多条后读出顺序一致，重复提交幂等，提交更短的列表会删掉多出来的行，
 *      提交空数组则删光；
 *    - 校验：单条超长整份拒绝且一行都不写入，归一后条数超上限拒绝，
 *      而"提交 11 条其中两条重复、归一后剩 10 条"必须放行（验证先归一再判条数的顺序）；
 *    - 标签含单引号 / 反斜杠 / 中文时能正确转义，既不破坏 SQL 也不改动内容；
 *    - 单向语义：两个不同用户给同一个联系人贴标签互不干扰。
 *
 *  这些用例会真的打开一个临时 .db 文件跑 SQL —— ContactTagManager 经 DbFactory
 *  取连接，而 DbFactory 读的是全局 CDbConfig，故每个用例先把 CDbConfig 指到本套
 *  专用的库文件上。SQLite 是嵌入式的，不需要任何外部服务，放进默认测试集没有负担。
 *
 *  balloonwj@qq.com   2026-08-13
 */
#include <stdio.h>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "doctest.h"
#include "../chatserversrc/ContactTagManager.h"
#include "../chatserversrc/Protocol.h"       //error_code
#include "../dbapi/DbConfig.h"
#include "../dbapi/IDatabase.h"
#include "../dbapi/DbFactory.h"
#include "../sqliteapi/SqliteManager.h"
#include "../base/Singleton.h"

//本套用例专用的临时数据库文件，与 CRM / SQD / SQM 几套用的文件分开，避免互相干扰
static const char* const TAG_DB_PATH = "flamingo_sqlite_tag_test.db";

//本套用例统一使用的时间戳（Unix 毫秒）。标签的落库时间不参与任何断言，
//取一个固定值即可，避免用例之间因取当前时间而结果不可复现。
static const int64_t TAG_TEST_TIME = 1700000000000LL;

//两个参与测试的用户与两个被贴标签的联系人。写成具名常量而不是散在各处的裸数字，
//读用例时才一眼看得出哪个是"贴标签的人"、哪个是"被贴的人"。
static const int64_t TAG_TEST_USER_A   = 100001;
static const int64_t TAG_TEST_USER_B   = 100004;
static const int64_t TAG_TEST_TARGET_1 = 100002;
static const int64_t TAG_TEST_TARGET_2 = 100003;

//删除测试库文件，连同 WAL 模式产生的两个附属文件一起清掉
static void removeTagDbFiles()
{
    std::string basePath(TAG_DB_PATH);
    remove(basePath.c_str());
    remove((basePath + "-wal").c_str());
    remove((basePath + "-shm").c_str());
}

//把测试库重建成一个干净的完整结构库，并把全局 CDbConfig 指到它上面。
//每个用例开头都调一次，保证从确定的初始状态开始 —— ContactTagManager 是单例，
//用例之间共享，唯一能隔离的就是底下这个库文件。
static void resetTagDb()
{
    removeTagDbFiles();

    //CSqliteManager::init 会把空文件建成完整结构（建表 + 补列 + 建索引与触发器）
    REQUIRE(Singleton<CSqliteManager>::Instance().init(TAG_DB_PATH));

    //DbFactory 按 CDbConfig 造连接；MySQL 那四个参数在 SQLite 模式下不参与连接
    Singleton<CDbConfig>::Instance().init(DB_TYPE_NAME_SQLITE, NULL, NULL, NULL,
                                          NULL, TAG_DB_PATH);
}

//数一数 t_contact_tag 里现在有几行，用于"整份覆盖真的删掉了多余的行"这类断言。
//数据库不可用时返回 -1，调用方须与 0 区分开。
static int64_t countTagRows()
{
    std::unique_ptr<IDatabase> pConn(DbFactory::createConnection());
    if (!pConn)
        return -1;

    QueryResult* pResult = pConn->query("SELECT COUNT(*) FROM t_contact_tag");
    if (pResult == NULL)
        return -1;

    int64_t count = -1;
    Field* pRow = pResult->fetch();
    if (pRow != NULL)
    {
        count = static_cast<int64_t>(pRow[0].getUInt64());
    }
    pResult->endQuery();
    delete pResult;
    return count;
}

//造一条由 count 个"标"字组成的标签。一个汉字在 utf8 下占 3 字节，
//故字节数恒为 count * 3，用来构造刚好压线与刚好超线的长度。
static std::string makeChineseTag(int count)
{
    std::string tag;
    for (int i = 0; i < count; ++i)
    {
        tag += "标";
    }
    return tag;
}

// ---------------------------------------------------------------------------
// CTM1  归一规则（纯函数，不碰数据库）
// ---------------------------------------------------------------------------

TEST_CASE("CTM1 归一：去首尾空白、丢空串、忽略大小写去重并保留首次出现者的大小写")
{
    std::vector<std::string> in;
    in.push_back("  设计  ");     //首尾空格要去掉
    in.push_back("\t外包\n");     //制表符与换行同样算空白
    in.push_back("   ");          //整条都是空白，去掉后为空串，应被丢弃
    in.push_back("");             //空串直接丢弃
    in.push_back("UI");
    in.push_back("ui");           //与上一条只差大小写，视为重复
    in.push_back("设计");         //与第一条去空白后相同，视为重复

    std::vector<std::string> out;
    ContactTagManager::normalizeTags(in, out);

    REQUIRE(out.size() == 3);
    CHECK(out[0] == "设计");
    CHECK(out[1] == "外包");
    //保留的是首次出现者的原始大小写：用户先写的是 UI，就存 UI 而不是 ui
    CHECK(out[2] == "UI");
}

TEST_CASE("CTM1b 归一：出参进函数先清空，空输入得到空结果")
{
    std::vector<std::string> out;
    out.push_back("上一次调用留下的脏数据");

    std::vector<std::string> in;
    ContactTagManager::normalizeTags(in, out);
    CHECK(out.empty());
}

// ---------------------------------------------------------------------------
// CTM2  写入与读回
// ---------------------------------------------------------------------------

TEST_CASE("CTM2 写入多条标签后读出的顺序与提交顺序一致；重复提交同一份是幂等的")
{
    resetTagDb();
    ContactTagManager& mgr = Singleton<ContactTagManager>::Instance();

    std::vector<std::string> tags;
    tags.push_back("设计");
    tags.push_back("外包");
    tags.push_back("北京");

    int errorCode = -1;
    REQUIRE(mgr.setTags(TAG_TEST_USER_A, TAG_TEST_TARGET_1, tags, TAG_TEST_TIME, errorCode));
    CHECK(errorCode == error_code_ok);
    CHECK(countTagRows() == 3);

    std::map<int32_t, std::vector<std::string> > all;
    REQUIRE(mgr.getTagsAll(TAG_TEST_USER_A, all));
    REQUIRE(all.size() == 1);
    REQUIRE(all[static_cast<int32_t>(TAG_TEST_TARGET_1)].size() == 3);
    //f_order 取的是提交时的下标，读回来必须还是这个顺序，而不是按标签文本排序
    CHECK(all[static_cast<int32_t>(TAG_TEST_TARGET_1)][0] == "设计");
    CHECK(all[static_cast<int32_t>(TAG_TEST_TARGET_1)][1] == "外包");
    CHECK(all[static_cast<int32_t>(TAG_TEST_TARGET_1)][2] == "北京");

    //把同一份列表再提交一遍：结果完全一样，行数也不变（不会插出重复行）
    REQUIRE(mgr.setTags(TAG_TEST_USER_A, TAG_TEST_TARGET_1, tags, TAG_TEST_TIME + 1, errorCode));
    CHECK(countTagRows() == 3);

    all.clear();
    REQUIRE(mgr.getTagsAll(TAG_TEST_USER_A, all));
    REQUIRE(all[static_cast<int32_t>(TAG_TEST_TARGET_1)].size() == 3);
    CHECK(all[static_cast<int32_t>(TAG_TEST_TARGET_1)][0] == "设计");

    //一个标签都没贴过的人：查询成功，出参为空
    std::map<int32_t, std::vector<std::string> > none;
    REQUIRE(mgr.getTagsAll(TAG_TEST_USER_B, none));
    CHECK(none.empty());
}

// ---------------------------------------------------------------------------
// CTM3  整份覆盖
// ---------------------------------------------------------------------------

TEST_CASE("CTM3 提交更短的列表会删掉多出来的行；提交空数组则删光")
{
    resetTagDb();
    ContactTagManager& mgr = Singleton<ContactTagManager>::Instance();

    std::vector<std::string> three;
    three.push_back("设计");
    three.push_back("外包");
    three.push_back("北京");

    int errorCode = -1;
    REQUIRE(mgr.setTags(TAG_TEST_USER_A, TAG_TEST_TARGET_1, three, TAG_TEST_TIME, errorCode));
    CHECK(countTagRows() == 3);

    //整份覆盖：只提交一条，另外两条必须消失，而不是留在库里
    std::vector<std::string> one;
    one.push_back("外包");
    REQUIRE(mgr.setTags(TAG_TEST_USER_A, TAG_TEST_TARGET_1, one, TAG_TEST_TIME + 1, errorCode));
    CHECK(countTagRows() == 1);

    std::map<int32_t, std::vector<std::string> > all;
    REQUIRE(mgr.getTagsAll(TAG_TEST_USER_A, all));
    REQUIRE(all[static_cast<int32_t>(TAG_TEST_TARGET_1)].size() == 1);
    CHECK(all[static_cast<int32_t>(TAG_TEST_TARGET_1)][0] == "外包");

    //空数组 = 清空。要的是"行没了"，而不是"行还在、值是空串"
    std::vector<std::string> empty;
    REQUIRE(mgr.setTags(TAG_TEST_USER_A, TAG_TEST_TARGET_1, empty, TAG_TEST_TIME + 2, errorCode));
    CHECK(errorCode == error_code_ok);
    CHECK(countTagRows() == 0);

    all.clear();
    REQUIRE(mgr.getTagsAll(TAG_TEST_USER_A, all));
    CHECK(all.empty());

    //再清一次、以及清一个压根没贴过标签的联系人：都算成功，客户端连点两下不该报错
    CHECK(mgr.setTags(TAG_TEST_USER_A, TAG_TEST_TARGET_1, empty, TAG_TEST_TIME + 3, errorCode));
    CHECK(mgr.setTags(TAG_TEST_USER_A, TAG_TEST_TARGET_2, empty, TAG_TEST_TIME + 3, errorCode));
}

// ---------------------------------------------------------------------------
// CTM4  单条长度校验
// ---------------------------------------------------------------------------

TEST_CASE("CTM4 有一条标签超长就整份拒绝，且一行都不写入")
{
    resetTagDb();
    ContactTagManager& mgr = Singleton<ContactTagManager>::Instance();

    //先贴一份正常的，用来验证被拒的那次提交没有把它删掉
    std::vector<std::string> original;
    original.push_back("设计");
    int errorCode = -1;
    REQUIRE(mgr.setTags(TAG_TEST_USER_A, TAG_TEST_TARGET_1, original, TAG_TEST_TIME, errorCode));
    CHECK(countTagRows() == 1);

    //16 个汉字恰好 48 字节，压在上限上，应当放行
    const std::string justFit = makeChineseTag(kMaxContactTagChars);
    CHECK(static_cast<int>(justFit.size()) == kMaxContactTagBytes);

    //17 个汉字 51 字节，超出上限
    const std::string tooLong = makeChineseTag(kMaxContactTagChars + 1);
    CHECK(static_cast<int>(tooLong.size()) > kMaxContactTagBytes);

    std::vector<std::string> bad;
    bad.push_back("外包");     //这一条是合格的
    bad.push_back(tooLong);    //坏在第二条上

    CHECK_FALSE(mgr.setTags(TAG_TEST_USER_A, TAG_TEST_TARGET_1, bad, TAG_TEST_TIME + 1, errorCode));
    CHECK(errorCode == error_code_bad_request);

    //关键断言：整份拒绝意味着"外包"也没写进去，原有的"设计"也没被删掉。
    //若实现改成"先删后插、插到坏的那条才失败"，这里会看到 0 行或 1 行"外包"。
    CHECK(countTagRows() == 1);
    std::map<int32_t, std::vector<std::string> > all;
    REQUIRE(mgr.getTagsAll(TAG_TEST_USER_A, all));
    REQUIRE(all[static_cast<int32_t>(TAG_TEST_TARGET_1)].size() == 1);
    CHECK(all[static_cast<int32_t>(TAG_TEST_TARGET_1)][0] == "设计");

    //压线的那条能正常写入
    std::vector<std::string> fit;
    fit.push_back(justFit);
    REQUIRE(mgr.setTags(TAG_TEST_USER_A, TAG_TEST_TARGET_1, fit, TAG_TEST_TIME + 2, errorCode));
    all.clear();
    REQUIRE(mgr.getTagsAll(TAG_TEST_USER_A, all));
    REQUIRE(all[static_cast<int32_t>(TAG_TEST_TARGET_1)].size() == 1);
    CHECK(all[static_cast<int32_t>(TAG_TEST_TARGET_1)][0] == justFit);
}

// ---------------------------------------------------------------------------
// CTM5  条数上限与"先归一再判条数"的顺序
// ---------------------------------------------------------------------------

TEST_CASE("CTM5 归一后条数超上限被拒；提交11条但其中两条重复、归一后剩10条应放行")
{
    resetTagDb();
    ContactTagManager& mgr = Singleton<ContactTagManager>::Instance();

    //先造 kMaxContactTagCount + 1 条互不相同的标签：归一去不掉任何一条，应被拒
    std::vector<std::string> overLimit;
    for (int i = 0; i < kMaxContactTagCount + 1; ++i)
    {
        overLimit.push_back(makeChineseTag(i + 1));
    }

    int errorCode = -1;
    CHECK_FALSE(mgr.setTags(TAG_TEST_USER_A, TAG_TEST_TARGET_1, overLimit,
                            TAG_TEST_TIME, errorCode));
    CHECK(errorCode == error_code_tag_limit);
    //被拒时一行都不写
    CHECK(countTagRows() == 0);

    //同样是 kMaxContactTagCount + 1 条，但其中一条是前面某条的大小写变体、
    //另一条只是多了首尾空格：归一后正好剩 kMaxContactTagCount 条，必须放行。
    //这一条用例就是"先归一、再判条数"这个顺序的守门人 —— 顺序反了它会失败。
    std::vector<std::string> dedupable;
    dedupable.push_back("UI");
    for (int i = 1; i < kMaxContactTagCount; ++i)
    {
        dedupable.push_back(makeChineseTag(i));
    }
    dedupable.push_back("ui");                          //与首条只差大小写
    dedupable.push_back("  " + makeChineseTag(1) + " ");  //与第二条只差首尾空格
    REQUIRE(static_cast<int>(dedupable.size()) == kMaxContactTagCount + 2);

    REQUIRE(mgr.setTags(TAG_TEST_USER_A, TAG_TEST_TARGET_1, dedupable,
                        TAG_TEST_TIME + 1, errorCode));
    CHECK(errorCode == error_code_ok);
    CHECK(countTagRows() == kMaxContactTagCount);

    std::map<int32_t, std::vector<std::string> > all;
    REQUIRE(mgr.getTagsAll(TAG_TEST_USER_A, all));
    REQUIRE(static_cast<int>(all[static_cast<int32_t>(TAG_TEST_TARGET_1)].size())
            == kMaxContactTagCount);
    //去重保留的是首次出现者的原始大小写
    CHECK(all[static_cast<int32_t>(TAG_TEST_TARGET_1)][0] == "UI");
}

// ---------------------------------------------------------------------------
// CTM6  转义
// ---------------------------------------------------------------------------

TEST_CASE("CTM6 标签含单引号 / 反斜杠 / 中文时正确转义：SQL 不被破坏，内容也不被改动")
{
    resetTagDb();
    ContactTagManager& mgr = Singleton<ContactTagManager>::Instance();

    //单引号是最典型的一个：不转义的话字面量会在这里提前闭合，轻则语句报错、
    //重则被拼出一条别的语句。反斜杠在 SQLite 下是普通字符、在 MySQL 下是转义引导符，
    //两种后端规则不同，都要能原样存取。
    std::vector<std::string> nasty;
    nasty.push_back("O'Brien");
    nasty.push_back("a''b\\c'; DROP TABLE t_contact_tag; --");
    nasty.push_back("设计 · 中文");

    int errorCode = -1;
    REQUIRE(mgr.setTags(TAG_TEST_USER_A, TAG_TEST_TARGET_1, nasty, TAG_TEST_TIME, errorCode));

    std::map<int32_t, std::vector<std::string> > all;
    REQUIRE(mgr.getTagsAll(TAG_TEST_USER_A, all));
    REQUIRE(all[static_cast<int32_t>(TAG_TEST_TARGET_1)].size() == 3);
    //读回来必须与写进去的一模一样：不能多出转义符，也不能少掉半截
    CHECK(all[static_cast<int32_t>(TAG_TEST_TARGET_1)][0] == "O'Brien");
    CHECK(all[static_cast<int32_t>(TAG_TEST_TARGET_1)][1] == "a''b\\c'; DROP TABLE t_contact_tag; --");
    CHECK(all[static_cast<int32_t>(TAG_TEST_TARGET_1)][2] == "设计 · 中文");

    //注入串真被当成数据存下来了，表还在、行还在
    CHECK(countTagRows() == 3);
}

// ---------------------------------------------------------------------------
// CTM7  参数校验
// ---------------------------------------------------------------------------

TEST_CASE("CTM7 userid / targetUserId 非正或两者相等一律拒绝")
{
    resetTagDb();
    ContactTagManager& mgr = Singleton<ContactTagManager>::Instance();

    std::vector<std::string> tags;
    tags.push_back("设计");

    int errorCode = -1;
    CHECK_FALSE(mgr.setTags(0, TAG_TEST_TARGET_1, tags, TAG_TEST_TIME, errorCode));
    CHECK(errorCode == error_code_bad_request);
    CHECK_FALSE(mgr.setTags(TAG_TEST_USER_A, 0, tags, TAG_TEST_TIME, errorCode));
    CHECK(errorCode == error_code_bad_request);
    CHECK_FALSE(mgr.setTags(-1, TAG_TEST_TARGET_1, tags, TAG_TEST_TIME, errorCode));
    CHECK(errorCode == error_code_bad_request);
    //给自己贴标签没有意义，通讯录里也不显示本人
    CHECK_FALSE(mgr.setTags(TAG_TEST_USER_A, TAG_TEST_USER_A, tags, TAG_TEST_TIME, errorCode));
    CHECK(errorCode == error_code_bad_request);

    //一次都没写进去
    CHECK(countTagRows() == 0);

    //读取侧同样拒绝非法 userid
    std::map<int32_t, std::vector<std::string> > all;
    CHECK_FALSE(mgr.getTagsAll(0, all));
    CHECK(all.empty());
}

// ---------------------------------------------------------------------------
// CTM8  单向语义
// ---------------------------------------------------------------------------

TEST_CASE("CTM8 两个不同用户给同一个联系人贴的标签互不干扰")
{
    resetTagDb();
    ContactTagManager& mgr = Singleton<ContactTagManager>::Instance();

    int errorCode = -1;

    //A 给 TARGET_1 与 TARGET_2 各贴一份
    std::vector<std::string> aForTarget1;
    aForTarget1.push_back("设计");
    aForTarget1.push_back("外包");
    REQUIRE(mgr.setTags(TAG_TEST_USER_A, TAG_TEST_TARGET_1, aForTarget1, TAG_TEST_TIME, errorCode));

    std::vector<std::string> aForTarget2;
    aForTarget2.push_back("测试");
    REQUIRE(mgr.setTags(TAG_TEST_USER_A, TAG_TEST_TARGET_2, aForTarget2, TAG_TEST_TIME, errorCode));

    //B 给同一个 TARGET_1 贴一份完全不同的 —— 标签是单向的，
    //同一个人在不同人的通讯录里的标签可以完全不同
    std::vector<std::string> bForTarget1;
    bForTarget1.push_back("客户方接口人");
    REQUIRE(mgr.setTags(TAG_TEST_USER_B, TAG_TEST_TARGET_1, bForTarget1, TAG_TEST_TIME, errorCode));

    std::map<int32_t, std::vector<std::string> > mine;
    REQUIRE(mgr.getTagsAll(TAG_TEST_USER_A, mine));
    REQUIRE(mine.size() == 2);
    REQUIRE(mine[static_cast<int32_t>(TAG_TEST_TARGET_1)].size() == 2);
    CHECK(mine[static_cast<int32_t>(TAG_TEST_TARGET_1)][0] == "设计");
    REQUIRE(mine[static_cast<int32_t>(TAG_TEST_TARGET_2)].size() == 1);
    CHECK(mine[static_cast<int32_t>(TAG_TEST_TARGET_2)][0] == "测试");

    std::map<int32_t, std::vector<std::string> > others;
    REQUIRE(mgr.getTagsAll(TAG_TEST_USER_B, others));
    REQUIRE(others.size() == 1);
    REQUIRE(others[static_cast<int32_t>(TAG_TEST_TARGET_1)].size() == 1);
    CHECK(others[static_cast<int32_t>(TAG_TEST_TARGET_1)][0] == "客户方接口人");

    //A 清空对 TARGET_1 的标签，不该动到 B 那一份，也不该动到 A 对 TARGET_2 的那一份
    std::vector<std::string> empty;
    REQUIRE(mgr.setTags(TAG_TEST_USER_A, TAG_TEST_TARGET_1, empty, TAG_TEST_TIME + 1, errorCode));

    mine.clear();
    REQUIRE(mgr.getTagsAll(TAG_TEST_USER_A, mine));
    REQUIRE(mine.size() == 1);
    CHECK(mine[static_cast<int32_t>(TAG_TEST_TARGET_2)][0] == "测试");

    others.clear();
    REQUIRE(mgr.getTagsAll(TAG_TEST_USER_B, others));
    REQUIRE(others.size() == 1);
    CHECK(others[static_cast<int32_t>(TAG_TEST_TARGET_1)][0] == "客户方接口人");
}

// ---------------------------------------------------------------------------
// CTM9  账号停用时的清理
// ---------------------------------------------------------------------------

TEST_CASE("CTM9 removeTagsForAll 删掉所有人贴给某个联系人的标签，不碰别的联系人")
{
    resetTagDb();
    ContactTagManager& mgr = Singleton<ContactTagManager>::Instance();

    int errorCode = -1;
    std::vector<std::string> tags;
    tags.push_back("设计");

    REQUIRE(mgr.setTags(TAG_TEST_USER_A, TAG_TEST_TARGET_1, tags, TAG_TEST_TIME, errorCode));
    REQUIRE(mgr.setTags(TAG_TEST_USER_B, TAG_TEST_TARGET_1, tags, TAG_TEST_TIME, errorCode));
    REQUIRE(mgr.setTags(TAG_TEST_USER_A, TAG_TEST_TARGET_2, tags, TAG_TEST_TIME, errorCode));
    CHECK(countTagRows() == 3);

    //TARGET_1 离职：A 与 B 贴给他的都要清掉，贴给 TARGET_2 的那条不受影响
    REQUIRE(mgr.removeTagsForAll(TAG_TEST_TARGET_1));
    CHECK(countTagRows() == 1);

    std::map<int32_t, std::vector<std::string> > mine;
    REQUIRE(mgr.getTagsAll(TAG_TEST_USER_A, mine));
    REQUIRE(mine.size() == 1);
    CHECK(mine.find(static_cast<int32_t>(TAG_TEST_TARGET_2)) != mine.end());

    //一行都没有时再删一次也算成功；非法 target 则拒绝
    CHECK(mgr.removeTagsForAll(TAG_TEST_TARGET_1));
    CHECK_FALSE(mgr.removeTagsForAll(0));
}
