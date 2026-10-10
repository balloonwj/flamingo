/**
 *  「备忘与提醒」的端到端联调用例（标签 [.live]），MemoLiveTests.cpp
 *
 *  MEM-L1（与用户确认过的联调用例，docs/备忘与提醒方案.md 第 5 节）的协议部分：群主 A 新建只提醒 B 的群备忘，
 *  B 收到推送、拉得到，C 收不到、拉不到；改成只提醒 C 后 B 收到删除、C 收到新增；删除后 C 收到删除。另外核对
 *  普通成员不能保存群备忘、个人备忘只推给本人。三个人都用 LiveChatClient 直接收发 v2 帧。
 *
 *  MEM-L1S（[memoseed]）：给 PC 客户端联调造数据。A 新建一条两分钟后到点、只提醒 B 的群备忘，不删除；
 *  B 用真实的 PC 客户端登录，核对右下角提醒弹窗（或全屏提醒）。环境变量 MEMO_SEED_STYLE=2 时用全屏方式，
 *  MEMO_SEED_MINUTES 改到点前的分钟数（默认 2）。
 *
 *  默认不参与常规测试：用例带 [.live] 标签，Catch2 会跳过。跑之前起 tmp/memotest 的测试服务端
 *  （start_server.ps1）；跑 MEM-L1 时不要开着 tmp/memotest 的任何 PC 客户端（同一账号同类终端重复登录会互相顶下线）。
 *
 *      FlamingoUnit.exe "[memolive]"
 *      FlamingoUnit.exe "[memoseed]"
 *
 *  账号：MY0127（5127，群 268435463 的群主，作 A）、MY0308（5308，作 B）、MY0010（5010，作 C），密码都是 perf1234。
 *
 *  balloonwj@qq.com   2026-10-03
 */
#include "stdafx.h"
#include "catch_amalgamated.hpp"

#include <stdlib.h>
#include <string>
#include "LiveChatClient.h"
#include "../net/MemoProtocolClient.h"
#include "../Feature/Memo/MemoRule.h"
#include "../Feature/Memo/MemoClock.h"
#include "../third-party/jsoncpp-1.9.0/json.h"

using namespace livetest;

namespace {

//tmp/memotest 测试服务端的 chatserver 端口
const unsigned short kMemoTestServerPort = 29800;

//三个人的账号、密码与 userid，以及他们所在的群
const char* kAccountA = "MY0127";
const char* kAccountB = "MY0308";
const char* kAccountC = "MY0010";
const char* kPassword = "perf1234";
const int64_t kUserB = 5308;
const int64_t kUserC = 5010;
const int64_t kGroup = 268435463LL;

//等一条应答或推送的最长时间、确认「没有」推送时等待的时间，单位毫秒
const int kWaitMs = 5000;
const int kNoNotifyWaitMs = 2000;

//error_code 里用到的取值：成功、不是群主或管理员
const int kCodeOk = 0;
const int kCodeNotGroupAdmin = 303;

//memo_change_type：新增或修改、删除
const int kChangeUpsert = 1;
const int kChangeDelete = 2;

//联调造数据：默认两分钟后到点
const int kSeedDefaultMinutes = 2;

bool parseJson(const std::string& text, Json::Value& root)
{
    Json::CharReaderBuilder b;
    JSONCPP_STRING errs;
    Json::CharReader* reader = b.newCharReader();
    bool ok = reader->parse(text.c_str(), text.c_str() + text.size(), &root, &errs);
    delete reader;
    return ok && root.isObject();
}

void login(LiveChatClient& c, const char* account)
{
    std::string reason;
    const bool ok = c.ConnectAndLogin(kMemoTestServerPort, kTestServerPubKey, account, kPassword, reason);
    INFO("login " << account << " failed: " << reason);
    REQUIRE(ok);
    c.ClearBacklog();
}

//开始时间：本地时间的明天 9:00（足够远，用例跑的过程中不会到点）
int64_t tomorrowNine()
{
    const int kHourNine = 9;
    const int kMinutesPerHour = 60;
    const memo::LocalMinute now = memo::NowLocalMinute();
    const int64_t tomorrow = now / memo::kMinutesPerDay + 1;
    return memo::StartFromLocalMinute(tomorrow * memo::kMinutesPerDay + kHourNine * kMinutesPerHour);
}

net::MemoItem groupMemo(const std::string& title, int64_t target)
{
    net::MemoItem m;
    m.m_ctype = net::kMemoCtypeGroup;
    m.m_groupid = kGroup;
    m.m_title = title;
    m.m_start = tomorrowNine();
    m.m_targets.push_back(target);
    return m;
}

//保存并等应答；返回应答的 code，成功时 outMemoid 为服务端分配的 ID
int save(LiveChatClient& c, const net::MemoItem& memo, int64_t& outMemoid)
{
    outMemoid = 0;
    REQUIRE(c.Send(cmd_memo_save, net::MemoProtocolClient::buildSaveRequest(memo)));
    std::string resp;
    REQUIRE(c.WaitFor(cmd_memo_save, resp, kWaitMs));
    net::MemoSaveResult r;
    REQUIRE(net::MemoProtocolClient::parseSaveResponse(resp, r));
    INFO("save response: " << resp);
    if (r.m_hasMemo)
        outMemoid = r.m_memo.m_memoid;
    return r.m_code;
}

//在 timeoutMs 内等一条关于 memoid 的推送；等到返回它的 changetype，没等到返回 0
int waitNotify(LiveChatClient& c, int64_t memoid, int timeoutMs)
{
    const ULONGLONG deadline = ::GetTickCount64() + (ULONGLONG)timeoutMs;
    while (::GetTickCount64() < deadline)
    {
        const int left = (int)(deadline - ::GetTickCount64());
        std::string body;
        if (!c.WaitFor(cmd_memo_notify, body, left > 0 ? left : 1))
            return 0;
        net::MemoNotify n;
        if (net::MemoProtocolClient::parseNotify(body, n) && n.m_memoid == memoid)
            return n.m_changeType;
    }
    return 0;
}

//拉全部备忘，看里面有没有 memoid
bool listHas(LiveChatClient& c, int64_t memoid)
{
    REQUIRE(c.Send(cmd_memo_list, net::MemoProtocolClient::buildListRequest(0)));
    std::string resp;
    REQUIRE(c.WaitFor(cmd_memo_list, resp, kWaitMs));
    net::MemoListResult r;
    REQUIRE(net::MemoProtocolClient::parseListResponse(resp, r));
    REQUIRE(r.m_code == kCodeOk);
    for (size_t i = 0; i < r.m_memos.size(); ++i)
    {
        if (r.m_memos[i].m_memoid == memoid)
            return true;
    }
    return false;
}

int removeMemo(LiveChatClient& c, int64_t memoid)
{
    REQUIRE(c.Send(cmd_memo_delete, net::MemoProtocolClient::buildDeleteRequest(memoid)));
    std::string resp;
    REQUIRE(c.WaitFor(cmd_memo_delete, resp, kWaitMs));
    net::MemoDeleteResult r;
    REQUIRE(net::MemoProtocolClient::parseDeleteResponse(resp, r));
    return r.m_code;
}

} // anonymous namespace

