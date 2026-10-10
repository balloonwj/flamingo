/**
 *  图片无法直接显示时的占位符：排版计算与绘制的实现，ImagePlaceholder.cpp
 *  balloonwj@qq.com   2026-09-30
 */

#include <windows.h>
#include <gdiplus.h>

#include "ImagePlaceholder.h"
#include "ChatMessageMenu.h"   // kChatImageState*：聊天气泡图片状态的取值
#include "../../Common/I18n/I18n.h"   // TR：界面文字的多语言查找

namespace balloonchatwnd {

namespace {

// 百分比换算的基数。
const int kPercentBase = 100;

// GDI+ 颜色的不透明 alpha 值。
const BYTE kOpaqueAlpha = 255;

// 下载标记外圈描边、箭头所用的白色。
const COLORREF kBadgeWhite = RGB(0xFF, 0xFF, 0xFF);

// 箭头头部折线的顶点数：左翼末端、尖端、右翼末端。
const int kArrowHeadPointCount = 3;

// 圆角矩形四段圆弧的角度（度）：每段四分之一圈，依次从左上角的 180 度开始。
const Gdiplus::REAL kQuarterTurnDeg = 90.0f;

// ---- 照片图标 ----

// 图标内部的底色（比占位块底色浅，使图标从底色上浮出来）。
const COLORREF kGlyphFill = RGB(0xFA, 0xF9, 0xF7);

// 圆角半径占图标宽度的比例。
const Gdiplus::REAL kGlyphCornerRatio = 0.14f;

// 描边宽度 = 图标宽度 / kGlyphPenDivisor，但不细于 kGlyphPenMinPx（再细在 20 像素
// 的图标上几乎看不见）。
const Gdiplus::REAL kGlyphPenDivisor = 16.0f;
const Gdiplus::REAL kGlyphPenMinPx   = 1.5f;

// 太阳：圆心位置（x 占图标宽度、y 占图标高度的比例）与半径（占图标宽度的比例）。
const Gdiplus::REAL kSunCenterXRatio = 0.30f;
const Gdiplus::REAL kSunCenterYRatio = 0.33f;
const Gdiplus::REAL kSunRadiusRatio  = 0.075f;

// 山脊折线的顶点：x 占图标宽度、y 占图标高度的比例，从左到右。
const int kMountainPointCount = 5;
const Gdiplus::REAL kMountainPoints[kMountainPointCount][2] =
{
    { 0.14f, 0.80f },   // 左山脚
    { 0.37f, 0.53f },   // 左峰
    { 0.52f, 0.68f },   // 两峰之间的谷
    { 0.67f, 0.49f },   // 右峰
    { 0.86f, 0.80f },   // 右山脚
};

// ---- 破图的裂缝 ----

// 裂缝折线的顶点：比例同上，自上而下；首尾两点分别落在图标的上下边缘。
const int kCrackPointCount = 5;
const Gdiplus::REAL kCrackPoints[kCrackPointCount][2] =
{
    { 0.60f, 0.00f },
    { 0.47f, 0.30f },
    { 0.60f, 0.54f },
    { 0.45f, 0.78f },
    { 0.52f, 1.00f },
};

// 两半沿水平方向各自错开的距离：图标宽度的 kCrackGapRatio，但不少于 kCrackGapMinPx。
const Gdiplus::REAL kCrackGapRatio = 0.035f;
const Gdiplus::REAL kCrackGapMinPx = 1.5f;

// 竖直方向的错开距离占水平错开距离的比例：左半往左下、右半往右上。
const Gdiplus::REAL kCrackGapYFactor = 0.5f;

// 两半各自倾斜的角度（度）。图标宽度小于 kCrackTiltSmallBelow 像素时加大到
// kCrackTiltSmallDeg，否则缩小之后裂开的效果看不出来。
const Gdiplus::REAL kCrackTiltDeg        = 4.0f;
const Gdiplus::REAL kCrackTiltSmallDeg   = 7.0f;
const Gdiplus::REAL kCrackTiltSmallBelow = 30.0f;

// 裁剪多边形向图标外延伸的距离，按图标宽度的倍数计：两半倾斜、错开之后仍须完整落在
// 各自的裁剪区里。
const Gdiplus::REAL kClipReachFactor = 2.0f;

// ---- 下载标记与加载圆环 ----

// 外圈白色描边的宽度（像素），把标记与身后的照片图标隔开。
const Gdiplus::REAL kBadgeRingPx = 1.5f;

// 箭头竖线的半长占直径的比例。
const Gdiplus::REAL kArrowHalfRatio = 0.26f;

// 箭头尖端在圆心下方的距离，占竖线半长的比例。
const Gdiplus::REAL kArrowTipRatio = 0.85f;

// 箭头两翼末端的横向张开距离，占竖线半长的比例。
const Gdiplus::REAL kArrowWingXRatio = 0.8f;

// 箭头两翼末端在圆心下方的距离，占竖线半长的比例。
const Gdiplus::REAL kArrowWingYRatio = 0.05f;

// 箭头描边宽度 = 直径 / kArrowPenDivisor，但不细于 kArrowPenMinPx。
const Gdiplus::REAL kArrowPenDivisor = 9.0f;
const Gdiplus::REAL kArrowPenMinPx   = 1.4f;

// 加载圆环的描边宽度 = 直径 / kSpinnerPenDivisor，但不细于 kSpinnerPenMinPx。
const Gdiplus::REAL kSpinnerPenDivisor = 7.0f;
const Gdiplus::REAL kSpinnerPenMinPx   = 1.6f;

// 加载圆环的圆弧：从正上方（-90 度）起，顺时针画 270 度。
const Gdiplus::REAL kSpinnerStartDeg = -90.0f;
const Gdiplus::REAL kSpinnerSweepDeg = 270.0f;

// 加载圆环未走到的那一段的轨道色（很浅的蓝）。
const COLORREF kSpinnerTrack = RGB(0xDD, 0xE6, 0xF4);

// 把 COLORREF 转成不透明的 GDI+ 颜色。
Gdiplus::Color ToColor(COLORREF c)
{
    return Gdiplus::Color(kOpaqueAlpha, GetRValue(c), GetGValue(c), GetBValue(c));
}

// 图标高度（像素）：宽度的 kImagePlaceholderGlyphHeightPercent%。排版与绘制都用它。
int GlyphHeightOf(int glyphSize)
{
    return glyphSize * kImagePlaceholderGlyphHeightPercent / kPercentBase;
}

Gdiplus::REAL GlyphHeightOf(Gdiplus::REAL glyphSize)
{
    return glyphSize * (Gdiplus::REAL)kImagePlaceholderGlyphHeightPercent / (Gdiplus::REAL)kPercentBase;
}

Gdiplus::REAL MaxReal(Gdiplus::REAL a, Gdiplus::REAL b)
{
    return (a > b) ? a : b;
}

// 往 path 里加一个圆角矩形。圆角直径超过宽或高时按宽 / 高收缩。
void AddRoundRect(Gdiplus::GraphicsPath& path, Gdiplus::REAL x, Gdiplus::REAL y,
                  Gdiplus::REAL w, Gdiplus::REAL h, Gdiplus::REAL radius)
{
    Gdiplus::REAL d = radius * 2;
    if (d > w)
    {
        d = w;
    }
    if (d > h)
    {
        d = h;
    }
    path.AddArc(x, y, d, d, kQuarterTurnDeg * 2, kQuarterTurnDeg);
    path.AddArc(x + w - d, y, d, d, kQuarterTurnDeg * 3, kQuarterTurnDeg);
    path.AddArc(x + w - d, y + h - d, d, d, 0.0f, kQuarterTurnDeg);
    path.AddArc(x, y + h - d, d, d, kQuarterTurnDeg, kQuarterTurnDeg);
    path.CloseFigure();
}

// 在 (x0, y0) 为左上角、宽 w 高 h 的范围内画一个完整的照片图标。调用方负责抗锯齿设置。
void DrawGlyphBody(Gdiplus::Graphics& g, Gdiplus::REAL x0, Gdiplus::REAL y0,
                   Gdiplus::REAL w, Gdiplus::REAL h, COLORREF stroke)
{
    const Gdiplus::REAL penWidth = MaxReal(kGlyphPenMinPx, w / kGlyphPenDivisor);

    Gdiplus::GraphicsPath frame;
    AddRoundRect(frame, x0, y0, w, h, w * kGlyphCornerRatio);
    Gdiplus::SolidBrush fill(ToColor(kGlyphFill));
    g.FillPath(&fill, &frame);

    Gdiplus::Pen pen(ToColor(stroke), penWidth);
    pen.SetLineJoin(Gdiplus::LineJoinRound);
    pen.SetStartCap(Gdiplus::LineCapRound);
    pen.SetEndCap(Gdiplus::LineCapRound);
    g.DrawPath(&pen, &frame);

    const Gdiplus::REAL sunR = w * kSunRadiusRatio;
    Gdiplus::SolidBrush sun(ToColor(stroke));
    g.FillEllipse(&sun, x0 + w * kSunCenterXRatio - sunR, y0 + h * kSunCenterYRatio - sunR,
                  sunR * 2, sunR * 2);

    Gdiplus::PointF ridge[kMountainPointCount];
    for (int i = 0; i < kMountainPointCount; ++i)
    {
        ridge[i].X = x0 + w * kMountainPoints[i][0];
        ridge[i].Y = y0 + h * kMountainPoints[i][1];
    }
    g.DrawLines(&pen, ridge, kMountainPointCount);
}

// 破图的一半：先按 (dx, dy) 平移、再绕 pivot 旋转 angle 度，然后只在 clipPoly 围出的
// 区域里画完整图标。裁剪多边形跟着变换一起移动，所以裂缝两侧各自带着自己那一半走。
void DrawBrokenHalf(Gdiplus::Graphics& g, const Gdiplus::PointF* clipPoly, int clipCount,
                    Gdiplus::REAL dx, Gdiplus::REAL dy, Gdiplus::REAL angle,
                    const Gdiplus::PointF& pivot,
                    Gdiplus::REAL x0, Gdiplus::REAL y0, Gdiplus::REAL w, Gdiplus::REAL h,
                    COLORREF stroke)
{
    const Gdiplus::GraphicsState state = g.Save();
    g.TranslateTransform(dx, dy);
    g.TranslateTransform(pivot.X, pivot.Y);
    g.RotateTransform(angle);
    g.TranslateTransform(-pivot.X, -pivot.Y);

    Gdiplus::GraphicsPath clip;
    clip.AddPolygon(clipPoly, clipCount);
    g.SetClip(&clip, Gdiplus::CombineModeIntersect);

    DrawGlyphBody(g, x0, y0, w, h, stroke);
    g.Restore(state);
}

} // anonymous namespace

// =======================================================================
// 排版计算
// =======================================================================

ImagePlaceholderLayout ComputeImagePlaceholderLayout(const RECT& rc, int captionW, int captionH)
{
    ImagePlaceholderLayout out;
    out.m_drawGlyph     = false;
    out.m_glyphSize     = 0;
    out.m_glyphCenter.x = 0;
    out.m_glyphCenter.y = 0;
    out.m_showCaption   = false;
    out.m_captionRect.left   = 0;
    out.m_captionRect.top    = 0;
    out.m_captionRect.right  = 0;
    out.m_captionRect.bottom = 0;

    const int w = rc.right - rc.left;
    const int h = rc.bottom - rc.top;
    if (w <= 0 || h <= 0)
    {
        return out;
    }

    //常规尺寸：短边的一定比例，夹在上下限之间。
    const int shortSide = (w < h) ? w : h;
    int glyph = shortSide * kImagePlaceholderGlyphPercent / kPercentBase;
    if (glyph < kImagePlaceholderGlyphMin)
    {
        glyph = kImagePlaceholderGlyphMin;
    }
    if (glyph > kImagePlaceholderGlyphMax)
    {
        glyph = kImagePlaceholderGlyphMax;
    }

    //占位块放不下常规尺寸时缩到恰好放下（四周各留一小段距离）。高度方向要按图标的
    //高宽比折算回宽度。
    const int maxByWidth  = w - kImagePlaceholderTightMargin * 2;
    const int maxByHeight = (h - kImagePlaceholderTightMargin * 2) * kPercentBase
                            / kImagePlaceholderGlyphHeightPercent;
    if (glyph > maxByWidth)
    {
        glyph = maxByWidth;
    }
    if (glyph > maxByHeight)
    {
        glyph = maxByHeight;
    }
    if (glyph < kImagePlaceholderGlyphAbsMin)
    {
        return out;   //小到画不出能辨认的图标，干脆不画
    }

    const int glyphH = GlyphHeightOf(glyph);

    //说明文字要在四周都留得出边距、并与图标隔开一段距离时才显示。
    bool showCaption = false;
    if (captionW > 0 && captionH > 0)
    {
        const bool fitsW = (w >= captionW + kImagePlaceholderCaptionMargin * 2);
        const bool fitsH = (h >= glyphH + kImagePlaceholderCaptionGap + captionH
                                 + kImagePlaceholderCaptionMargin * 2);
        showCaption = (fitsW && fitsH);
    }

    //图标（连同文字）作为一个整体居中。
    const int blockH = showCaption ? (glyphH + kImagePlaceholderCaptionGap + captionH) : glyphH;
    const int top = rc.top + (h - blockH) / 2;

    out.m_drawGlyph     = true;
    out.m_glyphSize     = glyph;
    out.m_glyphCenter.x = rc.left + w / 2;
    out.m_glyphCenter.y = top + glyphH / 2;
    out.m_showCaption   = showCaption;
    if (showCaption)
    {
        out.m_captionRect.left   = rc.left + (w - captionW) / 2;
        out.m_captionRect.top    = top + glyphH + kImagePlaceholderCaptionGap;
        out.m_captionRect.right  = out.m_captionRect.left + captionW;
        out.m_captionRect.bottom = out.m_captionRect.top + captionH;
    }
    return out;
}

SIZE MeasureImagePlaceholderCaption(HDC hdc, LPCWSTR text, int pixelHeight)
{
    SIZE size;
    size.cx = 0;
    size.cy = 0;
    if (hdc == NULL || text == NULL || text[0] == L'\0' || pixelHeight <= 0)
    {
        return size;
    }

    //参数与 ChatBubble.cpp 的 DrawGdiText 逐项一致（微软雅黑、常规字重、ClearType），
    //量出来的宽度才与画出来的对得上。
    HFONT font = ::CreateFontW(-pixelHeight, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                               DEFAULT_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS,
                               CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE,
                               i18n::UiFontName());
    if (font == NULL)
    {
        return size;
    }
    HFONT oldFont = (HFONT)::SelectObject(hdc, font);
    ::GetTextExtentPoint32W(hdc, text, (int)::wcslen(text), &size);
    ::SelectObject(hdc, oldFont);
    ::DeleteObject(font);
    return size;
}

// =======================================================================
// 聊天气泡：失败判断
// =======================================================================

ChatImageFailure ResolveChatImageFailure(int imageState, bool hasPath, bool decodeFailed)
{
    //状态本身就是失败：上传失败与下载失败各有各的说明文字。
    if (imageState == kChatImageStateFailed)
    {
        return kChatImageSendFailed;
    }
    if (imageState == kChatImageStateDownloadFailed)
    {
        return kChatImageLoadFailed;
    }
    //"正常"态但画不出来：本地文件丢失或损坏。没有路径的示例图不在此列。
    if (imageState == kChatImageStateReady && hasPath && decodeFailed)
    {
        return kChatImageLoadFailed;
    }
    return kChatImageNotFailed;
}

LPCWSTR ChatImageFailureCaption(ChatImageFailure failure)
{
    switch (failure)
    {
    //本端发出的图片没有上传成功
    case kChatImageSendFailed:
        return TR(L"图片发送失败");

    //收到的图片没下载下来，或本地文件丢失、损坏
    case kChatImageLoadFailed:
        return TR(L"图片加载失败");

    //不失败：不画说明文字
    default:
        return L"";
    }
}

COLORREF ChatImageFailureCaptionColor(ChatImageFailure failure)
{
    return (failure == kChatImageSendFailed) ? kImagePlaceholderCaptionRed
                                             : kImagePlaceholderCaptionGray;
}

// =======================================================================
// 消息记录窗：图片缩略图的状态判断
// =======================================================================

HistoryThumbDecision ResolveHistoryThumb(bool downloadable, bool fileExists, bool downloading,
                                         bool downloadFailed, bool decodeFailed)
{
    HistoryThumbDecision d;
    d.m_state     = kHistoryThumbImage;
    d.m_clickable = false;
    d.m_tooltip   = L"";

    //下载中优先：此时本地可能已有写了一半的文件，不能按"已有原图"去解码。
    if (downloading)
    {
        d.m_state = kHistoryThumbDownloading;
        return d;
    }

    //本地已有原图：能解码就显示缩略图，损坏的按破图。重新下载也只会拿到同一份内容
    //（按内容 MD5 缓存），所以不提供单击。
    if (fileExists)
    {
        d.m_state = decodeFailed ? kHistoryThumbBroken : kHistoryThumbImage;
        return d;
    }

    //本地没有原图。
    if (downloadFailed)
    {
        d.m_state     = kHistoryThumbBroken;
        d.m_clickable = downloadable;
        d.m_tooltip   = downloadable ? TR(L"下载失败，单击重试") : L"";
        return d;
    }
    if (downloadable)
    {
        d.m_state     = kHistoryThumbNeedDownload;
        d.m_clickable = true;
        d.m_tooltip   = TR(L"单击下载图片");
        return d;
    }
    d.m_state = kHistoryThumbBroken;   //缺少下载信息（老消息缺字段等），无从下载
    return d;
}

// =======================================================================
// GDI+ 绘制
// =======================================================================

void DrawPhotoGlyph(Gdiplus::Graphics& g, float cx, float cy, float size, COLORREF stroke)
{
    if (size <= 0.0f)
    {
        return;
    }
    const Gdiplus::GraphicsState state = g.Save();
    g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    const Gdiplus::REAL h = GlyphHeightOf((Gdiplus::REAL)size);
    DrawGlyphBody(g, cx - size / 2, cy - h / 2, size, h, stroke);
    g.Restore(state);
}

void DrawBrokenImageGlyph(Gdiplus::Graphics& g, float cx, float cy, float size, COLORREF stroke)
{
    if (size <= 0.0f)
    {
        return;
    }
    const Gdiplus::GraphicsState state = g.Save();
    g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);

