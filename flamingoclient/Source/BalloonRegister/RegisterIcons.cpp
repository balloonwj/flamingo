/**
 *  RegisterIcons.cpp
 *
 *  注册对话框线性图标的 GDI+ 绘制实现。每个图标的几何取自 design 的
 *  icons.jsx（viewBox 24）。绘制时先 translate + scale 把 viewBox 映射
 *  到目标区域，再描边 / 填充。
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

#include "RegisterIcons.h"

namespace balloonregister {

namespace {

// 图标统一 viewBox 边长（与 design icons.jsx 的 "0 0 24 24" 一致）。
const Gdiplus::REAL kRegIconViewBox = 24.0f;

// 配好圆头圆角的描边 Pen —— 与 design 的 strokeLinecap/round 风格一致。
void MakeStrokePen(Gdiplus::Pen& pen)
{
    pen.SetStartCap(Gdiplus::LineCapRound);
    pen.SetEndCap(Gdiplus::LineCapRound);
    pen.SetLineJoin(Gdiplus::LineJoinRound);
}

// 向 path 添加一个圆角矩形（viewBox 坐标）。
void AddRoundRect(Gdiplus::GraphicsPath& path,
                  Gdiplus::REAL x, Gdiplus::REAL y,
                  Gdiplus::REAL w, Gdiplus::REAL h, Gdiplus::REAL radius)
{
    Gdiplus::REAL r = radius;
    if (r > w / 2.0f)
    {
        r = w / 2.0f;
    }
    if (r > h / 2.0f)
    {
        r = h / 2.0f;
    }
    const Gdiplus::REAL d = r * 2.0f;
    path.AddArc(x,         y,         d, d, 180.0f, 90.0f);
    path.AddArc(x + w - d, y,         d, d, 270.0f, 90.0f);
    path.AddArc(x + w - d, y + h - d, d, d, 0.0f,   90.0f);
    path.AddArc(x,         y + h - d, d, d, 90.0f,  90.0f);
    path.CloseFigure();
}

} // anonymous namespace

void DrawRegIcon(Gdiplus::Graphics& g, int kind,
                 Gdiplus::REAL x, Gdiplus::REAL y, Gdiplus::REAL size,
                 const Gdiplus::Color& color, Gdiplus::REAL strokeWidth)
{
    if (size <= 0.0f)
    {
        return;
    }

    // 把 viewBox 24 映射到目标 [x, y, size] 方块。
    Gdiplus::GraphicsState st = g.Save();
    g.TranslateTransform(x, y);
    g.ScaleTransform(size / kRegIconViewBox, size / kRegIconViewBox);

    Gdiplus::Pen pen(color, strokeWidth);
    MakeStrokePen(pen);

    switch (kind)
    {
    case kRegIconUser:
        // 人像：头部圆 + 肩部弧。对应 icons.jsx 的 user
        // （circle cx12 cy8 r4 + 下方半身弧）。
        g.DrawEllipse(&pen, 8.0f, 4.0f, 8.0f, 8.0f);
        {
            // 肩部弧：以 (12,22) 为心、半径 8 的上半段弧，近似
            // "M4 20c1-4 4-6 8-6s7 2 8 6"。
            Gdiplus::GraphicsPath body;
            body.AddArc(4.0f, 13.0f, 16.0f, 16.0f, 200.0f, 140.0f);
            g.DrawPath(&pen, &body);
        }
        break;

    case kRegIconQrcode:
        // 二维码：左上 / 右上 / 左下三个方框 + 右下若干小段。
        // 对应 icons.jsx 的 qrcode。
        g.DrawRectangle(&pen, 4.0f, 4.0f, 6.0f, 6.0f);
        g.DrawRectangle(&pen, 14.0f, 4.0f, 6.0f, 6.0f);
        g.DrawRectangle(&pen, 4.0f, 14.0f, 6.0f, 6.0f);
        {
            // 右下角：几条短线段拼出散点造型（"M14 14h2v2 M18 14v2 ..."）。
            g.DrawLine(&pen, 14.0f, 14.0f, 16.0f, 14.0f);
            g.DrawLine(&pen, 16.0f, 14.0f, 16.0f, 16.0f);
            g.DrawLine(&pen, 18.0f, 14.0f, 18.0f, 16.0f);
            g.DrawLine(&pen, 14.0f, 18.0f, 16.0f, 18.0f);
            g.DrawLine(&pen, 18.0f, 18.0f, 20.0f, 18.0f);
        }
        break;

    case kRegIconLock:
        // 锁：下部锁体圆角矩形 + 上部锁梁半圆弧。对应 icons.jsx 的 lock
        // （rect x5 y11 w14 h9 + path M8 11V8a4 4 0 0 1 8 0v3）。
        {
            Gdiplus::GraphicsPath bodyRect;
            AddRoundRect(bodyRect, 5.0f, 11.0f, 14.0f, 9.0f, 2.0f);
            g.DrawPath(&pen, &bodyRect);

            // 锁梁：从锁体上沿向上的两根竖线 + 顶部半圆。
            Gdiplus::GraphicsPath shackle;
            shackle.AddLine(8.0f, 11.0f, 8.0f, 8.0f);
            shackle.AddArc(8.0f, 4.0f, 8.0f, 8.0f, 180.0f, 180.0f);
            shackle.AddLine(16.0f, 8.0f, 16.0f, 11.0f);
            g.DrawPath(&pen, &shackle);
        }
        break;

    case kRegIconPhone:
        // 电话听筒：经典话筒剪影。对应 icons.jsx 的 phone
        // （"M5 4h4l1.5 4-2 1.5a11 11 0 0 0 6 6L16 13.5l4 1.5v4..."）。
        {
            Gdiplus::GraphicsPath handset;
            handset.AddLine(5.0f, 4.0f, 9.0f, 4.0f);
            handset.AddLine(9.0f, 4.0f, 10.5f, 8.0f);
            handset.AddLine(10.5f, 8.0f, 8.5f, 9.5f);
            // 中段弧：模拟话筒中部贴合手掌的弯曲。
            handset.AddArc(3.5f, 9.5f, 11.0f, 11.0f, 0.0f, 90.0f);
            handset.AddLine(14.5f, 15.5f, 16.0f, 13.5f);
            handset.AddLine(16.0f, 13.5f, 20.0f, 15.0f);
            handset.AddLine(20.0f, 15.0f, 20.0f, 19.0f);
            // 收尾弧：右下角圆角收口。
            handset.AddArc(15.0f, 14.0f, 10.0f, 10.0f, 0.0f, 95.0f);
            g.DrawPath(&pen, &handset);
        }
        break;

    case kRegIconCheck:
        // 对勾：两段折线。对应 icons.jsx 的 check（"M5 12.5l4.5 4.5L20 7"）。
        {
            Gdiplus::GraphicsPath chk;
            chk.AddLine(5.0f, 12.5f, 9.5f, 17.0f);
            chk.AddLine(9.5f, 17.0f, 20.0f, 7.0f);
            g.DrawPath(&pen, &chk);
        }
        break;

    case kRegIconChevron:
        // 右尖括号：两段折线。对应 icons.jsx 的 chevron（"M9 6l6 6-6 6"）。
        // 注册返回按钮需要的是左箭头 —— 调用方先把绘制区按中心旋转
        // 180° 即得左向；本函数只负责画原始右向形状。
        {
            Gdiplus::GraphicsPath chev;
            chev.AddLine(9.0f, 6.0f, 15.0f, 12.0f);
            chev.AddLine(15.0f, 12.0f, 9.0f, 18.0f);
            g.DrawPath(&pen, &chev);
        }
        break;

    case kRegIconCompany:
        // 楼宇：一栋带门与窗格的办公楼，用于"公司名称"输入框。
        // 与 icons.jsx 其它图标同一套风格（viewBox 24、圆头圆角描边）。
        {
            // 楼体外轮廓：底边落在 y=20，与 user/lock 等图标的下沿齐平。
            Gdiplus::GraphicsPath outline;
            AddRoundRect(outline, 5.0f, 4.0f, 14.0f, 16.0f, 1.5f);
            g.DrawPath(&pen, &outline);

            // 底部大门：楼体正中的一个小矩形缺口，让轮廓一眼读作"楼"而非"框"。
            g.DrawLine(&pen, 10.5f, 20.0f, 10.5f, 16.0f);
            g.DrawLine(&pen, 10.5f, 16.0f, 13.5f, 16.0f);
            g.DrawLine(&pen, 13.5f, 16.0f, 13.5f, 20.0f);

            // 窗格：左右各两行短线，避免画满格子在 16px 下糊成一团。
            g.DrawLine(&pen, 8.0f, 8.0f, 9.5f, 8.0f);
            g.DrawLine(&pen, 14.5f, 8.0f, 16.0f, 8.0f);
            g.DrawLine(&pen, 8.0f, 12.0f, 9.5f, 12.0f);
            g.DrawLine(&pen, 14.5f, 12.0f, 16.0f, 12.0f);
        }
        break;

    default:
        break;
    }

    g.Restore(st);
}

} // namespace balloonregister
