/**
 *  「备忘与提醒」服务端单元测试（Suite MEM-S），test_memo.cpp
 *
 *  对应 docs/备忘与提醒方案.md 第 5 节已与用户确认的服务端用例：
 *
 *    MEM-S1  个人备忘只对本人：只有本人能看、能改，拉取时只取得到本人的
 *    MEM-S2  群备忘只有群主、管理员能增删改，普通成员与非成员不能
 *    MEM-S3  群备忘的可见范围：群主、管理员都能看；普通成员只在「提醒全体」或被指定时能看
 *    MEM-S4  退群、被踢之后：从提醒对象里移除，拉取时拉不到该群的备忘，推送对象里也没有他
 *    MEM-S5  字段校验：开始时间格式与日期合法性、标题与备注长度、提前分钟、重复规则、次数、提醒方式、提醒对象
 *    MEM-S6  推送对象：修改后能看到的人收到新增或修改，修改前能看到、修改后看不到的人收到删除
 *    MEM-S7  敏感词：群备忘的标题与备注都送去检查，个人备忘不检查
 *
 *  存储相关的用例真的打开一个临时库跑 SQL（MemoManager 经 DbFactory 取连接）。
 *
 *  用法：Tests.exe --test-case="MEM-S*"
 *
 *  balloonwj@qq.com   2026-10-03
 */
#include "doctest.h"
#include "../chatserversrc/MemoProtocol.h"
#include "../chatserversrc/MemoManager.h"
#include "../chatserversrc/Protocol.h"   //group_role_xxx / conversation_xxx / memo_xxx / error_code_memo_xxx
#include "../chatserversrc/SensitiveWordDefs.h"
#include "../dbapi/IDatabase.h"
#include "../dbapi/DbFactory.h"
#include "../jsoncpp1.9.0/json.h"
#include "test_dbbackend.h"

#include <memory>
#include <sstream>
#include <string>
#include <vector>

