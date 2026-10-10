/**
 *  鼠标效果的菜单（屏幕共享第 16 期），ShareMouseEffectMenu.h
 *
 *  把 BuildMouseEffectMenu 生成的菜单模型加进 balloonui 的 DuiMenu：有子项的加成子菜单；颜色项加成带图标的普通项，
 *  图标是该颜色的小方块（DuiMenu 的勾选标记与图标共用一列，当前颜色在方块里画白色对勾，见 RenderMouseColorSwatch）；其余加成可勾选项。
 *  共享控制条的「更多」菜单与录屏条的「鼠标效果」菜单共用这一份（16d 由 ShareControlBar.cpp 挪来）。
 *
 *  本文件遵循 balloonui 头文件约定：包含它之前调用方须已引入 ATL/WTL（即在 .cpp 里先 #include "stdafx.h"）。
 *
 *  典型用法：
 *      balloonwjui::DuiMenu menu;
 *      std::vector<std::unique_ptr<balloonwjui::DuiMenu> > subMenus;
 *      std::vector<std::unique_ptr<CImageEx> > icons;
 *      AppendMouseEffectMenu(menu, BuildMouseEffectMenu(settings, idBase), subMenus, icons);
 *      const UINT chosen = menu.TrackPopup(x, y, m_hWnd);   //subMenus 与 icons 须到这里之后才能释放
 *
 *  balloonwj@qq.com   2026-10-04
 */
#pragma once

#include <memory>
#include <vector>
#include "ShareMouseEffects.h"
#include "../../../third-party/balloonui/balloonui/ImageEx.h"   // CImageEx（颜色项的图标，第 16 期补充；实现在 balloonui_static 里）
#include "../../../third-party/balloonui/balloonui/Controls/List/DuiMenu.h"

namespace screenshare {

/**
 *  把鼠标效果的菜单模型加进 DuiMenu。
 *  @param menu     目标菜单
 *  @param items    菜单模型（见 BuildMouseEffectMenu）
 *  @param subMenus [入参 / 出参] 新建的子菜单放进这里，调用方须在 TrackPopup 返回之后才释放
 *  @param icons    [入参 / 出参] 新建的颜色项图标放进这里，调用方须在 TrackPopup 返回之后才释放（DuiMenu 只借用图标，不负责释放）
 */
void AppendMouseEffectMenu(balloonwjui::DuiMenu& menu, const std::vector<ShareMouseMenuItem>& items,
                           std::vector<std::unique_ptr<balloonwjui::DuiMenu> >& subMenus,
                           std::vector<std::unique_ptr<CImageEx> >& icons);

} // namespace screenshare
