/**
 *  ConversationListOpsTests.cpp
 *
 *  最近会话列表纯操作函数（BalloonMain/ConversationListOps.h）的测试：排序口径、
 *  按 (ctype,cid) 查找、删除单条 / 取标识快照、改置顶（含改完自动重排）、改免打扰、
 *  免打扰查询的兜底行为。
 *
 *  这些函数支撑会话列表右键菜单的"删除该会话 / 删除全部会话 / 置顶 / 免打扰"四个
 *  动作，出错的直接后果是删错会话或列表顺序错乱，故逐条钉死行为。
 *
 *  RD 系列另外覆盖"收到消息时算不算用户已经看到"的判据与未读数演算，支撑的是
 *  "聊天窗口就在眼前、消息也渲染进去了，会话列表却仍亮着红点"这个缺陷的修复。
 *
 *  用 Catch2 v3（amalgamated）作框架，纯内存断言，不创建窗口 / 不发协议。
 *  全局 _Module 由 BalloonChatLayoutTests.cpp 统一定义，本文件只引用。
 *
 *  balloonwj@qq.com   2026-07-31
 */

#include "stdafx.h"

#include "catch_amalgamated.hpp"

#include <vector>

#include "../BalloonMain/ConversationListOps.h"

using namespace balloonmain;

namespace {

// 会话类型常量，与 net 层 enum conversation_type 对齐（测试里不引 Protocol.h，
// 避免把服务端协议头拖进测试工程）。
const int kSingle = 1;   // 单聊
const int kGroup  = 2;   // 群聊

// 造一条会话。pinned / timestamp 是排序的两个决定因素，故都作显式参数。
net::ConversationItem MakeConv(int ctype, int64_t cid, int pinned, int64_t timestamp)
{
    net::ConversationItem c;
    c.m_ctype     = ctype;
    c.m_cid       = cid;
    c.m_pinned    = pinned;
    c.m_timestamp = timestamp;
    return c;
}

} // anonymous namespace

// =====================================================================
// OPS1~OPS3 —— 排序口径：置顶优先 → 时间倒序 → cid 升序兜底
// =====================================================================

TEST_CASE("OPS1 置顶会话排在未置顶会话前面", "[convops]")
{
    std::vector<net::ConversationItem> items;
    //故意让未置顶那条时间更新：置顶的优先级必须压过时间。
    items.push_back(MakeConv(kSingle, 1001, 0, 9000));
    items.push_back(MakeConv(kSingle, 1002, 1, 1000));

    SortConversations(items);

    REQUIRE(items.size() == 2);
    CHECK(items[0].m_cid == 1002);
    CHECK(items[1].m_cid == 1001);
}

TEST_CASE("OPS2 同为未置顶时按最后消息时间倒序", "[convops]")
{
    std::vector<net::ConversationItem> items;
    items.push_back(MakeConv(kSingle, 1001, 0, 1000));
    items.push_back(MakeConv(kSingle, 1002, 0, 3000));
    items.push_back(MakeConv(kSingle, 1003, 0, 2000));

    SortConversations(items);

    REQUIRE(items.size() == 3);
    CHECK(items[0].m_cid == 1002);
    CHECK(items[1].m_cid == 1003);
    CHECK(items[2].m_cid == 1001);
}

TEST_CASE("OPS3 置顶与时间都相同时按 cid 升序，顺序稳定", "[convops]")
{
    //时间戳只到毫秒，同一秒内的两条会话时间相同是常态；没有兜底键的话顺序会随
    //排序算法实现抖动，用户会看到列表每次刷新都在跳。
    std::vector<net::ConversationItem> items;
    items.push_back(MakeConv(kSingle, 1005, 0, 2000));
    items.push_back(MakeConv(kSingle, 1002, 0, 2000));
    items.push_back(MakeConv(kSingle, 1009, 0, 2000));

    SortConversations(items);

    REQUIRE(items.size() == 3);
    CHECK(items[0].m_cid == 1002);
    CHECK(items[1].m_cid == 1005);
    CHECK(items[2].m_cid == 1009);
}

// =====================================================================
// OPS4~OPS5 —— 按 (ctype,cid) 查找：ctype 必须参与比对
// =====================================================================

