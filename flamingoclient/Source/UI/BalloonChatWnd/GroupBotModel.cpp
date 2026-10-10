/**
 *  GroupBotModel.cpp
 *
 *  机器人在客户端显示用的纯逻辑，说明见 GroupBotModel.h 与 docs/机器人方案.md §4.2、§4.3。
 *
 *  balloonwj@qq.com   2026-10-08
 */
#include "stdafx.h"
#include "GroupBotModel.h"

#include <windows.h>
#include <ctype.h>    // isxdigit
#include <stdlib.h>   // strtoul
#include <vector>

#include "../../Common/I18n/I18n.h"        // TR / TRF：界面文字的多语言查找
#include "../BalloonMain/AvatarPalette.h"  // AvatarColorForUser：机器人没有设置底色时按账号取调色板颜色

namespace balloonchatwnd {

namespace {

//触发方式（与服务端 bot_trigger_type 一致）
const int kTriggerAt = 1;
const int kTriggerKeyword = 2;
const int kTriggerJoin = 3;
const int kTriggerSchedule = 4;
const int kTriggerWebhook = 6;
//关键词匹配方式
const int kMatchExact = 2;
const int kMatchPrefix = 3;
//定时种类
const int kScheduleWorkdays = 2;
const int kScheduleWeekly = 3;
const int kScheduleMonthly = 4;
//一周天数
const int kDaysPerWeek = 7;

//星期的名字（1 周一 … 7 周日），用时再经 TR 翻译
const wchar_t* const kWeekdayNames[kDaysPerWeek] =
{
    TR_NOOP(L"周一"), TR_NOOP(L"周二"), TR_NOOP(L"周三"), TR_NOOP(L"周四"), TR_NOOP(L"周五"), TR_NOOP(L"周六"),
    TR_NOOP(L"周日"),
};

//服务端为老客户端加的正文前缀（UTF-8 原文，服务端只生成简体中文）
const wchar_t* const kServerPrefixRobot = L"[机器人] ";          //i18n-ignore: 服务端生成的固定前缀，只用于识别
const wchar_t* const kServerPrefixAutoReply = L"[自动回复] "; //i18n-ignore: 同上

//UTF-8 转宽字符
std::wstring Widen(const std::string& s)
{
    if (s.empty())
        return std::wstring();
    const int n = ::MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), NULL, 0);
    if (n <= 0)
        return std::wstring();
    std::wstring w((size_t)n, L'\0');
    ::MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), &w[0], n);
    return w;
}

//把关键词用顿号连起来
std::wstring JoinKeywords(const std::vector<std::string>& kws)
{
    std::wstring out;
    for (size_t i = 0; i < kws.size(); ++i)
    {
        if (i > 0)
            out += TR(L"、");
        out += Widen(kws[i]);
    }
    return out;
}

//去掉开头的一段前缀；没有这段前缀时原样返回
std::wstring StripPrefix(const std::wstring& text, const wchar_t* prefix)
{
    const std::wstring p(prefix);
    if (text.compare(0, p.size(), p) == 0)
        return text.substr(p.size());
    return text;
}

} // anonymous namespace

std::wstring BotRowTag(const net::GroupBotInfo& b)
{
    if (b.m_status != 0)
        return TR(L"已停用");
    if (b.m_off)
        return TR(L"本群已关闭");
    if (!b.m_isGroupBot)
        return TR(L"企业");
    return std::wstring();
}

bool ShouldShowBotSection(bool botFeatureOn, bool loaded, size_t botCount)
{
    return botFeatureOn && loaded && botCount > 0;
}

std::vector<GroupBotSectionRow> BuildBotSectionRows(const std::vector<net::GroupBotInfo>& bots, UINT firstCtrlId,
                                                    size_t maxRows)
{
    std::vector<GroupBotSectionRow> rows;
    for (size_t i = 0; i < bots.size() && i < maxRows; ++i)
    {
        const net::GroupBotInfo& b = bots[i];
        GroupBotSectionRow row(Widen(b.m_name), firstCtrlId + static_cast<UINT>(i));
        row.m_avatarText = BotAvatarText(b);
        row.m_avatarColor = BotAvatarColor(b.m_color, balloonmain::AvatarColorForUser(b.m_userId));
        row.m_tag = BotRowTag(b);
        rows.push_back(row);
    }
    return rows;
}

