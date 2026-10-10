/**
 *  机器人模块对象 CBotModule 的单元测试（标签 [botdll]，无窗口模式，用假的宿主、假的窗口工厂与假的应答接收方）：
 *    - BOTD15 收发：命令经宿主发出并按 seq 登记，应答交回发起方；没有登记、命令号对不上的应答丢弃；
 *             撤销接收方后它的应答丢弃；号段以外的命令不发；宿主不放行时返回 0；
 *    - BOTD16 群机器人列表：宿主的定长结构体换成 Core 的列表；超过预备容量时按宿主报告的个数重取；宿主没有结果时返回 false；
 *    - BOTD17 选人：请求号登记到发起方名下，确认后交回；作废的请求号忽略；宿主弹不出时返回 0；
 *    - BOTD18 窗口：打开、再次打开复用、按群通知列表变化（0 表示全部群）、企业开关变化、断线重连、宿主关闭与用户关闭后释放；
 *             总开关关闭时全部关闭并且不再能打开；退出登录关闭全部窗口；
 *    - BOTD19 初始化参数不对时失败；版本号与号段判断；
 *    - BOTD21 选部门（docs/机器人方案.md §14.8）：请求号登记到发起方名下，确认后交回；作废的请求号忽略；宿主弹不出时返回 0；
 *             部门名称取宿主的，取不到时为「部门<id>」。
 *  框架 Catch2 v3。
 *  balloonwj@qq.com   2026-10-08
 */
#include "catch_amalgamated.hpp"

#include <string.h>
#include <wchar.h>
#include <map>
#include <string>
#include <vector>

#include "../BotApi.h"
#include "../Core/BotCodec.h"
#include "../Host/BotModule.h"

using namespace flamingobot;

namespace {

//假的宿主：记录发出的命令与选人请求，按设定返回群机器人列表
class FakeHost : public IBotHost
{
public:
    //发出的一条命令
    struct Sent
    {
        uint16_t    m_cmd;   //命令号
        uint32_t    m_seq;   //分配的 seq
        std::string m_body;  //包体
    };

    FakeHost()
        : m_nextSeq(100)
        , m_refuseSend(false)
        , m_hasList(true)
        , m_botCount(2)
        , m_refusePick(false)
        , m_lastPickId(0)
        , m_lastPickCount(0)
        , m_lastDeptPickId(0)
        , m_lastDeptPickCount(0)
        , m_listCalls(0)
    {
    }

    virtual void Log(int level, const char* text) override
    {
        (void)level;
        (void)text;
    }

    virtual const wchar_t* Translate(const wchar_t* context, const wchar_t* source) override
    {
        (void)context;
        return source;
    }

    virtual bool GetUiLanguage(wchar_t* langBuf, uint32_t langChars, wchar_t* fontBuf, uint32_t fontChars) override
    {
        (void)langBuf;
        (void)langChars;
        (void)fontBuf;
        (void)fontChars;
        return false;
    }

    virtual uint32_t SendBotCommand(uint16_t cmd, const char* json, uint32_t len) override
    {
        if (m_refuseSend)
            return 0;
        Sent s;
        s.m_cmd = cmd;
        s.m_seq = ++m_nextSeq;
        s.m_body.assign(json, len);
        m_sent.push_back(s);
        return s.m_seq;
    }

    virtual int64_t GetMyUserId() override
    {
        return 5308;
    }

    virtual bool GetMyDisplayName(wchar_t* buf, uint32_t chars) override
    {
        wcsncpy_s(buf, chars, L"我", _TRUNCATE);
        return true;
    }

    virtual bool GetUserDisplayName(int64_t userId, wchar_t* buf, uint32_t chars) override
    {
        if (userId == 5302)
        {
            wcsncpy_s(buf, chars, L"李四", _TRUNCATE);
            return true;
        }
        return false;
    }

    virtual bool GetDepartmentName(int64_t deptId, wchar_t* buf, uint32_t chars) override
    {
        if (deptId == 30)
        {
            wcsncpy_s(buf, chars, L"研发中心", _TRUNCATE);
            return true;
        }
        return false;
    }

