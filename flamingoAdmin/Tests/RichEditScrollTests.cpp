/**
 *  flamingoAdmin —— 公告正文滚动机制集成测试。
 *
 *  与其它"无 host 裸构造"用例不同,本文件需要【真 RICHEDIT50W 子窗口】才能
 *  验证滚动行为,故创建一个隐藏宿主窗口作 parent,EnsureCreated 出真子窗口、
 *  灌入长文本、按固定高度布局后,断言:
 *    用例 1:DuiRichEditHost 滚动 API —— 长文本溢出固定高度(contentH>viewH)、
 *            SetVScrollPos 能把内容向下滚动、再滚回顶部。
 *    用例 2:DuiRichEditHost::OnMouseWheel —— 多行且溢出时,滚轮真的滚动内容、
 *            返回 true(消费),并触发 VScrollChangedFn 回调(供悬浮条同步)。
 *    用例 3:OnMouseWheel 在"内容放得下"时返回 false(不消费),不误吞滚轮。
 *
 *  说明:现网"滚轮无效"的真因是 DUI 宿主(DuiHost::OnMouseWheel)按鼠标位置把
 *  滚轮派发给最顶层命中的控件且不冒泡 —— 正文这类寄宿 HWND 命中的就是它自己,
 *  而 DuiRichEditHost 原先没重写 OnMouseWheel,故滚轮被基类默认丢弃。本文件即
 *  验证"DuiRichEditHost 自己处理滚轮"这一修复的真实行为。
 *
 *  balloonwj@qq.com   2026-06-01
 */
#include "stdafx.h"
#include "Tests/TestFramework.h"

#include "Controls/Input/DuiRichEditHost.h"
#include "Controls/Input/DuiEditHost.h"
#include "Controls/Layout/DuiLayout.h"

