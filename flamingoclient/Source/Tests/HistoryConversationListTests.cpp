/**
 *  聊天记录管理器——左栏会话列表纯逻辑的单元测试（Suite HCL），HistoryConversationListTests.cpp
 *
 *  覆盖 UI/BalloonHistory/HistoryConversationList.h：
 *    · MergeManagerConversations：服务端会话列表与本地库汇总取并集、去重时取较新的一份、
 *      已离开的群的残余数据、本地摘要的生成、排序；
 *    · FilterManagerConversations：类型、名字（含拼音）、搜索范围三种筛选；
 *    · ResolveManagerSelection：列表重建后的选中项；
 *    · 范围按钮与命中说明的文字、范围去重。
 *  框架 Catch2 v3。方案见 docs/聊天记录管理器方案.md §4、§5。
 *  balloonwj@qq.com   2026-10-03
 */
#include "catch_amalgamated.hpp"
#include "../UI/BalloonHistory/HistoryConversationList.h"
#include <set>
#include <string>
#include <vector>

using namespace balloonhistory;

namespace {

//会话类型（与 net::conversation_type 取值一致）
const int kSingle = 1;
const int kGroup  = 2;

//消息类型（与 net/Protocol.h 的 message_type 取值一致）
const int kTypeText  = 1;
const int kTypeImage = 2;

//构造一条服务端会话
ManagerServerConversation MakeServer(int ctype, int64_t cid, int64_t msgid, int64_t ts,
                                     const std::wstring& digest)
{
    ManagerServerConversation s;
    s.m_ctype = ctype;
    s.m_cid = cid;
    s.m_lastMsgid = msgid;
    s.m_lastTimestamp = ts;
    s.m_digest = digest;
    return s;
}

//构造一条本地会话汇总
//  text：最后一条的正文；msgtype 为文本时写进 content 的 text 字段
LocalConversationBrief MakeLocal(int ctype, int64_t cid, int64_t msgid, int64_t ts, int msgtype,
                                 const std::string& text)
{
    LocalConversationBrief l;
    l.m_ctype = ctype;
    l.m_cid = cid;
    l.m_lastMsgid = msgid;
    l.m_lastTimestamp = ts;
    l.m_lastFromid = cid;
    l.m_lastMsgtype = msgtype;
    l.m_lastContent = "{\"text\":\"" + text + "\"}";
    return l;
}

//构造一个已填好名字的列表项
ManagerConversationItem MakeItem(int ctype, int64_t cid, const std::wstring& name)
{
    ManagerConversationItem item;
    item.m_ctype = ctype;
    item.m_cid = cid;
    item.m_name = name;
    return item;
}

} // namespace

TEST_CASE("HCL1 两份各自独有的会话都列出，标明是否在服务端列表里，按时间倒序", "[historymgr]")
{
    std::vector<ManagerServerConversation> server;
    server.push_back(MakeServer(kSingle, 10, 100, 1000, L"你好"));
    server.push_back(MakeServer(kGroup, 600001, 300, 3000, L"[图片]"));
    std::vector<LocalConversationBrief> local;
    local.push_back(MakeLocal(kSingle, 20, 200, 2000, kTypeText, "本地才有"));

    std::set<int64_t> myGroups;
    myGroups.insert(600001);
    const std::vector<ManagerConversationItem> items =
        MergeManagerConversations(server, local, myGroups, true);
    REQUIRE(items.size() == 3);
    CHECK(items[0].m_cid == 600001);
    CHECK(items[0].m_inServerList);
    CHECK(items[0].m_digest == L"[图片]");
    CHECK(items[1].m_cid == 20);
    CHECK_FALSE(items[1].m_inServerList);
    CHECK(items[1].m_digest == L"本地才有");
    CHECK(items[2].m_cid == 10);
    CHECK(items[2].m_inServerList);
    //名字由窗口另行填入
    CHECK(items[0].m_name.empty());
}

