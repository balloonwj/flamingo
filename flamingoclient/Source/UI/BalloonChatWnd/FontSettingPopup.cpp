/**
 *  FontSettingPopup.cpp —— 聊天字体设置浮层实现。
 *
 *  面板用 balloonui 现成布局（DuiVBox/DuiHBox/DuiLabel）搭骨架，交互单元为本文件
 *  内三个小自绘控件：FontChip（样式 / 字号的方形按钮，可高亮激活态）、ColorSwatch
 *  （颜色块，选中画环）、FontPreview（按当前设置实时渲染示例文字）。每个交互单元
 *  分配唯一 ctrlId，点击时据 ctrlId 更新 m_font，刷新激活态与预览，并回调宿主。
 *
 *  balloonwj@qq.com   2026-06-03
 */
#include "stdafx.h"
#include "FontSettingPopup.h"

#include <memory>
#include <gdiplus.h>

#include "../../Common/Log/IULog.h"
#include "../../../third-party/balloonui/balloonui/DuiNotify.h"
#include "../../../third-party/balloonui/balloonui/Controls/Layout/DuiLayout.h"
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiButton.h"
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiLabel.h"

using balloonwjui::DuiControl;
using balloonwjui::DuiButton;
using balloonwjui::DuiLabel;
using balloonwjui::DuiVBox;
using balloonwjui::DuiHBox;
using balloonwjui::DuiLayout;

// CFontSettingPopup 在全局命名空间（与 CEmojiPickerPopup 一致）。下面的匿名
// 命名空间承载本文件内部的小自绘控件与构建 helper。