namespace flamingoadmin { namespace test {

namespace {

// 创建一个隐藏宿主窗口,供 RichEdit 子窗口作 parent。不显示也能完成尺寸 /
// 换行 / 滚动状态计算。返回 HWND(失败返回 nullptr)。
HWND CreateHiddenHost()
{
    return ::CreateWindowEx(0, _T("STATIC"), _T(""), WS_POPUP,
                            0, 0, 600, 700, nullptr, nullptr,
                            ::GetModuleHandle(nullptr), nullptr);
}

// 抽干消息队列,让 RichEdit 同步处理 WM_SIZE 等(SetWindowPos 触发的重排)。
void Pump()
{
    MSG msg;
    while (::PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE))
    {
        ::TranslateMessage(&msg);
        ::DispatchMessage(&msg);
    }
}

// 把一段 40 行的长文本灌进 RichEdit,确保在固定高度里必然溢出。
void FillLongText(balloonwjui::DuiRichEditHost& re)
{
    CString body;
    for (int i = 1; i <= 40; ++i)
    {
        CString line;
        line.Format(_T("第 %d 行\r\n"), i);
        body += line;
    }
    re.SetText(body);
}

// VScrollChangedFn 测试回调:每次被调累加计数,供断言"滚动是否通知到外部"。
int g_scrollCbCount = 0;
void TestScrollCb(void* /*ctx*/)
{
    ++g_scrollCbCount;
}

// 读 DuiEditHost 子窗口当前行数(EM_GETLINECOUNT;含自动换行后的行)。无 HWND
// 返回 0。
int EditLineCount(const balloonwjui::DuiEditHost& e)
{
    HWND h = e.GetHostedHwnd();
    if (h == nullptr)
    {
        return 0;
    }
    return static_cast<int>(::SendMessage(h, EM_GETLINECOUNT, 0, 0));
}

// 一行不含换行符的长文本(40 个汉字),在窄框里若开启自动换行必然折成多行。
LPCTSTR kLongOneLine =
    _T("这是一行很长的中文文本没有任何换行符用于检验多行输入框是否会按控件宽度自动折行显示");

} // anonymous namespace

void RunAllRichEditScrollTests()
{
    // ---- 用例 1:DuiRichEditHost 滚动 API —— 长文本溢出 + SetVScrollPos 生效 ----
    BeginCase("RichEditScroll.LongText_OverflowsAndScrolls:长文本溢出固定高度且可滚动");
    {
        HWND host = CreateHiddenHost();
        FA_EXPECT_NOTNULL(host);
        if (host != nullptr)
        {
            balloonwjui::DuiRichEditHost re;
            re.SetMultiLine(true);
            re.SetWordWrap(true);
            re.SetReadOnly(true);
            re.SetShowVScroll(false);
            FA_EXPECT_TRUE(re.EnsureCreated(host));

            FillLongText(re);
            re.ForceLayout(RECT{ 0, 0, 300, 220 });   // 220 高,40 行必然溢出
            Pump();

            int cH = 0;
            int sP = 0;
            int vH = 0;
            re.GetVScrollMetrics(cH, sP, vH);
            FA_EXPECT_TRUE(vH > 0);          // 可视高有效
            FA_EXPECT_TRUE(cH > vH);         // 内容高 > 可视高 → 溢出
            FA_EXPECT_EQ(sP, 0);             // 初始在顶部

            re.SetVScrollPos(10000);         // 请求滚到很下面(会被夹取到底)
            Pump();
            int cH2 = 0;
            int sP2 = 0;
            int vH2 = 0;
            re.GetVScrollMetrics(cH2, sP2, vH2);
            FA_EXPECT_TRUE(sP2 > 0);         // 已向下滚动

            re.SetVScrollPos(0);             // 滚回顶部
            Pump();
            int sP3 = 0;
            re.GetVScrollMetrics(cH2, sP3, vH2);
            FA_EXPECT_EQ(sP3, 0);
        }
        if (host != nullptr)
        {
            ::DestroyWindow(host);
        }
    }

    // ---- 用例 2:OnMouseWheel —— 多行溢出时滚轮滚动内容 + 回调通知 ----
    BeginCase("RichEditScroll.OnMouseWheel_ScrollsAndNotifies:滚轮滚动正文并回调通知");
    {
        HWND host = CreateHiddenHost();
        FA_EXPECT_NOTNULL(host);
        if (host != nullptr)
        {
            balloonwjui::DuiRichEditHost re;
            re.SetMultiLine(true);
            re.SetWordWrap(true);
            re.SetReadOnly(true);
            re.SetShowVScroll(false);
            re.EnsureCreated(host);

            g_scrollCbCount = 0;
            re.SetOnVScrollChanged(&TestScrollCb, nullptr);

            FillLongText(re);
            re.ForceLayout(RECT{ 0, 0, 300, 220 });
            Pump();

            int cH = 0;
            int sP = 0;
            int vH = 0;
            re.GetVScrollMetrics(cH, sP, vH);
            FA_EXPECT_TRUE(cH > vH);         // 溢出
            FA_EXPECT_EQ(sP, 0);             // 初始顶部

            POINT pt = { 10, 10 };
            // 向下滚一格(zDelta 取 -WHEEL_DELTA)。
            bool handled = re.OnMouseWheel(pt, static_cast<short>(-WHEEL_DELTA), 0);
            Pump();
            FA_EXPECT_TRUE(handled);         // 多行 + 溢出 → 消费

            int sP2 = 0;
            re.GetVScrollMetrics(cH, sP2, vH);
            FA_EXPECT_TRUE(sP2 > 0);         // 内容真的向下滚动
            FA_EXPECT_TRUE(g_scrollCbCount > 0);  // 回调被触发(供悬浮条同步)
        }
        if (host != nullptr)
        {
            ::DestroyWindow(host);
        }
    }

    // ---- 用例 4:子窗口直接收到 WM_MOUSEWHEEL 也能滚动(复现现网场景)----
    //   Win10 默认"悬停滚动非活动窗口",滚轮直达正文子窗口而非 DUI 宿主,
    //   故必须由子类过程在子窗口层主动滚动。本用例直接向子 HWND 发
    //   WM_MOUSEWHEEL,断言内容滚动且回调触发。
    BeginCase("RichEditScroll.ChildHwndWheel_Scrolls:子窗口直收滚轮也能滚动并回调");
    {
        HWND host = CreateHiddenHost();
        FA_EXPECT_NOTNULL(host);
        if (host != nullptr)
        {
            balloonwjui::DuiRichEditHost re;
            re.SetMultiLine(true);
            re.SetWordWrap(true);
            re.SetReadOnly(true);
            re.SetShowVScroll(false);
            re.EnsureCreated(host);

            g_scrollCbCount = 0;
            re.SetOnVScrollChanged(&TestScrollCb, nullptr);

            FillLongText(re);
            re.ForceLayout(RECT{ 0, 0, 300, 220 });
            Pump();

            HWND child = re.GetHostedHwnd();
            FA_EXPECT_NOTNULL(child);

            int cH = 0;
            int sP0 = 0;
            int vH = 0;
            re.GetVScrollMetrics(cH, sP0, vH);
            FA_EXPECT_EQ(sP0, 0);

            // 直接向子窗口投递"向下滚一格"的滚轮消息(模拟 OS 直送子窗口)。
            if (child != nullptr)
            {
                ::SendMessage(child, WM_MOUSEWHEEL,
                              MAKEWPARAM(0, static_cast<WORD>(-WHEEL_DELTA)), 0);
            }
            Pump();

            int sP1 = 0;
            re.GetVScrollMetrics(cH, sP1, vH);
            FA_EXPECT_TRUE(sP1 > 0);              // 内容已向下滚动
            FA_EXPECT_TRUE(g_scrollCbCount > 0);  // 滚动变化已回调通知
        }
        if (host != nullptr)
        {
            ::DestroyWindow(host);
        }
    }

    // ---- 用例 3:OnMouseWheel —— 内容放得下时不消费滚轮 ----
    BeginCase("RichEditScroll.OnMouseWheel_ShortText_NotConsumed:短文本不溢出则不消费滚轮");
    {
        HWND host = CreateHiddenHost();
        FA_EXPECT_NOTNULL(host);
        if (host != nullptr)
        {
            balloonwjui::DuiRichEditHost re;
            re.SetMultiLine(true);
            re.SetWordWrap(true);
            re.SetReadOnly(true);
            re.SetShowVScroll(false);
            re.EnsureCreated(host);

            re.SetText(_T("只有一行,放得下。"));
            re.ForceLayout(RECT{ 0, 0, 300, 220 });
            Pump();

            POINT pt = { 10, 10 };
            bool handled = re.OnMouseWheel(pt, static_cast<short>(-WHEEL_DELTA), 0);
            FA_EXPECT_FALSE(handled);        // 放得下 → 不消费,交宿主另作处理
        }
        if (host != nullptr)
        {
            ::DestroyWindow(host);
        }
    }

    // ---- 用例 5:DuiEditHost 多行 + 开自动换行 → 长行折成多行 ----
    //   编辑公告对话框正文用的是可编辑 DuiEditHost。多行 EDIT 默认带
    //   ES_AUTOHSCROLL → 横向滚动不换行;SetWordWrap(true) 去掉该位后应按
    //   控件宽度自动折行。本用例在窄框里灌一行长文本,断言折成多行。
    BeginCase("EditWrap.MultiLine_WordWrapOn_WrapsLongLine:多行开自动换行后长行折成多行");
    {
        HWND host = CreateHiddenHost();
        FA_EXPECT_NOTNULL(host);
        if (host != nullptr)
        {
            balloonwjui::DuiEditHost e;
            e.SetMultiLine(true);
            e.SetWordWrap(true);
            FA_EXPECT_TRUE(e.EnsureCreated(host));
            e.SetText(kLongOneLine);
            e.ForceLayout(RECT{ 0, 0, 160, 120 });   // 窄框,一行长文本必然折行
            Pump();
            FA_EXPECT_TRUE(EditLineCount(e) > 1);     // 已自动换行
        }
        if (host != nullptr)
        {
            ::DestroyWindow(host);
        }
    }

    // ---- 用例 6:DuiEditHost 多行 + 不开自动换行(默认)→ 长行仍单行 ----
    //   验证 SetWordWrap 默认 false 时保持历史行为(ES_AUTOHSCROLL,不折行),
    //   即本次 balloonui 改动对未开启换行的多行输入框零回归。
    BeginCase("EditWrap.MultiLine_WordWrapOff_StaysOneLine:多行默认不换行长行仍单行");
    {
        HWND host = CreateHiddenHost();
        FA_EXPECT_NOTNULL(host);
        if (host != nullptr)
        {
            balloonwjui::DuiEditHost e;
            e.SetMultiLine(true);
            // 不调 SetWordWrap → 默认 false
            FA_EXPECT_TRUE(e.EnsureCreated(host));
            e.SetText(kLongOneLine);
            e.ForceLayout(RECT{ 0, 0, 160, 120 });
            Pump();
            FA_EXPECT_EQ(EditLineCount(e), 1);        // 不折行,仍 1 行
        }
        if (host != nullptr)
        {
            ::DestroyWindow(host);
        }
    }
}

}} // namespace flamingoadmin::test
