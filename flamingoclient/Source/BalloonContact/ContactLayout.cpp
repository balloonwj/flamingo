/**
 *  ContactLayout.cpp
 *
 *  联系人资料对话框客户区控件树的构造实现。布局以内嵌 XML 常量为唯一
 *  真值源（随 exe 出货、0 部署步骤），交给 balloonui 的 DuiXmlBuilder
 *  解析。各项尺寸严格照设计稿 dialogs.jsx 的 ContactInfoDialog 与
 *  components.jsx 的 Modal / Btn / FieldRow / Switch。
 *
 *  本文件还就地实现若干仅本布局自用的小控件：白底根面板、单行文本、
 *  头部多行资料块、标签流式容器、「+ 添加标签」虚线按钮、铅笔图标按钮、
 *  FieldRow 行容器、底部操作按钮。
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

#include "ContactLayout.h"
#include "TagChip.h"
#include "ContactIcons.h"
#include "../ClipboardText.h"   // 点击账号行复制到剪贴板
#include "../../balloonui/balloonui/Controls/Layout/DuiLayout.h"
#include "../../balloonui/balloonui/Controls/Basic/DuiButton.h"
#include "../../balloonui/balloonui/Controls/Input/DuiComboBox.h"
#include "../../balloonui/balloonui/DuiXmlBuilder.h"

namespace ballooncontact {

using balloonwjui::DuiControl;
using balloonwjui::DuiXmlBuilder;
using balloonwjui::DuiComboBox;

// =====================================================================
// 全局：主题色板（RGB），照任务说明的色板表
// =====================================================================

namespace {

const COLORREF kColorAccent     = RGB(0xFF, 0x7A, 0x45);   // 主题橙 #FF7A45
const COLORREF kColorAccentSoft = RGB(0xFF, 0xE6, 0xD6);   // 浅橙 #FFE6D6
const COLORREF kColorText       = RGB(0x1A, 0x1A, 0x1A);   // 主文字 #1A1A1A
const COLORREF kColorTextMuted  = RGB(0x6B, 0x6B, 0x6B);   // 次文字 #6B6B6B
const COLORREF kColorTextFaint  = RGB(0x9A, 0x9A, 0x9A);   // 弱文字 #9A9A9A
const COLORREF kColorBorder     = RGB(0xED, 0xED, 0xED);   // 分隔线 ≈ED
const COLORREF kColorBorderStrong = RGB(0xE0, 0xE0, 0xE0); // 强描边 ≈E0
const COLORREF kColorWhite      = RGB(0xFF, 0xFF, 0xFF);   // 白

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

// 用指定字号 / 字重创建一个微软雅黑字体。
HFONT CreateYaHeiFont(int pixelHeight, bool bold)
{
    return ::CreateFontW(
        -pixelHeight, 0, 0, 0,
        bold ? FW_SEMIBOLD : FW_NORMAL,
        FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Microsoft YaHei");
}

// 用 GDI 绘制单行文字。
void DrawGdiText(HDC hdc, LPCWSTR text, const RECT& rc,
                 int pixelHeight, bool bold, COLORREF color, UINT dtFlags)
{
    if (text == NULL || text[0] == L'\0' || pixelHeight <= 0)
    {
        return;
    }
    HFONT hFont = CreateYaHeiFont(pixelHeight, bold);
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

// 测量单行文字宽度（像素）。
int MeasureTextWidth(HDC hdc, LPCWSTR text, int pixelHeight, bool bold)
{
    if (hdc == NULL || text == NULL || text[0] == L'\0')
    {
        return 0;
    }
    HFONT hFont = CreateYaHeiFont(pixelHeight, bold);
    if (hFont == NULL)
    {
        return 0;
    }
    HFONT hOldFont = (HFONT)::SelectObject(hdc, hFont);
    SIZE  sz = { 0, 0 };
    ::GetTextExtentPoint32W(hdc, text, (int)::wcslen(text), &sz);
    ::SelectObject(hdc, hOldFont);
    ::DeleteObject(hFont);
    return sz.cx;
}

// 把 UTF-8 字节串转成 CString（宽字符）。内嵌 XML 是 u8 字面量（UTF-8），
// 属性里的中文必须走本函数 —— 不能用 CA2T（CA2T 按系统 ANSI 代码页解释，
// 中文系统会把 UTF-8 字节当 GBK 读，导致界面文字乱码）。
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

// 读取节点属性值；属性不存在时返回 def。
std::string GetNodeAttr(const DuiXmlBuilder::Node& n, const char* key,
                        const char* def = "")
{
    std::map<std::string, std::string>::const_iterator it = n.attrs.find(key);
    return (it != n.attrs.end()) ? it->second : std::string(def);
}

// 把 net-text 的 align 属性翻译成 DrawText 对齐标志。
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

// =====================================================================
// 布局自用小控件
// =====================================================================

/**
 *  ContactRootPanel —— 联系人资料对话框客户区根面板。铺纯白底，再交
 *  基类（DuiVBox）纵向排布子控件。
 */
