/**
 *  发帖窗口工具按钮的线条图标（样稿第 0 期 tmp/threadgroup/preview.html 里「表情」「图片」两个 SVG 的 GDI+ 画法）。
 *  画成 32 位预乘 alpha 位图，交给 DuiButton::SetLeadingIcon（按钮用 AlphaBlend 绘制）。
 *
 *  典型用法：
 *      HBITMAP icon = threadgroup::MakeToolIcon(threadgroup::tool_icon_face, 16, RGB(0x55, 0x55, 0x55));
 *      button->SetLeadingIcon(icon);   //位图由调用方持有，按钮销毁之后再 DeleteObject
 *
 *  balloonwj@qq.com   2026-10-07
 */
#pragma once

#include <windows.h>

namespace threadgroup {

//工具图标的种类
enum ToolIconKind
{
    tool_icon_face  = 1,   //「表情」：圆脸、两只眼睛、微笑的嘴
    tool_icon_image = 2,   //「图片」：圆角方框、左上角一个圆、下方折线山形
};

/**
 *  画一个工具图标。
 *  @param kind  图标种类，取值见 ToolIconKind
 *  @param px    位图边长，单位：像素
 *  @param color 线条颜色
 *  @return 32 位预乘 alpha、自上而下的位图（调用方负责 DeleteObject）；失败时返回 NULL
 */
HBITMAP MakeToolIcon(int kind, int px, COLORREF color);

} // namespace threadgroup
