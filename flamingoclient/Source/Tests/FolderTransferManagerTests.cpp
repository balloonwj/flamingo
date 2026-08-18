/**
 *  单聊文件夹传输会话表的纯逻辑单元测试（Suite FTM），FolderTransferManagerTests.cpp
 *
 *  FolderTransferManager 里能脱离网络、磁盘与窗口验证的部分都收在两处，本文件覆盖的就是
 *  它们：
 *    · 并发笔数上限的规范化 NormalizeFolderMaxConcurrent（FTM1~FTM3）—— 配置项
 *      flamingo.ini 的 [app] foldermaxconcurrent 缺失、非数字、负数、越界时都必须回落到
 *      默认的 5 笔，否则闸门要么形同虚设、要么把功能整个关死。
 *    · 会话表 FolderSessionRegistry（FTM4~FTM19）—— 名额占用与释放、失败记录的保留期、
 *      多条待应答邀请的取出、按会话 id / 本地占位 id 的路由、以及 1810 应答按帧 seq 的配对。
 *
 *  这几条是本次「本机全局只跑一笔 → 同时最多 5 笔」改造的核心不变量，故逐条钉死：
 *  名额满了必须拒绝新的一笔、任一笔结束后名额必须立刻回来、<u>失败之后名额立刻放开但
 *  重试记录仍在</u>（这两件事是分开的，混作一谈会让用户在一笔失败后干等 10 分钟）。
 *
 *  建通道、发信令、起状态机、回投窗口消息不在单元测试覆盖范围（与 FolderRelayClient
 *  一致，靠两账号真机联调验证），本文件不起任何连接、不读写任何文件、不建任何窗口。
 *
 *  框架 Catch2 v3。
 *
 *  balloonwj@qq.com   2026-08-14
 */

#include "catch_amalgamated.hpp"

#include <string>
#include <vector>

//被测头文件放在最后：它引入 <windows.h>，而后者会带进 min / max 宏。这两个宏一旦先定义，
//Catch2 与标准库里形如 std::numeric_limits<T>::min() 的写法就会被当成宏调用，编译期报出
//一串 C4003"函数式宏的参数不足"。
#include "../Feature/FolderTransfer/FolderTransferManager.h"

using namespace foldertransfer;

namespace {

//造一条发送侧记录：占着名额、还没拿到服务端会话 id（正在等 1810 的应答）。
FolderSessionRecord makeSenderRecord(const std::string& localId, int64_t peerUserId)
{
    FolderSessionRecord record;
    record.m_role = kFolderRoleSender;
    record.m_peerUserId = peerUserId;
    record.m_localId = localId;
    record.m_active = true;
    record.m_rootPath = L"D:\\work\\report";
    record.m_folderName = L"report";
    return record;
}

//造一条接收侧记录：占着名额，且带一条待应答的邀请。
FolderSessionRecord makeInviteRecord(const std::string& sessionId, int64_t fromUserId,
                                     const std::string& folderNameUtf8, int64_t receivedAtMs)
{
    FolderSessionRecord record;
    record.m_role = kFolderRoleRecv;
    record.m_peerUserId = fromUserId;
    record.m_sessionId = sessionId;
    record.m_active = true;
    record.m_invite.m_sessionId = sessionId;
    record.m_invite.m_fromUserId = fromUserId;
    record.m_invite.m_folderNameUtf8 = folderNameUtf8;
    record.m_invite.m_fileCount = 3;
    record.m_invite.m_totalBytes = 1024;
    record.m_invite.m_receivedAtMs = receivedAtMs;
    return record;
}

//往表里塞 count 笔占名额的传输，会话 id 形如 "s1"、"s2"……
void fillActiveSessions(FolderSessionRegistry& registry, int count)
{
    for (int i = 0; i < count; ++i)
    {
        FolderSessionRecord record;
        record.m_role = kFolderRoleSender;
        record.m_peerUserId = 100 + i;
        record.m_sessionId = "s" + std::to_string(i + 1);
        record.m_active = true;
        registry.Insert(record);
    }
}

//本文件里当作"现在"用的基准时刻（::GetTickCount64 取值，毫秒）。取一个远大于保留期的
//数，这样"失败于 now - 保留期 - 1 毫秒"这类算式不会算成负数。
const int64_t kNowMs = 100000000;

} // anonymous namespace

