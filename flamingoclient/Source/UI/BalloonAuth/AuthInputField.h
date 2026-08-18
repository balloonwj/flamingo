/**
 *  AuthInputField.h
 *
 *  登录 / 注册 / 网络设置窗用的圆角输入框容器 —— balloonui 自定义控件。
 *
 *  自身画一个 10px 圆角、#F5F4F1 填充的矩形 + 左侧前导图标（人形 / 锁形）；
 *  内嵌唯一一个真正的输入控件（DuiComboBox 账号下拉框 / DuiEdit
 *  密码框，由调用方 AddChild 进来）。内嵌控件被布局在图标右侧，且其
 *  底色应设为同样的 #F5F4F1、关闭自身边框，从而与圆角容器无缝融合 ——
 *  内嵌控件是矩形的，但被内缩在容器圆角范围内，四角由容器绘制，视觉
 *  上即呈现圆角输入框。
 *
 *  视觉对应 design/FlamingoIM-standalone.html 内 auth.jsx 的 Input。
 *
 *  本文件遵循 balloonui 头文件约定：包含它之前，调用方必须已引入
 *  ATL/WTL。
 *
 *  balloonwj@qq.com   2026-05-21
 */

#pragma once

#include "../../../third-party/balloonui/balloonui/DuiControl.h"

namespace balloonauth {

/**
 *  AuthInputField
 *
 *  圆角输入框容器。约定只挂 0 或 1 个子控件（真正的输入控件）。
 */
class AuthInputField : public balloonwjui::DuiControl
{
public:
    // 前导图标种类。
    enum IconKind
    {
        IconNone = 0,   // 不画图标
        IconUser = 1,   // 账号 —— 人形图标
        IconLock = 2,   // 密码 —— 锁形图标
    };

    AuthInputField();

    // 设置前导图标种类。
    //   kind：见 IconKind。
    void SetIconKind(IconKind kind) { m_iconKind = kind; Invalidate(); }

    // DuiControl 覆写：画圆角填充 + 前导图标，再绘制内嵌输入控件。
    void OnPaint(HDC hdc, const RECT& rcDirty) override;

    // DuiControl 覆写：把内嵌输入控件布局在前导图标右侧。
    void Layout(const RECT& rcAvail) override;

private:
    IconKind m_iconKind;   // 前导图标种类
};

} // namespace balloonauth
