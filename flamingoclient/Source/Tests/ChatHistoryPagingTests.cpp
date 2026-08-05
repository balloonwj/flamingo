/**
 *  ChatHistoryPagingTests.cpp
 *
 *  聊天窗「向上翻看更早消息」的分页判定（BalloonChatWnd/ChatHistoryPaging.h）测试。
 *
 *  这块逻辑的风险不在算法复杂，而在<u>触发时机</u>：多触发一次就会重复插入、顺序错乱；
 *  少触发一次用户就翻不动。故用例逐条钉死五个否决条件，以及两种应答（首屏 / 翻页）
 *  对状态的不同更新方式。
 *
 *  用 Catch2 v3（amalgamated），纯内存断言，不创建窗口、不发网络请求。
 *  全局 _Module 由 BalloonChatLayoutTests.cpp 统一定义，本文件只引用。
 *
 *  balloonwj@qq.com   2026-08-03
 */

#include "stdafx.h"

#include "catch_amalgamated.hpp"

#include "../BalloonChatWnd/ChatHistoryPaging.h"

using namespace balloonchatwnd;

namespace {

// 一个"典型可翻页现场"：首屏已到位、还有更早的、没有请求在途。
// 各用例在它基础上只改自己关心的那一项，别的保持正常，避免断言失败时
// 分不清是被哪个条件否决的。
HistoryPagingState ReadyState()
{
    HistoryPagingState st;
    st.m_oldestMsgid = 1000;   // 首屏已到位，锚点有效
    st.m_hasMore     = true;   // 服务端说还有更早的
    st.m_loading     = false;  // 没有请求在途
    return st;
}

// 一个"内容远超一屏、已滚到顶部"的视口现场。
const int kViewH    = 600;
const int kContentH = 5000;
const int kAtTop    = 0;

} // anonymous namespace

// =====================================================================
// P1-P5：五个否决条件
// =====================================================================

TEST_CASE("P1 滚到顶部且还有更早的消息，就该去拉下一页", "[paging]")
{
    CHECK(ShouldLoadOlderHistory(ReadyState(), kAtTop, kViewH, kContentH));
}

TEST_CASE("P2 已有请求在途时不再重复发", "[paging]")
{
    // 请求与应答之间隔着一次网络往返，其间用户很可能还在顶部继续滚 ——
    // 少了这道闸门会连发好几页，重复插入且顺序错乱。
    HistoryPagingState st = ReadyState();
    st.m_loading = true;

    CHECK_FALSE(ShouldLoadOlderHistory(st, kAtTop, kViewH, kContentH));
}

TEST_CASE("P3 服务端已说没有更早的了就不再发", "[paging]")
{
    HistoryPagingState st = ReadyState();
    st.m_hasMore = false;

    CHECK_FALSE(ShouldLoadOlderHistory(st, kAtTop, kViewH, kContentH));
}

TEST_CASE("P4 首屏还没到位时不发", "[paging]")
{
    // 锚点为 0 时若照发，before=0 的含义是"从最新开始"，服务端会把最新一页
    // 当成"更早的"返回，直接插到头部造成列表重影。
    HistoryPagingState st = ReadyState();
    st.m_oldestMsgid = 0;

    CHECK_FALSE(ShouldLoadOlderHistory(st, kAtTop, kViewH, kContentH));
}

TEST_CASE("P5 内容不足一屏时不发", "[paging]")
{
    // 连滚动条都没有，谈不上"往上翻"。此时 scrollY 恒为 0，不挡这一道的话
    // 消息很少的会话一进窗口就会无条件拉一页。
    HistoryPagingState st = ReadyState();

    CHECK_FALSE(ShouldLoadOlderHistory(st, kAtTop, kViewH, /*contentH*/ 300));
    // 恰好等于一屏也算不足（没有可滚动的余量）。
    CHECK_FALSE(ShouldLoadOlderHistory(st, kAtTop, kViewH, kViewH));
}

TEST_CASE("P6 还没滚到顶部阈值内就不发", "[paging]")
{
    HistoryPagingState st = ReadyState();

    // 远离顶部：不发。
    CHECK_FALSE(ShouldLoadOlderHistory(st, 2000, kViewH, kContentH));
    // 阈值之内：发。
    CHECK(ShouldLoadOlderHistory(st, kHistoryLoadTriggerY, kViewH, kContentH));
    // 刚过阈值一像素：不发。
    CHECK_FALSE(ShouldLoadOlderHistory(st, kHistoryLoadTriggerY + 1, kViewH, kContentH));
}

// =====================================================================
// P7-P10：应答到达后的状态更新
// =====================================================================

TEST_CASE("P7 首屏到达后记下锚点并解除在途", "[paging]")
{
    HistoryPagingState st;
    st.m_loading = true;

    OnFirstPageLoaded(st, /*oldestMsgid*/ 900, /*hasMore*/ true);

    CHECK(st.m_oldestMsgid == 900);
    CHECK(st.m_hasMore);
    CHECK_FALSE(st.m_loading);
    // 首屏一到位就该能翻了。
    CHECK(ShouldLoadOlderHistory(st, kAtTop, kViewH, kContentH));
}

TEST_CASE("P8 翻页到达后锚点前移", "[paging]")
{
    HistoryPagingState st = ReadyState();
    st.m_loading = true;

    OnOlderPageLoaded(st, /*oldestMsgid*/ 800, /*hasMore*/ true);

    CHECK(st.m_oldestMsgid == 800);   // 锚点往更早移动
    CHECK(st.m_hasMore);
    CHECK_FALSE(st.m_loading);
}

TEST_CASE("P9 翻页返回空页时锚点保持不变", "[paging]")
{
    // 这是个容易写错的边界：空页若把锚点清成 0，下一次翻页的 before 就变成
    // "从最新开始"，等于把最新一页当成更早的插到头部。
    HistoryPagingState st = ReadyState();
    st.m_loading = true;
    const long long anchorBefore = st.m_oldestMsgid;

    OnOlderPageLoaded(st, /*oldestMsgid*/ 0, /*hasMore*/ false);

    CHECK(st.m_oldestMsgid == anchorBefore);
    CHECK_FALSE(st.m_hasMore);
    CHECK_FALSE(st.m_loading);
}

TEST_CASE("P10 翻到会话开头后不再触发", "[paging]")
{
    HistoryPagingState st = ReadyState();
    st.m_loading = true;

    OnOlderPageLoaded(st, /*oldestMsgid*/ 700, /*hasMore*/ false);

    // 到底了：无论怎么滚都不该再发请求。
    CHECK_FALSE(ShouldLoadOlderHistory(st, kAtTop, kViewH, kContentH));
    CHECK_FALSE(ShouldLoadOlderHistory(st, 0, kViewH, 100000));
}

TEST_CASE("P11 新建状态默认不可翻页", "[paging]")
{
    // 窗口刚打开、切换会话后都是这个状态：什么都没加载，不该主动发请求。
    HistoryPagingState st;

    CHECK(st.m_oldestMsgid == 0);
    CHECK_FALSE(st.m_hasMore);
    CHECK_FALSE(st.m_loading);
    CHECK_FALSE(ShouldLoadOlderHistory(st, kAtTop, kViewH, kContentH));
}
