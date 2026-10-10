/**
 *  ChatIconButton.cpp
 *
 *  单聊窗图标按钮的绘制实现。
 *
 *  本 .cpp 不 include Flamingo 的 stdafx.h —— 它要被 Flamingo.vcxproj 与
 *  FlamingoUnit.vcxproj 共用。在 Flamingo.vcxproj 里需标记
 *  PrecompiledHeader=NotUsing。
 *
 *  balloonwj@qq.com   2026-05-21
 */

// ---- balloonui 头文件要求的 ATL/WTL 前置依赖（等价 NewChatDemo/stdafx.h）----
#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif
#define WINVER          0x0601      // 目标系统下限 = Windows 7
#define _WIN32_WINNT    0x0601
#define _WIN32_IE       0x0601
#define _RICHEDIT_VER   0x0300      // RichEdit 3.0，WTL 10 要求的下限（窗口类仍为 RichEdit20W）
#define _WTL_NO_CSTRING
#define _WTL_NO_WTYPES

#include <atlbase.h>
#include <atlstr.h>
#include <atltypes.h>
#include <atlapp.h>

extern CAppModule _Module;

#include <atlwin.h>
#include <atlcrack.h>
#include <atlmisc.h>

#include <gdiplus.h>
#pragma comment(lib, "gdiplus.lib")

#include "ChatIconButton.h"
#include "ChatIcons.h"
#include "../../../third-party/balloonui/balloonui/Controls/Feedback/DuiToolTip.h"
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiBadge.h"

#include <memory>

namespace balloonchatwnd {

namespace {

// 悬停底色（浅灰）。
const Gdiplus::Color kHoverFill(255, 0xF0, 0xEF, 0xEC);
// 图标颜色（design --text-muted #6B6B6B）。
const Gdiplus::Color kIconColor(255, 0x6B, 0x6B, 0x6B);
// 高亮状态（如字幕模式开着时的「字幕模式」按钮）的图标颜色：蓝色 #3B82F6。高亮时不画底色，
// 悬停时与普通按钮一样画浅灰底（用户 2026-10-05 改定：只让图标高亮，不要浅蓝背景）
const Gdiplus::Color kActiveIconColor(255, 0x3B, 0x82, 0xF6);
// 禁用状态的图标颜色（浅灰）：按钮被 SetEnabled(false) 时使用（最初用于聊天输入区 Markdown 模式下置灰的「表情」按钮，该模式已删除）。
// 禁用的控件不参与命中测试，没有悬停底，也收不到点击
const Gdiplus::Color kDisabledIconColor(255, 0xC4, 0xC4, 0xC4);

const Gdiplus::REAL kIconSize    = 18.0f;   // 图标显示尺寸
const Gdiplus::REAL kStrokeWidth = 2.0f;    // 图标描边宽度（viewBox 单位）
const Gdiplus::REAL kHoverRadius = 6.0f;    // 悬停底圆角

// 角标定位框：右边与按钮右边对齐，宽 kBadgeBoxWidth、高 kBadgeBoxHeight（像素），垂直中心落在图标的上沿上，
// 压住图标的右上角（头部按钮比图标高很多，贴按钮上沿会离图标太远）。
// DuiBadge 在定位框里居中画胶囊：一位数是直径等于框高的圆，「99+」时加宽到定位框的宽度
const int kBadgeBoxWidth  = 26;
const int kBadgeBoxHeight = 16;

// 向 path 添加一个圆角矩形。
void AddRoundRect(Gdiplus::GraphicsPath& path,
                  Gdiplus::REAL x, Gdiplus::REAL y,
                  Gdiplus::REAL w, Gdiplus::REAL h, Gdiplus::REAL radius)
{
    Gdiplus::REAL r = radius;
    if (r > w / 2.0f) { r = w / 2.0f; }
    if (r > h / 2.0f) { r = h / 2.0f; }
    const Gdiplus::REAL d = r * 2.0f;
    path.AddArc(x,         y,         d, d, 180.0f, 90.0f);
    path.AddArc(x + w - d, y,         d, d, 270.0f, 90.0f);
    path.AddArc(x + w - d, y + h - d, d, d, 0.0f,   90.0f);
    path.AddArc(x,         y + h - d, d, d, 90.0f,  90.0f);
    path.CloseFigure();
}

} // anonymous namespace

ChatIconButton::ChatIconButton()
    : m_iconKind(kChatIconMore)
    , m_badge(NULL)
    , m_badgeCount(0)
    , m_active(false)
    , m_hasToggleTips(false)
{
}

void ChatIconButton::SetActive(bool active)
{
    if (active == m_active)
    {
        return;
    }
    m_active = active;
    ApplyToggleTip();
    Invalidate();
}

void ChatIconButton::SetToggleTips(LPCTSTR activeTip, LPCTSTR inactiveTip)
{
    m_activeTip = (activeTip != NULL) ? activeTip : _T("");
    m_inactiveTip = (inactiveTip != NULL) ? inactiveTip : _T("");
    m_hasToggleTips = true;
    ApplyToggleTip();
}

CString ChatIconButton::GetToggleTip() const
{
    if (!m_hasToggleTips)
    {
        return CString();
    }
    return m_active ? m_activeTip : m_inactiveTip;
}

void ChatIconButton::ApplyToggleTip()
{
    if (!m_hasToggleTips)
    {
        return;
    }
    // 重复登记同一控件时 DuiToolTipMgr 覆盖原文字；下次鼠标悬停弹出提示时显示新文字
    balloonwjui::DuiToolTipMgr::Inst().Register(this, m_active ? (LPCTSTR)m_activeTip : (LPCTSTR)m_inactiveTip);
}

ChatIconButton::~ChatIconButton()
{
    // 控件销毁前注销 tooltip，避免管理器残留死指针。
    balloonwjui::DuiToolTipMgr::Inst().Unregister(this);
}

void ChatIconButton::SetIconKind(int kind)
{
    m_iconKind = kind;
    Invalidate();
}

void ChatIconButton::OnPaint(HDC hdc, const RECT& /*rcDirty*/)
{
    const int x = m_rcItem.left;
    const int y = m_rcItem.top;
    const int w = m_rcItem.right  - m_rcItem.left;
    const int h = m_rcItem.bottom - m_rcItem.top;
    if (w <= 0 || h <= 0)
    {
        return;
    }

    // GDI+ 的绘制放在一个块里：角标用 GDI 画在同一个 HDC 上，须等 Graphics 对象释放 HDC 之后
    {
        Gdiplus::Graphics g(hdc);
        g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);

        // 底色：不论是否高亮，只在悬停时有浅灰底（高亮只体现在图标颜色上）。行内缩 2px 留呼吸位。
        if (IsHover())
        {
            Gdiplus::GraphicsPath path;
            AddRoundRect(path, (Gdiplus::REAL)(x + 2), (Gdiplus::REAL)(y + 2),
                         (Gdiplus::REAL)(w - 4), (Gdiplus::REAL)(h - 4), kHoverRadius);
            Gdiplus::SolidBrush fill(kHoverFill);
            g.FillPath(&fill, &path);
        }

        // 图标居中。
        const Gdiplus::REAL iconX = (Gdiplus::REAL)x + ((Gdiplus::REAL)w - kIconSize) / 2.0f;
        const Gdiplus::REAL iconY = (Gdiplus::REAL)y + ((Gdiplus::REAL)h - kIconSize) / 2.0f;
        const Gdiplus::Color iconColor = !IsEnabled() ? kDisabledIconColor : (m_active ? kActiveIconColor : kIconColor);
        DrawChatIcon(g, m_iconKind, iconX, iconY, kIconSize, iconColor, kStrokeWidth);
    }

