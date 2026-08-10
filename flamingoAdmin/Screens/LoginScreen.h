/**
 *  flamingoAdmin —— 登录界面。
 *
 *  左侧品牌渐变面板 + 右侧登录表单。表单里的账户 / 密码输入框是 DuiEditHost
 *  （寄宿真 EDIT），其 EnsureCreated 由 AdminFrame 在装入界面后统一调用。
 *  登录按钮被点击时经 WM_DUI_NOTIFY 上报（ctrlId = kIdLoginSubmit）。
 *
 *  balloonwj@qq.com   2026.05.21
 */
#pragma once

#include "DuiControl.h"
#include <memory>

namespace flamingoadmin {

// 构建登录界面控件树，返回其根控件。
std::unique_ptr<balloonwjui::DuiControl> BuildLoginScreen();

} // namespace flamingoadmin