    virtual bool PickDepartments(HWND owner, uint32_t requestId, const int64_t* preselected, uint32_t count) override
    {
        (void)owner;
        (void)preselected;
        if (m_refusePick)
            return false;
        m_lastDeptPickId = requestId;
        m_lastDeptPickCount = count;
        return true;
    }

    virtual bool GetGroupBotList(int64_t groupId, BotGroupCapsInfo* caps, BotGroupItemInfo* items, uint32_t capacity,
                                 uint32_t* total) override
    {
        ++m_listCalls;
        if (!m_hasList || caps == NULL || total == NULL)
            return false;
        REQUIRE(caps->m_cbSize == sizeof(BotGroupCapsInfo));
        caps->m_isOwner = true;
        caps->m_groupBotAllowed = true;
        caps->m_externalAllowed = false;
        caps->m_groupBotMax = 5;
        *total = m_botCount;
        for (uint32_t i = 0; items != NULL && i < capacity && i < m_botCount; ++i)
        {
            REQUIRE(items[i].m_cbSize == sizeof(BotGroupItemInfo));
            items[i].m_botId = 1000 + i;
            items[i].m_userId = 5400 + i;
            items[i].m_ownerType = (i == 0) ? bot_owner_enterprise : bot_owner_group;
            items[i].m_isGroupBot = (i != 0);
            wcsncpy_s(items[i].m_name, kBotNameChars, (i == 0) ? L"IT 小助手" : L"值班提醒", _TRUNCATE);
            wcsncpy_s(items[i].m_color, kBotColorChars, L"#1E88E5", _TRUNCATE);
            items[i].m_off = (i == 0);
            items[i].m_canOff = true;
            items[i].m_active = (i != 0);
            (void)groupId;
        }
        return true;
    }

    virtual void RequestGroupBots(int64_t groupId) override
    {
        m_refreshed.push_back(groupId);
    }

    virtual bool PickContacts(HWND owner, uint32_t requestId, const int64_t* preselected, uint32_t count) override
    {
        (void)owner;
        (void)preselected;
        if (m_refusePick)
            return false;
        m_lastPickId = requestId;
        m_lastPickCount = count;
        return true;
    }

    uint32_t             m_nextSeq;        //上一个分配的 seq
    bool                 m_refuseSend;     //为 true 时不放行任何命令
    bool                 m_hasList;        //是否有群机器人列表
    uint32_t             m_botCount;       //群机器人列表的个数
    bool                 m_refusePick;     //为 true 时选人对话框弹不出
    uint32_t             m_lastPickId;     //最近一次选人请求号
    uint32_t             m_lastPickCount;  //最近一次预先勾选的人数
    uint32_t             m_lastDeptPickId; //最近一次选部门请求号
    uint32_t             m_lastDeptPickCount; //最近一次预先勾选的部门数
    int                  m_listCalls;      //GetGroupBotList 被调用的次数
    std::vector<Sent>    m_sent;           //发出的命令
    std::vector<int64_t> m_refreshed;      //请求重拉 2801 的群
};

//假的应答接收方：记录收到的应答与选人结果
class FakeSink : public IBotReplySink
{
public:
    virtual void OnBotReply(const BotReply& reply) override
    {
        m_replies.push_back(reply);
    }

    virtual void OnContactsPicked(uint32_t requestId, const std::vector<int64_t>& userIds) override
    {
        m_pickIds.push_back(requestId);
        m_picked = userIds;
    }

    virtual void OnDepartmentsPicked(uint32_t requestId, const std::vector<int64_t>& deptIds) override
    {
        m_deptPickIds.push_back(requestId);
        m_depts = deptIds;
    }

    std::vector<uint32_t> m_deptPickIds;   //收到的选部门请求号
    std::vector<int64_t>  m_depts;         //最近一次选好的部门
    std::vector<BotReply> m_replies;   //收到的应答
    std::vector<uint32_t> m_pickIds;   //收到的选人请求号
    std::vector<int64_t>  m_picked;    //最近一次选好的人
};

//假的顶层窗口：记录收到的通知
class FakeWindow : public IBotTopWindow
{
public:
    explicit FakeWindow(int* liveCount)
        : m_opens(0)
        , m_closed(false)
        , m_listChanges(0)
        , m_featureChanges(0)
        , m_reconnects(0)
        , m_liveCount(liveCount)
    {
        ++(*m_liveCount);
    }

