/**
 *  Markdown 文档绘制的实现，说明见 MdPaint.h。
 *
 *  balloonwj@qq.com   2026-10-06
 */
#include "MdPaint.h"

#include <algorithm>

//gdiplus.h 依赖 min / max 宏，而工程定义了 NOMINMAX，这里借用 std 版本
namespace Gdiplus
{
    using std::min;
    using std::max;
}
#include <objidl.h>
#include <gdiplus.h>

#include "MdAlert.h"
#include "MdCodeColor.h"
#include "MdFontCache.h"
#include "MdHostLog.h"

namespace flamingomd {

namespace {

//完全不透明
const BYTE kOpaque = 255;

//「复制」按钮底色的不透明度：半透明白底，压在代码文字上仍能看出按钮轮廓
const BYTE kCopyButtonBackAlpha = 230;

//「复制」按钮底色（白）
const BYTE kWhite = 255;

//「复制」按钮图标（两个错开的圆角方块）占按钮边长的百分比，以及两个方块之间的错开距离占图标边长的百分比
const int kCopyIconPercent = 50;
const int kCopyIconOffsetPercent = 30;

//任务框勾号的三个顶点，按任务框边长的百分比给出（左端、底端、右端）
const int kCheckLeftX = 22;
const int kCheckLeftY = 52;
const int kCheckBottomX = 42;
const int kCheckBottomY = 72;
const int kCheckRightX = 78;
const int kCheckRightY = 30;

//勾号线宽占任务框边长的百分比
const int kCheckStrokePercent = 12;

//行内代码底色上下各比字体高度外延的像素
const int kInlineCodeVPad = 1;

//百分比换算的分母
const int kPercent = 100;

//图标水平中线，按边长的百分比
const int kHalfPercent = 50;

//警告框图标的线宽占图标边长的百分比
const int kAlertStrokePercent = 10;

//警告框图标里「!」的竖线上端、下端与圆点圆心，按图标边长的百分比
const int kMarkTopPercent = 28;
const int kMarkBottomPercent = 56;
const int kMarkDotPercent = 74;

//三角形（警告）里的「!」整体下移的距离，按图标边长的百分比：三角形上部窄，居中会显得偏上
const int kTriangleMarkShiftPercent = 10;

//「i」（提示）的圆点圆心、竖线上端与下端，按图标边长的百分比
const int kInfoDotPercent = 30;
const int kInfoTopPercent = 46;
const int kInfoBottomPercent = 74;

//「!」与「i」圆点的半径，按图标边长的百分比
const int kDotRadiusPercent = 7;

//灯泡（技巧）：泡体的圆心纵坐标与半径、两条灯座横线的纵坐标与半宽，按图标边长的百分比
const int kBulbCenterYPercent = 38;
const int kBulbRadiusPercent = 32;
const int kBulbBase1Percent = 80;
const int kBulbBase2Percent = 96;
const int kBulbBaseHalfPercent = 16;

//八边形（注意）的切角长度，按图标边长的百分比
const int kOctagonCutPercent = 29;

//圆角方框（重要）的圆角半径，按图标边长的百分比
const int kBoxRadiusPercent = 20;

//三角形与八边形的顶点个数
const int kTrianglePoints = 3;
const int kOctagonPoints = 8;

//GDI+ 的启动状态：只启动一次，之后一直保持
INIT_ONCE g_gdiplusOnce = INIT_ONCE_STATIC_INIT;
bool g_gdiplusOk = false;
ULONG_PTR g_gdiplusToken = 0;

BOOL CALLBACK StartGdiplusOnce(PINIT_ONCE once, PVOID param, PVOID* context)
{
    (void)once;
    (void)param;
    (void)context;
    Gdiplus::GdiplusStartupInput input;
    g_gdiplusOk = (Gdiplus::GdiplusStartup(&g_gdiplusToken, &input, NULL) == Gdiplus::Ok);
    if (!g_gdiplusOk)
        MdLogError("paint: GdiplusStartup failed, shapes will not be drawn.");
    return TRUE;
}

//COLORREF 转 GDI+ 颜色
Gdiplus::Color ToColor(uint32_t c, BYTE alpha)
{
    return Gdiplus::Color(alpha, GetRValue(c), GetGValue(c), GetBValue(c));
}

//生成圆角矩形路径
void AddRoundRect(Gdiplus::GraphicsPath& path, const RECT& rc, int radius)
{
    const int w = rc.right - rc.left;
    const int h = rc.bottom - rc.top;
    int d = radius * 2;
    if (d > w)
        d = w;
    if (d > h)
        d = h;
    if (d <= 0)
    {
        path.AddRectangle(Gdiplus::Rect(rc.left, rc.top, w, h));
        return;
    }
    const float kQuarter = 90.0f;
    const float kRight = 0.0f;
    const float kBottom = 90.0f;
    const float kLeft = 180.0f;
    const float kTop = 270.0f;
    path.AddArc(rc.left, rc.top, d, d, kLeft, kQuarter);
    path.AddArc(rc.right - d, rc.top, d, d, kTop, kQuarter);
    path.AddArc(rc.right - d, rc.bottom - d, d, d, kRight, kQuarter);
    path.AddArc(rc.left, rc.bottom - d, d, d, kBottom, kQuarter);
    path.CloseFigure();
}

//填充圆角矩形
void FillRoundRect(Gdiplus::Graphics& g, const RECT& rc, int radius, const Gdiplus::Color& color)
{
    Gdiplus::GraphicsPath path;
    AddRoundRect(path, rc, radius);
    Gdiplus::SolidBrush brush(color);
    g.FillPath(&brush, &path);
}

//描边圆角矩形
void DrawRoundRect(Gdiplus::Graphics& g, const RECT& rc, int radius, const Gdiplus::Color& color, float width)
{
    Gdiplus::GraphicsPath path;
    AddRoundRect(path, rc, radius);
    Gdiplus::Pen pen(color, width);
    g.DrawPath(&pen, &path);
}

//矩形平移
RECT Offset(const RECT& rc, int dx, int dy)
{
    RECT r = { rc.left + dx, rc.top + dy, rc.right + dx, rc.bottom + dy };
    return r;
}

//用 GDI 填充实心矩形
void FillSolid(HDC hdc, const RECT& rc, uint32_t color)
{
    HBRUSH brush = ::CreateSolidBrush(color);
    ::FillRect(hdc, &rc, brush);
    ::DeleteObject(brush);
}

//判断纵向区间 [top, bottom) 是否与可见区间相交
bool VisibleY(int top, int bottom, int visTop, int visBottom)
{
    return bottom > visTop && top < visBottom;
}

//按颜色用途取文字颜色
uint32_t ColorForRole(const MdResolvedStyle& style, int role)
{
    //警告框标题：类型的主色
    if (role >= md_color_alert_first && role < md_color_alert_first + kMdAlertTypeCount)
        return MdAlertColor(role - md_color_alert_first + 1);

    switch (role)
    {
    //次要文字：引用块、有序列表编号
    case md_color_muted:
        return style.m_mutedColor;

    //链接
    case md_color_link:
        return style.m_linkColor;

    //正文
    default:
        return style.m_textColor;
    }
}

//画任务框
void PaintTaskBox(Gdiplus::Graphics& g, const RECT& rc, bool done, const MdResolvedStyle& style, int radius, int lineWidth)
{
    const int side = rc.right - rc.left;
    if (done)
    {
        FillRoundRect(g, rc, radius, ToColor(style.m_accentColor, kOpaque));
        const float stroke = static_cast<float>((std::max)(side * kCheckStrokePercent / kPercent, lineWidth));
        Gdiplus::Pen pen(Gdiplus::Color(kOpaque, kWhite, kWhite, kWhite), stroke);
        pen.SetLineJoin(Gdiplus::LineJoinRound);
        pen.SetStartCap(Gdiplus::LineCapRound);
        pen.SetEndCap(Gdiplus::LineCapRound);
        Gdiplus::Point pts[3] =
        {
            Gdiplus::Point(rc.left + side * kCheckLeftX / kPercent, rc.top + side * kCheckLeftY / kPercent),
            Gdiplus::Point(rc.left + side * kCheckBottomX / kPercent, rc.top + side * kCheckBottomY / kPercent),
            Gdiplus::Point(rc.left + side * kCheckRightX / kPercent, rc.top + side * kCheckRightY / kPercent),
        };
        g.DrawLines(&pen, pts, 3);
        return;
    }
    RECT inner = rc;
    inner.right -= lineWidth;
    inner.bottom -= lineWidth;
    DrawRoundRect(g, inner, radius, ToColor(style.m_mutedColor, kOpaque), static_cast<float>(lineWidth));
}

//画「复制」按钮：半透明白底圆角矩形，中间两个错开的圆角方块
void PaintCopyButton(Gdiplus::Graphics& g, const RECT& rc, bool hot, const MdResolvedStyle& style, int radius, int lineWidth)
{
    FillRoundRect(g, rc, radius, Gdiplus::Color(kCopyButtonBackAlpha, kWhite, kWhite, kWhite));
    const uint32_t iconColor = hot ? style.m_accentColor : style.m_mutedColor;
    DrawRoundRect(g, rc, radius, ToColor(hot ? style.m_accentColor : style.m_ruleColor, kOpaque),
                  static_cast<float>(lineWidth));

    const int side = rc.right - rc.left;
    const int icon = side * kCopyIconPercent / kPercent;
    const int offset = icon * kCopyIconOffsetPercent / kPercent;
    const int square = icon - offset;
    const int left = rc.left + (side - icon) / 2;
    const int top = rc.top + (side - icon) / 2;
    const int smallRadius = (std::max)(radius / 2, 1);
    RECT back = { left + offset, top, left + offset + square, top + square };
    RECT front = { left, top + offset, left + square, top + offset + square };
    DrawRoundRect(g, back, smallRadius, ToColor(iconColor, kOpaque), static_cast<float>(lineWidth));
    FillRoundRect(g, front, smallRadius, Gdiplus::Color(kOpaque, kWhite, kWhite, kWhite));
    DrawRoundRect(g, front, smallRadius, ToColor(iconColor, kOpaque), static_cast<float>(lineWidth));
}

//图标内的一点：按边长的百分比给出相对外框左上角的位置
Gdiplus::PointF IconPoint(const Gdiplus::RectF& box, int xPercent, int yPercent)
{
    return Gdiplus::PointF(box.X + box.Width * xPercent / kPercent, box.Y + box.Height * yPercent / kPercent);
}

//以 center 为圆心填充半径 radius 的圆点
void FillDot(Gdiplus::Graphics& g, Gdiplus::Brush& brush, const Gdiplus::PointF& center, float radius)
{
    g.FillEllipse(&brush, center.X - radius, center.Y - radius, radius * 2, radius * 2);
}

//画「!」：竖线加圆点；shiftPercent 为整体下移的距离（边长的百分比）
void PaintExclamation(Gdiplus::Graphics& g, const Gdiplus::RectF& box, Gdiplus::Pen& pen, Gdiplus::Brush& brush,
                      int shiftPercent)
{
    g.DrawLine(&pen, IconPoint(box, kHalfPercent, kMarkTopPercent + shiftPercent),
               IconPoint(box, kHalfPercent, kMarkBottomPercent + shiftPercent));
    FillDot(g, brush, IconPoint(box, kHalfPercent, kMarkDotPercent + shiftPercent),
            box.Width * kDotRadiusPercent / kPercent);
}

/**
 *  画警告框标题前的图标，各类型形状不同：提示为圆圈里的「i」，技巧为灯泡，重要为圆角方框里的「!」，
 *  警告为三角形里的「!」，注意为八边形里的「!」。颜色取类型的主色。
 *  @param rc        图标外框（正方形）
 *  @param type      警告框类型，取值见 enum MdAlertType
 *  @param lineWidth 细线宽度（线宽的下限）
 */
void PaintAlertIcon(Gdiplus::Graphics& g, const RECT& rc, int type, int lineWidth)
{
    const float side = static_cast<float>(rc.right - rc.left);
    const float stroke = (std::max)(side * kAlertStrokePercent / kPercent, static_cast<float>(lineWidth));
    const Gdiplus::Color color = ToColor(MdAlertColor(type), kOpaque);
    Gdiplus::Pen pen(color, stroke);
    pen.SetLineJoin(Gdiplus::LineJoinRound);
    pen.SetStartCap(Gdiplus::LineCapRound);
    pen.SetEndCap(Gdiplus::LineCapRound);
    Gdiplus::SolidBrush brush(color);

    //描边画在外框内侧：外框向内收半个线宽
    const Gdiplus::RectF box(static_cast<float>(rc.left) + stroke / 2, static_cast<float>(rc.top) + stroke / 2,
                             side - stroke, side - stroke);
    switch (type)
    {
    //提示：圆圈里的「i」
    case md_alert_note:
        g.DrawEllipse(&pen, box);
        FillDot(g, brush, IconPoint(box, kHalfPercent, kInfoDotPercent), box.Width * kDotRadiusPercent / kPercent);
        g.DrawLine(&pen, IconPoint(box, kHalfPercent, kInfoTopPercent), IconPoint(box, kHalfPercent, kInfoBottomPercent));
        break;

    //技巧：灯泡（圆形泡体加两条灯座横线）
    case md_alert_tip:
    {
        const float r = box.Width * kBulbRadiusPercent / kPercent;
        const Gdiplus::PointF c = IconPoint(box, kHalfPercent, kBulbCenterYPercent);
        g.DrawEllipse(&pen, c.X - r, c.Y - r, r * 2, r * 2);
        g.DrawLine(&pen, IconPoint(box, kHalfPercent - kBulbBaseHalfPercent, kBulbBase1Percent),
                   IconPoint(box, kHalfPercent + kBulbBaseHalfPercent, kBulbBase1Percent));
        g.DrawLine(&pen, IconPoint(box, kHalfPercent - kBulbBaseHalfPercent, kBulbBase2Percent),
                   IconPoint(box, kHalfPercent + kBulbBaseHalfPercent, kBulbBase2Percent));
        break;
    }

    //重要：圆角方框里的「!」
    case md_alert_important:
    {
        Gdiplus::GraphicsPath path;
        const float d = box.Width * kBoxRadiusPercent / kPercent * 2;
        const float kQuarter = 90.0f;
        const float kRight = 0.0f;
        const float kBottom = 90.0f;
        const float kLeft = 180.0f;
        const float kTop = 270.0f;
        path.AddArc(box.X, box.Y, d, d, kLeft, kQuarter);
        path.AddArc(box.GetRight() - d, box.Y, d, d, kTop, kQuarter);
        path.AddArc(box.GetRight() - d, box.GetBottom() - d, d, d, kRight, kQuarter);
        path.AddArc(box.X, box.GetBottom() - d, d, d, kBottom, kQuarter);
        path.CloseFigure();
        g.DrawPath(&pen, &path);
        PaintExclamation(g, box, pen, brush, 0);
        break;
    }

    //警告：三角形里的「!」
    case md_alert_warning:
    {
        const Gdiplus::PointF pts[kTrianglePoints] =
        {
            IconPoint(box, kHalfPercent, 0),
            IconPoint(box, kPercent, kPercent),
            IconPoint(box, 0, kPercent),
        };
        g.DrawPolygon(&pen, pts, kTrianglePoints);
        PaintExclamation(g, box, pen, brush, kTriangleMarkShiftPercent);
        break;
    }

    //注意：八边形里的「!」
    case md_alert_caution:
    {
        const int c = kOctagonCutPercent;
        const Gdiplus::PointF pts[kOctagonPoints] =
        {
            IconPoint(box, c, 0),
            IconPoint(box, kPercent - c, 0),
            IconPoint(box, kPercent, c),
            IconPoint(box, kPercent, kPercent - c),
            IconPoint(box, kPercent - c, kPercent),
            IconPoint(box, c, kPercent),
            IconPoint(box, 0, kPercent - c),
            IconPoint(box, 0, c),
        };
        g.DrawPolygon(&pen, pts, kOctagonPoints);
        PaintExclamation(g, box, pen, brush, 0);
        break;
    }

    //未知类型：不画
    default:
        break;
    }
}

//片段里 [from, to)（渲染后文字下标）对应的横向范围，相对片段左边
void FragmentSpan(const MdFragment& frag, int from, int to, int& x0, int& x1)
{
    const int begin = frag.m_flatBegin;
    const int count = static_cast<int>(frag.m_caret.size());
    const int a = (std::max)(from - begin, 0);
    const int b = (std::min)(to - begin, count);
    x0 = (a <= 0) ? 0 : frag.m_caret[a - 1];
    x1 = (b >= count) ? frag.m_width : frag.m_caret[b - 1];
}

} // namespace

bool EnsureMdGdiplus()
{
    ::InitOnceExecuteOnce(&g_gdiplusOnce, StartGdiplusOnce, NULL, NULL);
    return g_gdiplusOk;
}

void PaintMdLayout(const MdLayoutResult& layout, const MdResolvedStyle& style, HDC hdc, int x, int y,
                   const MdPaintParams& params)
{
    const bool gdiplusOk = EnsureMdGdiplus();

    //可见的纵向区间（文档坐标）
    int visTop = 0;
    int visBottom = layout.m_height;
    if (params.m_hasClip)
    {
        visTop = params.m_clip.top - y;
        visBottom = params.m_clip.bottom - y;
    }

    const int saved = ::SaveDC(hdc);
    {
        Gdiplus::Graphics* g = NULL;
        if (gdiplusOk)
        {
            g = new Gdiplus::Graphics(hdc);
            g->SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
            g->SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);
        }

        //1. 块底色：警告框底色、代码块圆角底色、表头底色（警告框底色排在框内装饰之前，先画）
        for (size_t i = 0; i < layout.m_decorations.size(); ++i)
        {
            const MdDecoration& d = layout.m_decorations[i];
            if (!VisibleY(d.m_rect.top, d.m_rect.bottom, visTop, visBottom))
                continue;
            const RECT rc = Offset(d.m_rect, x, y);
            if (d.m_kind == md_deco_alert_back)
            {
                FillSolid(hdc, rc, MdAlertBackColor(d.m_alertType));
            }
            else if (d.m_kind == md_deco_code_block)
            {
                if (g != NULL)
                    FillRoundRect(*g, rc, layout.m_cornerRadius, ToColor(style.m_codeBackColor, kOpaque));
                else
                    FillSolid(hdc, rc, style.m_codeBackColor);
            }
            else if (d.m_kind == md_deco_table_header)
            {
                FillSolid(hdc, rc, style.m_tableHeaderBackColor);
            }
        }

        //2. 行内代码底色
        for (size_t i = 0; i < layout.m_fragments.size(); ++i)
        {
            const MdFragment& f = layout.m_fragments[i];
            if ((f.m_style & md_style_code) == 0 || f.m_inCodeBlock)
                continue;
            if (!VisibleY(f.m_top, f.m_top + f.m_height, visTop, visBottom))
                continue;
            const MdFontMetrics m = MdFontCache::Instance().Metrics(f.m_fontIndex);
            RECT rc = { f.m_x, f.m_baseline - m.m_ascent - kInlineCodeVPad, f.m_x + f.m_width,
                        f.m_baseline + m.m_descent + kInlineCodeVPad };
            rc = Offset(rc, x, y);
            if (g != NULL)
                FillRoundRect(*g, rc, layout.m_cornerRadius, ToColor(style.m_codeBackColor, kOpaque));
            else
                FillSolid(hdc, rc, style.m_codeBackColor);
        }

        //GDI+ 画完的形状先提交，后面的选区用 GDI 画，次序不能颠倒
        if (g != NULL)
            g->Flush(Gdiplus::FlushIntentionSync);

        //3. 选区
        if (params.m_selEnd > params.m_selBegin)
        {
            for (size_t i = 0; i < layout.m_fragments.size(); ++i)
            {
                const MdFragment& f = layout.m_fragments[i];
                if (f.m_flatBegin < 0 || f.FlatEnd() <= params.m_selBegin || f.m_flatBegin >= params.m_selEnd)
                    continue;
                if (!VisibleY(f.m_top, f.m_top + f.m_height, visTop, visBottom))
                    continue;
                int x0 = 0;
                int x1 = 0;
                FragmentSpan(f, params.m_selBegin, params.m_selEnd, x0, x1);
                RECT rc = { f.m_x + x0, f.m_top, f.m_x + x1, f.m_top + f.m_height };
                FillSolid(hdc, Offset(rc, x, y), style.m_selectionBackColor);
            }
        }

        //4. 线条、项目符号、任务框
        for (size_t i = 0; i < layout.m_decorations.size(); ++i)
        {
            const MdDecoration& d = layout.m_decorations[i];
            if (!VisibleY(d.m_rect.top, d.m_rect.bottom, visTop, visBottom))
                continue;
            const RECT rc = Offset(d.m_rect, x, y);
            const int w = rc.right - rc.left;
            const int h = rc.bottom - rc.top;
            switch (d.m_kind)
            {
            //细线与竖条
            case md_deco_line:
                FillSolid(hdc, rc, style.m_ruleColor);
                break;

            //实心圆点
            case md_deco_bullet_disc:
                if (g != NULL)
                {
                    Gdiplus::SolidBrush brush(ToColor(style.m_textColor, kOpaque));
                    g->FillEllipse(&brush, rc.left, rc.top, w, h);
                }
                break;

            //空心圆圈
            case md_deco_bullet_circle:
                if (g != NULL)
                {
                    const float pw = static_cast<float>(layout.m_lineWidth);
                    Gdiplus::Pen pen(ToColor(style.m_textColor, kOpaque), pw);
                    g->DrawEllipse(&pen, static_cast<float>(rc.left) + pw / 2, static_cast<float>(rc.top) + pw / 2,
                                   static_cast<float>(w) - pw, static_cast<float>(h) - pw);
                }
                break;

            //实心方块
            case md_deco_bullet_square:
                FillSolid(hdc, rc, style.m_textColor);
                break;

            //任务框（未勾选 / 已勾选）
            case md_deco_task_open:
            case md_deco_task_done:
                if (g != NULL)
                    PaintTaskBox(*g, rc, d.m_kind == md_deco_task_done, style, layout.m_cornerRadius / 2 + 1,
                                 layout.m_lineWidth);
                break;

            //警告框左侧竖条：类型的主色
            case md_deco_alert_bar:
                FillSolid(hdc, rc, MdAlertColor(d.m_alertType));
                break;

            //警告框标题前的图标
            case md_deco_alert_icon:
                if (g != NULL)
                    PaintAlertIcon(*g, rc, d.m_alertType, layout.m_lineWidth);
                break;

            //底色已在第 1 步画过
            default:
                break;
            }
        }

        //GDI+ 画的圆点、任务框先提交，再用 GDI 画文字
        if (g != NULL)
            g->Flush(Gdiplus::FlushIntentionSync);

        //5. 文字：按基线对齐
        ::SetBkMode(hdc, TRANSPARENT);
        ::SetTextAlign(hdc, TA_BASELINE | TA_LEFT | TA_NOUPDATECP);
        int currentFont = -1;
        for (size_t i = 0; i < layout.m_fragments.size(); ++i)
        {
            const MdFragment& f = layout.m_fragments[i];
            if (!VisibleY(f.m_top, f.m_top + f.m_height, visTop, visBottom))
                continue;
            if (f.m_fontIndex != currentFont)
            {
                HFONT font = MdFontCache::Instance().Font(f.m_fontIndex);
                if (font != NULL)
                    ::SelectObject(hdc, font);
                currentFont = f.m_fontIndex;
            }
            //代码块里着了色的片段按着色种类取色，其余按颜色用途取色
            const bool tokenColored = f.m_inCodeBlock && f.m_tokenKind != kMdCodeTokenPlain;
            ::SetTextColor(hdc, tokenColored ? MdCodeTokenColor(f.m_tokenKind) : ColorForRole(style, f.m_colorRole));
            ::ExtTextOutW(hdc, x + f.m_x + f.m_padLeft, y + f.m_baseline, 0, NULL, f.m_text.c_str(),
                          static_cast<UINT>(f.m_text.size()), NULL);
        }

        //6. 鼠标所在代码块的「复制」按钮
        const int hoverCode = params.m_hoverCodeBlock;
        if (g != NULL && hoverCode >= 0 && static_cast<size_t>(hoverCode) < layout.m_copyButtons.size())
        {
            const RECT btn = layout.m_copyButtons[hoverCode];
            if (btn.right > btn.left && VisibleY(btn.top, btn.bottom, visTop, visBottom))
                PaintCopyButton(*g, Offset(btn, x, y), params.m_hoverCopyButton == hoverCode, style,
                                layout.m_cornerRadius, layout.m_lineWidth);
        }

        delete g;
    }
    ::RestoreDC(hdc, saved);
}

} // namespace flamingomd
