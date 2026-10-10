/**
 *  GroupManageIcons.cpp
 *
 *  群管理导航图标的绘制实现。全部图标在 24×24 的 viewBox 内用 GDI+ 描边
 *  绘制，再等比缩放到调用方给定的尺寸。
 *
 *  本 .cpp 不 include Flamingo 的 stdafx.h —— 自带 ATL/WTL 前置依赖，
 *  工程里标 PrecompiledHeader = NotUsing。
 *
 *  balloonwj@qq.com   2026-06-03
 */

// ---- balloonui 头文件要求的 ATL/WTL 前置依赖（等价 NewChatDemo/stdafx.h）----
#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif
#define WINVER          0x0601
#define _WIN32_WINNT    0x0601
#define _WIN32_IE       0x0601
#define _RICHEDIT_VER   0x0300      // RichEdit 3.0，WTL 10 要求的下限（窗口类仍为 RichEdit20W）
#define _WTL_NO_CSTRING
#define _WTL_NO_WTYPES

#include <atlbase.h>
#include <atlstr.h>
#include <atltypes.h>
#include <atlapp.h>

extern CAppModule _Module;

#include <atlwin.h>
#include <atlcrack.h>
#include <atlmisc.h>

#include <gdiplus.h>

#include "GroupManageIcons.h"

