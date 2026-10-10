/**
 *  GroupManageIcons.h
 *
 *  群管理对话框左侧导航用的线性图标集。与 BalloonChatWnd/ChatIcons、
 *  BalloonMain/MainPanelIcons 同款做法：统一 viewBox 24、统一描边风格，
 *  保证本对话框图标视觉一致。
 *
 *  本文件遵循 balloonui 头文件约定：包含它之前调用方须已引入 ATL/WTL，
 *  并且包含 <gdiplus.h>。
 *
 *  balloonwj@qq.com   2026-06-03
 */

#pragma once

#include <gdiplus.h>

namespace balloongroupmanage {

// =====================================================================
// 图标种类
// =====================================================================
//
// 对应左侧导航各项的引导图标，取值越界则不绘制。
enum GmIconKind
{
    kGmIconMembers  = 0,   // 双人形 —— 「成员管理」
    kGmIconApproval = 1,   // 对勾 —— 「加群审批」
    kGmIconMute     = 2,   // 禁止符（圆 + 斜杠）—— 「禁言管理」
    kGmIconInfo     = 3,   // 圆圈 i —— 「基本信息」
    kGmIconLock     = 4,   // 挂锁 —— 「群权限」
    kGmIconDanger   = 5,   // 垃圾桶 —— 「危险区域」
    kGmIconAnnounce = 6,    // 铃铛 —— 「群公告」（导航项与历史公告条目共用）
    kGmIconBot      = 7     // 机器人（圆角方头 + 两只眼睛 + 天线）—— 「群机器人」，造型与系统菜单的「我的机器人」一致
};

// =====================================================================
// 绘制
// =====================================================================

// 在指定区域内描边绘制一个群管理导航图标。
//
//   g：          目标 Graphics（调用方应已开启抗锯齿）。
//   kind：       图标种类，取值见 GmIconKind；越界值不绘制。
//   x / y：      绘制区域左上角（设备坐标）。
//   size：       绘制区域边长（图标按 viewBox 24 等比缩放到此尺寸）。
//   color：      描边 / 填充颜色。
//   strokeWidth：描边宽度，以 viewBox 24 为基准的单位（一般取 2.0）。
void DrawGmNavIcon(Gdiplus::Graphics& g, int kind,
                   Gdiplus::REAL x, Gdiplus::REAL y, Gdiplus::REAL size,
                   const Gdiplus::Color& color, Gdiplus::REAL strokeWidth);

} // namespace balloongroupmanage
