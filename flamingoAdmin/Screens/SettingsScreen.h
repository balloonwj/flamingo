/**
 *  flamingoAdmin —— 平台设置界面 SettingsScreen。
 *
 *  对应 admin 控制台模块 1 的「平台设置」部分：页头 + 两张卡 ——
 *    · 「个人资料」卡：姓名（可编辑）+ 邮箱 / 管辖范围（只读）+「保存」；
 *    · 「修改密码」卡：当前密码 / 新密码 / 确认新密码 +「修改密码」。
 *
 *  进入本界面时 AdminFrame 拉 profile.do 填充资料；「保存」走
 *  updateprofile.do、「修改密码」走 changepassword.do。本控件是自绘容器：
 *  卡片 / 标签 / 只读字段自绘，输入框与按钮是 DuiEditHost / FlatButton 子控件。
 *
 *  balloonwj@qq.com   2026-05-22
 */
#pragma once

#include "DuiControl.h"

namespace balloonwjui { class DuiEditHost; class DuiButton; }

namespace flamingoadmin {

// FlatButton 已迁到 balloonwjui::DuiButton; ui 命名空间不再需要 forward decl。

// 平台设置界面。
class SettingsScreen : public balloonwjui::DuiControl
{
public:
    SettingsScreen();

    // 用 profile.do 的结果填充「个人资料」卡。AdminFrame 在进入本界面、
    // 应答返回后调用。
    //   name：当前管理员姓名（填入可编辑输入框）。
    //   email：当前管理员邮箱（只读展示）。
    //   scopeText：管辖范围中文文案（只读展示，如「平台超级管理员」）。
    void SetProfile(const CString& name, const CString& email,
                    const CString& scopeText);

    // 取「个人资料」卡里姓名输入框的当前文字（已去首尾空白）。
    CString GetName() const;

    // 取「修改密码」卡的三个输入框文字（密码不做去空白处理）。
    CString GetOldPassword() const;
    CString GetNewPassword() const;
    CString GetConfirmPassword() const;

    // 清空「修改密码」卡的三个输入框（密码修改成功后由 AdminFrame 调用）。
    void ClearPasswordFields();

    void Layout(const RECT& rcAvail) override;
    void OnPaint(HDC hdc, const RECT& rcDirty) override;

private:
    // 借用指针，所有权均在控件树中。
    balloonwjui::DuiEditHost* m_nameEdit       = nullptr;  // 姓名输入框
    balloonwjui::DuiEditHost* m_oldPwEdit      = nullptr;  // 当前密码输入框
    balloonwjui::DuiEditHost* m_newPwEdit      = nullptr;  // 新密码输入框
    balloonwjui::DuiEditHost* m_confirmPwEdit  = nullptr;  // 确认新密码输入框
    balloonwjui::DuiButton*           m_btnSaveProfile = nullptr;  // 「保存」按钮
    balloonwjui::DuiButton*           m_btnChangePw    = nullptr;  // 「修改密码」按钮

    CString m_email;       // 邮箱（只读展示，SetProfile 填）
    CString m_scopeText;   // 管辖范围文案（只读展示，SetProfile 填）

    RECT m_card1Rc{};      // 「个人资料」卡矩形
    RECT m_card2Rc{};      // 「修改密码」卡矩形
};

} // namespace flamingoadmin
