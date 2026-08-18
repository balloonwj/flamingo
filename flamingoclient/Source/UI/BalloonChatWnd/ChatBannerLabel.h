/**
 *  ChatBannerLabel.h
 *
 *  聊天窗顶部那条状态提示带（离线 / 正在连接）用的标签控件。单聊窗与群聊窗共用。
 *
 *  存在理由 —— balloonui 的 DuiLabel 只画文字、背景透明，没有底色接口，做不出
 *  "整条铺一层浅色底、下边缘一根细分隔线"的提示带外观。本类按仓库 UI 约定走
 *  "子类化扩展"这一档：继承 DuiLabel，只在它画文字<u>之前</u>补一层底色与分隔线，
 *  文字排版 / 字体 / 颜色仍旧完全交给基类，不改 balloonui。
 *
 *  典型用法（颜色随连接态切换，见各聊天窗的 ApplyConnectionState）：
 *      banner->SetBannerColors(RGB(0xFF,0xF4,0xE5),   // 浅琥珀底
 *                              RGB(0xF3,0xE0,0xC0));  // 稍深的分隔线
 *      banner->SetTextColor(RGB(0xB4,0x53,0x09));     // 深琥珀字（基类接口）
 *      banner->SetText(L"当前为离线状态……");
 *
 *  本文件遵循 balloonui 头文件约定：包含它之前调用方须已引入 ATL/WTL。
 *
 *  balloonwj@qq.com   2026-08-03
 */

#pragma once

#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiLabel.h"

namespace balloonchatwnd {

/**
 *  ChatBannerLabel —— 带底色与下边框的状态提示带标签。
 *
 *  除底色与分隔线外的一切（文字、字体、对齐、省略号、以及点击与否）都由 DuiLabel
 *  负责，本类不接管任何交互：离线提示带保持默认的 ModeText（纯告知、不可点），
 *  「已 Pin N 条」提示带则由调用方 SetMode(ModeLink) 变成可点的链接（见
 *  ChatWindowLayout.h 的 ApplyPinBannerStyle）。
 */
class ChatBannerLabel : public balloonwjui::DuiLabel
{
public:
    ChatBannerLabel();

    /**
     *  设置提示带的底色与下边框色。两者都取 CLR_INVALID 时退化为纯文字标签
     *  （不铺底、不画线），与裸 DuiLabel 表现一致。
     *    bgColor      整条的填充底色；CLR_INVALID 表示不铺底。
     *    borderColor  下边缘 1px 分隔线的颜色；CLR_INVALID 表示不画线。
     *  副作用：触发重绘。
     */
    void SetBannerColors(COLORREF bgColor, COLORREF borderColor);

    /**
     *  DuiControl 覆写：先铺底色与下边框，再交给 DuiLabel 画文字。
     *    hdc      目标设备上下文。
     *    rcDirty  脏矩形（客户区坐标），原样转交基类。
     */
    void OnPaint(HDC hdc, const RECT& rcDirty) override;

private:
    COLORREF m_bgColor;      // 整条的填充底色，CLR_INVALID = 不铺底
    COLORREF m_borderColor;  // 下边缘分隔线颜色，CLR_INVALID = 不画线
};

} // namespace balloonchatwnd