namespace balloongroupmanage {

namespace {

// 图标设计基准画布边长（viewBox）。所有坐标按 24 单位书写，再缩放。
const Gdiplus::REAL kViewBox = 24.0f;

// 把 viewBox 坐标映射为设备坐标点。
//   vx / vy：viewBox 内坐标（0~24）；
//   scale：  缩放系数（= 目标尺寸 / 24）；
//   ox / oy：目标区域左上角设备坐标。
Gdiplus::PointF Vp(Gdiplus::REAL vx, Gdiplus::REAL vy,
                   Gdiplus::REAL scale, Gdiplus::REAL ox, Gdiplus::REAL oy)
{
    return Gdiplus::PointF(ox + vx * scale, oy + vy * scale);
}

// 描一条折线（viewBox 坐标数组）。
void StrokePolyline(Gdiplus::Graphics& g, Gdiplus::Pen& pen,
                    const Gdiplus::PointF* pts, int count)
{
    if (count >= 2)
    {
        g.DrawLines(&pen, pts, count);
    }
}

} // anonymous namespace

void DrawGmNavIcon(Gdiplus::Graphics& g, int kind,
                   Gdiplus::REAL x, Gdiplus::REAL y, Gdiplus::REAL size,
                   const Gdiplus::Color& color, Gdiplus::REAL strokeWidth)
{
    if (kind < kGmIconMembers || kind > kGmIconBot || size <= 0.0f)
    {
        return;
    }

    const Gdiplus::REAL scale = size / kViewBox;

    Gdiplus::Pen pen(color, strokeWidth * scale);
    pen.SetStartCap(Gdiplus::LineCapRound);
    pen.SetEndCap(Gdiplus::LineCapRound);
    pen.SetLineJoin(Gdiplus::LineJoinRound);

    Gdiplus::SolidBrush brush(color);

    switch (kind)
    {
    case kGmIconMembers:
        {
            // 前一个人：头 + 肩。
            g.DrawEllipse(&pen,
                Gdiplus::RectF(x + 5.5f * scale, y + 4.5f * scale,
                               6.0f * scale, 6.0f * scale));
            g.DrawArc(&pen,
                Gdiplus::RectF(x + 3.0f * scale, y + 13.0f * scale,
                               11.0f * scale, 11.0f * scale),
                180.0f, 180.0f);
            // 后一个人：头 + 右肩弧。
            g.DrawEllipse(&pen,
                Gdiplus::RectF(x + 13.0f * scale, y + 5.5f * scale,
                               5.0f * scale, 5.0f * scale));
            g.DrawArc(&pen,
                Gdiplus::RectF(x + 12.5f * scale, y + 13.5f * scale,
                               8.5f * scale, 10.0f * scale),
                250.0f, 110.0f);
        }
        break;

    case kGmIconApproval:
        {
            // 对勾。
            Gdiplus::PointF pts[3] =
            {
                Vp(5.0f,  12.5f, scale, x, y),
                Vp(10.0f, 17.5f, scale, x, y),
                Vp(19.0f, 7.0f,  scale, x, y),
            };
            StrokePolyline(g, pen, pts, 3);
        }
        break;

    case kGmIconMute:
        {
            // 禁止符：圆 + 左上到右下的斜杠。
            g.DrawEllipse(&pen,
                Gdiplus::RectF(x + 4.0f * scale, y + 4.0f * scale,
                               16.0f * scale, 16.0f * scale));
            g.DrawLine(&pen,
                Vp(6.7f, 6.7f, scale, x, y),
                Vp(17.3f, 17.3f, scale, x, y));
        }
        break;

    case kGmIconInfo:
        {
            // 圆圈 i：外圆 + 上方圆点 + 竖线。
            g.DrawEllipse(&pen,
                Gdiplus::RectF(x + 3.5f * scale, y + 3.5f * scale,
                               17.0f * scale, 17.0f * scale));
            const Gdiplus::REAL dotR = 1.0f * scale;
            g.FillEllipse(&brush,
                Gdiplus::RectF(x + 12.0f * scale - dotR, y + 7.5f * scale - dotR,
                               dotR * 2.0f, dotR * 2.0f));
            g.DrawLine(&pen,
                Vp(12.0f, 11.0f, scale, x, y),
                Vp(12.0f, 16.5f, scale, x, y));
        }
        break;

    case kGmIconLock:
        {
            // 挂锁：上方锁梁弧 + 下方锁体圆角矩形。
            g.DrawArc(&pen,
                Gdiplus::RectF(x + 8.0f * scale, y + 5.0f * scale,
                               8.0f * scale, 9.0f * scale),
                180.0f, 180.0f);
            Gdiplus::GraphicsPath body;
            body.AddArc(x + 6.0f * scale, y + 11.0f * scale,
                        4.0f * scale, 4.0f * scale, 180.0f, 90.0f);
            body.AddArc(x + 14.0f * scale, y + 11.0f * scale,
                        4.0f * scale, 4.0f * scale, 270.0f, 90.0f);
            body.AddArc(x + 14.0f * scale, y + 15.0f * scale,
                        4.0f * scale, 4.0f * scale, 0.0f, 90.0f);
            body.AddArc(x + 6.0f * scale, y + 15.0f * scale,
                        4.0f * scale, 4.0f * scale, 90.0f, 90.0f);
            body.CloseFigure();
            g.DrawPath(&pen, &body);
        }
        break;

    case kGmIconDanger:
        {
            // 垃圾桶：桶盖横线 + 顶部提手 + 桶身梯形 + 两条内竖线。
            g.DrawLine(&pen, Vp(5.0f, 7.0f, scale, x, y),
                             Vp(19.0f, 7.0f, scale, x, y));
            Gdiplus::PointF handle[4] =
            {
                Vp(9.5f, 7.0f,  scale, x, y),
                Vp(9.5f, 4.8f,  scale, x, y),
                Vp(14.5f, 4.8f, scale, x, y),
                Vp(14.5f, 7.0f, scale, x, y),
            };
            StrokePolyline(g, pen, handle, 4);
            Gdiplus::PointF bin[4] =
            {
                Vp(6.7f, 7.0f,   scale, x, y),
                Vp(7.6f, 19.8f,  scale, x, y),
                Vp(16.4f, 19.8f, scale, x, y),
                Vp(17.3f, 7.0f,  scale, x, y),
            };
            StrokePolyline(g, pen, bin, 4);
            g.DrawLine(&pen, Vp(10.2f, 10.0f, scale, x, y),
                             Vp(10.5f, 17.0f, scale, x, y));
            g.DrawLine(&pen, Vp(13.8f, 10.0f, scale, x, y),
                             Vp(13.5f, 17.0f, scale, x, y));
        }
        break;

    case kGmIconAnnounce:
        {
            // 铃铛：钟身（上窄下宽的钟形折线）+ 底部横梁 + 下方摆锤小弧。
            // 与「加群审批」的对勾一样只用折线描边，风格保持一致。
            Gdiplus::PointF body[6] =
            {
                Vp(6.0f,  16.6f, scale, x, y),   // 左下角（接底部横梁）
                Vp(7.3f,  14.6f, scale, x, y),   // 左肩内收
                Vp(7.3f,  10.6f, scale, x, y),   // 左侧竖直段（钟身腰部）
                Vp(16.7f, 10.6f, scale, x, y),   // 顶部经由右侧下行
                Vp(16.7f, 14.6f, scale, x, y),
                Vp(18.0f, 16.6f, scale, x, y),   // 右下角
            };
            StrokePolyline(g, pen, body, 6);

            // 钟顶：一段向上的弧，接在腰部两端之间
            g.DrawArc(&pen,
                      Gdiplus::RectF(Vp(7.3f, 5.6f, scale, x, y).X,
                                     Vp(7.3f, 5.6f, scale, x, y).Y,
                                     9.4f * scale, 10.0f * scale),
                      180.0f, 180.0f);

            // 底部横梁
            g.DrawLine(&pen, Vp(6.0f, 16.6f, scale, x, y),
                             Vp(18.0f, 16.6f, scale, x, y));

            // 摆锤：底部中央的一小段下垂弧
            g.DrawArc(&pen,
                      Gdiplus::RectF(Vp(10.4f, 16.6f, scale, x, y).X,
                                     Vp(10.4f, 16.6f, scale, x, y).Y,
                                     3.2f * scale, 3.2f * scale),
                      0.0f, 180.0f);
        }
        break;

    case kGmIconBot:
        {
            // 机器人：圆角方头（外框）+ 两只眼睛（零长线 + 圆头 = 实心点）+ 头顶一根天线与顶端的圆点，
            // 坐标与 MainPanelIcons.cpp 的 kIconBot 相同（viewBox 24）
            const Gdiplus::REAL kHeadL = 4.0f;    // 头部左边
            const Gdiplus::REAL kHeadT = 8.0f;    // 头部上边
            const Gdiplus::REAL kHeadW = 16.0f;   // 头部宽
            const Gdiplus::REAL kHeadH = 12.0f;   // 头部高
            const Gdiplus::REAL kR = 3.0f;        // 圆角半径
            const Gdiplus::REAL d = kR * 2.0f * scale;
            Gdiplus::GraphicsPath head;
            head.AddArc(x + kHeadL * scale, y + kHeadT * scale, d, d, 180.0f, 90.0f);
            head.AddArc(x + (kHeadL + kHeadW - kR * 2.0f) * scale, y + kHeadT * scale, d, d, 270.0f, 90.0f);
            head.AddArc(x + (kHeadL + kHeadW - kR * 2.0f) * scale, y + (kHeadT + kHeadH - kR * 2.0f) * scale, d, d,
                        0.0f, 90.0f);
            head.AddArc(x + kHeadL * scale, y + (kHeadT + kHeadH - kR * 2.0f) * scale, d, d, 90.0f, 90.0f);
            head.CloseFigure();
            g.DrawPath(&pen, &head);
            g.DrawLine(&pen, Vp(9.0f, 13.5f, scale, x, y), Vp(9.0f, 13.7f, scale, x, y));     // 左眼
            g.DrawLine(&pen, Vp(15.0f, 13.5f, scale, x, y), Vp(15.0f, 13.7f, scale, x, y));   // 右眼
            g.DrawLine(&pen, Vp(12.0f, 4.5f, scale, x, y), Vp(12.0f, 8.0f, scale, x, y));     // 天线
            g.DrawLine(&pen, Vp(12.0f, 3.0f, scale, x, y), Vp(12.0f, 3.2f, scale, x, y));     // 天线顶端的圆点
        }
        break;

    default:
        break;
    }
}

} // namespace balloongroupmanage
