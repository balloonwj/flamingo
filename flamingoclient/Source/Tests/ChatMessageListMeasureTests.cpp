/**
 *  ChatMessageListMeasureTests.cpp
 *
 *  聊天窗 resize 流畅度专项测试：量化消息区在窗口缩放时对每条气泡的
 *  "重量级折行测量"次数，坐实"每步缩放重复测量"的诊断，并作为缓存
 *  修复的回归断言。
 *
 *  背景：拖动聊天窗改变大小时，DuiHost::OnSize 会整树 SetRect 一次、
 *  窗口的 OnRelayout 又 ForceLayout 两次，每一遍都让 ChatMessageList::
 *  Layout 对每条气泡调用一次 ChatBubble::Measure。而文字气泡的 Measure
 *  代价不低（GetDC + 建字体 + DrawText 折行）。本测试通过 ChatBubble 的
 *  HeavyMeasureCount 探针，断言：同宽重排应复用缓存（0 次重测）、改宽 /
 *  改内容 / 进出多选才重测。
 *
 *  用 Catch2 v3（amalgamated 形态）。纯内存：不创建窗口、不要 HWND，
 *  ChatBubble::Measure 用屏幕 DC（GetDC(NULL)）即可测量。全局 _Module
 *  由 BalloonChatLayoutTests.cpp 统一定义，本文件只引用。
 *
 *  覆盖用例（与需求确认一致）：
 *    D1 首次布局必量          D2 同宽复用（缓存核心）
 *    D3 改宽必重测            D4 内容变更仅失效该条
 *    D5 进 / 出多选必重测      D6 非文字不入重量级路径
 *    D7 一步 resize = 三遍布局：缓存后从 3N 降到 N
 *
 *  balloonwj@qq.com   2026-06-03
 */

#include "stdafx.h"

#include "catch_amalgamated.hpp"

#include "../BalloonChatWnd/ChatBubble.h"
#include "../BalloonChatWnd/ChatMessageList.h"
#include "../BalloonChatWnd/ChatMockData.h"

using namespace balloonwjui;
using namespace balloonchatwnd;

namespace {

// 测试用消息区宽度（逻辑像素）；高度对折行测量无影响，给一个足够大的值。
const int kWidthA = 400;
const int kWidthB = 520;
const int kHeight = 600;

// 造一个宽 w、高 kHeight 的矩形（左上角原点）。
RECT MakeRect(int w)
{
    RECT r = { 0, 0, w, kHeight };
    return r;
}

// 往列表追加 n 条文字消息（自己 / 对方交替）。文字带中文长串以确保会
// 真正触发折行测量。
void AppendTextMessages(ChatMessageList& list, int n)
{
    for (int i = 0; i < n; ++i)
    {
        CString t;
        t.Format(_T("这是第 %d 条用于折行测量的较长测试消息内容内容内容"), i);
        list.AppendMessage(MakeTextMessage((i % 2) == 0, t));
    }
}

// 造一条指定 kind 的非文字消息（补齐该 kind 必要字段）。
ChatMessage MakeKindMessage(int kind)
{
    ChatMessage m = MakeTextMessage(false, _T("占位"));
    m.kind = kind;
    if (kind == kMsgEmoji)
    {
        m.text = _T("emoji");
    }
    else if (kind == kMsgImage)
    {
        m.imageW = 120;
        m.imageH = 90;
        m.imageCaption = _T("图片");
    }
    else if (kind == kMsgFile)
    {
        m.fileName = _T("design.fig");
        m.fileType = _T("fig");
        m.fileSize = _T("4.2 MB");
        m.fileProgress = 100;
    }
    else if (kind == kMsgTime)
    {
        m.text = _T("昨天 10:00");
    }
    return m;
}

// 取列表第 idx 个子控件并转成 ChatBubble*（断言非空）。
ChatBubble* BubbleAt(ChatMessageList& list, size_t idx)
{
    const std::vector<std::unique_ptr<DuiControl> >& kids = list.Children();
    REQUIRE(idx < kids.size());
    ChatBubble* b = dynamic_cast<ChatBubble*>(kids[idx].get());
    REQUIRE(b != NULL);
    return b;
}

} // anonymous namespace

// =====================================================================
// D1 首次布局：每条文字气泡都要测量一次
// =====================================================================

TEST_CASE("D1 首次布局对每条文字气泡测量一次", "[resize][measure]")
{
    const int n = 12;
    ChatMessageList list;
    AppendTextMessages(list, n);

    // 追加过程在初始空矩形上跑过布局，属搭建噪声 —— 清零后再以真实宽度
    // 排一次，统计这次的测量数。宽度由 0 变到 kWidthA，必然全部重测。
    ChatBubble::ResetHeavyMeasureCount();
    list.ForceLayout(MakeRect(kWidthA));

    CHECK(ChatBubble::HeavyMeasureCount() == (unsigned long)n);
}

// =====================================================================
// D2 同宽重排：应复用缓存，零次重量级测量（缓存修复核心）
//     改前：当前实现每次都重测 → N（本用例会红，正是诊断的实证）。
//     改后：宽度未变命中缓存 → 0。
// =====================================================================