namespace {

// 本套用例专用的临时库：SQLite 模式下的文件名、MySQL 模式下的库名（会被整个 DROP 重建）。
const char* const kMemSqliteFile = "flamingo_sqlite_memo_test.db";
const char* const kMemMysqlDb    = "flamingo_test_memo";

// 测试用户：群主、管理员、两名普通成员、一名不在群里的人。
const int64_t kOwner   = 3001;
const int64_t kAdmin   = 3002;
const int64_t kMemberB = 3003;
const int64_t kMemberC = 3004;
const int64_t kOutsider = 3005;

// 两个测试群。
const int64_t kGroupA = 268435601LL;
const int64_t kGroupB = 268435602LL;

// 一个合法的开始时间：2026-10-05 09:30。
const int64_t kStart = 202610050930LL;

// 基准时间戳（Unix 毫秒）。
const int64_t kMemTime = 1790000000000LL;

// 不是群成员时传给 isVisibleTo / canEdit 的角色。
const int kNotMember = -1;

// 执行一条 SQL，失败时中止用例。
void execSql(const std::string& sql)
{
    std::unique_ptr<IDatabase> pConn(DbFactory::createConnection());
    REQUIRE(pConn);
    REQUIRE(pConn->execute(sql.c_str()));
}

// 往 t_group_member 写一名成员。
void addMember(int64_t groupid, int64_t userid, int role)
{
    std::ostringstream os;
    os << "INSERT INTO t_group_member(f_groupid, f_userid, f_role, f_join_time) VALUES("
       << groupid << ", " << userid << ", " << role << ", " << kMemTime << ")";
    execSql(os.str());
}

// 从 t_group_member 删一名成员（模拟退群、被踢）。
void removeMember(int64_t groupid, int64_t userid)
{
    std::ostringstream os;
    os << "DELETE FROM t_group_member WHERE f_groupid = " << groupid << " AND f_userid = " << userid;
    execSql(os.str());
}

// 重建测试库并写入群 A 的四名成员与群 B 的一名成员（群主同时是群 B 的普通成员）。
void resetMemoDb()
{
    testdb::resetDb(kMemSqliteFile, kMemMysqlDb);
    addMember(kGroupA, kOwner, group_role_owner);
    addMember(kGroupA, kAdmin, group_role_admin);
    addMember(kGroupA, kMemberB, group_role_member);
    addMember(kGroupA, kMemberC, group_role_member);
    addMember(kGroupB, kOwner, group_role_member);
}

// 造一条合法的个人备忘。
MemoRecord personalMemo(int64_t userid, const std::string& title)
{
    MemoRecord m;
    m.ctype = conversation_single;
    m.owner = userid;
    m.creator = userid;
    m.editor = userid;
    m.title = title;
    m.start = kStart;
    m.style = memo_style_popup;
    m.createTime = kMemTime;
    m.updateTime = kMemTime;
    return m;
}

// 造一条合法的群备忘；targets 为空表示提醒全体成员。
MemoRecord groupMemo(int64_t groupid, const std::string& title, const std::vector<int64_t>& targets)
{
    MemoRecord m;
    m.ctype = conversation_group;
    m.owner = groupid;
    m.creator = kOwner;
    m.editor = kOwner;
    m.title = title;
    m.start = kStart;
    m.style = memo_style_popup;
    m.targetAll = targets.empty();
    m.targets = targets;
    m.createTime = kMemTime;
    m.updateTime = kMemTime;
    return m;
}

// 群 A 的成员与角色，与 resetMemoDb 写入的一致。
std::vector<MemoMemberRole> groupAMembers()
{
    std::vector<MemoMemberRole> v;
    v.push_back(MemoMemberRole(kOwner, group_role_owner));
    v.push_back(MemoMemberRole(kAdmin, group_role_admin));
    v.push_back(MemoMemberRole(kMemberB, group_role_member));
    v.push_back(MemoMemberRole(kMemberC, group_role_member));
    return v;
}

// 造 userid 列表。
std::vector<int64_t> Ids(int64_t a)
{
    return std::vector<int64_t>(1, a);
}

std::vector<int64_t> Ids(int64_t a, int64_t b)
{
    std::vector<int64_t> v;
    v.push_back(a);
    v.push_back(b);
    return v;
}

std::vector<int64_t> Ids(int64_t a, int64_t b, int64_t c)
{
    std::vector<int64_t> v = Ids(a, b);
    v.push_back(c);
    return v;
}

// 某用户经 listGroupMemosOfMember + isVisibleTo 最终拿到的群备忘标题，按返回顺序。
std::vector<std::string> visibleGroupTitles(MemoManager& mgr, int64_t userid, int64_t groupid)
{
    std::vector<MemoRecord> rows;
    std::vector<int> roles;
    REQUIRE(mgr.listGroupMemosOfMember(userid, groupid, rows, roles));
    REQUIRE(rows.size() == roles.size());
    std::vector<std::string> titles;
    for (size_t i = 0; i < rows.size(); ++i)
    {
        if (MemoProtocol::isVisibleTo(rows[i], userid, roles[i]))
            titles.push_back(rows[i].title);
    }
    return titles;
}

// 一个合法保存请求的 JSON 对象，用例在它的基础上改个别字段。
Json::Value baseSaveRequest()
{
    Json::Value v(Json::objectValue);
    v["memoid"] = "0";
    v["ctype"] = conversation_group;
    v["groupid"] = std::to_string(kGroupA);
    v["title"] = u8"周会";
    v["note"] = u8"带上周报";
    v["start"] = "2026-10-05 09:30";
    v["advance"] = 10;
    v["repeat"] = memo_repeat_weekly;
    v["weekdays"] = 1;
    v["count"] = 0;
    v["style"] = memo_style_popup;
    v["targetall"] = false;
    Json::Value targets(Json::arrayValue);
    targets.append(std::to_string(kMemberB));
    v["targets"] = targets;
    return v;
}

std::string toJson(const Json::Value& v)
{
    Json::StreamWriterBuilder wb;
    wb["indentation"] = "";
    return Json::writeString(wb, v);
}

// 解析并校验一个保存请求，返回 normalizeMemo 的错误码；结构解析失败时返回 error_code_bad_request。
int parseAndNormalize(const Json::Value& v, MemoRecord& out)
{
    if (!MemoProtocol::parseSaveRequest(toJson(v), out))
        return error_code_bad_request;
    return MemoProtocol::normalizeMemo(out);
}

} // namespace

