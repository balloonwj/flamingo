/**
 *  ChatMessageDeleteTests.cpp
 *
 *  聊天列表"按 msgid 移除消息行"的测试（Suite CD，标签 [msgdelete]）。
 *
 *  背景（2026-09-30）：删除消息改为本人视角的真删除，服务端删除成功后向本人全部设备推送
 *  cmd_messages_deleted_notify，已打开的聊天窗据此调用 ChatMessageList::RemoveMessagesByMsgid
 *  移除对应的气泡（见 docs/清理聊天记录方案.md §8）。
 *
 *    CD1  只移除命中的行，其余保持原顺序；返回实际移除条数，列表里没有的 msgid 不影响。
 *    CD2  移除后孤立的时间分隔行一并移除；撤回提示行带着原 msgid，不算孤立分隔行。
 *    CD3  移除后的排版与"只放剩余消息的列表直接排版"完全一致，证明重排过、没有留下空洞。
 *
 *  聊天窗「删除」改为真删除（§10）：删除请求交给服务端，气泡等删除推送到达后再移除。
 *
 *    CD4  单条删除，msgid 为 0 的行（发送中 / 发送失败）：照旧直接从列表移除。
 *    CD5  单条删除，有 msgid 的行：不立即移除，等删除推送到达。
 *    CD6  多选删除，选中行混有两种：msgid 为 0 的立即移除，有 msgid 的留着等推送并交给宿主，
 *         随后退出多选。
 *    CD7  多选操作条上的点击由列表自己处理，不落到操作条下面的消息行（联调时发现的问题）。
 *
 *  用 Catch2 v3（amalgamated 形态）。纯内存：不创建窗口、不要 HWND。列表不在窗口里时
 *  "投给宿主"这一步会跳过，所以 CD6 通过 DeleteSelected 的返回值核对交给宿主的 msgid。
 *  全局 _Module 由 BalloonChatLayoutTests.cpp 统一定义，本文件只引用。
 *
 *  balloonwj@qq.com   2026-09-30
 */
#include "stdafx.h"
#include "catch_amalgamated.hpp"

#include <vector>

#include "../UI/BalloonChatWnd/ChatBubble.h"
#include "../UI/BalloonChatWnd/ChatMessageList.h"
#include "../UI/BalloonChatWnd/ChatMockData.h"

using namespace balloonwjui;
using namespace balloonchatwnd;

namespace {

// 测试用消息区宽度 / 高度（逻辑像素），足够放下本组用例里的全部消息行。
const int kListWidth  = 400;
const int kListHeight = 800;

// 消息时间轴的起点（Unix 毫秒）与间隔。一分钟远小于插分隔行的阈值（5 分钟），
// 一小时远大于它，用来控制两条消息之间插不插时间分隔行。
const long long kT0       = 1700000000000LL;
const long long kMinuteMs = 60LL * 1000;
const long long kHourMs   = 60LL * kMinuteMs;

// 列表里一行的摘要：是不是纯粹的时间分隔行（kMsgTime 且没有 msgid）、以及 msgid。
struct RowInfo
{
    bool      m_isSeparator;   // true = 自动插入的时间分隔行
    long long m_msgid;         // 消息行的服务器 msgid；分隔行为 0
};

// 造一条对方发来的文字消息。
//   msgid：服务器 msgid；timestampMs：消息时间；text：正文（决定气泡高度）。
ChatMessage MakeMsg(long long msgid, long long timestampMs, LPCTSTR text)
{
    ChatMessage m = MakeTextMessage(false, text);
    m.msgid = msgid;
    m.timestampMs = timestampMs;
    return m;
}

// 按测试宽度排版。
void LayoutList(ChatMessageList& list)
{
    RECT rc = { 0, 0, kListWidth, kListHeight };
    list.ForceLayout(rc);
}

// 取出列表当前每一行的摘要，按显示顺序排列。
std::vector<RowInfo> Rows(ChatMessageList& list)
{
    std::vector<RowInfo> rows;
    const std::vector<std::unique_ptr<DuiControl> >& kids = list.Children();
    for (size_t i = 0; i < kids.size(); ++i)
    {
        ChatBubble* b = dynamic_cast<ChatBubble*>(kids[i].get());
        REQUIRE(b != NULL);
        RowInfo r;
        r.m_msgid = b->Msgid();
        r.m_isSeparator = (b->GetMessage().kind == kMsgTime && r.m_msgid == 0);
        rows.push_back(r);
    }
    return rows;
}

// 断言某一行是分隔行或是指定 msgid 的消息行。
void CheckRow(const std::vector<RowInfo>& rows, size_t idx, bool isSeparator, long long msgid)
{
    REQUIRE(idx < rows.size());
    INFO("row #" << idx);
    CHECK(rows[idx].m_isSeparator == isSeparator);
    CHECK(rows[idx].m_msgid == msgid);
}

} // anonymous namespace