    virtual ~FakeWindow()
    {
        --(*m_liveCount);
    }

    virtual bool Open(HWND owner) override
    {
        (void)owner;
        ++m_opens;
        return true;
    }

    virtual void Close() override
    {
        m_closed = true;
    }

    virtual void SetGroupName(const std::wstring& groupName) override
    {
        m_groupName = groupName;
    }

    virtual void OnGroupBotsChanged() override
    {
        ++m_listChanges;
    }

    virtual void OnFeaturesChanged() override
    {
        ++m_featureChanges;
    }

    virtual void OnReconnected() override
    {
        ++m_reconnects;
    }

    int          m_opens;           //Open 的次数
    bool         m_closed;          //是否被宿主关闭过
    int          m_listChanges;     //OnGroupBotsChanged 的次数
    int          m_featureChanges;  //OnFeaturesChanged 的次数
    int          m_reconnects;      //OnReconnected 的次数
    std::wstring m_groupName;       //最近一次设置的群名
    int*         m_liveCount;       //还没释放的窗口个数（测试持有）
};

//假的窗口工厂
class FakeFactory : public IBotWindowFactory
{
public:
    FakeFactory()
        : m_live(0)
        , m_myBots(NULL)
    {
    }

    virtual bool InitUi(IBotHost* host) override
    {
        (void)host;
        return true;
    }

    virtual void ShutdownUi() override
    {
    }

    virtual IBotTopWindow* CreateGroupManager(int64_t groupId, const std::wstring& groupName,
                                              IBotWindowOwner* owner) override
    {
        (void)owner;
        FakeWindow* w = new FakeWindow(&m_live);
        w->m_groupName = groupName;
        m_managers[groupId] = w;
        return w;
    }

    virtual IBotTopWindow* CreateMyBots(IBotWindowOwner* owner) override
    {
        (void)owner;
        m_myBots = new FakeWindow(&m_live);
        return m_myBots;
    }

    int                            m_live;      //还没释放的窗口个数
    std::map<int64_t, FakeWindow*> m_managers;  //最近一次为各群创建的管理窗（借用）
    FakeWindow*                    m_myBots;    //最近一次创建的「我的机器人」窗口（借用）
};

//按测试设定初始化一个无窗口模式的模块
void InitModule(CBotModule& module, FakeHost& host, FakeFactory* factory)
{
    module.SetHeadlessForTest(true);
    module.SetWindowFactory(factory);
    BotInitParams params;
    params.m_cbSize = sizeof(params);
    params.m_appDir = L".";
    REQUIRE(module.Init(&host, &params));
    module.OnLoggedIn();
}

} // namespace

