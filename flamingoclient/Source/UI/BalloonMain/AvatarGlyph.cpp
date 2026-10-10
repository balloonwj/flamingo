/**
 *  AvatarGlyph.cpp
 *
 *  字形头像居中画字的实现，说明见 AvatarGlyph.h。
 *
 *  本文件与 FlamingoUnit 测试工程共用；include 的 stdafx.h 在测试工程里解析为 Tests/stdafx.h。
 *
 *  balloonwj@qq.com   2026-10-03
 */
#include "stdafx.h"
#include "AvatarGlyph.h"

#include <gdiplus.h>

namespace balloonmain {

namespace {

// 头像画字使用的字体，与原先各处的 DrawGdiText 相同。
const wchar_t* const kGlyphFontFace = L"Microsoft YaHei";

// GetGlyphOutlineW 失败时的返回值。
const DWORD kGlyphOutlineError = GDI_ERROR;

// 单位矩阵（16.16 定点数），GetGlyphOutlineW 要求必须传变换矩阵。
MAT2 IdentityMat2()
{
    MAT2 m;
    ::ZeroMemory(&m, sizeof(m));
    m.eM11.value = 1;
    m.eM22.value = 1;
    return m;
}

// UTF-16 代理项的取值范围（高、低代理合在一起）。
const wchar_t kSurrogateFirst = 0xD800;
const wchar_t kSurrogateLast  = 0xDFFF;

// GetGlyphIndicesW 在 GGI_MARK_NONEXISTING_GLYPHS 下给字体里没有的字填的字形编号。
const WORD kMissingGlyphIndex = 0xFFFF;

// 用 hdc 当前选入的字体，求 text 以 TA_TOP 方式从原点书写时全部笔画的外框。有任何一个字取不到
// 可靠的度量时返回 false，由调用方退回别的画法：
//   · UTF-16 代理项（emoji 等）：GetGlyphOutlineW 按单个 UTF-16 单元查字形，对代理项给出的是
//     「缺字方框」的度量，不是实际画出来的那个字；
//   · 字体里没有的字：TextOutW 会借用别的字体画出来，而这里只能取到本字体缺字方框的度量。
bool MeasureInkFromOrigin(HDC hdc, LPCWSTR text, int len, RECT& out)
{
    for (int i = 0; i < len; ++i)
    {
        if (text[i] >= kSurrogateFirst && text[i] <= kSurrogateLast)
        {
            return false;
        }
        WORD index = 0;
        if (::GetGlyphIndicesW(hdc, text + i, 1, &index, GGI_MARK_NONEXISTING_GLYPHS) == GDI_ERROR
            || index == kMissingGlyphIndex)
        {
            return false;
        }
    }

    TEXTMETRICW tm;
    if (!::GetTextMetricsW(hdc, &tm))
    {
        return false;
    }
    const MAT2 ident = IdentityMat2();
    int pen = 0;
    bool any = false;
    for (int i = 0; i < len; ++i)
    {
        GLYPHMETRICS gm;
        ::ZeroMemory(&gm, sizeof(gm));
        if (::GetGlyphOutlineW(hdc, (UINT)text[i], GGO_METRICS, &gm, 0, NULL, &ident) == kGlyphOutlineError)
        {
            return false;
        }
        // 黑框横向从原点 + gmptGlyphOrigin.x 开始；纵向 gmptGlyphOrigin.y 是黑框上沿在基线以上的高度，
        // 而 TA_TOP 方式下基线位于原点之下 tmAscent 处。空白字符的黑框宽高为 1，不影响外框
        const RECT r = { pen + gm.gmptGlyphOrigin.x,
                         tm.tmAscent - gm.gmptGlyphOrigin.y,
                         pen + gm.gmptGlyphOrigin.x + (LONG)gm.gmBlackBoxX,
                         tm.tmAscent - gm.gmptGlyphOrigin.y + (LONG)gm.gmBlackBoxY };
        if (!any)
        {
            out = r;
            any = true;
        }
        else
        {
            out.left   = (r.left < out.left) ? r.left : out.left;
            out.top    = (r.top < out.top) ? r.top : out.top;
            out.right  = (r.right > out.right) ? r.right : out.right;
            out.bottom = (r.bottom > out.bottom) ? r.bottom : out.bottom;
        }
        pen += gm.gmCellIncX;
    }
    return any;
}

} // anonymous namespace

POINT ComputeCenteredTextOrigin(const RECT& box, const RECT& inkFromOrg)
{
    const LONG boxW = box.right - box.left;
    const LONG boxH = box.bottom - box.top;
    const LONG inkW = inkFromOrg.right - inkFromOrg.left;
    const LONG inkH = inkFromOrg.bottom - inkFromOrg.top;
    // 差值为奇数时整除向下取整，外框靠左 / 靠上，多出的一个像素留在右侧 / 下侧
    POINT org;
    org.x = box.left + (boxW - inkW) / 2 - inkFromOrg.left;
    org.y = box.top + (boxH - inkH) / 2 - inkFromOrg.top;
    return org;
}

void FillCenteredGlyph(Gdiplus::Graphics& g, LPCWSTR text, int len,
                       const Gdiplus::FontFamily& family, int fontStyle, float emSizePx,
                       const Gdiplus::RectF& box, const Gdiplus::Brush& brush)
{
    if (text == NULL || len <= 0 || text[0] == L'\0' || emSizePx <= 0.0f)
    {
        return;
    }

    // 用排版无附加留白的格式（GenericTypographic）把字转成轮廓路径。起点无关紧要，下面按外框平移
    Gdiplus::StringFormat fmt(Gdiplus::StringFormat::GenericTypographic());
    Gdiplus::GraphicsPath path;
    path.AddString(text, len, &family, fontStyle, emSizePx, Gdiplus::PointF(0.0f, 0.0f), &fmt);

    Gdiplus::RectF bounds;
    path.GetBounds(&bounds);
    if (bounds.Width <= 0.0f || bounds.Height <= 0.0f)
    {
        return;   // 空白字符没有笔画
    }

    Gdiplus::Matrix shift;
    shift.Translate(box.X + box.Width / 2.0f - (bounds.X + bounds.Width / 2.0f),
                    box.Y + box.Height / 2.0f - (bounds.Y + bounds.Height / 2.0f));
    path.Transform(&shift);
    g.FillPath(&brush, &path);
}

void FillAvatarBackground(Gdiplus::Graphics& g, const Gdiplus::Brush& brush, const Gdiplus::GraphicsPath& path)
{
    const Gdiplus::PixelOffsetMode oldMode = g.GetPixelOffsetMode();
    g.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);
    g.FillPath(&brush, &path);
    g.SetPixelOffsetMode(oldMode);
}