    const Gdiplus::REAL w  = size;
    const Gdiplus::REAL h  = GlyphHeightOf((Gdiplus::REAL)size);
    const Gdiplus::REAL x0 = cx - w / 2;
    const Gdiplus::REAL y0 = cy - h / 2;
    const Gdiplus::REAL gap   = MaxReal(kCrackGapMinPx, w * kCrackGapRatio);
    const Gdiplus::REAL tilt  = (w < kCrackTiltSmallBelow) ? kCrackTiltSmallDeg : kCrackTiltDeg;
    const Gdiplus::REAL reach = w * kClipReachFactor;

    //裂缝两侧的裁剪多边形：裂缝折线本身，加上在它首尾各补一点把折线延伸出图标上下
    //边缘，再绕到左侧（或右侧）很远处闭合。
    const int polyCount = kCrackPointCount + 4;
    Gdiplus::PointF left[polyCount];
    Gdiplus::PointF right[polyCount];
    const Gdiplus::REAL crackTopX    = x0 + w * kCrackPoints[0][0];
    const Gdiplus::REAL crackBottomX = x0 + w * kCrackPoints[kCrackPointCount - 1][0];

    left[0]  = Gdiplus::PointF(x0 - reach, y0 - reach);
    right[0] = Gdiplus::PointF(x0 + w + reach, y0 - reach);
    left[1]  = Gdiplus::PointF(crackTopX, y0 - reach);
    right[1] = left[1];
    for (int i = 0; i < kCrackPointCount; ++i)
    {
        left[i + 2]  = Gdiplus::PointF(x0 + w * kCrackPoints[i][0], y0 + h * kCrackPoints[i][1]);
        right[i + 2] = left[i + 2];
    }
    left[polyCount - 2]  = Gdiplus::PointF(crackBottomX, y0 + h + reach);
    right[polyCount - 2] = left[polyCount - 2];
    left[polyCount - 1]  = Gdiplus::PointF(x0 - reach, y0 + h + reach);
    right[polyCount - 1] = Gdiplus::PointF(x0 + w + reach, y0 + h + reach);

