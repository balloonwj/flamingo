/**
 *  机器人宿主的纯逻辑（docs/机器人方案.md 8.2）：
 *    - 把 exe 缓存的 2801 结果（net::GroupBotsResult）填进交给 FlamingoBot.dll 的定长结构体
 *      （flamingobot::BotGroupCapsInfo / BotGroupItemInfo）：UTF-8 转 UTF-16、按容量截断、结尾补 0；
 *    - 机器人管理界面是否可用、「我的机器人」入口是否显示的判定。
 *  写成纯函数便于单元测试（Source/Tests/BotHostTests.cpp，标签 [bothost]）；CBotHost 与主面板都调用这里，不在各处各写一份判断。
 *
 *  典型用法：
 *      uint32_t total = balloonbot::FillGroupBotList(result, caps, items, capacity);
 *      if (balloonbot::IsMyBotsEntryVisible(host.IsAvailable(), userMgr.m_bBotPersonalFeature))
 *          ShowMenuItem();
 *
 *  balloonwj@qq.com   2026-10-08
 */
#pragma once

#include <stdint.h>
#include <string>

#include "../../net/BotProtocolClient.h"           // net::GroupBotsResult / GroupBotInfo / GroupBotCaps
#include "../../../FlamingoBot/BotApi.h"           // flamingobot::BotGroupCapsInfo / BotGroupItemInfo

namespace balloonbot {

/**
 *  UTF-8 文字写进定长的宽字符缓冲区：写不下时截断（不拆开代理项对），结果始终以 '\0' 结尾。
 *  @param text  UTF-8 文字
 *  @param buf   目标缓冲区
 *  @param chars 缓冲区容量（字符数，含结尾 0）；为 0 时什么也不写
 */
void CopyUtf8ToFixed(const std::string& text, wchar_t* buf, uint32_t chars);

/**
 *  能力说明写进接口结构体。
 *  @param dst [出参] 须由调用方填好 m_cbSize；m_cbSize 不对时不写，返回 false
 */
bool FillGroupBotCaps(const net::GroupBotCaps& src, flamingobot::BotGroupCapsInfo& dst);

/**
 *  一个机器人写进接口结构体。
 *  @param dst [出参] 须由调用方填好 m_cbSize；m_cbSize 不对时不写，返回 false
 */
bool FillGroupBotItem(const net::GroupBotInfo& src, flamingobot::BotGroupItemInfo& dst);

/**
 *  整个 2801 结果写进接口结构体（IBotHost::GetGroupBotList 的实现用）。
 *  @param src      exe 缓存的 2801 结果
 *  @param caps     [出参] 能力说明，可为 NULL
 *  @param items    [出参] 机器人数组，可为 NULL（只取个数）
 *  @param capacity items 的元素个数
 *  @return 机器人的实际个数（可能大于 capacity，此时只填前 capacity 个）
 */
uint32_t FillGroupBotList(const net::GroupBotsResult& src, flamingobot::BotGroupCapsInfo* caps,
                          flamingobot::BotGroupItemInfo* items, uint32_t capacity);

/**
 *  机器人管理界面是否可用。
 *  @param loggedIn     已登录
 *  @param moduleReady  FlamingoBot.dll 已加载并初始化成功
 *  @param moduleActive 模块处于已登录状态（通知过 OnLoggedIn）
 *  @param botFeature   企业开关 features.bot 打开
 *  @return 都满足时返回 true
 */
bool IsBotUiAvailable(bool loggedIn, bool moduleReady, bool moduleActive, bool botFeature);

/**
 *  「我的机器人」入口是否显示（主面板系统菜单、系统设置「聊天」页的链接）。
 *  @param available       机器人管理界面可用（IsBotUiAvailable 的结果）
 *  @param personalFeature 企业开关 features.botpersonal 打开
 */
bool IsMyBotsEntryVisible(bool available, bool personalFeature);

/**
 *  群聊窗成员页签「机器人」分组标题右侧的「管理」是否显示。
 *  @param available 机器人管理界面可用
 *  @param isOwner   2801 的 caps.isowner：本人是群主
 */
bool IsGroupManageEntryVisible(bool available, bool isOwner);

} // namespace balloonbot