TEST_CASE("HCL2 两份都有同一会话时取较新的一份；完全相同时取服务端那份", "[historymgr]")
{
    std::set<int64_t> myGroups;

    SECTION("本地较新：时间、msgid、摘要都取本地，仍标为在服务端列表里")
    {
        std::vector<ManagerServerConversation> server;
        server.push_back(MakeServer(kSingle, 10, 100, 1000, L"旧的"));
        std::vector<LocalConversationBrief> local;
        local.push_back(MakeLocal(kSingle, 10, 105, 1500, kTypeImage, ""));
        const std::vector<ManagerConversationItem> items =
            MergeManagerConversations(server, local, myGroups, true);
        REQUIRE(items.size() == 1);
        CHECK(items[0].m_lastMsgid == 105);
        CHECK(items[0].m_lastTimestamp == 1500);
        CHECK(items[0].m_digest == L"[图片]");
        CHECK(items[0].m_inServerList);
    }

    SECTION("服务端较新：保留服务端那份")
    {
        std::vector<ManagerServerConversation> server;
        server.push_back(MakeServer(kSingle, 10, 110, 2000, L"服务端的"));
        std::vector<LocalConversationBrief> local;
        local.push_back(MakeLocal(kSingle, 10, 105, 1500, kTypeText, "本地的"));
        const std::vector<ManagerConversationItem> items =
            MergeManagerConversations(server, local, myGroups, true);
        REQUIRE(items.size() == 1);
        CHECK(items[0].m_lastMsgid == 110);
        CHECK(items[0].m_digest == L"服务端的");
    }

    SECTION("时间相同：msgid 大的那份胜出")
    {
        std::vector<ManagerServerConversation> server;
        server.push_back(MakeServer(kSingle, 10, 110, 2000, L"服务端的"));
        std::vector<LocalConversationBrief> local;
        local.push_back(MakeLocal(kSingle, 10, 111, 2000, kTypeText, "本地的"));
        const std::vector<ManagerConversationItem> items =
            MergeManagerConversations(server, local, myGroups, true);
        REQUIRE(items.size() == 1);
        CHECK(items[0].m_lastMsgid == 111);
        CHECK(items[0].m_digest == L"本地的");
    }

    SECTION("时间与 msgid 都相同：取服务端的摘要（它处理过撤回）")
    {
        std::vector<ManagerServerConversation> server;
        server.push_back(MakeServer(kSingle, 10, 110, 2000, L"你撤回了一条消息"));
        std::vector<LocalConversationBrief> local;
        local.push_back(MakeLocal(kSingle, 10, 110, 2000, kTypeText, "原文"));
        const std::vector<ManagerConversationItem> items =
            MergeManagerConversations(server, local, myGroups, true);
        REQUIRE(items.size() == 1);
        CHECK(items[0].m_digest == L"你撤回了一条消息");
    }
}

TEST_CASE("HCL3 只在本地有的群：群列表已加载且本人不在群里时不列出", "[historymgr]")
{
    std::vector<ManagerServerConversation> server;
    //服务端列表里的群不受 myGroupIds 限制
    server.push_back(MakeServer(kGroup, 700001, 500, 5000, L"服务端的群"));
    std::vector<LocalConversationBrief> local;
    local.push_back(MakeLocal(kGroup, 600001, 100, 1000, kTypeText, "还在的群"));
    local.push_back(MakeLocal(kGroup, 600002, 200, 2000, kTypeText, "离开的群"));
    local.push_back(MakeLocal(kSingle, 600002, 300, 3000, kTypeText, "同号的单聊"));

    std::set<int64_t> myGroups;
    myGroups.insert(600001);

    SECTION("群列表已加载：离开的群不列出，同号单聊照常列出")
    {
        const std::vector<ManagerConversationItem> items =
            MergeManagerConversations(server, local, myGroups, true);
        CHECK(items.size() == 3);
        CHECK(FindManagerConversation(items, kGroup, 700001) >= 0);
        CHECK(FindManagerConversation(items, kGroup, 600001) >= 0);
        CHECK(FindManagerConversation(items, kGroup, 600002) < 0);
        CHECK(FindManagerConversation(items, kSingle, 600002) >= 0);
    }

    SECTION("群列表还没加载：全部照常列出")
    {
        const std::vector<ManagerConversationItem> items =
            MergeManagerConversations(server, local, myGroups, false);
        CHECK(items.size() == 4);
        CHECK(FindManagerConversation(items, kGroup, 600002) >= 0);
    }
}

