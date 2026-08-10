/**
 *  flamingoAdmin —— 矢量图标绘制实现。
 *
 *  所有图标都在 0..24 的归一化坐标系里描述，DrawIcon 先把该坐标系映射到
 *  目标矩形再绘制，因此同一份图标数据在任意尺寸下都清晰。
 *
 *  balloonwj@qq.com   2026.05.21
 */
#include "stdafx.h"
#include "Widgets/IconRenderer.h"

#include <gdiplus.h>
#include <math.h>

// 让本文件可直接用 REAL（即 Gdiplus::REAL，等价于 float）。
using Gdiplus::REAL;

namespace flamingoadmin { namespace ui {

namespace {

// 图标归一化坐标系的边长。设计稿图标 viewBox 即 24。
const float kIconViewBox = 24.0f;

// 描边线宽（归一化坐标系下）。设计稿描边约 1.6~1.9，取 1.9 视觉饱满。
const float kStrokeWidth = 1.9f;

// 把 COLORREF 转 GDI+ 颜色。
Gdiplus::Color ToGdiColor(COLORREF c)
{
    return Gdiplus::Color(GetRValue(c), GetGValue(c), GetBValue(c));
}

// --- 各图标的绘制函数。入参 g 已把坐标系映射为 0..24，pen/brush 为描边色。---

void DrawDashboard(Gdiplus::Graphics& g, Gdiplus::Pen& pen)
{
    // 四宫格：左上大、右上扁、右下大、左下扁。
    pen.SetLineJoin(Gdiplus::LineJoinRound);
    g.DrawRectangle(&pen, 3.0f,  3.0f, 7.0f, 9.0f);
    g.DrawRectangle(&pen, 14.0f, 3.0f, 7.0f, 5.0f);
    g.DrawRectangle(&pen, 14.0f, 12.0f, 7.0f, 9.0f);
    g.DrawRectangle(&pen, 3.0f,  16.0f, 7.0f, 5.0f);
}

void DrawBuilding(Gdiplus::Graphics& g, Gdiplus::Pen& pen, Gdiplus::SolidBrush& brush)
{
    // 楼体外框 + 6 个窗洞 + 底部门洞。
    g.DrawRectangle(&pen, 4.0f, 3.0f, 16.0f, 18.0f);
    const float winX[2] = { 8.0f, 14.0f };
    const float winY[3] = { 7.0f, 11.0f, 15.0f };
    for (int r = 0; r < 3; ++r)
    {
        for (int c = 0; c < 2; ++c)
        {
            g.FillRectangle(&brush, winX[c], winY[r], 2.0f, 2.0f);
        }
    }
    g.DrawRectangle(&pen, 10.0f, 17.0f, 4.0f, 4.0f);   // 门
}

void DrawMegaphone(Gdiplus::Graphics& g, Gdiplus::Pen& pen)
{
    // 喇叭主体（梯形）+ 两道声波弧。
    pen.SetLineJoin(Gdiplus::LineJoinRound);
    Gdiplus::PointF horn[5] =
    {
        Gdiplus::PointF(3.0f, 9.0f),  Gdiplus::PointF(12.0f, 5.0f),
        Gdiplus::PointF(12.0f, 19.0f), Gdiplus::PointF(3.0f, 15.0f),
        Gdiplus::PointF(3.0f, 9.0f),
    };
    g.DrawLines(&pen, horn, 5);
    g.DrawArc(&pen, 13.0f, 7.0f, 7.0f, 10.0f, -70.0f, 140.0f);
    g.DrawArc(&pen, 15.0f, 4.0f, 9.0f, 16.0f, -65.0f, 130.0f);
}

void DrawShield(Gdiplus::Graphics& g, Gdiplus::Pen& pen)
{
    // 盾牌轮廓：上沿平、两侧收、底部尖。
    pen.SetLineJoin(Gdiplus::LineJoinRound);
    Gdiplus::PointF shield[6] =
    {
        Gdiplus::PointF(12.0f, 2.5f),  Gdiplus::PointF(20.0f, 5.5f),
        Gdiplus::PointF(20.0f, 12.0f), Gdiplus::PointF(12.0f, 21.5f),
        Gdiplus::PointF(4.0f,  12.0f), Gdiplus::PointF(4.0f,  5.5f),
    };
    g.DrawPolygon(&pen, shield, 6);
}

void DrawSettings(Gdiplus::Graphics& g, Gdiplus::Pen& pen)
{
    // 齿轮：外圈 8 个短齿（径向线）+ 圆环 + 圆心。
    const float cx = 12.0f, cy = 12.0f;
    const float rInner = 6.5f, rOuter = 9.5f;
    for (int i = 0; i < 8; ++i)
    {
        const double ang = i * (3.14159265 / 4.0);
        const float ca = static_cast<float>(cos(ang));
        const float sa = static_cast<float>(sin(ang));
        g.DrawLine(&pen,
            cx + ca * rInner, cy + sa * rInner,
            cx + ca * rOuter, cy + sa * rOuter);
    }
    g.DrawEllipse(&pen, cx - 6.0f, cy - 6.0f, 12.0f, 12.0f);
    g.DrawEllipse(&pen, cx - 2.6f, cy - 2.6f, 5.2f, 5.2f);
}

void DrawSearch(Gdiplus::Graphics& g, Gdiplus::Pen& pen)
{
    // 放大镜：镜片圆 + 手柄斜线。
    g.DrawEllipse(&pen, 4.0f, 4.0f, 12.0f, 12.0f);
    g.DrawLine(&pen, 15.0f, 15.0f, 20.5f, 20.5f);
}

void DrawBell(Gdiplus::Graphics& g, Gdiplus::Pen& pen)
{
    // 铃铛：钟体弧 + 底沿 + 下方小摆锤。
    pen.SetLineJoin(Gdiplus::LineJoinRound);
    Gdiplus::PointF body[5] =
    {
        Gdiplus::PointF(4.5f, 18.0f),  Gdiplus::PointF(6.5f, 14.5f),
        Gdiplus::PointF(6.5f, 9.5f),   Gdiplus::PointF(12.0f, 4.0f),
        Gdiplus::PointF(12.0f, 4.0f),
    };
    g.DrawLines(&pen, body, 4);
    Gdiplus::PointF body2[3] =
    {
        Gdiplus::PointF(12.0f, 4.0f), Gdiplus::PointF(17.5f, 9.5f),
        Gdiplus::PointF(17.5f, 14.5f),
    };
    g.DrawLines(&pen, body2, 3);
    g.DrawLine(&pen, 17.5f, 14.5f, 19.5f, 18.0f);
    g.DrawLine(&pen, 4.5f, 18.0f, 19.5f, 18.0f);
    g.DrawArc(&pen, 10.0f, 18.0f, 4.0f, 4.0f, 0.0f, 180.0f);
}

void DrawLogout(Gdiplus::Graphics& g, Gdiplus::Pen& pen)
{
    // 登出：左侧门框「[」+ 向右的箭头。
    pen.SetLineJoin(Gdiplus::LineJoinRound);
    Gdiplus::PointF frame[4] =
    {
        Gdiplus::PointF(11.0f, 4.0f), Gdiplus::PointF(4.0f, 4.0f),
        Gdiplus::PointF(4.0f, 20.0f), Gdiplus::PointF(11.0f, 20.0f),
    };
    g.DrawLines(&pen, frame, 4);
    g.DrawLine(&pen, 9.0f, 12.0f, 21.0f, 12.0f);
    Gdiplus::PointF arrow[3] =
    {
        Gdiplus::PointF(16.5f, 7.5f), Gdiplus::PointF(21.0f, 12.0f),
        Gdiplus::PointF(16.5f, 16.5f),
    };
    g.DrawLines(&pen, arrow, 3);
}

void DrawUser(Gdiplus::Graphics& g, Gdiplus::Pen& pen)
{
    // 人像：头部圆 + 肩部弧。
    g.DrawEllipse(&pen, 8.0f, 3.5f, 8.0f, 8.0f);
    g.DrawArc(&pen, 4.0f, 13.0f, 16.0f, 14.0f, 180.0f, 180.0f);
}

void DrawLock(Gdiplus::Graphics& g, Gdiplus::Pen& pen)
{
    // 挂锁：锁体圆角矩形 + 上方锁梁弧。
    pen.SetLineJoin(Gdiplus::LineJoinRound);
    g.DrawArc(&pen, 7.5f, 3.0f, 9.0f, 12.0f, 180.0f, 180.0f);
    g.DrawRectangle(&pen, 4.5f, 10.5f, 15.0f, 10.5f);
}

void DrawEye(Gdiplus::Graphics& g, Gdiplus::Pen& pen)
{
    // 眼睛：外侧眼形（两段对称弧）+ 中心瞳孔。
    g.DrawArc(&pen, 2.0f,  5.5f, 20.0f, 17.0f, 200.0f, 140.0f);
    g.DrawArc(&pen, 2.0f,  1.5f, 20.0f, 17.0f, 20.0f,  140.0f);
    g.DrawEllipse(&pen, 9.0f, 9.0f, 6.0f, 6.0f);
}

void DrawArrowRight(Gdiplus::Graphics& g, Gdiplus::Pen& pen)
{
    // 右箭头：横线 + 箭头。
    pen.SetLineJoin(Gdiplus::LineJoinRound);
    g.DrawLine(&pen, 4.0f, 12.0f, 19.0f, 12.0f);
    Gdiplus::PointF head[3] =
    {
        Gdiplus::PointF(13.0f, 6.0f), Gdiplus::PointF(19.0f, 12.0f),
        Gdiplus::PointF(13.0f, 18.0f),
    };
    g.DrawLines(&pen, head, 3);
}

void DrawPlus(Gdiplus::Graphics& g, Gdiplus::Pen& pen)
{
    // 加号：等长的竖线与横线在中心交叉。
    pen.SetStartCap(Gdiplus::LineCapRound);
    pen.SetEndCap(Gdiplus::LineCapRound);
    g.DrawLine(&pen, 12.0f, 5.0f, 12.0f, 19.0f);
    g.DrawLine(&pen, 5.0f, 12.0f, 19.0f, 12.0f);
}

void DrawClose(Gdiplus::Graphics& g, Gdiplus::Pen& pen)
{
    // 叉号：两条对角线。
    pen.SetStartCap(Gdiplus::LineCapRound);
    pen.SetEndCap(Gdiplus::LineCapRound);
    g.DrawLine(&pen, 6.5f, 6.5f, 17.5f, 17.5f);
    g.DrawLine(&pen, 17.5f, 6.5f, 6.5f, 17.5f);
}

void DrawInfo(Gdiplus::Graphics& g, Gdiplus::Pen& pen, Gdiplus::SolidBrush& brush)
{
    // 信息图标：外圆 + 上方圆点 + 下方竖杆，组成一个「i」。
    g.DrawEllipse(&pen, 3.0f, 3.0f, 18.0f, 18.0f);
    g.FillEllipse(&brush, 10.85f, 7.0f, 2.3f, 2.3f);
    pen.SetStartCap(Gdiplus::LineCapRound);
    pen.SetEndCap(Gdiplus::LineCapRound);
    g.DrawLine(&pen, 12.0f, 11.2f, 12.0f, 17.0f);
}

void DrawFolder(Gdiplus::Graphics& g, Gdiplus::Pen& pen)
{
    // 文件夹：左上带一个小凸起（标签）的轮廓。
    pen.SetLineJoin(Gdiplus::LineJoinRound);
    Gdiplus::PointF folder[7] =
    {
        Gdiplus::PointF(3.5f,  6.5f),  Gdiplus::PointF(9.0f,  6.5f),
        Gdiplus::PointF(11.0f, 9.0f),  Gdiplus::PointF(20.5f, 9.0f),
        Gdiplus::PointF(20.5f, 19.0f), Gdiplus::PointF(3.5f,  19.0f),
        Gdiplus::PointF(3.5f,  6.5f),
    };
    g.DrawLines(&pen, folder, 7);
}

void DrawTrash(Gdiplus::Graphics& g, Gdiplus::Pen& pen)
{
    // 垃圾桶：盖横线 + 提手 + 桶身梯形 + 两道竖纹。
    pen.SetLineJoin(Gdiplus::LineJoinRound);
    g.DrawLine(&pen, 4.0f, 6.5f, 20.0f, 6.5f);          // 盖
    Gdiplus::PointF handle[4] =
    {
        Gdiplus::PointF(9.5f, 6.5f), Gdiplus::PointF(9.5f, 4.0f),
        Gdiplus::PointF(14.5f, 4.0f), Gdiplus::PointF(14.5f, 6.5f),
    };
    g.DrawLines(&pen, handle, 4);
    Gdiplus::PointF body[4] =
    {
        Gdiplus::PointF(6.0f,  6.5f),  Gdiplus::PointF(7.2f,  20.5f),
        Gdiplus::PointF(16.8f, 20.5f), Gdiplus::PointF(18.0f, 6.5f),
    };
    g.DrawLines(&pen, body, 4);
    g.DrawLine(&pen, 10.0f, 10.0f, 10.4f, 17.0f);       // 竖纹
    g.DrawLine(&pen, 14.0f, 10.0f, 13.6f, 17.0f);
}

void DrawChevronRight(Gdiplus::Graphics& g, Gdiplus::Pen& pen)
{
    pen.SetLineJoin(Gdiplus::LineJoinRound);
    pen.SetStartCap(Gdiplus::LineCapRound);
    pen.SetEndCap(Gdiplus::LineCapRound);
    Gdiplus::PointF v[3] =
    {
        Gdiplus::PointF(9.5f, 5.5f), Gdiplus::PointF(15.5f, 12.0f),
        Gdiplus::PointF(9.5f, 18.5f),
    };
    g.DrawLines(&pen, v, 3);
}

void DrawChevronDown(Gdiplus::Graphics& g, Gdiplus::Pen& pen)
{
    pen.SetLineJoin(Gdiplus::LineJoinRound);
    pen.SetStartCap(Gdiplus::LineCapRound);
    pen.SetEndCap(Gdiplus::LineCapRound);
    Gdiplus::PointF v[3] =
    {
        Gdiplus::PointF(5.5f, 9.5f), Gdiplus::PointF(12.0f, 15.5f),
        Gdiplus::PointF(18.5f, 9.5f),
    };
    g.DrawLines(&pen, v, 3);
}

void DrawChevronLeft(Gdiplus::Graphics& g, Gdiplus::Pen& pen)
{
    pen.SetLineJoin(Gdiplus::LineJoinRound);
    pen.SetStartCap(Gdiplus::LineCapRound);
    pen.SetEndCap(Gdiplus::LineCapRound);
    Gdiplus::PointF v[3] =
    {
        Gdiplus::PointF(14.5f, 5.5f), Gdiplus::PointF(8.5f, 12.0f),
        Gdiplus::PointF(14.5f, 18.5f),
    };
    g.DrawLines(&pen, v, 3);
}

// 汉堡菜单：三条等距的水平圆头线（侧边栏折叠开关）。
void DrawMenu(Gdiplus::Graphics& g, Gdiplus::Pen& pen)
{
    pen.SetStartCap(Gdiplus::LineCapRound);
    pen.SetEndCap(Gdiplus::LineCapRound);
    g.DrawLine(&pen, 4.5f, 6.5f,  19.5f, 6.5f);
    g.DrawLine(&pen, 4.5f, 12.0f, 19.5f, 12.0f);
    g.DrawLine(&pen, 4.5f, 17.5f, 19.5f, 17.5f);
}

// 终端窗口：外框矩形 + 内部「>」命令提示符 + 一段光标横线（调试控制台图标）。
void DrawTerminal(Gdiplus::Graphics& g, Gdiplus::Pen& pen)
{
    pen.SetLineJoin(Gdiplus::LineJoinRound);
    g.DrawRectangle(&pen, 3.0f, 5.0f, 18.0f, 14.0f);   // 终端窗口外框
    Gdiplus::PointF prompt[3] =
    {
        Gdiplus::PointF(7.0f, 9.5f), Gdiplus::PointF(10.5f, 12.0f),
        Gdiplus::PointF(7.0f, 14.5f),
    };
    g.DrawLines(&pen, prompt, 3);                       // 「>」命令提示符
    g.DrawLine(&pen, 12.5f, 14.5f, 17.0f, 14.5f);       // 提示符后的光标横线
}

// 对勾：左下一短、右上一长的折线（toast 成功 / 完成提示）。
void DrawCheck(Gdiplus::Graphics& g, Gdiplus::Pen& pen)
{
    pen.SetStartCap(Gdiplus::LineCapRound);
    pen.SetEndCap(Gdiplus::LineCapRound);
    pen.SetLineJoin(Gdiplus::LineJoinRound);
    Gdiplus::PointF check[3] =
    {
        Gdiplus::PointF(5.0f,  12.5f),
        Gdiplus::PointF(10.0f, 17.5f),
        Gdiplus::PointF(19.0f, 7.0f),
    };
    g.DrawLines(&pen, check, 3);
}

// 警告：三角外框 + 内部感叹号（竖杆 + 圆点），toast 警示提示。
void DrawWarning(Gdiplus::Graphics& g, Gdiplus::Pen& pen, Gdiplus::SolidBrush& brush)
{
    pen.SetLineJoin(Gdiplus::LineJoinRound);
    Gdiplus::PointF tri[3] =
    {
        Gdiplus::PointF(12.0f, 3.5f),    // 顶点
        Gdiplus::PointF(21.0f, 19.5f),   // 右下角
        Gdiplus::PointF(3.0f,  19.5f),   // 左下角
    };
    g.DrawPolygon(&pen, tri, 3);
    pen.SetStartCap(Gdiplus::LineCapRound);
    pen.SetEndCap(Gdiplus::LineCapRound);
    g.DrawLine(&pen, 12.0f, 9.5f, 12.0f, 14.2f);        // 感叹号竖杆
    g.FillEllipse(&brush, 10.9f, 16.2f, 2.2f, 2.2f);    // 感叹号圆点
}

} // anonymous namespace

// 假定 g 已经 SetSmoothingMode + ScaleTransform 到 24x24 视图盒。
// 抽出来后供 DrawIcon(HDC,...) 与 RenderIconToBitmap 共用。
static void DrawIconCore(Gdiplus::Graphics& g, Icon icon, COLORREF color)
{
    Gdiplus::Pen pen(ToGdiColor(color), kStrokeWidth);
    pen.SetStartCap(Gdiplus::LineCapRound);
    pen.SetEndCap(Gdiplus::LineCapRound);
    Gdiplus::SolidBrush brush(ToGdiColor(color));

    switch (icon)
    {
    case Icon::Dashboard:  DrawDashboard(g, pen);          break;
    case Icon::Building:   DrawBuilding(g, pen, brush);    break;
    case Icon::Megaphone:  DrawMegaphone(g, pen);          break;
    case Icon::Shield:     DrawShield(g, pen);             break;
    case Icon::Settings:   DrawSettings(g, pen);           break;
    case Icon::Search:     DrawSearch(g, pen);             break;
    case Icon::Bell:       DrawBell(g, pen);               break;
    case Icon::Logout:     DrawLogout(g, pen);             break;
    case Icon::User:       DrawUser(g, pen);               break;
    case Icon::Lock:       DrawLock(g, pen);               break;
    case Icon::Eye:        DrawEye(g, pen);                break;
    case Icon::ArrowRight: DrawArrowRight(g, pen);         break;
    case Icon::Plus:         DrawPlus(g, pen);             break;
    case Icon::Close:        DrawClose(g, pen);            break;
    case Icon::Info:         DrawInfo(g, pen, brush);      break;
    case Icon::Folder:       DrawFolder(g, pen);           break;
    case Icon::Trash:        DrawTrash(g, pen);            break;
    case Icon::ChevronRight: DrawChevronRight(g, pen);     break;
    case Icon::ChevronDown:  DrawChevronDown(g, pen);      break;
    case Icon::ChevronLeft:  DrawChevronLeft(g, pen);      break;
    case Icon::Menu:         DrawMenu(g, pen);             break;
    case Icon::Terminal:     DrawTerminal(g, pen);         break;
    case Icon::Check:        DrawCheck(g, pen);            break;
    case Icon::Warning:      DrawWarning(g, pen, brush);   break;
    default:                                              break;
    }
}

void DrawIcon(HDC hdc, Icon icon, const RECT& rc, COLORREF color)
{
    const int w = rc.right - rc.left;
    const int h = rc.bottom - rc.top;
    if (w <= 0 || h <= 0)
    {
        return;
    }

    Gdiplus::Graphics g(hdc);
    g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);