TEST_CASE("BOTD15 命令按 seq 登记，应答交回发起方", "[botdll]")
{
    FakeHost host;
    CBotModule module;
    InitModule(module, host, NULL);
    FakeSink a;
    FakeSink b;
    const uint32_t ca = module.RegisterClient(&a);
    const uint32_t cb = module.RegisterClient(&b);
    CHECK(ca != cb);

    const uint32_t seqA = module.SendCommand(ca, bot_cmd_get_detail, BotCodec::BuildGetDetailRequest(12));
    const uint32_t seqB = module.SendCommand(cb, bot_cmd_get_my_bots, BotCodec::BuildGetMyBotsRequest());
    REQUIRE(seqA != 0);
    REQUIRE(seqB != 0);
    REQUIRE(host.m_sent.size() == 2);
    CHECK(host.m_sent[0].m_cmd == bot_cmd_get_detail);
    CHECK(module.PendingRequestCount() == 2);

    //应答交回发起方
    module.ProcessFrame(bot_cmd_get_my_bots, seqB, "{\"code\":0,\"allowed\":1,\"max\":3,\"bots\":[]}");
    REQUIRE(b.m_replies.size() == 1);
    CHECK(b.m_replies[0].m_allowed);
    CHECK(a.m_replies.empty());
    //同一个 seq 再来一次：已经认领过，丢弃
    module.ProcessFrame(bot_cmd_get_my_bots, seqB, "{\"code\":0}");
    CHECK(b.m_replies.size() == 1);
    //命令号对不上：不认领
    module.ProcessFrame(bot_cmd_try, seqA, "{\"code\":0}");
    CHECK(a.m_replies.empty());
    //没有登记的 seq：丢弃
    module.ProcessFrame(bot_cmd_get_detail, 9999, "{\"code\":0}");
    CHECK(a.m_replies.empty());
    //包体解析不出：仍交回，m_parsed 为 false
    module.ProcessFrame(bot_cmd_get_detail, seqA, "garbage");
    REQUIRE(a.m_replies.size() == 1);
    CHECK_FALSE(a.m_replies[0].m_parsed);

    //撤销接收方后它的应答丢弃，登记一并作废
    const uint32_t seqC = module.SendCommand(ca, bot_cmd_try, "{}");
    REQUIRE(seqC != 0);
    module.UnregisterClient(ca);
    CHECK(module.PendingRequestCount() == 0);
    module.ProcessFrame(bot_cmd_try, seqC, "{\"code\":0}");
    CHECK(a.m_replies.size() == 1);

    //号段以外的命令、空包体不发；宿主不放行时返回 0
    CHECK(module.SendCommand(cb, 2801, "{}") == 0);
    CHECK(module.SendCommand(cb, 2810, "{}") == 0);
    CHECK(module.SendCommand(cb, bot_cmd_try, "") == 0);
    host.m_refuseSend = true;
    CHECK(module.SendCommand(cb, bot_cmd_try, "{}") == 0);
    CHECK(module.PendingRequestCount() == 0);

    //断线重连：未收到应答的请求作废
    host.m_refuseSend = false;
    REQUIRE(module.SendCommand(cb, bot_cmd_try, "{}") != 0);
    module.OnLoggedIn();
    CHECK(module.PendingRequestCount() == 0);
    module.Shutdown();
}

TEST_CASE("BOTD16 群机器人列表从宿主的定长结构体换成 Core 的列表", "[botdll]")
{
    FakeHost host;
    CBotModule module;
    InitModule(module, host, NULL);
    GroupBotList list;
    REQUIRE(module.GetGroupBots(88, list));
    CHECK(host.m_listCalls == 1);
    CHECK(list.m_caps.m_isOwner);
    CHECK(list.m_caps.m_groupBotMax == 5);
    REQUIRE(list.m_bots.size() == 2);
    CHECK(list.m_bots[0].m_botId == 1000);
    CHECK(list.m_bots[0].m_name == L"IT 小助手");
    CHECK_FALSE(list.m_bots[0].m_isGroupBot);
    CHECK(list.m_bots[0].m_off);
    CHECK(list.m_bots[1].m_isGroupBot);
    CHECK(list.m_bots[1].m_color == L"#1E88E5");

    //超过预备的容量：按宿主报告的个数重取一次
    host.m_botCount = 70;
    host.m_listCalls = 0;
    REQUIRE(module.GetGroupBots(88, list));
    CHECK(host.m_listCalls == 2);
    CHECK(list.m_bots.size() == 70);

    host.m_hasList = false;
    CHECK_FALSE(module.GetGroupBots(88, list));
    CHECK(list.m_bots.empty());
    CHECK_FALSE(module.GetGroupBots(0, list));

    //显示名：宿主认识的取宿主的，不认识的用「用户<id>」
    CHECK(module.UserName(5302) == L"李四");
    CHECK(module.UserName(42) == L"用户42");
    CHECK(module.MyUserId() == 5308);
    module.RequestGroupBots(88);
    REQUIRE(host.m_refreshed.size() == 1);
    CHECK(host.m_refreshed[0] == 88);
    module.Shutdown();
}

