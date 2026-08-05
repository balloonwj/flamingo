/**
 *  AvatarPickerTile.cpp —— 可点击 + 可选中头像 / 色块磁贴实现。
 *
 *  绘制：先让基类 DuiAvatar 画头像位图 / 纯色块，选中时再用 GDI+ 在外圈叠画一圈
 *  AA 强调色描边（形状跟随基类的圆 / 圆角矩形）。点击：抬起即向父发 DUIN_CLICK。
 *
 *  balloonwj@qq.com   2026-06-05
 */
#include "stdafx.h"
#include "AvatarPickerTile.h"

#include <gdiplus.h>
#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "msimg32.lib")   // AlphaBlend

#include "../../balloonui/balloonui/DuiNotify.h"

namespace balloonmain {

// 选中描边宽度（逻辑像素）与离磁贴边缘的内缩量。
static const Gdiplus::REAL kSelRingWidth = 2.0f;
static const Gdiplus::REAL kSelRingInset = 1.0f;

// 默认强调色：品牌橙（与 AvatarPalette 第 0 槽一致）。
static const COLORREF kDefaultAccent = RGB(0xFF, 0x7A, 0x45);

AvatarPickerTile::AvatarPickerTile()
    : m_owned(NULL)
    , m_selected(false)
    , m_accent(kDefaultAccent)
{
}

AvatarPickerTile::~AvatarPickerTile()
{
    if (m_owned != NULL)
    {
        ::DeleteObject(m_owned);
        m_owned = NULL;
    }
}

void AvatarPickerTile::SetOwnedBitmap(HBITMAP hbm)
{
    if (m_owned == hbm)
    {
        return;
    }
    if (m_owned != NULL)
    {
        ::DeleteObject(m_owned);
    }
    m_owned = hbm;
    SetBitmap(hbm);   // 基类仅借用，所有权仍在本控件
    Invalidate();
}

void AvatarPickerTile::SetSelected(bool sel)
{
    if (m_selected == sel)
    {
        return;
    }
    m_selected = sel;
    Invalidate();
}

bool AvatarPickerTile::OnLButtonUp(POINT /*pt*/, UINT /*mkFlags*/)
{
    // 抬起即点击：父（修改头像对话框）按 ctrlId 区分被点的是哪枚磁贴 / 色点。
    NotifyParent(balloonwjui::DUIN_CLICK);
    return true;
}

void AvatarPickerTile::OnPaint(HDC hdc, const RECT& rcDirty)
{
    RECT rc = GetRect();
    int rcW = rc.right - rc.left;
    int rcH = rc.bottom - rc.top;

    if (m_owned != NULL && rcW > 0 && rcH > 0)
    {
        // 自绘位图：用 AlphaBlend 画，绕开 DuiAvatar 的 GDI+ 取位图路径——那条路径
        // 对本控件自绘的 top-down DIB 会上下颠倒（见 dd/165307 头像翻转）。AlphaBlend
        // 读 DIB 头、尊重 top-down，与 DuiTreeView 联系人头像同款画法，方向正确。
        // 位图本身已是圆角方形（角外透明），故无需再裁剪。
        BITMAP bm = {};
        ::GetObject(m_owned, sizeof(bm), &bm);
        if (bm.bmWidth > 0 && bm.bmHeight > 0)
        {
            HDC     mem = ::CreateCompatibleDC(hdc);
            HGDIOBJ old = ::SelectObject(mem, m_owned);
            BLENDFUNCTION bf = { AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };
            ::AlphaBlend(hdc, rc.left, rc.top, rcW, rcH,
                         mem, 0, 0, bm.bmWidth, bm.bmHeight, bf);
            ::SelectObject(mem, old);
            ::DeleteDC(mem);
        }
    }
    else
    {
        // 无位图（首字母页的纯色底色点）：交基类画实色形状 fallback。
        balloonwjui::DuiAvatar::OnPaint(hdc, rcDirty);
    }

    if (!m_selected)
    {
        return;
    }

    Gdiplus::REAL w = (Gdiplus::REAL)(rc.right - rc.left);
    Gdiplus::REAL h = (Gdiplus::REAL)(rc.bottom - rc.top);
    if (w <= 0.0f || h <= 0.0f)
    {
        return;
    }

    Gdiplus::Graphics g(hdc);
    g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    g.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);

    Gdiplus::Pen pen(Gdiplus::Color(255, GetRValue(m_accent),
                                    GetGValue(m_accent), GetBValue(m_accent)),
                     kSelRingWidth);

    // 描边矩形：从磁贴边缘内缩半个线宽 + kSelRingInset，让整条线落在控件内、不被裁。
    Gdiplus::REAL off = kSelRingInset + kSelRingWidth * 0.5f;
    Gdiplus::REAL x = (Gdiplus::REAL)rc.left + off;
    Gdiplus::REAL y = (Gdiplus::REAL)rc.top + off;
    Gdiplus::REAL rw = w - off * 2.0f;
    Gdiplus::REAL rh = h - off * 2.0f;
    if (rw <= 0.0f || rh <= 0.0f)
    {
        return;
    }

    // 描边形状跟随基类形状（圆 / 圆角矩形）。
    if (GetShape() == balloonwjui::DuiAvatar::ShapeCircle)
    {
        g.DrawEllipse(&pen, x, y, rw, rh);
    }
    else
    {
        Gdiplus::REAL r = (Gdiplus::REAL)GetCornerRadius();
        Gdiplus::REAL maxR = (rw < rh ? rw : rh) * 0.5f;
        if (r > maxR)
        {
            r = maxR;
        }
        if (r < 0.0f)
        {
            r = 0.0f;
        }
        Gdiplus::GraphicsPath path;
        if (r <= 0.0f)
        {
            path.AddRectangle(Gdiplus::RectF(x, y, rw, rh));
        }
        else
        {
            Gdiplus::REAL d = r * 2.0f;
            path.AddArc(x,          y,          d, d, 180.0f, 90.0f);
            path.AddArc(x + rw - d, y,          d, d, 270.0f, 90.0f);
            path.AddArc(x + rw - d, y + rh - d, d, d,   0.0f, 90.0f);
            path.AddArc(x,          y + rh - d, d, d,  90.0f, 90.0f);
            path.CloseFigure();
        }
        g.DrawPath(&pen, &path);
    }
}

} // namespace balloonmain