// =====================================================================
// FTM1~FTM3 —— 并发笔数上限的规范化（配置项缺失 / 非法时的回落）
// =====================================================================

TEST_CASE("FTM1 配置项缺失或取值非法时回落默认的 5 笔", "[foldermanager]")
{
    //键不存在时读取接口回的就是调用方给的 0，与"值写成非数字"、"值写成负数"落到同一条
    //越界分支上，三者都必须回落到默认值。
    CHECK(NormalizeFolderMaxConcurrent(0) == kFolderMaxConcurrentDefault);
    CHECK(NormalizeFolderMaxConcurrent(-1) == kFolderMaxConcurrentDefault);
    CHECK(NormalizeFolderMaxConcurrent(-100) == kFolderMaxConcurrentDefault);
    CHECK(kFolderMaxConcurrentDefault == 5);
}

TEST_CASE("FTM2 合法取值原样生效", "[foldermanager]")
{
    CHECK(NormalizeFolderMaxConcurrent(1) == 1);
    CHECK(NormalizeFolderMaxConcurrent(2) == 2);
    CHECK(NormalizeFolderMaxConcurrent(5) == 5);
    CHECK(NormalizeFolderMaxConcurrent(kFolderMaxConcurrentHardLimit)
          == kFolderMaxConcurrentHardLimit);
}

TEST_CASE("FTM3 超过硬上限的取值同样回落默认值", "[foldermanager]")
{
    //硬上限之外的数不是"截断到上限"而是"当成填错了"：填 100 的人多半是笔误或误解，
    //按 16 笔跑起来只会让每一笔都变慢，回落到 5 更接近他真正想要的。
    CHECK(NormalizeFolderMaxConcurrent(kFolderMaxConcurrentHardLimit + 1)
          == kFolderMaxConcurrentDefault);
    CHECK(NormalizeFolderMaxConcurrent(100) == kFolderMaxConcurrentDefault);
    CHECK(NormalizeFolderMaxConcurrent(1000000) == kFolderMaxConcurrentDefault);
}

// =====================================================================
// FTM4~FTM7 —— 名额闸门：占用、达上限拒绝、结束后释放
// =====================================================================

TEST_CASE("FTM4 会话表默认上限为 5 笔且初始不忙", "[foldermanager]")
{
    FolderSessionRegistry registry;
    CHECK(registry.GetMaxConcurrent() == kFolderMaxConcurrentDefault);
    CHECK(registry.ActiveCount() == 0);
    CHECK_FALSE(registry.IsFull());
    CHECK(registry.Size() == 0);
}

TEST_CASE("FTM5 设上限时越界值同样回落默认值", "[foldermanager]")
{
    FolderSessionRegistry registry;

    registry.SetMaxConcurrent(3);
    CHECK(registry.GetMaxConcurrent() == 3);

    registry.SetMaxConcurrent(0);
    CHECK(registry.GetMaxConcurrent() == kFolderMaxConcurrentDefault);

    registry.SetMaxConcurrent(kFolderMaxConcurrentHardLimit + 1);
    CHECK(registry.GetMaxConcurrent() == kFolderMaxConcurrentDefault);
}

TEST_CASE("FTM6 名额未满时不拦，满了才拦", "[foldermanager]")
{
    FolderSessionRegistry registry;

    //前 4 笔都还有余量
    fillActiveSessions(registry, 4);
    CHECK(registry.ActiveCount() == 4);
    CHECK_FALSE(registry.IsFull());

    //第 5 笔一进来就到顶，此后再发起要被拒
    fillActiveSessions(registry, 1);
    CHECK(registry.ActiveCount() == 5);
    CHECK(registry.IsFull());
}

