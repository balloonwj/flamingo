/**
 *  MainPanelHeaderMsgBoxTests.cpp
 *
 *  主面板头部底部两个按钮（MainPanelHeader 的公告中心铃铛与消息盒子按钮，2026-10-04 起）的测试，
 *  标签 [header][msgbox]：
 *    HMB1 铃铛右侧不再画「你有 N 条新公告」文字，铃铛的命中区只有方块本身（有角标时加上角标）；
 *    HMB2 消息盒子按钮与铃铛在同一行、尺寸相同，在铃铛右侧，两者不重叠；
 *    HMB3 未读数负数按 0 处理；0 时没有角标，大于 0 时消息盒子角标是灰色、公告角标仍是黄色；
 *    HMB4 点消息盒子按钮发 DUIN_HEADER_MSGBOX_CLICK，点铃铛发 DUIN_HEADER_BELL_CLICK，点两者之间不发；
 *    HMB5 鼠标移到铃铛或消息盒子上登记对应的提示文字，移出或离开控件后清除，悬停期间未读数变化时提示跟着变。
 *  头部控件在内存位图上绘制；HMB4 建一个不显示的宿主窗口截获通知。框架 Catch2 v3；全局 _Module 由
 *  BalloonChatLayoutTests.cpp 统一定义。
 *
 *  balloonwj@qq.com   2026-10-04
 */
#include "stdafx.h"

#include "catch_amalgamated.hpp"

#include "../UI/BalloonMain/MainPanelHeader.h"
#include "../../../third-party/balloonui/balloonui/DuiHost.h"
#include "../../../third-party/balloonui/balloonui/DuiNotify.h"
#include "../../../third-party/balloonui/balloonui/Controls/Feedback/DuiToolTip.h"

#include <gdiplus.h>
#include <stdlib.h>   // abs
#include <utility>
#include <vector>

using balloonmain::MainPanelHeader;

namespace {

// 头部控件的尺寸：与主面板真实布局一致（宽 320、Banner 高 108）
const int kHmbW = 320;
const int kHmbH = 108;
// 底部按钮的边长（与 MainPanelHeader.cpp 的 kBellBoxSize 相同）
const int kHmbBox = 24;
// 角标溢出方块的像素（与 MainPanelHeader.cpp 的 kBadgeOverflow 相同）
const int kHmbOverflow = 3;
// 一位数角标的直径（与 MainPanelHeader.cpp 的 kBadgeHeight 相同）
const int kHmbBadgeD = 15;
// 取角标填充色的采样点：距角标左缘、上缘的像素，避开描边与居中的数字
const int kHmbBadgeProbeX = 3;
const int kHmbBadgeProbeY = 7;
// 颜色比较的容差（每个分量）
const int kHmbColorTol = 3;
// 渐变背景的绿色分量最大为 0x7A（三段渐变色里最大的那个），暖白文字的绿色分量是 0xEC；
// 采样到高于这个阈值的绿色分量就说明那里画了文字
const int kHmbTextGreen = 0xB0;
// 检查「铃铛右侧没有文字」的横向范围终点（原先文字最长能画到的位置）
const int kHmbTextScanRight = 220;
// 测试用的头部控件 id
const UINT kHmbHeaderId = 77;
// 测试里注入的未读数
const int kHmbAnnounceUnread = 3;
const int kHmbMsgBoxUnread = 5;
const int kHmbMsgBoxUnreadLater = 8;

// 灰色角标与黄色角标的填充色（与 MainPanelHeader.cpp 的 kMsgBoxBadgeFill、kBadgeFill 相同）
const COLORREF kHmbGray = RGB(0x9A, 0xA0, 0xA6);
const COLORREF kHmbYellow = RGB(0xFF, 0xE0, 0x66);

// 进程内启动 / 关闭 GDI+（头部控件用 GDI+ 画形状）
class HmbScopedGdiplus
{
public:
    // 启动 GDI+；失败时 Ok() 返回 false
    HmbScopedGdiplus()
        : m_token(0)
    {
        Gdiplus::GdiplusStartupInput input;
        m_ok = (Gdiplus::GdiplusStartup(&m_token, &input, NULL) == Gdiplus::Ok);
    }

    // 关闭 GDI+
    ~HmbScopedGdiplus()
    {
        if (m_ok)
            Gdiplus::GdiplusShutdown(m_token);
    }

