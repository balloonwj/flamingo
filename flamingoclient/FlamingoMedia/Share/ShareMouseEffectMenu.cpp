/**
 *  鼠标效果的菜单，说明见 ShareMouseEffectMenu.h。
 *  balloonwj@qq.com   2026-10-04
 */
#include "stdafx.h"

#include "ShareMouseEffectMenu.h"

namespace screenshare {

namespace {

//颜色项图标的位深：32 位 BGRA，带 alpha 通道（DuiMenu 用 CImage::Draw 画图标，带 alpha 通道的位图按预乘 alpha 混合）
const int kSwatchIconBitsPerPixel = 32;

/**
 *  建一张颜色项的图标：32 位预乘 alpha 的 CImageEx，画上 RenderMouseColorSwatch 的结果。
 *  @param color   取值见 share_mouse_color
 *  @param checked 是否是当前选中的颜色（方块里画白色对勾）
 *  @return 新建的图标，调用方持有；建位图失败时返回 NULL（这一项不带图标）
 */
CImageEx* createSwatchIcon(int color, bool checked)
{
    std::unique_ptr<CImageEx> img(new CImageEx());
    //高度取负值：自上而下的位图，GetBits 指向第一行、GetPitch 为正
    if (!img->Create(kShareMouseSwatchSize, -kShareMouseSwatchSize, kSwatchIconBitsPerPixel, CImage::createAlphaChannel))
        return NULL;
    RenderMouseColorSwatch(static_cast<uint8_t*>(img->GetBits()), kShareMouseSwatchSize, img->GetPitch(),
                           ShareMouseColorRgb(color), checked);
    return img.release();
}

} // namespace

void AppendMouseEffectMenu(balloonwjui::DuiMenu& menu, const std::vector<ShareMouseMenuItem>& items,
                           std::vector<std::unique_ptr<balloonwjui::DuiMenu> >& subMenus,
                           std::vector<std::unique_ptr<CImageEx> >& icons)
{
    for (size_t i = 0; i < items.size(); ++i)
    {
        const ShareMouseMenuItem& item = items[i];
        if (!item.m_children.empty())
        {
            std::unique_ptr<balloonwjui::DuiMenu> sub(new balloonwjui::DuiMenu());
            AppendMouseEffectMenu(*sub, item.m_children, subMenus, icons);
            menu.AppendSubMenu(item.m_id, item.m_text.c_str(), sub.get());
            subMenus.push_back(std::move(sub));
        }
        else if (item.m_swatchColor >= 0)
        {
            //颜色项：图标是该颜色的小方块，当前颜色的方块里有白色对勾
            std::unique_ptr<CImageEx> icon(createSwatchIcon(item.m_swatchColor, item.m_checked));
            menu.AppendItem(item.m_id, item.m_text.c_str(), icon.get());
            if (icon)
                icons.push_back(std::move(icon));
        }
        else
        {
            menu.AppendChecked(item.m_id, item.m_text.c_str(), item.m_checked);
        }
    }
}

} // namespace screenshare