TEST_CASE("MEM-S1 个人备忘只对本人")
{
    resetMemoDb();
    MemoManager mgr;

    MemoRecord mine = personalMemo(kMemberB, u8"交周报");
    MemoRecord others = personalMemo(kMemberC, u8"体检");
    REQUIRE(mgr.insertMemo(mine));
    REQUIRE(mgr.insertMemo(others));
    CHECK(mine.memoid > 0);
    CHECK(others.memoid != mine.memoid);

    //拉取：只取得到本人的那一条
    std::vector<MemoRecord> rows;
    REQUIRE(mgr.listPersonalMemos(kMemberB, rows));
    REQUIRE(rows.size() == 1);
    CHECK(rows[0].memoid == mine.memoid);
    CHECK(rows[0].title == u8"交周报");

    //可见与可改：本人可以，别人（哪怕是同群的群主）不行；个人备忘不看群角色
    CHECK(MemoProtocol::isVisibleTo(mine, kMemberB, kNotMember));
    CHECK_FALSE(MemoProtocol::isVisibleTo(mine, kMemberC, group_role_owner));
    CHECK(MemoProtocol::canEdit(mine, kMemberB, kNotMember));
    CHECK_FALSE(MemoProtocol::canEdit(mine, kOwner, group_role_owner));

    //推送对象只有本人
    std::vector<int64_t> viewers;
    MemoProtocol::collectViewers(mine, groupAMembers(), viewers);
    CHECK(viewers == Ids(kMemberB));

    //个人备忘不会出现在群备忘的拉取结果里
    CHECK(visibleGroupTitles(mgr, kMemberB, 0).empty());
}

TEST_CASE("MEM-S2 群备忘只有群主、管理员能增删改")
{
    MemoRecord memo = groupMemo(kGroupA, u8"周会", Ids(kMemberB));
    CHECK(MemoProtocol::canEdit(memo, kOwner, group_role_owner));
    CHECK(MemoProtocol::canEdit(memo, kAdmin, group_role_admin));
    CHECK_FALSE(MemoProtocol::canEdit(memo, kMemberB, group_role_member));
    CHECK_FALSE(MemoProtocol::canEdit(memo, kOutsider, kNotMember));
}

TEST_CASE("MEM-S3 群备忘的可见范围")
{
    resetMemoDb();
    MemoManager mgr;

    MemoRecord onlyB = groupMemo(kGroupA, u8"只提醒B", Ids(kMemberB));
    MemoRecord all = groupMemo(kGroupA, u8"提醒全体", std::vector<int64_t>());
    MemoRecord otherGroup = groupMemo(kGroupB, u8"群B的备忘", std::vector<int64_t>());
    REQUIRE(mgr.insertMemo(onlyB));
    REQUIRE(mgr.insertMemo(all));
    REQUIRE(mgr.insertMemo(otherGroup));

    //读回的提醒对象与写入的一致
    MemoRecord back;
    bool found = false;
    REQUIRE(mgr.getMemo(onlyB.memoid, back, found));
    REQUIRE(found);
    CHECK_FALSE(back.targetAll);
    CHECK(back.targets == Ids(kMemberB));

    //群主、管理员能看到群 A 的两条；B 两条都能看到；C 只看到提醒全体的那一条
    std::vector<std::string> both;
    both.push_back(u8"只提醒B");
    both.push_back(u8"提醒全体");
    CHECK(visibleGroupTitles(mgr, kAdmin, kGroupA) == both);
    CHECK(visibleGroupTitles(mgr, kMemberB, kGroupA) == both);
    CHECK(visibleGroupTitles(mgr, kMemberC, kGroupA) == std::vector<std::string>(1, u8"提醒全体"));

    //不带群 id 拉全部：群主在群 B 是普通成员，群 B 的备忘提醒全体，所以三条都在
    CHECK(visibleGroupTitles(mgr, kOwner, 0).size() == 3);

    //不在群里的人什么都拉不到
    CHECK(visibleGroupTitles(mgr, kOutsider, 0).empty());
}

