/**
 *  PresenceTreeMarksTests.cpp
 *
 *  主面板树上在线状态标记写入（balloonmain::ApplyUserPresenceMarks /
 *  ApplyConversationPresenceMarks）的测试，即方案中的 AG5（离线用户头像置灰，2026-10-03）：
 *    AG5a  同一个人的两个节点（部门树与「我的收藏」分区）都被写入在线状态与灰彩，别人不受影响；
 *    AG5b  对方上线后再写一次，灰彩取消；
 *    AG5c  在线状态传 kPresenceMarkKeep 时只改灰彩、不动在线状态角标（本人连接态变化走这条路）；
 *    AG5d  消息列表只处理与该用户的单聊行，会话 id 数值相同的群会话行不受影响；
 *    AG5e  树为 NULL 或映射里没有该用户时返回 0。
 *  2026-10-07 在线状态从行右端的状态徽标改为头像右下角的角标（docs/消息提醒模式与消息盒子方案.md 第 13 节），
 *  本组改为核对 MainPanelTreeView::GetItemPresence，并新增：
 *    AG5f  在线状态推送依次改为离开、忙碌、在线：同一行的在线状态随之就地更新，任何时候都不设基类的行末状态徽标。
 *
 *  在真实的 MainPanelTreeView 上调用，不创建窗口。框架 Catch2 v3；全局 _Module 由
 *  BalloonChatLayoutTests.cpp 统一定义。
 *
 *  balloonwj@qq.com   2026-10-03
 */
#include "stdafx.h"

#include "catch_amalgamated.hpp"

#include <map>
#include <utility>

#include "../UI/BalloonMain/MainPanelTreeView.h"
#include "../UI/BalloonMain/PresenceTreeMarks.h"
#include "../net/Protocol.h"   // presence_type

using balloonmain::ApplyUserPresenceMarks;
using balloonmain::ApplyConversationPresenceMarks;
using balloonmain::MainPanelTreeView;

namespace {

// 用例里用到的几个用户 id。
const UINT kUserA = 1001;
const UINT kUserB = 1002;

// 消息列表映射里的会话类型：1 单聊、2 群（与 net/Protocol.h 的 conversation_type 相同）。
const int kConvSingle = 1;
const int kConvGroup  = 2;

} // anonymous namespace

TEST_CASE("AG5a 同一个人的两个节点都写入在线状态与灰彩，别人不受影响", "[offlinegray][presencemarks]")
{
    MainPanelTreeView tv;
    const int deptRoot = tv.AddRoot(_T("部门"));
    const int favRoot  = tv.AddRoot(_T("我的收藏"));
    const int aInDept  = tv.AddChild(deptRoot, _T("A"));
    const int bInDept  = tv.AddChild(deptRoot, _T("B"));
    const int aInFav   = tv.AddChild(favRoot, _T("A"));

    std::map<int, UINT> nodeUserIds;
    nodeUserIds[aInDept] = kUserA;
    nodeUserIds[bInDept] = kUserB;
    nodeUserIds[aInFav]  = kUserA;

    CHECK(ApplyUserPresenceMarks(&tv, nodeUserIds, kUserA, presence_offline, true) == 2);

    CHECK(tv.IsItemIconGrayed(aInDept));
    CHECK(tv.IsItemIconGrayed(aInFav));
    CHECK(tv.GetItemPresence(aInDept) == presence_offline);
    CHECK(tv.GetItemPresence(aInFav) == presence_offline);
    // 行右端不再挂状态徽标
    CHECK(tv.GetItemStatusIcon(aInDept) == NULL);
    CHECK(tv.GetItemStatusIcon(aInFav) == NULL);

    // B 与两个分组根节点都不受影响。
    CHECK_FALSE(tv.IsItemIconGrayed(bInDept));
    CHECK(tv.GetItemPresence(bInDept) == MainPanelTreeView::kNoPresence);
    CHECK_FALSE(tv.IsItemIconGrayed(deptRoot));
    CHECK_FALSE(tv.IsItemIconGrayed(favRoot));
}

TEST_CASE("AG5b 对方上线后再写一次，灰彩取消", "[offlinegray][presencemarks]")
{
    MainPanelTreeView tv;
    const int root = tv.AddRoot(_T("部门"));
    const int a    = tv.AddChild(root, _T("A"));
    std::map<int, UINT> nodeUserIds;
    nodeUserIds[a] = kUserA;

    REQUIRE(ApplyUserPresenceMarks(&tv, nodeUserIds, kUserA, presence_offline, true) == 1);
    REQUIRE(tv.IsItemIconGrayed(a));

    CHECK(ApplyUserPresenceMarks(&tv, nodeUserIds, kUserA, presence_online, false) == 1);
    CHECK_FALSE(tv.IsItemIconGrayed(a));
    CHECK(tv.GetItemPresence(a) == presence_online);
}