class ContactRootPanel : public balloonwjui::DuiVBox
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
 *  ContactText —— 单行文本控件。字号 / 颜色 / 粗细 / 对齐由 XML 属性
 *  决定，用于小标题与 FieldRow 标签。纯绘制控件。
 */
class ContactText : public balloonwjui::DuiControl
{
public:
    ContactText()
        : m_size(13)
        , m_color(kColorText)
        , m_bold(false)
        , m_align(DT_LEFT)
    {
    }

    // 配置文本内容与样式。
    void Configure(LPCTSTR text, int size, COLORREF color, bool bold, UINT align)
    {
        m_text  = (text != NULL) ? text : _T("");
        m_size  = size;
        m_color = color;
        m_bold  = bold;
        m_align = align;
    }

    // 修改文本内容（保持样式不变）。供「备注名」当前值刷新等场景用。
    void SetTextContent(LPCTSTR text)
    {
        m_text = (text != NULL) ? text : _T("");
        Invalidate();
    }

    void OnPaint(HDC hdc, const RECT& /*rcDirty*/) override
    {
        DrawGdiText(hdc, (LPCWSTR)m_text, m_rcItem, m_size, m_bold, m_color,
                    m_align | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX
                    | DT_END_ELLIPSIS);
    }

    bool OnSetCursor(POINT /*pt*/) override { return false; }

private:
    CString  m_text;    // 文本内容
    int      m_size;    // 字号（像素）
    COLORREF m_color;   // 文字颜色
    bool     m_bold;    // 是否粗体
    UINT     m_align;   // 水平对齐（DT_LEFT / DT_CENTER / DT_RIGHT）
};

/**
 *  ContactHeaderInfo —— 头部右侧的多行资料块。一控件画四行：
 *    第 1 行：姓名（备注名，18 粗体）+ 真名（12 弱色，括号）；
 *    第 2 行：职务 · 部门（12，accent 色，字重 500）；
 *    第 3 行：账号 + 地区（12，text-muted）；
 *    第 4 行：个性签名（12，text-muted）。
 *  四行的字号 / 颜色 / 行距严格照设计稿 ContactInfoDialog 的 header。
 */
class ContactHeaderInfo : public balloonwjui::DuiControl
{
public:
    // 用模型数据配置四行内容。
    //   primaryName：第 1 行主名（备注名优先，否则真名）；
    //   realNameParen：第 1 行紧跟的括号真名（如「(江屿白)」），无则空；
    //   titleDept：第 2 行「职务 · 部门」，无职务 / 部门则空；
    //   accountRegion：第 3 行「账号 XXX · 地区」展示文本；
    //   signature：第 4 行个性签名；
    //   account：裸账号(点账号行复制它到剪贴板,不含"账号 "前缀与地区)。
    void Configure(LPCTSTR primaryName, LPCTSTR realNameParen,
                   LPCTSTR titleDept, LPCTSTR accountRegion,
                   LPCTSTR signature, LPCTSTR account)
    {
        m_primaryName   = (primaryName != NULL) ? primaryName : _T("");
        m_realNameParen = (realNameParen != NULL) ? realNameParen : _T("");
        m_titleDept     = (titleDept != NULL) ? titleDept : _T("");
        m_accountRegion = (accountRegion != NULL) ? accountRegion : _T("");
        m_signature     = (signature != NULL) ? signature : _T("");
        m_account       = (account != NULL) ? account : _T("");
        Invalidate();
    }