TEST_CASE("FTM7 某一笔结束后名额立刻释放", "[foldermanager]")
{
    FolderSessionRegistry registry;
    fillActiveSessions(registry, 5);
    REQUIRE(registry.IsFull());

    //传完 / 被拒绝 / 被撤回都是把整条记录删掉
    CHECK(registry.Erase("s3"));
    CHECK(registry.ActiveCount() == 4);
    CHECK_FALSE(registry.IsFull());
    CHECK(registry.Size() == 4);
    CHECK(registry.FindBySessionId("s3") == NULL);

    //删一个不存在的 id 不改变任何东西
    CHECK_FALSE(registry.Erase("s3"));
    CHECK_FALSE(registry.Erase(""));
    CHECK(registry.ActiveCount() == 4);
}

// =====================================================================
// FTM8~FTM11 —— 失败：名额立刻释放，重试记录仍在保留期内
// =====================================================================

TEST_CASE("FTM8 失败后名额立刻释放但记录与清单仍在", "[foldermanager]")
{
    FolderSessionRegistry registry;
    fillActiveSessions(registry, 5);
    REQUIRE(registry.IsFull());

    FolderSessionRecord* record = registry.FindBySessionId("s2");
    REQUIRE(record != NULL);
    //清单是重试续传的本钱，失败之后必须原样留着
    record->m_manifestReady = true;
    record->m_totalFiles = 42;

    registry.MarkFailed(*record, kNowMs);

    //名额这一刻就回来了：用户不必等这一笔的 10 分钟保留期过完才能另发一笔
    CHECK(registry.ActiveCount() == 4);
    CHECK_FALSE(registry.IsFull());

    //而记录本身、连同清单与规模统计照旧在表里
    CHECK(registry.Size() == 5);
    const FolderSessionRecord* kept = registry.FindBySessionId("s2");
    REQUIRE(kept != NULL);
    CHECK_FALSE(kept->m_active);
    CHECK(kept->m_failedAtMs == kNowMs);
    CHECK(kept->m_manifestReady);
    CHECK(kept->m_totalFiles == 42);
}

TEST_CASE("FTM9 保留期内可重试，期满不可", "[foldermanager]")
{
    FolderSessionRegistry registry;
    fillActiveSessions(registry, 1);
    FolderSessionRecord* record = registry.FindBySessionId("s1");
    REQUIRE(record != NULL);
    registry.MarkFailed(*record, kNowMs);

    //刚失败、以及差 1 毫秒到期，都还能续传
    CHECK(registry.IsWithinRetryWindow("s1", kNowMs));
    CHECK(registry.IsWithinRetryWindow("s1", kNowMs + kFolderRetryWindowMs - 1));

    //正好到期与已经超期都不能再续传（服务端那边会话已销毁）
    CHECK_FALSE(registry.IsWithinRetryWindow("s1", kNowMs + kFolderRetryWindowMs));
    CHECK_FALSE(registry.IsWithinRetryWindow("s1", kNowMs + kFolderRetryWindowMs + 1));
}

TEST_CASE("FTM10 没失败过的会话与不存在的会话都不给重试", "[foldermanager]")
{
    FolderSessionRegistry registry;
    fillActiveSessions(registry, 2);

    //还在正常传输中的那一笔不是"可重试的失败会话"
    CHECK_FALSE(registry.IsWithinRetryWindow("s1", kNowMs));
    //查无此会话
    CHECK_FALSE(registry.IsWithinRetryWindow("no-such-session", kNowMs));
    CHECK_FALSE(registry.IsWithinRetryWindow("", kNowMs));
}

