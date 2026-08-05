/**
 *  MainPanelTreeView.cpp
 *
 *  本 .cpp 不 include Flamingo 的 stdafx.h —— 它要被 Flamingo.vcxproj 与
 *  FlamingoUnit.vcxproj 共用。在 Flamingo.vcxproj 里需标记
 *  PrecompiledHeader=NotUsing。
 *
 *  balloonwj@qq.com   2026-05-28
 */

// ---- balloonui 头文件要求的 ATL/WTL 前置依赖(等价 NewChatDemo/stdafx.h) ----
#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif
#define WINVER          0x0601      // 目标系统下限 = Windows 7
#define _WIN32_WINNT    0x0601
#define _WIN32_IE       0x0601
#define _RICHEDIT_VER   0x0200
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

#include <vector>

#include "MainPanelTreeView.h"
#include "MainPanelScrollView.h"   // ForwardWheelToScrollView
#include "BadgeRender.h"           // FormatBadgeText / ComputeBadgeWidth / ComputeBadgeRect
#include "MainPanelIcons.h"        // DrawLineIcon / kIconMute（免打扰会话的静音图标）
#include "../../balloonui/balloonui/DuiResMgr.h"   // 取基类右侧文字默认字体(画时间/量宽)

namespace balloonmain {

namespace {

// ---- 未读红点配色 / 几何（与 MainPanelTab / MainPanelListItem 角标同口径）----

// 未读角标红 #FF4D4F。
const Gdiplus::Color kBadgeRed(255, 0xFF, 0x4D, 0x4F);
// 角标数字色（白）。
const COLORREF kBadgeTextColor = RGB(255, 255, 255);
// 红点圆 / 胶囊高度（直径，逻辑像素）。
const int kBadgeDiameter = 16;
// 每多一位数字，胶囊横向加宽的逻辑像素（与 MainPanelTab 同口径）。
const int kBadgeExtraPerChar = 7;
// 红点右端内距：对齐 DuiTreeView 单列右端 padding（其私有常量 kRightPad=8），
// 使红点右边缘与上排时间右边缘对齐。
const int kBadgeRightPad = 8;
// 红点垂直中心相对行中线 mid 的下移量（逻辑像素）：基类把"预览"副标签按
// DT_TOP 画在 [mid, bottom]，其文字中心约在 mid + 副标签半字高处，这里取 6 让
// 红点与预览同高。
const int kBadgePreviewCenterOffset = 6;
// 角标数字字号（逻辑像素）。
const int kBadgeTextPx = 10;

// ---- 置顶 / 免打扰标记的配色与几何 ----

// 置顶行的覆盖色：低透明度的黑（alpha 26/255 ≈ 10%）。压在文字与头像上不至于让它们
// 发灰，只让整行比旁边的行"重"一点。原先取 14（≈5.5%）在浅色背景上几乎看不出来，故
// 提到 26；再往上加就开始明显影响文字对比度了（本控件的覆盖是画在内容<u>之上</u>的，
// 见 OnPaint 说明）。底色只是辅助，置顶的主要标记是下面那根左侧强调竖条。
const Gdiplus::Color kPinnedRowTint(26, 0, 0, 0);

// ---- 置顶行左侧强调竖条 ----

// 竖条颜色：品牌橙 #FF7A45（与头像调色板第 0 槽、选中态描边同一个橙，见 AvatarPalette.h）。
// 不透明 —— 它是置顶的主要视觉标记，压在行左边缘的空白上，不与任何文字重叠。
const Gdiplus::Color kPinnedAccentColor(255, 0xFF, 0x7A, 0x45);

// 竖条宽度（逻辑像素）。3px 在 320 宽的面板里足够醒目又不显得笨重。
const int kPinnedAccentWidth = 3;

// 竖条高度占行高的百分比。取一半：短于行高才看得出是"标记"而不是分隔线，
// 上下留白也让连续几行置顶时每行的竖条各自独立、不连成一整条。
const int kPinnedAccentHeightPercent = 50;

// 竖条圆角半径（逻辑像素）。取宽度的一半即两端半圆，呈胶囊状。
const Gdiplus::REAL kPinnedAccentRadius = (Gdiplus::REAL)kPinnedAccentWidth / 2.0f;

// 免打扰会话的未读胶囊底色（中灰 #9AA0A6）。保留数字但不再用醒目的红——"有新消息，
// 但不必打扰你"。灰度取到能让白色数字仍然清晰的程度。
const Gdiplus::Color kBadgeMutedGray(255, 0x9A, 0xA0, 0xA6);

// 静音图标边长（逻辑像素）。与未读胶囊同排，故比胶囊直径略小。
const int kMuteIconSize = 12;
// 静音图标与其右侧未读胶囊之间的横向间距（逻辑像素）。
const int kMuteIconGap = 4;
// 静音图标的描边宽度，以 MainPanelIcons 的 viewBox 24 为基准（与菜单图标同口径）。
const Gdiplus::REAL kMuteIconStroke = 2.0f;

// 向 path 添加一个圆角矩形（四段 90° 圆弧 + 闭合）。radius == 高/2 且为正方形时
// 即为正圆；宽 > 高时呈横向胶囊。半径会被 clamp 到不超过宽 / 高的一半。
void AddRoundRectPath(Gdiplus::GraphicsPath& path,
                      Gdiplus::REAL x, Gdiplus::REAL y,
                      Gdiplus::REAL w, Gdiplus::REAL h, Gdiplus::REAL radius)
{
    Gdiplus::REAL r = radius;
    if (r > w / 2.0f)
    {
        r = w / 2.0f;
    }
    if (r > h / 2.0f)
    {
        r = h / 2.0f;
    }
    const Gdiplus::REAL d = r * 2.0f;
    path.AddArc(x,         y,         d, d, 180.0f, 90.0f);
    path.AddArc(x + w - d, y,         d, d, 270.0f, 90.0f);
    path.AddArc(x + w - d, y + h - d, d, d, 0.0f,   90.0f);
    path.AddArc(x,         y + h - d, d, d, 90.0f,  90.0f);
    path.CloseFigure();
}

// 用 GDI 在矩形内居中绘制角标数字 —— DrawText 经系统 ClearType 光栅化，清晰锐利。
void DrawBadgeText(HDC hdc, LPCWSTR text, const RECT& rc)
{
    if (text == NULL || text[0] == L'\0')
    {
        return;
    }
    HFONT hFont = ::CreateFontW(
        -kBadgeTextPx, 0, 0, 0,
        FW_SEMIBOLD,
        FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE,
        L"Microsoft YaHei");
    if (hFont == NULL)
    {
        return;
    }
    HFONT    hOldFont  = (HFONT)::SelectObject(hdc, hFont);
    int      oldBkMode = ::SetBkMode(hdc, TRANSPARENT);
    COLORREF oldColor  = ::SetTextColor(hdc, kBadgeTextColor);

    RECT r = rc;
    ::DrawTextW(hdc, text, -1, &r,
                DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOCLIP);

    ::SetTextColor(hdc, oldColor);
    ::SetBkMode(hdc, oldBkMode);
    ::SelectObject(hdc, hOldFont);
    ::DeleteObject(hFont);
}

// 用基类右侧文字所用的默认字体，量一段文字的像素宽（屏幕 DC，与窗口是否已
// 创建无关）。取不到返回 0。
int MeasureDefaultTextWidth(LPCWSTR text)
{
    if (text == NULL || text[0] == L'\0')
    {
        return 0;
    }
    HDC dc = ::GetDC(NULL);
    if (dc == NULL)
    {
        return 0;
    }
    HFONT f = balloonwjui::DuiResMgr::Inst().GetDefaultFont();
    HGDIOBJ old = (f != NULL) ? ::SelectObject(dc, f) : NULL;
    SIZE sz = { 0, 0 };
    ::GetTextExtentPoint32W(dc, text, (int)wcslen(text), &sz);
    if (old != NULL)
    {
        ::SelectObject(dc, old);
    }
    ::ReleaseDC(NULL, dc);
    return sz.cx;
}

// 造一串等宽空格，使其在默认字体下的渲染宽 >= widthPx。塞进基类 rightText 占位
// （空格不可见），让基类据此把左侧标题 / 预览裁剪在右侧列左边、不顶进来。
CString BuildSpacerForWidth(int widthPx)
{
    if (widthPx <= 0)
    {
        return CString();
    }
    int spaceW = MeasureDefaultTextWidth(L" ");
    if (spaceW <= 0)
    {
        spaceW = 4;   // 取不到字体时退化为经验值（约 4px / 空格）
    }
    int count = widthPx / spaceW + 2;   // 向上取整再多留 1 个，确保够宽
    CString s;
    for (int i = 0; i < count; ++i)
    {
        s += L' ';
    }
    return s;
}

// 用基类同款默认字体 / 灰色，在矩形内右对齐 + 底对齐画上排右侧文字（会话行的时间 /
// 群行的成员数）—— 与基类两行节点的主标签同一垂直带（DT_BOTTOM 贴 mid），从而与
// 左侧的姓名 / 群名同高。
void DrawRightTopText(HDC hdc, LPCWSTR text, const RECT& rc, COLORREF color)
{
    if (text == NULL || text[0] == L'\0')
    {
        return;
    }
    HFONT f = balloonwjui::DuiResMgr::Inst().GetDefaultFont();
    HFONT oldFont = (f != NULL) ? (HFONT)::SelectObject(hdc, f) : NULL;
    int      oldBkMode = ::SetBkMode(hdc, TRANSPARENT);
    COLORREF oldColor  = ::SetTextColor(hdc, color);

    RECT r = rc;
    ::DrawTextW(hdc, text, -1, &r,
                DT_RIGHT | DT_BOTTOM | DT_SINGLELINE | DT_NOPREFIX);

    ::SetTextColor(hdc, oldColor);
    ::SetBkMode(hdc, oldBkMode);
    if (oldFont != NULL)
    {
        ::SelectObject(hdc, oldFont);
    }
}

// 一个待绘红点：外形矩形 + 显示文字 + 是否免打扰（决定底色红 / 灰）。OnPaint 先一遍
// 算好，再分两遍（GDI+ 填底、GDI 写字）绘制，避免在同一 HDC 上交错混用 GDI+ 与 GDI。
struct BadgeDraw
{
    RECT    m_rc;      // 红点外形矩形（客户区坐标）
    CString m_text;    // 红点内显示的数字文字
    bool    m_muted;   // 该会话是否免打扰：true 用灰底，false 用红底

