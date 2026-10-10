/**
 *  flamingoAdmin —— 平台设置界面 SettingsScreen。
 *
 *  对应 admin 控制台模块 1 的「平台设置」部分：页头 + 两张卡 ——
 *    · 「个人资料」卡：姓名（可编辑）+ 邮箱 / 管辖范围（只读）+「保存」；
 *    · 「修改密码」卡：当前密码 / 新密码 / 确认新密码 +「修改密码」。
 *  企业管理员原有第三张「功能开关」卡（2026-10-03 起），2026-10-08 起全部功能开关集中到企业详情屏的
 *  「功能开关」子 tab（FeatureSwitchesPage，企业管理员的「本企业」页同样有这个 tab），这张卡随之删除。
 *
 *  滚动（2026-10-05 起，此前窗口较矮时下方的卡片显示不全）：页头与各张卡作为一个整体，内容高过本界面时可用滚轮或右侧的
 *  悬浮滚动条（OverlayScrollbar）上下滚动；绘制时裁剪到本界面矩形，滚出本界面的部分既看不见也点不到。
 *
 *  进入本界面时 AdminFrame 拉 profile.do 填充资料；「保存」走
 *  updateprofile.do、「修改密码」走 changepassword.do。本控件是自绘容器：
 *  卡片 / 标签 / 只读字段自绘，输入框与按钮是 DuiEdit / FlatButton 子控件。
 *
 *  balloonwj@qq.com   2026-05-22
 */
#pragma once

#include "DuiControl.h"

namespace balloonwjui { class DuiEdit; class DuiButton; }

namespace flamingoadmin {

class OverlayScrollbar;

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

    // ---- 滚动（单元测试也用这几项核对布局）----

    // 当前滚动偏移（像素，0 表示在顶部）。
    int  GetScroll() const { return m_scroll; }

    // 最大滚动偏移（像素）：内容总高减去本界面高度，内容放得下时为 0。须在 Layout 之后读取。
    int  GetScrollMax() const { return m_scrollMax; }

    // 滚动到指定偏移并重新摆放子控件。
    //   scroll：目标偏移（像素），超出 [0, GetScrollMax()] 时夹取到边界。
    // 副作用：偏移确有变化时重排并重绘本界面，同步滚动条。
    void ScrollTo(int scroll);

    void Layout(const RECT& rcAvail) override;
    void OnPaint(HDC hdc, const RECT& rcDirty) override;

    // 滚轮：内容放不下时滚动本界面并返回 true；放得下时返回 false，交给上层处理。
    //   pt：host-client 坐标（不使用）；zDelta：滚轮增量（WHEEL_DELTA 的倍数或分数）；mkFlags：不使用。
    bool OnMouseWheel(POINT pt, short zDelta, UINT mkFlags) override;

private:
    // 滚动条拖动回调（DuiScrollBar::OnScrollFn）：ctx 为本界面，newScroll 为新的滚动偏移。
    static void OnScrollbarScroll(void* ctx, int newScroll);

    // 借用指针，所有权均在控件树中。
    balloonwjui::DuiEdit* m_nameEdit       = nullptr;  // 姓名输入框
    balloonwjui::DuiEdit* m_oldPwEdit      = nullptr;  // 当前密码输入框
    balloonwjui::DuiEdit* m_newPwEdit      = nullptr;  // 新密码输入框
    balloonwjui::DuiEdit* m_confirmPwEdit  = nullptr;  // 确认新密码输入框
    balloonwjui::DuiButton*           m_btnSaveProfile = nullptr;  // 「保存」按钮
    balloonwjui::DuiButton*           m_btnChangePw    = nullptr;  // 「修改密码」按钮
    CString m_email;       // 邮箱（只读展示，SetProfile 填）
    CString m_scopeText;   // 管辖范围文案（只读展示，SetProfile 填）

    RECT m_card1Rc{};      // 「个人资料」卡矩形
    RECT m_card2Rc{};      // 「修改密码」卡矩形

    OverlayScrollbar* m_scrollbar = nullptr;  // 右缘的悬浮滚动条（借用，所有权在控件树中）
    int  m_scroll    = 0;   // 当前滚动偏移（像素）
    int  m_scrollMax = 0;   // 最大滚动偏移（像素），每次 Layout 时重算；内容放得下时为 0
};

} // namespace flamingoadmin