TEST_CASE("CD1 按 msgid 移除：只移除命中的行，其余保持原顺序", "[msgdelete]")
{
    // 四条消息间隔一分钟：只在最前面插一行时间分隔
    ChatMessageList list;
    for (long long i = 1; i <= 4; ++i)
    {
        list.AppendMessage(MakeMsg(i, kT0 + i * kMinuteMs, _T("消息")));
    }
    LayoutList(list);
    REQUIRE(Rows(list).size() == 5);

    // 99 不在列表里，0 是非法 msgid：都不影响结果，也不计入
    std::vector<long long> ids;
    ids.push_back(2);
    ids.push_back(99);
    ids.push_back(0);
    CHECK(list.RemoveMessagesByMsgid(ids) == 1);

    const std::vector<RowInfo> rows = Rows(list);
    REQUIRE(rows.size() == 4);
    CheckRow(rows, 0, true, 0);
    CheckRow(rows, 1, false, 1);
    CheckRow(rows, 2, false, 3);
    CheckRow(rows, 3, false, 4);
}

TEST_CASE("CD2 移除后孤立的时间分隔行一并移除，撤回提示行不受影响", "[msgdelete]")
{
    // 1 与 2 之间隔一小时，插两行分隔：[分隔, 1, 分隔, 2, 3]
    const long long t1 = kT0;
    const long long t2 = kT0 + kHourMs;
    const long long t3 = t2 + kMinuteMs;

    // 删掉 1：第一行分隔下面紧跟另一行分隔，成了孤立的，一并移除
    {
        ChatMessageList list;
        list.AppendMessage(MakeMsg(1, t1, _T("一")));
        list.AppendMessage(MakeMsg(2, t2, _T("二")));
        list.AppendMessage(MakeMsg(3, t3, _T("三")));
        LayoutList(list);
        REQUIRE(Rows(list).size() == 5);

        std::vector<long long> ids;
        ids.push_back(1);
        CHECK(list.RemoveMessagesByMsgid(ids) == 1);

        const std::vector<RowInfo> rows = Rows(list);
        REQUIRE(rows.size() == 3);
        CheckRow(rows, 0, true, 0);
        CheckRow(rows, 1, false, 2);
        CheckRow(rows, 2, false, 3);
    }

    // 删掉 2、3：第二行分隔下面已到列表末尾，一并移除
    {
        ChatMessageList list;
        list.AppendMessage(MakeMsg(1, t1, _T("一")));
        list.AppendMessage(MakeMsg(2, t2, _T("二")));
        list.AppendMessage(MakeMsg(3, t3, _T("三")));
        LayoutList(list);

        std::vector<long long> ids;
        ids.push_back(2);
        ids.push_back(3);
        CHECK(list.RemoveMessagesByMsgid(ids) == 2);

        const std::vector<RowInfo> rows = Rows(list);
        REQUIRE(rows.size() == 2);
        CheckRow(rows, 0, true, 0);
        CheckRow(rows, 1, false, 1);
    }

    // 撤回提示行：同样是居中样式，但带着原消息的 msgid，属于内容。
    // [分隔, 1, 撤回提示(2)] 删掉 1 之后，分隔行下面还有撤回提示，不算孤立
    {
        ChatMessageList list;
        list.AppendMessage(MakeMsg(1, t1, _T("一")));
        list.AppendMessage(MakeMsg(2, t1 + kMinuteMs, _T("二")));
        LayoutList(list);
        REQUIRE(list.RecallMessageByMsgid(2, _T("对方撤回了一条消息")));

        std::vector<long long> ids;
        ids.push_back(1);
        CHECK(list.RemoveMessagesByMsgid(ids) == 1);

        const std::vector<RowInfo> rows = Rows(list);
        REQUIRE(rows.size() == 2);
        CheckRow(rows, 0, true, 0);
        CheckRow(rows, 1, false, 2);
    }
}

