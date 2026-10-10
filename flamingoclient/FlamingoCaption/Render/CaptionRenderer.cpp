/**
 *  字幕的绘制，实现见 CaptionRenderer.h。
 *
 *  balloonwj@qq.com   2026-10-06
 */
#include "stdafx.h"

#include "CaptionRenderer.h"

#include <objidl.h>
#include <gdiplus.h>

#include "../Model/CaptionLayout.h"

namespace caption {

namespace {

//字幕字体：依次尝试（理由见头文件）
const wchar_t* const kFontFaces[] = { L"Microsoft YaHei", L"SimHei", L"Microsoft JhengHei" };
//行高与字号之比
const float kLineSpacing = 1.3f;
//字幕最宽占可用宽度的百分比
const int kMaxWidthPercent = 92;
const int kPercent = 100;
//说话人前缀的颜色（黄色）与中间结果的文字颜色（浅灰），0xRRGGBB
const uint32_t kSpeakerColor = 0xFFD54F;
const uint32_t kPartialColor = 0xD0D0D0;
//完全不透明
const BYTE kOpaque = 255;
//底框在文字左右留出的空白与字号之比
const float kBoxPaddingRatio = 0.3f;
//避开画中画后可用宽度至少占图层宽度的几分之一，太窄就不避开
const int kMinSpanDivisor = 3;
//避开画中画时字幕与它之间的空隙与字号之比
const float kAvoidGapRatio = 0.5f;
//避开判断时字幕带的高度按几行估计
const int kBandRows = 3;
//字号下限（像素）
const int kMinFontPx = 10;
//全角冒号：说话人前缀「某某：」
const wchar_t kFullColon = 0xFF1A;
//基准画面高度，见 kCaptionStyleBaseHeight
const int kBaseHeight = kCaptionStyleBaseHeight;

Gdiplus::Color ToColor(uint32_t rgb, BYTE alpha)
{
    return Gdiplus::Color(alpha, static_cast<BYTE>((rgb >> 16) & 0xFF), static_cast<BYTE>((rgb >> 8) & 0xFF),
                          static_cast<BYTE>(rgb & 0xFF));
}

//测量上下文：折行时交给 WrapCaptionText 的测量函数
struct MeasureContext
{
    Gdiplus::Graphics*           m_graphics;   //测量用的 Graphics
    Gdiplus::Font*               m_font;       //字体
    const Gdiplus::StringFormat* m_format;     //版式（GenericTypographic，计入行尾空格）
};

float MeasureThunk(void* ctx, const wchar_t* text, int len)
{
    MeasureContext* m = static_cast<MeasureContext*>(ctx);
    if (len <= 0)
        return 0.0f;
    Gdiplus::RectF box;
    m->m_graphics->MeasureString(text, len, m->m_font, Gdiplus::PointF(0.0f, 0.0f), m->m_format, &box);
    return box.Width;
}

//选第一个系统里有的字体；都没有时用系统默认的无衬线字体
Gdiplus::FontFamily* CreateFamily()
{
    for (size_t i = 0; i < sizeof(kFontFaces) / sizeof(kFontFaces[0]); ++i)
    {
        Gdiplus::FontFamily* f = new Gdiplus::FontFamily(kFontFaces[i]);
        if (f->IsAvailable())
            return f;
        delete f;
    }
    return Gdiplus::FontFamily::GenericSansSerif()->Clone();
}

//折好的一行
struct Row
{
    std::wstring m_text;        //这一行的文字（含前缀部分）
    size_t       m_prefixLen;   //开头属于说话人前缀的字符数
    bool         m_partial;     //是否中间结果
};

} // namespace

CaptionPaintLine::CaptionPaintLine()
    : m_partial(false)
{
}

CaptionPaintParams::CaptionPaintParams()
    : m_width(0)
    , m_height(0)
    , m_refHeight(kBaseHeight)
    , m_hasAvoid(false)
{
    m_avoid.left = 0;
    m_avoid.top = 0;
    m_avoid.right = 0;
    m_avoid.bottom = 0;
}

int CaptionRenderer::Scale(int px, int refHeight)
{
    const int v = static_cast<int>(static_cast<int64_t>(px) * refHeight / kBaseHeight);
    return v > 0 ? v : 1;
}

bool CaptionRenderer::Paint(const std::vector<CaptionPaintLine>& lines, const CaptionPaintParams& p, uint32_t* bgra,
                            RECT* bounds)
{
    if (bounds != NULL)
        SetRectEmpty(bounds);
    if (bgra == NULL || p.m_width <= 0 || p.m_height <= 0 || p.m_refHeight <= 0)
        return false;
    if (lines.empty())
        return true;

    Gdiplus::Bitmap bmp(p.m_width, p.m_height, p.m_width * 4, PixelFormat32bppPARGB, reinterpret_cast<BYTE*>(bgra));
    Gdiplus::Graphics g(&bmp);
    g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    g.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHighQuality);

    int fontPx = Scale(p.m_style.m_fontPx, p.m_refHeight);
    if (fontPx < kMinFontPx)
        fontPx = kMinFontPx;
    const int outline = p.m_style.m_outlinePx > 0 ? Scale(p.m_style.m_outlinePx, p.m_refHeight) : 0;
    const int margin = Scale(p.m_style.m_bottomMarginPx, p.m_refHeight);
    const float lineH = fontPx * kLineSpacing;

