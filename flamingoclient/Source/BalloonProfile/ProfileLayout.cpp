/**
 *  ProfileLayout.cpp
 *
 *  个人信息对话框客户区控件树的构造实现。布局以内嵌 XML 常量为唯一
 *  真值源（随 exe 出货、0 部署步骤），交给 balloonui 的 DuiXmlBuilder
 *  解析。本文件还就地实现若干仅布局自用的小控件：白底根面板、单行文本、
 *  带铅笔角标的头像、状态胶囊、主 / 次操作按钮。
 *
 *  对话框分"查看 / 编辑"两态，两套 XML 各自构造一棵控件树（两态布局
 *  高度不同，故不共用一棵树做显隐）。
 *
 *  本 .cpp 不 include Flamingo 的 stdafx.h —— 它要被 Flamingo.vcxproj 与
 *  FlamingoUnit.vcxproj 共用。在 Flamingo.vcxproj 里需标记
 *  PrecompiledHeader=NotUsing。
 *
 *  balloonwj@qq.com   2026-05-22
 */

// ---- balloonui 头文件要求的 ATL/WTL 前置依赖（等价 NewChatDemo/stdafx.h）----
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

#include <string>
#include <map>
#include <stdlib.h>

#include <gdiplus.h>
#pragma comment(lib, "gdiplus.lib")

#include "ProfileLayout.h"
#include "ProfileModel.h"
#include "../../../third-party/balloonui/balloonui/Controls/Layout/DuiLayout.h"
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiButton.h"
#include "../../../third-party/balloonui/balloonui/Controls/Input/DuiEditHost.h"
#include "../../../third-party/balloonui/balloonui/Controls/Input/DuiComboBox.h"
#include "../../../third-party/balloonui/balloonui/DuiXmlBuilder.h"