TEST_CASE("CD3 移除后的排版与只放剩余消息的列表直接排版一致", "[msgdelete]")
{
    // 正文长短不一，各行高度不同：若移除后没有重排，下方各行的位置必然对不上
    LPCTSTR const texts[] =
    {
        _T("短"),
        _T("这是一条比较长的消息，用来把气泡撑高一点，好让位置差异更明显。"),
        _T("中等长度的一条消息"),
        _T("又一条比较长的消息，内容足够长，会折成两行甚至三行显示在气泡里。")
    };

    ChatMessageList removed;
    for (int i = 0; i < 4; ++i)
    {
        removed.AppendMessage(MakeMsg(i + 1, kT0 + i * kMinuteMs, texts[i]));
    }
    LayoutList(removed);

    std::vector<long long> ids;
    ids.push_back(2);
    ids.push_back(4);
    REQUIRE(removed.RemoveMessagesByMsgid(ids) == 2);

    // 对照：只放 1、3 两条，时间与正文都相同
    ChatMessageList direct;
    direct.AppendMessage(MakeMsg(1, kT0, texts[0]));
    direct.AppendMessage(MakeMsg(3, kT0 + 2 * kMinuteMs, texts[2]));
    LayoutList(direct);

    const std::vector<std::unique_ptr<DuiControl> >& a = removed.Children();
    const std::vector<std::unique_ptr<DuiControl> >& b = direct.Children();
    REQUIRE(a.size() == b.size());
    for (size_t i = 0; i < a.size(); ++i)
    {
        const RECT ra = a[i]->GetRect();
        const RECT rb = b[i]->GetRect();
        INFO("row #" << i << ": removed=(" << ra.left << "," << ra.top << "," << ra.right << ","
             << ra.bottom << ") direct=(" << rb.left << "," << rb.top << "," << rb.right << ","
             << rb.bottom << ")");
        CHECK(ra.left == rb.left);
        CHECK(ra.top == rb.top);
        CHECK(ra.right == rb.right);
        CHECK(ra.bottom == rb.bottom);
    }
}

TEST_CASE("CD4 单条删除：msgid 为 0 的行照旧直接从列表移除", "[msgdelete]")
{
    // 第 2 条是发送中 / 发送失败的行，服务器还没给它分配 msgid
    ChatMessageList list;
    list.AppendMessage(MakeMsg(1, kT0 + kMinuteMs, _T("一")));
    list.AppendMessage(MakeMsg(0, kT0 + 2 * kMinuteMs, _T("发送失败的消息")));
    list.AppendMessage(MakeMsg(3, kT0 + 3 * kMinuteMs, _T("三")));
    LayoutList(list);
    REQUIRE(Rows(list).size() == 4);

    // 找到那一行（下标 2：分隔、1、本行、3）
    ChatBubble* target = dynamic_cast<ChatBubble*>(list.Children()[2].get());
    REQUIRE(target != NULL);
    REQUIRE(target->Msgid() == 0);

    // 返回 true 表示已就地移除，target 已销毁
    CHECK(list.RequestDeleteBubble(target));

    const std::vector<RowInfo> rows = Rows(list);
    REQUIRE(rows.size() == 3);
    CheckRow(rows, 0, true, 0);
    CheckRow(rows, 1, false, 1);
    CheckRow(rows, 2, false, 3);
}

TEST_CASE("CD5 单条删除：有 msgid 的行不立即移除，等删除推送到达", "[msgdelete]")
{
    ChatMessageList list;
    list.AppendMessage(MakeMsg(1, kT0 + kMinuteMs, _T("一")));
    list.AppendMessage(MakeMsg(2, kT0 + 2 * kMinuteMs, _T("二")));
    LayoutList(list);
    REQUIRE(Rows(list).size() == 3);

    ChatBubble* target = dynamic_cast<ChatBubble*>(list.Children()[2].get());
    REQUIRE(target != NULL);
    REQUIRE(target->Msgid() == 2);

    // 返回 false 表示只转交了宿主，target 仍然有效、仍在列表里
    CHECK_FALSE(list.RequestDeleteBubble(target));
    REQUIRE(Rows(list).size() == 3);
    CHECK(target->Msgid() == 2);

    // 删除推送到达后才移除
    std::vector<long long> ids;
    ids.push_back(2);
    CHECK(list.RemoveMessagesByMsgid(ids) == 1);

    const std::vector<RowInfo> rows = Rows(list);
    REQUIRE(rows.size() == 2);
    CheckRow(rows, 0, true, 0);
    CheckRow(rows, 1, false, 1);
}

