/**
 *  会话列表行位置同步的单元测试（Suite CRS），ConversationRowSyncTests.cpp
 *
 *  覆盖 balloonmain::FindConversationMoveTarget。每个用例都在真实的 DuiTreeView 上按它
 *  算出的结果调用 MoveRoot，再比对树的行顺序与排好序的会话列表是否一致：
 *    CRS1  最后一个会话收到新消息 —— 挪到最前，顺序一致；
 *    CRS2  有置顶会话时非置顶会话收到消息 —— 落在置顶区之后，置顶区不变；
 *    CRS3  新会话：追加一行后调整位置 —— 位置正确，行数加 1；
 *    CRS4  会话不在列表里 / 它的行不存在 / 它后面那一项的行不存在 —— 返回 false，树不变；
 *    CRS5  300 个会话上随机 200 次操作 —— 每次之后顺序一致，已有节点 id 不变。
 *
 *  框架 Catch2 v3。全局 _Module 由 BalloonChatLayoutTests.cpp 统一定义。
 *
 *  balloonwj@qq.com   2026-10-01
 */
#include "stdafx.h"

#include "catch_amalgamated.hpp"

#include <random>
#include <vector>

#include "../../../third-party/balloonui/balloonui/Controls/List/DuiTreeView.h"
#include "../UI/BalloonMain/ConversationListOps.h"
#include "../UI/BalloonMain/ConversationRowSync.h"

using balloonmain::ConvNodeKey;
using balloonmain::FindConversationMoveTarget;

namespace {

typedef std::map<ConvNodeKey, int> NodeMap;

// 基准时间戳（Unix 毫秒）与相邻会话的时间间隔。
const int64_t kBaseTimestamp = 1700000000000LL;
const int64_t kGapMs         = 60000;

// 造一个会话。
net::ConversationItem MakeConv(int ctype, int64_t cid, int64_t timestamp, int pinned)
{
    net::ConversationItem c;
    c.m_ctype     = ctype;
    c.m_cid       = cid;
    c.m_timestamp = timestamp;
    c.m_pinned    = pinned;
    return c;
}

// 会话的文字标识，写进行文字，用来核对节点没有张冠李戴。
CString LabelOf(const net::ConversationItem& c)
{
    CString s;
    s.Format(_T("%d:%lld"), c.m_ctype, (long long)c.m_cid);
    return s;
}

// 按已排序的会话列表建树，并登记会话 → 节点。
void BuildTree(balloonwjui::DuiTreeView& tv, const std::vector<net::ConversationItem>& sorted,
               NodeMap& nodeOf)
{
    for (size_t i = 0; i < sorted.size(); ++i)
    {
        const int node = tv.AddRoot(LabelOf(sorted[i]));
        nodeOf[ConvNodeKey(sorted[i].m_ctype, sorted[i].m_cid)] = node;
    }
}

// 树的行顺序是否与排好序的会话列表一致，且每行文字对得上。
bool TreeMatchesSorted(const balloonwjui::DuiTreeView& tv,
                       const std::vector<net::ConversationItem>& sorted, const NodeMap& nodeOf)
{
    if (tv.GetVisibleCount() != (int)sorted.size())
    {
        return false;
    }
    for (size_t i = 0; i < sorted.size(); ++i)
    {
        NodeMap::const_iterator it = nodeOf.find(ConvNodeKey(sorted[i].m_ctype, sorted[i].m_cid));
        if (it == nodeOf.end() || tv.GetIdAtVisibleRow((int)i) != it->second)
        {
            return false;
        }
        if (tv.GetItemLabel(it->second) != LabelOf(sorted[i]))
        {
            return false;
        }
    }
    return true;
}

// 按 FindConversationMoveTarget 的结果挪动一行（主面板里就是这么用的）。
bool Place(balloonwjui::DuiTreeView& tv, const std::vector<net::ConversationItem>& sorted,
           const NodeMap& nodeOf, int ctype, int64_t cid)
{
    int node = -1;
    int before = -1;
    if (!FindConversationMoveTarget(sorted, nodeOf, ctype, cid, node, before))
    {
        return false;
    }
    return tv.MoveRoot(node, before);
}

// 取树当前的可见行 id 序列，用于核对"树没有被动过"。
std::vector<int> RowIds(const balloonwjui::DuiTreeView& tv)
{
    std::vector<int> ids;
    for (int i = 0; i < tv.GetVisibleCount(); ++i)
    {
        ids.push_back(tv.GetIdAtVisibleRow(i));
    }
    return ids;
}

// 造 n 个单聊会话，时间从新到旧排列。
std::vector<net::ConversationItem> MakeSortedConvs(int n)
{
    std::vector<net::ConversationItem> convs;
    for (int i = 0; i < n; ++i)
    {
        convs.push_back(MakeConv(1, 1000 + i, kBaseTimestamp - i * kGapMs, 0));
    }
    balloonmain::SortConversations(convs);
    return convs;
}

} // namespace

