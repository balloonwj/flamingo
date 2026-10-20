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
#include "ChatMarkdownIcons.h"   // DrawMarkdownChatIcon：Markdown 开关与格式条的图标

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

// 描边一个圆角矩形（viewBox 坐标）。
//   left / top / right / bottom：矩形四边；radius：圆角半径，须不大于宽、高的一半。
void DrawRoundBox(Gdiplus::Graphics& g, Gdiplus::Pen& pen, Gdiplus::REAL left, Gdiplus::REAL top,
                  Gdiplus::REAL right, Gdiplus::REAL bottom, Gdiplus::REAL radius)
{
    const Gdiplus::REAL d = radius * 2.0f;
    Gdiplus::GraphicsPath path;
    path.AddArc(left, top, d, d, 180.0f, 90.0f);
    path.AddArc(right - d, top, d, d, 270.0f, 90.0f);
    path.AddArc(right - d, bottom - d, d, d, 0.0f, 90.0f);
    path.AddArc(left, bottom - d, d, d, 90.0f, 90.0f);
    path.CloseFigure();
    g.DrawPath(&pen, &path);
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
        // 图钉（按钉）：钉帽、收窄的钉身、向两侧张开的底盘，底盘下伸出一根钉针；整体斜放
        // 45°，钉帽朝右上、钉针指向左下，与 📌 字符是同一造型。
        // 不能画成「水滴形 + 中心小圆环」：那是地图定位针的造型，旧版本就是这样画的，
        // 显示在消息旁边会被理解为「位置」，而不是「已 Pin」。
        // 钉帽到底盘这一段填实，钉针单独描一笔。不只描边的原因是该图标实际显示尺寸只有
        // 14~16px，描边宽度折算下来不足 1px，只描边时在该尺寸下实测线条发虚、看不出形状。
        {
            // 以下坐标按「钉帽朝上、钉针朝下」的竖直姿态给出，旋转在后面统一做。
            const Gdiplus::REAL kPinAxisX   = 12.0f;   // 钉身中轴 x
            const Gdiplus::REAL kCapHalfW   = 4.5f;    // 钉帽半宽
            const Gdiplus::REAL kCapTop     = 4.0f;    // 钉帽顶边 y
            const Gdiplus::REAL kCapBottom  = 7.5f;    // 钉帽底边 y，也是钉身顶端
            const Gdiplus::REAL kNeckHalfW  = 2.2f;    // 钉身半宽
            const Gdiplus::REAL kNeckBottom = 11.5f;   // 钉身底端 y，自此向两侧张开成底盘
            const Gdiplus::REAL kBaseHalfW  = 6.5f;    // 底盘半宽
            const Gdiplus::REAL kBaseTop    = 15.0f;   // 底盘两侧竖边的上端 y
            const Gdiplus::REAL kBaseBottom = 17.5f;   // 底盘底边 y，也是钉针根部
            const Gdiplus::REAL kNeedleTip  = 23.0f;   // 钉针尖 y
            // 绕 viewBox 中心顺时针旋转 45°。上面的竖直坐标已按旋转后的外接框在 viewBox
            // 内大致居中来取值（旋转后约占 x、y 各 3.5~20.8），旋转之后不需要再平移。
            const Gdiplus::REAL kTiltCenter  = kViewBox / 2.0f;   // 旋转中心（x、y 相同）
            const Gdiplus::REAL kTiltDegrees = 45.0f;             // 顺时针旋转角（度）

            // GDI+ 的变换默认追加在已有变换之前，所以三步的实际作用顺序是：先把旋转
            // 中心移到原点、再旋转、再移回原处，最后才是函数开头的缩放与平移。
            g.TranslateTransform(kTiltCenter, kTiltCenter);
            g.RotateTransform(kTiltDegrees);
            g.TranslateTransform(-kTiltCenter, -kTiltCenter);

            // 钉帽 + 钉身 + 底盘的外轮廓，自钉帽左上角起顺时针一圈。
            const Gdiplus::PointF outline[] =
            {
                Gdiplus::PointF(kPinAxisX - kCapHalfW,  kCapTop),       // 钉帽左上
                Gdiplus::PointF(kPinAxisX + kCapHalfW,  kCapTop),       // 钉帽右上
                Gdiplus::PointF(kPinAxisX + kCapHalfW,  kCapBottom),    // 钉帽右下
                Gdiplus::PointF(kPinAxisX + kNeckHalfW, kCapBottom),    // 钉身右上
                Gdiplus::PointF(kPinAxisX + kNeckHalfW, kNeckBottom),   // 钉身右下
                Gdiplus::PointF(kPinAxisX + kBaseHalfW, kBaseTop),      // 底盘右上
                Gdiplus::PointF(kPinAxisX + kBaseHalfW, kBaseBottom),   // 底盘右下
                Gdiplus::PointF(kPinAxisX - kBaseHalfW, kBaseBottom),   // 底盘左下
                Gdiplus::PointF(kPinAxisX - kBaseHalfW, kBaseTop),      // 底盘左上
                Gdiplus::PointF(kPinAxisX - kNeckHalfW, kNeckBottom),   // 钉身左下
                Gdiplus::PointF(kPinAxisX - kNeckHalfW, kCapBottom),    // 钉身左上
                Gdiplus::PointF(kPinAxisX - kCapHalfW,  kCapBottom),    // 钉帽左下
            };
            const INT kOutlinePointCount = (INT)(sizeof(outline) / sizeof(outline[0]));

            Gdiplus::GraphicsPath body;
            body.AddPolygon(outline, kOutlinePointCount);
            g.FillPath(&brush, &body);
            // 填实之后再沿轮廓描一圈：圆角连接让钉帽、底盘的尖角略微变圆，
            // 与本组其它图标的圆头圆角风格一致。
            g.DrawPath(&pen, &body);

            // 钉针比其它笔画粗一些：按调用方给的描边宽度画，在 14px 下不足 1px，
            // 实测只剩一道很淡的虚影，整枚图钉看上去就成了一个色块。
            const Gdiplus::REAL kNeedleWidthScale = 1.5f;   // 钉针线宽相对描边宽度的倍数
            Gdiplus::Pen needlePen(color, strokeWidth * kNeedleWidthScale);
            MakeStrokePen(needlePen);
            g.DrawLine(&needlePen, kPinAxisX, kBaseBottom, kPinAxisX, kNeedleTip);
        }
        break;

    case kChatIconClear:
        // 扫帚：竖直手柄 + 一道扎绳 + 下宽上窄的梯形刷头，刷头里三条刷毛线向下散开。
        {
            g.DrawLine(&pen, 12.0f, 3.0f, 12.0f, 11.0f);   // 手柄
            g.DrawLine(&pen, 8.5f, 11.0f, 15.5f, 11.0f);   // 扎绳
            Gdiplus::GraphicsPath head;
            head.AddLine(8.5f, 11.0f, 15.5f, 11.0f);
            head.AddLine(15.5f, 11.0f, 18.0f, 20.0f);
            head.AddLine(18.0f, 20.0f, 6.0f, 20.0f);
            head.CloseFigure();
            g.DrawPath(&pen, &head);
            g.DrawLine(&pen, 10.0f, 14.0f, 9.2f, 20.0f);    // 左刷毛
            g.DrawLine(&pen, 12.0f, 14.0f, 12.0f, 20.0f);   // 中刷毛
            g.DrawLine(&pen, 14.0f, 14.0f, 14.8f, 20.0f);   // 右刷毛
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

    case kChatIconRelay:
        // 群接龙：写字板外框 + 顶部夹子，板内三行「实心圆点 + 横线」表示一条条接龙。
        {
            const Gdiplus::REAL kBoardLeft   = 5.0f;    // 写字板左边 x
            const Gdiplus::REAL kBoardRight  = 19.0f;   // 写字板右边 x
            const Gdiplus::REAL kBoardTop    = 4.5f;    // 写字板顶边 y
            const Gdiplus::REAL kBoardBottom = 21.0f;   // 写字板底边 y
            const Gdiplus::REAL kClipLeft    = 9.0f;    // 夹子左边 x
            const Gdiplus::REAL kClipRight   = 15.0f;   // 夹子右边 x
            const Gdiplus::REAL kClipTop     = 2.5f;    // 夹子顶边 y
            const Gdiplus::REAL kClipBottom  = 6.5f;    // 夹子底边 y
            const Gdiplus::REAL kDotX        = 7.6f;    // 条目圆点左上角 x
            const Gdiplus::REAL kDotSize     = 2.2f;    // 条目圆点直径
            const Gdiplus::REAL kLineLeft    = 11.0f;   // 条目横线左端 x
            const Gdiplus::REAL kLineRight   = 16.5f;   // 条目横线右端 x
            const Gdiplus::REAL kRowY[]      = { 10.5f, 14.0f, 17.5f };   // 三行条目的中线 y

            Gdiplus::GraphicsPath board;
            board.AddLine(kClipLeft, kBoardTop, kBoardLeft, kBoardTop);
            board.AddLine(kBoardLeft, kBoardTop, kBoardLeft, kBoardBottom);
            board.AddLine(kBoardLeft, kBoardBottom, kBoardRight, kBoardBottom);
            board.AddLine(kBoardRight, kBoardBottom, kBoardRight, kBoardTop);
            board.AddLine(kBoardRight, kBoardTop, kClipRight, kBoardTop);
            g.DrawPath(&pen, &board);
            g.DrawRectangle(&pen, kClipLeft, kClipTop, kClipRight - kClipLeft, kClipBottom - kClipTop);

            for (size_t ri = 0; ri < sizeof(kRowY) / sizeof(kRowY[0]); ++ri)
            {
                g.FillEllipse(&brush, kDotX, kRowY[ri] - kDotSize / 2.0f, kDotSize, kDotSize);
                g.DrawLine(&pen, kLineLeft, kRowY[ri], kLineRight, kRowY[ri]);
            }
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

    case kChatIconShare:
        // 文件共享：文件夹外框（与发送文件夹同一外形）+ 夹内三个圆点用两条线相连，是通用的「共享」符号。
        {
            const Gdiplus::REAL kFolderTop    = 9.6f;   // 夹体顶边 y
            const Gdiplus::REAL kFolderBottom = 20.2f;  // 夹体底边 y
            const Gdiplus::REAL kTabTop       = 6.8f;   // 左上页签顶边 y
            const Gdiplus::REAL kDotR         = 1.5f;   // 三个圆点的半径
            const Gdiplus::REAL kLeftX        = 8.6f;   // 左侧圆点圆心 x
            const Gdiplus::REAL kRightX       = 15.6f;  // 右侧两个圆点圆心 x
            const Gdiplus::REAL kMidY         = 15.0f;  // 左侧圆点圆心 y
            const Gdiplus::REAL kUpperY       = 12.6f;  // 右上圆点圆心 y
            const Gdiplus::REAL kLowerY       = 17.4f;  // 右下圆点圆心 y

            Gdiplus::GraphicsPath folder;
            folder.AddLine(3.0f, kFolderBottom, 3.0f, kTabTop);
            folder.AddLine(3.0f, kTabTop, 9.0f, kTabTop);
            folder.AddLine(9.0f, kTabTop, 11.3f, kFolderTop);
            folder.AddLine(11.3f, kFolderTop, 21.0f, kFolderTop);
            folder.AddLine(21.0f, kFolderTop, 21.0f, kFolderBottom);
            folder.CloseFigure();
            g.DrawPath(&pen, &folder);

            Gdiplus::Pen thin(color, strokeWidth * 0.6f);
            MakeStrokePen(thin);
            g.DrawLine(&thin, kLeftX, kMidY, kRightX, kUpperY);
            g.DrawLine(&thin, kLeftX, kMidY, kRightX, kLowerY);
            g.FillEllipse(&brush, kLeftX - kDotR, kMidY - kDotR, kDotR * 2.0f, kDotR * 2.0f);
            g.FillEllipse(&brush, kRightX - kDotR, kUpperY - kDotR, kDotR * 2.0f, kDotR * 2.0f);
            g.FillEllipse(&brush, kRightX - kDotR, kLowerY - kDotR, kDotR * 2.0f, kDotR * 2.0f);
        }
        break;

    case kChatIconScreenShare:
        // 共享屏幕：与远程桌面相同的显示器外形（圆角屏幕 + 竖杆 + 底座），屏幕内画一个向上的箭头表示「把屏幕分享出去」，
        // 与远程桌面（只有显示器）区分开。
        {
            const Gdiplus::REAL kArrowX      = 12.0f;   // 箭头竖线 x（屏幕水平中线）
            const Gdiplus::REAL kArrowTop    = 7.0f;    // 箭头尖 y
            const Gdiplus::REAL kArrowBottom = 12.5f;   // 箭头竖线底端 y
            const Gdiplus::REAL kArrowWing   = 2.5f;    // 箭头两翼的水平与垂直跨度

            Gdiplus::GraphicsPath screen;
            screen.AddArc(3.0f, 4.0f, 4.0f, 4.0f, 180.0f, 90.0f);
            screen.AddArc(17.0f, 4.0f, 4.0f, 4.0f, 270.0f, 90.0f);
            screen.AddArc(17.0f, 11.0f, 4.0f, 4.0f, 0.0f, 90.0f);
            screen.AddArc(3.0f, 11.0f, 4.0f, 4.0f, 90.0f, 90.0f);
            screen.CloseFigure();
            g.DrawPath(&pen, &screen);
            g.DrawLine(&pen, 12.0f, 15.0f, 12.0f, 19.0f);
            g.DrawLine(&pen, 8.0f, 19.0f, 16.0f, 19.0f);
            g.DrawLine(&pen, kArrowX, kArrowTop, kArrowX, kArrowBottom);
            g.DrawLine(&pen, kArrowX - kArrowWing, kArrowTop + kArrowWing, kArrowX, kArrowTop);
            g.DrawLine(&pen, kArrowX + kArrowWing, kArrowTop + kArrowWing, kArrowX, kArrowTop);
        }
        break;

    case kChatIconGroupInvite:
        // 入群邀请：左侧一个人形（圆形的头 + 半圆弧的肩），右上角一个加号，即「把人加进来」的通用画法
        {
            const Gdiplus::REAL kHeadCX     = 9.5f;    // 头部圆心 x
            const Gdiplus::REAL kHeadCY     = 8.0f;    // 头部圆心 y
            const Gdiplus::REAL kHeadR      = 3.5f;    // 头部半径
            const Gdiplus::REAL kShoulderX  = 3.0f;    // 肩部弧外接矩形左边 x
            const Gdiplus::REAL kShoulderY  = 14.0f;   // 肩部弧外接矩形顶边 y
            const Gdiplus::REAL kShoulderW  = 13.0f;   // 肩部弧外接矩形宽
            const Gdiplus::REAL kShoulderH  = 12.0f;   // 肩部弧外接矩形高（只画上半段）
            const Gdiplus::REAL kPlusCX     = 18.5f;   // 加号中心 x
            const Gdiplus::REAL kPlusCY     = 8.0f;    // 加号中心 y
            const Gdiplus::REAL kPlusHalf   = 2.8f;    // 加号半臂长
            const Gdiplus::REAL kArcStart   = 180.0f;  // 肩部弧起始角（度），从左侧水平方向开始
            const Gdiplus::REAL kArcSweep   = 180.0f;  // 肩部弧扫过的角度（度），画上半圆

            g.DrawEllipse(&pen, kHeadCX - kHeadR, kHeadCY - kHeadR, kHeadR * 2.0f, kHeadR * 2.0f);
            g.DrawArc(&pen, kShoulderX, kShoulderY, kShoulderW, kShoulderH, kArcStart, kArcSweep);
            g.DrawLine(&pen, kPlusCX - kPlusHalf, kPlusCY, kPlusCX + kPlusHalf, kPlusCY);
            g.DrawLine(&pen, kPlusCX, kPlusCY - kPlusHalf, kPlusCX, kPlusCY + kPlusHalf);
        }
        break;

    case kChatIconVoiceWave1:
    case kChatIconVoiceWave2:
    case kChatIconVoiceWave3:
        // 语音：左侧实心喇叭（方块 + 喇叭口），右侧 1~3 道同心圆弧表示声波；播放时按帧轮换道数
        {
            const Gdiplus::REAL kBoxLeft    = 3.0f;    // 喇叭方块左边 x
            const Gdiplus::REAL kBoxTop     = 9.0f;    // 喇叭方块顶边 y
            const Gdiplus::REAL kBoxRight   = 7.0f;    // 喇叭方块右边 x（喇叭口从这里开始）
            const Gdiplus::REAL kBoxBottom  = 15.0f;   // 喇叭方块底边 y
            const Gdiplus::REAL kHornRight  = 12.0f;   // 喇叭口右边 x
            const Gdiplus::REAL kHornTop    = 4.5f;    // 喇叭口上沿 y
            const Gdiplus::REAL kHornBottom = 19.5f;   // 喇叭口下沿 y
            const Gdiplus::REAL kWaveCX     = 12.0f;   // 声波圆心 x
            const Gdiplus::REAL kWaveCY     = 12.0f;   // 声波圆心 y
            const Gdiplus::REAL kWaveR1     = 3.5f;    // 第 1 道声波半径
            const Gdiplus::REAL kWaveStep   = 3.5f;    // 相邻两道声波的半径差
            const Gdiplus::REAL kWaveStart  = -45.0f;  // 声波圆弧起始角（度）
            const Gdiplus::REAL kWaveSweep  = 90.0f;   // 声波圆弧扫过的角度（度）

            Gdiplus::PointF horn[6] = {
                Gdiplus::PointF(kBoxLeft, kBoxTop), Gdiplus::PointF(kBoxRight, kBoxTop),
                Gdiplus::PointF(kHornRight, kHornTop), Gdiplus::PointF(kHornRight, kHornBottom),
                Gdiplus::PointF(kBoxRight, kBoxBottom), Gdiplus::PointF(kBoxLeft, kBoxBottom)
            };
            g.FillPolygon(&brush, horn, 6);
            const int waves = (kind == kChatIconVoiceWave1) ? 1 : ((kind == kChatIconVoiceWave2) ? 2 : 3);
            for (int i = 0; i < waves; ++i)
            {
                const Gdiplus::REAL r = kWaveR1 + kWaveStep * (Gdiplus::REAL)i;
                g.DrawArc(&pen, kWaveCX - r, kWaveCY - r, r * 2.0f, r * 2.0f, kWaveStart, kWaveSweep);
            }
        }
        break;

    case kChatIconRecord:
        // 屏幕录制：场记板。上方是带两道斜纹的打板，下方板身里一个实心圆点表示「录制」。2026-10-06 用户从两组候选里
        // 选定（候选效果图在 tmp/rec_icon/），之前是「圆环 + 实心圆点」。与 MainPanelIcons.cpp 的 kIconRecord 同一造型，
        // 两处的几何常量须保持一致（单元测试 RIC1 检查两者逐像素接近）。
        {
            const Gdiplus::REAL kLeft        = 3.5f;    // 打板与板身的左边 x
            const Gdiplus::REAL kRight       = 20.5f;   // 打板与板身的右边 x
            const Gdiplus::REAL kClapTop     = 5.5f;    // 打板顶边 y
            const Gdiplus::REAL kBodyTop     = 10.0f;   // 板身顶边 y，也是打板的底边
            const Gdiplus::REAL kBodyBottom  = 20.0f;   // 板身底边 y
            const Gdiplus::REAL kCorner      = 1.5f;    // 板身圆角半径
            const Gdiplus::REAL kStripe1X    = 9.5f;    // 第一道斜纹在打板顶边上的 x
            const Gdiplus::REAL kStripe2X    = 15.5f;   // 第二道斜纹在打板顶边上的 x
            const Gdiplus::REAL kStripeSlant = 2.5f;    // 斜纹从顶边到底边向左偏移的距离
            const Gdiplus::REAL kDotX        = 12.0f;   // 圆点圆心 x
            const Gdiplus::REAL kDotY        = 15.0f;   // 圆点圆心 y
            const Gdiplus::REAL kDotR        = 2.2f;    // 圆点半径

            const Gdiplus::REAL d = kCorner * 2.0f;
            Gdiplus::GraphicsPath body;
            body.AddArc(kLeft, kBodyTop, d, d, 180.0f, 90.0f);
            body.AddArc(kRight - d, kBodyTop, d, d, 270.0f, 90.0f);
            body.AddArc(kRight - d, kBodyBottom - d, d, d, 0.0f, 90.0f);
            body.AddArc(kLeft, kBodyBottom - d, d, d, 90.0f, 90.0f);
            body.CloseFigure();
            g.DrawPath(&pen, &body);

            // 打板：左边、顶边、右边三条线，底边与板身顶边共用
            Gdiplus::GraphicsPath clapper;
            clapper.AddLine(kLeft, kBodyTop, kLeft, kClapTop);
            clapper.AddLine(kLeft, kClapTop, kRight, kClapTop);
            clapper.AddLine(kRight, kClapTop, kRight, kBodyTop);
            g.DrawPath(&pen, &clapper);
            g.DrawLine(&pen, kStripe1X, kClapTop, kStripe1X - kStripeSlant, kBodyTop);
            g.DrawLine(&pen, kStripe2X, kClapTop, kStripe2X - kStripeSlant, kBodyTop);

            g.FillEllipse(&brush, kDotX - kDotR, kDotY - kDotR, kDotR * 2.0f, kDotR * 2.0f);
        }
        break;

    case kChatIconDisk:
        // 磁盘：横置的圆角矩形机身，下部一条分隔线，右下一个实心指示灯。
        {
            const Gdiplus::REAL kBodyLeft   = 3.0f;    // 机身左边 x
            const Gdiplus::REAL kBodyTop    = 7.0f;    // 机身顶边 y
            const Gdiplus::REAL kBodyW      = 18.0f;   // 机身宽
            const Gdiplus::REAL kBodyH      = 11.0f;   // 机身高
            const Gdiplus::REAL kCorner     = 2.5f;    // 圆角半径
            const Gdiplus::REAL kSplitY     = 13.5f;   // 分隔线 y
            const Gdiplus::REAL kLedX       = 17.0f;   // 指示灯圆心 x
            const Gdiplus::REAL kLedY       = 15.8f;   // 指示灯圆心 y
            const Gdiplus::REAL kLedR       = 1.1f;    // 指示灯半径

            Gdiplus::GraphicsPath body;
            const Gdiplus::REAL d = kCorner * 2.0f;
            body.AddArc(kBodyLeft, kBodyTop, d, d, 180.0f, 90.0f);
            body.AddArc(kBodyLeft + kBodyW - d, kBodyTop, d, d, 270.0f, 90.0f);
            body.AddArc(kBodyLeft + kBodyW - d, kBodyTop + kBodyH - d, d, d, 0.0f, 90.0f);
            body.AddArc(kBodyLeft, kBodyTop + kBodyH - d, d, d, 90.0f, 90.0f);
            body.CloseFigure();
            g.DrawPath(&pen, &body);
            g.DrawLine(&pen, kBodyLeft, kSplitY, kBodyLeft + kBodyW, kSplitY);
            g.FillEllipse(&brush, kLedX - kLedR, kLedY - kLedR, kLedR * 2.0f, kLedR * 2.0f);
        }
        break;

    case kChatIconSubtitle:
        // 字幕模式：圆角框表示画面，框内下部一短一长两行居中的横线表示字幕
        {
            Gdiplus::GraphicsPath frame;
            frame.AddArc(3.0f, 5.0f, 4.0f, 4.0f, 180.0f, 90.0f);
            frame.AddArc(17.0f, 5.0f, 4.0f, 4.0f, 270.0f, 90.0f);
            frame.AddArc(17.0f, 15.0f, 4.0f, 4.0f, 0.0f, 90.0f);
            frame.AddArc(3.0f, 15.0f, 4.0f, 4.0f, 90.0f, 90.0f);
            frame.CloseFigure();
            g.DrawPath(&pen, &frame);
            g.DrawLine(&pen, 9.0f, 12.5f, 15.0f, 12.5f);   // 上一行（短）
            g.DrawLine(&pen, 7.0f, 15.5f, 17.0f, 15.5f);   // 下一行（长）
        }
        break;

    case kChatIconLock:
    case kChatIconUnlock:
        // 挂锁：圆角矩形锁身 + 中间的锁孔圆点 + 上方半圆锁梁。锁定时锁梁两腿都插进锁身；
        // 解锁时整个锁梁抬高，左腿离开锁身（右腿仍连着），是「打开的锁」的通用画法
        {
            const Gdiplus::REAL kBodyTop    = 11.0f;   // 锁身顶边 y
            const Gdiplus::REAL kBodyLeft   = 5.0f;    // 锁身左边 x
            const Gdiplus::REAL kBodyW      = 14.0f;   // 锁身宽
            const Gdiplus::REAL kBodyH      = 9.0f;    // 锁身高
            const Gdiplus::REAL kBodyRadius = 2.0f;    // 锁身圆角半径
            const Gdiplus::REAL kShackleR   = 4.0f;    // 锁梁半径
            const Gdiplus::REAL kShackleCX  = 12.0f;   // 锁梁圆心 x
            const Gdiplus::REAL kHoleR      = 1.3f;    // 锁孔圆点半径
            const Gdiplus::REAL kHoleY      = 15.5f;   // 锁孔圆点圆心 y
            const bool open = (kind == kChatIconUnlock);
            const Gdiplus::REAL shackleCY = open ? 6.0f : 8.0f;   // 锁梁圆心 y：解锁时抬高 2
            const Gdiplus::REAL leftLegBottom = open ? 8.0f : kBodyTop;   // 左腿下端：解锁时不碰锁身

            Gdiplus::GraphicsPath body;
            const Gdiplus::REAL d = kBodyRadius * 2.0f;
            body.AddArc(kBodyLeft, kBodyTop, d, d, 180.0f, 90.0f);
            body.AddArc(kBodyLeft + kBodyW - d, kBodyTop, d, d, 270.0f, 90.0f);
            body.AddArc(kBodyLeft + kBodyW - d, kBodyTop + kBodyH - d, d, d, 0.0f, 90.0f);
            body.AddArc(kBodyLeft, kBodyTop + kBodyH - d, d, d, 90.0f, 90.0f);
            body.CloseFigure();
            g.DrawPath(&pen, &body);
            g.DrawArc(&pen, kShackleCX - kShackleR, shackleCY - kShackleR, kShackleR * 2.0f, kShackleR * 2.0f,
                      180.0f, 180.0f);
            g.DrawLine(&pen, kShackleCX - kShackleR, shackleCY, kShackleCX - kShackleR, leftLegBottom);
            g.DrawLine(&pen, kShackleCX + kShackleR, shackleCY, kShackleCX + kShackleR, kBodyTop);
            g.FillEllipse(&brush, kShackleCX - kHoleR, kHoleY - kHoleR, kHoleR * 2.0f, kHoleR * 2.0f);
        }
        break;

    case kChatIconBackToChat:
        // 回到聊天窗：右上角缺口的方框表示窗口，一支箭头从框内指向右上角，即「跳转到窗口」的通用画法
        {
            Gdiplus::GraphicsPath frame;
            frame.AddLine(11.0f, 4.0f, 6.0f, 4.0f);
            frame.AddArc(4.0f, 4.0f, 4.0f, 4.0f, 270.0f, -90.0f);
            frame.AddLine(4.0f, 6.0f, 4.0f, 18.0f);
            frame.AddArc(4.0f, 16.0f, 4.0f, 4.0f, 180.0f, -90.0f);
            frame.AddLine(6.0f, 20.0f, 18.0f, 20.0f);
            frame.AddArc(16.0f, 16.0f, 4.0f, 4.0f, 90.0f, -90.0f);
            frame.AddLine(20.0f, 18.0f, 20.0f, 13.0f);
            g.DrawPath(&pen, &frame);
            g.DrawLine(&pen, 11.0f, 13.0f, 20.0f, 4.0f);   // 箭杆
            g.DrawLine(&pen, 14.5f, 4.0f, 20.0f, 4.0f);    // 箭头上翼
            g.DrawLine(&pen, 20.0f, 4.0f, 20.0f, 9.5f);    // 箭头右翼
        }
        break;

    case kChatIconClose:
        // 叉号：两条对角线
        g.DrawLine(&pen, 6.0f, 6.0f, 18.0f, 18.0f);
        g.DrawLine(&pen, 18.0f, 6.0f, 6.0f, 18.0f);
        break;

    case kChatIconAnnounce:
        // 群公告：扩音器。左侧一个小方块作喇叭尾部，向右张开成喇叭口，喇叭口右端一条竖线封口，
        // 方块下方斜出一截手柄，即「发通知」的通用画法
        {
            const Gdiplus::REAL kTailLeft   = 3.5f;    // 喇叭尾部方块左边 x
            const Gdiplus::REAL kTailRight  = 8.0f;    // 喇叭尾部方块右边 x（喇叭口从这里开始张开）
            const Gdiplus::REAL kTailTop    = 9.0f;    // 喇叭尾部方块顶边 y
            const Gdiplus::REAL kTailBottom = 15.0f;   // 喇叭尾部方块底边 y
            const Gdiplus::REAL kMouthX     = 19.5f;   // 喇叭口右端 x
            const Gdiplus::REAL kMouthTop   = 4.0f;    // 喇叭口上沿 y
            const Gdiplus::REAL kMouthBot   = 20.0f;   // 喇叭口下沿 y
            const Gdiplus::REAL kHandleX0   = 6.0f;    // 手柄起点 x（尾部方块底边上）
            const Gdiplus::REAL kHandleX1   = 8.0f;    // 手柄终点 x
            const Gdiplus::REAL kHandleY1   = 20.0f;   // 手柄终点 y

            Gdiplus::GraphicsPath horn;
            horn.AddLine(kTailLeft, kTailTop, kTailRight, kTailTop);
            horn.AddLine(kTailRight, kTailTop, kMouthX, kMouthTop);
            horn.AddLine(kMouthX, kMouthTop, kMouthX, kMouthBot);
            horn.AddLine(kMouthX, kMouthBot, kTailRight, kTailBottom);
            horn.AddLine(kTailRight, kTailBottom, kTailLeft, kTailBottom);
            horn.CloseFigure();
            g.DrawPath(&pen, &horn);
            g.DrawLine(&pen, kHandleX0, kTailBottom, kHandleX1, kHandleY1);
        }
        break;

    case kChatIconMemo:
        // 群备忘：日历。圆角外框，框内上部一条横线隔出表头，顶边上方两个挂环，格内一个对勾
        {
            const Gdiplus::REAL kBoxLeft    = 4.0f;    // 外框左边 x
            const Gdiplus::REAL kBoxTop     = 6.0f;    // 外框顶边 y
            const Gdiplus::REAL kBoxRight   = 20.0f;   // 外框右边 x
            const Gdiplus::REAL kBoxBottom  = 20.0f;   // 外框底边 y
            const Gdiplus::REAL kCorner     = 2.0f;    // 外框圆角直径
            const Gdiplus::REAL kHeaderY    = 10.0f;   // 表头分隔线 y
            const Gdiplus::REAL kRingLeftX  = 8.5f;    // 左挂环 x
            const Gdiplus::REAL kRingRightX = 15.5f;   // 右挂环 x
            const Gdiplus::REAL kRingTop    = 3.5f;    // 挂环上端 y
            const Gdiplus::REAL kRingBottom = 8.0f;    // 挂环下端 y
            const Gdiplus::REAL kTickX0     = 8.5f;    // 对勾起点 x
            const Gdiplus::REAL kTickY0     = 15.0f;   // 对勾起点 y
            const Gdiplus::REAL kTickX1     = 11.0f;   // 对勾拐点 x
            const Gdiplus::REAL kTickY1     = 17.5f;   // 对勾拐点 y
            const Gdiplus::REAL kTickX2     = 15.5f;   // 对勾终点 x
            const Gdiplus::REAL kTickY2     = 12.5f;   // 对勾终点 y
            const Gdiplus::REAL kArc90      = 90.0f;   // 圆角弧扫过的角度（度）

            Gdiplus::GraphicsPath box;
            box.AddArc(kBoxLeft, kBoxTop, kCorner, kCorner, 2.0f * kArc90, kArc90);
            box.AddArc(kBoxRight - kCorner, kBoxTop, kCorner, kCorner, 3.0f * kArc90, kArc90);
            box.AddArc(kBoxRight - kCorner, kBoxBottom - kCorner, kCorner, kCorner, 0.0f, kArc90);
            box.AddArc(kBoxLeft, kBoxBottom - kCorner, kCorner, kCorner, kArc90, kArc90);
            box.CloseFigure();
            g.DrawPath(&pen, &box);
            g.DrawLine(&pen, kBoxLeft, kHeaderY, kBoxRight, kHeaderY);
            g.DrawLine(&pen, kRingLeftX, kRingTop, kRingLeftX, kRingBottom);
            g.DrawLine(&pen, kRingRightX, kRingTop, kRingRightX, kRingBottom);
            g.DrawLine(&pen, kTickX0, kTickY0, kTickX1, kTickY1);
            g.DrawLine(&pen, kTickX1, kTickY1, kTickX2, kTickY2);
        }
        break;

    case kChatIconCaret:
        // 下拉箭头：图标中心一个尖朝下的实心小三角。按钮只有 14 像素宽，三角形只占 viewBox
        // 中央 7×4 的范围，按钮宽度小于图标边长时也不会画出按钮之外
        {
            const Gdiplus::REAL kCaretLeft   = 8.5f;    // 三角形左上角 x
            const Gdiplus::REAL kCaretRight  = 15.5f;   // 三角形右上角 x
            const Gdiplus::REAL kCaretTop    = 10.5f;   // 三角形上边 y
            const Gdiplus::REAL kCaretTipX   = 12.0f;   // 尖端 x（水平居中）
            const Gdiplus::REAL kCaretTipY   = 14.5f;   // 尖端 y
            Gdiplus::PointF tri[3] =
            {
                Gdiplus::PointF(kCaretLeft, kCaretTop),
                Gdiplus::PointF(kCaretRight, kCaretTop),
                Gdiplus::PointF(kCaretTipX, kCaretTipY),
            };
            Gdiplus::SolidBrush caretBrush(color);
            g.FillPolygon(&caretBrush, tri, 3);
        }
        break;

    case kChatIconFont:
        // 字体设置 —— 一个大写字母「A」（两条斜腿 + 中横），文字 / 字体的通用
        // 形象。对照 design/聊天字体设置1.png toolbar 上的「AF」按钮。
        g.DrawLine(&pen, 5.5f, 19.0f, 12.0f, 5.0f);    // 左腿
        g.DrawLine(&pen, 12.0f, 5.0f, 18.5f, 19.0f);   // 右腿
        g.DrawLine(&pen, 8.2f, 13.0f, 15.8f, 13.0f);   // 中横
        break;

    case kChatIconCode:
        // 发送代码 ——「</>」：左右两个尖括号夹一条斜线，代码的通用形象
        g.DrawLine(&pen, 8.0f, 7.0f, 3.5f, 12.0f);     // 左尖括号上半
        g.DrawLine(&pen, 3.5f, 12.0f, 8.0f, 17.0f);    // 左尖括号下半
        g.DrawLine(&pen, 16.0f, 7.0f, 20.5f, 12.0f);   // 右尖括号上半
        g.DrawLine(&pen, 20.5f, 12.0f, 16.0f, 17.0f);  // 右尖括号下半
        g.DrawLine(&pen, 13.5f, 5.0f, 10.5f, 19.0f);   // 中间的斜线
        break;

    case kChatIconSystemAudio:
        // 共享电脑声音：与远程桌面相同的显示器外形（圆角屏幕 + 竖杆 + 底座），屏幕内画一个八分音符（实心符头 + 符干 +
        // 符尾），表示「把电脑播放的声音分享出去」，与共享屏幕（屏幕内向上的箭头）区分开
        {
            const Gdiplus::REAL kHeadCX   = 10.4f;   // 符头中心 x
            const Gdiplus::REAL kHeadCY   = 11.6f;   // 符头中心 y
            const Gdiplus::REAL kHeadRX   = 2.0f;    // 符头横向半径
            const Gdiplus::REAL kHeadRY   = 1.5f;    // 符头纵向半径
            const Gdiplus::REAL kStemX    = 12.2f;   // 符干 x（贴着符头右缘）
            const Gdiplus::REAL kStemTop  = 6.5f;    // 符干顶端 y
            const Gdiplus::REAL kFlagX    = 15.2f;   // 符尾末端 x
            const Gdiplus::REAL kFlagY    = 8.6f;    // 符尾末端 y

            Gdiplus::GraphicsPath screen;
            screen.AddArc(3.0f, 4.0f, 4.0f, 4.0f, 180.0f, 90.0f);
            screen.AddArc(17.0f, 4.0f, 4.0f, 4.0f, 270.0f, 90.0f);
            screen.AddArc(17.0f, 11.0f, 4.0f, 4.0f, 0.0f, 90.0f);
            screen.AddArc(3.0f, 11.0f, 4.0f, 4.0f, 90.0f, 90.0f);
            screen.CloseFigure();
            g.DrawPath(&pen, &screen);
            g.DrawLine(&pen, 12.0f, 15.0f, 12.0f, 19.0f);
            g.DrawLine(&pen, 8.0f, 19.0f, 16.0f, 19.0f);
            g.FillEllipse(&brush, kHeadCX - kHeadRX, kHeadCY - kHeadRY, kHeadRX * 2.0f, kHeadRY * 2.0f);
            g.DrawLine(&pen, kStemX, kHeadCY, kStemX, kStemTop);
            g.DrawLine(&pen, kStemX, kStemTop, kFlagX, kFlagY);
        }
        break;

    case kChatIconOrgTree:
        // 组织架构图（「按部门」切换按钮）：上方一个框，竖线向下接到一条横线，横线两端各向下接一个框，
        // 表示部门的上下级。几何与 2026-10-07 给用户看的候选样图 A 相同（tmp/icon_preview/member_view_icons.py）
        {
            const Gdiplus::REAL kBoxRadius   = 1.5f;    // 三个框的圆角半径
            const Gdiplus::REAL kTopLeft     = 8.5f;    // 上方框左边 x
            const Gdiplus::REAL kTopRight    = 15.5f;   // 上方框右边 x
            const Gdiplus::REAL kTopTop      = 3.0f;    // 上方框顶边 y
            const Gdiplus::REAL kTopBottom   = 8.5f;    // 上方框底边 y（竖线从这里向下）
            const Gdiplus::REAL kCenterX     = 12.0f;   // 竖线 x
            const Gdiplus::REAL kBarY        = 12.0f;   // 连接横线 y
            const Gdiplus::REAL kLeftStemX   = 6.0f;    // 左下框上方连接线 x（左下框的中线）
            const Gdiplus::REAL kRightStemX  = 18.0f;   // 右下框上方连接线 x（右下框的中线）
            const Gdiplus::REAL kLowTop      = 15.5f;   // 两个下方框的顶边 y
            const Gdiplus::REAL kLowBottom   = 21.0f;   // 两个下方框的底边 y
            const Gdiplus::REAL kLowHalfW    = 3.5f;    // 下方框宽度的一半

            DrawRoundBox(g, pen, kTopLeft, kTopTop, kTopRight, kTopBottom, kBoxRadius);
            g.DrawLine(&pen, kCenterX, kTopBottom, kCenterX, kBarY);
            // 横线与两端向下的短线画成一条折线，拐角按圆角连接
            const Gdiplus::PointF connector[4] =
            {
                Gdiplus::PointF(kLeftStemX, kLowTop),
                Gdiplus::PointF(kLeftStemX, kBarY),
                Gdiplus::PointF(kRightStemX, kBarY),
                Gdiplus::PointF(kRightStemX, kLowTop),
            };
            g.DrawLines(&pen, connector, 4);
            DrawRoundBox(g, pen, kLeftStemX - kLowHalfW, kLowTop, kLeftStemX + kLowHalfW, kLowBottom, kBoxRadius);
            DrawRoundBox(g, pen, kRightStemX - kLowHalfW, kLowTop, kRightStemX + kLowHalfW, kLowBottom, kBoxRadius);
        }
        break;

    // 合并转发的聊天记录卡片：一个带尖角的对话气泡，右侧再画一道弧线，表示后面还叠着一条（docs/聊天卡片美化方案.md 3.1）
    case kChatIconChatRecord:
        {
            const Gdiplus::REAL kBubbleLeft   = 2.5f;    // 气泡左边 x
            const Gdiplus::REAL kBubbleTop    = 5.0f;    // 气泡顶边 y
            const Gdiplus::REAL kBubbleRight  = 17.5f;   // 气泡右边 x
            const Gdiplus::REAL kBubbleBottom = 15.0f;   // 气泡底边 y
            const Gdiplus::REAL kBubbleCorner = 4.0f;    // 气泡圆角直径
            const Gdiplus::REAL kTailRight    = 10.0f;   // 尖角在底边上的右端 x
            const Gdiplus::REAL kTailLeft     = 6.0f;    // 尖角在底边上的左端 x，也是尖角竖边的 x
            const Gdiplus::REAL kTailTipY     = 18.0f;   // 尖角下端 y
            const Gdiplus::REAL kBackX        = 20.5f;   // 后面那一条的竖线 x
            const Gdiplus::REAL kBackTop      = 9.5f;    // 竖线上端 y
            const Gdiplus::REAL kBackBottom   = 15.0f;   // 竖线下端 y（从这里转成圆角）
            const Gdiplus::REAL kBackCorner   = 4.0f;    // 竖线转角的圆角直径
            const Gdiplus::REAL kArc90        = 90.0f;   // 圆角弧扫过的角度（度）

            Gdiplus::GraphicsPath bubble;
            bubble.AddArc(kBubbleLeft, kBubbleTop, kBubbleCorner, kBubbleCorner, 2.0f * kArc90, kArc90);
            bubble.AddArc(kBubbleRight - kBubbleCorner, kBubbleTop, kBubbleCorner, kBubbleCorner, 3.0f * kArc90, kArc90);
            bubble.AddArc(kBubbleRight - kBubbleCorner, kBubbleBottom - kBubbleCorner, kBubbleCorner, kBubbleCorner, 0.0f,
                          kArc90);
            bubble.AddLine(kBubbleRight - kBubbleCorner / 2.0f, kBubbleBottom, kTailRight, kBubbleBottom);
            bubble.AddLine(kTailRight, kBubbleBottom, kTailLeft, kTailTipY);
            bubble.AddLine(kTailLeft, kTailTipY, kTailLeft, kBubbleBottom);
            bubble.AddArc(kBubbleLeft, kBubbleBottom - kBubbleCorner, kBubbleCorner, kBubbleCorner, kArc90, kArc90);
            bubble.CloseFigure();
            g.DrawPath(&pen, &bubble);

            g.DrawLine(&pen, kBackX, kBackTop, kBackX, kBackBottom);
            g.DrawArc(&pen, kBackX - kBackCorner, kBackBottom - kBackCorner / 2.0f, kBackCorner, kBackCorner, 0.0f, kArc90);
        }
        break;

    // 云端录像卡片：圆角外框，框内一个向右的播放三角（同上）
    case kChatIconCloudRecord:
        {
            const Gdiplus::REAL kBoxLeft    = 3.0f;    // 外框左边 x
            const Gdiplus::REAL kBoxTop     = 5.0f;    // 外框顶边 y
            const Gdiplus::REAL kBoxRight   = 21.0f;   // 外框右边 x
            const Gdiplus::REAL kBoxBottom  = 19.0f;   // 外框底边 y
            const Gdiplus::REAL kBoxRadius  = 2.5f;    // 外框圆角半径
            const Gdiplus::REAL kPlayLeft   = 10.2f;   // 播放三角左边 x
            const Gdiplus::REAL kPlayTop    = 9.3f;    // 播放三角上顶点 y
            const Gdiplus::REAL kPlayBottom = 14.7f;   // 播放三角下顶点 y
            const Gdiplus::REAL kPlayTipX   = 14.8f;   // 播放三角尖端 x
            const Gdiplus::REAL kPlayTipY   = 12.0f;   // 播放三角尖端 y

            DrawRoundBox(g, pen, kBoxLeft, kBoxTop, kBoxRight, kBoxBottom, kBoxRadius);
            Gdiplus::GraphicsPath play;
            play.AddLine(kPlayLeft, kPlayTop, kPlayTipX, kPlayTipY);
            play.AddLine(kPlayTipX, kPlayTipY, kPlayLeft, kPlayBottom);
            play.CloseFigure();
            g.DrawPath(&pen, &play);
        }
        break;

    // 语音通话卡片：电话听筒的轮廓线。几何取自卡片效果图（tmp/chatstyle/cards/卡片对比.html 的 i-phone）：
    // 上端听筒、下端话筒各是一段折线，两者之间是一条略向左下弯的内弧与一条向左下鼓出的外弧，圆弧用贝塞尔曲线逼近
    case kChatIconPhoneLine:
        {
            Gdiplus::GraphicsPath path;
            path.AddLine(6.6f, 3.5f, 9.4f, 3.5f);       // 听筒顶边
            path.AddLine(9.4f, 3.5f, 10.9f, 7.7f);      // 听筒右侧斜边
            path.AddLine(10.9f, 7.7f, 8.9f, 9.0f);      // 听筒下沿
            path.AddBezier(8.9f, 9.0f, 10.07f, 11.9f, 12.1f, 13.93f, 15.0f, 15.1f);   // 内弧（半径 10.5）
            path.AddLine(15.0f, 15.1f, 16.3f, 13.1f);   // 话筒上沿
            path.AddLine(16.3f, 13.1f, 20.5f, 14.6f);   // 话筒右上斜边
            path.AddLine(20.5f, 14.6f, 20.5f, 17.4f);   // 话筒右边
            path.AddBezier(20.5f, 17.4f, 20.5f, 18.5f, 19.5f, 19.4f, 18.4f, 19.4f);   // 话筒右下圆角
            path.AddBezier(18.4f, 19.4f, 11.29f, 18.31f, 5.69f, 12.71f, 4.6f, 5.6f);  // 外弧（半径 16.5）
            path.AddBezier(4.6f, 5.6f, 4.6f, 4.5f, 5.5f, 3.5f, 6.6f, 3.5f);           // 听筒左上圆角
            path.CloseFigure();
            g.DrawPath(&pen, &path);
        }
        break;

    // 复制：前面一张完整的圆角纸，后面一张只露出左上角的两条边（几何取自卡片效果图的 i-copy）
    case kChatIconCopy:
        {
            const Gdiplus::REAL kFrontLeft  = 8.0f;    // 前面那张的左边 x
            const Gdiplus::REAL kFrontTop   = 8.0f;    // 前面那张的顶边 y
            const Gdiplus::REAL kFrontRight = 20.0f;   // 前面那张的右边 x
            const Gdiplus::REAL kFrontBot   = 20.0f;   // 前面那张的底边 y
            const Gdiplus::REAL kRadius     = 2.0f;    // 圆角半径
            const Gdiplus::REAL kBackLeft   = 4.0f;    // 后面那张的左边 x
            const Gdiplus::REAL kBackTop    = 4.0f;    // 后面那张的顶边 y
            const Gdiplus::REAL kBackRight  = 16.0f;   // 后面那张的右边 x（只画到前面那张的顶边）
            const Gdiplus::REAL kBackBot    = 16.0f;   // 后面那张的底边 y（只画到前面那张的左边）
            const Gdiplus::REAL kArc90      = 90.0f;   // 圆角弧扫过的角度（度）
            const Gdiplus::REAL d = kRadius * 2.0f;

            DrawRoundBox(g, pen, kFrontLeft, kFrontTop, kFrontRight, kFrontBot, kRadius);
            Gdiplus::GraphicsPath back;
            back.AddLine(kBackRight, kFrontTop, kBackRight, kBackTop + kRadius);
            back.AddArc(kBackRight - d, kBackTop, d, d, 0.0f, -kArc90);
            back.AddLine(kBackRight - kRadius, kBackTop, kBackLeft + kRadius, kBackTop);
            back.AddArc(kBackLeft, kBackTop, d, d, -kArc90, -kArc90);
            back.AddLine(kBackLeft, kBackTop + kRadius, kBackLeft, kBackBot - kRadius);
            back.AddArc(kBackLeft, kBackBot - d, d, d, 2.0f * kArc90, -kArc90);
            back.AddLine(kBackLeft + kRadius, kBackBot, kFrontLeft, kBackBot);
            g.DrawPath(&pen, &back);
        }
        break;

    // Markdown 编辑器按钮与格式按钮的图标（kChatIconMarkdown 起的一段），绘制在 ChatMarkdownIcons.cpp
    default:
        DrawMarkdownChatIcon(g, kind, pen, color);
        break;
    }

    g.Restore(st);
}

} // namespace balloonchatwnd