TEST_CASE("FTM11 重试重新占名额，超期记录被清理", "[foldermanager]")
{
    FolderSessionRegistry registry;
    fillActiveSessions(registry, 3);

    FolderSessionRecord* failed = registry.FindBySessionId("s1");
    REQUIRE(failed != NULL);
    registry.MarkFailed(*failed, kNowMs);
    REQUIRE(registry.ActiveCount() == 2);

    //保留期内点「重试」：这一笔重新占住一个名额
    failed->m_active = true;
    CHECK(registry.ActiveCount() == 3);

    //另造一笔早已超期的失败记录，它应当被清掉，而活跃的那三笔一条都不能少
    FolderSessionRecord stale;
    stale.m_role = kFolderRoleSender;
    stale.m_sessionId = "stale";
    stale.m_active = false;
    stale.m_failedAtMs = kNowMs - kFolderRetryWindowMs - 1;
    registry.Insert(stale);
    CHECK(registry.Size() == 4);

    CHECK(registry.PurgeExpired(kNowMs) == 1);
    CHECK(registry.Size() == 3);
    CHECK(registry.FindBySessionId("stale") == NULL);
    CHECK(registry.ActiveCount() == 3);

    //刚失败还没超期的那种不能被清掉 —— 清了就等于把断点续传的本钱扔了
    FolderSessionRecord fresh;
    fresh.m_role = kFolderRoleSender;
    fresh.m_sessionId = "fresh";
    fresh.m_active = false;
    fresh.m_failedAtMs = kNowMs - 1;
    registry.Insert(fresh);
    CHECK(registry.PurgeExpired(kNowMs) == 0);
    CHECK(registry.FindBySessionId("fresh") != NULL);
}

// =====================================================================
// FTM12~FTM14 —— 待应答邀请：同一对端可以同时挂着多条
// =====================================================================

TEST_CASE("FTM12 同一对端的多条待应答邀请全部取出且保持先后顺序", "[foldermanager]")
{
    FolderSessionRegistry registry;
    //同一个人先后发来两个文件夹，中间夹着另一个人发来的一条
    registry.Insert(makeInviteRecord("sess-a", 1001, "report", kNowMs));
    registry.Insert(makeInviteRecord("sess-b", 2002, "photos", kNowMs + 1));
    registry.Insert(makeInviteRecord("sess-c", 1001, "code", kNowMs + 2));

    std::vector<FolderPendingInvite> invites;
    CHECK(registry.CollectPendingInvites(1001, invites) == 2);
    REQUIRE(invites.size() == 2);
    //顺序即收到的先后，聊天窗按这个顺序把气泡逐条补出来
    CHECK(invites[0].m_sessionId == "sess-a");
    CHECK(invites[0].m_folderNameUtf8 == "report");
    CHECK(invites[1].m_sessionId == "sess-c");
    CHECK(invites[1].m_folderNameUtf8 == "code");

    //另一个人的那条只属于他自己的会话窗
    CHECK(registry.CollectPendingInvites(2002, invites) == 1);
    REQUIRE(invites.size() == 1);
    CHECK(invites[0].m_sessionId == "sess-b");

    //与之无关的对端查不出任何邀请，出参被清空
    CHECK(registry.CollectPendingInvites(3003, invites) == 0);
    CHECK(invites.empty());
    CHECK(registry.CollectPendingInvites(0, invites) == 0);
}

TEST_CASE("FTM13 已应答与已作废的邀请不再被取出", "[foldermanager]")
{
    FolderSessionRegistry registry;
    registry.Insert(makeInviteRecord("sess-a", 1001, "report", kNowMs));
    registry.Insert(makeInviteRecord("sess-c", 1001, "code", kNowMs + 2));

    //用户点了「接收」：这一条不再"待应答"，气泡此后由传输进度维护
    FolderSessionRecord* accepted = registry.FindBySessionId("sess-a");
    REQUIRE(accepted != NULL);
    accepted->m_invite = FolderPendingInvite();

    std::vector<FolderPendingInvite> invites;
    CHECK(registry.CollectPendingInvites(1001, invites) == 1);
    REQUIRE(invites.size() == 1);
    CHECK(invites[0].m_sessionId == "sess-c");

    //另一条被撤回 / 超时：整条记录删掉，一条都查不出来
    CHECK(registry.Erase("sess-c"));
    CHECK(registry.CollectPendingInvites(1001, invites) == 0);
}