namespace {

// ---- 配色 / 尺寸常量 ---------------------------------------------------

const COLORREF kAccent       = RGB(0xFF, 0x7A, 0x45);   // 激活态主色（橙）
const COLORREF kChipBorder   = RGB(0xD6, 0xD3, 0xCE);   // 未激活 chip 边框
const COLORREF kChipText     = RGB(0x55, 0x55, 0x55);   // 未激活 chip 文字
const COLORREF kLabelText    = RGB(0x8A, 0x88, 0x83);   // 行标签灰
const COLORREF kPreviewBg    = RGB(0xF7, 0xF6, 0xF3);   // 预览底
const COLORREF kPanelBg      = RGB(0xFF, 0xFF, 0xFF);   // 面板底
const COLORREF kPanelBorder  = RGB(0xE2, 0xE0, 0xDB);   // 面板边

// 颜色块调色板（0xRRGGBB，与 net::TextFont::m_colorRgb 同口径）。
const unsigned int kPalette[] =
{
    0x1A1A1A, 0xE54B4B, 0xF59E0B, 0x22A565, 0x3A86D9,
    0x8B5CF6, 0xEC6C9A, 0x6B7280, 0xFFFFFF,
};
const int kPaletteCount = (int)(sizeof(kPalette) / sizeof(kPalette[0]));

// 字号快选值。
const int kQuickSizes[] = { 12, 14, 16, 20 };
const int kQuickCount = (int)(sizeof(kQuickSizes) / sizeof(kQuickSizes[0]));

// 字号上下限与步进。
const int kSizeMin  = 8;
const int kSizeMax  = 72;
const int kSizeStep = 1;

// 面板各行之间的纵向间距（逻辑像素），与 BuildContentOnce 里 root->SetGap 的取值一致。
const int kPanelRowGap = 12;
// 「使用默认字体」那一行的高度与按钮宽度（逻辑像素）。宽度放得下六个汉字加两侧留白。
const int kUseDefaultRowH  = 26;
const int kUseDefaultChipW = 96;

// 不可点的按钮的文字色（浅灰），与可点时的 kChipText 拉开。
const COLORREF kChipTextDisabled = RGB(0xB8, 0xB6, 0xB1);

// ---- 控件 id（面板内唯一；取一段不与其它弹窗冲突的较大基址）----
const UINT kIdBold       = 9301;
const UINT kIdItalic     = 9302;
const UINT kIdUnderline  = 9303;
const UINT kIdStrike     = 9304;
const UINT kIdClear      = 9305;
const UINT kIdSizeMinus  = 9306;
const UINT kIdSizePlus   = 9307;
const UINT kIdSizeValue  = 9308;
const UINT kIdPreview    = 9309;
const UINT kIdQuickBase  = 9320;   // 第 i 个字号快选 = kIdQuickBase + i
const UINT kIdSwatchBase = 9340;   // 第 i 个颜色块   = kIdSwatchBase + i

// 把 0xRRGGBB 转成 COLORREF（0x00BBGGRR）。
COLORREF RgbToColorRef(unsigned int rgb)
{
    return RGB((rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF);
}

// 把 COLORREF 的 GDI+ 不透明色。
Gdiplus::Color ToGdip(COLORREF c)
{
    return Gdiplus::Color(255, GetRValue(c), GetGValue(c), GetBValue(c));
}

// 画一个抗锯齿圆角矩形（可选填充 + 可选描边）。
void FillRoundRect(HDC hdc, const RECT& rc, float radius,
                   COLORREF fill, COLORREF border, float borderW)
{
    const float w = (float)(rc.right - rc.left);
    const float h = (float)(rc.bottom - rc.top);
    if (w <= 0.0f || h <= 0.0f)
    {
        return;
    }
    Gdiplus::Graphics g(hdc);
    g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);

    float r = radius;
    if (r > w / 2.0f) { r = w / 2.0f; }
    if (r > h / 2.0f) { r = h / 2.0f; }
    const float inset = (border != CLR_INVALID) ? borderW / 2.0f : 0.0f;
    const float x = (float)rc.left + inset;
    const float y = (float)rc.top + inset;
    const float ww = w - inset * 2.0f;
    const float hh = h - inset * 2.0f;
    const float d = r * 2.0f;

    Gdiplus::GraphicsPath path;
    if (d > 0.0f)
    {
        path.AddArc(x,          y,          d, d, 180.0f, 90.0f);
        path.AddArc(x + ww - d, y,          d, d, 270.0f, 90.0f);
        path.AddArc(x + ww - d, y + hh - d, d, d, 0.0f,   90.0f);
        path.AddArc(x,          y + hh - d, d, d, 90.0f,  90.0f);
        path.CloseFigure();
    }
    else
    {
        path.AddRectangle(Gdiplus::RectF(x, y, ww, hh));
    }
    if (fill != CLR_INVALID)
    {
        Gdiplus::SolidBrush brush(ToGdip(fill));
        g.FillPath(&brush, &path);
    }
    if (border != CLR_INVALID && borderW > 0.0f)
    {
        Gdiplus::Pen pen(ToGdip(border), borderW);
        g.DrawPath(&pen, &path);
    }
}

// 用微软雅黑在 rc 内绘制文字（支持斜体 / 下划线 / 删除线）。
void DrawGdi(HDC hdc, LPCWSTR text, const RECT& rc, int px, bool bold,
             bool italic, bool underline, bool strike, COLORREF color, UINT dtFlags)
{
    if (text == NULL || text[0] == L'\0' || px <= 0)
    {
        return;
    }
    HFONT hFont = ::CreateFontW(
        -px, 0, 0, 0, bold ? FW_SEMIBOLD : FW_NORMAL,
        italic ? TRUE : FALSE, underline ? TRUE : FALSE, strike ? TRUE : FALSE,
        DEFAULT_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Microsoft YaHei");
    if (hFont == NULL)
    {
        return;
    }
    HFONT    hOld   = (HFONT)::SelectObject(hdc, hFont);
    int      oldBk  = ::SetBkMode(hdc, TRANSPARENT);
    COLORREF oldClr = ::SetTextColor(hdc, color);
    RECT r = rc;
    ::DrawTextW(hdc, text, -1, &r, dtFlags);
    ::SetTextColor(hdc, oldClr);
    ::SetBkMode(hdc, oldBk);
    ::SelectObject(hdc, hOld);
    ::DeleteObject(hFont);
}

// =====================================================================
// FontChip —— 样式 / 字号用的方形按钮，可高亮激活态。继承 DuiButton 复用
// 点击 / DUIN_CLICK 通知，只覆写 OnPaint。
// =====================================================================
class FontChip : public DuiButton
{
public:
    FontChip()
        : m_active(false), m_gBold(false), m_gItalic(false)
        , m_gUnderline(false), m_gStrike(false)
    {
    }

    // 设置激活态（激活：橙底白字）。
    void SetActive(bool a) { m_active = a; Invalidate(); }

    // 设置文字本身的字形（让 B/I/U/S 的字母按其代表的样式显示）。
    void SetGlyphStyle(bool bold, bool italic, bool underline, bool strike)
    {
        m_gBold = bold;
        m_gItalic = italic;
        m_gUnderline = underline;
        m_gStrike = strike;
    }

    void OnPaint(HDC hdc, const RECT& /*rcDirty*/) override
    {
        const int w = m_rcItem.right - m_rcItem.left;
        const int h = m_rcItem.bottom - m_rcItem.top;
        if (w <= 0 || h <= 0)
        {
            return;
        }
        if (m_active)
        {
            FillRoundRect(hdc, m_rcItem, 5.0f, kAccent, CLR_INVALID, 0.0f);
        }
        else
        {
            //不可点时没有悬停反馈，底色恒为白
            const COLORREF fill = (IsHover() && IsEnabled()) ? RGB(0xF6, 0xF5, 0xF2) : RGB(0xFF, 0xFF, 0xFF);
            FillRoundRect(hdc, m_rcItem, 5.0f, fill, kChipBorder, 1.0f);
        }
        COLORREF txt = m_active ? RGB(0xFF, 0xFF, 0xFF) : kChipText;
        if (!IsEnabled())
        {
            txt = kChipTextDisabled;
        }
        DrawGdi(hdc, (LPCWSTR)GetText(), m_rcItem, 13, m_gBold,
                m_gItalic, m_gUnderline, m_gStrike, txt,
                DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    }

    bool OnSetCursor(POINT /*pt*/) override { return false; }

private:
    bool m_active;      // 是否激活（高亮）
    bool m_gBold;       // 文字字形：粗
    bool m_gItalic;     // 文字字形：斜
    bool m_gUnderline;  // 文字字形：下划线
    bool m_gStrike;     // 文字字形：删除线
};

// =====================================================================
// ColorSwatch —— 颜色块，选中时画一圈高亮环。
// =====================================================================
class ColorSwatch : public DuiButton
{
public:
    ColorSwatch() : m_color(RGB(0, 0, 0)), m_selected(false) {}

    void SetSwatch(COLORREF c) { m_color = c; }
    void SetSelected(bool s) { m_selected = s; Invalidate(); }

    void OnPaint(HDC hdc, const RECT& /*rcDirty*/) override
    {
        const int w = m_rcItem.right - m_rcItem.left;
        const int h = m_rcItem.bottom - m_rcItem.top;
        if (w <= 0 || h <= 0)
        {
            return;
        }
        // 选中：外圈橙色环 + 内缩色块；未选：色块 + 1px 浅灰边（白色块也可见）。
        if (m_selected)
        {
            FillRoundRect(hdc, m_rcItem, 5.0f, CLR_INVALID, kAccent, 2.0f);
            RECT inner = { m_rcItem.left + 3, m_rcItem.top + 3,
                           m_rcItem.right - 3, m_rcItem.bottom - 3 };
            FillRoundRect(hdc, inner, 3.0f, m_color, RGB(0xCF, 0xCD, 0xC8), 1.0f);
        }
        else
        {
            FillRoundRect(hdc, m_rcItem, 5.0f, m_color, RGB(0xCF, 0xCD, 0xC8), 1.0f);
        }
    }

    bool OnSetCursor(POINT /*pt*/) override { return false; }

private:
    COLORREF m_color;     // 色块颜色（COLORREF）
    bool     m_selected;  // 是否选中
};

// =====================================================================
// FontPreview —— 按当前字体设置实时渲染示例文字。
// =====================================================================
class FontPreview : public DuiControl
{
public:
    void SetPreviewFont(const net::TextFont& f) { m_font = f; Invalidate(); }

    void OnPaint(HDC hdc, const RECT& /*rcDirty*/) override
    {
        FillRoundRect(hdc, m_rcItem, 6.0f, kPreviewBg, RGB(0xE7, 0xE5, 0xE1), 1.0f);
        int px = m_font.m_size;
        if (px > 22) { px = 22; }   // 预览框高有限，超大字号压一下
        if (px < 8)  { px = 8;  }
        RECT r = { m_rcItem.left + 12, m_rcItem.top, m_rcItem.right - 12, m_rcItem.bottom };
        DrawGdi(hdc, L"样式预览文字 Preview", r, px,
                m_font.m_bold, m_font.m_italic, m_font.m_underline, m_font.m_strike,
                RgbToColorRef(m_font.m_colorRgb),
                DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
    }

    bool OnSetCursor(POINT /*pt*/) override { return false; }

private:
    net::TextFont m_font;
};

// 造一行标签（"样式" / "字号" / "颜色"）。
std::unique_ptr<DuiControl> MakeRowLabel(LPCTSTR text)
{
    std::unique_ptr<DuiLabel> lb(new DuiLabel());
    lb->SetText(text);
    lb->SetTextColor(kLabelText);
    lb->SetTextAlign(DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    return std::unique_ptr<DuiControl>(lb.release());
}

// 造一个 FontChip。
std::unique_ptr<DuiControl> MakeChip(UINT id, LPCTSTR text)
{
    std::unique_ptr<FontChip> c(new FontChip());
    c->SetCtrlId(id);
    c->SetText(text);
    return std::unique_ptr<DuiControl>(c.release());
}

} // anonymous namespace

// =======================================================================
// 构造 / 析构
// =======================================================================

CFontSettingPopup::CFontSettingPopup()
    : m_cb(NULL)
    , m_ud(NULL)
    , m_useDefaultCb(NULL)
    , m_useDefaultUd(NULL)
    , m_useDefaultEnabled(false)
    , m_built(false)
    , m_pRoot(NULL)
    , m_pSizeValue(NULL)
    , m_pPreview(NULL)
    , m_pUseDefault(NULL)
{
    // 默认字体：14px、黑、无样式（m_valid 先置 true，使面板始终代表一份具体字体）。
    m_font.m_valid     = true;
    m_font.m_size      = 14;
    m_font.m_colorRgb  = 0x1A1A1A;
}

CFontSettingPopup::~CFontSettingPopup()
{
    // 控件树由 DuiHost 基类析构释放。
}

void CFontSettingPopup::SetChangeCallback(FontChangeCallback cb, void* userdata)
{
    m_cb = cb;
    m_ud = userdata;
}

void CFontSettingPopup::SetUseDefaultCallback(UseDefaultCallback cb, void* userdata)
{
    m_useDefaultCb = cb;
    m_useDefaultUd = userdata;
}

void CFontSettingPopup::SetUseDefaultEnabled(bool enabled)
{
    m_useDefaultEnabled = enabled;
    if (m_pUseDefault != NULL)
    {
        m_pUseDefault->SetEnabled(enabled);
    }
}

void CFontSettingPopup::SetFont(const net::TextFont& font)
{
    m_font = font;
    m_font.m_valid = true;
    if (m_font.m_size < kSizeMin) { m_font.m_size = kSizeMin; }
    if (m_font.m_size > kSizeMax) { m_font.m_size = kSizeMax; }
    if (m_built)
    {
        RefreshControls();
    }
}

// =======================================================================
// 控件树
// =======================================================================

void CFontSettingPopup::BuildContentOnce()
{
    if (m_built)
    {
        return;
    }
    typedef DuiLayout::Hint Hint;

    std::unique_ptr<DuiVBox> root(new DuiVBox());
    root->SetBgColor(kPanelBg);
    root->SetBorderColor(kPanelBorder);
    root->SetBorderWidth(1.0f);
    root->SetCornerRadius(10);
    root->SetPadding(14, 14, 14, 14);
    root->SetGap(12);

    // ---- 样式行：标签 + B/I/U/S + 清除 ----
    std::unique_ptr<DuiHBox> styleRow(new DuiHBox());
    styleRow->SetGap(6);
    styleRow->AddChild(MakeRowLabel(_T("样式")), Hint().Fixed(40));
    {
        std::unique_ptr<DuiControl> b = MakeChip(kIdBold, _T("B"));
        ((FontChip*)b.get())->SetGlyphStyle(true, false, false, false);
        styleRow->AddChild(std::move(b), Hint().Fixed(30));
        std::unique_ptr<DuiControl> i = MakeChip(kIdItalic, _T("I"));
        ((FontChip*)i.get())->SetGlyphStyle(false, true, false, false);
        styleRow->AddChild(std::move(i), Hint().Fixed(30));
        std::unique_ptr<DuiControl> u = MakeChip(kIdUnderline, _T("U"));
        ((FontChip*)u.get())->SetGlyphStyle(false, false, true, false);
        styleRow->AddChild(std::move(u), Hint().Fixed(30));
        std::unique_ptr<DuiControl> s = MakeChip(kIdStrike, _T("S"));
        ((FontChip*)s.get())->SetGlyphStyle(false, false, false, true);
        styleRow->AddChild(std::move(s), Hint().Fixed(30));
    }
    styleRow->AddChild(std::unique_ptr<DuiControl>(new DuiControl()), Hint().Weight(1));
    styleRow->AddChild(MakeChip(kIdClear, _T("清除")), Hint().Fixed(50));
    root->AddChild(std::move(styleRow), Hint().Fixed(30));

    // ---- 字号行：标签 + − 值 + + 快选 ----
    std::unique_ptr<DuiHBox> sizeRow(new DuiHBox());
    sizeRow->SetGap(6);
    sizeRow->AddChild(MakeRowLabel(_T("字号")), Hint().Fixed(40));
    sizeRow->AddChild(MakeChip(kIdSizeMinus, _T("−")), Hint().Fixed(28));
    {
        std::unique_ptr<DuiLabel> val(new DuiLabel());
        val->SetCtrlId(kIdSizeValue);
        val->SetText(_T("14"));
        val->SetTextColor(RGB(0x2A, 0x2A, 0x2A));
        val->SetTextAlign(DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        m_pSizeValue = val.get();
        sizeRow->AddChild(std::unique_ptr<DuiControl>(val.release()), Hint().Fixed(38));
    }
    sizeRow->AddChild(MakeChip(kIdSizePlus, _T("+")), Hint().Fixed(28));
    sizeRow->AddChild(std::unique_ptr<DuiControl>(new DuiControl()), Hint().Fixed(8));
    {
        for (int i = 0; i < kQuickCount; ++i)
        {
            CString t;
            t.Format(_T("%d"), kQuickSizes[i]);
            sizeRow->AddChild(MakeChip(kIdQuickBase + (UINT)i, t), Hint().Fixed(34));
        }
    }
    sizeRow->AddChild(std::unique_ptr<DuiControl>(new DuiControl()), Hint().Weight(1));
    root->AddChild(std::move(sizeRow), Hint().Fixed(30));

    // ---- 颜色行：标签 + 一排色块 ----
    std::unique_ptr<DuiHBox> colorRow(new DuiHBox());
    colorRow->SetGap(6);
    colorRow->AddChild(MakeRowLabel(_T("颜色")), Hint().Fixed(40));
    for (int i = 0; i < kPaletteCount; ++i)
    {
        std::unique_ptr<ColorSwatch> sw(new ColorSwatch());
        sw->SetCtrlId(kIdSwatchBase + (UINT)i);
        sw->SetSwatch(RgbToColorRef(kPalette[i]));
        colorRow->AddChild(std::unique_ptr<DuiControl>(sw.release()), Hint().Fixed(24));
    }
    colorRow->AddChild(std::unique_ptr<DuiControl>(new DuiControl()), Hint().Weight(1));
    root->AddChild(std::move(colorRow), Hint().Fixed(24));

    // ---- 预览 ----
    {
        std::unique_ptr<FontPreview> pv(new FontPreview());
        pv->SetCtrlId(kIdPreview);
        m_pPreview = pv.get();
        root->AddChild(std::unique_ptr<DuiControl>(pv.release()), Hint().Fixed(40));
    }

    // ---- 「使用默认字体」：只有宿主装了对应回调（聊天窗）才有这一行，靠右放 ----
    int extraH = 0;
    if (m_useDefaultCb != NULL)
    {
        std::unique_ptr<DuiHBox> defaultRow(new DuiHBox());
        defaultRow->AddChild(std::unique_ptr<DuiControl>(new DuiControl()), Hint().Weight(1));
        std::unique_ptr<DuiControl> chip = MakeChip(kIdUseDefault, _T("使用默认字体"));
        chip->SetEnabled(m_useDefaultEnabled);
        m_pUseDefault = chip.get();
        defaultRow->AddChild(std::move(chip), Hint().Fixed(kUseDefaultChipW));
        root->AddChild(std::move(defaultRow), Hint().Fixed(kUseDefaultRowH));
        extraH = kPanelRowGap + kUseDefaultRowH;
    }

    m_pRoot = root.get();
    SetContent(std::unique_ptr<DuiControl>(root.release()));

    // 面板宽高（逻辑像素）：宽容纳字号行，高 = padding + 各行 + gap。
    SetSize(360, 14 + 30 + 12 + 30 + 12 + 24 + 12 + 40 + 14 + extraH);
    // toolbar 在窗口底部，面板优先朝上弹（越界时 DuiPopupHost 自动翻边）。
    SetEdge(balloonwjui::DuiPopupHost::EdgeAbove);

    m_built = true;
    RefreshControls();
}

void CFontSettingPopup::RefreshControls()
{
    if (m_pRoot == NULL)
    {
        return;
    }

    // B/I/U/S 激活态。
    FontChip* b = dynamic_cast<FontChip*>(m_pRoot->FindCtrlById(kIdBold));
    if (b != NULL) { b->SetActive(m_font.m_bold); }
    FontChip* i = dynamic_cast<FontChip*>(m_pRoot->FindCtrlById(kIdItalic));
    if (i != NULL) { i->SetActive(m_font.m_italic); }
    FontChip* u = dynamic_cast<FontChip*>(m_pRoot->FindCtrlById(kIdUnderline));
    if (u != NULL) { u->SetActive(m_font.m_underline); }
    FontChip* s = dynamic_cast<FontChip*>(m_pRoot->FindCtrlById(kIdStrike));
    if (s != NULL) { s->SetActive(m_font.m_strike); }

    // 字号数值 + 快选激活态。
    if (m_pSizeValue != NULL)
    {
        CString t;
        t.Format(_T("%d"), m_font.m_size);
        ((DuiLabel*)m_pSizeValue)->SetText(t);
    }
    for (int k = 0; k < kQuickCount; ++k)
    {
        FontChip* q = dynamic_cast<FontChip*>(m_pRoot->FindCtrlById(kIdQuickBase + (UINT)k));
        if (q != NULL)
        {
            q->SetActive(m_font.m_size == kQuickSizes[k]);
        }
    }

    // 颜色块选中态。
    for (int c = 0; c < kPaletteCount; ++c)
    {
        ColorSwatch* sw = dynamic_cast<ColorSwatch*>(
            m_pRoot->FindCtrlById(kIdSwatchBase + (UINT)c));
        if (sw != NULL)
        {
            sw->SetSelected((m_font.m_colorRgb & 0xFFFFFF) == (kPalette[c] & 0xFFFFFF));
        }
    }

    // 预览。
    if (m_pPreview != NULL)
    {
        ((FontPreview*)m_pPreview)->SetPreviewFont(m_font);
    }
}

void CFontSettingPopup::NotifyChange()
{
    if (m_cb != NULL)
    {
        m_cb(m_ud, m_font);
    }
}

void CFontSettingPopup::OpenAt(const RECT& anchorScreen, HWND /*owner*/)
{
    BuildContentOnce();
    RefreshControls();
    // owner 传 NULL：DuiHost::SendNotify 以 GetParent(m_hWnd) 为通知目标，与
    // EmojiPickerPopup 同口径。
    Show(anchorScreen, NULL);
    LOG_INFO(_T("字体设置面板已打开。"));
}

// =======================================================================
// 子控件通知
// =======================================================================

LRESULT CFontSettingPopup::OnChildNotify(UINT /*uMsg*/, WPARAM /*wParam*/,
                                         LPARAM lParam, BOOL& bHandled)
{
    const balloonwjui::DuiNotify* pNotify =
        reinterpret_cast<const balloonwjui::DuiNotify*>(lParam);
    if (pNotify == NULL || pNotify->code != balloonwjui::DUIN_CLICK)
    {
        bHandled = FALSE;
        return 0;
    }

    if (HandleClick(pNotify->ctrlId))
    {
        return 0;
    }

    bHandled = FALSE;
    return 0;
}

bool CFontSettingPopup::HandleClick(UINT ctrlId)
{
    const UINT id = ctrlId;

    //「使用默认字体」：不改面板里的字体设置，只回调宿主，由宿主删掉会话自定义字体、
    //改用全局默认后再经 SetFont 回显。没有该按钮或按钮不可点时不处理
    if (id == kIdUseDefault)
    {
        if (m_useDefaultCb == NULL || !m_useDefaultEnabled)
        {
            return false;
        }
        m_useDefaultCb(m_useDefaultUd);
        return true;
    }

    bool handled = true;

    if (id == kIdBold)
    {
        m_font.m_bold = !m_font.m_bold;
    }
    else if (id == kIdItalic)
    {
        m_font.m_italic = !m_font.m_italic;
    }
    else if (id == kIdUnderline)
    {
        m_font.m_underline = !m_font.m_underline;
    }
    else if (id == kIdStrike)
    {
        m_font.m_strike = !m_font.m_strike;
    }
    else if (id == kIdClear)
    {
        // 清除：仅清四个字形开关（保留字号与颜色）。
        m_font.m_bold = false;
        m_font.m_italic = false;
        m_font.m_underline = false;
        m_font.m_strike = false;
    }
    else if (id == kIdSizeMinus)
    {
        m_font.m_size -= kSizeStep;
        if (m_font.m_size < kSizeMin) { m_font.m_size = kSizeMin; }
    }
    else if (id == kIdSizePlus)
    {
        m_font.m_size += kSizeStep;
        if (m_font.m_size > kSizeMax) { m_font.m_size = kSizeMax; }
    }
    else if (id >= kIdQuickBase && id < kIdQuickBase + (UINT)kQuickCount)
    {
        m_font.m_size = kQuickSizes[id - kIdQuickBase];
    }
    else if (id >= kIdSwatchBase && id < kIdSwatchBase + (UINT)kPaletteCount)
    {
        m_font.m_colorRgb = kPalette[id - kIdSwatchBase] & 0xFFFFFF;
    }
    else
    {
        handled = false;
    }

    if (handled)
    {
        m_font.m_valid = true;
        RefreshControls();
        NotifyChange();
    }
    return handled;
}