TEST_CASE("MEM-S4 退群、被踢之后看不到该群的备忘")
{
    resetMemoDb();
    MemoManager mgr;

    MemoRecord memo = groupMemo(kGroupA, u8"周会", Ids(kMemberB, kMemberC));
    REQUIRE(mgr.insertMemo(memo));

    //B 退群：会话层先删成员行，再从提醒对象里移除
    removeMember(kGroupA, kMemberB);
    REQUIRE(mgr.removeTargetsInGroup(kGroupA, Ids(kMemberB)));

    MemoRecord back;
    bool found = false;
    REQUIRE(mgr.getMemo(memo.memoid, back, found));
    REQUIRE(found);
    CHECK(back.targets == Ids(kMemberC));
    CHECK(visibleGroupTitles(mgr, kMemberB, 0).empty());
    CHECK(visibleGroupTitles(mgr, kMemberC, kGroupA).size() == 1);

    //退群的人在退群前能看到、退群后看不到：会话层据此给他推删除
    CHECK(MemoProtocol::isVisibleTo(memo, kMemberB, group_role_member));
    CHECK_FALSE(MemoProtocol::isVisibleTo(back, kMemberB, kNotMember));

    //移除只影响本群：群 B 的提醒对象不受影响
    MemoRecord inB = groupMemo(kGroupB, u8"群B", Ids(kOwner));
    REQUIRE(mgr.insertMemo(inB));
    REQUIRE(mgr.removeTargetsInGroup(kGroupA, Ids(kOwner)));
    REQUIRE(mgr.getMemo(inB.memoid, back, found));
    CHECK(back.targets == Ids(kOwner));

    //解散群 A：该群的备忘与提醒对象全部删除，群 B 的保留
    REQUIRE(mgr.deleteGroupMemos(kGroupA));
    REQUIRE(mgr.getMemo(memo.memoid, back, found));
    CHECK_FALSE(found);
    std::vector<MemoRecord> rows;
    REQUIRE(mgr.listGroupMemos(kGroupB, rows));
    CHECK(rows.size() == 1);

    //删除单条：备忘与它的提醒对象一起删掉，再删一次也算成功
    REQUIRE(mgr.deleteMemo(inB.memoid));
    REQUIRE(mgr.getMemo(inB.memoid, back, found));
    CHECK_FALSE(found);
    CHECK(mgr.deleteMemo(inB.memoid));
}