TEST_CASE("FTM14 失败之后不再把邀请当作待应答", "[foldermanager]")
{
    FolderSessionRegistry registry;
    registry.Insert(makeInviteRecord("sess-a", 1001, "report", kNowMs));

    FolderSessionRecord* record = registry.FindBySessionId("sess-a");
    REQUIRE(record != NULL);
    registry.MarkFailed(*record, kNowMs);

    //失败之后再给用户一个「接收」按钮，点下去必然失败
    std::vector<FolderPendingInvite> invites;
    CHECK(registry.CollectPendingInvites(1001, invites) == 0);
    //但记录本身仍在，且仍在保留期内（接收端的重试是续着写已落盘的部分）
    CHECK(registry.IsWithinRetryWindow("sess-a", kNowMs));
}

// =====================================================================
// FTM15~FTM19 —— 路由：会话 id / 本地占位 id 两种定位方式，以及邀请应答按帧 seq 的配对
// =====================================================================

TEST_CASE("FTM15 按会话 id 与本地占位 id 都能定位到同一笔", "[foldermanager]")
{
    FolderSessionRegistry registry;
    registry.Insert(makeSenderRecord("local-1", 1001));
    registry.Insert(makeInviteRecord("sess-b", 2002, "photos", kNowMs));

    //服务端会话 id 还没回来，只能按本地占位 id 查
    CHECK(registry.FindBySessionId("local-1") == NULL);
    REQUIRE(registry.FindByLocalId("local-1") != NULL);
    REQUIRE(registry.FindByAnyId("local-1") != NULL);
    CHECK(registry.FindByAnyId("local-1")->m_peerUserId == 1001);

    //应答到达后两种 id 都指向同一条记录
    registry.FindByLocalId("local-1")->m_sessionId = "sess-a";
    REQUIRE(registry.FindByAnyId("sess-a") != NULL);
    CHECK(registry.FindByAnyId("sess-a") == registry.FindByAnyId("local-1"));

    //按会话 id 删得掉，按本地占位 id 也删得掉
    CHECK(registry.Erase("sess-a"));
    CHECK(registry.FindByAnyId("local-1") == NULL);
    CHECK(registry.Size() == 1);
    CHECK(registry.FindByAnyId("sess-b") != NULL);
}

TEST_CASE("FTM16 邀请应答按帧 seq 配对，乱序到达也各归各笔", "[foldermanager]")
{
    FolderSessionRegistry registry;
    registry.Insert(makeSenderRecord("local-1", 1001));
    registry.Insert(makeSenderRecord("local-2", 2002));
    registry.Insert(makeSenderRecord("local-3", 3003));
    registry.RegisterInviteSeq(11, "local-1");
    registry.RegisterInviteSeq(12, "local-2");
    registry.RegisterInviteSeq(13, "local-3");
    CHECK(registry.InviteAckPendingCount() == 3);

    //应答乱序到达（第三笔的先回来）照样各归各笔 —— 这正是按 seq 配对而不是按先后次序
    //配对的意义：不必依赖"服务端就地回包、应答顺序等于请求顺序"这个前提
    std::string localId;
    REQUIRE(registry.TakeInviteSeq(13, localId));
    CHECK(localId == "local-3");
    REQUIRE(registry.FindByLocalId(localId) != NULL);
    CHECK(registry.FindByLocalId(localId)->m_peerUserId == 3003);
    registry.FindByLocalId(localId)->m_sessionId = "sess-3";

    REQUIRE(registry.TakeInviteSeq(11, localId));
    CHECK(localId == "local-1");
    REQUIRE(registry.FindByLocalId(localId) != NULL);
    CHECK(registry.FindByLocalId(localId)->m_peerUserId == 1001);

    //取过的 seq 不会再命中第二次（同一条应答重复到达时不会误伤别人那一笔）
    CHECK_FALSE(registry.TakeInviteSeq(11, localId));
    CHECK(localId.empty());
    CHECK(registry.InviteAckPendingCount() == 1);
}