    //family 先于 font 声明：析构顺序与声明相反，保证 font 析构时 family 仍有效
    std::unique_ptr<Gdiplus::FontFamily> familyHolder(CreateFamily());
    Gdiplus::FontFamily* family = familyHolder.get();
    Gdiplus::Font font(family, static_cast<Gdiplus::REAL>(fontPx), Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
    Gdiplus::StringFormat fmt(Gdiplus::StringFormat::GenericTypographic());
    fmt.SetFormatFlags(fmt.GetFormatFlags() | Gdiplus::StringFormatFlagsMeasureTrailingSpaces);
    MeasureContext mc;
    mc.m_graphics = &g;
    mc.m_font = &font;
    mc.m_format = &fmt;

    //可用的水平范围：画中画与字幕带在高度上重叠时，改在它旁边较宽的一侧
    float spanL = 0.0f;
    float spanR = static_cast<float>(p.m_width);
    if (p.m_hasAvoid)
    {
        const float bandBottom = static_cast<float>(p.m_height - margin);
        const float bandTop = bandBottom - lineH * kBandRows;
        if (p.m_avoid.bottom > bandTop && p.m_avoid.top < bandBottom)
        {
            const float gap = fontPx * kAvoidGapRatio;
            const float leftSpan = p.m_avoid.left - gap;
            const float rightSpan = p.m_width - (p.m_avoid.right + gap);
            if (leftSpan >= rightSpan && leftSpan >= p.m_width / kMinSpanDivisor)
                spanR = leftSpan;
            else if (rightSpan > leftSpan && rightSpan >= p.m_width / kMinSpanDivisor)
                spanL = p.m_avoid.right + gap;
        }
    }
    const float maxWidth = (spanR - spanL) * kMaxWidthPercent / kPercent;
    const float cx = (spanL + spanR) / 2.0f;

    //折行
    std::vector<Row> rows;
    for (size_t i = 0; i < lines.size(); ++i)
    {
        std::wstring prefix;
        if (p.m_style.m_showSpeaker && !lines[i].m_speaker.empty())
            prefix = lines[i].m_speaker + kFullColon;
        const std::vector<std::wstring> wrapped = WrapCaptionText(prefix + lines[i].m_text, maxWidth, &MeasureThunk, &mc);
        for (size_t k = 0; k < wrapped.size(); ++k)
        {
            Row r;
            r.m_text = wrapped[k];
            r.m_prefixLen = (k == 0) ? (prefix.size() < wrapped[k].size() ? prefix.size() : wrapped[k].size()) : 0;
            r.m_partial = lines[i].m_partial;
            rows.push_back(r);
        }
    }

    //从底部往上画
    Gdiplus::Pen pen(ToColor(p.m_style.m_outlineColor, kOpaque), static_cast<Gdiplus::REAL>(outline * 2));
    pen.SetLineJoin(Gdiplus::LineJoinRound);
    Gdiplus::SolidBrush textBrush(ToColor(p.m_style.m_textColor, kOpaque));
    Gdiplus::SolidBrush partialBrush(ToColor(kPartialColor, kOpaque));
    Gdiplus::SolidBrush speakerBrush(ToColor(kSpeakerColor, kOpaque));
    Gdiplus::SolidBrush boxBrush(ToColor(p.m_style.m_outlineColor, static_cast<BYTE>(p.m_style.m_boxAlpha)));
    RECT drawn;
    SetRectEmpty(&drawn);
    float y = static_cast<float>(p.m_height - margin);
    for (size_t n = rows.size(); n > 0; --n)
    {
        const Row& r = rows[n - 1];
        const float top = y - lineH;
        const float width = MeasureThunk(&mc, r.m_text.c_str(), static_cast<int>(r.m_text.size()));
        const float prefixW = MeasureThunk(&mc, r.m_text.c_str(), static_cast<int>(r.m_prefixLen));
        const float x0 = cx - width / 2.0f;
        const float textTop = top + (lineH - fontPx) / 2.0f;
        if (p.m_style.m_boxAlpha > 0)
        {
            const float pad = fontPx * kBoxPaddingRatio;
            g.FillRectangle(&boxBrush, x0 - pad, top, width + pad * 2.0f, lineH);
        }
        Gdiplus::GraphicsPath prefixPath;
        Gdiplus::GraphicsPath textPath;
        if (r.m_prefixLen > 0)
        {
            prefixPath.AddString(r.m_text.c_str(), static_cast<INT>(r.m_prefixLen), family, Gdiplus::FontStyleBold,
                                 static_cast<Gdiplus::REAL>(fontPx), Gdiplus::PointF(x0, textTop), &fmt);
        }
        if (r.m_text.size() > r.m_prefixLen)
        {
            textPath.AddString(r.m_text.c_str() + r.m_prefixLen, static_cast<INT>(r.m_text.size() - r.m_prefixLen),
                               family, Gdiplus::FontStyleBold, static_cast<Gdiplus::REAL>(fontPx),
                               Gdiplus::PointF(x0 + prefixW, textTop), &fmt);
        }
        if (outline > 0)
        {
            g.DrawPath(&pen, &prefixPath);
            g.DrawPath(&pen, &textPath);
        }
        g.FillPath(&speakerBrush, &prefixPath);
        g.FillPath(r.m_partial ? &partialBrush : &textBrush, &textPath);

        RECT rowRect;
        rowRect.left = static_cast<LONG>(x0) - outline;
        rowRect.top = static_cast<LONG>(top);
        rowRect.right = static_cast<LONG>(x0 + width) + outline + 1;
        rowRect.bottom = static_cast<LONG>(top + lineH) + 1;
        UnionRect(&drawn, &drawn, &rowRect);
        y = top;
    }
    if (bounds != NULL)
    {
        RECT all;
        all.left = 0;
        all.top = 0;
        all.right = p.m_width;
        all.bottom = p.m_height;
        IntersectRect(bounds, &drawn, &all);
    }
    return true;
}

} // namespace caption