TEST_CASE("D2 同宽重排复用缓存不再重测", "[resize][measure]")
{
    const int n = 12;
    ChatMessageList list;
    AppendTextMessages(list, n);

    list.ForceLayout(MakeRect(kWidthA));   // 首次以 kWidthA 测量

    ChatBubble::ResetHeavyMeasureCount();
    list.ForceLayout(MakeRect(kWidthA));   // 同宽再排一次

    CHECK(ChatBubble::HeavyMeasureCount() == 0ul);
}

// =====================================================================
// D3 改宽重排：宽度变了必须重新折行测量全部
// =====================================================================

TEST_CASE("D3 改宽重排重新测量全部", "[resize][measure]")
{
    const int n = 12;
    ChatMessageList list;
    AppendTextMessages(list, n);

    list.ForceLayout(MakeRect(kWidthA));   // 首次以 kWidthA 测量

    ChatBubble::ResetHeavyMeasureCount();
    list.ForceLayout(MakeRect(kWidthB));   // 改成 kWidthB

    CHECK(ChatBubble::HeavyMeasureCount() == (unsigned long)n);
}

// =====================================================================
// D4 内容变更：只失效被改的那一条，其余仍命中缓存
//     改前：每次重测全部 → N（本用例会红）。改后：仅 1。
// =====================================================================

TEST_CASE("D4 内容变更仅该条失效", "[resize][measure]")
{
    const int n = 12;
    ChatMessageList list;
    AppendTextMessages(list, n);

    list.ForceLayout(MakeRect(kWidthA));   // 首次测量，全部进缓存

    // 改第 0 条的内容 —— SetMessage 内部把该条折行缓存置为未测量。
    BubbleAt(list, 0)->SetMessage(MakeTextMessage(true, _T("改后的新内容，同样需要折行测量一下")));

    ChatBubble::ResetHeavyMeasureCount();
    list.ForceLayout(MakeRect(kWidthA));   // 同宽重排

    CHECK(ChatBubble::HeavyMeasureCount() == 1ul);
}

// =====================================================================
// D5 进 / 出多选：多选改变气泡可用宽度，缓存键须含多选状态，必须重测
//     （正确性坑 —— 不能因为宽度没变就吃过期高度）
// =====================================================================

TEST_CASE("D5 进出多选模式都重新测量", "[resize][measure]")
{
    const int n = 12;
    ChatMessageList list;
    AppendTextMessages(list, n);

    list.ForceLayout(MakeRect(kWidthA));   // 非多选下首次测量

    // 进入多选：SetMultiSelectMode 内部会 ForceLayout 一次（同宽）。
    ChatBubble::ResetHeavyMeasureCount();
    list.SetMultiSelectMode(true);
    CHECK(ChatBubble::HeavyMeasureCount() == (unsigned long)n);

    // 退出多选：同宽，但多选状态又变了，仍须全部重测。
    ChatBubble::ResetHeavyMeasureCount();
    list.SetMultiSelectMode(false);
    CHECK(ChatBubble::HeavyMeasureCount() == (unsigned long)n);
}

// =====================================================================
// D6 非文字气泡（emoji / 图片 / 文件 / 时间分隔）行高是定值，
//     Measure 不进重量级折行路径
// =====================================================================

TEST_CASE("D6 非文字气泡不触发重量级测量", "[resize][measure]")
{
    ChatMessageList list;
    list.AppendMessage(MakeKindMessage(kMsgEmoji));
    list.AppendMessage(MakeKindMessage(kMsgImage));
    list.AppendMessage(MakeKindMessage(kMsgFile));
    list.AppendMessage(MakeKindMessage(kMsgTime));

    ChatBubble::ResetHeavyMeasureCount();
    list.ForceLayout(MakeRect(kWidthA));
    CHECK(ChatBubble::HeavyMeasureCount() == 0ul);

    // 再改宽重排，依然不应有任何重量级测量。
    list.ForceLayout(MakeRect(kWidthB));
    CHECK(ChatBubble::HeavyMeasureCount() == 0ul);
}

// =====================================================================
// D7 一步 resize = 三遍布局（DuiHost::OnSize 的 SetRect + OnRelayout 的
//     两次 ForceLayout）。模拟同宽三连排：
//       改前：3 × N 次重量级测量。
//       改后：第一遍 N，后两遍命中缓存 0 → 合计 N。
// =====================================================================

TEST_CASE("D7 一步缩放三遍同宽布局只测量一轮", "[resize][measure]")
{
    const int n = 12;
    ChatMessageList list;
    AppendTextMessages(list, n);

    list.ForceLayout(MakeRect(kWidthA));   // 先在 kWidthA 安顿好

    // 模拟拖动到 kWidthB 的"一步"：尺寸变化后整树一次 + OnRelayout 两次，
    // 三次都在同一个新宽度 kWidthB 上重排。
    ChatBubble::ResetHeavyMeasureCount();
    list.ForceLayout(MakeRect(kWidthB));
    list.ForceLayout(MakeRect(kWidthB));
    list.ForceLayout(MakeRect(kWidthB));

    CHECK(ChatBubble::HeavyMeasureCount() == (unsigned long)n);
}