    // 把 0..24 的归一化坐标系映射到目标矩形。
    g.TranslateTransform(static_cast<REAL>(rc.left), static_cast<REAL>(rc.top));
    g.ScaleTransform(w / kIconViewBox, h / kIconViewBox);

    DrawIconCore(g, icon, color);
}

HBITMAP RenderIconToBitmap(Icon icon, int size, COLORREF color)
{
    if (size < 1)
    {
        return nullptr;
    }

    // 1) 用 GDI+ Bitmap (PARGB) 当画布, 透明清屏后画图标 —— PARGB 保证 alpha
    //    通道写入正确, 后续 ::AlphaBlend 渲染时不会有黑边。
    Gdiplus::Bitmap bmp(size, size, PixelFormat32bppPARGB);
    {
        Gdiplus::Graphics g(&bmp);
        g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        g.Clear(Gdiplus::Color(0, 0, 0, 0));
        const Gdiplus::REAL sx = static_cast<Gdiplus::REAL>(size) / kIconViewBox;
        g.ScaleTransform(sx, sx);
        DrawIconCore(g, icon, color);
    }

    // 2) LockBits 拿 PARGB 像素 -> 创建独立 DIBSection -> memcpy 过去。
    //    这样返回的 HBITMAP 与 GDI+ 解耦, 进程级生命期由 caller 管理。
    Gdiplus::Rect lockRc(0, 0, size, size);
    Gdiplus::BitmapData bd;
    if (bmp.LockBits(&lockRc, Gdiplus::ImageLockModeRead,
                     PixelFormat32bppPARGB, &bd) != Gdiplus::Ok)
    {
        return nullptr;
    }

    BITMAPINFO bi = {};
    bi.bmiHeader.biSize        = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth       = size;
    bi.bmiHeader.biHeight      = -size;        // top-down
    bi.bmiHeader.biPlanes      = 1;
    bi.bmiHeader.biBitCount    = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    void* destBits = nullptr;
    HBITMAP hbm = ::CreateDIBSection(nullptr, &bi, DIB_RGB_COLORS,
                                     &destBits, nullptr, 0);
    if (hbm && destBits)
    {
        const int rowBytes = size * 4;
        BYTE* src = static_cast<BYTE*>(bd.Scan0);
        BYTE* dst = static_cast<BYTE*>(destBits);
        for (int y = 0; y < size; ++y)
        {
            ::memcpy(dst + y * rowBytes, src + y * bd.Stride, rowBytes);
        }
    }
    bmp.UnlockBits(&bd);
    return hbm;
}

}} // namespace flamingoadmin::ui
