/**
 *  ContactIcons.cpp
 *
 *  联系人资料对话框线性图标的 GDI+ 绘制实现。每个图标的几何取自 design
 *  的 icons.jsx（viewBox 24）。绘制时先 translate + scale 把 viewBox 映射
 *  到目标区域，再描边。
 *
 *  电话听筒 / 摄像机 / 时钟三个图标单聊窗已经有同款（ChatIcons.cpp），本文件
 *  只把它们转发过去，不再重画一份。
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
#include "../BalloonChatWnd/ChatIcons.h"   // 电话听筒 / 摄像机 / 时钟三个图标转发给它

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

// MapToChatIcon 的返回值：本图标在 ChatIcons 里没有同款，要由本文件自己画。
// 取 -1 是因为 ChatIconKind 从 0 起连续编号，负值与任何有效图标都区分得开。
const int kNoChatIconCounterpart = -1;

// 把 ContactIconKind 映射到 BalloonChatWnd/ChatIcons.h 里的同款图标。
//   kind：ContactIconKind 取值。
//   返回：对应的 balloonchatwnd::ChatIconKind；没有同款时返回 kNoChatIconCounterpart。
int MapToChatIcon(int kind)
{
    switch (kind)
    {
    //电话听筒：与单聊窗头部「语音通话」按钮同款
    case kContactIconPhone:
        return balloonchatwnd::kChatIconPhone;

    //摄像机：与单聊窗头部「视频通话」按钮同款
    case kContactIconVideo:
        return balloonchatwnd::kChatIconVideo;

    //时钟：与单聊窗头部「消息记录」按钮同款
    case kContactIconHistory:
        return balloonchatwnd::kChatIconHistory;

    //其余图标（铅笔 / 聊天气泡 / 地图定位针）ChatIcons 里没有，由本文件自己画
    default:
        return kNoChatIconCounterpart;
    }
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

    // 与单聊窗同款的那几个图标直接转发过去。转发必须放在下面的坐标变换<u>之前</u>：
    // DrawChatIcon 内部同样要把 viewBox 24 映射到目标区域，先叠一层变换会缩放两次。
    const int chatIconKind = MapToChatIcon(kind);
    if (chatIconKind != kNoChatIconCounterpart)
    {
        balloonchatwnd::DrawChatIcon(g, chatIconKind, x, y, size, color, strokeWidth);
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

    case kContactIconLocate:
        // 地图定位针 —— icons.jsx 里没有同款，按同一套 viewBox 24 描边风格新画：
        //   针头是一段圆弧（圆心 12,10、半径 5），下方留一个约 90 度的开口；
        //   开口两端各引一条直线收到针尖 12,21，与圆弧闭合成水滴形；
        //   针头正中再画一个半径 2 的小圆环，与地图应用的定位针形态一致。
        {
            // 圆弧的外接矩形：左上角 (7,5)、边长 10，即圆心 (12,10)、半径 5。
            const Gdiplus::REAL kHeadLeft = 7.0f;
            const Gdiplus::REAL kHeadTop  = 5.0f;
            const Gdiplus::REAL kHeadSize = 10.0f;
            // GDI+ 的角度从 3 点钟方向起算、顺时针为正。从 135 度扫过 270 度，
            // 于是弧起于左下 (8.5,13.5)，经左、上、右三侧，收于右下 (15.5,13.5)。
            const Gdiplus::REAL kArcStartAngle = 135.0f;
            const Gdiplus::REAL kArcSweepAngle = 270.0f;
            // 圆弧终点（右下开口处）与针尖坐标。
            const Gdiplus::REAL kArcEndX = 15.54f;
            const Gdiplus::REAL kArcEndY = 13.54f;
            const Gdiplus::REAL kTipX    = 12.0f;
            const Gdiplus::REAL kTipY    = 21.0f;
            // 针头正中小圆环的外接矩形：左上角 (10,8)、边长 4，即圆心 (12,10)、半径 2。
            const Gdiplus::REAL kDotLeft = 10.0f;
            const Gdiplus::REAL kDotTop  = 8.0f;
            const Gdiplus::REAL kDotSize = 4.0f;

            Gdiplus::GraphicsPath pin;
            pin.AddArc(kHeadLeft, kHeadTop, kHeadSize, kHeadSize,
                       kArcStartAngle, kArcSweepAngle);
            pin.AddLine(kArcEndX, kArcEndY, kTipX, kTipY);
            // 闭合：由针尖连回圆弧起点（左下开口处），水滴形就此合拢。
            pin.CloseFigure();
            g.DrawPath(&pen, &pin);

            g.DrawEllipse(&pen, kDotLeft, kDotTop, kDotSize, kDotSize);
        }
        break;

    default:
        // 越界图标类型不绘制。
        break;
    }

    g.Restore(st);
}

} // namespace ballooncontact
