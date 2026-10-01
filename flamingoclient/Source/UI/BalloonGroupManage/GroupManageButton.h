/**
 *  GroupManageButton.h
 *
 *  群管理对话框里通用的操作按钮。设计图的主色是橙色，而 balloonui 的
 *  DuiButton::Variant::Primary 是品牌蓝，配色对不上；故继承 DuiButton
 *  复用点击 / 通知 / hover 状态，只覆写 OnPaint 自绘四种风格：
 *    · 橙色实心主按钮（邀请成员 / 同意 / 保存修改 / 发布公告 …）
 *    · 白底描边次按钮（拒绝 / 重置 / 复制 / 延长 / 解除 …）
 *    · 淡红实心危险按钮（移除全部成员 / 解散群聊）
 *    · 红字描边危险按钮（撤回）
 *  这是一个参数化按钮（靠 SetButtonStyle 切换风格），不是一次性子类，
 *  全对话框各处复用。
 *
 *  本文件遵循 balloonui 头文件约定：包含它之前调用方须已引入 ATL/WTL。
 *
 *  balloonwj@qq.com   2026-06-03
 */

#pragma once

#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiButton.h"

namespace balloongroupmanage {

// 按钮风格。
enum GmButtonStyle
{
    kGmBtnPrimary       = 0,   // 橙色实心 + 白字（主操作）
    kGmBtnSecondary     = 1,   // 白底 + 灰边 + 深字（次操作）
    kGmBtnDanger        = 2,   // 淡红实心 + 红字（不可逆危险操作）
    kGmBtnDangerOutline = 3     // 透明底 + 红边 + 红字（弱危险操作，如撤回）
};

/**
 *  GroupManageButton —— 群管理对话框通用操作按钮（自绘）。
 */
class GroupManageButton : public balloonwjui::DuiButton
{
public:
    GroupManageButton();

    // 设置按钮风格。会触发重绘。
    //   style：取值见 GmButtonStyle。
    void SetButtonStyle(int style);

    // 设置文字字号（96 DPI 下的像素，绘制时按当前 DPI 换算）。默认 13。会触发重绘。
    //   px：> 0 的字号。
    void SetTextPx(int px);

    // 设置圆角半径（96 DPI 下的像素，绘制时按当前 DPI 换算）。默认 8。会触发重绘。
    //   px：>= 0 的半径。
    void SetCornerRadius(int px);

    // DuiControl 覆写：按当前风格 + hover 态自绘圆角按钮。
    void OnPaint(HDC hdc, const RECT& rcDirty) override;

    // 查某种按钮风格的文字字重。OnPaint 与单元测试共用这一处。
    //   style：取值见 GmButtonStyle；越界时按常规字重。
    // @return 字重，取值见 GmFontWeight。纯函数，无副作用。
    static int TextWeightForStyle(int style);

    // DuiControl 覆写：用普通箭头光标（不切手型）。
    bool OnSetCursor(POINT pt) override { return false; }

private:
    int m_style;     // 按钮风格，取值见 GmButtonStyle
    int m_textPx;    // 文字字号（像素）
    int m_cornerR;   // 圆角半径（像素）
};

} // namespace balloongroupmanage
