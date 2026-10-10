/**
 *  发帖窗口工具按钮的线条图标，见 ThreadToolIcons.h。
 *  balloonwj@qq.com   2026-10-07
 */
#include "stdafx.h"
#include "ThreadToolIcons.h"

#include <gdiplus.h>
#include <string.h>

namespace threadgroup {

namespace {

//样稿 SVG 的坐标系边长（viewBox 0 0 18 18），以下坐标都在这个坐标系里，绘制时按位图边长等比缩放
const Gdiplus::REAL kViewBox = 18.0f;
const Gdiplus::REAL kStrokeWidth = 1.4f;   //线宽

//「表情」：圆脸、眼睛、嘴（三次贝塞尔：M6 11 c1.5 1.6 4.5 1.6 6 0）
const Gdiplus::REAL kFaceCenter = 9.0f;    //脸的圆心（x 与 y 相同）
const Gdiplus::REAL kFaceRadius = 7.0f;    //脸的半径
const Gdiplus::REAL kEyeLeftX = 6.5f;      //左眼圆心 x
const Gdiplus::REAL kEyeRightX = 11.5f;    //右眼圆心 x
const Gdiplus::REAL kEyeY = 7.5f;          //两眼圆心 y
const Gdiplus::REAL kEyeRadius = 0.9f;     //眼睛半径（实心圆；样稿的 0.6 半径加线宽在小尺寸下看不清，略放大）
const Gdiplus::REAL kMouthStartX = 6.0f;   //嘴的起点 x
const Gdiplus::REAL kMouthY = 11.0f;       //嘴两端的 y
const Gdiplus::REAL kMouthEndX = 12.0f;    //嘴的终点 x
const Gdiplus::REAL kMouthCtrl1X = 7.5f;   //第一个控制点 x
const Gdiplus::REAL kMouthCtrl2X = 10.5f;  //第二个控制点 x
const Gdiplus::REAL kMouthCtrlY = 12.6f;   //两个控制点的 y

//「图片」：圆角方框 rect(2,3,14,12,rx=2)、太阳 circle(6.5,7.5,r=1.3)、山形折线 M3 13 l4-4 3 3 2-2 3 3
const Gdiplus::REAL kFrameX = 2.0f;        //方框左上角 x
const Gdiplus::REAL kFrameY = 3.0f;        //方框左上角 y
const Gdiplus::REAL kFrameW = 14.0f;       //方框宽
const Gdiplus::REAL kFrameH = 12.0f;       //方框高
const Gdiplus::REAL kFrameRadius = 2.0f;   //方框圆角
const Gdiplus::REAL kSunX = 6.5f;          //太阳圆心 x
const Gdiplus::REAL kSunY = 7.5f;          //太阳圆心 y
const Gdiplus::REAL kSunRadius = 1.3f;     //太阳半径
const Gdiplus::PointF kMountain[] = {      //山形折线的各点
    Gdiplus::PointF(3.0f, 13.0f), Gdiplus::PointF(7.0f, 9.0f), Gdiplus::PointF(10.0f, 12.0f),
    Gdiplus::PointF(12.0f, 10.0f), Gdiplus::PointF(15.0f, 13.0f) };
const int kMountainPoints = sizeof(kMountain) / sizeof(kMountain[0]);   //折线的点数

const Gdiplus::REAL kQuarterTurn = 90.0f;        //圆角每段弧的角度
const Gdiplus::REAL kHalfTurn = 180.0f;          //左上角弧的起始角
const Gdiplus::REAL kThreeQuarterTurn = 270.0f;  //右上角弧的起始角
const int kBytesPerPixel = 4;                    //32 位位图每像素的字节数
const BYTE kOpaque = 0xFF;                       //GDI+ 颜色里「完全不透明」的 alpha

//画「表情」
void DrawFace(Gdiplus::Graphics& g, Gdiplus::Pen& pen, Gdiplus::Brush& brush)
{
    g.DrawEllipse(&pen, kFaceCenter - kFaceRadius, kFaceCenter - kFaceRadius, 2 * kFaceRadius, 2 * kFaceRadius);
    g.FillEllipse(&brush, kEyeLeftX - kEyeRadius, kEyeY - kEyeRadius, 2 * kEyeRadius, 2 * kEyeRadius);
    g.FillEllipse(&brush, kEyeRightX - kEyeRadius, kEyeY - kEyeRadius, 2 * kEyeRadius, 2 * kEyeRadius);
    g.DrawBezier(&pen, kMouthStartX, kMouthY, kMouthCtrl1X, kMouthCtrlY, kMouthCtrl2X, kMouthCtrlY, kMouthEndX, kMouthY);
}

//画「图片」
void DrawImage(Gdiplus::Graphics& g, Gdiplus::Pen& pen)
{
    Gdiplus::GraphicsPath frame;
    const Gdiplus::REAL d = 2 * kFrameRadius;
    frame.AddArc(kFrameX, kFrameY, d, d, kHalfTurn, kQuarterTurn);
    frame.AddArc(kFrameX + kFrameW - d, kFrameY, d, d, kThreeQuarterTurn, kQuarterTurn);
    frame.AddArc(kFrameX + kFrameW - d, kFrameY + kFrameH - d, d, d, 0.0f, kQuarterTurn);
    frame.AddArc(kFrameX, kFrameY + kFrameH - d, d, d, kQuarterTurn, kQuarterTurn);
    frame.CloseFigure();
    g.DrawPath(&pen, &frame);
    g.DrawEllipse(&pen, kSunX - kSunRadius, kSunY - kSunRadius, 2 * kSunRadius, 2 * kSunRadius);
    g.DrawLines(&pen, kMountain, kMountainPoints);
}

} // namespace

HBITMAP MakeToolIcon(int kind, int px, COLORREF color)
{
    if (px <= 0)
        return NULL;
    Gdiplus::Bitmap canvas(px, px, PixelFormat32bppPARGB);
    if (canvas.GetLastStatus() != Gdiplus::Ok)
        return NULL;
    {
        Gdiplus::Graphics g(&canvas);
        g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        g.Clear(Gdiplus::Color(0, 0, 0, 0));
        const Gdiplus::REAL scale = static_cast<Gdiplus::REAL>(px) / kViewBox;
        g.ScaleTransform(scale, scale);
        const Gdiplus::Color c(kOpaque, GetRValue(color), GetGValue(color), GetBValue(color));
        Gdiplus::Pen pen(c, kStrokeWidth);
        pen.SetLineJoin(Gdiplus::LineJoinRound);
        pen.SetStartCap(Gdiplus::LineCapRound);
        pen.SetEndCap(Gdiplus::LineCapRound);
        Gdiplus::SolidBrush brush(c);
        if (kind == tool_icon_face)
            DrawFace(g, pen, brush);
        else
            DrawImage(g, pen);
    }
    //拷到 DIB：biHeight 为负数表示自上而下，与 GDI+ 的行序一致，逐行照抄即可
    BITMAPINFO bi;
    ::ZeroMemory(&bi, sizeof(bi));
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = px;
    bi.bmiHeader.biHeight = -px;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    void* bits = NULL;
    HBITMAP hbm = ::CreateDIBSection(NULL, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
    if (hbm == NULL || bits == NULL)
    {
        if (hbm != NULL)
            ::DeleteObject(hbm);
        return NULL;
    }
    Gdiplus::Rect all(0, 0, px, px);
    Gdiplus::BitmapData data;
    ::ZeroMemory(&data, sizeof(data));
    if (canvas.LockBits(&all, Gdiplus::ImageLockModeRead, PixelFormat32bppPARGB, &data) != Gdiplus::Ok)
    {
        ::DeleteObject(hbm);
        return NULL;
    }
    const int stride = px * kBytesPerPixel;
    for (int y = 0; y < px; ++y)
    {
        const BYTE* src = static_cast<const BYTE*>(data.Scan0) + static_cast<INT_PTR>(data.Stride) * y;
        BYTE* dst = static_cast<BYTE*>(bits) + static_cast<INT_PTR>(stride) * y;
        ::memcpy(dst, src, stride);
    }
    canvas.UnlockBits(&data);
    ::GdiFlush();
    return hbm;
}

} // namespace threadgroup