namespace balloonprofile {

using balloonwjui::DuiControl;
using balloonwjui::DuiXmlBuilder;
using balloonwjui::DuiEditHost;
using balloonwjui::DuiComboBox;

// =====================================================================
// 几何常量（逻辑像素）—— 取自设计稿 screens.jsx::PersonalInfoDialog
//   与 components.jsx 的 Modal / FieldRow / Btn。
// =====================================================================
namespace {

// ---- 对话框整体 -------------------------------------------------------
//   左右内边距 20（Modal body padding 20）直接写在内嵌 XML 里。
const int kBodyPadTop       = 18;   // 客户区上内边距（Modal body padding 18）
const int kBodyPadBottom    = 18;   // 客户区下内边距（Modal body padding 18）

// ---- 头部区 -----------------------------------------------------------
// 头部行高取右侧信息列的内容自然高度（姓名 24 + 间距 4 + 账号 16 +
// 间距 6 + 状态胶囊 22 = 72），略高于 64 的头像 —— 头像在行内垂直居中。
const int kHeaderHeight     = 72;   // 头部行高（右侧信息列内容总高）
const int kHeaderPadBottom  = 14;   // 头部区下方留白（设计 paddingBottom 14）

// ---- 字段区 -----------------------------------------------------------
const int kFieldsPadV       = 10;   // 字段区上下内边距（设计 padding 10 0）
const int kFieldCount       = 6;    // 字段行数（昵称 / 签名 / 性别 / 生日 / 所在地 / 邮箱）
const int kFieldRowPadV     = 12;   // 单个字段行的上下内边距（FieldRow padding 12 0）
const int kViewValueHeight  = 22;   // 查看态值文本的内容高度
const int kEditValueHeight  = 32;   // 编辑态文本框的高度（BareInput height 32）
// 查看态 / 编辑态单行字段的总行高 = 上下 padding + 内容高度。
const int kViewRowHeight    = kFieldRowPadV * 2 + kViewValueHeight;   // 46
const int kEditRowHeight    = kFieldRowPadV * 2 + kEditValueHeight;   // 56

// ---- 底部按钮区 -------------------------------------------------------
//   左右内边距 18（设计 padding 12 18）直接写在内嵌 XML 里。
const int kFooterPadV       = 12;   // 底部按钮区上下内边距（设计 padding 12 18）
const int kBtnHeight        = 34;   // 按钮高度（Btn size md height 34）
const int kFooterHeight     = kFooterPadV * 2 + kBtnHeight;   // 58
const int kSeparatorHeight  = 1;    // 分隔线高度

} // anonymous namespace

namespace {

// =====================================================================
// 公共绘制小工具
// =====================================================================

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

// 用 GDI 绘制单行文字。
//   pixelHeight：字号（像素高度）；bold：是否半粗体；color：文字色；
//   dtFlags：DrawText 对齐与裁剪标志。
void DrawGdiText(HDC hdc, LPCWSTR text, const RECT& rc,
                 int pixelHeight, bool bold, COLORREF color, UINT dtFlags)
{
    if (text == NULL || text[0] == L'\0' || pixelHeight <= 0)
    {
        return;
    }
    HFONT hFont = ::CreateFontW(
        -pixelHeight, 0, 0, 0,
        bold ? FW_SEMIBOLD : FW_NORMAL,
        FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Microsoft YaHei");
    if (hFont == NULL)
    {
        return;
    }
    HFONT    hOldFont  = (HFONT)::SelectObject(hdc, hFont);
    int      oldBkMode = ::SetBkMode(hdc, TRANSPARENT);
    COLORREF oldColor  = ::SetTextColor(hdc, color);
    RECT r = rc;
    ::DrawTextW(hdc, text, -1, &r, dtFlags);
    ::SetTextColor(hdc, oldColor);
    ::SetBkMode(hdc, oldBkMode);
    ::SelectObject(hdc, hOldFont);
    ::DeleteObject(hFont);
}

// 测量单行文字宽度（用于状态胶囊按文字宽度自适应）。
int MeasureTextWidth(HDC hdc, LPCWSTR text, int pixelHeight, bool bold)
{
    if (text == NULL || text[0] == L'\0')
    {
        return 0;
    }
    HFONT hFont = ::CreateFontW(
        -pixelHeight, 0, 0, 0,
        bold ? FW_SEMIBOLD : FW_NORMAL,
        FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Microsoft YaHei");
    if (hFont == NULL)
    {
        return 0;
    }
    HFONT hOldFont = (HFONT)::SelectObject(hdc, hFont);
    SIZE sz = { 0, 0 };
    ::GetTextExtentPoint32W(hdc, text, (int)::wcslen(text), &sz);
    ::SelectObject(hdc, hOldFont);
    ::DeleteObject(hFont);
    return sz.cx;
}

// 在指定区域内绘制"铅笔"线性图标。形状取自 design icons.jsx 的 edit：
//   主体 path "M4 20h4l11-11-4-4L4 16z"，笔尖横线 "M14 6l4 4"，viewBox 24。
// 先把 viewBox 映射到 [x,y,size] 方块再描边。
void DrawEditPencil(Gdiplus::Graphics& g, Gdiplus::REAL x, Gdiplus::REAL y,
                    Gdiplus::REAL size, const Gdiplus::Color& color,
                    Gdiplus::REAL strokeWidth)
{
    if (size <= 0.0f)
    {
        return;
    }
    const Gdiplus::REAL kViewBox = 24.0f;   // icons.jsx 统一 viewBox

    Gdiplus::GraphicsState st = g.Save();
    g.TranslateTransform(x, y);
    g.ScaleTransform(size / kViewBox, size / kViewBox);

    Gdiplus::Pen pen(color, strokeWidth);
    pen.SetStartCap(Gdiplus::LineCapRound);
    pen.SetEndCap(Gdiplus::LineCapRound);
    pen.SetLineJoin(Gdiplus::LineJoinRound);

    // 铅笔主体：闭合多边形（笔杆 + 笔头）。
    Gdiplus::GraphicsPath body;
    body.AddLine(4.0f,  20.0f, 8.0f,  20.0f);
    body.AddLine(8.0f,  20.0f, 19.0f, 9.0f);
    body.AddLine(19.0f, 9.0f,  15.0f, 5.0f);
    body.AddLine(15.0f, 5.0f,  4.0f,  16.0f);
    body.CloseFigure();
    g.DrawPath(&pen, &body);

    // 笔尖处的橡皮 / 金属箍横线。
    g.DrawLine(&pen, 14.0f, 6.0f, 18.0f, 10.0f);

    g.Restore(st);
}

// =====================================================================
// 主题配色（取自设计稿主题色板）
// =====================================================================

const Gdiplus::Color kAccent     (255, 0xFF, 0x7A, 0x45);  // accent 橙
const Gdiplus::Color kAccentSoft (255, 0xFF, 0xE6, 0xD6);  // accent-soft 浅橙
const COLORREF kColorText     = RGB(0x1A, 0x1A, 0x1A);     // 主文字
const COLORREF kColorMuted    = RGB(0x6B, 0x6B, 0x6B);     // 次级文字
const COLORREF kColorAccent   = RGB(0xFF, 0x7A, 0x45);     // 强调橙（文字用）
const COLORREF kColorBorder   = RGB(0xED, 0xED, 0xED);     // 分隔线 / 边框
const COLORREF kColorWhite    = RGB(0xFF, 0xFF, 0xFF);     // 白

// =====================================================================
// 布局自用小控件
// =====================================================================

/**
 *  ProfileRootPanel —— 个人信息对话框客户区根面板。铺纯白底，再交基类
 *  （DuiVBox）纵向排布子控件。
 */
class ProfileRootPanel : public balloonwjui::DuiVBox
{
public:
    void OnPaint(HDC hdc, const RECT& rcDirty) override
    {
        HBRUSH white = ::CreateSolidBrush(kColorWhite);
        ::FillRect(hdc, &m_rcItem, white);
        ::DeleteObject(white);
        DuiVBox::OnPaint(hdc, rcDirty);
    }
};

/**
 *  ProfileSeparator —— 1px 横向分隔线（设计稿 var(--border) 色）。
 *  纯绘制控件。
 */
class ProfileSeparator : public balloonwjui::DuiControl
{
public:
    void OnPaint(HDC hdc, const RECT& /*rcDirty*/) override
    {
        // 只画顶部那 1px —— 控件本身高度即 1，直接整块填充。
        HBRUSH line = ::CreateSolidBrush(kColorBorder);
        ::FillRect(hdc, &m_rcItem, line);
        ::DeleteObject(line);
    }

    bool OnSetCursor(POINT /*pt*/) override { return false; }
};

/**
 *  ProfileText —— 单行文本控件。字号 / 颜色 / 粗细 / 对齐由 XML 属性决定，
 *  用于字段标签、字段值（查看态）、姓名、账号等。纯绘制控件。
 */
class ProfileText : public balloonwjui::DuiControl
{
public:
    ProfileText()
        : m_size(13)
        , m_color(kColorText)
        , m_bold(false)
        , m_align(DT_LEFT)
    {
    }

    // 配置文本内容与样式。
    //   text：文本（nullptr 视为空）；size：字号像素；color：文字色；
    //   bold：是否半粗体；align：水平对齐（DT_LEFT / DT_CENTER / DT_RIGHT）。
    void Configure(LPCTSTR text, int size, COLORREF color, bool bold, UINT align)
    {
        m_text  = (text != NULL) ? text : _T("");
        m_size  = size;
        m_color = color;
        m_bold  = bold;
        m_align = align;
    }

    // 仅更新文本内容（样式不变）。窗口侧把模型值灌进查看态时调用。
    void SetTextValue(LPCTSTR text)
    {
        m_text = (text != NULL) ? text : _T("");
        Invalidate();
    }

    void OnPaint(HDC hdc, const RECT& /*rcDirty*/) override
    {
        DrawGdiText(hdc, (LPCWSTR)m_text, m_rcItem, m_size, m_bold, m_color,
                    m_align | DT_VCENTER | DT_SINGLELINE
                            | DT_NOPREFIX | DT_END_ELLIPSIS);
    }

    bool OnSetCursor(POINT /*pt*/) override { return false; }

private:
    CString  m_text;    // 文本内容
    int      m_size;    // 字号（像素）
    COLORREF m_color;   // 文字颜色
    bool     m_bold;    // 是否半粗体
    UINT     m_align;   // 水平对齐
};

/**
 *  ProfileAvatar —— 个人信息对话框头部的头像控件。圆角矩形底色 +
 *  字母占位 + 右下角铅笔角标（accent 圆底白图标，带 2px 白边）。
 *  继承 DuiButton 复用点击 / DUIN_CLICK 通知（点击头像 = 换头像入口）。
 */
class ProfileAvatar : public balloonwjui::DuiButton
{
public:
    ProfileAvatar()
        : m_letter(_T("L"))
    {
    }

    // 设置字母占位（一般取姓名首字）。nullptr / 空串视为不画字母。
    void SetLetter(LPCTSTR letter)
    {
        m_letter = (letter != NULL) ? letter : _T("");
        Invalidate();
    }

    void OnPaint(HDC hdc, const RECT& /*rcDirty*/) override
    {
        const int ctrlW = m_rcItem.right  - m_rcItem.left;
        const int ctrlH = m_rcItem.bottom - m_rcItem.top;
        if (ctrlW <= 0 || ctrlH <= 0)
        {
            return;
        }

        // 控件矩形比头像本身略大（kAvatarBox），右下角预留 kBadgeOffset
        // 给角标外溢 —— 否则角标会被 host 按 m_rcItem 裁掉。头像方块画
        // 在控件矩形的左上角。
        const int kAvatarBox  = 64;   // 头像方块边长（设计 size 64）
        const int kBadgeSize  = 24;   // 角标直径（设计 width/height 24）
        const int kBadgeBorder = 2;   // 角标白边宽度（设计 border 2px）
        const int kBadgeOffset = 4;   // 角标相对头像右下外溢量（bottom/right -4）

        const int x = m_rcItem.left;
        const int y = m_rcItem.top;
        const int w = kAvatarBox;
        const int h = kAvatarBox;

        Gdiplus::Graphics g(hdc);
        g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);

        // ---- 头像方块：accent 底 + 圆角 22%（取边长 22%）----
        const Gdiplus::REAL kRoundedRatio = 0.22f;   // 设计 rounded="22%"
        const Gdiplus::REAL radius = (Gdiplus::REAL)w * kRoundedRatio;
        Gdiplus::GraphicsPath square;
        AddRoundRect(square, (Gdiplus::REAL)x, (Gdiplus::REAL)y,
                     (Gdiplus::REAL)w, (Gdiplus::REAL)h, radius);
        Gdiplus::SolidBrush bg(kAccent);
        g.FillPath(&bg, &square);

        // ---- 字母占位（白色、字号约头像边长 42%）----
        const int letterPx = (int)((Gdiplus::REAL)w * 0.42f);
        RECT letterRc = { x, y, x + w, y + h };
        DrawGdiText(hdc, (LPCWSTR)m_letter, letterRc, letterPx, true,
                    kColorWhite,
                    DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

        // ---- 右下角铅笔角标：24×24 accent 圆 + 2px 白边 + 白铅笔 ----
        // 角标圆心：头像右下角向外偏移 kBadgeOffset。
        const Gdiplus::REAL bcx = (Gdiplus::REAL)(x + w + kBadgeOffset - kBadgeSize / 2);
        const Gdiplus::REAL bcy = (Gdiplus::REAL)(y + h + kBadgeOffset - kBadgeSize / 2);
        // 白边底圆。
        {
            Gdiplus::SolidBrush whiteRing(Gdiplus::Color(255, 0xFF, 0xFF, 0xFF));
            const Gdiplus::REAL d = (Gdiplus::REAL)kBadgeSize;
            g.FillEllipse(&whiteRing, bcx - d / 2.0f, bcy - d / 2.0f, d, d);
        }
        // accent 内圆。
        {
            Gdiplus::SolidBrush accent(kAccent);
            const Gdiplus::REAL d = (Gdiplus::REAL)(kBadgeSize - kBadgeBorder * 2);
            g.FillEllipse(&accent, bcx - d / 2.0f, bcy - d / 2.0f, d, d);
        }
        // 白色铅笔图标（设计 size 12，居中于角标内圆）。
        {
            const Gdiplus::REAL kGlyph = 12.0f;       // 角标内铅笔尺寸
            const Gdiplus::REAL kStroke = 2.0f;       // 铅笔描边宽度（viewBox 24 基准）
            DrawEditPencil(g, bcx - kGlyph / 2.0f, bcy - kGlyph / 2.0f,
                           kGlyph, Gdiplus::Color(255, 0xFF, 0xFF, 0xFF), kStroke);
        }
    }

    bool OnSetCursor(POINT /*pt*/) override { return false; }

private:
    CString m_letter;   // 字母占位（姓名首字）
};

/**
 *  ProfileStatusPill —— 一个在线状态胶囊（在线 / 离开 / 勿扰）。
 *  胶囊：高 22、全圆角、左右内边距 8；内部左侧 6×6 状态色圆点 + 右侧
 *  中文文字。选中态 accent-soft 底 + accent 粗体字；未选中 rgba(0,0,0,.04)
 *  底 + text-muted 字。继承 DuiButton 复用点击 / DUIN_CLICK 通知。
 */
class ProfileStatusPill : public balloonwjui::DuiButton
{
public:
    ProfileStatusPill()
        : m_status(kProfileStatusOnline)
        , m_selected(false)
    {
    }

    // 设置本胶囊代表的在线状态（取值见 ProfileStatus）。会触发重绘。
    void SetStatusKind(int status)
    {
        m_status = status;
        Invalidate();
    }
    int GetStatusKind() const { return m_status; }

    // 设置选中态（选中：accent-soft 底 + accent 粗体字）。会触发重绘。
    void SetPillSelected(bool selected)
    {
        if (m_selected != selected)
        {
            m_selected = selected;
            Invalidate();
        }
    }
    bool IsPillSelected() const { return m_selected; }

    void OnPaint(HDC hdc, const RECT& /*rcDirty*/) override
    {
        const int x = m_rcItem.left;
        const int y = m_rcItem.top;
        const int w = m_rcItem.right  - m_rcItem.left;
        const int h = m_rcItem.bottom - m_rcItem.top;
        if (w <= 0 || h <= 0)
        {
            return;
        }

        Gdiplus::Graphics g(hdc);
        g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);

        // ---- 胶囊底（全圆角，半径 = 高度一半）----
        {
            Gdiplus::GraphicsPath pill;
            AddRoundRect(pill, (Gdiplus::REAL)x, (Gdiplus::REAL)y,
                         (Gdiplus::REAL)w, (Gdiplus::REAL)h,
                         (Gdiplus::REAL)h / 2.0f);
            Gdiplus::Color fill = m_selected
                ? kAccentSoft
                // 未选中：rgba(0,0,0,.04) —— 半透明黑，约 10/255 alpha。
                : Gdiplus::Color(10, 0, 0, 0);
            Gdiplus::SolidBrush brush(fill);
            g.FillPath(&brush, &pill);
        }

        // ---- 左侧 6×6 状态色圆点 ----
        const int kDotSize  = 6;    // 状态圆点直径（设计 6×6）
        const int kPadLR    = 8;    // 胶囊左右内边距（设计 padding 0 8）
        const int kDotTextGap = 4;  // 圆点与文字间距（设计 gap 4）
        const COLORREF dotClr = ProfileStatusColor(m_status);
        const int dotX = x + kPadLR;
        const int dotY = y + (h - kDotSize) / 2;
        {
            Gdiplus::SolidBrush dot(Gdiplus::Color(255,
                GetRValue(dotClr), GetGValue(dotClr), GetBValue(dotClr)));
            g.FillEllipse(&dot, (Gdiplus::REAL)dotX, (Gdiplus::REAL)dotY,
                          (Gdiplus::REAL)kDotSize, (Gdiplus::REAL)kDotSize);
        }

        // ---- 右侧中文文字（字号 11）----
        const int kTextSize = 11;   // 胶囊文字字号（设计 fontSize 11）
        RECT txtRc = { dotX + kDotSize + kDotTextGap, y,
                       x + w - kPadLR, y + h };
        const COLORREF txtClr = m_selected ? kColorAccent : kColorMuted;
        DrawGdiText(hdc, (LPCWSTR)ProfileStatusText(m_status), txtRc,
                    kTextSize, m_selected, txtClr,
                    DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    }

    bool OnSetCursor(POINT /*pt*/) override { return false; }

private:
    int  m_status;     // 代表的在线状态，见 ProfileStatus
    bool m_selected;   // 是否选中
};

/**
 *  ProfileActionButton —— 个人信息对话框底部的操作按钮。primary=true 为
 *  橙色实心主按钮（编辑资料 / 保存），false 为透明底描边次按钮
 *  （关闭 / 取消）。可选在文字左侧画铅笔图标（"编辑资料"按钮用）。
 *  继承 DuiButton 复用点击 / 通知，只覆写 OnPaint。
 */
class ProfileActionButton : public balloonwjui::DuiButton
{
public:
    ProfileActionButton()
        : m_primary(false)
        , m_withPencil(false)
    {
    }

    // primary：true = 橙色主按钮；false = 透明底描边次按钮。
    void SetPrimary(bool primary)
    {
        m_primary = primary;
        Invalidate();
    }

    // withPencil：true = 文字左侧画一个铅笔图标（仅"编辑资料"按钮用）。
    void SetWithPencil(bool withPencil)
    {
        m_withPencil = withPencil;
        Invalidate();
    }

    void OnPaint(HDC hdc, const RECT& /*rcDirty*/) override
    {
        const int x = m_rcItem.left;
        const int y = m_rcItem.top;
        const int w = m_rcItem.right  - m_rcItem.left;
        const int h = m_rcItem.bottom - m_rcItem.top;
        if (w <= 0 || h <= 0)
        {
            return;
        }

        Gdiplus::Graphics g(hdc);
        g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);

        // 按钮圆角 10（设计 Btn size md borderRadius 10）。
        const Gdiplus::REAL kBtnRadius = 10.0f;
        Gdiplus::GraphicsPath path;
        AddRoundRect(path, (Gdiplus::REAL)x + 0.5f, (Gdiplus::REAL)y + 0.5f,
                     (Gdiplus::REAL)w - 1.0f, (Gdiplus::REAL)h - 1.0f, kBtnRadius);

        COLORREF textColor;
        if (m_primary)
        {
            // 橙色实心主按钮（hover 略深）。
            const Gdiplus::Color fill = IsHover()
                ? Gdiplus::Color(255, 0xFF, 0x6A, 0x30)
                : kAccent;
            Gdiplus::SolidBrush brush(fill);
            g.FillPath(&brush, &path);
            textColor = kColorWhite;
        }
        else
        {
            // 透明底描边次按钮（hover 浅灰底）。
            if (IsHover())
            {
                Gdiplus::SolidBrush brush(Gdiplus::Color(255, 0xF6, 0xF5, 0xF2));
                g.FillPath(&brush, &path);
            }
            // ghost 描边色 = border-strong，比 border 略深。
            Gdiplus::Pen border(Gdiplus::Color(255, 0xD9, 0xD9, 0xD9), 1.0f);
            g.DrawPath(&border, &path);
            textColor = kColorText;
        }

        // 文字（字号 13.5 → 取 14 像素近似，半粗体）。
        const int kBtnTextSize = 14;
        const Gdiplus::Color glyphColor(255,
            GetRValue(textColor), GetGValue(textColor), GetBValue(textColor));

        if (m_withPencil)
        {
            // "编辑资料"：铅笔图标 + 文字整体水平居中。图标 size 13、
            // 图标与文字间距 6（设计 Btn gap 6）。
            const int kIconSize = 13;   // 按钮内铅笔尺寸（设计 size 13）
            const int kIconGap  = 6;    // 图标与文字间距
            const int textW = MeasureTextWidth(hdc, (LPCWSTR)GetText(),
                                               kBtnTextSize, true);
            const int groupW = kIconSize + kIconGap + textW;
            const int groupX = x + (w - groupW) / 2;
            const int iconY  = y + (h - kIconSize) / 2;
            DrawEditPencil(g, (Gdiplus::REAL)groupX, (Gdiplus::REAL)iconY,
                           (Gdiplus::REAL)kIconSize, glyphColor, 2.0f);
            RECT txtRc = { groupX + kIconSize + kIconGap, y, x + w, y + h };
            DrawGdiText(hdc, (LPCWSTR)GetText(), txtRc, kBtnTextSize, true,
                        textColor,
                        DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        }
        else
        {
            DrawGdiText(hdc, (LPCWSTR)GetText(), m_rcItem, kBtnTextSize, true,
                        textColor,
                        DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        }
    }

    bool OnSetCursor(POINT /*pt*/) override { return false; }

private:
    bool m_primary;      // true=橙色主按钮，false=透明底次按钮
    bool m_withPencil;   // true=文字左侧带铅笔图标
};

// =====================================================================
// XML 属性解析
// =====================================================================

// 读取节点属性值；属性不存在时返回 def。
std::string GetNodeAttr(const DuiXmlBuilder::Node& n, const char* key,
                        const char* def = "")
{
    std::map<std::string, std::string>::const_iterator it = n.attrs.find(key);
    return (it != n.attrs.end()) ? it->second : std::string(def);
}

// 把 UTF-8 字节串转成 CString（宽字符）。内嵌 XML 是 u8 字面量（UTF-8），
// 属性里的中文必须走本函数 —— 不能用 CA2T：CA2T 按系统 ANSI 代码页
// （中文系统为 GBK）解释，会把 UTF-8 字节当 GBK 读，导致界面文字乱码。
CString Utf8ToCString(const std::string& utf8)
{
    if (utf8.empty())
    {
        return CString();
    }
    const int wlen = ::MultiByteToWideChar(
        CP_UTF8, 0, utf8.c_str(), (int)utf8.size(), NULL, 0);
    if (wlen <= 0)
    {
        return CString();
    }
    CString result;
    LPWSTR buf = result.GetBuffer(wlen);
    ::MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), (int)utf8.size(),
                          buf, wlen);
    result.ReleaseBuffer(wlen);
    return result;
}

// 把 "r,g,b" 解析成 COLORREF；解析失败返回 def。
COLORREF ParseColor(const std::string& s, COLORREF def)
{
    int rgb[3] = { 0, 0, 0 };
    int idx = 0;
    int cur = 0;
    bool hasDigit = false;
    for (size_t i = 0; i <= s.size() && idx < 3; ++i)
    {
        const char c = (i < s.size()) ? s[i] : ',';
        if (c >= '0' && c <= '9')
        {
            cur = cur * 10 + (c - '0');
            hasDigit = true;
        }
        else if (c == ',')
        {
            if (!hasDigit)
            {
                return def;
            }
            if (cur > 255)
            {
                cur = 255;
            }
            rgb[idx] = cur;
            ++idx;
            cur = 0;
            hasDigit = false;
        }
    }
    if (idx < 3)
    {
        return def;
    }
    return RGB(rgb[0], rgb[1], rgb[2]);
}

// 把 profile-text 的 align 属性翻译成 DrawText 对齐标志。
UINT TextAlignFromAttr(const std::string& align)
{
    if (align == "center")
    {
        return DT_CENTER;
    }
    if (align == "right")
    {
        return DT_RIGHT;
    }
    return DT_LEFT;
}

// 把 profile-pill 的 status 属性翻译成 ProfileStatus。
int StatusKindFromAttr(const std::string& s)
{
    if (s == "away")
    {
        return kProfileStatusAway;
    }
    if (s == "busy")
    {
        return kProfileStatusBusy;
    }
    //其余取值（含老布局里写过的 "invisible"，该状态已去掉）一律按"在线"处理
    return kProfileStatusOnline;
}

// =====================================================================
// 自定义标签工厂
// =====================================================================
//
// 接 profile-root / profile-sep / profile-text / profile-avatar /
// profile-pill / profile-actionbtn / profile-combo / control；其余标签
// （vbox / hbox / edit）走 builder 内置处理。
std::unique_ptr<DuiControl> CreateProfileCustomControl(const DuiXmlBuilder::Node& n)
{
    if (n.tag == "profile-root")
    {
        return std::unique_ptr<DuiControl>(new ProfileRootPanel());
    }

    if (n.tag == "profile-sep")
    {
        return std::unique_ptr<DuiControl>(new ProfileSeparator());
    }

    if (n.tag == "profile-text")
    {
        std::unique_ptr<ProfileText> text(new ProfileText());
        const CString     strText = Utf8ToCString(GetNodeAttr(n, "text"));
        const std::string strSize = GetNodeAttr(n, "size", "13");
        const COLORREF color = ParseColor(GetNodeAttr(n, "color"), kColorText);
        const bool bold  = (GetNodeAttr(n, "bold") == "true");
        const UINT align = TextAlignFromAttr(GetNodeAttr(n, "align"));
        text->Configure(strText, atoi(strSize.c_str()), color, bold, align);
        return std::unique_ptr<DuiControl>(text.release());
    }

    if (n.tag == "profile-avatar")
    {
        std::unique_ptr<ProfileAvatar> avatar(new ProfileAvatar());
        const std::string letter = GetNodeAttr(n, "letter", "L");
        avatar->SetLetter(Utf8ToCString(letter));
        return std::unique_ptr<DuiControl>(avatar.release());
    }

    if (n.tag == "profile-pill")
    {
        std::unique_ptr<ProfileStatusPill> pill(new ProfileStatusPill());
        pill->SetStatusKind(StatusKindFromAttr(GetNodeAttr(n, "status")));
        pill->SetPillSelected(GetNodeAttr(n, "selected") == "true");
        return std::unique_ptr<DuiControl>(pill.release());
    }

    if (n.tag == "profile-actionbtn")
    {
        std::unique_ptr<ProfileActionButton> btn(new ProfileActionButton());
        btn->SetText(Utf8ToCString(GetNodeAttr(n, "text")));
        btn->SetPrimary(GetNodeAttr(n, "primary") == "true");
        btn->SetWithPencil(GetNodeAttr(n, "pencil") == "true");
        return std::unique_ptr<DuiControl>(btn.release());
    }

    if (n.tag == "profile-combo")
    {
        // 性别下拉框 —— DuiComboBox 不支持原生 XML，故在工厂里建。
        std::unique_ptr<DuiComboBox> combo(new DuiComboBox());
        combo->SetEditable(false);          // 只读下拉（不可手输入）
        combo->AddString(_T("女"));
        combo->AddString(_T("男"));
        combo->AddString(_T("保密"));
        combo->SetCurSel(0, false);         // 默认选中"女"，不发通知
        combo->SetItemHeight(24);           // 下拉项行高
        return std::unique_ptr<DuiControl>(combo.release());
    }

    if (n.tag == "control")
    {
        return std::unique_ptr<DuiControl>(new DuiControl());
    }

    return nullptr;
}

// =====================================================================
// 内嵌 XML 布局
// =====================================================================
//
// 头部区（两态共用）：头像 + 姓名 / 账号 / 四个状态胶囊。
// 字段区：六行 FieldRow（标签 + 值），查看态值为只读文本、编辑态值为
//   文本框 / 下拉框。
// 底部按钮区：查看态"关闭 + 编辑资料"、编辑态"取消 + 保存"。
//
// 头部行高 64、字段区单行高度按模式不同（kViewRowHeight / kEditRowHeight），
// 故查看 / 编辑各一份完整 XML。
// =====================================================================

// 查看态完整客户区 XML。
const char* const kProfileViewXml = u8R"xml(
<profile-root padding="0" gap="0">
  <vbox padding="20,18,20,18" gap="0">
    <hbox fixedHeight="72" gap="14">
      <profile-avatar id="900" letter="L" fixedWidth="70" fixedHeight="70"/>
      <vbox weight="1" gap="0">
        <profile-text text="林知夏" size="17" color="26,26,26" bold="true" fixedHeight="24"/>
        <hbox fixedHeight="4"/>
        <profile-text text="账号 88234561" size="12" color="107,107,107" fixedHeight="16"/>
        <hbox fixedHeight="6"/>
        <hbox fixedHeight="22" gap="6">
          <profile-pill id="910" status="online"    selected="true"  fixedWidth="58"/>
          <profile-pill id="911" status="away"      fixedWidth="58"/>
          <profile-pill id="912" status="busy"      fixedWidth="58"/>
          <control weight="1"/>
        </hbox>
      </vbox>
    </hbox>
    <hbox fixedHeight="14"/>
    <profile-sep fixedHeight="1"/>
    <vbox padding="0,10,0,10" gap="0">
      <hbox fixedHeight="46" gap="16">
        <profile-text text="昵称" size="13" color="26,26,26" fixedWidth="120"/>
        <profile-text id="920" text="林知夏" size="13" color="26,26,26" align="right" weight="1"/>
      </hbox>
      <profile-sep fixedHeight="1"/>
      <hbox fixedHeight="46" gap="16">
        <profile-text text="个性签名" size="13" color="26,26,26" fixedWidth="120"/>
        <profile-text id="921" text="想做一件让自己骄傲的事" size="13" color="107,107,107" align="right" weight="1"/>
      </hbox>
      <profile-sep fixedHeight="1"/>
      <hbox fixedHeight="46" gap="16">
        <profile-text text="性别" size="13" color="26,26,26" fixedWidth="120"/>
        <profile-text id="922" text="女" size="13" color="26,26,26" align="right" weight="1"/>
      </hbox>
      <profile-sep fixedHeight="1"/>
      <hbox fixedHeight="46" gap="16">
        <profile-text text="生日" size="13" color="26,26,26" fixedWidth="120"/>
        <profile-text id="923" text="1996-08-12" size="13" color="26,26,26" align="right" weight="1"/>
      </hbox>
      <profile-sep fixedHeight="1"/>
      <hbox fixedHeight="46" gap="16">
        <profile-text text="所在地" size="13" color="26,26,26" fixedWidth="120"/>
        <profile-text id="924" text="上海" size="13" color="26,26,26" align="right" weight="1"/>
      </hbox>
      <profile-sep fixedHeight="1"/>
      <hbox fixedHeight="46" gap="16">
        <profile-text text="邮箱" size="13" color="26,26,26" fixedWidth="120"/>
        <profile-text id="925" text="lin.zhixia@flamingo.im" size="13" color="26,26,26" align="right" weight="1"/>
      </hbox>
    </vbox>
  </vbox>
  <profile-sep fixedHeight="1"/>
  <hbox fixedHeight="58" padding="18,12,18,12" gap="8">
    <control weight="1"/>
    <profile-actionbtn id="960" text="关闭"     primary="false" fixedWidth="78"/>
    <profile-actionbtn id="961" text="编辑资料" primary="true" pencil="true" fixedWidth="112"/>
  </hbox>
</profile-root>
)xml";

// 编辑态完整客户区 XML。
const char* const kProfileEditXml = u8R"xml(
<profile-root padding="0" gap="0">
  <vbox padding="20,18,20,18" gap="0">
    <hbox fixedHeight="72" gap="14">
      <profile-avatar id="900" letter="L" fixedWidth="70" fixedHeight="70"/>
      <vbox weight="1" gap="0">
        <profile-text text="林知夏" size="17" color="26,26,26" bold="true" fixedHeight="24"/>
        <hbox fixedHeight="4"/>
        <profile-text text="账号 88234561" size="12" color="107,107,107" fixedHeight="16"/>
        <hbox fixedHeight="6"/>
        <hbox fixedHeight="22" gap="6">
          <profile-pill id="910" status="online"    selected="true"  fixedWidth="58"/>
          <profile-pill id="911" status="away"      fixedWidth="58"/>
          <profile-pill id="912" status="busy"      fixedWidth="58"/>
          <control weight="1"/>
        </hbox>
      </vbox>
    </hbox>
    <hbox fixedHeight="14"/>
    <profile-sep fixedHeight="1"/>
    <vbox padding="0,10,0,10" gap="0">
      <hbox fixedHeight="56" gap="16">
        <profile-text text="昵称" size="13" color="26,26,26" fixedWidth="120"/>
        <control weight="1"/>
        <vbox fixedWidth="160" gap="0">
          <control weight="1"/>
          <edit id="930" fixedHeight="32"/>
          <control weight="1"/>
        </vbox>
      </hbox>
      <profile-sep fixedHeight="1"/>
      <hbox fixedHeight="56" gap="16">
        <profile-text text="个性签名" size="13" color="26,26,26" fixedWidth="120"/>
        <control weight="1"/>
        <vbox fixedWidth="200" gap="0">
          <control weight="1"/>
          <edit id="931" fixedHeight="32"/>
          <control weight="1"/>
        </vbox>
      </hbox>
      <profile-sep fixedHeight="1"/>
      <hbox fixedHeight="56" gap="16">
        <profile-text text="性别" size="13" color="26,26,26" fixedWidth="120"/>
        <control weight="1"/>
        <vbox fixedWidth="140" gap="0">
          <control weight="1"/>
          <profile-combo id="932" fixedHeight="28"/>
          <control weight="1"/>
        </vbox>
      </hbox>
      <profile-sep fixedHeight="1"/>
      <hbox fixedHeight="56" gap="16">
        <profile-text text="生日" size="13" color="26,26,26" fixedWidth="120"/>
        <control weight="1"/>
        <vbox fixedWidth="140" gap="0">
          <control weight="1"/>
          <edit id="933" fixedHeight="32"/>
          <control weight="1"/>
        </vbox>
      </hbox>
      <profile-sep fixedHeight="1"/>
      <hbox fixedHeight="56" gap="16">
        <profile-text text="所在地" size="13" color="26,26,26" fixedWidth="120"/>
        <control weight="1"/>
        <vbox fixedWidth="140" gap="0">
          <control weight="1"/>
          <edit id="934" fixedHeight="32"/>
          <control weight="1"/>
        </vbox>
      </hbox>
      <profile-sep fixedHeight="1"/>
      <hbox fixedHeight="56" gap="16">
        <profile-text text="邮箱" size="13" color="26,26,26" fixedWidth="120"/>
        <control weight="1"/>
        <vbox fixedWidth="200" gap="0">
          <control weight="1"/>
          <edit id="935" fixedHeight="32"/>
          <control weight="1"/>
        </vbox>
      </hbox>
    </vbox>
  </vbox>
  <profile-sep fixedHeight="1"/>
  <hbox fixedHeight="58" padding="18,12,18,12" gap="8">
    <control weight="1"/>
    <profile-actionbtn id="962" text="取消" primary="false" fixedWidth="78"/>
    <profile-actionbtn id="963" text="保存" primary="true"  fixedWidth="88"/>
  </hbox>
</profile-root>
)xml";

} // anonymous namespace

