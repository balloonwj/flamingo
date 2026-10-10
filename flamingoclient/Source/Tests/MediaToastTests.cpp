/**
 *  FlamingoMedia 通用轻量提示 CMediaToast 的单元测试（标签 [mediatoast]），MediaToastTests.cpp
 *  背景：2026-10-07 起分享链接的复制、重置、停用与失败提示，以及共享与录制互斥被拦下的提示，都由 MessageBox 改成这个
 *  非模态提示（docs/屏幕共享与录制方案.md 第 13 节）。
 *  覆盖：
 *    - MTST1 摆放：默认贴在参照矩形正下方居中；下方放不下时放进参照矩形底部；放在窗口里面时靠底边；没有参照时在工作区
 *      中央偏下；结果总在工作区以内；
 *    - MTST2 淡入淡出：0 时全透明、淡入后不透明、停留期间不透明、淡出后结束；
 *    - MTST3 窗口：显示后可见、不抢焦点、置顶，文字与样式记下；再显示一条时只换文字不另开窗口；Hide 后不可见；
 *      没有参照窗口时也能显示。
 *  框架 Catch2 v3。
 *  balloonwj@qq.com   2026-10-07
 */
#include "stdafx.h"
#include "catch_amalgamated.hpp"

#include "../../FlamingoMedia/Common/MediaToast.h"

using namespace flamingomedia;

namespace {

//工作区与参照矩形（屏幕坐标）
const RECT kMtstWork = { 0, 0, 1920, 1040 };
const RECT kMtstBar = { 600, 8, 1320, 72 };
//提示窗口的尺寸
const LONG kMtstW = 300;
const LONG kMtstH = 40;

SIZE MtstSize()
{
    SIZE s;
    s.cx = kMtstW;
    s.cy = kMtstH;
    return s;
}

bool MtstInside(const RECT& rc, const RECT& work)
{
    return rc.left >= work.left && rc.top >= work.top && rc.right <= work.right && rc.bottom <= work.bottom;
}

} // namespace

TEST_CASE("MTST1 toast placement below, inside and centered", "[mediatoast]")
{
    //控制条正下方居中
    RECT rc = ComputeMediaToastRect(kMtstBar, media_toast_below, MtstSize(), kMtstWork);
    CHECK(rc.top > kMtstBar.bottom);
    CHECK((rc.left + rc.right) / 2 == (kMtstBar.left + kMtstBar.right) / 2);
    CHECK(rc.right - rc.left == kMtstW);
    CHECK(rc.bottom - rc.top == kMtstH);
    //参照矩形贴着工作区底边：放进它的底部
    const RECT low = { 600, 980, 1320, 1040 };
    rc = ComputeMediaToastRect(low, media_toast_below, MtstSize(), kMtstWork);
    CHECK(rc.bottom <= low.bottom);
    CHECK(MtstInside(rc, kMtstWork));
    //窗口里面靠底边
    const RECT wnd = { 100, 100, 900, 700 };
    rc = ComputeMediaToastRect(wnd, media_toast_inside_bottom, MtstSize(), kMtstWork);
    CHECK(rc.bottom < wnd.bottom);
    CHECK(rc.top > wnd.top);
    CHECK((rc.left + rc.right) / 2 == (wnd.left + wnd.right) / 2);
    //没有参照：工作区中央偏下
    const RECT none = { 0, 0, 0, 0 };
    rc = ComputeMediaToastRect(none, media_toast_below, MtstSize(), kMtstWork);
    CHECK((rc.top + rc.bottom) / 2 > (kMtstWork.top + kMtstWork.bottom) / 2);
    CHECK(MtstInside(rc, kMtstWork));
    //参照矩形伸出工作区右边：夹回工作区
    const RECT right = { 1800, 8, 2400, 72 };
    rc = ComputeMediaToastRect(right, media_toast_below, MtstSize(), kMtstWork);
    CHECK(MtstInside(rc, kMtstWork));
}

TEST_CASE("MTST2 toast fades in, holds and fades out", "[mediatoast]")
{
    bool finished = true;
    CHECK(MediaToastAlphaAt(0, finished) == 0);
    CHECK_FALSE(finished);
    CHECK(MediaToastAlphaAt(kMediaToastFadeInMs / 2, finished) > 0);
    CHECK(MediaToastAlphaAt(kMediaToastFadeInMs, finished) == 255);
    CHECK(MediaToastAlphaAt(kMediaToastFadeInMs + kMediaToastHoldMs - 1, finished) == 255);
    const int64_t fadeMid = kMediaToastFadeInMs + kMediaToastHoldMs + kMediaToastFadeOutMs / 2;
    const int mid = MediaToastAlphaAt(fadeMid, finished);
    CHECK(mid > 0);
    CHECK(mid < 255);
    CHECK_FALSE(finished);
    CHECK(MediaToastAlphaAt(kMediaToastFadeInMs + kMediaToastHoldMs + kMediaToastFadeOutMs, finished) == 0);
    CHECK(finished);
}

TEST_CASE("MTST3 toast window shows, replaces text and hides", "[mediatoast]")
{
    CMediaToast toast;
    toast.Show(kMtstBar, media_toast_below, L"分享链接已复制", media_toast_info);   //i18n-ignore: 测试数据
    REQUIRE(toast.IsWindow());
    CHECK(toast.IsShowing());
    CHECK(toast.IsWindowVisible());
    CHECK(toast.Text() == L"分享链接已复制");   //i18n-ignore: 测试数据
    CHECK(toast.Style() == media_toast_info);
    const LONG_PTR ex = ::GetWindowLongPtrW(toast.m_hWnd, GWL_EXSTYLE);
    CHECK((ex & WS_EX_NOACTIVATE) != 0);
    CHECK((ex & WS_EX_TOPMOST) != 0);
    CHECK((ex & WS_EX_TOOLWINDOW) != 0);
    const HWND first = toast.m_hWnd;
    //再显示一条：同一个窗口
    toast.Show(kMtstBar, media_toast_below, L"操作没有成功", media_toast_error);   //i18n-ignore: 测试数据
    CHECK(toast.m_hWnd == first);
    CHECK(toast.Style() == media_toast_error);
    toast.Hide();
    CHECK_FALSE(toast.IsShowing());
    CHECK_FALSE(toast.IsWindowVisible());
    //没有参照窗口
    toast.ShowNearWindow(NULL, L"提示", media_toast_info);   //i18n-ignore: 测试数据
    CHECK(toast.IsShowing());
    toast.Destroy();
    CHECK_FALSE(toast.IsWindow());
}