TEST_CASE("MEM-S5 字段校验")
{
    MemoRecord out;

    SUBCASE("合法请求原样解析")
    {
        REQUIRE(parseAndNormalize(baseSaveRequest(), out) == error_code_ok);
        CHECK(out.memoid == 0);
        CHECK(out.ctype == conversation_group);
        CHECK(out.owner == kGroupA);
        CHECK(out.start == kStart);
        CHECK(out.advance == 10);
        CHECK(out.repeat == memo_repeat_weekly);
        CHECK(out.weekdays == 1);
        CHECK(out.count == 0);
        CHECK(out.targets == Ids(kMemberB));
        CHECK(MemoProtocol::formatStart(out.start) == "2026-10-05 09:30");
    }

    SUBCASE("开始时间的格式与日期")
    {
        int64_t v = 0;
        CHECK(MemoProtocol::parseStart("2028-02-29 23:59", v));
        CHECK(v == 202802292359LL);
        CHECK_FALSE(MemoProtocol::parseStart("2026-02-29 10:00", v));   //平年没有 2 月 29 日
        CHECK_FALSE(MemoProtocol::parseStart("2026-04-31 10:00", v));
        CHECK_FALSE(MemoProtocol::parseStart("2026-13-01 10:00", v));
        CHECK_FALSE(MemoProtocol::parseStart("2026-10-05 24:00", v));
        CHECK_FALSE(MemoProtocol::parseStart("2026-10-05 09:60", v));
        CHECK_FALSE(MemoProtocol::parseStart("2026-10-5 09:30", v));
        CHECK_FALSE(MemoProtocol::parseStart("2026/10/05 09:30", v));
        CHECK_FALSE(MemoProtocol::parseStart("1999-12-31 09:30", v));

        Json::Value req = baseSaveRequest();
        req["start"] = "2026-10-05";
        CHECK(parseAndNormalize(req, out) == error_code_bad_request);
    }

    SUBCASE("标题与备注")
    {
        Json::Value req = baseSaveRequest();
        req["title"] = u8"　 周会 ";
        req["note"] = u8"  缩进保留\n";
        REQUIRE(parseAndNormalize(req, out) == error_code_ok);
        CHECK(out.title == u8"周会");
        CHECK(out.note == u8"  缩进保留");

        req["title"] = "   ";
        CHECK(parseAndNormalize(req, out) == error_code_memo_invalid);

        req["title"] = u8"第一行\n第二行";
        CHECK(parseAndNormalize(req, out) == error_code_memo_invalid);

        std::string title50;
        for (size_t i = 0; i < kMemoTitleMaxChars; ++i)
            title50 += u8"字";
        req["title"] = title50;
        CHECK(parseAndNormalize(req, out) == error_code_ok);
        req["title"] = title50 + u8"字";
        CHECK(parseAndNormalize(req, out) == error_code_memo_invalid);

        std::string note200;
        for (size_t i = 0; i < kMemoNoteMaxChars; ++i)
            note200 += u8"注";
        req["title"] = u8"周会";
        req["note"] = note200;
        CHECK(parseAndNormalize(req, out) == error_code_ok);
        req["note"] = note200 + u8"注";
        CHECK(parseAndNormalize(req, out) == error_code_memo_invalid);

        req["note"] = 5;
        CHECK(parseAndNormalize(req, out) == error_code_bad_request);
    }

    SUBCASE("提前分钟、重复、次数、方式")
    {
        Json::Value req = baseSaveRequest();
        req["advance"] = kMemoAdvanceMaxMinutes;
        CHECK(parseAndNormalize(req, out) == error_code_ok);
        req["advance"] = kMemoAdvanceMaxMinutes + 1;
        CHECK(parseAndNormalize(req, out) == error_code_memo_invalid);
        req["advance"] = -1;
        CHECK(parseAndNormalize(req, out) == error_code_memo_invalid);

        req = baseSaveRequest();
        req["repeat"] = memo_repeat_yearly + 1;
        CHECK(parseAndNormalize(req, out) == error_code_memo_invalid);

        //每周重复至少选一天，且只能用到低 7 位
        req = baseSaveRequest();
        req["weekdays"] = 0;
        CHECK(parseAndNormalize(req, out) == error_code_memo_invalid);
        req["weekdays"] = kMemoWeekdayAllMask + 1;
        CHECK(parseAndNormalize(req, out) == error_code_memo_invalid);

        //非每周重复时星期掩码被清零；不重复时次数恒为 1
        req = baseSaveRequest();
        req["repeat"] = memo_repeat_none;
        req["count"] = 5;
        REQUIRE(parseAndNormalize(req, out) == error_code_ok);
        CHECK(out.weekdays == 0);
        CHECK(out.count == 1);

        req = baseSaveRequest();
        req["repeat"] = memo_repeat_daily;
        req["count"] = kMemoCountMax;
        CHECK(parseAndNormalize(req, out) == error_code_ok);
        req["count"] = kMemoCountMax + 1;
        CHECK(parseAndNormalize(req, out) == error_code_memo_invalid);

        req = baseSaveRequest();
        req["style"] = memo_style_fullscreen;
        CHECK(parseAndNormalize(req, out) == error_code_ok);
        req["style"] = 0;
        CHECK(parseAndNormalize(req, out) == error_code_memo_invalid);
    }

    SUBCASE("提醒对象")
    {
        //群备忘：不提醒全体又没有指定任何人
        Json::Value req = baseSaveRequest();
        req["targets"] = Json::Value(Json::arrayValue);
        CHECK(parseAndNormalize(req, out) == error_code_memo_target_invalid);

        //提醒全体时指定的名单被清空
        req["targetall"] = true;
        req["targets"].append(std::to_string(kMemberC));
        REQUIRE(parseAndNormalize(req, out) == error_code_ok);
        CHECK(out.targets.empty());

        //重复的人去重并升序
        req = baseSaveRequest();
        req["targets"] = Json::Value(Json::arrayValue);
        req["targets"].append(std::to_string(kMemberC));
        req["targets"].append(std::to_string(kMemberB));
        req["targets"].append(std::to_string(kMemberC));
        REQUIRE(parseAndNormalize(req, out) == error_code_ok);
        CHECK(out.targets == Ids(kMemberB, kMemberC));

        //提醒对象不是字符串
        req["targets"].append(42);
        CHECK(parseAndNormalize(req, out) == error_code_bad_request);

        //群备忘必须带群 id；个人备忘忽略提醒对象
        req = baseSaveRequest();
        req["groupid"] = "0";
        CHECK(parseAndNormalize(req, out) == error_code_bad_request);
        req["ctype"] = conversation_single;
        REQUIRE(parseAndNormalize(req, out) == error_code_ok);
        CHECK(out.owner == 0);
        CHECK(out.targets.empty());
        CHECK_FALSE(out.targetAll);

        req["ctype"] = conversation_service;
        CHECK(parseAndNormalize(req, out) == error_code_bad_request);
    }

    SUBCASE("删除与拉取请求")
    {
        int64_t id = 0;
        CHECK(MemoProtocol::parseDeleteRequest("{\"memoid\":\"12\"}", id));
        CHECK(id == 12);
        CHECK_FALSE(MemoProtocol::parseDeleteRequest("{\"memoid\":\"0\"}", id));
        CHECK_FALSE(MemoProtocol::parseDeleteRequest("{\"memoid\":12}", id));
        CHECK_FALSE(MemoProtocol::parseDeleteRequest("{}", id));

        int64_t groupid = -1;
        CHECK(MemoProtocol::parseListRequest("", groupid));
        CHECK(groupid == 0);
        CHECK(MemoProtocol::parseListRequest("{}", groupid));
        CHECK(groupid == 0);
        CHECK(MemoProtocol::parseListRequest("{\"groupid\":\"268435601\"}", groupid));
        CHECK(groupid == kGroupA);
        CHECK_FALSE(MemoProtocol::parseListRequest("{\"groupid\":268435601}", groupid));
    }
}

