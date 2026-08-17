/**
 *  flamingoAdmin —— 公告正文滚动机制集成测试。
 *
 *  公告详情面板的标题 / 正文用的是无窗口富文本控件 DuiRichEdit。本文件验证
 *  正文区赖以工作的三条滚动行为:
 *    用例 1:滚动接口 —— 长文本溢出固定高度(内容高 > 可视高)、SetVScrollPos
 *            能把内容向下滚动、再滚回顶部。
 *    用例 2:滚轮 —— 内容放得下时不消费(返回 false),不误吞滚轮。
 *
 *  第二条是有来历的:DUI 宿主按鼠标位置把滚轮派发给命中的控件,控件不处理才
 *  往上冒泡。正文若把滚轮一律吞掉,鼠标停在正文上时预览区的整页滚动就再也
 *  收不到滚轮了 —— 不报错、不崩溃,只是滚不动,很难查。2026-08-16 迁移到
 *  无窗口控件时,这条正是靠这类用例发现新控件把滚轮吞掉了。
 *
 *  「滚轮确实滚动了内容」这一半没有自动化:滚轮的命中判定与窗口在屏幕上的
 *  实际位置有关,而单测里没有真正显示出来的窗口。那一半靠人工核对。
 *
 *  与本文件的上一版(针对寄宿真子窗口的旧控件)相比,整套脚手架都不再需要:
 *  无窗口控件不依赖任何窗口,不必创建隐藏宿主窗口、不必 EnsureCreated、
 *  也不必抽干消息队列等子窗口处理 WM_SIZE。构造完直接布局、灌文本即可断言。
 *
 *  balloonwj@qq.com   2026-06-01
 */
#include "stdafx.h"
#include "Tests/TestFramework.h"

#include "Controls/Input/DuiRichEdit.h"
#include "Controls/Layout/DuiLayout.h"

namespace flamingoadmin { namespace test {

namespace {

// 正文框在用例里的固定尺寸(像素)。高度取 220,配合下面 40 行的长文本必然溢出。
const int kBodyW = 300;
const int kBodyH = 220;

// 造一个与公告正文同款的只读富文本控件:多行、自动换行、只读、无光标。
// 与 AnnouncementPreviewPanel::MakeReadOnlyRichEdit 的关键设置保持一致 ——
// 用例要验的是那个面板里的正文行为,配置对不上就测不到真实情况。
void SetUpBodyEdit(balloonwjui::DuiRichEdit& re)
{
    re.SetMultiLine(true);
    re.SetWordWrap(true);
    re.SetReadOnly(true);
    re.SetShowCaret(false);
    // 高度由外层布局决定,不随内容长高 —— 否则控件会自己长到装得下全部内容,
    // 就永远不会溢出,滚动也就无从谈起,用例会失去意义。
    re.SetAutoGrow(false);
    re.ForceLayout(RECT{ 0, 0, kBodyW, kBodyH });
}

// 把一段 40 行的长文本灌进控件,确保在固定高度里必然溢出。
void FillLongText(balloonwjui::DuiRichEdit& re)
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

} // anonymous namespace

void RunAllRichEditScrollTests()
{
    // ---- 用例 1:滚动接口 —— 长文本溢出 + SetVScrollPos 生效 ----
    BeginCase("RichEditScroll.LongText_OverflowsAndScrolls:长文本溢出固定高度且可滚动");
    {
        balloonwjui::DuiRichEdit re;
        SetUpBodyEdit(re);
        FillLongText(re);

        int cH = 0;
        int sP = 0;
        int vH = 0;
        re.GetVScrollMetrics(cH, sP, vH);
        FA_EXPECT_TRUE(vH > 0);          // 可视高有效
        FA_EXPECT_TRUE(cH > vH);         // 内容高 > 可视高 → 溢出
        FA_EXPECT_EQ(sP, 0);             // 初始在顶部

        // 请求滚到很下面。引擎会把超出范围的位置夹取到最底,所以这里不断言
        // 具体数值,只断言"确实往下滚了"。
        re.SetVScrollPos(10000);
        int cH2 = 0;
        int sP2 = 0;
        int vH2 = 0;
        re.GetVScrollMetrics(cH2, sP2, vH2);
        FA_EXPECT_TRUE(sP2 > 0);

        // 再滚回顶部。
        re.SetVScrollPos(0);
        int cH3 = 0;
        int sP3 = 0;
        int vH3 = 0;
        re.GetVScrollMetrics(cH3, sP3, vH3);
        FA_EXPECT_EQ(sP3, 0);
    }

    // ---- 用例 2:滚轮 —— 内容装得下时不消费,让事件继续往上冒泡 ----
    BeginCase("RichEditScroll.Wheel_NotConsumedWhenFits:内容装得下时不吞滚轮");
    {
        balloonwjui::DuiRichEdit re;
        SetUpBodyEdit(re);
        re.SetText(_T("只有一行,装得下。"));

        int cH = 0;
        int sP = 0;
        int vH = 0;
        re.GetVScrollMetrics(cH, sP, vH);
        FA_EXPECT_TRUE(cH <= vH);        // 前提:确实没有溢出

        POINT pt;
        pt.x = kBodyW / 2;
        pt.y = kBodyH / 2;
        FA_EXPECT_TRUE(!re.OnMouseWheel(pt, -WHEEL_DELTA, 0));
    }
}

}} // namespace flamingoadmin::test