TEST_CASE("OPS4 查找命中返回该会话，未命中返回 NULL", "[convops]")
{
    std::vector<net::ConversationItem> items;
    items.push_back(MakeConv(kSingle, 1001, 0, 1000));
    items.push_back(MakeConv(kGroup, 700001, 0, 2000));

    const net::ConversationItem* pHit = FindConversation(items, kGroup, 700001);
    REQUIRE(pHit != NULL);
    CHECK(pHit->m_cid == 700001);
    CHECK(pHit->m_ctype == kGroup);

    CHECK(FindConversation(items, kSingle, 999999) == NULL);
}

TEST_CASE("OPS5 cid 相同但会话类型不同的两条会话互不混淆", "[convops]")
{
    //单聊 cid 是对端 userid、群聊 cid 是群 id，两个 id 空间理论上不重叠，但查找
    //口径必须带上 ctype——否则一旦 id 空间将来发生交叠就会串会话。
    std::vector<net::ConversationItem> items;
    items.push_back(MakeConv(kSingle, 5001, 0, 1000));
    items.push_back(MakeConv(kGroup, 5001, 0, 2000));

    const net::ConversationItem* pSingle = FindConversation(items, kSingle, 5001);
    const net::ConversationItem* pGroup  = FindConversation(items, kGroup, 5001);
    REQUIRE(pSingle != NULL);
    REQUIRE(pGroup != NULL);
    CHECK(pSingle->m_timestamp == 1000);
    CHECK(pGroup->m_timestamp == 2000);
}

// =====================================================================
// OPS6~OPS9 —— 删除单条 / 取标识快照（支撑"删除该会话 / 删除全部会话"）
// =====================================================================

TEST_CASE("OPS6 删除指定会话，其余会话不受影响", "[convops]")
{
    std::vector<net::ConversationItem> items;
    items.push_back(MakeConv(kSingle, 1001, 0, 1000));
    items.push_back(MakeConv(kSingle, 1002, 0, 2000));
    items.push_back(MakeConv(kGroup, 700001, 0, 3000));

    CHECK(RemoveConversation(items, kSingle, 1002) == 1);

    REQUIRE(items.size() == 2);
    CHECK(FindConversation(items, kSingle, 1002) == NULL);
    CHECK(FindConversation(items, kSingle, 1001) != NULL);
    CHECK(FindConversation(items, kGroup, 700001) != NULL);
}

TEST_CASE("OPS7 删除不存在的会话是安全的无操作", "[convops]")
{
    std::vector<net::ConversationItem> items;
    items.push_back(MakeConv(kSingle, 1001, 0, 1000));

    CHECK(RemoveConversation(items, kSingle, 999999) == 0);
    CHECK(items.size() == 1);

    //同 cid 但类型不同也不该被误删。
    CHECK(RemoveConversation(items, kGroup, 1001) == 0);
    CHECK(items.size() == 1);
}

TEST_CASE("OPS8 脏数据里的重复会话会被一并删掉", "[convops]")
{
    //(ctype,cid) 理论上唯一，真出现重复时若只删第一条，列表上会留下一条点不动、
    //删不掉的残留行，故删除按"删光所有匹配项"实现。
    std::vector<net::ConversationItem> items;
    items.push_back(MakeConv(kSingle, 1001, 0, 1000));
    items.push_back(MakeConv(kSingle, 1001, 0, 2000));
    items.push_back(MakeConv(kSingle, 1002, 0, 3000));

    CHECK(RemoveConversation(items, kSingle, 1001) == 2);
    REQUIRE(items.size() == 1);
    CHECK(items[0].m_cid == 1002);
}

TEST_CASE("OPS9 标识快照按列表顺序给出全部会话，空列表给空快照", "[convops]")
{
    std::vector<net::ConversationItem> items;
    items.push_back(MakeConv(kGroup, 700001, 0, 3000));
    items.push_back(MakeConv(kSingle, 1001, 0, 1000));

    std::vector<ConversationKey> keys = CollectConversationKeys(items);
    REQUIRE(keys.size() == 2);
    CHECK(keys[0].m_ctype == kGroup);
    CHECK(keys[0].m_cid == 700001);
    CHECK(keys[1].m_ctype == kSingle);
    CHECK(keys[1].m_cid == 1001);

    std::vector<net::ConversationItem> empty;
    CHECK(CollectConversationKeys(empty).empty());
}

