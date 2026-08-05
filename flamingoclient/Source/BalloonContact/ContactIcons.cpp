/**
 *  ContactIcons.cpp
 *
 *  联系人资料对话框线性图标的 GDI+ 绘制实现。每个图标的几何取自 design
 *  的 icons.jsx（viewBox 24）。绘制时先 translate + scale 把 viewBox 映射
 *  到目标区域，再描边。
 *
 *  本 .cpp 不 include Flamingo 的 stdafx.h —— 它要被 Flamingo.vcxproj 与
 *  FlamingoUnit.vcxproj 共用。在 Flamingo.vcxproj 里需标记
 *  PrecompiledHeader=NotUsing。
 *
 *  balloonwj@qq.com   2026-05-22
 */

#include <windows.h>
#include <gdiplus.h>
#pragma comment(lib, "gdiplus.lib")

#include "ContactIcons.h"

namespace ballooncontact {

namespace {

// icons.jsx 的统一画布尺寸 —— 所有 path 坐标都基于 24×24 viewBox。
const Gdiplus::REAL kViewBox = 24.0f;

// 配好圆头圆角的描边 Pen —— 与 ChatIcons 的 MakeStrokePen 一致。
void MakeStrokePen(Gdiplus::Pen& pen)
{
    pen.SetStartCap(Gdiplus::LineCapRound);
    pen.SetEndCap(Gdiplus::LineCapRound);
    pen.SetLineJoin(Gdiplus::LineJoinRound);
}

} // anonymous namespace

void DrawContactIcon(Gdiplus::Graphics& g, int kind,
                     Gdiplus::REAL x, Gdiplus::REAL y, Gdiplus::REAL size,
                     const Gdiplus::Color& color, Gdiplus::REAL strokeWidth)
{
    if (size <= 0.0f)
    {
        return;
    }

    // 把 viewBox 24 映射到 [x, y, size] —— 后续坐标全部用 icons.jsx 原值。
    Gdiplus::GraphicsState st = g.Save();
    g.TranslateTransform(x, y);
    g.ScaleTransform(size / kViewBox, size / kViewBox);

    Gdiplus::Pen pen(color, strokeWidth);
    MakeStrokePen(pen);

    switch (kind)
    {
    case kContactIconEdit:
        // 铅笔 —— icons.jsx edit：
        //   主体笔身 <path d="M4 20h4l11-11-4-4L4 16z"/>（闭合五边形）；
        //   笔尖横杠 <path d="M14 6l4 4"/>（笔头与笔杆的分隔线）。
        {
            // 笔身：闭合多边形。
            Gdiplus::GraphicsPath body;
            Gdiplus::PointF pts[] =
            {
                Gdiplus::PointF(4.0f,  20.0f),
                Gdiplus::PointF(8.0f,  20.0f),
                Gdiplus::PointF(19.0f, 9.0f),
                Gdiplus::PointF(15.0f, 5.0f),
                Gdiplus::PointF(4.0f,  16.0f),
            };
            body.AddPolygon(pts, 5);
            g.DrawPath(&pen, &body);

            // 笔尖横杠。
            g.DrawLine(&pen, 14.0f, 6.0f, 18.0f, 10.0f);
        }
        break;

    case kContactIconChat:
        // 聊天气泡 —— icons.jsx chat：<path d="M4 5h16v11H8l-4 4z"/>。
        // 一个矩形主体，左下角带一条朝下的小尾巴，整条闭合折线。
        {
            Gdiplus::GraphicsPath bubble;
            Gdiplus::PointF pts[] =
            {
                Gdiplus::PointF(4.0f,  5.0f),    // 左上
                Gdiplus::PointF(20.0f, 5.0f),    // 右上
                Gdiplus::PointF(20.0f, 16.0f),   // 右下
                Gdiplus::PointF(8.0f,  16.0f),   // 底边尾巴起点
                Gdiplus::PointF(4.0f,  20.0f),   // 尾巴尖
            };
            bubble.AddPolygon(pts, 5);
            g.DrawPath(&pen, &bubble);
        }
        break;

    default:
        // 越界图标类型不绘制。
        break;
    }

    g.Restore(st);
}

} // namespace ballooncontact
