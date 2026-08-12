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

// ---- 在线状态徽标（presence badge）的几何常量 ----
//
// 全部以 viewBox 24 为基准，圆心固定在正中 (12,12)。数值不是随手定的：按 10px /
// 12px / 16px 三档实拍逐一调过（2026-08-12 效果图确认），指针与横杠细于 3.0 时
// 10px 下会糊成一团看不出字形，空心环内孔小于 5.6 时看不出是个环、只像个灰点。
const Gdiplus::REAL kBadgeCenter      = 12.0f;   // 徽标圆心（viewBox 24 的正中）
const Gdiplus::REAL kBadgeRadius      = 10.0f;   // 圆底 / 外环半径
const Gdiplus::REAL kBadgeGlyphWidth  = 3.0f;    // 白色字形（时钟指针 / 横杠）的线宽
const Gdiplus::REAL kBadgeHourEndY    = 7.4f;    // 时针端点 y —— 自圆心指向 12 点方向
const Gdiplus::REAL kBadgeMinuteEndX  = 15.8f;   // 分针端点 x —— 自圆心指向 3 点方向
const Gdiplus::REAL kBadgeBarLeftX    = 7.0f;    // 忙碌横杠左端 x
const Gdiplus::REAL kBadgeBarRightX   = 17.0f;   // 忙碌横杠右端 x
const Gdiplus::REAL kBadgeRingHoleR   = 5.6f;    // 离线空心环的内孔半径（真镂空）

// ---- 「特别关注」星标（kIconStar）----
// 顶点数：正五角星的轮廓是十边形，外圆五个尖、内圆五个凹。
const int kStarPointCount = 10;
// 外接半径（viewBox 24 单位）。取 10 与 kBadgeRadius 同值，星与状态徽标在同一
// 尺寸下画出来视觉重量相当，摆在同一行里不会一个显眼一个发虚。
const Gdiplus::REAL kStarOuterRadius = 10.0f;
// 内外半径比。0.382 是正五芒星的固有比例（由正五边形对角线的黄金分割得出），
// 偏大星显得胖、偏小显得瘦。
const Gdiplus::REAL kStarInnerRatio = 0.382f;
// 第一个顶点的方位角（度）。-90 即 12 点方向，保证尖角朝正上。
const double kStarStartAngleDeg = -90.0;
// 相邻顶点的夹角（度）：十个顶点均分一周，360 / 10。
const double kStarStepAngleDeg = 36.0;
// 圆周率。<cmath> 的 M_PI 不是标准里保证有的（MSVC 要先定义 _USE_MATH_DEFINES），
// 本文件只用这一处，直接给个常量省得为它动编译选项。
const double kPi = 3.14159265358979323846;

// 徽标上的字形恒为白色 —— 圆底已经带了状态色，字形再着色会两种颜色打架；
// 同类状态徽标的通行做法也是彩色圆底 + 纯白字形。
const Gdiplus::Color kBadgeGlyphColor(255, 255, 255, 255);

// 画徽标的实心圆底（在线 / 离开 / 忙碌三种字形共用这一层）。
void FillBadgeDisc(Gdiplus::Graphics& g, const Gdiplus::Color& color)
{
    Gdiplus::SolidBrush brush(color);
    g.FillEllipse(&brush,
                  kBadgeCenter - kBadgeRadius, kBadgeCenter - kBadgeRadius,
                  kBadgeRadius * 2.0f, kBadgeRadius * 2.0f);
}

// 在徽标圆底上画一条白色字形线段（圆头圆角，线宽 kBadgeGlyphWidth）。
void DrawBadgeGlyphLine(Gdiplus::Graphics& g,
                        Gdiplus::REAL x0, Gdiplus::REAL y0,
                        Gdiplus::REAL x1, Gdiplus::REAL y1)
{
    Gdiplus::Pen pen(kBadgeGlyphColor, kBadgeGlyphWidth);
    MakeStrokePen(pen);
    g.DrawLine(&pen, x0, y0, x1, y1);
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

    case kIconPresenceOnline:
        // 在线：一个实心圆，不加任何字形 —— "满"本身就是在线的意思，也与另外三种
        // 带缺口 / 带字形的徽标形成最强对比。
        FillBadgeDisc(g, color);
        break;

    case kIconPresenceAway:
        // 离开：实心圆底 + 白色时钟指针（时针指 12 点、分针指 3 点），取"离开一会儿、
        // 稍后回来"的意思。
        FillBadgeDisc(g, color);
        DrawBadgeGlyphLine(g, kBadgeCenter, kBadgeCenter, kBadgeCenter, kBadgeHourEndY);
        DrawBadgeGlyphLine(g, kBadgeCenter, kBadgeCenter, kBadgeMinuteEndX, kBadgeCenter);
        break;

    case kIconPresenceBusy:
        // 忙碌：实心圆底 + 一条居中白横杠，即通用的"禁止 / 请勿打扰"符号。
        FillBadgeDisc(g, color);
        DrawBadgeGlyphLine(g, kBadgeBarLeftX, kBadgeCenter, kBadgeBarRightX, kBadgeCenter);
        break;

    case kIconPresenceOffline:
        // 离线：空心圆环。内孔用 FillModeAlternate 真镂空（外圆与内圆同属一条 path，
        // 交替填充规则下内圆成为洞），底色直接透出来 —— 这样在白底、悬停底、深色胶囊
        // 上都成立，不必假设背景是白的。
        {
            Gdiplus::GraphicsPath ring(Gdiplus::FillModeAlternate);
            ring.AddEllipse(kBadgeCenter - kBadgeRadius, kBadgeCenter - kBadgeRadius,
                            kBadgeRadius * 2.0f, kBadgeRadius * 2.0f);
            ring.AddEllipse(kBadgeCenter - kBadgeRingHoleR, kBadgeCenter - kBadgeRingHoleR,
                            kBadgeRingHoleR * 2.0f, kBadgeRingHoleR * 2.0f);

            Gdiplus::SolidBrush brush(color);
            g.FillPath(&brush, &ring);
        }
        break;

    case kIconStar:
        // 特别关注：实心正五角星，尖角朝上。十个顶点交替落在外圆与内圆上，
        // 内外半径比取正五芒星的 0.382 —— 比这个值大星会显得胖、小则显得瘦。
        {
            Gdiplus::PointF pts[kStarPointCount];
            for (int i = 0; i < kStarPointCount; ++i)
            {
                // 自 12 点方向起，每个顶点转 36 度（360 / 10）。
                const double angle = (kStarStartAngleDeg + i * kStarStepAngleDeg)
                                     * kPi / 180.0;
                const Gdiplus::REAL r = ((i % 2) == 0)
                                        ? kStarOuterRadius
                                        : (kStarOuterRadius * kStarInnerRatio);
                pts[i].X = kBadgeCenter + r * (Gdiplus::REAL)cos(angle);
                pts[i].Y = kBadgeCenter + r * (Gdiplus::REAL)sin(angle);
            }

            Gdiplus::SolidBrush brush(color);
            g.FillPolygon(&brush, pts, kStarPointCount);
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
