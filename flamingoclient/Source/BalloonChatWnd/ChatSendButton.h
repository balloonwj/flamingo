/**
 *  ChatSendButton.h
 *
 *  单聊窗输入区右下角的「发送」按钮 —— 橙色圆角底 + 纸飞机图标 + "发送"
 *  文字。继承 DuiButton 复用点击 / 通知逻辑，只覆写 OnPaint。
 *
 *  本文件遵循 balloonui 头文件约定：包含它之前调用方须已引入 ATL/WTL。
 *
 *  balloonwj@qq.com   2026-05-21
 */

#pragma once

#include "../../balloonui/balloonui/Controls/Basic/DuiButton.h"

namespace balloonchatwnd {

//发送按钮右侧「▼」下拉区的宽度（逻辑像素）：内含一条竖分隔线 + 一个下三角。
const int kSendBtnDropdownZoneW = 24;

/**
 *  ChatSendButton —— 橙色「发送」拆分按钮。
 *
 *  主体（左侧大块）= 纸飞机图标 + "发送"文字，点击即发送（沿用 DuiButton 的
 *  DUIN_CLICK）。右侧用一条竖分隔线隔出一个「▼」下拉区，点它不发送、而是发出
 *  DUIN_SENDBTN_DROPDOWN 通知，由父窗口据此弹出「Enter 发送 / Ctrl+Enter 发送」
 *  切换菜单。对照设计图 dd/发送三角.png。
 */
class ChatSendButton : public balloonwjui::DuiButton
{
public:
    //本控件特有通知码（从 DUIN_CUSTOM 之后扩展，见 DuiNotify.h）。
    enum
    {
        DUIN_SENDBTN_DROPDOWN = balloonwjui::DUIN_CUSTOM + 1,   //点右侧「▼」下拉区
    };

    ChatSendButton();
    ~ChatSendButton();   // 注销本控件的 tooltip 注册

    // DuiControl 覆写：绘制橙色圆角底 + 纸飞机图标 + 文字 + 右侧分隔线与 ▼。
    void OnPaint(HDC hdc, const RECT& rcDirty) override;

    // DuiControl 覆写：用普通箭头光标。
    bool OnSetCursor(POINT pt) override { return false; }

    // DuiControl 覆写：按下落在右侧「▼」下拉区时发 DUIN_SENDBTN_DROPDOWN 通知、
    // 不进入按钮按下态（从而不触发发送）；落在主体则交基类按普通按钮处理。
    bool OnLButtonDown(POINT pt, UINT mkFlags) override;

private:
    //判断一个点（host 客户区坐标）是否落在右侧「▼」下拉区内。
    bool InDropdownZone(POINT pt) const;
};

} // namespace balloonchatwnd