std::wstring BotAvatarText(const net::GroupBotInfo& b)
{
    const std::wstring glyph = Widen(b.m_glyph);
    if (!glyph.empty())
        return glyph;
    const std::wstring name = Widen(b.m_name);
    if (name.empty())
        return std::wstring();
    //名称首字是代理对（如表情）时取完整的两个码元
    const size_t len = (name.size() > 1 && name[0] >= 0xD800 && name[0] <= 0xDBFF) ? 2 : 1;
    return name.substr(0, len);
}

COLORREF BotAvatarColor(const std::string& hex, COLORREF fallback)
{
    //只认 "#RRGGBB"：7 个字符、后 6 个都是十六进制数字
    const size_t kHexColorLen = 7;
    if (hex.size() != kHexColorLen || hex[0] != '#')
        return fallback;
    for (size_t i = 1; i < hex.size(); ++i)
    {
        if (!isxdigit((unsigned char)hex[i]))
            return fallback;
    }
    const unsigned long rgb = strtoul(hex.c_str() + 1, NULL, 16);
    return RGB((rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF);
}

std::wstring BotUsageLine(const net::BotUsageItem& u)
{
    switch (u.m_type)
    {
    //被 @：注明是否用问答作答
    case kTriggerAt:
        return u.m_qa ? std::wstring(TR(L"在群里 @我 并输入问题，我会从群问答里找答案")) : std::wstring(TR(L"在群里 @我"));
    //关键词：按匹配方式写，要求 @ 时补一句
    case kTriggerKeyword:
    {
        const std::wstring kws = JoinKeywords(u.m_keywords);
        std::wstring line;
        if (u.m_match == kMatchExact)
            line = TRF(L"发送「{0}」", kws);
        else if (u.m_match == kMatchPrefix)
            line = TRF(L"发送以「{0}」开头的消息", kws);
        else
            line = TRF(L"发送包含「{0}」的消息", kws);
        if (u.m_needAt)
            line += TR(L"（需要同时 @我）");
        return line;
    }
    //新成员入群
    case kTriggerJoin:
        return TR(L"新成员入群时发送欢迎消息");
    //定时：按种类写
    case kTriggerSchedule:
    {
        const std::wstring time = Widen(u.m_time);
        if (u.m_scheduleKind == kScheduleWorkdays)
            return TRF(L"每个工作日 {0} 发送消息", time);
        if (u.m_scheduleKind == kScheduleWeekly)
        {
            std::wstring days;
            for (size_t i = 0; i < u.m_weekdays.size(); ++i)
            {
                const int d = u.m_weekdays[i];
                if (d < 1 || d > kDaysPerWeek)
                    continue;
                if (!days.empty())
                    days += TR(L"、");
                days += TR(kWeekdayNames[d - 1]);
            }
            return TRF(L"每周的{0} {1} 发送消息", days, time);
        }
        if (u.m_scheduleKind == kScheduleMonthly)
            return TRF(L"每月 {0} 号 {1} 发送消息", u.m_monthDay, time);
        return TRF(L"每天 {0} 发送消息", time);
    }
    //外部系统推送
    case kTriggerWebhook:
        return TR(L"接收外部系统推送的消息");
    //不认识的类型不显示
    default:
        return std::wstring();
    }
}

BotCardView MakeBotCardView(const net::GroupBotInfo& b, COLORREF fallbackColor)
{
    BotCardView view;
    view.m_name = Widen(b.m_name);
    view.m_avatarText = BotAvatarText(b);
    view.m_avatarColor = BotAvatarColor(b.m_color, fallbackColor);
    view.m_tag = BotRowTag(b);
    view.m_desc = Widen(b.m_desc);
    //不认识的用法类型写不成文字，不显示；一项都没有时由资料卡显示「暂无可用的触发方式」
    for (size_t i = 0; i < b.m_usage.size(); ++i)
    {
        const std::wstring line = BotUsageLine(b.m_usage[i]);
        if (!line.empty())
            view.m_usageLines.push_back(line);
    }
    view.m_source = b.m_isGroupBot ? TR(L"群主配置") : TR(L"企业管理员配置");
    return view;
}

std::wstring BotReplyDisplayText(const std::wstring& text, const std::string& kind)
{
    if (kind.empty())
        return text;
    if (kind == "autoreply")
        return std::wstring(TR(L"[自动回复] ")) + StripPrefix(text, kServerPrefixAutoReply);
    return std::wstring(TR(L"[机器人代答] ")) + StripPrefix(text, kServerPrefixRobot);
}

} // namespace balloonchatwnd
