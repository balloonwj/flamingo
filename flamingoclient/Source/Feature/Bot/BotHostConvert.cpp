/**
 *  机器人宿主的纯逻辑，见 BotHostConvert.h。
 *  balloonwj@qq.com   2026-10-08
 */
#include "stdafx.h"
#include "BotHostConvert.h"

#include <windows.h>
#include <string.h>

namespace balloonbot {

namespace {

//UTF-16 高代理项的范围
const wchar_t kHighSurrogateFirst = 0xD800;
const wchar_t kHighSurrogateLast = 0xDBFF;

//UTF-8 转 UTF-16
std::wstring Widen(const std::string& text)
{
    if (text.empty())
        return std::wstring();
    const int chars = ::MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), NULL, 0);
    if (chars <= 0)
        return std::wstring();
    std::wstring out(static_cast<size_t>(chars), L'\0');
    ::MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), &out[0], chars);
    return out;
}

} // namespace

void CopyUtf8ToFixed(const std::string& text, wchar_t* buf, uint32_t chars)
{
    if (buf == NULL || chars == 0)
        return;
    const std::wstring wide = Widen(text);
    size_t count = wide.size();
    if (count > chars - 1)
    {
        count = chars - 1;
        //截断处正好落在代理项对中间：连高代理项一起去掉，不留半个字
        if (count > 0 && wide[count - 1] >= kHighSurrogateFirst && wide[count - 1] <= kHighSurrogateLast)
            --count;
    }
    if (count > 0)
        memcpy(buf, wide.data(), count * sizeof(wchar_t));
    buf[count] = L'\0';
}

bool FillGroupBotCaps(const net::GroupBotCaps& src, flamingobot::BotGroupCapsInfo& dst)
{
    if (dst.m_cbSize != sizeof(flamingobot::BotGroupCapsInfo))
        return false;
    dst.m_isOwner = src.m_isOwner;
    dst.m_groupBotAllowed = src.m_groupBotAllowed;
    dst.m_externalAllowed = src.m_externalAllowed;
    dst.m_groupBotMax = src.m_groupBotMax;
    return true;
}

bool FillGroupBotItem(const net::GroupBotInfo& src, flamingobot::BotGroupItemInfo& dst)
{
    if (dst.m_cbSize != sizeof(flamingobot::BotGroupItemInfo))
        return false;
    dst.m_botId = src.m_botId;
    dst.m_userId = src.m_userId;
    dst.m_ownerType = src.m_ownerType;
    dst.m_isGroupBot = src.m_isGroupBot;
    CopyUtf8ToFixed(src.m_name, dst.m_name, flamingobot::kBotNameChars);
    CopyUtf8ToFixed(src.m_desc, dst.m_desc, flamingobot::kBotDescChars);
    CopyUtf8ToFixed(src.m_glyph, dst.m_glyph, flamingobot::kBotGlyphChars);
    CopyUtf8ToFixed(src.m_color, dst.m_color, flamingobot::kBotColorChars);
    dst.m_status = src.m_status;
    dst.m_off = src.m_off;
    dst.m_active = src.m_active;
    dst.m_canOff = src.m_canOff;
    dst.m_atable = src.m_atable;
    return true;
}

uint32_t FillGroupBotList(const net::GroupBotsResult& src, flamingobot::BotGroupCapsInfo* caps,
                          flamingobot::BotGroupItemInfo* items, uint32_t capacity)
{
    if (caps != NULL)
        FillGroupBotCaps(src.m_caps, *caps);
    const uint32_t total = static_cast<uint32_t>(src.m_bots.size());
    for (uint32_t i = 0; items != NULL && i < capacity && i < total; ++i)
        FillGroupBotItem(src.m_bots[i], items[i]);
    return total;
}

bool IsBotUiAvailable(bool loggedIn, bool moduleReady, bool moduleActive, bool botFeature)
{
    return loggedIn && moduleReady && moduleActive && botFeature;
}

bool IsMyBotsEntryVisible(bool available, bool personalFeature)
{
    return available && personalFeature;
}

bool IsGroupManageEntryVisible(bool available, bool isOwner)
{
    return available && isOwner;
}

} // namespace balloonbot
