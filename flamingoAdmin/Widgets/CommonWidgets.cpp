/**
 *  flamingoAdmin —— 公共业务控件与辅助函数实现。
 *
 *  balloonwj@qq.com   2026.05.21
 */
#include "stdafx.h"
#include "Widgets/CommonWidgets.h"
#include "Widgets/UiHelpers.h"
#include "Widgets/IconRenderer.h"
#include "Theme/AdminTheme.h"

#include "Controls/Layout/DuiLayout.h"   // DuiVBox::PaintBackground(卡片底统一走 balloonui)
#include "Controls/Basic/DuiLabel.h"     // StatTile 三行内容用 DuiLabel 子控件呈现

#include "DuiNotify.h"

#include <tchar.h>

namespace flamingoadmin { namespace ui {

// 本文件在 namespace flamingoadmin::ui 内，theme:: 直接解析到 flamingoadmin::theme。

namespace {

// ---- 字号（磅）----
const int kEyebrowPt   = 8;    // 页头 eyebrow 小标签
const int kPageTitlePt = 17;   // 页头主标题
const int kStatValuePt = 18;   // 统计卡数值

// ---- PageHeader 布局（96 dpi 逻辑像素）----
const int kHdrPadL      = 24;  // 页头左内距
const int kHdrPadR      = 24;  // 页头右内距
const int kHdrPadTop    = 22;  // eyebrow 距页头顶部
const int kHdrEyebrowH  = 15;  // eyebrow 行高
const int kHdrTitleH    = 32;  // 标题行高
const int kHdrSubGap    = 3;   // 标题与副标题间距
const int kHdrSubH      = 18;  // 副标题行高
const int kHdrActionH   = 34;  // 右侧操作按钮高度
const int kHdrActionGap = 8;   // 操作按钮间距

// ---- StatTile 布局(StatTile 构造函数里使用) ----
const int kStatPadH    = 16;   // 统计卡左右内距(像素)
const int kStatPadTop  = 14;   // 统计卡顶部内距(像素)
const int kStatLabelH  = 15;   // 第 1 行:标签行高(像素)
const int kStatValueH  = 30;   // 第 2 行:数值行高(像素)
const int kStatGap1    = 4;    // 第 1/2 行之间间距(像素,挂在 value 的 marginT 上)
const int kStatDeltaH  = 16;   // 第 3 行:变化量行高(像素)

// ---- SegControl 布局 ----
const int kSegInnerPad = 2;    // 外槽与分段之间的内边距
const int kSegRadius   = 7;    // 外槽圆角

// ---- 徽章 / 圆点 ----
const int kChipPadH    = 9;    // 徽章左右内距
const int kChipDotSize = 6;    // 徽章内圆点直径
const int kChipDotGap  = 6;    // 圆点与文字间距
const int kSeverityDot = 8;    // 级别圆点直径

// 量算一段文字在指定字体下的宽度（像素）。
int MeasureTextW(LPCTSTR text, HFONT font)
{
    HDC   dc      = ::GetDC(nullptr);
    HFONT oldFont = static_cast<HFONT>(::SelectObject(dc, font));
    SIZE  sz      = { 0, 0 };
    ::GetTextExtentPoint32(dc, text, lstrlen(text), &sz);
    ::SelectObject(dc, oldFont);
    ::ReleaseDC(nullptr, dc);
    return sz.cx;
}

// 判断字符是否落在 CJK 统一汉字区。
bool IsCjk(TCHAR ch)
{
    return ch >= 0x4E00 && ch <= 0x9FA5;
}

} // anonymous namespace

// ===========================================================================
// 数据辅助
// ===========================================================================
CString Initials(const CString& name)
{
    CString s = name;
    s.Trim();
    if (s.IsEmpty())
    {
        return CString(_T("?"));
    }

    // 含中文：长度 <= 2 取全部，否则取末 2 字。
    bool hasCjk = false;
    for (int i = 0; i < s.GetLength(); ++i)
    {
        if (IsCjk(s[i]))
        {
            hasCjk = true;
            break;
        }
    }
    if (hasCjk)
    {
        return s.GetLength() <= 2 ? s : s.Right(2);
    }

    // 纯英文：取前两个单词的首字母，大写。
    CString out;
    bool    inWord = false;
    for (int i = 0; i < s.GetLength() && out.GetLength() < 2; ++i)
    {
        const TCHAR ch = s[i];
        if (ch == _T(' ') || ch == _T('\t'))
        {
            inWord = false;
        }
        else if (!inWord)
        {
            out += static_cast<TCHAR>(_totupper(ch));
            inWord = true;
        }
    }
    return out.IsEmpty() ? s.Left(1) : out;
}

COLORREF AvatarColor(const CString& seed)
{
    // 与设计稿 COLORS_AVATAR 一致的 8 色调色板。
    static const COLORREF kPalette[] =
    {
        RGB(0xFF, 0x75, 0x63), RGB(0x28, 0x64, 0xD4),
        RGB(0x8E, 0x5B, 0xD9), RGB(0x1F, 0x9D, 0x55),
        RGB(0xD8, 0x8A, 0x17), RGB(0x0E, 0xA5, 0xA4),
        RGB(0xDB, 0x27, 0x77), RGB(0x47, 0x55, 0x69),
    };
    const int kPaletteSize = sizeof(kPalette) / sizeof(kPalette[0]);

    // 与设计稿 avatarColor() 相同的 32 位滚动哈希：h = h*31 + ch。
    int h = 0;
    for (int i = 0; i < seed.GetLength(); ++i)
    {
        h = h * 31 + static_cast<int>(seed[i]);
    }
    int idx = h % kPaletteSize;
    if (idx < 0)
    {
        idx += kPaletteSize;
    }
    return kPalette[idx];
}

COLORREF SeverityColor(Severity sev)
{
    switch (sev)
    {
    case Severity::Warn:   return theme::kColorWarn;
    case Severity::Danger: return theme::kColorDanger;
    case Severity::Normal: return theme::kColorOk;
    case Severity::Info:
    default:               return theme::kColorInfo;
    }
}

COLORREF AccentColor(Accent accent)
{
    switch (accent)
    {
    case Accent::Ok:   return theme::kColorOk;
    case Accent::Warn: return theme::kColorWarn;
    case Accent::Info: return theme::kColorInfo;
    case Accent::None:
    default:           return theme::kInk400;
    }
}

LPCTSTR EnterpriseStatusLabel(EnterpriseStatus status)
{
    switch (status)
    {
    case EnterpriseStatus::Active:    return _T("活跃");
    case EnterpriseStatus::Pending:   return _T("待审核");
    case EnterpriseStatus::Suspended: return _T("已停用");
    default:                          return _T("未知");
    }
}

// 取企业状态徽章的语义色（圆点 / 文字）。
static COLORREF EnterpriseStatusColor(EnterpriseStatus status)
{
    switch (status)
    {
    case EnterpriseStatus::Active:    return theme::kColorOk;
    case EnterpriseStatus::Pending:   return theme::kColorWarn;
    case EnterpriseStatus::Suspended: return theme::kColorInfo;
    default:                          return theme::kInk400;
    }
}

// ===========================================================================
// 自绘辅助
// ===========================================================================
void DrawCard(HDC hdc, const RECT& rc)
{
    // 转调 balloonui 统一卡片底实现, 让 owner-draw 流程与 DuiVBox 控件
    // (开了卡片样式)走同一段绘制 —— 视觉永远一致, 后续 admin 想改卡
    // 片色板只动这一行 theme 参数即可。
    balloonwjui::DuiVBox::PaintBackground(hdc, rc,
        theme::kInk0, theme::kRadiusLg, theme::kInk100, 1.0f);
}

void DrawSeverityDot(HDC hdc, int cx, int cy, Severity sev)
{
    RECT rc =
    {
        cx - kSeverityDot / 2, cy - kSeverityDot / 2,
        cx + kSeverityDot / 2, cy + kSeverityDot / 2,
    };
    FillRoundRect(hdc, rc, SeverityColor(sev), kSeverityDot / 2);
}

int ChipWidth(HDC /*hdc*/, LPCTSTR text, bool hasDot)
{
    const int textW = MeasureTextW(text, UiFont(theme::kFontSizeSmall, false));
    int w = kChipPadH + textW + kChipPadH;
    if (hasDot)
    {
        w += kChipDotSize + kChipDotGap;
    }
    return w;
}

void DrawChip(HDC hdc, const RECT& rc, LPCTSTR text,
              COLORREF textClr, COLORREF bgClr, bool hasDot, COLORREF dotClr)
{
    FillRoundRect(hdc, rc, bgClr, theme::kRadiusSm);

    int textLeft = rc.left + kChipPadH;
    if (hasDot)
    {
        const int cy = (rc.top + rc.bottom) / 2;
        RECT rcDot =
        {
            rc.left + kChipPadH,                 cy - kChipDotSize / 2,
            rc.left + kChipPadH + kChipDotSize,  cy + kChipDotSize / 2,
        };
        FillRoundRect(hdc, rcDot, dotClr, kChipDotSize / 2);
        textLeft += kChipDotSize + kChipDotGap;
    }

    RECT rcText = { textLeft, rc.top, rc.right - kChipPadH, rc.bottom };
    DrawTextLine(hdc, text, rcText, textClr, UiFont(theme::kFontSizeSmall, false),
                 DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
}

int StatusChipWidth(HDC hdc, EnterpriseStatus status)
{
    return ChipWidth(hdc, EnterpriseStatusLabel(status), true);
}

int DrawStatusChip(HDC hdc, int left, int cy, EnterpriseStatus status)
{
    // 徽章固定高度 20px。
    const int kChipH = 20;
    LPCTSTR   text   = EnterpriseStatusLabel(status);
    const int w      = ChipWidth(hdc, text, true);
    RECT rc = { left, cy - kChipH / 2, left + w, cy + kChipH / 2 };
    DrawChip(hdc, rc, text, theme::kInk600, theme::kInk50,
             true, EnterpriseStatusColor(status));
    return w;
}

void DrawAvatar(HDC hdc, const RECT& rc, COLORREF bgColor, LPCTSTR text)
{
    const int h = rc.bottom - rc.top;
    // 圆角方块底（圆角约为边长的 30%，接近设计稿头像的弧度）。
    FillRoundRect(hdc, rc, bgColor, h * 3 / 10);
    // 居中白色缩写，字号随头像大小自适应。
    int pt = h / 3;
    if (pt < 8)
    {
        pt = 8;
    }
    DrawTextLine(hdc, text, rc, theme::kInk0, UiFont(pt, true),
                 DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
}

// ===========================================================================
// PageHeader
// ===========================================================================
void PageHeader::SetTexts(LPCTSTR eyebrow, LPCTSTR title, LPCTSTR subtitle)
{
    m_eyebrow  = eyebrow;
    m_title    = title;
    m_subtitle = subtitle;
    Invalidate();
}

void PageHeader::AddAction(std::unique_ptr<balloonwjui::DuiControl> action, int width)
{
    m_actionWidths.push_back(width);
    AddChild(std::move(action));
}

void PageHeader::Layout(const RECT& rcAvail)
{
    m_rcItem = rcAvail;

    // 操作按钮从右往左依次摆放，垂直居中于页头。
    const int cy   = (m_rcItem.top + m_rcItem.bottom) / 2;
    int       right = m_rcItem.right - kHdrPadR;
    for (int i = static_cast<int>(m_children.size()) - 1; i >= 0; --i)
    {
        const int w = (i < static_cast<int>(m_actionWidths.size()))
                          ? m_actionWidths[i] : 96;
        RECT rc =
        {
            right - w, cy - kHdrActionH / 2,
            right,     cy + kHdrActionH / 2,
        };
        m_children[i]->ForceLayout(rc);
        right -= w + kHdrActionGap;
    }
}

void PageHeader::OnPaint(HDC hdc, const RECT& rcDirty)
{
    if (!m_bVisible)
    {
        return;
    }

    int y = m_rcItem.top + kHdrPadTop;

    // eyebrow：品牌色小标签。
    RECT rcEyebrow = { m_rcItem.left + kHdrPadL, y,
                       m_rcItem.right - kHdrPadR, y + kHdrEyebrowH };
    DrawTextLine(hdc, m_eyebrow, rcEyebrow, theme::kFlamingo600,
                 UiFont(kEyebrowPt, true),
                 DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    y += kHdrEyebrowH;

    // 主标题。
    RECT rcTitle = { m_rcItem.left + kHdrPadL, y,
                     m_rcItem.right - kHdrPadR, y + kHdrTitleH };
    DrawTextLine(hdc, m_title, rcTitle, theme::kInk700,
                 UiFont(kPageTitlePt, true),
                 DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    y += kHdrTitleH + kHdrSubGap;

    // 副标题（可空）。
    if (!m_subtitle.IsEmpty())
    {
        RECT rcSub = { m_rcItem.left + kHdrPadL, y,
                       m_rcItem.right - kHdrPadR, y + kHdrSubH };
        DrawTextLine(hdc, m_subtitle, rcSub, theme::kInk400,
                     UiFont(theme::kFontSizeNormal, false),
                     DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    }

    // 再绘制右侧操作按钮。
    balloonwjui::DuiControl::OnPaint(hdc, rcDirty);
}

// ===========================================================================
// StatTile —— 卡片底 + 3 个 DuiLabel 子控件
//
// 自绘版被替换为 "DuiVBox(开卡片样式) + 3 个 DuiLabel" 的组合,卡片底由
// DuiVBox 基类的 4 个卡片样式 setter 接管,三行文字由 DuiLabel 各自负责
// (字体 / 颜色 / 单行截断 / Invalidate),本类不再 override OnPaint。
// API 仍只暴露 SetContent 一个函数,与历史完全一致。
// ===========================================================================
StatTile::StatTile()
{
    // ---- 卡片底:开 DuiVBox 4 个卡片样式 setter,基类 OnPaint 会自己画 ----
    SetBgColor      (theme::kInk0);          // 白色卡片底
    SetCornerRadius (theme::kRadiusLg);      // 与设计稿一致的大圆角
    SetBorderColor  (theme::kInk100);        // 浅灰描边
    SetBorderWidth  (1.0f);                  // 1px 描边线宽

    // ---- 容器排布:左右内距 16、顶部内距 14、底部不留;子之间无 gap,
    //      第 1/2 行之间的 4px 间距由 value 的 marginTop 承担,与原自绘
    //      版的 kStatGap1 一一对应。 ----
    SetPadding(kStatPadH, kStatPadTop, kStatPadH, 0);
    SetGap(0);

    // ---- 第 1 行:指标名(小号字 / 中灰) ----
    std::unique_ptr<balloonwjui::DuiLabel> lblLabel(new balloonwjui::DuiLabel());
    lblLabel->SetFont(UiFont(theme::kFontSizeSmall, false));
    lblLabel->SetTextColor(theme::kInk400);
    m_lblLabel = lblLabel.get();
    AddChild(std::move(lblLabel), Hint().Fixed(kStatLabelH));

    // ---- 第 2 行:指标值(18pt 半粗 / 主文字色),marginTop=4 = 原 kStatGap1 ----
    std::unique_ptr<balloonwjui::DuiLabel> lblValue(new balloonwjui::DuiLabel());
    lblValue->SetFont(UiFont(kStatValuePt, true));
    lblValue->SetTextColor(theme::kInk700);
    m_lblValue = lblValue.get();
    AddChild(std::move(lblValue), Hint().Fixed(kStatValueH).Margin(0, kStatGap1, 0, 0));

    // ---- 第 3 行:变化量(小号字,初始按 Accent::None 中性灰;空 delta 时
    //      由 SetContent 走 SetVisible(false) 隐藏,DuiVBox::Layout 会自动
    //      跳过不可见子的位置计算) ----
    std::unique_ptr<balloonwjui::DuiLabel> lblDelta(new balloonwjui::DuiLabel());
    lblDelta->SetFont(UiFont(theme::kFontSizeSmall, false));
    lblDelta->SetTextColor(AccentColor(Accent::None));
    m_lblDelta = lblDelta.get();
    AddChild(std::move(lblDelta), Hint().Fixed(kStatDeltaH));
}

void StatTile::SetContent(LPCTSTR label, LPCTSTR value, LPCTSTR delta,
                          Accent accent)
{
    // label / value 恒可见,直接转写;DuiLabel::SetText 内部已 Invalidate。
    m_lblLabel->SetText(label);
    m_lblValue->SetText(value);

    // delta 处理:空串 → 隐藏整行;非空 → 写文字 + 着色 + 显示。
    // 这样既保留了"空 delta 不出现"的历史视觉,又能在二次回填非空
    // delta 时自动恢复显示。
    const bool hasDelta = (delta != nullptr && delta[0] != _T('\0'));
    if (hasDelta)
    {
        m_lblDelta->SetText(delta);
        m_lblDelta->SetTextColor(AccentColor(accent));
        m_lblDelta->SetVisible(true);
    }
    else
    {
        m_lblDelta->SetText(_T(""));
        m_lblDelta->SetVisible(false);
    }
}

// ===========================================================================
// SegControl
// ===========================================================================
void SegControl::AddSegment(LPCTSTR label)
{
    m_labels.push_back(CString(label));
}

void SegControl::SetActiveIndex(int index)
{
    if (index >= 0 && index < static_cast<int>(m_labels.size()))
    {
        m_active = index;
        Invalidate();
    }
}

void SegControl::SegmentRects(std::vector<RECT>& out) const
{
    out.clear();
    const int n = static_cast<int>(m_labels.size());
    if (n == 0)
    {
        return;
    }
    // 外槽内缩 kSegInnerPad 后等分给各分段。
    const int areaL = m_rcItem.left   + kSegInnerPad;
    const int areaT = m_rcItem.top    + kSegInnerPad;
    const int areaR = m_rcItem.right  - kSegInnerPad;
    const int areaB = m_rcItem.bottom - kSegInnerPad;
    const int areaW = areaR - areaL;
    for (int i = 0; i < n; ++i)
    {
        RECT rc =
        {
            areaL + areaW * i / n,       areaT,
            areaL + areaW * (i + 1) / n, areaB,
        };
        out.push_back(rc);
    }
}

void SegControl::OnPaint(HDC hdc, const RECT& /*rcDirty*/)
{
    if (!m_bVisible)
    {
        return;
    }

    // 外槽：浅灰底 + 1px 边框。
    FillRoundRect(hdc, m_rcItem, theme::kInk50, kSegRadius);
    StrokeRoundRect(hdc, m_rcItem, theme::kInk100, kSegRadius, 1.0f);

    std::vector<RECT> rects;
    SegmentRects(rects);
    for (int i = 0; i < static_cast<int>(rects.size()); ++i)
    {
        const bool active = (i == m_active);
        if (active)
        {
            // 选中段：白色凸起底。
            FillRoundRect(hdc, rects[i], theme::kInk0, kSegRadius - kSegInnerPad);
        }
        DrawTextLine(hdc, m_labels[i], rects[i],
                     active ? theme::kInk700 : theme::kInk500,
                     UiFont(theme::kFontSizeNormal, active),
                     DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    }
}

bool SegControl::OnLButtonUp(POINT pt, UINT /*mkFlags*/)
{
    std::vector<RECT> rects;
    SegmentRects(rects);
    for (int i = 0; i < static_cast<int>(rects.size()); ++i)
    {
        if (::PtInRect(&rects[i], pt))
        {
            if (i != m_active)
            {
                m_active = i;
                Invalidate();
                NotifyParent(balloonwjui::DUIN_VALUECHANGED, i);
            }
            return true;
        }
    }
    return true;
}

bool SegControl::OnSetCursor(POINT /*pt*/)
{
    ::SetCursor(::LoadCursor(nullptr, IDC_HAND));
    return true;
}

// 注:历史 CardBox::OnPaint 已删除 —— 改用 balloonwjui::DuiVBox + 4 个
//    卡片样式 setter, 见 EnterprisesScreen / NewEnterpriseModal 的用法。

}} // namespace flamingoadmin::ui