TEST_CASE("HCL4 排序：时间倒序，同毫秒按 msgid 倒序，再按会话类型、cid 升序", "[historymgr]")
{
    std::vector<ManagerConversationItem> items;
    ManagerConversationItem a = MakeItem(kGroup, 5, L"a");
    a.m_lastTimestamp = 1000;
    a.m_lastMsgid = 10;
    ManagerConversationItem b = MakeItem(kSingle, 9, L"b");
    b.m_lastTimestamp = 1000;
    b.m_lastMsgid = 10;
    ManagerConversationItem c = MakeItem(kSingle, 3, L"c");
    c.m_lastTimestamp = 1000;
    c.m_lastMsgid = 10;
    ManagerConversationItem d = MakeItem(kSingle, 1, L"d");
    d.m_lastTimestamp = 1000;
    d.m_lastMsgid = 12;
    ManagerConversationItem e = MakeItem(kSingle, 2, L"e");
    e.m_lastTimestamp = 2000;
    e.m_lastMsgid = 1;
    items.push_back(a);
    items.push_back(b);
    items.push_back(c);
    items.push_back(d);
    items.push_back(e);

    SortManagerConversations(items);
    REQUIRE(items.size() == 5);
    CHECK(items[0].m_name == L"e");
    CHECK(items[1].m_name == L"d");
    CHECK(items[2].m_name == L"c");
    CHECK(items[3].m_name == L"b");
    CHECK(items[4].m_name == L"a");
}

TEST_CASE("HCL5 筛选：类型、名字（汉字、拼音首字母、全拼、大小写）、搜索范围", "[historymgr]")
{
    std::vector<ManagerConversationItem> items;
    items.push_back(MakeItem(kSingle, 10, L"苏沐辰"));
    items.push_back(MakeItem(kGroup, 600001, L"产品研发群"));
    items.push_back(MakeItem(kSingle, 11, L"Alice"));
    items.push_back(MakeItem(kGroup, 600002, L"苏州出差小组"));
    const std::vector<HistoryConversationKey> noScope;

    SECTION("类型")
    {
        CHECK(FilterManagerConversations(items, kManagerTypeAll, L"", noScope).size() == 4);
        const std::vector<int> singles = FilterManagerConversations(items, kManagerTypeSingle, L"", noScope);
        REQUIRE(singles.size() == 2);
        CHECK(singles[0] == 0);
        CHECK(singles[1] == 2);
        const std::vector<int> groups = FilterManagerConversations(items, kManagerTypeGroup, L"", noScope);
        REQUIRE(groups.size() == 2);
        CHECK(groups[0] == 1);
        CHECK(groups[1] == 3);
    }

    SECTION("名字")
    {
        //汉字子串
        const std::vector<int> su = FilterManagerConversations(items, kManagerTypeAll, L"苏", noScope);
        REQUIRE(su.size() == 2);
        CHECK(su[0] == 0);
        CHECK(su[1] == 3);
        //拼音首字母
        const std::vector<int> smc = FilterManagerConversations(items, kManagerTypeAll, L"smc", noScope);
        REQUIRE(smc.size() == 1);
        CHECK(smc[0] == 0);
        //全拼
        const std::vector<int> chanpin = FilterManagerConversations(items, kManagerTypeAll, L"chanpin", noScope);
        REQUIRE(chanpin.size() == 1);
        CHECK(chanpin[0] == 1);
        //ASCII 大小写不敏感
        const std::vector<int> alice = FilterManagerConversations(items, kManagerTypeAll, L"ALI", noScope);
        REQUIRE(alice.size() == 1);
        CHECK(alice[0] == 2);
        //名字与类型一起生效
        CHECK(FilterManagerConversations(items, kManagerTypeGroup, L"苏", noScope).size() == 1);
        //都不匹配
        CHECK(FilterManagerConversations(items, kManagerTypeAll, L"不存在", noScope).empty());
    }

    SECTION("搜索范围：只保留范围里的会话，范围里重复的项不影响结果")
    {
        std::vector<HistoryConversationKey> scope;
        scope.push_back(HistoryConversationKey(kGroup, 600002));
        scope.push_back(HistoryConversationKey(kSingle, 10));
        scope.push_back(HistoryConversationKey(kSingle, 10));
        //同号不同类型的会话不算在范围里
        scope.push_back(HistoryConversationKey(kSingle, 600001));
        const std::vector<int> rows = FilterManagerConversations(items, kManagerTypeAll, L"", scope);
        REQUIRE(rows.size() == 2);
        CHECK(rows[0] == 0);
        CHECK(rows[1] == 3);
        //范围与名字一起生效
        const std::vector<int> su = FilterManagerConversations(items, kManagerTypeAll, L"苏州", scope);
        REQUIRE(su.size() == 1);
        CHECK(su[0] == 3);
    }
}