    //两半都绕图标底边中点旋转：左半往左下错开、逆时针倾斜；右半往右上错开、顺时针倾斜。
    const Gdiplus::PointF pivot(x0 + w / 2, y0 + h);
    DrawBrokenHalf(g, left, polyCount, -gap, gap * kCrackGapYFactor, -tilt, pivot,
                   x0, y0, w, h, stroke);
    DrawBrokenHalf(g, right, polyCount, gap, -gap * kCrackGapYFactor, tilt, pivot,
                   x0, y0, w, h, stroke);

    g.Restore(state);
}

void DrawDownloadBadge(Gdiplus::Graphics& g, float cx, float cy, float diameter, COLORREF fill)
{
    if (diameter <= 0.0f)
    {
        return;
    }
    const Gdiplus::GraphicsState state = g.Save();
    g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);

    const Gdiplus::REAL r = diameter / 2;
    Gdiplus::SolidBrush ring(ToColor(kBadgeWhite));
    g.FillEllipse(&ring, cx - r - kBadgeRingPx, cy - r - kBadgeRingPx,
                  diameter + kBadgeRingPx * 2, diameter + kBadgeRingPx * 2);
    Gdiplus::SolidBrush disc(ToColor(fill));
    g.FillEllipse(&disc, cx - r, cy - r, diameter, diameter);

    Gdiplus::Pen pen(ToColor(kBadgeWhite),
                     MaxReal(kArrowPenMinPx, diameter / kArrowPenDivisor));
    pen.SetStartCap(Gdiplus::LineCapRound);
    pen.SetEndCap(Gdiplus::LineCapRound);
    pen.SetLineJoin(Gdiplus::LineJoinRound);
    const Gdiplus::REAL half = diameter * kArrowHalfRatio;
    const Gdiplus::REAL tipY = cy + half * kArrowTipRatio;
    g.DrawLine(&pen, cx, cy - half, cx, tipY);
    const Gdiplus::PointF head[kArrowHeadPointCount] =
    {
        Gdiplus::PointF(cx - half * kArrowWingXRatio, cy + half * kArrowWingYRatio),
        Gdiplus::PointF(cx, tipY),
        Gdiplus::PointF(cx + half * kArrowWingXRatio, cy + half * kArrowWingYRatio),
    };
    g.DrawLines(&pen, head, kArrowHeadPointCount);

    g.Restore(state);
}