TEST_CASE("BOTD17 选人请求登记到发起方名下", "[botdll]")
{
    FakeHost host;
    CBotModule module;
    InitModule(module, host, NULL);
    FakeSink sink;
    const uint32_t client = module.RegisterClient(&sink);
    std::vector<int64_t> pre;
    pre.push_back(5302);
    const uint32_t req = module.PickContacts(client, NULL, pre);
    REQUIRE(req != 0);
    CHECK(host.m_lastPickId == req);
    CHECK(host.m_lastPickCount == 1);

    const int64_t ids[] = { 5302, 5303, 0 };
    module.OnContactsPicked(req, ids, 3);
    REQUIRE(sink.m_pickIds.size() == 1);
    CHECK(sink.m_pickIds[0] == req);
    //无效的 userid（0）丢掉
    CHECK(sink.m_picked.size() == 2);
    //同一个请求号只认一次
    module.OnContactsPicked(req, ids, 2);
    CHECK(sink.m_pickIds.size() == 1);

    //撤销发起方之后的确认忽略
    const uint32_t req2 = module.PickContacts(client, NULL, std::vector<int64_t>());
    REQUIRE(req2 != 0);
    module.UnregisterClient(client);
    module.OnContactsPicked(req2, ids, 1);
    CHECK(sink.m_pickIds.size() == 1);

    //宿主弹不出
    host.m_refusePick = true;
    const uint32_t c2 = module.RegisterClient(&sink);
    CHECK(module.PickContacts(c2, NULL, pre) == 0);
    module.Shutdown();
}

TEST_CASE("BOTD21 选部门请求登记到发起方名下；部门名称", "[botdll]")
{
    FakeHost host;
    CBotModule module;
    InitModule(module, host, NULL);
    FakeSink sink;
    const uint32_t client = module.RegisterClient(&sink);
    std::vector<int64_t> pre;
    pre.push_back(30);
    pre.push_back(31);
    const uint32_t req = module.PickDepartments(client, NULL, pre);
    REQUIRE(req != 0);
    CHECK(host.m_lastDeptPickId == req);
    CHECK(host.m_lastDeptPickCount == 2);

    //确认：交回发起方，无效的部门ID（0）丢掉；不会被当成选人结果
    const int64_t ids[] = { 30, 0, 40 };
    module.OnDepartmentsPicked(req, ids, 3);
    REQUIRE(sink.m_deptPickIds.size() == 1);
    CHECK(sink.m_deptPickIds[0] == req);
    REQUIRE(sink.m_depts.size() == 2);
    CHECK(sink.m_depts[1] == 40);
    CHECK(sink.m_pickIds.empty());
    //同一个请求号只认一次；选人与选部门的请求号不会互相串用
    module.OnDepartmentsPicked(req, ids, 1);
    CHECK(sink.m_deptPickIds.size() == 1);
    const uint32_t contactReq = module.PickContacts(client, NULL, std::vector<int64_t>());
    REQUIRE(contactReq != 0);
    CHECK(contactReq != req);

    //撤销发起方之后的确认忽略
    const uint32_t req2 = module.PickDepartments(client, NULL, std::vector<int64_t>());
    REQUIRE(req2 != 0);
    module.UnregisterClient(client);
    module.OnDepartmentsPicked(req2, ids, 1);
    CHECK(sink.m_deptPickIds.size() == 1);

    //宿主弹不出
    host.m_refusePick = true;
    const uint32_t c2 = module.RegisterClient(&sink);
    CHECK(module.PickDepartments(c2, NULL, pre) == 0);

    //部门名称：宿主有就用宿主的，没有时为「部门<id>」
    CHECK(module.DeptName(30) == L"研发中心");
    CHECK(module.DeptName(99) == L"部门99");
    module.Shutdown();
}