TEST_CASE("HCL6 列表重建后的选中项", "[historymgr]")
{
    std::vector<ManagerConversationItem> items;
    items.push_back(MakeItem(kSingle, 10, L"a"));
    items.push_back(MakeItem(kGroup, 600001, L"b"));
    items.push_back(MakeItem(kSingle, 11, L"c"));

    //原来选中的还在：取它的新下标
    CHECK(ResolveManagerSelection(items, kGroup, 600001, 0) == 1);
    //原来选中的不在了：取原位置上的那一行
    CHECK(ResolveManagerSelection(items, kGroup, 999, 1) == 1);
    //原位置已超出末尾：取最后一行
    CHECK(ResolveManagerSelection(items, kGroup, 999, 5) == 2);
    //重建前没有选中项：取第一行
    CHECK(ResolveManagerSelection(items, kSingle, 0, -1) == 0);
    //列表为空
    const std::vector<ManagerConversationItem> empty;
    CHECK(ResolveManagerSelection(empty, kSingle, 10, 0) == -1);
}

TEST_CASE("HCL7 范围按钮、命中说明的文字，范围去重排序，类型筛选的兜底", "[historymgr]")
{
    CHECK(BuildManagerScopeButtonText(0) == L"范围：全部会话");
    CHECK(BuildManagerScopeButtonText(3) == L"范围：已选 3 个会话");

    CHECK(BuildManagerHitText(1, L"明天十点开会") == L"明天十点开会");
    CHECK(BuildManagerHitText(1, L"") == L"1 条相关聊天记录");
    CHECK(BuildManagerHitText(12, L"明天十点开会") == L"12 条相关聊天记录");

    std::vector<HistoryConversationKey> scope;
    scope.push_back(HistoryConversationKey(kGroup, 5));
    scope.push_back(HistoryConversationKey(kSingle, 9));
    scope.push_back(HistoryConversationKey(kGroup, 5));
    scope.push_back(HistoryConversationKey(kSingle, 3));
    const std::vector<HistoryConversationKey> norm = NormalizeManagerScope(scope);
    REQUIRE(norm.size() == 3);
    CHECK(norm[0].m_ctype == kSingle);
    CHECK(norm[0].m_cid == 3);
    CHECK(norm[1].m_cid == 9);
    CHECK(norm[2].m_ctype == kGroup);

    //不认识的类型筛选取值按「全部」处理
    const int kUnknownFilter = 7;
    CHECK(MatchesManagerTypeFilter(kGroup, kUnknownFilter));
    CHECK(MatchesManagerTypeFilter(kSingle, kManagerTypeAll));
    CHECK_FALSE(MatchesManagerTypeFilter(kSingle, kManagerTypeGroup));
}

TEST_CASE("HCL8 范围弹层里勾选与取消：集合保持去重、升序，底部文字随个数变化", "[historymgr]")
{
    std::vector<HistoryConversationKey> scope;
    CHECK(BuildManagerScopeSelectedText(scope.size()) == L"未选择");

    //勾选三个会话，次序打乱
    CHECK(ToggleManagerScopeKey(scope, HistoryConversationKey(kGroup, 600001)));
    CHECK(ToggleManagerScopeKey(scope, HistoryConversationKey(kSingle, 20)));
    CHECK(ToggleManagerScopeKey(scope, HistoryConversationKey(kSingle, 10)));
    REQUIRE(scope.size() == 3);
    CHECK(scope[0].m_cid == 10);
    CHECK(scope[1].m_cid == 20);
    CHECK(scope[2].m_ctype == kGroup);
    CHECK(BuildManagerScopeSelectedText(scope.size()) == L"已选 3 个");
    CHECK(IsInManagerScope(scope, HistoryConversationKey(kSingle, 20)));
    //同号不同类型的会话不在范围里
    CHECK_FALSE(IsInManagerScope(scope, HistoryConversationKey(kSingle, 600001)));

    //再点一次即取消
    CHECK_FALSE(ToggleManagerScopeKey(scope, HistoryConversationKey(kSingle, 20)));
    REQUIRE(scope.size() == 2);
    CHECK_FALSE(IsInManagerScope(scope, HistoryConversationKey(kSingle, 20)));
    CHECK(scope[0].m_cid == 10);
    CHECK(scope[1].m_cid == 600001);
}
