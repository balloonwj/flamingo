/**
 *  AuthButtons.h
 *
 *  登录 / 注册 / 网络设置窗用的两个自绘按钮控件：
 *   · AuthPrimaryButton —— 橙色圆角主操作按钮（"登 录" 等）。
 *   · AuthLinkLabel     —— 无边框文字链接（注册账号 / 网络设置 等）。
 *
 *  两者都继承 balloonui 的 DuiButton —— 复用其点击 / 按压 / 悬停事件
 *  逻辑（点击发 DUIN_CLICK，由对话框 WM_DUI_NOTIFY 接住），只覆写
 *  OnPaint 改变外观。因仍是 DuiButton 子类，FindCtrlById 后的
 *  dynamic_cast<DuiButton*> 仍成立。
 *
 *  本文件遵循 balloonui 头文件约定：包含它之前，调用方必须已引入
 *  ATL/WTL。
 *
 *  balloonwj@qq.com   2026-05-21
 */

#pragma once

#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiButton.h"

namespace balloonauth {

/**
 *  AuthPrimaryButton —— 登录页主操作按钮。
 *
 *  外观对应 design 的 auth.jsx Btn(primary, size lg)：12px 圆角、
 *  #FF7A45 实心填充（悬停 #FF6A30）、白色居中文字。按钮文字经
 *  DuiButton::SetText 设置。
 */
class AuthPrimaryButton : public balloonwjui::DuiButton
{
public:
    AuthPrimaryButton();

    // DuiControl 覆写：画圆角橙色按钮 + 白色居中文字。
    void OnPaint(HDC hdc, const RECT& rcDirty) override;
};

/**
 *  AuthLinkLabel —— 文字链接（注册账号 / 网络设置 / 忘记密码）。
 *
 *  外观对应 design 的 auth.jsx 里的 <a>：单行彩色文字，无边框无填充。
 *  链接颜色由 SetLinkColor 指定（DuiButton 自带的 SetTextColor 无配套
 *  getter，故本类另存一份颜色）。
 */
class AuthLinkLabel : public balloonwjui::DuiButton
{
public:
    AuthLinkLabel();

    // 设置链接文字颜色。
    //   c：COLORREF 颜色值。默认主题橙 #FF7A45。
    void SetLinkColor(COLORREF c) { m_linkColor = c; Invalidate(); }

    // DuiControl 覆写：只画一行居中的彩色文字。
    void OnPaint(HDC hdc, const RECT& rcDirty) override;

private:
    COLORREF m_linkColor;   // 链接文字颜色
};

/**
 *  AuthCheckBox —— 登录页用的复选框（记住密码 / 自动登录）。
 *
 *  继承 DuiButton 并在构造时设为 Checkbox 风格，复用其勾选 / 通知逻辑
 *  （点击 toggle 并发 DUIN_VALUECHANGED）；覆写 OnPaint 只画"小圆角
 *  方框 + 文字"，去掉 DuiButton 复选框自带的按钮底色方块，外观贴合
 *  design 的 Check。
 */
class AuthCheckBox : public balloonwjui::DuiButton
{
public:
    AuthCheckBox();

    // DuiControl 覆写：画勾选方框 + 标签文字。
    void OnPaint(HDC hdc, const RECT& rcDirty) override;
};

} // namespace balloonauth