    void OnPaint(HDC hdc, const RECT& /*rcDirty*/) override
    {
        const int x = m_rcItem.left;
        const int w = m_rcItem.right - m_rcItem.left;
        if (w <= 0)
        {
            return;
        }

        // ---- 行几何常量（逻辑像素，照设计稿 header）----
        const int kNameSize     = 18;   // 主名字号
        const int kRealNameSize = 12;   // 括号真名字号
        const int kSubSize      = 12;   // 第 2/3/4 行字号
        const int kNameRowH     = 24;   // 第 1 行行高
        const int kSubRowH      = 17;   // 第 2/3/4 行行高
        const int kGapTitle     = 3;    // 第 1 行到第 2 行的上间距
        const int kGapAccount   = 2;    // 第 2 行到第 3 行的上间距
        const int kGapSign      = 2;    // 第 3 行到第 4 行的上间距
        const int kNameGapParen = 6;    // 主名与括号真名之间的间距

        // 四行内容整体在控件高度内垂直居中（头像 64 高，文字块通常更矮）。
        int totalH = kNameRowH;
        if (!m_titleDept.IsEmpty())
        {
            totalH += kGapTitle + kSubRowH;
        }
        totalH += kGapAccount + kSubRowH;   // 账号行始终有
        if (!m_signature.IsEmpty())
        {
            totalH += kGapSign + kSubRowH;
        }
        const int ctrlH = m_rcItem.bottom - m_rcItem.top;
        int y = m_rcItem.top + ((ctrlH > totalH) ? (ctrlH - totalH) / 2 : 0);

        // ---- 第 1 行：主名（粗体）+ 括号真名（弱色）----
        {
            const int nameW = MeasureTextWidth(hdc, (LPCWSTR)m_primaryName,
                                               kNameSize, true);
            RECT nameRc = { x, y, x + nameW, y + kNameRowH };
            // 主名宽度可能超出控件，dt 不裁切但限制在控件右界内。
            if (nameRc.right > m_rcItem.right)
            {
                nameRc.right = m_rcItem.right;
            }
            DrawGdiText(hdc, (LPCWSTR)m_primaryName, nameRc, kNameSize, true,
                        kColorText,
                        DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX
                        | DT_END_ELLIPSIS);

            if (!m_realNameParen.IsEmpty())
            {
                const int parenX = nameRc.right + kNameGapParen;
                RECT parenRc = { parenX, y, m_rcItem.right, y + kNameRowH };
                DrawGdiText(hdc, (LPCWSTR)m_realNameParen, parenRc,
                            kRealNameSize, false, kColorTextFaint,
                            DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX
                            | DT_END_ELLIPSIS);
            }
            y += kNameRowH;
        }

        // ---- 第 2 行：职务 · 部门（accent 色）----
        if (!m_titleDept.IsEmpty())
        {
            y += kGapTitle;
            RECT rc = { x, y, m_rcItem.right, y + kSubRowH };
            DrawGdiText(hdc, (LPCWSTR)m_titleDept, rc, kSubSize, false,
                        kColorAccent,
                        DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX
                        | DT_END_ELLIPSIS);
            y += kSubRowH;
        }

        // ---- 第 3 行：账号 · 地区（text-muted）----
        {
            y += kGapAccount;
            RECT rc = { x, y, m_rcItem.right, y + kSubRowH };
            // 记下账号行矩形(host 坐标),供 OnLButtonUp/OnSetCursor 命中判断:点该行复制账号。
            m_accountRect = rc;
            DrawGdiText(hdc, (LPCWSTR)m_accountRegion, rc, kSubSize, false,
                        kColorTextMuted,
                        DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX
                        | DT_END_ELLIPSIS);
            y += kSubRowH;
        }

        // ---- 第 4 行：个性签名（text-muted）----
        if (!m_signature.IsEmpty())
        {
            y += kGapSign;
            RECT rc = { x, y, m_rcItem.right, y + kSubRowH };
            DrawGdiText(hdc, (LPCWSTR)m_signature, rc, kSubSize, false,
                        kColorTextMuted,
                        DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX
                        | DT_END_ELLIPSIS);
        }
    }

    // 点账号行(第 3 行)→ 把裸账号复制到剪贴板。命中账号行矩形才处理,其余位置不拦。
    bool OnLButtonUp(POINT pt, UINT /*mkFlags*/) override
    {
        if (!m_account.IsEmpty() && ::PtInRect(&m_accountRect, pt))
        {
            flamingo::CopyTextToClipboard(m_account);
            return true;
        }
        return false;
    }

    // 鼠标在账号行上显手型,提示可点复制;其余位置维持默认箭头。
    bool OnSetCursor(POINT pt) override
    {
        if (!m_account.IsEmpty() && ::PtInRect(&m_accountRect, pt))
        {
            ::SetCursor(::LoadCursor(NULL, IDC_HAND));
            return true;
        }
        return false;
    }

private:
    CString m_primaryName;     // 第 1 行主名（备注名 / 真名）
    CString m_realNameParen;   // 第 1 行括号真名，可空
    CString m_titleDept;       // 第 2 行职务 · 部门，可空
    CString m_accountRegion;   // 第 3 行账号 · 地区（展示文本）
    CString m_account;         // 裸账号（点击复制用，不含"账号 "前缀与地区）
    CString m_signature;       // 第 4 行个性签名，可空
    RECT    m_accountRect = { 0, 0, 0, 0 };   // 账号行矩形(OnPaint 填,host 坐标),点击命中用
};

/**
 *  ContactTagFlow —— 标签流式容器。子控件（标签胶囊 + 「+ 添加标签」
 *  按钮）按可用宽度从左到右排布，排不下自动换行。每个子控件的宽 / 高
 *  由其自身决定 —— 容器只负责定位。
 */
class ContactTagFlow : public balloonwjui::DuiControl
{
public:
    ContactTagFlow()
        : m_gap(6)
        , m_rowHeight(24)
    {
    }

    // 设置子控件之间的水平 / 垂直间距（设计稿 gap:6）。
    void SetGap(int gap) { m_gap = (gap < 0) ? 0 : gap; }

