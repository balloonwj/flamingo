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

// ---- 「我的收藏」书签徽章（kIconBookmark）----
// 书签的外形是五边形：左上 → 右上 → 右下 → 底边中间的缺口 → 左下。
// 以下坐标均为 viewBox 24 单位。宽 12、高 19，与星标（外接直径 20）视觉重量相当，
// 窄一些是书签本身的比例。
const Gdiplus::REAL kBookmarkLeftX   = 6.0f;    // 左边线 x
const Gdiplus::REAL kBookmarkRightX  = 18.0f;   // 右边线 x
const Gdiplus::REAL kBookmarkTopY    = 2.5f;    // 上沿 y
const Gdiplus::REAL kBookmarkBottomY = 21.5f;   // 左右两个下角的 y
const Gdiplus::REAL kBookmarkNotchY  = 16.0f;   // 底边 V 形缺口顶点的 y（在中线上）
// 书签五边形的顶点数。
const int kBookmarkPointCount = 5;

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

    case kIconBookmark:
        // 收藏：实心书签。顺时针依次为左上、右上、右下、底边中间的缺口顶点、左下。
        {
            const Gdiplus::REAL centerX = (kBookmarkLeftX + kBookmarkRightX) / 2.0f;
            Gdiplus::PointF pts[kBookmarkPointCount] =
            {
                Gdiplus::PointF(kBookmarkLeftX,  kBookmarkTopY),
                Gdiplus::PointF(kBookmarkRightX, kBookmarkTopY),
                Gdiplus::PointF(kBookmarkRightX, kBookmarkBottomY),
                Gdiplus::PointF(centerX,         kBookmarkNotchY),
                Gdiplus::PointF(kBookmarkLeftX,  kBookmarkBottomY),
            };

            Gdiplus::SolidBrush brush(color);
            g.FillPolygon(&brush, pts, kBookmarkPointCount);
        }
        break;

    case kIconShare:
        // 文件共享：文件夹外框（左上带页签）+ 夹内三个圆点用两条线相连
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

            g.DrawLine(&pen, kLeftX, kMidY, kRightX, kUpperY);
            g.DrawLine(&pen, kLeftX, kMidY, kRightX, kLowerY);
            Gdiplus::SolidBrush brush(color);
            g.FillEllipse(&brush, kLeftX - kDotR, kMidY - kDotR, kDotR * 2.0f, kDotR * 2.0f);
            g.FillEllipse(&brush, kRightX - kDotR, kUpperY - kDotR, kDotR * 2.0f, kDotR * 2.0f);
            g.FillEllipse(&brush, kRightX - kDotR, kLowerY - kDotR, kDotR * 2.0f, kDotR * 2.0f);
        }
        break;

    case kIconBellSlash:
        // 接收消息但不提醒：与 kIconBell 同一个铃铛，再加一道左上到右下的斜线
        g.DrawLine(&pen, 12.0f, 5.0f, 12.0f, 7.5f);                 // 顶部小钮
        {
            Gdiplus::GraphicsPath bell;
            bell.AddBezier(5.5f, 17.0f, 6.0f, 12.0f, 7.0f, 8.5f, 12.0f, 8.0f);   // 左侧
            bell.AddBezier(12.0f, 8.0f, 17.0f, 8.5f, 18.0f, 12.0f, 18.5f, 17.0f); // 右侧
            g.DrawPath(&pen, &bell);
        }
        g.DrawLine(&pen, 4.0f, 17.0f, 20.0f, 17.0f);               // 底部横杆
        g.DrawArc(&pen, 10.0f, 17.0f, 4.0f, 4.0f, 0.0f, 180.0f);   // 底部小摆锤
        g.DrawLine(&pen, 4.0f, 4.0f, 20.0f, 20.0f);                // 斜线
        break;

    case kIconDrawer:
        // 收进消息盒子：两格抽屉柜。外框 + 中间一道隔板 + 上下两格各一个横向拉手
        g.DrawRectangle(&pen, 4.0f, 3.5f, 16.0f, 17.0f);           // 外框
        g.DrawLine(&pen, 4.0f, 12.0f, 20.0f, 12.0f);               // 中间隔板
        g.DrawLine(&pen, 10.0f, 7.8f, 14.0f, 7.8f);                // 上格拉手
        g.DrawLine(&pen, 10.0f, 16.2f, 14.0f, 16.2f);              // 下格拉手
        break;

    case kIconCalendar:
        // 备忘与提醒：日历。外框 + 顶部两个挂环 + 表头横线 + 两个日期点（零长线 + 圆头 = 实心点）
        g.DrawRectangle(&pen, 4.0f, 6.0f, 16.0f, 14.0f);           // 外框
        g.DrawLine(&pen, 8.5f, 3.5f, 8.5f, 8.0f);                  // 左挂环
        g.DrawLine(&pen, 15.5f, 3.5f, 15.5f, 8.0f);                // 右挂环
        g.DrawLine(&pen, 4.0f, 11.0f, 20.0f, 11.0f);               // 表头横线
        g.DrawLine(&pen, 9.0f, 15.5f, 9.0f, 15.7f);                // 日期点 1
        g.DrawLine(&pen, 15.0f, 15.5f, 15.0f, 15.7f);              // 日期点 2
        break;

    case kIconPhone:
        // 发起语音通话：电话听筒。一段圆弧作手柄，两端各一个实心小圆作听筒与话筒（与聊天窗头部的语音按钮同一造型）
        {
            const Gdiplus::REAL kCx = 12.0f;          // 手柄圆弧圆心 x
            const Gdiplus::REAL kCy = 12.0f;          // 手柄圆弧圆心 y
            const Gdiplus::REAL kR = 7.0f;            // 手柄圆弧半径
            const Gdiplus::REAL kStart = 130.0f;      // 圆弧起始角（度）
            const Gdiplus::REAL kSweep = 150.0f;      // 圆弧扫过角（度）
            const Gdiplus::REAL kEarR = 2.6f;         // 两端圆头半径
            const double kDegToRad = 3.14159265358979 / 180.0;
            g.DrawArc(&pen, kCx - kR, kCy - kR, kR * 2.0f, kR * 2.0f, kStart, kSweep);
            Gdiplus::SolidBrush ear(color);
            for (int end = 0; end < 2; ++end)
            {
                const double ang = ((end == 0) ? kStart : (kStart + kSweep)) * kDegToRad;
                const Gdiplus::REAL ex = kCx + kR * (Gdiplus::REAL)cos(ang);
                const Gdiplus::REAL ey = kCy + kR * (Gdiplus::REAL)sin(ang);
                g.FillEllipse(&ear, ex - kEarR, ey - kEarR, kEarR * 2.0f, kEarR * 2.0f);
            }
        }
        break;

    case kIconVideoCall:
        // 发起视频通话：摄像机。左边机身方框，右边镜头三角（与聊天窗头部的视频按钮同一造型）
        {
            g.DrawRectangle(&pen, 3.0f, 7.0f, 12.0f, 10.0f);   // 机身
            Gdiplus::GraphicsPath lens;
            lens.AddLine(15.0f, 10.5f, 21.0f, 7.5f);
            lens.AddLine(21.0f, 7.5f, 21.0f, 16.5f);
            lens.AddLine(21.0f, 16.5f, 15.0f, 13.5f);
            lens.CloseFigure();
            g.DrawPath(&pen, &lens);                            // 镜头
        }
        break;

    case kIconBlock:
        // 屏蔽：禁止符。圆圈 + 左上到右下一道斜线（斜线端点落在圆周以内，与圆圈留一点距离）
        g.DrawEllipse(&pen, 4.0f, 4.0f, 16.0f, 16.0f);             // 圆圈
        g.DrawLine(&pen, 6.4f, 6.4f, 17.6f, 17.6f);                // 斜线
        break;

    case kIconRecord:
        // 屏幕录制：场记板。上方是带两道斜纹的打板，下方板身里一个实心圆点表示「录制」（2026-10-06 用户选定，之前是
        // 「圆环 + 实心圆点」）。与 ChatIcons.cpp 的 kChatIconRecord 同一造型，两处的几何常量须保持一致（单元测试 RIC1）。
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

            Gdiplus::GraphicsPath body;
            AddRoundRect(body, kLeft, kBodyTop, kRight - kLeft, kBodyBottom - kBodyTop, kCorner);
            g.DrawPath(&pen, &body);

            // 打板：左边、顶边、右边三条线，底边与板身顶边共用
            Gdiplus::GraphicsPath clapper;
            clapper.AddLine(kLeft, kBodyTop, kLeft, kClapTop);
            clapper.AddLine(kLeft, kClapTop, kRight, kClapTop);
            clapper.AddLine(kRight, kClapTop, kRight, kBodyTop);
            g.DrawPath(&pen, &clapper);
            g.DrawLine(&pen, kStripe1X, kClapTop, kStripe1X - kStripeSlant, kBodyTop);
            g.DrawLine(&pen, kStripe2X, kClapTop, kStripe2X - kStripeSlant, kBodyTop);

            Gdiplus::SolidBrush dot(color);
            g.FillEllipse(&dot, kDotX - kDotR, kDotY - kDotR, kDotR * 2.0f, kDotR * 2.0f);
        }
        break;

    case kIconScreenShare:
        // 发起屏幕共享：显示器（屏幕方框 + 支架 + 底座）+ 屏幕里向上的箭头（「把屏幕分享出去」）
        {
            g.DrawRectangle(&pen, 2.5f, 3.5f, 19.0f, 13.0f);       // 屏幕
            g.DrawLine(&pen, 12.0f, 16.5f, 12.0f, 20.0f);          // 支架
            g.DrawLine(&pen, 8.0f, 20.5f, 16.0f, 20.5f);           // 底座
            g.DrawLine(&pen, 12.0f, 13.5f, 12.0f, 7.0f);           // 箭杆
            Gdiplus::GraphicsPath head;
            head.AddLine(9.0f, 10.0f, 12.0f, 7.0f);                // 箭头左斜
            head.AddLine(12.0f, 7.0f, 15.0f, 10.0f);               // 箭头右斜
            g.DrawPath(&pen, &head);
        }
        break;

    case kIconRecordList:
        // 录制管理：录像文件。方框表示画面，中间一个实心播放三角（与「屏幕录制」的场记板区分）
        {
            g.DrawRectangle(&pen, 3.5f, 5.5f, 17.0f, 13.0f);       // 画面方框
            const Gdiplus::PointF play[3] = {
                Gdiplus::PointF(10.0f, 9.0f),                      // 三角左上
                Gdiplus::PointF(10.0f, 15.0f),                     // 三角左下
                Gdiplus::PointF(15.5f, 12.0f)                      // 三角右尖
            };
            Gdiplus::SolidBrush fill(color);
            g.FillPolygon(&fill, play, 3);
        }
        break;

    case kIconOcr:
        // 图片文字识别：四个角的取景框（表示扫描）+ 中间一个「T」（表示文字），与看图窗、截图的识别按钮同一造型
        {
            const Gdiplus::REAL kLo  = 3.5f;    // 取景框左、上边
            const Gdiplus::REAL kHi  = 20.5f;   // 取景框右、下边
            const Gdiplus::REAL kArm = 5.0f;    // 每个角的角臂长
            g.DrawLine(&pen, kLo, kLo + kArm, kLo, kLo);           // 左上角竖臂
            g.DrawLine(&pen, kLo, kLo, kLo + kArm, kLo);           // 左上角横臂
            g.DrawLine(&pen, kHi - kArm, kLo, kHi, kLo);           // 右上角横臂
            g.DrawLine(&pen, kHi, kLo, kHi, kLo + kArm);           // 右上角竖臂
            g.DrawLine(&pen, kHi, kHi - kArm, kHi, kHi);           // 右下角竖臂
            g.DrawLine(&pen, kHi, kHi, kHi - kArm, kHi);           // 右下角横臂
            g.DrawLine(&pen, kLo + kArm, kHi, kLo, kHi);           // 左下角横臂
            g.DrawLine(&pen, kLo, kHi, kLo, kHi - kArm);           // 左下角竖臂
            g.DrawLine(&pen, 8.0f, 8.5f, 16.0f, 8.5f);             // 「T」横笔
            g.DrawLine(&pen, 12.0f, 8.5f, 12.0f, 16.5f);           // 「T」竖笔
        }
        break;

    case kIconMarkdown:
        // Markdown 编辑器：圆角框 + 框内「M」与向下的箭头，与聊天窗工具栏的 Markdown 编辑器按钮同一造型
        {
            const Gdiplus::REAL kLeft   = 2.0f;    // 圆角框左边
            const Gdiplus::REAL kTop    = 5.0f;    // 圆角框上边
            const Gdiplus::REAL kRight  = 22.0f;   // 圆角框右边
            const Gdiplus::REAL kBottom = 19.0f;   // 圆角框下边
            const Gdiplus::REAL kDiam   = 5.0f;    // 圆角直径
            Gdiplus::GraphicsPath frame;
            frame.AddArc(kLeft, kTop, kDiam, kDiam, 180.0f, 90.0f);                      // 左上角
            frame.AddArc(kRight - kDiam, kTop, kDiam, kDiam, 270.0f, 90.0f);             // 右上角
            frame.AddArc(kRight - kDiam, kBottom - kDiam, kDiam, kDiam, 0.0f, 90.0f);    // 右下角
            frame.AddArc(kLeft, kBottom - kDiam, kDiam, kDiam, 90.0f, 90.0f);            // 左下角
            frame.CloseFigure();
            g.DrawPath(&pen, &frame);
            const Gdiplus::PointF m[5] = {
                Gdiplus::PointF(5.5f, 15.0f),                      // 「M」左下
                Gdiplus::PointF(5.5f, 9.0f),                       // 「M」左上
                Gdiplus::PointF(8.5f, 12.0f),                      // 「M」中间的谷底
                Gdiplus::PointF(11.5f, 9.0f),                      // 「M」右上
                Gdiplus::PointF(11.5f, 15.0f)                      // 「M」右下
            };
            g.DrawLines(&pen, m, 5);
            g.DrawLine(&pen, 17.0f, 9.0f, 17.0f, 15.0f);           // 箭杆
            const Gdiplus::PointF head[3] = {
                Gdiplus::PointF(14.5f, 12.5f),                     // 箭头左翼
                Gdiplus::PointF(17.0f, 15.0f),                     // 箭头尖
                Gdiplus::PointF(19.5f, 12.5f)                      // 箭头右翼
            };
            g.DrawLines(&pen, head, 3);
        }
        break;

    case kIconKey:
        // 修改密码：钥匙。左侧圆形钥匙柄，柄右缘引出水平钥匙杆，杆的右段向下两个齿（远端的齿更长）
        {
            const Gdiplus::REAL kBowLeft  = 2.5f;    // 钥匙柄外接圆左边
            const Gdiplus::REAL kBowTop   = 7.5f;    // 钥匙柄外接圆上边
            const Gdiplus::REAL kBowDiam  = 9.0f;    // 钥匙柄直径
            const Gdiplus::REAL kShaftY   = 12.0f;   // 钥匙杆所在的水平线（与钥匙柄圆心同高）
            const Gdiplus::REAL kShaftEnd = 21.5f;   // 钥匙杆右端
            g.DrawEllipse(&pen, kBowLeft, kBowTop, kBowDiam, kBowDiam);
            g.DrawLine(&pen, kBowLeft + kBowDiam, kShaftY, kShaftEnd, kShaftY);   // 钥匙杆
            g.DrawLine(&pen, 17.0f, kShaftY, 17.0f, 15.0f);                       // 近端的短齿
            g.DrawLine(&pen, kShaftEnd, kShaftY, kShaftEnd, 16.5f);               // 远端的长齿
        }
        break;

    case kIconBot:
        // 机器人：圆角方头（外框）+ 两只眼睛（零长线 + 圆头 = 实心点）+ 头顶一根天线与顶端的圆点
        {
            Gdiplus::GraphicsPath head;
            const Gdiplus::REAL kHeadL = 4.0f;    // 头部左边
            const Gdiplus::REAL kHeadT = 8.0f;    // 头部上边
            const Gdiplus::REAL kHeadW = 16.0f;   // 头部宽
            const Gdiplus::REAL kHeadH = 12.0f;   // 头部高
            const Gdiplus::REAL kR = 3.0f;        // 圆角半径
            head.AddArc(kHeadL, kHeadT, kR * 2.0f, kR * 2.0f, 180.0f, 90.0f);
            head.AddArc(kHeadL + kHeadW - kR * 2.0f, kHeadT, kR * 2.0f, kR * 2.0f, 270.0f, 90.0f);
            head.AddArc(kHeadL + kHeadW - kR * 2.0f, kHeadT + kHeadH - kR * 2.0f, kR * 2.0f, kR * 2.0f, 0.0f, 90.0f);
            head.AddArc(kHeadL, kHeadT + kHeadH - kR * 2.0f, kR * 2.0f, kR * 2.0f, 90.0f, 90.0f);
            head.CloseFigure();
            g.DrawPath(&pen, &head);
            g.DrawLine(&pen, 9.0f, 13.5f, 9.0f, 13.7f);       // 左眼
            g.DrawLine(&pen, 15.0f, 13.5f, 15.0f, 13.7f);     // 右眼
            g.DrawLine(&pen, 12.0f, 4.5f, 12.0f, 8.0f);       // 天线
            g.DrawLine(&pen, 12.0f, 3.0f, 12.0f, 3.2f);       // 天线顶端的圆点
        }
        break;

    case kIconScreenshot:
        // 截图：剪刀。左侧上下两个圆环（握柄），两条刀刃从圆环向右交叉展开；与聊天窗工具栏的截图按钮同一造型
        {
            const Gdiplus::REAL kRingLeft  = 4.6f;    // 两个圆环外接正方形的左边
            const Gdiplus::REAL kRingDiam  = 4.8f;    // 圆环直径
            const Gdiplus::REAL kUpperTop  = 4.6f;    // 上圆环外接正方形的上边
            const Gdiplus::REAL kLowerTop  = 14.6f;   // 下圆环外接正方形的上边
            const Gdiplus::REAL kBladeFrom = 9.0f;    // 刀刃起点的横坐标（圆环右下 / 右上）
            const Gdiplus::REAL kBladeTo   = 21.0f;   // 刀刃终点的横坐标
            g.DrawEllipse(&pen, kRingLeft, kUpperTop, kRingDiam, kRingDiam);
            g.DrawEllipse(&pen, kRingLeft, kLowerTop, kRingDiam, kRingDiam);
            g.DrawLine(&pen, kBladeFrom, 9.0f, kBladeTo, 19.0f);    // 从上圆环引向右下的刀刃
            g.DrawLine(&pen, kBladeFrom, 15.0f, kBladeTo, 5.0f);    // 从下圆环引向右上的刀刃
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

HBITMAP CreateCircleIconBitmap(int kind, int sizePx, COLORREF bgColor, COLORREF fgColor)
{
    if (sizePx <= 0)
    {
        return NULL;
    }

    BITMAPINFO bi = {};
    bi.bmiHeader.biSize        = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth       = sizePx;
    bi.bmiHeader.biHeight      = -sizePx;   // 自上而下
    bi.bmiHeader.biPlanes      = 1;
    bi.bmiHeader.biBitCount    = 32;
    bi.bmiHeader.biCompression = BI_RGB;

    void* bits = NULL;
    HBITMAP hBmp = ::CreateDIBSection(NULL, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
    if (hBmp == NULL || bits == NULL)
    {
        if (hBmp != NULL)
        {
            ::DeleteObject(hBmp);
        }
        return NULL;
    }
    ::ZeroMemory(bits, (size_t)sizePx * sizePx * 4);

    {
        // 直接在 DIB 的像素上画（预乘 alpha 格式），作用域结束时 GDI+ 对象先于返回析构
        Gdiplus::Bitmap canvas(sizePx, sizePx, sizePx * 4, PixelFormat32bppPARGB, static_cast<BYTE*>(bits));
        Gdiplus::Graphics g(&canvas);
        g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        g.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);

        const Gdiplus::REAL side = (Gdiplus::REAL)sizePx;
        Gdiplus::SolidBrush bg(Gdiplus::Color(255, GetRValue(bgColor), GetGValue(bgColor), GetBValue(bgColor)));
        g.FillEllipse(&bg, 0.0f, 0.0f, side, side);

        // 图标占圆底边长的一半，居中；描边按 viewBox 24 取 2，与列表里的徽章同一粗细
        const Gdiplus::REAL kIconRatio = 0.5f;
        const Gdiplus::REAL kStroke = 2.0f;
        const Gdiplus::REAL iconSize = side * kIconRatio;
        const Gdiplus::REAL offset = (side - iconSize) / 2.0f;
        DrawLineIcon(g, kind, offset, offset, iconSize,
                     Gdiplus::Color(255, GetRValue(fgColor), GetGValue(fgColor), GetBValue(fgColor)), kStroke);
    }
    return hBmp;
}

} // namespace balloonmain