std::unique_ptr<DuiControl> BuildProfileBody(bool editMode)
{
    const char* xml = editMode ? kProfileEditXml : kProfileViewXml;
    return DuiXmlBuilder::FromString(xml, &CreateProfileCustomControl);
}

void SetProfileViewText(DuiControl* root, UINT ctrlId, LPCTSTR text)
{
    if (root == NULL)
    {
        return;
    }
    DuiControl* ctrl = root->FindCtrlById(ctrlId);
    ProfileText* txt = dynamic_cast<ProfileText*>(ctrl);
    if (txt != NULL)
    {
        txt->SetTextValue(text);
    }
}

void SetProfilePillSelected(DuiControl* root, UINT ctrlId, bool selected)
{
    if (root == NULL)
    {
        return;
    }
    DuiControl* ctrl = root->FindCtrlById(ctrlId);
    ProfileStatusPill* pill = dynamic_cast<ProfileStatusPill*>(ctrl);
    if (pill != NULL)
    {
        pill->SetPillSelected(selected);
    }
}

int GetProfilePillStatus(DuiControl* root, UINT ctrlId)
{
    if (root == NULL)
    {
        return -1;
    }
    DuiControl* ctrl = root->FindCtrlById(ctrlId);
    ProfileStatusPill* pill = dynamic_cast<ProfileStatusPill*>(ctrl);
    if (pill != NULL)
    {
        return pill->GetStatusKind();
    }
    return -1;
}

void SetProfileAvatarLetter(DuiControl* root, UINT ctrlId, LPCTSTR letter)
{
    if (root == NULL)
    {
        return;
    }
    DuiControl* ctrl = root->FindCtrlById(ctrlId);
    ProfileAvatar* avatar = dynamic_cast<ProfileAvatar*>(ctrl);
    if (avatar != NULL)
    {
        avatar->SetLetter(letter);
    }
}

int ProfileBodyHeight(bool editMode)
{
    // 客户区总高度 = 客户区上内边距 + 头部行 + 头部下留白 + 头部分隔线
    //   + 字段区（上下内边距 + 六行 + 五条行间分隔线）
    //   + 客户区下内边距 + 底部分隔线 + 底部按钮区。
    const int rowH      = editMode ? kEditRowHeight : kViewRowHeight;
    const int fieldsH   = kFieldsPadV
                        + kFieldCount * rowH
                        + (kFieldCount - 1) * kSeparatorHeight
                        + kFieldsPadV;
    return kBodyPadTop
         + kHeaderHeight
         + kHeaderPadBottom
         + kSeparatorHeight
         + fieldsH
         + kBodyPadBottom
         + kSeparatorHeight
         + kFooterHeight;
}

} // namespace balloonprofile