TEST_CASE("FTM17 取消掉的那一笔应答回来时不误伤别人", "[foldermanager]")
{
    FolderSessionRegistry registry;
    registry.Insert(makeSenderRecord("local-1", 1001));
    registry.Insert(makeSenderRecord("local-2", 2002));
    registry.RegisterInviteSeq(21, "local-1");
    registry.RegisterInviteSeq(22, "local-2");

    //用户在应答到达前取消了第一笔：记录没了，登记项还在
    CHECK(registry.Erase("local-1"));

    //它的应答回来时按 seq 命中登记项、却查不到记录 —— 调用方据此把服务端刚建好的那笔
    //会话撤掉，而不是把会话 id 记到别人的气泡上
    std::string localId;
    REQUIRE(registry.TakeInviteSeq(21, localId));
    CHECK(localId == "local-1");
    CHECK(registry.FindByLocalId(localId) == NULL);

    //另一笔完全不受影响
    REQUIRE(registry.TakeInviteSeq(22, localId));
    CHECK(localId == "local-2");
    REQUIRE(registry.FindByLocalId(localId) != NULL);
    CHECK(registry.FindByLocalId(localId)->m_peerUserId == 2002);
}

TEST_CASE("FTM18 对不上的 seq 一律不命中，也不动任何记录", "[foldermanager]")
{
    FolderSessionRegistry registry;
    registry.Insert(makeSenderRecord("local-1", 1001));
    registry.RegisterInviteSeq(31, "local-1");

    //陌生的 seq（本端刚 Reset 过、或服务端串号）不能落到任何一笔上
    std::string localId;
    CHECK_FALSE(registry.TakeInviteSeq(99, localId));
    CHECK(localId.empty());
    CHECK(registry.InviteAckPendingCount() == 1);
    CHECK(registry.FindByLocalId("local-1") != NULL);
    CHECK(registry.FindByLocalId("local-1")->m_sessionId.empty());

    //seq 为 0 表示请求根本没发出去，不登记也不命中
    registry.RegisterInviteSeq(0, "local-1");
    CHECK(registry.InviteAckPendingCount() == 1);
    CHECK_FALSE(registry.TakeInviteSeq(0, localId));

    //本笔自己的 seq 照常命中
    REQUIRE(registry.TakeInviteSeq(31, localId));
    CHECK(localId == "local-1");
}

TEST_CASE("FTM19 整体清理把记录与等待应答表一并清空", "[foldermanager]")
{
    FolderSessionRegistry registry;
    fillActiveSessions(registry, 3);
    registry.Insert(makeSenderRecord("local-1", 1001));
    registry.RegisterInviteSeq(41, "local-1");
    REQUIRE(registry.Size() == 4);
    REQUIRE(registry.InviteAckPendingCount() == 1);

    //退出登录 / 被踢下线：重试窗口只在本次登录期间有效，全部作废
    registry.Clear();
    CHECK(registry.Size() == 0);
    CHECK(registry.ActiveCount() == 0);
    CHECK_FALSE(registry.IsFull());
    CHECK(registry.InviteAckPendingCount() == 0);
    //上限是配置项，不随登出重置
    CHECK(registry.GetMaxConcurrent() == kFolderMaxConcurrentDefault);
}