TEST_CASE("MEM-L1 群备忘只推给、只给被提醒的成员与群主管理员看，改提醒对象与删除都同步", "[.live][memolive]")
{
    LiveChatClient a;
    LiveChatClient b;
    LiveChatClient c;
    login(a, kAccountA);
    login(b, kAccountB);
    login(c, kAccountC);

    //普通成员不能保存群备忘
    int64_t memoid = 0;
    CHECK(save(c, groupMemo("MEM-L1 by member", kUserB), memoid) == kCodeNotGroupAdmin);

    //群主新建只提醒 B 的群备忘：B 收到新增，C 收不到；B 拉得到，C 拉不到
    REQUIRE(save(a, groupMemo("MEM-L1 weekly sync", kUserB), memoid) == kCodeOk);
    REQUIRE(memoid > 0);
    CHECK(waitNotify(b, memoid, kWaitMs) == kChangeUpsert);
    CHECK(waitNotify(c, memoid, kNoNotifyWaitMs) == 0);
    CHECK(listHas(a, memoid));
    CHECK(listHas(b, memoid));
    CHECK_FALSE(listHas(c, memoid));

    //改成只提醒 C：B 收到删除、拉不到了，C 收到新增
    net::MemoItem changed = groupMemo("MEM-L1 weekly sync", kUserC);
    changed.m_memoid = memoid;
    int64_t savedId = 0;
    REQUIRE(save(a, changed, savedId) == kCodeOk);
    CHECK(savedId == memoid);
    CHECK(waitNotify(b, memoid, kWaitMs) == kChangeDelete);
    CHECK(waitNotify(c, memoid, kWaitMs) == kChangeUpsert);
    CHECK_FALSE(listHas(b, memoid));
    CHECK(listHas(c, memoid));

    //普通成员不能删群备忘；群主删除后 C 收到删除
    CHECK(removeMemo(c, memoid) == kCodeNotGroupAdmin);
    CHECK(removeMemo(a, memoid) == kCodeOk);
    CHECK(waitNotify(c, memoid, kWaitMs) == kChangeDelete);
    CHECK_FALSE(listHas(a, memoid));

    //个人备忘只推给本人：B 新建一条，A 收不到；B 拉得到，A 拉不到；最后删掉
    net::MemoItem personal;
    personal.m_ctype = net::kMemoCtypePersonal;
    personal.m_title = "MEM-L1 personal";
    personal.m_start = tomorrowNine();
    int64_t personalId = 0;
    REQUIRE(save(b, personal, personalId) == kCodeOk);
    CHECK(waitNotify(a, personalId, kNoNotifyWaitMs) == 0);
    CHECK(listHas(b, personalId));
    CHECK_FALSE(listHas(a, personalId));
    CHECK(removeMemo(a, personalId) != kCodeOk);
    CHECK(removeMemo(b, personalId) == kCodeOk);
}

TEST_CASE("MEM-L1S 给 PC 客户端联调造一条快到点、只提醒 B 的群备忘", "[.live][memoseed]")
{
    LiveChatClient a;
    login(a, kAccountA);

    int minutes = kSeedDefaultMinutes;
    char buf[16] = { 0 };
    size_t len = 0;
    if (getenv_s(&len, buf, sizeof(buf), "MEMO_SEED_MINUTES") == 0 && len > 0)
        minutes = atoi(buf);
    int style = 1;
    if (getenv_s(&len, buf, sizeof(buf), "MEMO_SEED_STYLE") == 0 && len > 0)
        style = atoi(buf);

    net::MemoItem memo = groupMemo(style == 2 ? "MEM-L1 fullscreen check" : "MEM-L1 popup check", kUserB);
    memo.m_start = memo::StartFromLocalMinute(memo::NowLocalMinute() + minutes);
    memo.m_style = style;
    memo.m_note = "MEM-L1 seed";
    int64_t memoid = 0;
    REQUIRE(save(a, memo, memoid) == kCodeOk);
    WARN("seeded memo " << memoid << " at " << net::MemoProtocolClient::formatStart(memo.m_start)
         << ", style " << style);
}