TEST_CASE("MEM-S6 推送对象")
{
    const std::vector<MemoMemberRole> members = groupAMembers();

    //新建只提醒 B：群主、管理员、B 收到
    MemoRecord before = groupMemo(kGroupA, u8"周会", Ids(kMemberB));
    std::vector<int64_t> beforeViewers;
    MemoProtocol::collectViewers(before, members, beforeViewers);
    CHECK(beforeViewers == Ids(kOwner, kAdmin, kMemberB));

    //改成只提醒 C：C 收到新增，B 收到删除
    MemoRecord after = before;
    after.targets = Ids(kMemberC);
    std::vector<int64_t> afterViewers;
    MemoProtocol::collectViewers(after, members, afterViewers);
    CHECK(afterViewers == Ids(kOwner, kAdmin, kMemberC));

    std::vector<int64_t> removed;
    MemoProtocol::collectRemovedViewers(beforeViewers, afterViewers, removed);
    CHECK(removed == Ids(kMemberB));

    //改成提醒全体：没有人收到删除
    MemoRecord toAll = after;
    toAll.targetAll = true;
    toAll.targets.clear();
    std::vector<int64_t> allViewers;
    MemoProtocol::collectViewers(toAll, members, allViewers);
    CHECK(allViewers.size() == members.size());
    MemoProtocol::collectRemovedViewers(afterViewers, allViewers, removed);
    CHECK(removed.empty());

    //管理员被降为普通成员：对只提醒 C 的备忘，他从此看不到
    CHECK(MemoProtocol::isVisibleTo(after, kAdmin, group_role_admin));
    CHECK_FALSE(MemoProtocol::isVisibleTo(after, kAdmin, group_role_member));

    //推送包体
    after.memoid = 12;
    Json::Value v;
    Json::CharReaderBuilder b;
    std::string errs;
    const std::string upsert = MemoProtocol::buildUpsertNotify(after);
    std::unique_ptr<Json::CharReader> reader(b.newCharReader());
    REQUIRE(reader->parse(upsert.c_str(), upsert.c_str() + upsert.size(), &v, &errs));
    CHECK(v["changetype"].asInt() == memo_change_upsert);
    CHECK(v["memo"]["memoid"].asString() == "12");
    CHECK(v["memo"]["groupid"].asString() == std::to_string(kGroupA));
    CHECK(v["memo"]["start"].asString() == "2026-10-05 09:30");
    CHECK(v["memo"]["targets"].size() == 1);
    CHECK(v["memo"]["targets"][0].asString() == std::to_string(kMemberC));

    const std::string del = MemoProtocol::buildDeleteNotify(12);
    REQUIRE(reader->parse(del.c_str(), del.c_str() + del.size(), &v, &errs));
    CHECK(v["changetype"].asInt() == memo_change_delete);
    CHECK(v["memoid"].asString() == "12");
    CHECK_FALSE(v.isMember("memo"));
}

