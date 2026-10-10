/**
 *  BotUiBridge.h
 *
 *  机器人管理界面的入口桥接（docs/机器人方案.md §8.2）：群聊窗的「管理」、主面板的「我的机器人」只调用这里，
 *  由机器人插件宿主（FlamingoBot.dll 加载成功之后）登记实际的打开函数。没有登记、或插件不可用时入口隐藏，
 *  调用打开函数什么都不做。这样界面代码不直接依赖插件宿主，插件缺失时也能照常编译与运行。
 *
 *  只在界面线程上使用，不加锁。
 *
 *  典型用法：
 *      botui::SetHooks(&IsBotModuleReady, &OpenGroupBots, &OpenMyBotsWindow);   //宿主就绪后
 *      if (botui::IsAvailable())
 *          botui::OpenGroupManager(m_hWnd, groupId, groupName);
 *
 *  balloonwj@qq.com   2026-10-08
 */
#pragma once

#include <windows.h>
#include <stdint.h>

namespace botui {

//插件是否可用（已加载、已初始化、企业开关打开）
typedef bool (*IsAvailableFn)();
//打开某个群的群机器人管理窗：owner 为所有者窗口，groupid 为群号，groupName 为群名（宽字符，可为 NULL）
typedef void (*OpenGroupManagerFn)(HWND owner, int64_t groupid, const wchar_t* groupName);
//打开「我的机器人」窗口
typedef void (*OpenMyBotsFn)(HWND owner);

//机器人的四项企业开关有变化：bot 总开关、允许群机器人、允许个人机器人、允许群机器人的外部服务
typedef void (*FeaturesFn)(bool bot, bool groupBot, bool personal, bool groupExternal);
//某个群可用的机器人有变化（2820）：groupid 为 0 表示本企业全部群
typedef void (*GroupBotsChangedFn)(int64_t groupid);

/**
 *  登记或清除实际的实现。宿主在插件就绪后登记、卸载前清除（三个都传 NULL）。
 */
void SetHooks(IsAvailableFn isAvailable, OpenGroupManagerFn openGroup, OpenMyBotsFn openMine);

/**
 *  登记或清除开关变化与群机器人变化的回调（宿主在加载插件之前就可以登记，开关打开时据此补加载）。
 */
void SetNotifyHooks(FeaturesFn onFeatures, GroupBotsChangedFn onGroupBots);

/**
 *  主面板在 1013 改变了机器人开关之后调用；没有登记回调时什么都不做。
 */
void NotifyFeatures(bool bot, bool groupBot, bool personal, bool groupExternal);

/**
 *  主面板收到 2820 之后调用；没有登记回调时什么都不做。
 */
void NotifyGroupBotsChanged(int64_t groupid);

/**
 *  插件是否可用；没有登记时返回 false。
 */
bool IsAvailable();

/**
 *  打开群机器人管理窗；不可用时什么都不做并返回 false。
 */
bool OpenGroupManager(HWND owner, int64_t groupid, const wchar_t* groupName);

/**
 *  打开「我的机器人」窗口；不可用时什么都不做并返回 false。
 */
bool OpenMyBots(HWND owner);

} // namespace botui