    // 设置每行（即每枚胶囊）的统一高度（设计稿胶囊高 24）。
    void SetRowHeight(int h) { m_rowHeight = (h < 1) ? 1 : h; }

    // 容器在当前子控件下排满需要的总高度（逻辑像素）。窗口据此可在
    // 增删标签后判断是否需要调整窗口高度（本对话框标签通常单行，留作
    // 后续扩展用）。
    int CalcDesiredHeight(int availWidth) const
    {
        const int rows = CountRows(availWidth);
        if (rows <= 0)
        {
            return m_rowHeight;
        }
        return rows * m_rowHeight + (rows - 1) * m_gap;
    }

    void Layout(const RECT& rcAvail) override
    {
        m_rcItem = rcAvail;

        const int availW = rcAvail.right - rcAvail.left;
        if (availW <= 0)
        {
            return;
        }

        // 测量子控件宽度需要一个 DC —— 用桌面 DC（只测文字，不绘制）。
        HDC hdcMeasure = ::GetDC(NULL);

        int curX = rcAvail.left;
        int curY = rcAvail.top;
        for (const auto& child : m_children)
        {
            DuiControl* c = child.get();
            if (c == NULL || !c->IsVisible())
            {
                continue;
            }

            const int childW = MeasureChildWidth(c, hdcMeasure);

            // 本行放不下（且不是行首）→ 换行。
            if (curX > rcAvail.left && curX + childW > rcAvail.right)
            {
                curX = rcAvail.left;
                curY += m_rowHeight + m_gap;
            }

            RECT childRc;
            childRc.left   = curX;
            childRc.top    = curY;
            childRc.right  = curX + childW;
            childRc.bottom = curY + m_rowHeight;
            // 必须用 SetRect 而非 Layout —— DuiControl::Layout 的默认实现
            // 只递归子节点、不写 m_rcItem；唯有 SetRect / ForceLayout 才会
            // 真正把矩形落到控件上（与 balloonui 各容器的约定一致）。若此
            // 处只调 Layout，标签胶囊 / 「+ 添加标签」按钮的 m_rcItem 恒为
            // 全零，OnPaint 在 w<=0 处直接返回，整片标签区都不显示。
            c->SetRect(childRc);

            curX += childW + m_gap;
        }

        if (hdcMeasure != NULL)
        {
            ::ReleaseDC(NULL, hdcMeasure);
        }
    }

    bool OnSetCursor(POINT /*pt*/) override { return false; }

private:
    // 测量一个子控件的理想宽度。标签胶囊 / 「+ 添加标签」按钮各自提供
    // 宽度算法；其它控件回退到 GetDesiredSize().cx。
    int MeasureChildWidth(DuiControl* c, HDC hdcMeasure) const;

    // 在给定可用宽度下，按当前子控件排布需要多少行。
    int CountRows(int availWidth) const
    {
        if (availWidth <= 0)
        {
            return 1;
        }
        HDC hdcMeasure = ::GetDC(NULL);
        int rows  = 1;
        int curX  = 0;
        for (const auto& child : m_children)
        {
            DuiControl* c = child.get();
            if (c == NULL || !c->IsVisible())
            {
                continue;
            }
            const int childW = MeasureChildWidth(c, hdcMeasure);
            if (curX > 0 && curX + childW > availWidth)
            {
                ++rows;
                curX = 0;
            }
            curX += childW + m_gap;
        }
        if (hdcMeasure != NULL)
        {
            ::ReleaseDC(NULL, hdcMeasure);
        }
        return rows;
    }

private:
    int m_gap;         // 子控件间距（水平 + 垂直）
    int m_rowHeight;   // 每行高度
};

/**
 *  ContactAddTagButton —— 标签区末尾的「+ 添加标签」按钮（自绘）。
 *  虚线边框、全圆角、透明底、accent 文字。点击向父冒 DUIN_CLICK，由
 *  窗口弹出标签输入。继承 DuiButton 复用点击 / 通知，只覆写 OnPaint。
 */
class ContactAddTagButton : public balloonwjui::DuiButton
{
public:
    // 「+ 添加标签」按钮的固定宽度（设计稿 width:90）。
    static const int kFixedWidth = 90;

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

        // 全圆角虚线边（设计稿 1px dashed border-strong，borderRadius:999）。
        const Gdiplus::REAL r = (Gdiplus::REAL)h / 2.0f - 0.5f;
        const Gdiplus::REAL d = r * 2.0f;
        Gdiplus::GraphicsPath path;
        path.AddArc((Gdiplus::REAL)x + 0.5f,             (Gdiplus::REAL)y + 0.5f,
                    d, d, 90.0f, 180.0f);
        path.AddArc((Gdiplus::REAL)x + w - d - 0.5f,     (Gdiplus::REAL)y + 0.5f,
                    d, d, 270.0f, 180.0f);
        path.CloseFigure();