TEST_CASE("MEM-S7 敏感词：群备忘查标题与备注，个人备忘不查")
{
    CHECK(chatserver::sensitive_source_memo == 14);

    std::vector<std::string> texts;
    MemoRecord g = groupMemo(kGroupA, u8"周会", Ids(kMemberB));
    g.note = u8"带上周报";
    MemoProtocol::collectSensitiveTexts(g, texts);
    REQUIRE(texts.size() == 2);
    CHECK(texts[0] == u8"周会");
    CHECK(texts[1] == u8"带上周报");

    g.note.clear();
    MemoProtocol::collectSensitiveTexts(g, texts);
    CHECK(texts.size() == 1);

    MemoRecord p = personalMemo(kMemberB, u8"周会");
    p.note = u8"带上周报";
    MemoProtocol::collectSensitiveTexts(p, texts);
    CHECK(texts.empty());
}

TEST_CASE("MEM-S8 存储：修改覆盖各列并整体替换提醒对象，计数按所属")
{
    resetMemoDb();
    MemoManager mgr;

    MemoRecord memo = groupMemo(kGroupA, u8"周会", Ids(kMemberB, kMemberC));
    memo.note = u8"带上 'quote' 周报";
    REQUIRE(mgr.insertMemo(memo));

    MemoRecord changed = memo;
    changed.editor = kAdmin;
    changed.title = u8"周会（改）";
    changed.note = std::string();
    changed.start = 202610061000LL;
    changed.advance = 30;
    changed.repeat = memo_repeat_monthly;
    changed.count = 3;
    changed.style = memo_style_fullscreen;
    changed.targets = Ids(kAdmin);
    changed.updateTime = kMemTime + 1;
    REQUIRE(mgr.updateMemo(changed));

    MemoRecord back;
    bool found = false;
    REQUIRE(mgr.getMemo(memo.memoid, back, found));
    REQUIRE(found);
    CHECK(back.creator == kOwner);
    CHECK(back.editor == kAdmin);
    CHECK(back.title == u8"周会（改）");
    CHECK(back.note.empty());
    CHECK(back.start == 202610061000LL);
    CHECK(back.advance == 30);
    CHECK(back.repeat == memo_repeat_monthly);
    CHECK(back.count == 3);
    CHECK(back.style == memo_style_fullscreen);
    CHECK(back.targets == Ids(kAdmin));
    CHECK(back.createTime == kMemTime);
    CHECK(back.updateTime == kMemTime + 1);

    //改成提醒全体：提醒对象表里不再有这条的行
    changed.targetAll = true;
    changed.targets.clear();
    REQUIRE(mgr.updateMemo(changed));
    REQUIRE(mgr.getMemo(memo.memoid, back, found));
    CHECK(back.targetAll);
    CHECK(back.targets.empty());

    MemoRecord p1 = personalMemo(kMemberB, u8"一");
    MemoRecord p2 = personalMemo(kMemberB, u8"二");
    REQUIRE(mgr.insertMemo(p1));
    REQUIRE(mgr.insertMemo(p2));
    int count = -1;
    REQUIRE(mgr.countMemos(conversation_single, kMemberB, count));
    CHECK(count == 2);
    REQUIRE(mgr.countMemos(conversation_group, kGroupA, count));
    CHECK(count == 1);
    REQUIRE(mgr.countMemos(conversation_single, kMemberC, count));
    CHECK(count == 0);
}