TEST_CASE("CD6 多选删除：msgid 为 0 的行立即移除，有 msgid 的行留着等推送，退出多选", "[msgdelete]")
{
    // [分隔, 1, 0(失败), 3, 0(发送中), 5]
    ChatMessageList list;
    list.AppendMessage(MakeMsg(1, kT0 + kMinuteMs, _T("一")));
    list.AppendMessage(MakeMsg(0, kT0 + 2 * kMinuteMs, _T("发送失败的消息")));
    list.AppendMessage(MakeMsg(3, kT0 + 3 * kMinuteMs, _T("三")));
    list.AppendMessage(MakeMsg(0, kT0 + 4 * kMinuteMs, _T("发送中的消息")));
    list.AppendMessage(MakeMsg(5, kT0 + 5 * kMinuteMs, _T("五")));
    LayoutList(list);
    REQUIRE(Rows(list).size() == 6);

    // 选中 1、失败那行、3、发送中那行；5 不选
    const std::vector<std::unique_ptr<DuiControl> >& kids = list.Children();
    ChatBubble* first = dynamic_cast<ChatBubble*>(kids[1].get());
    REQUIRE(first != NULL);
    list.EnterMultiSelectWith(first);
    for (size_t i = 2; i <= 4; ++i)
    {
        ChatBubble* b = dynamic_cast<ChatBubble*>(kids[i].get());
        REQUIRE(b != NULL);
        b->SetSelected(true);
    }
    REQUIRE(list.IsMultiSelectMode());

    // 交给宿主的是有 msgid 的两条，按列表顺序
    const std::vector<long long> posted = list.DeleteSelected();
    REQUIRE(posted.size() == 2);
    CHECK(posted[0] == 1);
    CHECK(posted[1] == 3);

    // msgid 为 0 的两行已移除，有 msgid 的全部留着
    const std::vector<RowInfo> rows = Rows(list);
    REQUIRE(rows.size() == 4);
    CheckRow(rows, 0, true, 0);
    CheckRow(rows, 1, false, 1);
    CheckRow(rows, 2, false, 3);
    CheckRow(rows, 3, false, 5);

    // 已退出多选，留下的行不再是选中态
    CHECK_FALSE(list.IsMultiSelectMode());
    for (size_t i = 0; i < list.Children().size(); ++i)
    {
        ChatBubble* b = dynamic_cast<ChatBubble*>(list.Children()[i].get());
        REQUIRE(b != NULL);
        CHECK_FALSE(b->IsSelected());
    }

    // 未处于多选时再调用什么都不做
    CHECK(list.DeleteSelected().empty());
    CHECK(Rows(list).size() == 4);
}

TEST_CASE("CD7 多选操作条上的点击由列表自己处理，不落到操作条下面的消息行", "[msgdelete]")
{
    // 2026-10-01 联调发现：滚到底部后进入多选，可视区底部让出 48 像素给操作条，最后几行
    // 消息的矩形伸进了操作条下面。基类命中测试先问子控件，于是点「取消 / 转发 / 删除」
    // 都被下面的消息行接收，表现为点了操作条却切换了某条消息的选中态。
    const int listHeight = 300;
    ChatMessageList list;
    for (long long i = 1; i <= 12; ++i)
    {
        list.AppendMessage(MakeMsg(i, kT0 + i * kMinuteMs,
                                   _T("一条足够长的消息，让整个列表的内容高度超出可视区，需要滚动才能看全。")));
    }
    RECT rc = { 0, 0, kListWidth, listHeight };
    list.ForceLayout(rc);
    list.ScrollToBottom();
    list.ForceLayout(rc);

    ChatBubble* first = dynamic_cast<ChatBubble*>(list.Children()[1].get());
    REQUIRE(first != NULL);
    list.EnterMultiSelectWith(first);
    REQUIRE(list.IsMultiSelectMode());

    // 操作条占据列表底部 48 像素（kMultiSelectBarH）；取操作条内左、中、右三处落点
    const int barMidY = listHeight - 24;
    const POINT inBar[] = { { 30, barMidY }, { kListWidth / 2, barMidY }, { kListWidth - 30, barMidY } };
    for (size_t i = 0; i < sizeof(inBar) / sizeof(inBar[0]); ++i)
    {
        INFO("point (" << inBar[i].x << ", " << inBar[i].y << ")");

        // 前提：这个落点确实压着某一行消息，否则本用例测不出问题
        bool coveredByRow = false;
        for (size_t k = 0; k < list.Children().size(); ++k)
        {
            const RECT r = list.Children()[k]->GetRect();
            if (::PtInRect(&r, inBar[i]))
            {
                coveredByRow = true;
            }
        }
        REQUIRE(coveredByRow);

        CHECK(list.HitTest(inBar[i]) == &list);
    }

    // 操作条上方的消息区照常命中消息行
    const POINT inRows = { kListWidth / 2, listHeight - 48 - 20 };
    CHECK(dynamic_cast<ChatBubble*>(list.HitTest(inRows)) != NULL);
}