void DrawDownloadingBadge(Gdiplus::Graphics& g, float cx, float cy, float diameter)
{
    if (diameter <= 0.0f)
    {
        return;
    }
    const Gdiplus::GraphicsState state = g.Save();
    g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);

    const Gdiplus::REAL r = diameter / 2;
    Gdiplus::SolidBrush ring(ToColor(kBadgeWhite));
    g.FillEllipse(&ring, cx - r - kBadgeRingPx, cy - r - kBadgeRingPx,
                  diameter + kBadgeRingPx * 2, diameter + kBadgeRingPx * 2);

    //圆弧画在描边宽度以内，避免粗描边的外沿超出标记的直径。
    const Gdiplus::REAL penWidth = MaxReal(kSpinnerPenMinPx, diameter / kSpinnerPenDivisor);
    const Gdiplus::RectF arc(cx - r + penWidth, cy - r + penWidth,
                             diameter - penWidth * 2, diameter - penWidth * 2);
    Gdiplus::Pen track(ToColor(kSpinnerTrack), penWidth);
    g.DrawEllipse(&track, arc);
    Gdiplus::Pen pen(ToColor(kImagePlaceholderBadgeBlue), penWidth);
    pen.SetStartCap(Gdiplus::LineCapRound);
    pen.SetEndCap(Gdiplus::LineCapRound);
    g.DrawArc(&pen, arc, kSpinnerStartDeg, kSpinnerSweepDeg);

    g.Restore(state);
}

void PaintImagePlaceholderShapes(Gdiplus::Graphics& g, const RECT& rc,
                                 const ImagePlaceholderLayout& layout, COLORREF bg)
{
    const int w = rc.right - rc.left;
    const int h = rc.bottom - rc.top;
    if (w <= 0 || h <= 0)
    {
        return;
    }
    Gdiplus::SolidBrush brush(ToColor(bg));
    g.FillRectangle(&brush, (Gdiplus::REAL)rc.left, (Gdiplus::REAL)rc.top,
                    (Gdiplus::REAL)w, (Gdiplus::REAL)h);
    if (layout.m_drawGlyph)
    {
        DrawBrokenImageGlyph(g, (float)layout.m_glyphCenter.x, (float)layout.m_glyphCenter.y,
                             (float)layout.m_glyphSize, kImagePlaceholderGlyphStroke);
    }
}

} // namespace balloonchatwnd