// =====================================================================
// OPS10~OPS12 —— 改置顶：改完自动重排，无变化时返回 false
// =====================================================================

TEST_CASE("OPS10 置顶生效后该会话立刻排到最前", "[convops]")
{
    std::vector<net::ConversationItem> items;
    items.push_back(MakeConv(kSingle, 1001, 0, 3000));
    items.push_back(MakeConv(kSingle, 1002, 0, 2000));
    items.push_back(MakeConv(kSingle, 1003, 0, 1000));
    SortConversations(items);

    //最旧的那条被置顶后应排到最前，其余保持时间倒序。
    CHECK(SetConversationPinned(items, kSingle, 1003, true) == true);

    REQUIRE(items.size() == 3);
    CHECK(items[0].m_cid == 1003);
    CHECK(items[0].m_pinned == 1);
    CHECK(items[1].m_cid == 1001);
    CHECK(items[2].m_cid == 1002);
}

TEST_CASE("OPS11 取消置顶后按时间归位", "[convops]")
{
    std::vector<net::ConversationItem> items;
    items.push_back(MakeConv(kSingle, 1001, 1, 1000));
    items.push_back(MakeConv(kSingle, 1002, 0, 3000));
    SortConversations(items);
    REQUIRE(items[0].m_cid == 1001);

    CHECK(SetConversationPinned(items, kSingle, 1001, false) == true);

    CHECK(items[0].m_cid == 1002);
    CHECK(items[1].m_cid == 1001);
    CHECK(items[1].m_pinned == 0);
}

TEST_CASE("OPS12 置顶状态没变 / 会话不存在时返回 false", "[convops]")
{
    //返回值是调用方"要不要重绘列表"的依据，没变就不该谎报变化。
    std::vector<net::ConversationItem> items;
    items.push_back(MakeConv(kSingle, 1001, 1, 1000));

    CHECK(SetConversationPinned(items, kSingle, 1001, true) == false);
    CHECK(items[0].m_pinned == 1);
    CHECK(SetConversationPinned(items, kSingle, 999999, true) == false);
}

// =====================================================================
// OPS13~OPS15 —— 改免打扰：不影响排序，查询对未知会话按"不免打扰"兜底
// =====================================================================

TEST_CASE("OPS13 设置免打扰只改标志，不改列表顺序", "[convops]")
{
    std::vector<net::ConversationItem> items;
    items.push_back(MakeConv(kSingle, 1001, 0, 3000));
    items.push_back(MakeConv(kSingle, 1002, 0, 1000));
    SortConversations(items);

    CHECK(SetConversationMuted(items, kSingle, 1001, true) == true);

    CHECK(IsConversationMuted(items, kSingle, 1001) == true);
    //顺序不变：免打扰不参与排序。
    CHECK(items[0].m_cid == 1001);
    CHECK(items[1].m_cid == 1002);
}

TEST_CASE("OPS14 取消免打扰后查询回到 false", "[convops]")
{
    std::vector<net::ConversationItem> items;
    items.push_back(MakeConv(kSingle, 1001, 0, 1000));

    CHECK(SetConversationMuted(items, kSingle, 1001, true) == true);
    CHECK(SetConversationMuted(items, kSingle, 1001, true) == false);   // 已是该状态
    CHECK(SetConversationMuted(items, kSingle, 1001, false) == true);
    CHECK(IsConversationMuted(items, kSingle, 1001) == false);
}

TEST_CASE("OPS15 查询未知会话按不免打扰处理", "[convops]")
{
    //新消息可能来自一个本地还没有会话行的对端（会话是收到消息那一刻才建的）。
    //这种情况必须按"不免打扰"处理，否则提示音和托盘闪烁会被静默吞掉。
    std::vector<net::ConversationItem> items;
    CHECK(IsConversationMuted(items, kSingle, 1001) == false);
}