TEST_CASE("AG5c 在线状态传 kPresenceMarkKeep 时只改灰彩、不动在线状态角标", "[offlinegray][presencemarks]")
{
    MainPanelTreeView tv;
    const int root = tv.AddRoot(_T("部门"));
    const int self = tv.AddChild(root, _T("我"));
    std::map<int, UINT> nodeUserIds;
    nodeUserIds[self] = kUserA;

    tv.SetItemPresence(self, presence_busy);

    // 本端掉线：只把头像置灰，在线状态保持原样。
    CHECK(ApplyUserPresenceMarks(&tv, nodeUserIds, kUserA, balloonmain::kPresenceMarkKeep, true) == 1);
    CHECK(tv.IsItemIconGrayed(self));
    CHECK(tv.GetItemPresence(self) == presence_busy);

    // 重新连上：恢复彩色，在线状态仍不变。
    CHECK(ApplyUserPresenceMarks(&tv, nodeUserIds, kUserA, balloonmain::kPresenceMarkKeep, false) == 1);
    CHECK_FALSE(tv.IsItemIconGrayed(self));
    CHECK(tv.GetItemPresence(self) == presence_busy);
}

TEST_CASE("AG5d 消息列表只处理与该用户的单聊行，同数值的群会话行不受影响", "[offlinegray][presencemarks]")
{
    MainPanelTreeView tv;
    const int singleA = tv.AddRoot(_T("A"));
    const int singleB = tv.AddRoot(_T("B"));
    const int groupSameId = tv.AddRoot(_T("群"));

    std::map<int, std::pair<int, int64_t> > nodeConv;
    nodeConv[singleA]     = std::make_pair(kConvSingle, (int64_t)kUserA);
    nodeConv[singleB]     = std::make_pair(kConvSingle, (int64_t)kUserB);
    nodeConv[groupSameId] = std::make_pair(kConvGroup, (int64_t)kUserA);   // 群号恰好与 A 的 userid 相同

    CHECK(ApplyConversationPresenceMarks(&tv, nodeConv, (int64_t)kUserA, presence_offline, true) == 1);
    CHECK(tv.IsItemIconGrayed(singleA));
    CHECK(tv.GetItemPresence(singleA) == presence_offline);
    CHECK(tv.GetItemStatusIcon(singleA) == NULL);

    CHECK_FALSE(tv.IsItemIconGrayed(singleB));
    CHECK_FALSE(tv.IsItemIconGrayed(groupSameId));
    CHECK(tv.GetItemPresence(groupSameId) == MainPanelTreeView::kNoPresence);
}

TEST_CASE("AG5e 树为 NULL 或映射里没有该用户时返回 0", "[offlinegray][presencemarks]")
{
    std::map<int, UINT> nodeUserIds;
    std::map<int, std::pair<int, int64_t> > nodeConv;
    CHECK(ApplyUserPresenceMarks(NULL, nodeUserIds, kUserA, presence_online, true) == 0);
    CHECK(ApplyConversationPresenceMarks(NULL, nodeConv, (int64_t)kUserA, presence_online, true) == 0);

    MainPanelTreeView tv;
    const int b = tv.AddRoot(_T("B"));
    nodeUserIds[b] = kUserB;
    nodeConv[b] = std::make_pair(kConvSingle, (int64_t)kUserB);
    CHECK(ApplyUserPresenceMarks(&tv, nodeUserIds, kUserA, presence_online, true) == 0);
    CHECK(ApplyConversationPresenceMarks(&tv, nodeConv, (int64_t)kUserA, presence_online, true) == 0);
    CHECK_FALSE(tv.IsItemIconGrayed(b));
    CHECK(tv.GetItemPresence(b) == MainPanelTreeView::kNoPresence);
}

TEST_CASE("AG5f 在线状态推送依次改为离开、忙碌、在线：同一行就地更新，不设行末状态徽标", "[offlinegray][presencemarks]")
{
    MainPanelTreeView tv;
    const int single = tv.AddRoot(_T("A"));
    std::map<int, std::pair<int, int64_t> > nodeConv;
    nodeConv[single] = std::make_pair(kConvSingle, (int64_t)kUserA);

    const int sequence[] = { presence_away, presence_busy, presence_online, presence_offline };
    const bool grayFor[] = { false, false, false, true };
    for (int i = 0; i < 4; ++i)
    {
        INFO("step " << i);
        CHECK(ApplyConversationPresenceMarks(&tv, nodeConv, (int64_t)kUserA, sequence[i], grayFor[i]) == 1);
        CHECK(tv.GetItemPresence(single) == sequence[i]);
        CHECK(tv.IsItemIconGrayed(single) == grayFor[i]);
        CHECK(tv.GetItemStatusIcon(single) == NULL);
    }

    // 整表重建前清空行装饰时，在线状态的记录一并清掉
    tv.ClearAllRowDecorations();
    CHECK(tv.GetItemPresence(single) == MainPanelTreeView::kNoPresence);
}
