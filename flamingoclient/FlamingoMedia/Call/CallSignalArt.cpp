/**
 *  通话窗底部栏的网络信号图标的实现，CallSignalArt.cpp
 *  竖条几何、位图绘制、位图缓存与显示控件，说明见 CallSignalArt.h。
 *  balloonwj@qq.com   2026-10-06
 */
#include "stdafx.h"
#include "CallSignalArt.h"

#include <string.h>

namespace call {

namespace {

//设计尺寸 20×16（逻辑像素）下的竖条几何，其它尺寸按比例换算
const int kBarDesignW      = 3;    //竖条宽
const int kBarDesignGap    = 2;    //相邻两根竖条的间距
const int kBarDesignLeft   = 1;    //最左一根距位图左边
const int kBarDesignBottom = 15;   //竖条底边（不含），即距位图下边 1
const int kBarDesignMinH   = 5;    //最矮一根的高
const int kBarDesignStepH  = 3;    //相邻两根的高度差

//32 位 DIB 每像素字节数与各分量的偏移（BGRA 顺序）
const int kBytesPerPixel = 4;
const int kOffsetBlue    = 0;
const int kOffsetGreen   = 1;
const int kOffsetRed     = 2;
const int kOffsetAlpha   = 3;
//不透明的透明度分量
const BYTE kOpaqueAlpha  = 255;
//位图的位深
const WORD kBitsPerPixel = 32;

//设计尺寸下的坐标按比例换算到实际像素（四舍五入）
int ScaleX(int designX, int widthPx)
{
    return ::MulDiv(designX, widthPx, kCallSignalIconW);
}

int ScaleY(int designY, int heightPx)
{
    return ::MulDiv(designY, heightPx, kCallSignalIconH);
}

//把矩形内的像素填成不透明的颜色；矩形已在位图范围内
void FillOpaque(BYTE* bits, int widthPx, const RECT& rc, COLORREF color)
{
    for (int y = rc.top; y < rc.bottom; ++y)
    {
        BYTE* row = bits + static_cast<size_t>(y) * widthPx * kBytesPerPixel;
        for (int x = rc.left; x < rc.right; ++x)
        {
            BYTE* px = row + static_cast<size_t>(x) * kBytesPerPixel;
            //不透明时预乘透明度不改变颜色分量
            px[kOffsetBlue] = GetBValue(color);
            px[kOffsetGreen] = GetGValue(color);
            px[kOffsetRed] = GetRValue(color);
            px[kOffsetAlpha] = kOpaqueAlpha;
        }
    }
}

} // namespace

RECT CallSignalBarRect(int index, int widthPx, int heightPx)
{
    RECT rc = { 0, 0, 0, 0 };
    if (index < 0 || index >= kSignalBarCount || widthPx <= 0 || heightPx <= 0)
        return rc;
    const int designLeft = kBarDesignLeft + index * (kBarDesignW + kBarDesignGap);
    const int designTop = kBarDesignBottom - (kBarDesignMinH + index * kBarDesignStepH);
    rc.left = ScaleX(designLeft, widthPx);
    rc.right = ScaleX(designLeft + kBarDesignW, widthPx);
    rc.top = ScaleY(designTop, heightPx);
    rc.bottom = ScaleY(kBarDesignBottom, heightPx);
    //尺寸很小时按比例换算可能得到空矩形，至少保留 1 像素宽高
    if (rc.right <= rc.left)
        rc.right = rc.left + 1;
    if (rc.bottom <= rc.top)
        rc.top = rc.bottom - 1;
    return rc;
}

HBITMAP MakeCallSignalBitmap(const CallSignalLook& look, int widthPx, int heightPx)
{
    if (widthPx <= 0 || heightPx <= 0)
        return NULL;

    BITMAPINFO bi;
    memset(&bi, 0, sizeof(bi));
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = widthPx;
    bi.bmiHeader.biHeight = -heightPx;   //负数表示自上而下
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = kBitsPerPixel;
    bi.bmiHeader.biCompression = BI_RGB;
    void* bits = NULL;
    HBITMAP bmp = ::CreateDIBSection(NULL, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
    if (bmp == NULL || bits == NULL)
    {
        if (bmp != NULL)
            ::DeleteObject(bmp);
        return NULL;
    }
    //CreateDIBSection 分配的像素初始为 0，即完全透明，只需画竖条
    BYTE* pixels = static_cast<BYTE*>(bits);
    for (int i = 0; i < kSignalBarCount; ++i)
    {
        const RECT rc = CallSignalBarRect(i, widthPx, heightPx);
        FillOpaque(pixels, widthPx, rc, (i < look.m_bars) ? look.m_litColor : look.m_dimColor);
    }
    ::GdiFlush();
    return bmp;
}

CallSignalArtCache::CallSignalArtCache()
{
}

CallSignalArtCache::~CallSignalArtCache()
{
    for (std::map<Key, HBITMAP>::iterator it = m_bitmaps.begin(); it != m_bitmaps.end(); ++it)
    {
        if (it->second != NULL)
            ::DeleteObject(it->second);
    }
    m_bitmaps.clear();
}

HBITMAP CallSignalArtCache::Get(int grade, int widthPx, int heightPx)
{
    if (widthPx <= 0 || heightPx <= 0)
        return NULL;
    const Key key(grade, std::make_pair(widthPx, heightPx));
    std::map<Key, HBITMAP>::iterator it = m_bitmaps.find(key);
    if (it != m_bitmaps.end())
        return it->second;
    HBITMAP bmp = MakeCallSignalBitmap(SignalLookForGrade(grade), widthPx, heightPx);
    //创建失败不缓存，下次再试
    if (bmp != NULL)
        m_bitmaps[key] = bmp;
    return bmp;
}

CallSignalIcon::CallSignalIcon()
    : m_bitmap(NULL)
{
    //只供查看：不接收焦点（否则点击后会画虚线焦点框），不显示文字
    SetTabStop(false);
    SetText(_T(""));
}

void CallSignalIcon::SetSignalBitmap(HBITMAP bitmap)
{
    if (bitmap == m_bitmap)
        return;
    m_bitmap = bitmap;
    SetBgBitmap(bitmap, bitmap, bitmap, bitmap);
    Invalidate();
}

bool CallSignalIcon::OnSetCursor(POINT pt)
{
    (void)pt;
    ::SetCursor(::LoadCursor(NULL, IDC_ARROW));
    return true;
}

} // namespace call