// =====================================================================
// RD1~RD7 —— 收到消息的当下"算不算用户已经看到"的判据
//
// 口径：窗口存在、句柄有效、可见、未最小化、且是当前前台窗口，五项全中才算
// 已经看到。窗口被别的窗口盖住或用户正在别处忙时照常亮红点，等用户切回该窗口
// 再清。单聊与群聊共用这一个函数 —— 签名里没有 ctype，同口径由此在类型层面
// 就得到保证，不会出现两边各写一套判据的情况。
// =====================================================================

TEST_CASE("RD1 窗口存在、有效、可见、未最小化且在前台时算已读", "[convunread]")
{
    CHECK(ShouldTreatAsReadOnArrival(true, true, true, false, true) == true);
}

TEST_CASE("RD2 聊天窗根本没打开时不算已读", "[convunread]")
{
    CHECK(ShouldTreatAsReadOnArrival(false, false, false, false, false) == false);
}

TEST_CASE("RD3 窗口对象还在但句柄已失效时不算已读", "[convunread]")
{
    //窗口关掉之后 m_chatWindows 里的指针可能还没回收，此时 ::IsWindow 为假。
    CHECK(ShouldTreatAsReadOnArrival(true, false, true, false, true) == false);
}

TEST_CASE("RD4 窗口不可见时不算已读", "[convunread]")
{
    CHECK(ShouldTreatAsReadOnArrival(true, true, false, false, true) == false);
}

TEST_CASE("RD5 窗口最小化时不算已读", "[convunread]")
{
    //钉死一个 Win32 陷阱：最小化的窗口 ::IsWindowVisible 仍返回 TRUE，只判可见
    //会把最小化误判成"用户正看着"。故意把 isVisible 传 true 来复现这个场景。
    CHECK(ShouldTreatAsReadOnArrival(true, true, true, true, true) == false);
}

TEST_CASE("RD6 窗口开着但不在前台时不算已读", "[convunread]")
{
    CHECK(ShouldTreatAsReadOnArrival(true, true, true, false, false) == false);
}

TEST_CASE("RD7 五个条件的全部组合里只有一种算已读", "[convunread]")
{
    //防止将来有人把 && 写成 ||、或把某一项的正负号弄反：穷举 32 种组合，逐个
    //比对，并断言判为已读的组合有且只有一种。
    const int kCombinationCount = 32;   // 五个布尔量共 2^5 种组合
    int trueCount = 0;
    for (int mask = 0; mask < kCombinationCount; ++mask)
    {
        const bool windowExists = (mask & 1) != 0;
        const bool isWindow     = (mask & 2) != 0;
        const bool isVisible    = (mask & 4) != 0;
        const bool isIconic     = (mask & 8) != 0;
        const bool isForeground = (mask & 16) != 0;

        const bool got = ShouldTreatAsReadOnArrival(windowExists, isWindow, isVisible,
                                                    isIconic, isForeground);
        const bool expected =
            windowExists && isWindow && isVisible && !isIconic && isForeground;
        CHECK(got == expected);
        if (got)
        {
            ++trueCount;
        }
    }
    CHECK(trueCount == 1);
}

// =====================================================================
// RD8~RD11 —— 收到消息之后未读数该变成多少
// =====================================================================

TEST_CASE("RD8 窗口在前台时连同旧未读一起清零", "[convunread]")
{
    //本次修复的核心：光"这条不累加"是不够的。该会话此前可能已经攒了 3 条未读，
    //用户在前台看到新消息时那 3 条也该一并清掉，否则红点照样亮着。
    CHECK(NextUnreadCount(3, true, true) == 0);
}

TEST_CASE("RD9 窗口不在前台时未读加一", "[convunread]")
{
    CHECK(NextUnreadCount(3, true, false) == 4);
}

TEST_CASE("RD10 自己在别端发出、同步回来的消息不动未读", "[convunread]")
{
    //countAsUnread 为 false（见 ConversationsProtocolClient::shouldCountAsUnread），
    //这类推送既不加也不清，维持原值。
    CHECK(NextUnreadCount(3, false, false) == 3);
}

TEST_CASE("RD11 未读数为负的脏数据按零起算", "[convunread]")
{
    CHECK(NextUnreadCount(-5, false, false) == 0);
    CHECK(NextUnreadCount(-5, true, false) == 1);
    CHECK(NextUnreadCount(-5, true, true) == 0);
}