void DrawCenteredGlyphGdi(HDC hdc, LPCWSTR text, const RECT& box, int pixelHeight, bool bold, COLORREF color)
{
    if (hdc == NULL || text == NULL || text[0] == L'\0' || pixelHeight <= 0)
    {
        return;
    }
    HFONT hFont = ::CreateFontW(
        -pixelHeight, 0, 0, 0,
        bold ? FW_SEMIBOLD : FW_NORMAL,
        FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE,
        kGlyphFontFace);
    if (hFont == NULL)
    {
        return;
    }
    HFONT    hOldFont  = (HFONT)::SelectObject(hdc, hFont);
    int      oldBkMode = ::SetBkMode(hdc, TRANSPARENT);
    COLORREF oldColor  = ::SetTextColor(hdc, color);
    UINT     oldAlign  = ::SetTextAlign(hdc, TA_TOP | TA_LEFT | TA_NOUPDATECP);

    const int len = (int)::wcslen(text);
    RECT ink;
    if (MeasureInkFromOrigin(hdc, text, len, ink))
    {
        const POINT org = ComputeCenteredTextOrigin(box, ink);
        ::TextOutW(hdc, org.x, org.y, text, len);
    }
    else
    {
        // 取不到字形度量（如 UTF-16 代理对）：退回原先按排版框居中的画法，至少把字画出来
        RECT r = box;
        ::DrawTextW(hdc, text, -1, &r, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOCLIP | DT_NOPREFIX);
    }

    ::SetTextAlign(hdc, oldAlign);
    ::SetTextColor(hdc, oldColor);
    ::SetBkMode(hdc, oldBkMode);
    ::SelectObject(hdc, hOldFont);
    ::DeleteObject(hFont);
}

} // namespace balloonmain
