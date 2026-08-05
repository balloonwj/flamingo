/**
 *  MainPanelMenuIcons.cpp
 *
 *  线描图标 → CImageEx 位图的渲染实现。做法对照 MainPanelAvatar.cpp 的
 *  "GDI+ AA 绘制 → 预乘 alpha 位图"：建一张 32bpp top-down 带 alpha 的
 *  CImage，用 GDI+ 把图标（MainPanelIcons::DrawLineIcon）画上去，背景透明、
 *  边缘抗锯齿；CImage 自带 alpha 通道，DuiMenu 调 CImage::Draw 时走 AlphaBlend。
 *
 *  本文件依赖 CImageEx（atlimage），只在 Flamingo.vcxproj 编译，故照常 include
 *  Flamingo 的 stdafx.h（不进 FlamingoUnit 测试工程）。
 *
 *  balloonwj@qq.com   2026-06-03
 */

#include "stdafx.h"
#include "MainPanelMenuIcons.h"
#include "MainPanelIcons.h"        // DrawLineIcon / IconKind
#include "../ImageEx.h"            // CImageEx（CImage 子类）

#include <gdiplus.h>
#pragma comment(lib, "gdiplus.lib")

namespace balloonmain {

namespace {

// GDI+ 懒初始化 token —— 与 MainPanelAvatar.cpp 同模式（多次 GdiplusStartup
// 安全；本翻译单元自留 token，进程退出时自动回收，无需 GdiplusShutdown）。
ULONG_PTR& GdipToken()
{
    static ULONG_PTR s_token = 0;
    return s_token;
}

void EnsureGdiplus()
{
    if (GdipToken() != 0)
    {
        return;
    }
    Gdiplus::GdiplusStartupInput input;
    Gdiplus::GdiplusStartup(&GdipToken(), &input, nullptr);
}

// 菜单图标描边宽度，以 viewBox 24 为基准（与 MainPanelIcons 其它图标同口径）。
const Gdiplus::REAL kMenuIconStroke = 2.0f;

} // anonymous namespace

CImageEx* CreateMenuIconImage(int iconKind, COLORREF color, int sizePx)
{
    if (sizePx <= 0)
    {
        return NULL;
    }

    EnsureGdiplus();

    CImageEx* img = new CImageEx();

    // 负高度 → top-down DIB：GetPitch() 为正、GetBits() 指向视觉首行，直接交给
    // GDI+ 不存在上下翻转歧义。createAlphaChannel → 32bpp 带 alpha，CImage::Draw
    // 走 AlphaBlend（DuiMenu 即这样绘制图标）。
    if (!img->Create(sizePx, -sizePx, 32, CImage::createAlphaChannel))
    {
        delete img;
        return NULL;
    }

    // 用 Gdiplus::Bitmap 直接包 CImage 的像素 buffer。PARGB = premultiplied alpha，
    // GDI+ 写入时自动预乘，符合 AlphaBlend 对源位图的要求。
    Gdiplus::Bitmap bitmap(sizePx, sizePx, img->GetPitch(),
                           PixelFormat32bppPARGB,
                           static_cast<BYTE*>(img->GetBits()));

    Gdiplus::Graphics g(&bitmap);
    g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    g.Clear(Gdiplus::Color(0, 0, 0, 0));   // 全透明底（预乘下 a=0 即 rgb 也为 0）

    Gdiplus::Color stroke(255,
                          GetRValue(color),
                          GetGValue(color),
                          GetBValue(color));
    DrawLineIcon(g, iconKind, 0.0f, 0.0f,
                 static_cast<Gdiplus::REAL>(sizePx), stroke, kMenuIconStroke);

    // Graphics / Bitmap 析构自动 flush，像素已写入 CImage 的 DIB，返回即可。
    return img;
}

void AppendStatusMenuItems(balloonwjui::DuiMenu& menu,
                           std::vector<std::unique_ptr<CImageEx> >& icons,
                           const std::vector<StatusMenuEntry>& model,
                           int iconSizePx)
{
    icons.reserve(icons.size() + model.size());

    for (size_t i = 0; i < model.size(); ++i)
    {
        const StatusMenuEntry& e = model[i];

        // 图标类型由行模型给出：当前所处的状态是带外环的 kIconStatusCurrent，其余
        // 是普通实心点 kIconOnline；颜色一律取该状态的圆点色，与胶囊同源。
        CImageEx* icon = CreateMenuIconImage(e.iconKind, e.dotColor, iconSizePx);

        // 先存进容器保活，再交给 DuiMenu 借用；渲染失败时 icon 为 NULL，AppendItem 容忍。
        icons.push_back(std::unique_ptr<CImageEx>(icon));
        menu.AppendItem(e.id, e.label, icon);
    }
}

} // namespace balloonmain
