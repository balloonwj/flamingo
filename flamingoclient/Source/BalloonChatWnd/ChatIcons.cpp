/**
 *  ChatIcons.cpp
 *
 *  单聊窗线性图标的 GDI+ 绘制实现。每个图标的几何取自 design 的
 *  icons.jsx（viewBox 24）。绘制时先 translate + scale 把 viewBox 映射到
 *  目标区域，再描边 / 填充。
 *
 *  本 .cpp 不 include Flamingo 的 stdafx.h —— 它要被 Flamingo.vcxproj 与
 *  FlamingoUnit.vcxproj 共用。在 Flamingo.vcxproj 里需标记
 *  PrecompiledHeader=NotUsing。
 *
 *  balloonwj@qq.com   2026-05-21
 */

#include <windows.h>
#include <math.h>
#include <gdiplus.h>
#pragma comment(lib, "gdiplus.lib")

#include "ChatIcons.h"

namespace balloonchatwnd {

namespace {

const Gdiplus::REAL kViewBox = 24.0f;

// 配好圆头圆角的描边 Pen。
void MakeStrokePen(Gdiplus::Pen& pen)
{
    pen.SetStartCap(Gdiplus::LineCapRound);
    pen.SetEndCap(Gdiplus::LineCapRound);
    pen.SetLineJoin(Gdiplus::LineJoinRound);
}

} // anonymous namespace

void DrawChatIcon(Gdiplus::Graphics& g, int kind,
                  Gdiplus::REAL x, Gdiplus::REAL y, Gdiplus::REAL size,
                  const Gdiplus::Color& color, Gdiplus::REAL strokeWidth)
{
    if (size <= 0.0f)
    {
        return;
    }

    Gdiplus::GraphicsState st = g.Save();
    g.TranslateTransform(x, y);
    g.ScaleTransform(size / kViewBox, size / kViewBox);

    Gdiplus::Pen pen(color, strokeWidth);
    MakeStrokePen(pen);
    Gdiplus::SolidBrush brush(color);

    switch (kind)
    {
    case kChatIconPhone:
        // 电话听筒 —— 弯曲手柄（带圆头的粗描边圆弧）+ 两端实心小圆
        // （听筒 / 话筒），同色叠加成一个无缝的电话听筒剪影。对照设计稿
        // 085135.png 头部的语音按钮造型。
        {
            // 手柄：圆心 (kArcCx, kArcCy)、半径 kArcRadius 的一段圆弧，
            // 用带圆头的较粗笔描边即得一条弯曲的实心手柄。
            const Gdiplus::REAL kArcCx     = 12.0f;   // 手柄圆弧圆心 x
            const Gdiplus::REAL kArcCy     = 12.0f;   // 手柄圆弧圆心 y
            const Gdiplus::REAL kArcRadius = 7.0f;    // 手柄圆弧半径
            const Gdiplus::REAL kArcStart  = 130.0f;  // 手柄圆弧起始角（度）
            const Gdiplus::REAL kArcSweep  = 150.0f;  // 手柄圆弧扫过角（度）
            const Gdiplus::REAL kHandleW   = strokeWidth + 1.2f;  // 手柄粗细

            Gdiplus::Pen handle(color, kHandleW);
            handle.SetStartCap(Gdiplus::LineCapRound);
            handle.SetEndCap(Gdiplus::LineCapRound);
            g.DrawArc(&handle,
                      kArcCx - kArcRadius, kArcCy - kArcRadius,
                      kArcRadius * 2.0f, kArcRadius * 2.0f,
                      kArcStart, kArcSweep);

            // 两端听筒 / 话筒：以圆弧两端点为心的实心小圆，半径略大于
            // 手柄半粗 → 鼓出"耳 / 嘴"造型，让剪影一眼识别为电话。
            const double        kDeg2Rad   = 3.14159265358979 / 180.0;
            const Gdiplus::REAL kEarRadius = kHandleW * 0.95f;   // 听筒圆半径
            Gdiplus::SolidBrush earBrush(color);
            for (int end = 0; end < 2; ++end)
            {
                const double angDeg =
                    (end == 0) ? kArcStart : (kArcStart + kArcSweep);
                const double ang = angDeg * kDeg2Rad;
                const Gdiplus::REAL ex =
                    kArcCx + kArcRadius * (Gdiplus::REAL)cos(ang);
                const Gdiplus::REAL ey =
                    kArcCy + kArcRadius * (Gdiplus::REAL)sin(ang);
                g.FillEllipse(&earBrush, ex - kEarRadius, ey - kEarRadius,
                              kEarRadius * 2.0f, kEarRadius * 2.0f);
            }
        }
        break;

    case kChatIconVideo:
        // 摄像机：机身圆角矩形 + 右侧镜头梯形。
        {
            Gdiplus::GraphicsPath body;
            body.AddArc(3.0f, 7.0f, 4.0f, 4.0f, 180.0f, 90.0f);
            body.AddArc(12.0f, 7.0f, 4.0f, 4.0f, 270.0f, 90.0f);
            body.AddArc(12.0f, 13.0f, 4.0f, 4.0f, 0.0f, 90.0f);
            body.AddArc(3.0f, 13.0f, 4.0f, 4.0f, 90.0f, 90.0f);
            body.CloseFigure();
            g.DrawPath(&pen, &body);
            Gdiplus::GraphicsPath lens;
            lens.AddLine(16.0f, 11.0f, 21.0f, 8.0f);
            lens.AddLine(21.0f, 8.0f, 21.0f, 16.0f);
            lens.AddLine(21.0f, 16.0f, 16.0f, 13.0f);
            g.DrawPath(&pen, &lens);
        }
        break;

    case kChatIconHistory:
        // 时钟：圆 + 指针。
        g.DrawEllipse(&pen, 4.0f, 4.0f, 16.0f, 16.0f);
        {
            Gdiplus::GraphicsPath hand;
            hand.AddLine(12.0f, 8.0f, 12.0f, 12.0f);
            hand.AddLine(12.0f, 12.0f, 15.0f, 14.0f);
            g.DrawPath(&pen, &hand);
        }
        break;

    case kChatIconRemoteDesktop:
        // 显示器：屏幕圆角矩形 + 底部支架（竖杆 + 底座横线）。
        {
            Gdiplus::GraphicsPath screen;
            screen.AddArc(3.0f, 4.0f, 4.0f, 4.0f, 180.0f, 90.0f);
            screen.AddArc(17.0f, 4.0f, 4.0f, 4.0f, 270.0f, 90.0f);
            screen.AddArc(17.0f, 11.0f, 4.0f, 4.0f, 0.0f, 90.0f);
            screen.AddArc(3.0f, 11.0f, 4.0f, 4.0f, 90.0f, 90.0f);
            screen.CloseFigure();
            g.DrawPath(&pen, &screen);
            g.DrawLine(&pen, 12.0f, 15.0f, 12.0f, 19.0f);
            g.DrawLine(&pen, 8.0f, 19.0f, 16.0f, 19.0f);
        }
        break;

    case kChatIconPin:
        // 图钉：上部圆头 + 下方针尖（水滴形），头部中心挖一个小圆孔。
        // 圆心取 (12,10)、半径 7；圆弧自 140° 顺时针扫 260°，留出的缺口朝下，
        // 再由弧的终点连到针尖 (12,21)，CloseFigure 把针尖接回弧的起点。
        {
            Gdiplus::GraphicsPath pin;
            pin.AddArc(5.0f, 3.0f, 14.0f, 14.0f, 140.0f, 260.0f);
            pin.AddLine(17.4f, 14.5f, 12.0f, 21.0f);
            pin.CloseFigure();
            g.DrawPath(&pen, &pin);
            g.DrawEllipse(&pen, 9.5f, 7.5f, 5.0f, 5.0f);
        }
        break;

    case kChatIconFolder:
        // 发送文件夹：文件夹外框（带左上页签）+ 夹内一支向上的箭头。
        // 箭头两翼开口取得比常规箭头大（左右各 3.6、下探 2.9），是为了在按钮实际
        // 显示的 18px 下仍能看出是箭头 —— 开口小的话三笔会糊成一个十字，实测确认过。
        {
            const Gdiplus::REAL kFolderTop    = 9.6f;   // 夹体顶边 y（页签右侧那条水平边）
            const Gdiplus::REAL kFolderBottom = 20.2f;  // 夹体底边 y
            const Gdiplus::REAL kTabTop       = 6.8f;   // 左上页签顶边 y（= 夹体顶边上移一个页签高度）
            const Gdiplus::REAL kArrowTail    = 19.0f;  // 箭杆下端 y
            const Gdiplus::REAL kArrowTip     = 13.4f;  // 箭尖 y
            const Gdiplus::REAL kArrowWingDx  = 3.6f;   // 箭头两翼相对箭尖的水平展开量
            const Gdiplus::REAL kArrowWingDy  = 2.9f;   // 箭头两翼相对箭尖的下探量
            const Gdiplus::REAL kArrowCx      = 12.0f;  // 箭头中轴 x（夹体水平居中）

            Gdiplus::GraphicsPath folder;
            folder.AddLine(3.0f, kFolderBottom, 3.0f, kTabTop);
            folder.AddLine(3.0f, kTabTop, 9.0f, kTabTop);
            folder.AddLine(9.0f, kTabTop, 11.3f, kFolderTop);
            folder.AddLine(11.3f, kFolderTop, 21.0f, kFolderTop);
            folder.AddLine(21.0f, kFolderTop, 21.0f, kFolderBottom);
            folder.CloseFigure();
            g.DrawPath(&pen, &folder);

            g.DrawLine(&pen, kArrowCx, kArrowTail, kArrowCx, kArrowTip);

            Gdiplus::GraphicsPath head;
            head.AddLine(kArrowCx - kArrowWingDx, kArrowTip + kArrowWingDy, kArrowCx, kArrowTip);
            head.AddLine(kArrowCx, kArrowTip, kArrowCx + kArrowWingDx, kArrowTip + kArrowWingDy);
            g.DrawPath(&pen, &head);
        }
        break;

    case kChatIconMore:
        // 三个实心圆点。
        g.FillEllipse(&brush, 4.8f, 10.8f, 2.4f, 2.4f);
        g.FillEllipse(&brush, 10.8f, 10.8f, 2.4f, 2.4f);
        g.FillEllipse(&brush, 16.8f, 10.8f, 2.4f, 2.4f);
        break;

    case kChatIconEmoji:
        // 笑脸：圆 + 两眼 + 嘴部曲线。
        g.DrawEllipse(&pen, 4.0f, 4.0f, 16.0f, 16.0f);
        g.FillEllipse(&brush, 8.0f, 9.0f, 2.0f, 2.0f);
        g.FillEllipse(&brush, 14.0f, 9.0f, 2.0f, 2.0f);
        {
            Gdiplus::GraphicsPath mouth;
            mouth.AddBezier(8.5f, 14.5f, 9.7f, 16.3f, 14.3f, 16.3f, 15.5f, 14.5f);
            g.DrawPath(&pen, &mouth);
        }
        break;

    case kChatIconImage:
        // 图片：圆角矩形 + 小圆 + 山形折线。
        {
            Gdiplus::GraphicsPath frame;
            frame.AddArc(4.0f, 5.0f, 4.0f, 4.0f, 180.0f, 90.0f);
            frame.AddArc(16.0f, 5.0f, 4.0f, 4.0f, 270.0f, 90.0f);
            frame.AddArc(16.0f, 15.0f, 4.0f, 4.0f, 0.0f, 90.0f);
            frame.AddArc(4.0f, 15.0f, 4.0f, 4.0f, 90.0f, 90.0f);
            frame.CloseFigure();
            g.DrawPath(&pen, &frame);
            g.DrawEllipse(&pen, 7.6f, 8.6f, 2.8f, 2.8f);
            Gdiplus::GraphicsPath hill;
            hill.AddLine(5.0f, 17.0f, 9.0f, 13.0f);
            hill.AddLine(9.0f, 13.0f, 12.0f, 16.0f);
            hill.AddLine(12.0f, 16.0f, 15.0f, 13.0f);
            hill.AddLine(15.0f, 13.0f, 19.0f, 17.0f);
            g.DrawPath(&pen, &hill);
        }
        break;

    case kChatIconFile:
        // 文件：带折角的纸张。
        {
            Gdiplus::GraphicsPath sheet;
            sheet.AddLine(7.0f, 3.0f, 14.0f, 3.0f);
            sheet.AddLine(14.0f, 3.0f, 19.0f, 8.0f);
            sheet.AddLine(19.0f, 8.0f, 19.0f, 21.0f);
            sheet.AddLine(19.0f, 21.0f, 7.0f, 21.0f);
            sheet.CloseFigure();
            g.DrawPath(&pen, &sheet);
            Gdiplus::GraphicsPath fold;
            fold.AddLine(14.0f, 3.0f, 14.0f, 8.0f);
            fold.AddLine(14.0f, 8.0f, 19.0f, 8.0f);
            g.DrawPath(&pen, &fold);
        }
        break;

    case kChatIconCut:
        // 剪刀：两个圆环 + 两条交叉刀刃。
        g.DrawEllipse(&pen, 4.6f, 4.6f, 4.8f, 4.8f);
        g.DrawEllipse(&pen, 4.6f, 14.6f, 4.8f, 4.8f);
        g.DrawLine(&pen, 9.0f, 9.0f, 21.0f, 19.0f);
        g.DrawLine(&pen, 9.0f, 15.0f, 21.0f, 5.0f);
        break;

    case kChatIconMic:
        // 麦克风：上部圆角矩形 + 下方托弧 + 支杆。
        {
            Gdiplus::GraphicsPath cap;
            cap.AddArc(9.5f, 3.5f, 5.0f, 5.0f, 180.0f, 180.0f);
            cap.AddArc(9.5f, 9.5f, 5.0f, 5.0f, 0.0f, 180.0f);
            cap.CloseFigure();
            g.DrawPath(&pen, &cap);
            g.DrawArc(&pen, 6.0f, 5.0f, 12.0f, 12.0f, 0.0f, 180.0f);
            g.DrawLine(&pen, 12.0f, 17.0f, 12.0f, 20.0f);
        }
        break;

    case kChatIconPlus:
        // 加号。
        g.DrawLine(&pen, 12.0f, 5.0f, 12.0f, 19.0f);
        g.DrawLine(&pen, 5.0f, 12.0f, 19.0f, 12.0f);
        break;

    case kChatIconCheck:
        // 对勾。
        {
            Gdiplus::GraphicsPath chk;
            chk.AddLine(5.0f, 12.5f, 9.5f, 17.0f);
            chk.AddLine(9.5f, 17.0f, 20.0f, 7.0f);
            g.DrawPath(&pen, &chk);
        }
        break;

    case kChatIconSend:
        // 纸飞机。
        {
            Gdiplus::GraphicsPath plane;
            plane.AddLine(4.0f, 12.0f, 20.0f, 4.0f);
            plane.AddLine(20.0f, 4.0f, 17.0f, 20.0f);
            plane.AddLine(17.0f, 20.0f, 12.0f, 14.0f);
            plane.AddLine(12.0f, 14.0f, 4.0f, 12.0f);
            plane.CloseFigure();
            g.DrawPath(&pen, &plane);
        }
        break;

    case kChatIconShake:
        // 窗口抖动：中间一个小窗（圆角矩形 + 顶部标题分隔线），左右各两条
        // 长短不一的竖向运动线，表示窗口正在左右晃动。
        {
            // 小窗：圆角矩形 x 8..16、y 7..17（圆角半径 1.5 → 弧框 d=3）。
            Gdiplus::GraphicsPath win;
            win.AddArc(8.0f,  7.0f,  3.0f, 3.0f, 180.0f, 90.0f);   // 左上角
            win.AddArc(13.0f, 7.0f,  3.0f, 3.0f, 270.0f, 90.0f);   // 右上角
            win.AddArc(13.0f, 14.0f, 3.0f, 3.0f, 0.0f,   90.0f);   // 右下角
            win.AddArc(8.0f,  14.0f, 3.0f, 3.0f, 90.0f,  90.0f);   // 左下角
            win.CloseFigure();
            g.DrawPath(&pen, &win);
            // 标题分隔线。
            g.DrawLine(&pen, 9.0f, 10.0f, 15.0f, 10.0f);

            // 左侧运动线（近窗长、远窗短）。
            g.DrawLine(&pen, 5.6f, 10.0f, 5.6f, 14.0f);
            g.DrawLine(&pen, 3.7f, 11.2f, 3.7f, 12.8f);
            // 右侧运动线（对称）。
            g.DrawLine(&pen, 18.4f, 10.0f, 18.4f, 14.0f);
            g.DrawLine(&pen, 20.3f, 11.2f, 20.3f, 12.8f);
        }
        break;

    case kChatIconSettings:
        // 简化齿轮（viewBox 24）：中心圆 + 外圈 + 8 颗向外伸的短齿。
        g.DrawEllipse(&pen, 9.4f, 9.4f, 5.2f, 5.2f);
        g.DrawEllipse(&pen, 6.0f, 6.0f, 12.0f, 12.0f);
        for (int i = 0; i < 8; ++i)
        {
            const double a = i * 3.14159265 / 4.0;   // 8 颗齿，间隔 45°
            const Gdiplus::REAL cx = (Gdiplus::REAL)cos(a);
            const Gdiplus::REAL cy = (Gdiplus::REAL)sin(a);
            g.DrawLine(&pen, 12.0f + 6.0f * cx, 12.0f + 6.0f * cy,
                       12.0f + 9.0f * cx, 12.0f + 9.0f * cy);
        }
        break;

    case kChatIconFont:
        // 字体设置 —— 一个大写字母「A」（两条斜腿 + 中横），文字 / 字体的通用
        // 形象。对照 design/聊天字体设置1.png toolbar 上的「AF」按钮。
        g.DrawLine(&pen, 5.5f, 19.0f, 12.0f, 5.0f);    // 左腿
        g.DrawLine(&pen, 12.0f, 5.0f, 18.5f, 19.0f);   // 右腿
        g.DrawLine(&pen, 8.2f, 13.0f, 15.8f, 13.0f);   // 中横
        break;

    default:
        break;
    }

    g.Restore(st);
}

} // namespace balloonchatwnd