        Gdiplus::Pen pen(Gdiplus::Color(255,
            GetRValue(kColorBorderStrong), GetGValue(kColorBorderStrong),
            GetBValue(kColorBorderStrong)), 1.0f);
        pen.SetDashStyle(Gdiplus::DashStyleDash);
        g.DrawPath(&pen, &path);

        // 文字「+ 添加标签」—— accent 色、字号 11.5（取整 12）。
        DrawGdiText(hdc, (LPCWSTR)GetText(), m_rcItem, 12, false, kColorAccent,
                    DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    }

    bool OnSetCursor(POINT /*pt*/) override { return false; }
};

/**
 *  ContactIconButton —— 「备注名」行的铅笔编辑小按钮（自绘）。透明底，
 *  只画一个铅笔图标；hover 时画一个极浅的圆角底。点击向父冒 DUIN_CLICK。
 */
class ContactIconButton : public balloonwjui::DuiButton
{
public:
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

        // hover 浅灰底（设计稿 IconBtn hover 用 --hover 色）。
        if (IsHover())
        {
            Gdiplus::GraphicsPath bg;
            AddRoundRect(bg, (Gdiplus::REAL)x, (Gdiplus::REAL)y,
                         (Gdiplus::REAL)w, (Gdiplus::REAL)h, 6.0f);
            Gdiplus::SolidBrush brush(Gdiplus::Color(255, 0xF2, 0xF1, 0xEE));
            g.FillPath(&brush, &bg);
        }

        // 铅笔图标 —— 设计稿 Icon.edit size 13，居中绘制。
        const Gdiplus::REAL kGlyph = 13.0f;
        const Gdiplus::REAL gx = (Gdiplus::REAL)x + (w - kGlyph) / 2.0f;
        const Gdiplus::REAL gy = (Gdiplus::REAL)y + (h - kGlyph) / 2.0f;
        DrawContactIcon(g, kContactIconEdit, gx, gy, kGlyph,
                        Gdiplus::Color(255,
                            GetRValue(kColorTextMuted),
                            GetGValue(kColorTextMuted),
                            GetBValue(kColorTextMuted)),
                        1.6f);
    }

    bool OnSetCursor(POINT /*pt*/) override { return false; }
};

/**
 *  ContactFieldRow —— 设计稿 FieldRow 的等价行容器。继承 DuiHBox 做
 *  「左标签 + 右控件」横向排布；divider=true 时在行底部画一条 1px
 *  分隔线。子控件由 XML 提供（标签 ContactText、右侧控件 / 容器）。
 */
class ContactFieldRow : public balloonwjui::DuiHBox
{
public:
    ContactFieldRow() : m_divider(true) {}

    // 设置本行底部是否画分隔线。
    void SetDivider(bool b) { m_divider = b; }

    void OnPaint(HDC hdc, const RECT& rcDirty) override
    {
        // 先让基类排布并绘制左右子控件。
        DuiHBox::OnPaint(hdc, rcDirty);

        // 再在行底画 1px 分隔线（divider=true 时）。
        if (m_divider)
        {
            RECT line;
            line.left   = m_rcItem.left;
            line.right  = m_rcItem.right;
            line.bottom = m_rcItem.bottom;
            line.top    = m_rcItem.bottom - 1;
            HBRUSH br = ::CreateSolidBrush(kColorBorder);
            ::FillRect(hdc, &line, br);
            ::DeleteObject(br);
        }
    }

private:
    bool m_divider;   // 行底是否画分隔线
};

/**
 *  ContactActionButton —— 底部操作按钮（自绘）。primary=true 为 accent
 *  实心主按钮（「发消息」，左侧带聊天气泡图标），false 为白底描边 ghost
 *  按钮（「关闭」）。继承 DuiButton 复用点击 / 通知，只覆写 OnPaint。
 */
class ContactActionButton : public balloonwjui::DuiButton
{
public:
    ContactActionButton()
        : m_primary(false)
        , m_withChatIcon(false)
    {
    }

    // 设为主按钮（accent 实心）或 ghost 按钮（白底描边）。
    void SetPrimary(bool primary)
    {
        m_primary = primary;
        Invalidate();
    }

