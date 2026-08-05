/**
 *  ChatRootPanel.h
 *
 *  单聊窗客户区的根面板 —— 一个会填白底的纵向布局容器。
 *
 *  缘由：balloonui 的窗口客户区默认底色并非纯白，而设计稿 07 里头部与
 *  输入区都是纯白；若不铺白底，输入框内嵌的 Win32 EDIT（自带白底）会在
 *  非白背景上显出一个突兀的白方框。故让根面板铺白底，头部 / 输入区这些
 *  透明的 hbox/vbox 便都呈白色；消息区另由 ChatMessageList 覆盖自己的
 *  暖白底。
 *
 *  本文件遵循 balloonui 头文件约定：包含它之前调用方须已引入 ATL/WTL。
 *
 *  balloonwj@qq.com   2026-05-21
 */

#pragma once

#include "../../balloonui/balloonui/Controls/Layout/DuiLayout.h"

namespace balloonchatwnd {

/**
 *  ChatRootPanel —— 填白底的纵向布局容器，用作单聊窗客户区根控件。
 */
class ChatRootPanel : public balloonwjui::DuiVBox
{
public:
    // DuiControl 覆写：先铺白底，再绘制子控件。
    void OnPaint(HDC hdc, const RECT& rcDirty) override;
};

} // namespace balloonchatwnd