    BadgeDraw()
        : m_muted(false)
    {
        m_rc.left   = 0;
        m_rc.top    = 0;
        m_rc.right  = 0;
        m_rc.bottom = 0;
    }
};

} // anonymous namespace

MainPanelTreeView::MainPanelTreeView()
{
    // 滚轮 / 角标外的默认行为继承自 DuiTreeView,无额外初始化。
}

void MainPanelTreeView::SetItemRightTopText(int id, LPCTSTR text)
{
    CString t = (text != NULL) ? text : _T("");
    if (t.IsEmpty())
    {
        // 空文字不留空条目：只用到这一个能力的树（如群树）在人数全未知时映射恒空，
        // OnPaint 仍能走短路分支。
        m_rightTopText.erase(id);
    }
    else
    {
        m_rightTopText[id] = t;
    }

    UpdateRightReserve_(id);
    Invalidate();
}

void MainPanelTreeView::SetItemRightStacked(int id, LPCTSTR timeText, int badgeCount)
{
    // 先落红点数据：下面的 SetItemRightTopText 会重算右侧预留宽，而预留宽要同时
    // 考虑上排时间与下排红点，红点晚一步写就会按旧值算宽。
    if (badgeCount > 0)
    {
        m_badges[id] = badgeCount;
    }
    else
    {
        m_badges.erase(id);
    }

    // 时间与群成员数走的是同一条上排通路，交给它统一处理（内部会重算预留宽并重绘）。
    SetItemRightTopText(id, timeText);
}

void MainPanelTreeView::SetItemConversationFlags(int id, bool pinned, bool muted)
{
    if (pinned)
    {
        m_pinnedRows.insert(id);
    }
    else
    {
        m_pinnedRows.erase(id);
    }

    if (muted)
    {
        m_mutedRows.insert(id);
    }
    else
    {
        m_mutedRows.erase(id);
    }

    // 免打扰会在未读胶囊左边多出一个静音图标，右下角占位变宽，须重算预留宽，
    // 否则左侧的预览文字会顶到图标底下。
    UpdateRightReserve_(id);
    Invalidate();
}

void MainPanelTreeView::ClearAllRowDecorations()
{
    if (m_rightTopText.empty() && m_badges.empty()
        && m_pinnedRows.empty() && m_mutedRows.empty())
    {
        return;
    }
    m_rightTopText.clear();
    m_badges.clear();
    m_pinnedRows.clear();
    m_mutedRows.clear();
    Invalidate();
}

void MainPanelTreeView::UpdateRightReserve_(int id)
{
    // 上排宽 = 上排右侧文字宽（会话树的时间 / 群树的成员数，用基类默认字体量）。
    int topW = 0;
    std::map<int, CString>::const_iterator itTop = m_rightTopText.find(id);
    if (itTop != m_rightTopText.end())
    {
        topW = MeasureDefaultTextWidth((LPCWSTR)itTop->second);
    }

    // 下排宽 = 未读胶囊宽（按位数算）+ 免打扰时的静音图标宽与间距。
    int bottomW = 0;
    std::map<int, int>::const_iterator itBadge = m_badges.find(id);
    if (itBadge != m_badges.end() && itBadge->second > 0)
    {
        CString badgeText = FormatBadgeText(itBadge->second);
        bottomW = ComputeBadgeWidth(badgeText.GetLength(),
                                    kBadgeDiameter, kBadgeExtraPerChar);
    }
    if (m_mutedRows.count(id) != 0)
    {
        bottomW += kMuteIconSize;
        if (bottomW > kMuteIconSize)
        {
            bottomW += kMuteIconGap;   // 图标与胶囊之间的间距，只在两者都在时才需要
        }
    }

    // 换算成等宽空格塞进基类 rightText，让基类把左侧标题 / 预览裁剪在右侧列左边。
    int reserveW = (topW > bottomW) ? topW : bottomW;
    SetItemRightText(id, BuildSpacerForWidth(reserveW));
}

void MainPanelTreeView::OnPaint(HDC hdc, const RECT& rcDirty)
{
    // 先让基类把整行其余内容画完（行底色 / 头像 / 标题 / 预览）。被本类接管的行，
    // 基类右侧文字是占位空格、不可见；未接管的行（如联系人树）右侧文字仍由基类正常绘制。
    DuiTreeView::OnPaint(hdc, rcDirty);

    // 一样都没接管的树（联系人树）直接短路，零开销。
    if (m_rightTopText.empty() && m_badges.empty()
        && m_pinnedRows.empty() && m_mutedRows.empty())
    {
        return;
    }

    const int rowHeight = GetRowHeight();
    if (rowHeight <= 0)
    {
        return;
    }

    const int rightEdge    = m_rcItem.right - kBadgeRightPad;
    const COLORREF topTextClr = GetRightTextColor();   // 与基类右侧文字同色（默认灰）
    const int visibleCount = GetVisibleCount();

    // ---- 第一遍：遍历可见行，收集置顶行底色 / 画上排时间 / 收集下排静音图标与红点 ----
    std::vector<RECT>      pinnedRects;   // 待覆盖淡暗色的置顶行矩形
    std::vector<RECT>      muteRects;     // 待画静音图标的矩形
    std::vector<BadgeDraw> draws;
    for (int row = 0; row < visibleCount; ++row)
    {
        int nodeId = GetIdAtVisibleRow(row);

        // 复算该行矩形 —— 与 DuiTreeView 单列绘制同口径：m_rcItem.top + r*rowHeight。
        RECT rcRow;
        rcRow.left   = m_rcItem.left;
        rcRow.right  = m_rcItem.right;
        rcRow.top    = m_rcItem.top + row * rowHeight;
        rcRow.bottom = rcRow.top + rowHeight;

        // 不与脏区相交的行跳过（基类没重绘它，hdc 也被裁掉，省一次绘制）。
        if (rcRow.bottom <= rcDirty.top || rcRow.top >= rcDirty.bottom)
        {
            continue;
        }

        const int  mid   = (rcRow.top + rcRow.bottom) / 2;
        const bool muted = (m_mutedRows.count(nodeId) != 0);

        // 置顶行：整行覆一层淡暗色。这里只收集矩形，实际填充统一放到第二遍的 GDI+
        // 块里做，避免在同一 HDC 上与下面的 GDI 文字绘制交错。
        if (m_pinnedRows.count(nodeId) != 0)
        {
            pinnedRects.push_back(rcRow);
        }

        // 上排右侧文字（会话行的时间 / 群行的成员数）：与主标签同一垂直带
        // （基类主标签画在 [top, mid] 内、DT_BOTTOM）。
        std::map<int, CString>::const_iterator itTop = m_rightTopText.find(nodeId);
        if (itTop != m_rightTopText.end() && !itTop->second.IsEmpty())
        {
            RECT rcTop = { rcRow.left, rcRow.top, rightEdge, mid };
            DrawRightTopText(hdc, (LPCWSTR)itTop->second, rcTop, topTextClr);
        }

        // 下排：静音图标 + 未读胶囊，右对齐排列，与预览同高（基类预览画在
        // [mid, bottom] 内、DT_TOP）。免打扰时胶囊改灰底。
        const int centerY = mid + kBadgePreviewCenterOffset;
        int bottomRight = rightEdge;   // 下排从右往左排布的游标

        std::map<int, int>::const_iterator itBadge = m_badges.find(nodeId);
        if (itBadge != m_badges.end() && itBadge->second > 0)
        {
            BadgeDraw d;
            d.m_text  = FormatBadgeText(itBadge->second);
            d.m_muted = muted;
            if (!d.m_text.IsEmpty())
            {
                int badgeWidth = ComputeBadgeWidth(d.m_text.GetLength(),
                                                   kBadgeDiameter, kBadgeExtraPerChar);
                d.m_rc = ComputeBadgeRect(bottomRight, badgeWidth, kBadgeDiameter, centerY);
                draws.push_back(d);
                bottomRight = d.m_rc.left - kMuteIconGap;   // 静音图标让到胶囊左边
            }
        }

        if (muted)
        {
            RECT rcMute;
            rcMute.right  = bottomRight;
            rcMute.left   = rcMute.right - kMuteIconSize;
            rcMute.top    = centerY - kMuteIconSize / 2;
            rcMute.bottom = rcMute.top + kMuteIconSize;
            muteRects.push_back(rcMute);
        }
    }

    if (pinnedRects.empty() && muteRects.empty() && draws.empty())
    {
        return;
    }

    // ---- 第二遍：GDI+ 抗锯齿画置顶行底色、胶囊底、静音图标 ----
    {
        Gdiplus::Graphics g(hdc);
        g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);

        // 置顶行底色必须先于胶囊 / 图标画，否则会把它们一起蒙上一层。
        {
            Gdiplus::SolidBrush tint(kPinnedRowTint);
            for (size_t i = 0; i < pinnedRects.size(); ++i)
            {
                const RECT& rc = pinnedRects[i];
                g.FillRectangle(&tint,
                                rc.left, rc.top,
                                rc.right - rc.left, rc.bottom - rc.top);
            }
        }

        // 左侧强调竖条 —— 置顶行的主要标记。画在底色之后：底色是覆盖整行的半透明
        // 层，先画竖条会被它蒙一层、橙色发暗。竖条本身不透明，压住底色不受影响。
        {
            Gdiplus::SolidBrush accent(kPinnedAccentColor);
            for (size_t i = 0; i < pinnedRects.size(); ++i)
            {
                const RECT rcBar = ComputePinnedAccentRect(
                    pinnedRects[i], kPinnedAccentWidth, kPinnedAccentHeightPercent);
                if (rcBar.right <= rcBar.left || rcBar.bottom <= rcBar.top)
                {
                    continue;   // 行高异常时算出空矩形，跳过
                }
                Gdiplus::GraphicsPath barPath;
                AddRoundRectPath(barPath,
                                 (Gdiplus::REAL)rcBar.left, (Gdiplus::REAL)rcBar.top,
                                 (Gdiplus::REAL)(rcBar.right - rcBar.left),
                                 (Gdiplus::REAL)(rcBar.bottom - rcBar.top),
                                 kPinnedAccentRadius);
                g.FillPath(&accent, &barPath);
            }
        }

        // 未读胶囊（单字符即正圆）：免打扰用灰底，其余用红底。
        Gdiplus::SolidBrush redBrush(kBadgeRed);
        Gdiplus::SolidBrush grayBrush(kBadgeMutedGray);
        for (size_t i = 0; i < draws.size(); ++i)
        {
            const RECT& rc = draws[i].m_rc;
            Gdiplus::GraphicsPath path;
            AddRoundRectPath(path,
                             (Gdiplus::REAL)rc.left, (Gdiplus::REAL)rc.top,
                             (Gdiplus::REAL)(rc.right - rc.left),
                             (Gdiplus::REAL)(rc.bottom - rc.top),
                             (Gdiplus::REAL)kBadgeDiameter / 2.0f);
            g.FillPath(draws[i].m_muted ? &grayBrush : &redBrush, &path);
        }

        // 静音图标：与右侧文字同色（默认灰），描边风格与菜单图标同源。
        const Gdiplus::Color muteColor(255, GetRValue(topTextClr),
                                       GetGValue(topTextClr), GetBValue(topTextClr));
        for (size_t i = 0; i < muteRects.size(); ++i)
        {
            const RECT& rc = muteRects[i];
            DrawLineIcon(g, kIconMute,
                         (Gdiplus::REAL)rc.left, (Gdiplus::REAL)rc.top,
                         (Gdiplus::REAL)kMuteIconSize, muteColor, kMuteIconStroke);
        }
    }

    // ---- 第三遍：GDI 画白色数字（GDI+ 块已结束，不与 GDI 交错）----
    for (size_t i = 0; i < draws.size(); ++i)
    {
        DrawBadgeText(hdc, (LPCWSTR)draws[i].m_text, draws[i].m_rc);
    }
}

bool MainPanelTreeView::OnMouseWheel(POINT pt, short zDelta, UINT mkFlags)
{
    // 多列模式:基类自带水平 / 垂直滚动消化,返回 true。
    // 单列模式:基类直接 return false(详见 DuiTreeView.cpp 中 OnMouseWheel
    //          的 !IsMultiCol 短路)。这条路径下沿父链找最近的 DuiScrollView
    //          子类把滚轮上抛 —— 主面板里这就是 MainPanelScrollView。
    if (DuiTreeView::OnMouseWheel(pt, zDelta, mkFlags))
    {
        return true;
    }
    return ForwardWheelToScrollView(this, pt, zDelta, mkFlags);
}

} // namespace balloonmain
