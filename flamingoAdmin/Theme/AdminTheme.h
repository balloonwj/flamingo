/**
 *  flamingoAdmin —— 全局视觉主题：调色板、圆角、关键尺寸、字体。
 *
 *  数值来源于设计稿 design/FlamingoIM Enterprise Admin.html 解包后的 :root
 *  CSS 变量与各组件样式。所有界面统一从这里取色、取尺寸，禁止在界面代码里
 *  直接写裸色值 / 裸像素（开发约定文档「禁止魔数」要求）。
 *
 *  balloonwj@qq.com   2026.05.21
 */
#pragma once

#include <windows.h>
#include <tchar.h>

namespace flamingoadmin { namespace theme {

// ===========================================================================
// flamingo 品牌色阶（暖红 / 火烈鸟色）
// 对应设计稿 --flamingo-50 ... --flamingo-700，500 为主色。
// ===========================================================================
constexpr COLORREF kFlamingo50  = RGB(0xff, 0xf1, 0xef);  // 最浅：选中项背景
constexpr COLORREF kFlamingo100 = RGB(0xff, 0xe0, 0xdc);  // 很浅：hover 背景
constexpr COLORREF kFlamingo200 = RGB(0xff, 0xc1, 0xb9);  // 浅
constexpr COLORREF kFlamingo300 = RGB(0xff, 0x9b, 0x8e);  // 中浅：渐变端
constexpr COLORREF kFlamingo400 = RGB(0xff, 0x75, 0x63);  // 中：渐变 / 图标
constexpr COLORREF kFlamingo500 = RGB(0xff, 0x5a, 0x44);  // 主色：主按钮 / 强调
constexpr COLORREF kFlamingo600 = RGB(0xe8, 0x4a, 0x36);  // 深：主按钮 hover
constexpr COLORREF kFlamingo700 = RGB(0xb8, 0x34, 0x1f);  // 最深：主色文字

// ===========================================================================
// ink 中性灰色阶（偏冷，Windows 风格）
// 对应设计稿 --ink-0 ... --ink-800：0 为纯白，数值越大越深。
// ===========================================================================
constexpr COLORREF kInk0   = RGB(0xff, 0xff, 0xff);  // 纯白：卡片背景
constexpr COLORREF kInk25  = RGB(0xfa, 0xfb, 0xfc);  // 近白：应用底色
constexpr COLORREF kInk50  = RGB(0xf4, 0xf5, 0xf7);  // 极浅灰：分区背景
constexpr COLORREF kInk100 = RGB(0xea, 0xec, 0xef);  // 浅灰：分隔线 / 边框
constexpr COLORREF kInk150 = RGB(0xdf, 0xe2, 0xe7);  // 浅灰：输入框边框
constexpr COLORREF kInk200 = RGB(0xcb, 0xd0, 0xd8);  // 中浅灰：边框 hover
constexpr COLORREF kInk300 = RGB(0xa7, 0xae, 0xb9);  // 中灰：占位图标
constexpr COLORREF kInk400 = RGB(0x6f, 0x76, 0x82);  // 中灰：次要文字
constexpr COLORREF kInk500 = RGB(0x4a, 0x51, 0x60);  // 深灰：正文次级
constexpr COLORREF kInk600 = RGB(0x2f, 0x35, 0x40);  // 深：标题次级
constexpr COLORREF kInk700 = RGB(0x1c, 0x22, 0x30);  // 很深：主文字 / 标题
constexpr COLORREF kInk800 = RGB(0x10, 0x14, 0x1d);  // 最深

// ===========================================================================
// 语义色：状态、级别提示统一用这组色
// ===========================================================================
constexpr COLORREF kColorOk     = RGB(0x1f, 0x9d, 0x55);  // 成功 / 正常（绿）
constexpr COLORREF kColorWarn   = RGB(0xd8, 0x8a, 0x17);  // 警告（橙）
constexpr COLORREF kColorInfo   = RGB(0x28, 0x64, 0xd4);  // 信息（蓝）
constexpr COLORREF kColorDanger = RGB(0xd6, 0x34, 0x2a);  // 危险（红）

// ===========================================================================
// 圆角半径（96-dpi 逻辑像素）
// ===========================================================================
constexpr int kRadiusSm = 4;   // 小圆角：徽章 / 输入框
constexpr int kRadiusMd = 6;   // 中圆角：按钮 / 卡片
constexpr int kRadiusLg = 10;  // 大圆角：弹窗 / 大卡片

// ===========================================================================
// 关键布局尺寸（96-dpi 逻辑像素）
// ===========================================================================
constexpr int kTitleBarHeight      = 32;   // 自绘标题栏高度
constexpr int kSidebarWidth        = 224;  // 侧边栏标准态宽度
constexpr int kSidebarWidthCompact = 64;   // 侧边栏紧凑态宽度
constexpr int kTopBarHeight        = 46;   // 顶栏高度

// ===========================================================================
// 字体
// balloonui 默认 UI 字体即微软雅黑；这里统一声明字体族与各场景字号（pt）。
// ===========================================================================
const TCHAR* const kFontFamily = _T("Microsoft YaHei");  // 中文 UI 字体族

constexpr int kFontSizeSmall   = 9;   // 小号：次要文字 / 徽章
constexpr int kFontSizeNormal  = 10;  // 常规：正文
constexpr int kFontSizeTitle   = 12;  // 标题：卡片标题
constexpr int kFontSizeHeading = 16;  // 大标题：页头

}} // namespace flamingoadmin::theme