TEST_CASE("CRS1 最后一个会话收到新消息:挪到最前,顺序一致", "[convrowsync]")
{
    std::vector<net::ConversationItem> convs = MakeSortedConvs(10);
    balloonwjui::DuiTreeView tv;
    NodeMap nodeOf;
    BuildTree(tv, convs, nodeOf);

    const int64_t lastCid = convs.back().m_cid;
    const int nodeBefore = nodeOf[ConvNodeKey(1, lastCid)];
    convs.back().m_timestamp = kBaseTimestamp + kGapMs;   // 比所有会话都新
    balloonmain::SortConversations(convs);

    REQUIRE(Place(tv, convs, nodeOf, 1, lastCid));
    CHECK(TreeMatchesSorted(tv, convs, nodeOf));
    CHECK(tv.GetIdAtVisibleRow(0) == nodeBefore);   // 节点 id 不变，只是位置变了
}

TEST_CASE("CRS2 有置顶会话时非置顶会话收到消息:落在置顶区之后,置顶区不变", "[convrowsync]")
{
    std::vector<net::ConversationItem> convs = MakeSortedConvs(10);
    for (int i = 0; i < 3; ++i)
    {
        convs[(size_t)i].m_pinned = 1;   // 前三个置顶
    }
    balloonmain::SortConversations(convs);
    balloonwjui::DuiTreeView tv;
    NodeMap nodeOf;
    BuildTree(tv, convs, nodeOf);
    const std::vector<int> rowsBefore = RowIds(tv);
    const std::vector<int> pinnedRows(rowsBefore.begin(), rowsBefore.begin() + 3);

    const int64_t cid = convs.back().m_cid;
    convs.back().m_timestamp = kBaseTimestamp + kGapMs;
    balloonmain::SortConversations(convs);

    REQUIRE(Place(tv, convs, nodeOf, 1, cid));
    CHECK(TreeMatchesSorted(tv, convs, nodeOf));
    CHECK(tv.GetIdAtVisibleRow(3) == nodeOf[ConvNodeKey(1, cid)]);
    const std::vector<int> rows = RowIds(tv);
    CHECK(std::vector<int>(rows.begin(), rows.begin() + 3) == pinnedRows);
}

TEST_CASE("CRS3 新会话:追加一行后调整位置,行数加 1", "[convrowsync]")
{
    std::vector<net::ConversationItem> convs = MakeSortedConvs(10);
    balloonwjui::DuiTreeView tv;
    NodeMap nodeOf;
    BuildTree(tv, convs, nodeOf);

    // 一个时间落在中间的新群会话。
    const net::ConversationItem fresh = MakeConv(2, 900001, kBaseTimestamp - 4 * kGapMs + 1, 0);
    convs.push_back(fresh);
    balloonmain::SortConversations(convs);
    nodeOf[ConvNodeKey(2, 900001)] = tv.AddRoot(LabelOf(fresh));   // 先追加到末尾

    REQUIRE(Place(tv, convs, nodeOf, 2, 900001));
    CHECK(tv.GetVisibleCount() == 11);
    CHECK(TreeMatchesSorted(tv, convs, nodeOf));
}