    // 角标画在图标之上（本类覆写了 OnPaint，子控件不会自动绘制）
    if (m_badge != NULL && m_badge->IsShowing())
    {
        m_badge->OnPaint(hdc, m_badge->GetRect());
    }
}

void ChatIconButton::SetBadgeCount(int count)
{
    if (count < 0)
    {
        count = 0;
    }
    if (count == m_badgeCount)
    {
        return;
    }
    m_badgeCount = count;
    if (m_badge == NULL)
    {
        if (count == 0)
        {
            return;
        }
        std::unique_ptr<balloonwjui::DuiBadge> badge(new balloonwjui::DuiBadge());
        // 不可用的控件不参与命中测试：点在角标上仍然是点这个按钮
        badge->SetEnabled(false);
        m_badge = badge.get();
        AddChild(std::unique_ptr<balloonwjui::DuiControl>(badge.release()));
        PlaceBadge();
    }
    m_badge->SetCount(count);
    Invalidate();
}

RECT ChatIconButton::GetBadgeRect() const
{
    if (m_badge == NULL)
    {
        RECT empty = { 0, 0, 0, 0 };
        return empty;
    }
    return m_badge->GetRect();
}

void ChatIconButton::Layout(const RECT& rcAvail)
{
    balloonwjui::DuiButton::Layout(rcAvail);
    PlaceBadge();
}

void ChatIconButton::PlaceBadge()
{
    if (m_badge == NULL)
    {
        return;
    }
    //图标在按钮里垂直居中，上沿在中线以上半个图标高处；按钮太矮时定位框不越过按钮上沿
    const int iconTop = (m_rcItem.top + m_rcItem.bottom) / 2 - (int)(kIconSize / 2.0f);
    int top = iconTop - kBadgeBoxHeight / 2;
    if (top < m_rcItem.top)
    {
        top = m_rcItem.top;
    }
    RECT box = { m_rcItem.right - kBadgeBoxWidth, top, m_rcItem.right, top + kBadgeBoxHeight };
    m_badge->SetRect(box);
}

} // namespace balloonchatwnd