    // 设置按钮文字左侧是否绘制聊天气泡图标（「发消息」按钮用）。
    void SetWithChatIcon(bool b)
    {
        m_withChatIcon = b;
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

        // 圆角 10（设计稿 Btn size md borderRadius:10）。
        Gdiplus::GraphicsPath path;
        AddRoundRect(path, (Gdiplus::REAL)x + 0.5f, (Gdiplus::REAL)y + 0.5f,
                     (Gdiplus::REAL)w - 1.0f, (Gdiplus::REAL)h - 1.0f, 10.0f);

        COLORREF textColor;
        if (m_primary)
        {
            // accent 实心主按钮，hover 略深。
            const Gdiplus::Color fill = IsHover()
                ? Gdiplus::Color(255, 0xFF, 0x6A, 0x30)
                : Gdiplus::Color(255, 0xFF, 0x7A, 0x45);
            Gdiplus::SolidBrush brush(fill);
            g.FillPath(&brush, &path);
            Gdiplus::Pen border(Gdiplus::Color(255, 0xFF, 0x7A, 0x45), 1.0f);
            g.DrawPath(&border, &path);
            textColor = kColorWhite;
        }
        else
        {
            // ghost 按钮：白底（hover 浅灰）+ border-strong 描边。
            const Gdiplus::Color fill = IsHover()
                ? Gdiplus::Color(255, 0xF6, 0xF5, 0xF2)
                : Gdiplus::Color(255, 0xFF, 0xFF, 0xFF);
            Gdiplus::SolidBrush brush(fill);
            g.FillPath(&brush, &path);
            Gdiplus::Pen border(Gdiplus::Color(255,
                GetRValue(kColorBorderStrong), GetGValue(kColorBorderStrong),
                GetBValue(kColorBorderStrong)), 1.0f);
            g.DrawPath(&border, &path);
            textColor = kColorText;
        }

        // 文字字号 13.5（取整 14，照 Btn size md）。
        const int kTextSize = 14;

        if (m_withChatIcon)
        {
            // 图标 + 文字整体水平居中。设计稿 Icon.chat size 14、icon 与
            // 文字 gap 6。
            const int kIconSize = 14;
            const int kIconGap  = 6;
            const int textW = MeasureTextWidth(hdc, (LPCWSTR)GetText(),
                                               kTextSize, true);
            const int blockW = kIconSize + kIconGap + textW;
            int bx = x + (w - blockW) / 2;
            if (bx < x)
            {
                bx = x;
            }
            const int iconY = y + (h - kIconSize) / 2;
            DrawContactIcon(g, kContactIconChat,
                            (Gdiplus::REAL)bx, (Gdiplus::REAL)iconY,
                            (Gdiplus::REAL)kIconSize,
                            Gdiplus::Color(255,
                                GetRValue(textColor), GetGValue(textColor),
                                GetBValue(textColor)),
                            1.7f);

            RECT textRc = { bx + kIconSize + kIconGap, y, x + w, y + h };
            DrawGdiText(hdc, (LPCWSTR)GetText(), textRc, kTextSize, true,
                        textColor,
                        DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        }
        else
        {
            DrawGdiText(hdc, (LPCWSTR)GetText(), m_rcItem, kTextSize, true,
                        textColor,
                        DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        }
    }

    bool OnSetCursor(POINT /*pt*/) override { return false; }

private:
    bool m_primary;        // true=accent 主按钮，false=ghost 按钮
    bool m_withChatIcon;   // 文字左侧是否画聊天气泡图标
};

// ContactTagFlow::MeasureChildWidth —— 放在类外，因为它要用到 TagChip /
// ContactAddTagButton 的完整定义。
int ContactTagFlow::MeasureChildWidth(DuiControl* c, HDC hdcMeasure) const
{
    // 标签胶囊：按其文字算理想宽度。
    TagChip* chip = dynamic_cast<TagChip*>(c);
    if (chip != NULL)
    {
        return chip->CalcDesiredWidth(hdcMeasure);
    }
    // 「+ 添加标签」按钮：固定宽度。
    ContactAddTagButton* addBtn = dynamic_cast<ContactAddTagButton*>(c);
    if (addBtn != NULL)
    {
        return ContactAddTagButton::kFixedWidth;
    }
    // 其它控件：回退到 GetDesiredSize。
    SIZE sz = c->GetDesiredSize();
    return (sz.cx > 0) ? sz.cx : 60;
}

// =====================================================================
// 内嵌 XML 布局
// =====================================================================
//
// contact-root（白底 vbox，padding 20,18,20,18）自上而下：
//   · 头部行（头像 + 多行资料块），fixedHeight 64；
//   · 头部下边距 spacer（14，对应设计稿 header paddingBottom）；
//   · 分隔线；
//   · 标签区（小标题 + 流式胶囊容器）；
//   · 分隔线；
//   · 备注名 + 分组两行 FieldRow；
//   · 分隔线；
//   · 其它开关三行 FieldRow；
//   · 弹性占位 control（weight=1，把底部按钮推到底）；
//   · 底部分隔线 + 按钮行（关闭 / 发消息）。
//
// 说明：XML 的 margin 属性只取首个整数（balloonui builder 限制），无法
// 表达"单边 margin"，因此区块间的纵向间距一律用独立 spacer control 行
// 显式给出，不依赖 margin。
//
// 数字像素全部照 dialogs.jsx ContactInfoDialog；不出现裸魔数 —— XML 里
// 的尺寸是设计稿的"真值数据"，等同配置常量。
// =====================================================================
const char* const kContactBodyXml = u8R"xml(
<contact-root padding="20,18,20,18" gap="0">

  <hbox fixedHeight="84" gap="14">
    <avatar id="1000" name="江" shape="round-rect" corner-radius="9"
            status="away" fallback-bg-color="59,130,246"
            initials-color="255,255,255"
            fixedWidth="64" fixedHeight="64" alignCross="center"/>
    <contact-header id="1001" weight="1"/>
  </hbox>

  <control fixedHeight="14"/>
  <separator fixedHeight="1" color="237,237,237"/>

  <vbox fixedHeight="76" padding="0,14,0,14" gap="8">
    <contact-text text="标签" size="11" color="107,107,107" fixedHeight="16"/>
    <contact-tagflow id="1010" weight="1" gap="6" row-height="24"/>
  </vbox>

  <separator fixedHeight="1" color="237,237,237"/>

  <vbox fixedHeight="132" padding="0,14,0,14" gap="0">
    <field-row fixedHeight="52" divider="false" gap="16">
      <contact-text text="备注名" size="14" color="26,26,26" weight="1"/>
      <contact-valuetext id="1020" text="设计组组长" size="13"
                         color="26,26,26" align="right"
                         fixedWidth="150" alignCross="center"/>
      <edit id="1022" fixedWidth="140" fixedHeight="28" alignCross="center"/>
      <contact-iconbtn id="1021" fixedWidth="22" fixedHeight="22"
                       alignCross="center"/>
    </field-row>
    <field-row fixedHeight="52" divider="false" gap="16">
      <contact-text text="分组" size="14" color="26,26,26" weight="1"/>
      <combobox id="1023" fixedWidth="170" fixedHeight="28"
                alignCross="center"/>
    </field-row>
  </vbox>

  <separator fixedHeight="1" color="237,237,237"/>

  <vbox fixedHeight="170" padding="0,14,0,0" gap="0">
    <field-row fixedHeight="52" divider="true" gap="16">
      <contact-text text="消息免打扰" size="14" color="26,26,26" weight="1"/>
      <switch id="1030" fixedWidth="36" fixedHeight="20" alignCross="center"
              on-color="255,122,69" off-color="216,213,208"/>
    </field-row>
    <field-row fixedHeight="52" divider="false" gap="16">
      <contact-text text="置顶聊天" size="14" color="26,26,26" weight="1"/>
      <switch id="1031" fixedWidth="36" fixedHeight="20" alignCross="center"
              on-color="255,122,69" off-color="216,213,208"/>
    </field-row>
  </vbox>

  <control weight="1"/>

  <separator fixedHeight="1" color="237,237,237"/>

  <hbox fixedHeight="58" padding="0,12,0,12" gap="8">
    <control weight="1"/>
    <contact-actionbtn id="1040" text="关闭"   primary="false"
                       fixedWidth="76" fixedHeight="34" alignCross="center"/>
    <contact-actionbtn id="1041" text="发消息" primary="true" chat-icon="true"
                       fixedWidth="104" fixedHeight="34" alignCross="center"/>
  </hbox>

</contact-root>
)xml";

// =====================================================================
// 自定义标签工厂
// =====================================================================
//
// 接 contact-root / contact-text / contact-header / contact-tagflow /
// contact-addtag / contact-valuetext / contact-iconbtn / field-row /
// contact-actionbtn / combobox / control；其余标签（vbox / hbox / edit /
// switch / separator / avatar）走 builder 内置处理。
std::unique_ptr<DuiControl> CreateContactCustomControl(
    const DuiXmlBuilder::Node& n)
{
    if (n.tag == "contact-root")
    {
        return std::unique_ptr<DuiControl>(new ContactRootPanel());
    }

    if (n.tag == "contact-text" || n.tag == "contact-valuetext")
    {
        std::unique_ptr<ContactText> text(new ContactText());
        const CString     strText = Utf8ToCString(GetNodeAttr(n, "text"));
        const std::string strSize = GetNodeAttr(n, "size", "13");
        const std::string strColor = GetNodeAttr(n, "color", "26,26,26");

        // 解析 "r,g,b" 颜色 —— 复用 builder 的 ParseColor 不便（在匿名 ns），
        // 这里就地解析。
        int rgb[3] = { 26, 26, 26 };
        {
            int idx = 0;
            int cur = 0;
            bool hasDigit = false;
            for (size_t i = 0; i <= strColor.size() && idx < 3; ++i)
            {
                const char ch = (i < strColor.size()) ? strColor[i] : ',';
                if (ch >= '0' && ch <= '9')
                {
                    cur = cur * 10 + (ch - '0');
                    hasDigit = true;
                }
                else if (ch == ',')
                {
                    if (hasDigit)
                    {
                        rgb[idx] = (cur > 255) ? 255 : cur;
                        ++idx;
                    }
                    cur = 0;
                    hasDigit = false;
                }
            }
        }
        const bool bold = (GetNodeAttr(n, "bold") == "true");
        const UINT align = TextAlignFromAttr(GetNodeAttr(n, "align"));
        text->Configure(strText, atoi(strSize.c_str()),
                        RGB(rgb[0], rgb[1], rgb[2]), bold, align);
        return std::unique_ptr<DuiControl>(text.release());
    }

    if (n.tag == "contact-header")
    {
        return std::unique_ptr<DuiControl>(new ContactHeaderInfo());
    }

    if (n.tag == "contact-tagflow")
    {
        std::unique_ptr<ContactTagFlow> flow(new ContactTagFlow());
        const std::string strGap = GetNodeAttr(n, "gap", "6");
        const std::string strRow = GetNodeAttr(n, "row-height", "24");
        flow->SetGap(atoi(strGap.c_str()));
        flow->SetRowHeight(atoi(strRow.c_str()));
        return std::unique_ptr<DuiControl>(flow.release());
    }

    if (n.tag == "contact-iconbtn")
    {
        std::unique_ptr<ContactIconButton> btn(new ContactIconButton());
        btn->SetButtonType(balloonwjui::DuiButton::StylePushButton);
        return std::unique_ptr<DuiControl>(btn.release());
    }

    if (n.tag == "field-row")
    {
        std::unique_ptr<ContactFieldRow> row(new ContactFieldRow());
        row->SetDivider(GetNodeAttr(n, "divider", "true") != "false");
        const std::string strGap = GetNodeAttr(n, "gap", "0");
        row->SetGap(atoi(strGap.c_str()));
        return std::unique_ptr<DuiControl>(row.release());
    }

    if (n.tag == "contact-actionbtn")
    {
        std::unique_ptr<ContactActionButton> btn(new ContactActionButton());
        const CString btnText = Utf8ToCString(GetNodeAttr(n, "text"));
        btn->SetText(btnText);
        btn->SetPrimary(GetNodeAttr(n, "primary") == "true");
        btn->SetWithChatIcon(GetNodeAttr(n, "chat-icon") == "true");
        return std::unique_ptr<DuiControl>(btn.release());
    }

    if (n.tag == "combobox")
    {
        // DuiComboBox 暂未原生支持 XML —— 在此就地构造。选项 / 选中项由
        // 窗口在拿到 ContactModel 后填入；这里只造空 combo。
        std::unique_ptr<DuiComboBox> combo(new DuiComboBox());
        combo->SetEditable(false);          // read-only 下拉
        return std::unique_ptr<DuiControl>(combo.release());
    }

    if (n.tag == "control")
    {
        return std::unique_ptr<DuiControl>(new DuiControl());
    }

    return nullptr;
}

} // anonymous namespace