TEST_CASE("CRS4 缺会话或缺行时返回 false,树不变", "[convrowsync]")
{
    std::vector<net::ConversationItem> convs = MakeSortedConvs(5);
    balloonwjui::DuiTreeView tv;
    NodeMap nodeOf;
    BuildTree(tv, convs, nodeOf);
    const std::vector<int> before = RowIds(tv);
    int node = 0;
    int target = 0;

    // 会话不在列表里。
    CHECK_FALSE(FindConversationMoveTarget(convs, nodeOf, 1, 424242, node, target));

    // 它自己的行不存在。
    NodeMap missingSelf = nodeOf;
    missingSelf.erase(ConvNodeKey(1, convs[2].m_cid));
    CHECK_FALSE(FindConversationMoveTarget(convs, missingSelf, 1, convs[2].m_cid, node, target));

    // 排在它后面那一项的行不存在。
    NodeMap missingNext = nodeOf;
    missingNext.erase(ConvNodeKey(1, convs[3].m_cid));
    CHECK_FALSE(FindConversationMoveTarget(convs, missingNext, 1, convs[2].m_cid, node, target));

    CHECK(RowIds(tv) == before);

    // 排在最后的会话：目标为 -1（移到末尾），且返回 true。
    REQUIRE(FindConversationMoveTarget(convs, nodeOf, 1, convs.back().m_cid, node, target));
    CHECK(node == nodeOf[ConvNodeKey(1, convs.back().m_cid)]);
    CHECK(target == -1);
}

TEST_CASE("CRS5 300 个会话随机 200 次操作:顺序始终一致,节点 id 不变", "[convrowsync]")
{
    const int kConvs = 300;
    const int kOps   = 200;
    std::mt19937 rng(20261001);
    std::vector<net::ConversationItem> convs = MakeSortedConvs(kConvs);
    balloonwjui::DuiTreeView tv;
    NodeMap nodeOf;
    BuildTree(tv, convs, nodeOf);
    const NodeMap originalNodes = nodeOf;

    int64_t now = kBaseTimestamp;
    int64_t nextCid = 5000;
    for (int op = 0; op < kOps; ++op)
    {
        const int roll = (int)(rng() % 10);
        int ctype = 1;
        int64_t cid = 0;
        if (roll < 6)
        {
            //某个会话收到新消息
            net::ConversationItem& c = convs[rng() % convs.size()];
            now += kGapMs;
            c.m_timestamp = now;
            ctype = c.m_ctype;
            cid = c.m_cid;
            balloonmain::SortConversations(convs);
        }
        else if (roll < 8)
        {
            //置顶或取消置顶（SetConversationPinned 内部会重新排序）
            const net::ConversationItem& c = convs[rng() % convs.size()];
            ctype = c.m_ctype;
            cid = c.m_cid;
            balloonmain::SetConversationPinned(convs, ctype, cid, c.m_pinned == 0);
        }
        else
        {
            //新会话
            now += kGapMs;
            const net::ConversationItem fresh = MakeConv(2, nextCid++, now, 0);
            ctype = fresh.m_ctype;
            cid = fresh.m_cid;
            convs.push_back(fresh);
            balloonmain::SortConversations(convs);
            nodeOf[ConvNodeKey(ctype, cid)] = tv.AddRoot(LabelOf(fresh));
        }

        REQUIRE(Place(tv, convs, nodeOf, ctype, cid));
        REQUIRE(TreeMatchesSorted(tv, convs, nodeOf));
    }

    // 一开始就有的会话，节点 id 始终没变。
    for (NodeMap::const_iterator it = originalNodes.begin(); it != originalNodes.end(); ++it)
    {
        CHECK(nodeOf[it->first] == it->second);
    }
}