TEST_CASE("BOTD18 顶层窗口的打开、通知、关闭与释放", "[botdll]")
{
    FakeHost host;
    FakeFactory factory;
    CBotModule module;
    InitModule(module, host, &factory);

    REQUIRE(module.OpenGroupBotManager(NULL, 88, L"项目群"));
    REQUIRE(module.OpenGroupBotManager(NULL, 99, NULL));
    FakeWindow* w88 = factory.m_managers[88];
    FakeWindow* w99 = factory.m_managers[99];
    CHECK(w88->m_groupName == L"项目群");
    CHECK(module.ManagerCount() == 2);
    //再次打开同一个群：复用，群名更新
    REQUIRE(module.OpenGroupBotManager(NULL, 88, L"新群名"));
    CHECK(factory.m_managers[88] == w88);
    CHECK(w88->m_opens == 2);
    CHECK(w88->m_groupName == L"新群名");
    CHECK(factory.m_live == 2);

    //列表变化：指定群只通知那个群，0 通知全部
    module.OnGroupBotsChanged(88);
    CHECK(w88->m_listChanges == 1);
    CHECK(w99->m_listChanges == 0);
    module.OnGroupBotsChanged(0);
    CHECK(w88->m_listChanges == 2);
    CHECK(w99->m_listChanges == 1);

    //企业开关与断线重连
    module.OnFeaturesChanged(true, true, false, true);
    CHECK(w88->m_featureChanges == 1);
    CHECK(module.Features().m_groupExternal);
    CHECK_FALSE(module.Features().m_personal);
    module.OnLoggedIn();
    CHECK(w99->m_reconnects == 1);

    //「我的机器人」
    REQUIRE(module.OpenMyBots(NULL));
    FakeWindow* mine = factory.m_myBots;
    REQUIRE(module.OpenMyBots(NULL));
    CHECK(factory.m_myBots == mine);
    CHECK(mine->m_opens == 2);
    CHECK(factory.m_live == 3);

    //宿主关闭某个群的管理窗：立即释放
    module.CloseGroupBotManager(99);
    CHECK(module.ManagerCount() == 1);
    CHECK(factory.m_live == 2);

    //用户关闭：先标记，释放时才删除；关闭之后再打开同一个群会新建
    module.OnTopWindowClosed(bot_window_group_manager, 88);
    CHECK(factory.m_live == 2);
    module.ReleaseClosedWindows();
    CHECK(module.ManagerCount() == 0);
    CHECK(factory.m_live == 1);
    module.OnTopWindowClosed(bot_window_my_bots, 0);
    REQUIRE(module.OpenMyBots(NULL));
    //关闭过的窗口对象已经释放，这是新建的一个（只打开过一次）
    CHECK(factory.m_myBots->m_opens == 1);
    CHECK(factory.m_live == 1);

    //总开关关闭：全部关闭，之后不能再打开
    REQUIRE(module.OpenGroupBotManager(NULL, 88, NULL));
    module.OnFeaturesChanged(false, true, true, false);
    CHECK(factory.m_live == 0);
    CHECK_FALSE(module.OpenGroupBotManager(NULL, 88, NULL));
    CHECK_FALSE(module.OpenMyBots(NULL));

    //退出登录：全部关闭
    module.OnFeaturesChanged(true, true, true, false);
    REQUIRE(module.OpenGroupBotManager(NULL, 88, NULL));
    module.OnLoggingOut();
    CHECK(factory.m_live == 0);
    CHECK_FALSE(module.OpenGroupBotManager(NULL, 0, NULL));
    module.Shutdown();
    //关闭之后不能再打开
    CHECK_FALSE(module.OpenGroupBotManager(NULL, 88, NULL));
}

TEST_CASE("BOTD19 初始化参数、版本号与号段", "[botdll]")
{
    FakeHost host;
    CBotModule module;
    module.SetHeadlessForTest(true);
    CHECK_FALSE(module.Init(NULL, NULL));
    BotInitParams params;
    params.m_cbSize = sizeof(params) + 1;
    params.m_appDir = L".";
    CHECK_FALSE(module.Init(&host, &params));
    params.m_cbSize = sizeof(params);
    CHECK(module.Init(&host, &params));
    CHECK(module.GetApiVersion() == kApiVersion);
    module.Shutdown();

    CHECK(IsApiVersionCompatible(kApiVersion, kApiVersion));
    CHECK_FALSE(IsApiVersionCompatible(kApiVersion, kApiVersion + 1));
    CHECK(IsBotDllCommand(2802));
    CHECK(IsBotDllCommand(2809));
    CHECK_FALSE(IsBotDllCommand(2801));
    CHECK_FALSE(IsBotDllCommand(2810));
    CHECK_FALSE(IsBotDllCommand(2820));
}