    // GDI+ 是否启动成功
    bool Ok() const
    {
        return m_ok;
    }

private:
    ULONG_PTR m_token;   // GdiplusStartup 返回的令牌，关闭时要用
    bool      m_ok;      // 是否启动成功
};

// 建一个已注入用户资料的头部控件（注入后底部铃铛的未读数取 SetAnnounceUnread 的值，不取模拟数据）
void HmbSetup(MainPanelHeader& header)
{
    const RECT rc = { 0, 0, kHmbW, kHmbH };
    header.SetRect(rc);
    header.SetCtrlId(kHmbHeaderId);
    header.SetUserData(_T("张三"), _T("MY0001"), _T("签名"), _T("张"), RGB(0xFF, 0x7A, 0x45), 0, 0);
}

// 把头部控件画到白底内存位图上，读出若干点的颜色，并返回整幅像素（每像素 0x00RRGGBB，自上而下）。
//   points：要采样的点（控件坐标）；colors：[出参] 对应各点的颜色；pixels：[出参] 整幅像素。
// @return 位图创建成功返回 true
bool HmbPaint(MainPanelHeader& header, const std::vector<POINT>& points, std::vector<COLORREF>& colors,
              std::vector<DWORD>& pixels)
{
    BITMAPINFO bi;
    ZeroMemory(&bi, sizeof(bi));
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = kHmbW;
    bi.bmiHeader.biHeight = -kHmbH;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    void* bits = NULL;
    HDC dc = ::CreateCompatibleDC(NULL);
    HBITMAP bmp = ::CreateDIBSection(dc, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
    if (dc == NULL || bmp == NULL || bits == NULL)
        return false;
    HGDIOBJ old = ::SelectObject(dc, bmp);
    RECT rc = { 0, 0, kHmbW, kHmbH };
    ::FillRect(dc, &rc, (HBRUSH)::GetStockObject(WHITE_BRUSH));
    header.OnPaint(dc, rc);
    const DWORD* px = static_cast<const DWORD*>(bits);
    pixels.assign(px, px + kHmbW * kHmbH);
    colors.clear();
    for (size_t i = 0; i < points.size(); ++i)
    {
        const DWORD v = px[points[i].y * kHmbW + points[i].x];
        colors.push_back(RGB((v >> 16) & 0xFF, (v >> 8) & 0xFF, v & 0xFF));
    }
    ::SelectObject(dc, old);
    ::DeleteObject(bmp);
    ::DeleteDC(dc);
    return true;
}

// 只画一次、不采样（用来让控件算出命中矩形）
bool HmbPaintOnly(MainPanelHeader& header)
{
    std::vector<POINT> none;
    std::vector<COLORREF> colors;
    std::vector<DWORD> pixels;
    return HmbPaint(header, none, colors, pixels);
}

// 两个颜色每个分量都在容差之内
bool HmbNear(COLORREF a, COLORREF b)
{
    return abs((int)GetRValue(a) - (int)GetRValue(b)) <= kHmbColorTol
        && abs((int)GetGValue(a) - (int)GetGValue(b)) <= kHmbColorTol
        && abs((int)GetBValue(a) - (int)GetBValue(b)) <= kHmbColorTol;
}

// 命中矩形右上角的一位数角标里，取填充色的采样点
POINT HmbBadgeProbe(const RECT& hit)
{
    POINT pt;
    pt.x = hit.right - kHmbBadgeD + kHmbBadgeProbeX;
    pt.y = hit.top + kHmbBadgeProbeY;
    return pt;
}

// 矩形中心
POINT HmbCenter(const RECT& rc)
{
    POINT pt;
    pt.x = (rc.left + rc.right) / 2;
    pt.y = (rc.top + rc.bottom) / 2;
    return pt;
}

// 记录 WM_DUI_NOTIFY 的父窗口：按到达顺序记下（通知码, ctrlId）
class HmbNotifyRecorder : public CWindowImpl<HmbNotifyRecorder>
{
public:
    DECLARE_WND_CLASS(_T("MainPanelHeaderMsgBoxNotifyRecorder"))

    BEGIN_MSG_MAP(HmbNotifyRecorder)
        MESSAGE_HANDLER(WM_DUI_NOTIFY, OnDuiNotify)
    END_MSG_MAP()

    // 记下一条通知
    LRESULT OnDuiNotify(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam, BOOL& /*bHandled*/)
    {
        const balloonwjui::DuiNotify* n = reinterpret_cast<const balloonwjui::DuiNotify*>(lParam);
        if (n != nullptr)
        {
            m_notes.push_back(std::make_pair(n->code, n->ctrlId));
        }
        return 0;
    }

    std::vector<std::pair<UINT, UINT> > m_notes;   // 收到的（通知码, ctrlId），按到达顺序
};

} // anonymous namespace

TEST_CASE("HMB1 铃铛右侧不再画文字，铃铛的命中区只有方块与角标", "[header][msgbox]")
{
    HmbScopedGdiplus gdiplus;
    REQUIRE(gdiplus.Ok());
    MainPanelHeader header;
    HmbSetup(header);

    // 没有未读公告：命中区就是 24x24 的方块
    std::vector<POINT> none;
    std::vector<COLORREF> colors;
    std::vector<DWORD> pixels;
    REQUIRE(HmbPaint(header, none, colors, pixels));
    const RECT bell = header.GetBellHitRect();
    CHECK((bell.right - bell.left) == kHmbBox);
    CHECK((bell.bottom - bell.top) == kHmbBox);

    // 两个按钮右侧到原先文字最远处，这一行里没有暖白色的文字像素
    const RECT box = header.GetMsgBoxHitRect();
    int textPixels = 0;
    for (int y = bell.top; y < bell.bottom; ++y)
    {
        for (int x = box.right + kHmbOverflow; x < kHmbTextScanRight; ++x)
        {
            const DWORD v = pixels[y * kHmbW + x];
            if ((int)((v >> 8) & 0xFF) > kHmbTextGreen)
                ++textPixels;
        }
    }
    CHECK(textPixels == 0);

    // 有未读公告：命中区加上溢出的角标，向右、向上各多 3 像素，不再向右延伸到文字
    header.SetAnnounceUnread(kHmbAnnounceUnread);
    REQUIRE(HmbPaintOnly(header));
    const RECT bell2 = header.GetBellHitRect();
    CHECK((bell2.right - bell2.left) == kHmbBox + kHmbOverflow);
    CHECK((bell2.bottom - bell2.top) == kHmbBox + kHmbOverflow);
}

TEST_CASE("HMB2 消息盒子按钮在铃铛右侧、同一行、同样大小，两者不重叠", "[header][msgbox]")
{
    HmbScopedGdiplus gdiplus;
    REQUIRE(gdiplus.Ok());
    MainPanelHeader header;
    HmbSetup(header);
    REQUIRE(HmbPaintOnly(header));

    const RECT bell = header.GetBellHitRect();
    const RECT box = header.GetMsgBoxHitRect();
    REQUIRE_FALSE(::IsRectEmpty(&box));
    CHECK(box.top == bell.top);
    CHECK(box.bottom == bell.bottom);
    CHECK((box.right - box.left) == (bell.right - bell.left));
    CHECK(box.left > bell.right);
    RECT inter;
    CHECK_FALSE(::IntersectRect(&inter, &bell, &box));
    // 在控件之内
    CHECK(box.right <= kHmbW);
    CHECK(box.bottom <= kHmbH);

    // 两个按钮都带角标时同样不重叠
    header.SetAnnounceUnread(kHmbAnnounceUnread);
    header.SetMsgBoxUnread(kHmbMsgBoxUnread);
    REQUIRE(HmbPaintOnly(header));
    const RECT bell2 = header.GetBellHitRect();
    const RECT box2 = header.GetMsgBoxHitRect();
    CHECK_FALSE(::IntersectRect(&inter, &bell2, &box2));
}

TEST_CASE("HMB3 未读数：负数按 0；0 时无角标，大于 0 时消息盒子角标灰色、公告角标黄色", "[header][msgbox]")
{
    HmbScopedGdiplus gdiplus;
    REQUIRE(gdiplus.Ok());
    MainPanelHeader header;
    HmbSetup(header);

    CHECK(header.GetMsgBoxUnread() == 0);
    header.SetMsgBoxUnread(-2);
    CHECK(header.GetMsgBoxUnread() == 0);

    // 0：命中区就是方块，没有溢出的角标
    REQUIRE(HmbPaintOnly(header));
    const RECT box0 = header.GetMsgBoxHitRect();
    CHECK((box0.right - box0.left) == kHmbBox);
    CHECK((box0.bottom - box0.top) == kHmbBox);

    // 大于 0：两个角标都画出来，颜色各不相同
    header.SetMsgBoxUnread(kHmbMsgBoxUnread);
    header.SetAnnounceUnread(kHmbAnnounceUnread);
    CHECK(header.GetMsgBoxUnread() == kHmbMsgBoxUnread);
    REQUIRE(HmbPaintOnly(header));
    const RECT box = header.GetMsgBoxHitRect();
    const RECT bell = header.GetBellHitRect();
    CHECK((box.right - box.left) == kHmbBox + kHmbOverflow);

    std::vector<POINT> probes;
    probes.push_back(HmbBadgeProbe(box));
    probes.push_back(HmbBadgeProbe(bell));
    std::vector<COLORREF> colors;
    std::vector<DWORD> pixels;
    REQUIRE(HmbPaint(header, probes, colors, pixels));
    CHECK(HmbNear(colors[0], kHmbGray));
    CHECK(HmbNear(colors[1], kHmbYellow));
}

TEST_CASE("HMB4 点消息盒子按钮与铃铛各发各的通知，点两者之间不发", "[header][msgbox]")
{
    HmbScopedGdiplus gdiplus;
    REQUIRE(gdiplus.Ok());
    // 两个窗口都不带 WS_VISIBLE，跑测试时屏幕上不会闪出窗口
    HmbNotifyRecorder recorder;
    balloonwjui::DuiHost host;
    RECT wrc = { 0, 0, kHmbW, kHmbH };
    recorder.Create(NULL, wrc, _T("MainPanelHeaderMsgBoxTests"), WS_POPUP);
    REQUIRE(recorder.IsWindow());
    host.Create(recorder.m_hWnd, wrc, NULL, WS_CHILD);
    REQUIRE(host.IsWindow());

    {
        MainPanelHeader header;
        HmbSetup(header);
        header.AttachToHost(&host);
        REQUIRE(HmbPaintOnly(header));
        const RECT bell = header.GetBellHitRect();
        const RECT box = header.GetMsgBoxHitRect();

        CHECK(header.OnLButtonUp(HmbCenter(box), 0));
        CHECK(header.OnLButtonUp(HmbCenter(bell), 0));
        // 两个按钮之间的空隙：不发通知，留给窗口拖动
        POINT gap;
        gap.x = (bell.right + box.left) / 2;
        gap.y = (bell.top + bell.bottom) / 2;
        CHECK_FALSE(header.OnLButtonUp(gap, 0));

        REQUIRE(recorder.m_notes.size() == 2);
        CHECK(recorder.m_notes[0].first == (UINT)MainPanelHeader::DUIN_HEADER_MSGBOX_CLICK);
        CHECK(recorder.m_notes[0].second == kHmbHeaderId);
        CHECK(recorder.m_notes[1].first == (UINT)MainPanelHeader::DUIN_HEADER_BELL_CLICK);
        CHECK(recorder.m_notes[1].second == kHmbHeaderId);
        // 两个通知码数值不同
        CHECK((UINT)MainPanelHeader::DUIN_HEADER_MSGBOX_CLICK != (UINT)MainPanelHeader::DUIN_HEADER_BELL_CLICK);
    }

    host.DestroyWindow();
    recorder.DestroyWindow();
}

TEST_CASE("HMB5 鼠标移到铃铛或消息盒子上登记提示，移出或离开后清除，未读数变化时提示跟着变", "[header][msgbox]")
{
    HmbScopedGdiplus gdiplus;
    REQUIRE(gdiplus.Ok());
    MainPanelHeader header;
    HmbSetup(header);
    REQUIRE(HmbPaintOnly(header));
    balloonwjui::DuiToolTipMgr& tips = balloonwjui::DuiToolTipMgr::Inst();
    const POINT bellPt = HmbCenter(header.GetBellHitRect());
    const POINT boxPt = HmbCenter(header.GetMsgBoxHitRect());
    POINT outside;
    outside.x = kHmbW / 2;
    outside.y = kHmbH / 2;

    CHECK(header.GetHoverZone() == MainPanelHeader::kHoverNone);
    CHECK(tips.GetText(&header).IsEmpty());

    // 铃铛：没有未读时提示入口名称
    header.OnMouseMove(bellPt, 0);
    CHECK(header.GetHoverZone() == MainPanelHeader::kHoverBell);
    CHECK(tips.GetText(&header) == CString(L"公告中心"));
    // 悬停期间未读数变化，提示跟着变
    header.SetAnnounceUnread(kHmbAnnounceUnread);
    CHECK(tips.GetText(&header) == CString(L"你有 3 条新公告"));

    // 直接移到消息盒子上：提示换成消息盒子的
    header.OnMouseMove(boxPt, 0);
    CHECK(header.GetHoverZone() == MainPanelHeader::kHoverMsgBox);
    CHECK(tips.GetText(&header) == CString(L"消息盒子"));
    header.SetMsgBoxUnread(kHmbMsgBoxUnreadLater);
    CHECK(tips.GetText(&header) == CString(L"消息盒子：8 条未读消息"));
    // 悬停在消息盒子上时改公告未读数，不影响当前提示
    header.SetAnnounceUnread(kHmbAnnounceUnread + 1);
    CHECK(tips.GetText(&header) == CString(L"消息盒子：8 条未读消息"));

    // 移到按钮之外的渐变区：提示注销
    header.OnMouseMove(outside, 0);
    CHECK(header.GetHoverZone() == MainPanelHeader::kHoverNone);
    CHECK(tips.GetText(&header).IsEmpty());

    // 再移入后直接离开控件：同样注销
    header.OnMouseMove(bellPt, 0);
    CHECK_FALSE(tips.GetText(&header).IsEmpty());
    header.OnMouseLeave();
    CHECK(header.GetHoverZone() == MainPanelHeader::kHoverNone);
    CHECK(tips.GetText(&header).IsEmpty());
}