// =====================================================================
// 对外接口
// =====================================================================

std::unique_ptr<DuiControl> BuildContactBody()
{
    return DuiXmlBuilder::FromString(kContactBodyXml,
                                     &CreateContactCustomControl);
}

std::unique_ptr<DuiControl> CreateTagChip(LPCTSTR text, UINT ctrlId)
{
    std::unique_ptr<TagChip> chip(new TagChip());
    chip->SetTagText(text);
    chip->SetCtrlId(ctrlId);
    return std::unique_ptr<DuiControl>(chip.release());
}

std::unique_ptr<DuiControl> CreateAddTagButton()
{
    std::unique_ptr<ContactAddTagButton> btn(new ContactAddTagButton());
    btn->SetButtonType(balloonwjui::DuiButton::StylePushButton);
    btn->SetText(_T("+ 添加标签"));
    btn->SetCtrlId((UINT)kIdAddTagButton);
    return std::unique_ptr<DuiControl>(btn.release());
}

void ConfigureContactHeader(DuiControl* root,
                            LPCTSTR primaryName, LPCTSTR realNameParen,
                            LPCTSTR titleDept, LPCTSTR accountRegion,
                            LPCTSTR signature, LPCTSTR account)
{
    if (root == NULL)
    {
        return;
    }
    ContactHeaderInfo* header = dynamic_cast<ContactHeaderInfo*>(
        root->FindCtrlById((UINT)kIdHeaderInfo));
    if (header != NULL)
    {
        header->Configure(primaryName, realNameParen, titleDept,
                          accountRegion, signature, account);
    }
}

void SetContactText(DuiControl* root, UINT ctrlId, LPCTSTR text)
{
    if (root == NULL)
    {
        return;
    }
    ContactText* ct = dynamic_cast<ContactText*>(root->FindCtrlById(ctrlId));
    if (ct != NULL)
    {
        ct->SetTextContent(text);
    }
}

void RelayoutContactBody(DuiControl* root)
{
    if (root == NULL)
    {
        return;
    }
    // 以根控件当前矩形强制重排整树 —— 标签流式容器据此重新换行排布。
    root->ForceLayout(root->GetRect());
    root->Invalidate();
}

} // namespace ballooncontact
