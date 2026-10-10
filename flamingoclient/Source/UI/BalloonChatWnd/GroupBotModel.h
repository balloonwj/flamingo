/**
 *  GroupBotModel.h
 *
 *  机器人在客户端显示用的纯逻辑（docs/机器人方案.md §4.2、§4.3）：群聊窗成员页签里机器人一行的标签、头像上的字与底色，
 *  资料卡小窗的内容（简介、用法、来源）、用法说明按界面语言写成文字，以及单聊里机器人代答消息的显示文字
 *  （去掉服务端为老客户端加的前缀，换成按界面语言翻译的标记）。
 *  不碰窗口与网络，单测见 Source/Tests/BotClientTests.cpp（标签 [bot]）。
 *
 *  典型用法：
 *      row.m_tag = balloonchatwnd::BotRowTag(info);
 *      m_botCard.Open(hwnd, balloonchatwnd::MakeBotCardView(info, fallbackColor));
 *      text = balloonchatwnd::BotReplyDisplayText(text, notify.m_botKind);
 *
 *  balloonwj@qq.com   2026-10-08
 */
#pragma once

#include <windows.h>   // COLORREF
#include <string>
#include <vector>

#include "../../net/BotProtocolClient.h"   // net::GroupBotInfo / BotUsageItem
#include "BotCardLayout.h"                 // BotCardView：资料卡小窗的内容
#include "GroupBotSection.h"               // GroupBotSectionRow：机器人一行的内容

namespace balloonchatwnd {

/**
 *  成员页签机器人行右侧的小标签：停用的写「已停用」，在本群关闭的写「本群已关闭」，其余企业机器人写「企业」，
 *  本群的群机器人没有标签（返回空串）。2026-10-08 起机器人行改成与成员行相同的样式，名字与状态分开显示。
 *    b：2801 下发的机器人。
 *  @return 标签文字；没有标签时为空串
 */
std::wstring BotRowTag(const net::GroupBotInfo& b);

/**
 *  群聊窗成员页签的「机器人 N」一栏要不要显示：企业打开了机器人、本群的 2801 已到达、且本群至少有一个机器人。
 *  2026-10-08 起这一栏只用来查看（添加与管理的入口在群管理窗口），群主在群里没有机器人时也不显示。
 *    botFeatureOn：企业是否打开了机器人（features.bot）。
 *    loaded：      本群的 2801 是否已到达。
 *    botCount：    本群的机器人个数（含停用的与在本群关闭了的）。
 */
bool ShouldShowBotSection(bool botFeatureOn, bool loaded, size_t botCount);

/**
 *  把本群的机器人换成一行行的显示内容：名字、头像上的字与底色（没有设置底色时按机器人账号从调色板取）、右侧标签。
 *  成员页签与群管理窗口的「群机器人」页共用。
 *    bots：       2801 下发的机器人，只读。
 *    firstCtrlId：第一行的控件 id，第 i 行为 firstCtrlId + i。
 *    maxRows：    最多换算几行，超出的不要。
 *  @return 各行内容，顺序与 bots 一致
 */
std::vector<GroupBotSectionRow> BuildBotSectionRows(const std::vector<net::GroupBotInfo>& bots, UINT firstCtrlId,
                                                    size_t maxRows);

/**
 *  机器人头像上显示的字：设置了头像字形时用字形，否则取名称的第一个字；名称也为空时返回空串。
 *    b：2801 下发的机器人。
 */
std::wstring BotAvatarText(const net::GroupBotInfo& b);

/**
 *  机器人头像的底色："#RRGGBB" 解析成颜色；没有设置或格式不对时返回 fallback。
 *    hex：     2801 下发的 color 字段（UTF-8）。
 *    fallback：取不到颜色时的默认色（调用方一般按 userid 从调色板取）。
 */
COLORREF BotAvatarColor(const std::string& hex, COLORREF fallback);

/**
 *  一项用法说明写成一句话，例如「在群里 @我 并输入问题，我会从群问答里找答案」「每个工作日 09:00 发送消息」。
 *    u：用法说明的一项。
 *  @return 一句话；不认识的类型返回空串
 */
std::wstring BotUsageLine(const net::BotUsageItem& u);

/**
 *  资料卡小窗要显示的内容：名称、头像上的字与底色、标签（同 BotRowTag）、简介、用法说明（只收能写成文字的项）、
 *  来源（「企业管理员配置」或「群主配置」）。2026-10-08 起资料卡由系统消息框改成应用风格的小窗（用户确认）。
 *    b：            2801 下发的机器人。
 *    fallbackColor：机器人没有设置头像底色时用的颜色（调用方一般按 userid 从调色板取）。
 *  @return 资料卡内容
 */
BotCardView MakeBotCardView(const net::GroupBotInfo& b, COLORREF fallbackColor);

/**
 *  单聊里机器人代答消息的显示文字：去掉服务端为老客户端加的「[机器人] 」「[自动回复] 」前缀，前面加上按界面语言
 *  翻译的标记「[机器人代答] 」或「[自动回复] 」。kind 为空（不是代答消息）时原样返回。
 *    text：消息正文（宽字符）。
 *    kind：content.bot.kind（"personal" / "autoreply"）；其余取值按 "personal" 处理。
 *  @return 显示文字
 */
std::wstring BotReplyDisplayText(const std::wstring& text, const std::string& kind);

} // namespace balloonchatwnd
