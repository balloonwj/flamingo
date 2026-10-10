/**
 *  P2PBadge.cpp
 *
 *  「P2P」徽章的判定、量尺寸、绘制与生成 balloonui 控件，见 P2PBadge.h。
 *
 *  balloonwj@qq.com   2026-10-04
 */
#include "P2PBadge.h"

#include <gdiplus.h>

#include "../../Common/I18n/I18n.h"   //i18n::UiFontName：叠加徽章用界面字体

namespace balloonmain {

namespace {

//链路实际路径里表示直连的两个取值，与 FlamingoP2PApi.h 的 p2p_link_path_lan / p2p_link_path_punched 相同。
//本文件不包含 P2P 模块的头文件（FlamingoMedia.dll、FlamingoFileShare.dll 也编译本文件，不该为一枚徽章引入 P2P 接口）
const int kLinkPathLan = 1;
const int kLinkPathPunched = 2;

//胶囊两端圆弧的起始角与扫过的角度（度，GDI+ 以 x 轴正向为 0、顺时针为正）：左端从 90 度扫到 270 度，右端从 270 度扫到 90 度
const float kLeftArcStartDeg = 90.0f;
const float kRightArcStartDeg = 270.0f;
const float kArcSweepDeg = 180.0f;

/**
 *  确保 GDI+ 在本模块可用。本进程没有全局的 GdiplusStartup，各模块按仓库既有做法各自惰性初始化一次
 *  （原委见 ChatBubble.cpp 的 EnsureGdiplusForBubble）；多次启动是安全的，各持一个 token。
 */
void EnsureGdiplusForBadge()
{
    static ULONG_PTR s_token = 0;
    if (s_token != 0)
        return;
    Gdiplus::GdiplusStartupInput input;
    Gdiplus::GdiplusStartup(&s_token, &input, NULL);
}

} // namespace

bool ShouldShowP2PBadge(int linkPath)
{
    //只有数据没经过服务器的两种路径才显示；经 imgserver 一对一中转时与没有 P2P 一样不显示（方案决定 4）
    return linkPath == kLinkPathLan || linkPath == kLinkPathPunched;
}

SIZE MeasureP2PBadge(HDC hdc, HFONT font)
{
    SIZE sz = { 0, 0 };
    if (hdc == NULL)
        return sz;

    HGDIOBJ old = NULL;
    if (font != NULL)
        old = ::SelectObject(hdc, font);
    SIZE text = { 0, 0 };
    ::GetTextExtentPoint32W(hdc, kP2PBadgeText, (int)wcslen(kP2PBadgeText), &text);
    if (font != NULL)
        ::SelectObject(hdc, old);

    sz.cx = text.cx + 2 * kP2PBadgePadX;
    sz.cy = text.cy + 2 * kP2PBadgePadY;
    return sz;
}

void DrawP2PBadge(HDC hdc, const RECT& rc, HFONT font)
{
    const int w = rc.right - rc.left;
    const int h = rc.bottom - rc.top;
    if (hdc == NULL || w <= 0 || h <= 0)
        return;

    //底：两段圆弧拼成的胶囊，抗锯齿填充，做法与聊天窗头部的状态胶囊相同（ChatHeaderInfo.cpp）
    EnsureGdiplusForBadge();
    {
        Gdiplus::Graphics g(hdc);
        g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        //圆弧所在正方形的边长等于外框高度，两端因而是完整的半圆
        const Gdiplus::REAL d = (Gdiplus::REAL)h;
        Gdiplus::GraphicsPath pill;
        if ((Gdiplus::REAL)w <= d)
        {
            //外框比高度还窄时画成正圆，不让两段圆弧交叉
            pill.AddEllipse((Gdiplus::REAL)rc.left, (Gdiplus::REAL)rc.top, (Gdiplus::REAL)w, d);
        }
        else
        {
            pill.AddArc((Gdiplus::REAL)rc.left, (Gdiplus::REAL)rc.top, d, d, kLeftArcStartDeg, kArcSweepDeg);
            pill.AddArc((Gdiplus::REAL)rc.right - d, (Gdiplus::REAL)rc.top, d, d, kRightArcStartDeg, kArcSweepDeg);
            pill.CloseFigure();
        }
        Gdiplus::SolidBrush brush(Gdiplus::Color(255, GetRValue(kP2PBadgeBgColor), GetGValue(kP2PBadgeBgColor),
                                                 GetBValue(kP2PBadgeBgColor)));
        g.FillPath(&brush, &pill);
    }

    //字：GDI 透明背景居中输出，与界面其它文字同一套字体渲染
    HGDIOBJ oldFont = NULL;
    if (font != NULL)
        oldFont = ::SelectObject(hdc, font);
    const int oldMode = ::SetBkMode(hdc, TRANSPARENT);
    const COLORREF oldColor = ::SetTextColor(hdc, kP2PBadgeTextColor);
    RECT textRc = rc;
    ::DrawTextW(hdc, kP2PBadgeText, -1, &textRc, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_NOCLIP);
    ::SetTextColor(hdc, oldColor);
    ::SetBkMode(hdc, oldMode);
    if (font != NULL)
        ::SelectObject(hdc, oldFont);
}

std::unique_ptr<balloonwjui::DuiBadge> MakeP2PBadgeControl()
{
    std::unique_ptr<balloonwjui::DuiBadge> badge(new balloonwjui::DuiBadge());
    badge->SetText(kP2PBadgeText);
    badge->SetBgColor(kP2PBadgeBgColor);
    badge->SetTextColor(kP2PBadgeTextColor);
    //-1：圆角半径取高度的一半，两端是半圆
    badge->SetCornerRadius(-1);
    //0：不截断，「P2P」三个字符总是完整显示
    badge->SetMaxDisplayChars(0);
    //文字固定不为空，这一项只是写明意图：显隐由调用方按链路路径用 SetVisible 控制
    badge->SetHideWhenEmpty(false);
    return badge;
}

void DrawP2PBadgeOverlay(HDC hdc, const RECT& area)
{
    if (hdc == NULL)
        return;
    HFONT font = ::CreateFontW(-kP2PBadgeOverlayFontPx, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                               OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE,
                               i18n::UiFontName());
    if (font == NULL)
        return;
    const SIZE sz = MeasureP2PBadge(hdc, font);
    const int left = area.left + kP2PBadgeOverlayMargin;
    const int top = area.top + kP2PBadgeOverlayMargin;
    //画面放不下徽章（窗口被拖得很小）：不画，免得画到画面外面
    if (sz.cx > 0 && left + sz.cx <= area.right && top + sz.cy <= area.bottom)
    {
        const RECT rc = { left, top, left + sz.cx, top + sz.cy };
        DrawP2PBadge(hdc, rc, font);
    }
    ::DeleteObject(font);
}

bool ApplyP2PBadgeVisibility(balloonwjui::DuiBadge* badge, int linkPath)
{
    if (badge == NULL)
        return false;
    const bool show = ShouldShowP2PBadge(linkPath);
    if (badge->IsVisible() == show)
        return false;
    badge->SetVisible(show);
    //SetVisible 只置标志并重绘，不重新布局；隐藏期间父容器可能已经重排过，徽章停在过时的矩形上，
    //故按父容器当前的矩形强制重排一次（必须用 ForceLayout：矩形没变时 SetRect 直接返回）
    balloonwjui::DuiControl* parent = badge->GetParent();
    if (parent != NULL)
        parent->ForceLayout(parent->GetRect());
    return true;
}

} // namespace balloonmain
