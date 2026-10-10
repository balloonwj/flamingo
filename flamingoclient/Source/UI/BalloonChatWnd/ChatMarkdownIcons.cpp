/**
 *  ChatMarkdownIcons.cpp
 *
 *  Markdown 模式开关与格式条图标的 GDI+ 绘制，说明见 ChatMarkdownIcons.h。字母类图标（B、S、数字）用 GraphicsPath
 *  的 AddString 生成轮廓再填充，其余用线段与圆角矩形描边。
 *
 *  本 .cpp 不 include Flamingo 的 stdafx.h —— 与 ChatIcons.cpp 一样被 Flamingo.vcxproj 与 FlamingoUnit.vcxproj 共用，
 *  Flamingo.vcxproj 里标记 PrecompiledHeader=NotUsing。
 *
 *  balloonwj@qq.com   2026-10-06
 */
#include <windows.h>
#include <gdiplus.h>

#include "ChatMarkdownIcons.h"
#include "ChatIcons.h"

namespace balloonchatwnd {

namespace {

//viewBox 的边长
const Gdiplus::REAL kViewBox = 24.0f;

//字母图标的字号（viewBox 单位）与列表编号的字号
const Gdiplus::REAL kLetterEm = 17.0f;
const Gdiplus::REAL kDigitEm = 6.5f;

//下划线图标：字母 U 上移的距离（给下方的横线留出位置），viewBox 单位
const Gdiplus::REAL kUnderlineLetterLift = 1.5f;

//警告框图标：感叹号下面那个圆点的半径，viewBox 单位
const Gdiplus::REAL kAlertDotRadius = 1.3f;

//字母图标优先使用的字体；本机没有时退回系统的无衬线字体
const wchar_t* const kIconFontFamily = L"Segoe UI";

//列表三行的纵坐标，以及横线的左右端
const Gdiplus::REAL kRowY[3] = { 7.0f, 12.0f, 17.0f };
const Gdiplus::REAL kListLineLeft = 9.5f;
const Gdiplus::REAL kListLineRight = 20.5f;

//无序列表圆点的半径与圆心横坐标
const Gdiplus::REAL kBulletRadius = 1.4f;
const Gdiplus::REAL kBulletX = 5.0f;

//链环的旋转角（度）与旋转中心
const Gdiplus::REAL kLinkAngle = -45.0f;
const Gdiplus::REAL kCenter = 12.0f;

//添加圆角矩形路径
void AddRound(Gdiplus::GraphicsPath& path, Gdiplus::REAL x, Gdiplus::REAL y, Gdiplus::REAL w, Gdiplus::REAL h,
              Gdiplus::REAL r)
{
    const Gdiplus::REAL d = r * 2.0f;
    path.AddArc(x, y, d, d, 180.0f, 90.0f);
    path.AddArc(x + w - d, y, d, d, 270.0f, 90.0f);
    path.AddArc(x + w - d, y + h - d, d, d, 0.0f, 90.0f);
    path.AddArc(x, y + h - d, d, d, 90.0f, 90.0f);
    path.CloseFigure();
}

//描边圆角矩形
void StrokeRound(Gdiplus::Graphics& g, Gdiplus::Pen& pen, Gdiplus::REAL x, Gdiplus::REAL y, Gdiplus::REAL w,
                 Gdiplus::REAL h, Gdiplus::REAL r)
{
    Gdiplus::GraphicsPath path;
    AddRound(path, x, y, w, h, r);
    g.DrawPath(&pen, &path);
}

//在矩形内居中填充一段文字的轮廓
void FillText(Gdiplus::Graphics& g, const wchar_t* text, int style, Gdiplus::REAL em, const Gdiplus::RectF& box,
              const Gdiplus::Color& color)
{
    Gdiplus::FontFamily preferred(kIconFontFamily);
    const Gdiplus::FontFamily* family = preferred.IsAvailable() ? &preferred : Gdiplus::FontFamily::GenericSansSerif();
    Gdiplus::StringFormat format;
    format.SetAlignment(Gdiplus::StringAlignmentCenter);
    format.SetLineAlignment(Gdiplus::StringAlignmentCenter);
    Gdiplus::GraphicsPath path;
    path.AddString(text, -1, family, style, em, box, &format);
    Gdiplus::SolidBrush brush(color);
    g.FillPath(&brush, &path);
}

//列表右侧的三行横线
void ListLines(Gdiplus::Graphics& g, Gdiplus::Pen& pen)
{
    for (int i = 0; i < 3; ++i)
        g.DrawLine(&pen, kListLineLeft, kRowY[i], kListLineRight, kRowY[i]);
}

} // namespace

bool DrawMarkdownChatIcon(Gdiplus::Graphics& g, int kind, Gdiplus::Pen& pen, const Gdiplus::Color& color)
{
    const Gdiplus::RectF whole(0.0f, 0.0f, kViewBox, kViewBox);
    switch (kind)
    {
    //Markdown 开关：圆角框 + 「M」+ 向下的箭头
    case kChatIconMarkdown:
    {
        StrokeRound(g, pen, 2.0f, 5.0f, 20.0f, 14.0f, 2.5f);
        Gdiplus::PointF m[5] =
        {
            Gdiplus::PointF(5.5f, 15.0f), Gdiplus::PointF(5.5f, 9.0f), Gdiplus::PointF(8.5f, 12.0f),
            Gdiplus::PointF(11.5f, 9.0f), Gdiplus::PointF(11.5f, 15.0f),
        };
        g.DrawLines(&pen, m, 5);
        g.DrawLine(&pen, 17.0f, 9.0f, 17.0f, 15.0f);
        Gdiplus::PointF head[3] =
        {
            Gdiplus::PointF(14.5f, 12.5f), Gdiplus::PointF(17.0f, 15.0f), Gdiplus::PointF(19.5f, 12.5f),
        };
        g.DrawLines(&pen, head, 3);
        return true;
    }

    //标题：字母 H
    case kChatIconMdHeading:
        g.DrawLine(&pen, 6.5f, 5.0f, 6.5f, 19.0f);
        g.DrawLine(&pen, 17.5f, 5.0f, 17.5f, 19.0f);
        g.DrawLine(&pen, 6.5f, 12.0f, 17.5f, 12.0f);
        return true;

    //加粗：粗体 B
    case kChatIconMdBold:
        FillText(g, L"B", Gdiplus::FontStyleBold, kLetterEm, whole, color);
        return true;

    //斜体：上下两条短横 + 一条斜线
    case kChatIconMdItalic:
        g.DrawLine(&pen, 10.5f, 5.0f, 18.0f, 5.0f);
        g.DrawLine(&pen, 6.0f, 19.0f, 13.5f, 19.0f);
        g.DrawLine(&pen, 14.25f, 5.0f, 9.75f, 19.0f);
        return true;

    //删除线：字母 S + 横线
    case kChatIconMdStrike:
        FillText(g, L"S", Gdiplus::FontStyleRegular, kLetterEm, whole, color);
        g.DrawLine(&pen, 4.0f, 12.0f, 20.0f, 12.0f);
        return true;

    //行内代码：一对尖括号
    case kChatIconMdCode:
    {
        Gdiplus::PointF left[3] =
        {
            Gdiplus::PointF(8.5f, 6.5f), Gdiplus::PointF(3.5f, 12.0f), Gdiplus::PointF(8.5f, 17.5f),
        };
        Gdiplus::PointF right[3] =
        {
            Gdiplus::PointF(15.5f, 6.5f), Gdiplus::PointF(20.5f, 12.0f), Gdiplus::PointF(15.5f, 17.5f),
        };
        g.DrawLines(&pen, left, 3);
        g.DrawLines(&pen, right, 3);
        return true;
    }

    //代码块：圆角框 + 框内小尖括号
    case kChatIconMdCodeBlock:
    {
        StrokeRound(g, pen, 2.5f, 4.0f, 19.0f, 16.0f, 2.5f);
        Gdiplus::PointF left[3] =
        {
            Gdiplus::PointF(10.0f, 9.0f), Gdiplus::PointF(7.0f, 12.0f), Gdiplus::PointF(10.0f, 15.0f),
        };
        Gdiplus::PointF right[3] =
        {
            Gdiplus::PointF(14.0f, 9.0f), Gdiplus::PointF(17.0f, 12.0f), Gdiplus::PointF(14.0f, 15.0f),
        };
        g.DrawLines(&pen, left, 3);
        g.DrawLines(&pen, right, 3);
        return true;
    }

    //链接：两个斜扣的圆角链环
    case kChatIconMdLink:
    {
        Gdiplus::GraphicsState st = g.Save();
        g.TranslateTransform(kCenter, kCenter);
        g.RotateTransform(kLinkAngle);
        g.TranslateTransform(-kCenter, -kCenter);
        StrokeRound(g, pen, 2.5f, 9.0f, 10.5f, 6.0f, 3.0f);
        StrokeRound(g, pen, 11.0f, 9.0f, 10.5f, 6.0f, 3.0f);
        g.Restore(st);
        return true;
    }

    //引用：左侧粗竖条 + 两长一短三行横线
    case kChatIconMdQuote:
    {
        Gdiplus::Pen bar(color, pen.GetWidth() + 1.0f);
        bar.SetStartCap(Gdiplus::LineCapRound);
        bar.SetEndCap(Gdiplus::LineCapRound);
        g.DrawLine(&bar, 5.0f, 6.0f, 5.0f, 18.0f);
        g.DrawLine(&pen, 9.5f, 8.0f, 19.5f, 8.0f);
        g.DrawLine(&pen, 9.5f, 12.0f, 19.5f, 12.0f);
        g.DrawLine(&pen, 9.5f, 16.0f, 16.0f, 16.0f);
        return true;
    }

    //无序列表：三个圆点 + 三行横线
    case kChatIconMdBulletList:
    {
        Gdiplus::SolidBrush brush(color);
        for (int i = 0; i < 3; ++i)
            g.FillEllipse(&brush, kBulletX - kBulletRadius, kRowY[i] - kBulletRadius, kBulletRadius * 2.0f,
                          kBulletRadius * 2.0f);
        ListLines(g, pen);
        return true;
    }

    //有序列表：左侧 1 2 3 + 三行横线
    case kChatIconMdNumberList:
    {
        const wchar_t* const digits[3] = { L"1", L"2", L"3" };
        for (int i = 0; i < 3; ++i)
        {
            const Gdiplus::RectF box(1.5f, kRowY[i] - 4.0f, 6.0f, 8.0f);
            FillText(g, digits[i], Gdiplus::FontStyleBold, kDigitEm, box, color);
        }
        ListLines(g, pen);
        return true;
    }

    //任务列表：带勾的方框 + 空方框，各跟一行横线
    case kChatIconMdTaskList:
    {
        StrokeRound(g, pen, 3.0f, 4.5f, 6.0f, 6.0f, 1.2f);
        Gdiplus::PointF check[3] =
        {
            Gdiplus::PointF(4.4f, 7.6f), Gdiplus::PointF(5.8f, 9.0f), Gdiplus::PointF(8.0f, 6.2f),
        };
        g.DrawLines(&pen, check, 3);
        StrokeRound(g, pen, 3.0f, 13.5f, 6.0f, 6.0f, 1.2f);
        g.DrawLine(&pen, 12.5f, 7.5f, 21.0f, 7.5f);
        g.DrawLine(&pen, 12.5f, 16.5f, 21.0f, 16.5f);
        return true;
    }

    //表格：圆角框 + 一条横线 + 两条竖线
    case kChatIconMdTable:
        StrokeRound(g, pen, 3.0f, 5.0f, 18.0f, 14.0f, 2.0f);
        g.DrawLine(&pen, 3.0f, 10.0f, 21.0f, 10.0f);
        g.DrawLine(&pen, 9.0f, 5.0f, 9.0f, 19.0f);
        g.DrawLine(&pen, 15.0f, 5.0f, 15.0f, 19.0f);
        return true;

    //下划线：字母 U（略微上移）+ 下方一条横线
    case kChatIconMdUnderline:
    {
        const Gdiplus::RectF raised(0.0f, -kUnderlineLetterLift, kViewBox, kViewBox);
        FillText(g, L"U", Gdiplus::FontStyleRegular, kLetterEm, raised, color);
        g.DrawLine(&pen, 5.0f, 20.5f, 19.0f, 20.5f);
        return true;
    }

    //分割线：中间一条长横线，上下各一条短横线（表示上下两段文字）
    case kChatIconMdRule:
        g.DrawLine(&pen, 3.0f, 12.0f, 21.0f, 12.0f);
        g.DrawLine(&pen, 7.0f, 6.0f, 17.0f, 6.0f);
        g.DrawLine(&pen, 7.0f, 18.0f, 17.0f, 18.0f);
        return true;

    //警告框：三角形 + 框内感叹号（竖线与一个实心圆点）
    case kChatIconMdAlert:
    {
        const Gdiplus::PointF triangle[3] =
        {
            Gdiplus::PointF(12.0f, 3.5f), Gdiplus::PointF(21.0f, 19.5f), Gdiplus::PointF(3.0f, 19.5f),
        };
        g.DrawPolygon(&pen, triangle, 3);
        g.DrawLine(&pen, 12.0f, 9.0f, 12.0f, 13.5f);
        Gdiplus::SolidBrush dot(color);
        g.FillEllipse(&dot, 12.0f - kAlertDotRadius, 16.5f - kAlertDotRadius, kAlertDotRadius * 2.0f,
                      kAlertDotRadius * 2.0f);
        return true;
    }

    //其它种类不归本函数绘制
    default:
        return false;
    }
}

} // namespace balloonchatwnd
