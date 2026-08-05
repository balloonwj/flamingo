/**
 *  AboutDlgModel.h
 *
 *  "关于"对话框的<u>纯数据 / 纯逻辑</u>部分 —— 把对话框要展示的固定文案（应用名、
 *  版本号、Build、标语、版权、官网链接）以及"标题栏标题（含进程 id）的拼装"
 *  从 UI 类 CBalloonAboutDlg 里抽出来，做成不依赖 HWND / balloonui / 业务对象的
 *  纯函数与常量，便于被 FlamingoUnit 测试工程直接调用断言。
 *
 *  设计稿：design/关于.png（应用名 + 版本/Build + 标语 + 五个功能行 + 版权页脚）。
 *  与设计稿的差异（已与用户确认）：版本号写 2.0.0；对话框里的品牌图标换成 app
 *  自身图标（IDR_MAINFRAME，这部分在 CBalloonAboutDlg 里做，本文件不涉及）。
 *
 *  本文件不 include stdafx.h，工程里须把对应 .cpp 标记 PrecompiledHeader=NotUsing。
 *  只用 std::wstring，刻意不引入 ATL / CString，保证测试工程零额外依赖。
 *
 *  balloonwj@qq.com   2026-06-05
 */
#pragma once

#include <string>

namespace balloonmain {

// ====================================================================
// "关于"对话框的固定展示文案（对照 design/关于.png）
// ====================================================================
// 说明：均为宽字符串字面量常量。namespace 作用域下 const/constexpr 变量默认内部
// 链接，故放在头文件里每个翻译单元各持一份、无 ODR 冲突。

// 应用名（品牌区主标题，同时用于标题栏"关于 <应用名> [pid]"）。
constexpr const wchar_t* kAboutAppName = L"FlamingoIM";

// 版本号（已与用户确认写 2.0.0，不取设计稿的 3.2.1）。
constexpr const wchar_t* kAboutVersion = L"2.0.0";

// 构建号（沿用设计稿，仅作展示，不参与任何逻辑）。
constexpr const wchar_t* kAboutBuild = L"20260518";

// 品牌区版本行完整文案：版本 2.0.0 (Build 20260518)。
constexpr const wchar_t* kAboutVersionLine = L"版本 2.0.0 (Build 20260518)";

// 品牌区标语（设计稿原文）。
constexpr const wchar_t* kAboutSlogan = L"让沟通像火烈鸟一样优雅";

// 页脚版权串（设计稿原文）。
constexpr const wchar_t* kAboutCopyright = L"© 2024-2026 FlamingoIM Team · All Rights Reserved";

// "官方网站"行的<u>默认</u> URL。实际取值改为运行期从 flamingo.ini 的
// [ui] websiteurl 读取（见 CBalloonAboutDlg::LoadWebsiteUrlFromIni），本常量只在
// 配置文件缺该键 / 读不到时兜底，同时也是随包发布的 ini 里写的值。
// 行右侧显示的链接文本就是这个 URL 原文（含协议头），点击打开的也是它。
constexpr const wchar_t* kAboutWebsiteUrl = L"https://github.com/balloonwj/flamingo";

/**
 *  拼装"关于"对话框的标题栏标题 —— 形如「关于 FlamingoIM [12345]」，方括号内是
 *  当前进程 id（用户要求标题栏带上进程 id，便于多开 / 排障时区分实例）。
 *
 *    processId：当前进程 id（调用方传 ::GetCurrentProcessId() 的结果；类型与
 *               Win32 DWORD 一致，这里用 unsigned long 避免在纯单元里依赖 windows.h）。
 *
 *  @return 标题字符串，例：processId=12345 → L"关于 FlamingoIM [12345]"。
 *          纯函数、无副作用，可直接单测。
 */
std::wstring BuildAboutTitle(unsigned long processId);

} // namespace balloonmain
