/**
 *  MainPanelIcons.cpp
 *
 *  主面板线性图标的 GDI+ 描边绘制实现。每个图标的几何逐字取自 design 的
 *  icons.jsx（viewBox 24）。绘制时先 translate + scale 把 viewBox 映射到
 *  目标区域，再用圆头圆角的 Pen 描边。
 *
 *  本 .cpp 不 include Flamingo 的 stdafx.h —— 它要被 Flamingo.vcxproj 与
 *  FlamingoUnit.vcxproj 共用。在 Flamingo.vcxproj 里需标记
 *  PrecompiledHeader=NotUsing。
 *
 *  balloonwj@qq.com   2026-05-21
 */

#include <windows.h>
#include <gdiplus.h>
#include <math.h>          // cos / sin —— 齿轮图标按角度均布轮齿
#pragma comment(lib, "gdiplus.lib")

#include "MainPanelIcons.h"

namespace balloonmain {

namespace {

// viewBox 基准边长（design icons.jsx 全部用 0 0 24 24）。
const Gdiplus::REAL kViewBox = 24.0f;

// 配好圆头圆角的描边 Pen —— 线性图标的端点 / 折点都要圆润。
void MakeStrokePen(Gdiplus::Pen& pen)
{
    pen.SetStartCap(Gdiplus::LineCapRound);
    pen.SetEndCap(Gdiplus::LineCapRound);
    pen.SetLineJoin(Gdiplus::LineJoinRound);
}

// 往 path 添加一个圆角矩形（四段 90° 圆弧 + 闭合）。锁体等需要圆角描边时用。
void AddRoundRect(Gdiplus::GraphicsPath& path,
                  Gdiplus::REAL x, Gdiplus::REAL y,
                  Gdiplus::REAL w, Gdiplus::REAL h, Gdiplus::REAL radius)
{
    Gdiplus::REAL r = radius;
    if (r > w / 2.0f) { r = w / 2.0f; }
    if (r > h / 2.0f) { r = h / 2.0f; }
    const Gdiplus::REAL d = r * 2.0f;

    path.AddArc(x,         y,         d, d, 180.0f, 90.0f);
    path.AddArc(x + w - d, y,         d, d, 270.0f, 90.0f);
    path.AddArc(x + w - d, y + h - d, d, d, 0.0f,   90.0f);
    path.AddArc(x,         y + h - d, d, d, 90.0f,  90.0f);
    path.CloseFigure();
}

// 齿轮齿数（系统设置图标）。8 颗齿沿圆周均布，每 45° 一颗。
const int kGearTeeth = 8;

// 画一个齿轮：中心圆 + kGearTeeth 颗沿半径方向向外的短"齿"（圆头描边后
// 形似轮齿）。cx/cy 为齿轮中心，rInner 为齿根半径，rOuter 为齿尖半径。
void DrawGear(Gdiplus::Graphics& g, Gdiplus::Pen& pen,
              Gdiplus::REAL cx, Gdiplus::REAL cy,
              Gdiplus::REAL rInner, Gdiplus::REAL rOuter, Gdiplus::REAL rHub)
{
    // 轮毂中心圆。
    g.DrawEllipse(&pen, cx - rHub, cy - rHub, rHub * 2.0f, rHub * 2.0f);

    // 8 颗轮齿：每 45° 画一条沿半径方向的短线段（齿根 → 齿尖）。
    const double kPi = 3.14159265358979323846;
    for (int i = 0; i < kGearTeeth; ++i)
    {
        double ang = (2.0 * kPi / kGearTeeth) * i;
        Gdiplus::REAL c = (Gdiplus::REAL)cos(ang);
        Gdiplus::REAL s = (Gdiplus::REAL)sin(ang);
        g.DrawLine(&pen,
                   cx + rInner * c, cy + rInner * s,
                   cx + rOuter * c, cy + rOuter * s);
    }
}

// 画一个朝右的喇叭轮廓（左侧矩形音箱 + 右侧梯形号角），供"声音 / 静音"图标共用。
// 右侧的声波弧或叉号由各自的 case 另画。
void AddSpeaker(Gdiplus::GraphicsPath& path)
{
    path.AddLine(3.0f,  9.0f,  8.0f,  9.0f);    // 音箱顶边
    path.AddLine(8.0f,  9.0f,  13.0f, 4.0f);    // 号角上斜到尖
    path.AddLine(13.0f, 4.0f,  13.0f, 20.0f);   // 号角外缘（竖）
    path.AddLine(13.0f, 20.0f, 8.0f,  15.0f);   // 号角下斜回音箱
    path.AddLine(8.0f,  15.0f, 3.0f,  15.0f);   // 音箱底边
    path.CloseFigure();                          // 回到起点闭合
}

} // anonymous namespace

void DrawLineIcon(Gdiplus::Graphics& g, int kind,
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

    switch (kind)
    {
    case kIconHistory:
        // 时钟：圆 r8 + 指针折线 (12,8)-(12,12)-(15,14)。
        g.DrawEllipse(&pen, 4.0f, 4.0f, 16.0f, 16.0f);
        {
            Gdiplus::GraphicsPath hand;
            hand.AddLine(12.0f, 8.0f, 12.0f, 12.0f);
            hand.AddLine(12.0f, 12.0f, 15.0f, 14.0f);
            g.DrawPath(&pen, &hand);
        }
        break;

    case kIconUser:
        // 人形：头部圆 r4 + 肩部两段贝塞尔。
        g.DrawEllipse(&pen, 8.0f, 4.0f, 8.0f, 8.0f);
        {
            Gdiplus::GraphicsPath body;
            body.AddBezier(4.0f, 20.0f, 5.0f, 16.0f, 8.0f, 14.0f, 12.0f, 14.0f);
            body.AddBezier(12.0f, 14.0f, 16.0f, 14.0f, 19.0f, 16.0f, 20.0f, 20.0f);
            g.DrawPath(&pen, &body);
        }
        break;

    case kIconGroup:
        // 群组：两个头部圆 + 两段肩部曲线。
        g.DrawEllipse(&pen, 5.4f,  6.4f, 5.2f, 5.2f);
        g.DrawEllipse(&pen, 13.4f, 6.4f, 5.2f, 5.2f);
        {
            Gdiplus::GraphicsPath body1;
            body1.AddBezier(3.0f, 19.0f, 3.5f, 16.6f, 5.5f, 15.4f, 8.0f, 15.4f);
            body1.AddBezier(8.0f, 15.4f, 10.5f, 15.4f, 12.5f, 16.6f, 13.0f, 19.0f);
            g.DrawPath(&pen, &body1);

            Gdiplus::GraphicsPath body2;
            body2.AddBezier(14.0f, 19.0f, 14.4f, 16.6f, 16.5f, 15.4f, 19.0f, 15.4f);
            g.DrawPath(&pen, &body2);
        }
        break;

    case kIconList:
        // 三横线（汉堡菜单）。
        g.DrawLine(&pen, 4.0f, 7.0f,  20.0f, 7.0f);
        g.DrawLine(&pen, 4.0f, 12.0f, 20.0f, 12.0f);
        g.DrawLine(&pen, 4.0f, 17.0f, 20.0f, 17.0f);
        break;

    case kIconPlus:
        // 加号。
        g.DrawLine(&pen, 12.0f, 5.0f,  12.0f, 19.0f);
        g.DrawLine(&pen, 5.0f,  12.0f, 19.0f, 12.0f);
        break;

    case kIconSearch:
        // 放大镜：圆 r6 + 手柄线。
        g.DrawEllipse(&pen, 5.0f, 5.0f, 12.0f, 12.0f);
        g.DrawLine(&pen, 20.0f, 20.0f, 16.0f, 16.0f);
        break;

    case kIconChevron:
        // 右尖括号 (9,6)-(15,12)-(9,18)。
        {
            Gdiplus::GraphicsPath chev;
            chev.AddLine(9.0f, 6.0f, 15.0f, 12.0f);
            chev.AddLine(15.0f, 12.0f, 9.0f, 18.0f);
            g.DrawPath(&pen, &chev);
        }
        break;

    case kIconLock:
        // 挂锁：顶部弧（shackle）+ 圆角锁体 + 钥匙孔竖线。
        g.DrawArc(&pen, 7.0f, 3.5f, 10.0f, 12.0f, 180.0f, 180.0f);   // 顶部半圆弧
        g.DrawLine(&pen, 7.0f, 9.5f, 7.0f, 11.0f);                   // 左挂钩落到锁体
        g.DrawLine(&pen, 17.0f, 9.5f, 17.0f, 11.0f);                 // 右挂钩落到锁体
        {
            Gdiplus::GraphicsPath body;
            AddRoundRect(body, 5.0f, 11.0f, 14.0f, 9.0f, 2.5f);      // 锁体
            g.DrawPath(&pen, &body);
        }
        g.DrawLine(&pen, 12.0f, 14.0f, 12.0f, 17.0f);               // 钥匙孔竖线
        break;

    case kIconSend:
        // 纸飞机（发送）：外轮廓多边形 + 内折线。
        {
            Gdiplus::GraphicsPath plane;
            plane.AddLine(2.0f, 9.0f, 22.0f, 2.0f);                  // 左下 → 右上尖
            plane.AddLine(22.0f, 2.0f, 15.0f, 22.0f);               // 右上尖 → 右下
            plane.AddLine(15.0f, 22.0f, 11.0f, 13.0f);              // 右下 → 内折点
            plane.AddLine(11.0f, 13.0f, 2.0f, 9.0f);                // 内折点 → 左下（闭合）
            g.DrawPath(&pen, &plane);
            g.DrawLine(&pen, 22.0f, 2.0f, 11.0f, 13.0f);            // 机身内折线
        }
        break;

    case kIconBell:
        // 铃铛：顶钮 + 钟体两侧曲线 + 底部横杆 + 底部小摆锤弧。
        g.DrawLine(&pen, 12.0f, 5.0f, 12.0f, 7.5f);                 // 顶部小钮
        {
            Gdiplus::GraphicsPath bell;
            bell.AddBezier(5.5f, 17.0f, 6.0f, 12.0f, 7.0f, 8.5f, 12.0f, 8.0f);   // 左侧
            bell.AddBezier(12.0f, 8.0f, 17.0f, 8.5f, 18.0f, 12.0f, 18.5f, 17.0f); // 右侧
            g.DrawPath(&pen, &bell);
        }
        g.DrawLine(&pen, 4.0f, 17.0f, 20.0f, 17.0f);               // 底部横杆
        g.DrawArc(&pen, 10.0f, 17.0f, 4.0f, 4.0f, 0.0f, 180.0f);   // 底部小摆锤
        break;

    case kIconGear:
        // 齿轮（系统设置）：中心圆 + 8 颗轮齿。
        DrawGear(g, pen, 12.0f, 12.0f, 6.5f, 9.5f, 3.2f);
        break;

    case kIconGlobe:
        // 地球（网络设置）：外圆 + 经线（竖椭圆）+ 赤道横线。
        g.DrawEllipse(&pen, 4.0f, 4.0f, 16.0f, 16.0f);             // 地球外圆
        g.DrawEllipse(&pen, 9.0f, 4.0f, 6.0f, 16.0f);             // 经线（竖向窄椭圆）
        g.DrawLine(&pen, 4.0f, 12.0f, 20.0f, 12.0f);             // 赤道
        break;

    case kIconInfo:
        // 圆圈 i（关于）：外圆 + 顶部圆点 + 竖干。
        g.DrawEllipse(&pen, 4.0f, 4.0f, 16.0f, 16.0f);            // 外圆
        g.DrawLine(&pen, 12.0f, 7.8f, 12.0f, 8.0f);              // 顶部圆点（零长线 + 圆头 = 实心点）
        g.DrawLine(&pen, 12.0f, 11.0f, 12.0f, 16.5f);           // 竖干
        break;

    case kIconOnline:
        // 在线：实心圆点（presence 圆点）。用与描边同色的实心画刷填充。
        {
            Gdiplus::SolidBrush brush(color);
            g.FillEllipse(&brush, 7.0f, 7.0f, 10.0f, 10.0f);
        }
        break;

    case kIconStatusCurrent:
        // 当前在线状态：外环 + 内实心点（单选按钮选中的样子）。外环直径取 15，与
        // kIconOnline 的实心点(直径 10)视觉重量相当，不会因为多画一圈就显得突兀；
        // 内点缩到直径 6，与外环之间留出一圈可见空隙，缩到 16 像素后仍分得清。
        {
            const Gdiplus::REAL kRingXY   = 4.5f;    // 外环外接方框左上角（viewBox 24 坐标）
            const Gdiplus::REAL kRingSize = 15.0f;   // 外环直径（viewBox 24 单位）
            const Gdiplus::REAL kCoreXY   = 9.0f;    // 内实心点外接方框左上角
            const Gdiplus::REAL kCoreSize = 6.0f;    // 内实心点直径

            g.DrawEllipse(&pen, kRingXY, kRingXY, kRingSize, kRingSize);

            Gdiplus::SolidBrush brush(color);
            g.FillEllipse(&brush, kCoreXY, kCoreXY, kCoreSize, kCoreSize);
        }
        break;

    case kIconOffline:
        // 离线：空心圆环（与在线实心点形成"●在线 / ○离线"对照）。
        g.DrawEllipse(&pen, 7.0f, 7.0f, 10.0f, 10.0f);
        break;

    case kIconPanel:
        // 主面板（窗口）：圆角矩形窗体 + 顶部标题栏分隔横线。
        {
            Gdiplus::GraphicsPath body;
            AddRoundRect(body, 4.0f, 5.0f, 16.0f, 14.0f, 2.0f);
            g.DrawPath(&pen, &body);
        }
        g.DrawLine(&pen, 4.0f, 9.0f, 20.0f, 9.0f);              // 标题栏分隔线
        break;

    case kIconExit:
        // 退出（登出）：左侧门框（开口朝右的 C）+ 向右指出的箭头。
        {
            Gdiplus::GraphicsPath frame;
            frame.AddLine(11.0f, 4.0f, 5.0f, 4.0f);             // 门框顶边
            frame.AddLine(5.0f, 4.0f, 5.0f, 20.0f);            // 门框左边
            frame.AddLine(5.0f, 20.0f, 11.0f, 20.0f);          // 门框底边
            g.DrawPath(&pen, &frame);
            g.DrawLine(&pen, 10.0f, 12.0f, 21.0f, 12.0f);      // 箭杆
            Gdiplus::GraphicsPath head;
            head.AddLine(17.0f, 8.0f, 21.0f, 12.0f);           // 箭头上斜
            head.AddLine(21.0f, 12.0f, 17.0f, 16.0f);          // 箭头下斜
            g.DrawPath(&pen, &head);
        }
        break;

    case kIconSound:
        // 声音开着：喇叭 + 右侧两道声波弧（朝右外凸）。
        {
            Gdiplus::GraphicsPath spk;
            AddSpeaker(spk);
            g.DrawPath(&pen, &spk);
            g.DrawArc(&pen, 14.0f, 8.0f, 4.0f,  8.0f,  -60.0f, 120.0f);   // 内圈声波
            g.DrawArc(&pen, 16.0f, 5.0f, 5.0f, 14.0f,  -60.0f, 120.0f);   // 外圈声波
        }
        break;

    case kIconMute:
        // 已静音：喇叭 + 右侧打叉。
        {
            Gdiplus::GraphicsPath spk;
            AddSpeaker(spk);
            g.DrawPath(&pen, &spk);
            g.DrawLine(&pen, 15.5f, 8.5f, 21.0f, 15.5f);       // 叉（左上 → 右下）
            g.DrawLine(&pen, 21.0f, 8.5f, 15.5f, 15.5f);       // 叉（右上 → 左下）
        }
        break;

    case kIconLogout:
        // 登出：左侧半身人形（头 + 肩，比 kIconUser 窄，给右边的箭头让位）+ 向右箭头。
        // 与 kIconExit 的"门框 + 箭头"形成区分：这里强调"这个人离开"，那里是"离开这道门"。
        {
            g.DrawEllipse(&pen, 2.5f, 4.0f, 7.0f, 7.0f);           // 头部
            Gdiplus::GraphicsPath body;
            body.AddBezier(1.0f, 20.0f, 1.8f, 16.0f, 3.5f, 14.0f, 6.0f, 14.0f);    // 左肩
            body.AddBezier(6.0f, 14.0f, 8.5f, 14.0f, 10.2f, 16.0f, 11.0f, 20.0f);  // 右肩
            g.DrawPath(&pen, &body);

            g.DrawLine(&pen, 13.5f, 12.0f, 21.5f, 12.0f);          // 箭杆
            Gdiplus::GraphicsPath head;
            head.AddLine(18.0f, 8.5f, 21.5f, 12.0f);               // 箭头上斜
            head.AddLine(21.5f, 12.0f, 18.0f, 15.5f);              // 箭头下斜
            g.DrawPath(&pen, &head);
        }
        break;

    default:
        break;
    }

    g.Restore(st);
}

} // namespace balloonmain
