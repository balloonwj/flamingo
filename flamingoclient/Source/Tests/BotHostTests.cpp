/**
 *  机器人宿主纯逻辑的单元测试（标签 [bothost]，Feature/Bot/BotHostConvert）：
 *    - BOTH1 2801 结果写进交给 DLL 的定长结构体：各字段、UTF-8 转 UTF-16、按容量截断且结尾补 0；
 *    - BOTH2 结构体大小不对时不写；机器人比容量多时只填前几个、返回实际个数；items 为 NULL 时只取个数；
 *    - BOTH3 入口的显隐：管理界面可用、「我的机器人」入口、群聊窗的「管理」入口；
 *    - BOTH4 接口约定：DLL 收发的号段与 exe 的 BotProtocolClient::isDllFrame 一致。
 *  框架 Catch2 v3。
 *  balloonwj@qq.com   2026-10-08
 */
#include "stdafx.h"

#include "catch_amalgamated.hpp"

#include <string.h>
#include <string>
#include <vector>

#include "../Feature/Bot/BotHostConvert.h"

using namespace balloonbot;

namespace {

//一个准备好 m_cbSize 的机器人结构体
flamingobot::BotGroupItemInfo EmptyItem()
{
    flamingobot::BotGroupItemInfo item;
    memset(&item, 0, sizeof(item));
    item.m_cbSize = sizeof(item);
    return item;
}

//一份 2801 结果
net::GroupBotsResult SampleResult(size_t count)
{
    net::GroupBotsResult r;
    r.m_code = 0;
    r.m_groupid = 88;
    r.m_caps.m_isOwner = true;
    r.m_caps.m_groupBotAllowed = true;
    r.m_caps.m_externalAllowed = true;
    r.m_caps.m_groupBotMax = 5;
    for (size_t i = 0; i < count; ++i)
    {
        net::GroupBotInfo b;
        b.m_botId = 12 + static_cast<int64_t>(i);
        b.m_userId = 5401;
        b.m_ownerType = 2;
        b.m_isGroupBot = true;
        b.m_name = u8"值班提醒";
        b.m_desc = u8"每个工作日提醒值班";
        b.m_glyph = u8"值";
        b.m_color = "#43A047";
        b.m_status = 1;
        b.m_off = false;
        b.m_active = false;
        b.m_canOff = true;
        b.m_atable = true;
        r.m_bots.push_back(b);
    }
    return r;
}

} // namespace

TEST_CASE("BOTH1 2801 结果写进定长结构体", "[bothost]")
{
    const net::GroupBotsResult r = SampleResult(1);
    flamingobot::BotGroupCapsInfo caps;
    memset(&caps, 0, sizeof(caps));
    caps.m_cbSize = sizeof(caps);
    flamingobot::BotGroupItemInfo items[2] = { EmptyItem(), EmptyItem() };
    REQUIRE(FillGroupBotList(r, &caps, items, 2) == 1);
    CHECK(caps.m_isOwner);
    CHECK(caps.m_externalAllowed);
    CHECK(caps.m_groupBotMax == 5);
    CHECK(items[0].m_botId == 12);
    CHECK(items[0].m_userId == 5401);
    CHECK(items[0].m_isGroupBot);
    CHECK(std::wstring(items[0].m_name) == L"值班提醒");
    CHECK(std::wstring(items[0].m_glyph) == L"值");
    CHECK(std::wstring(items[0].m_color) == L"#43A047");
    CHECK(items[0].m_status == 1);
    CHECK(items[0].m_canOff);
    CHECK(items[0].m_atable);
    //没有用到的那一项保持原样
    CHECK(items[1].m_botId == 0);

    //截断：结尾补 0，不留半个代理项
    wchar_t shortBuf[4];
    CopyUtf8ToFixed(u8"一二三四五", shortBuf, 4);
    CHECK(std::wstring(shortBuf) == L"一二三");
    const std::string emoji = "\xF0\x9F\x98\x80";   //一个需要代理项对的字
    CopyUtf8ToFixed("ab" + emoji, shortBuf, 4);
    CHECK(std::wstring(shortBuf) == L"ab");
    CopyUtf8ToFixed("", shortBuf, 4);
    CHECK(shortBuf[0] == L'\0');
}

TEST_CASE("BOTH2 结构体大小不对时不写；个数多于容量时只填前几个", "[bothost]")
{
    const net::GroupBotsResult r = SampleResult(3);
    flamingobot::BotGroupItemInfo bad = EmptyItem();
    bad.m_cbSize = sizeof(bad) - 1;
    CHECK_FALSE(FillGroupBotItem(r.m_bots[0], bad));
    CHECK(bad.m_botId == 0);
    flamingobot::BotGroupCapsInfo badCaps;
    memset(&badCaps, 0, sizeof(badCaps));
    CHECK_FALSE(FillGroupBotCaps(r.m_caps, badCaps));

    flamingobot::BotGroupItemInfo items[2] = { EmptyItem(), EmptyItem() };
    CHECK(FillGroupBotList(r, NULL, items, 2) == 3);
    CHECK(items[1].m_botId == 13);
    CHECK(FillGroupBotList(r, NULL, NULL, 0) == 3);
    CHECK(FillGroupBotList(SampleResult(0), NULL, items, 2) == 0);
}

TEST_CASE("BOTH3 入口的显隐", "[bothost]")
{
    CHECK(IsBotUiAvailable(true, true, true, true));
    CHECK_FALSE(IsBotUiAvailable(false, true, true, true));
    CHECK_FALSE(IsBotUiAvailable(true, false, true, true));
    CHECK_FALSE(IsBotUiAvailable(true, true, false, true));
    CHECK_FALSE(IsBotUiAvailable(true, true, true, false));
    CHECK(IsMyBotsEntryVisible(true, true));
    CHECK_FALSE(IsMyBotsEntryVisible(true, false));
    CHECK_FALSE(IsMyBotsEntryVisible(false, true));
    CHECK(IsGroupManageEntryVisible(true, true));
    CHECK_FALSE(IsGroupManageEntryVisible(true, false));
    CHECK_FALSE(IsGroupManageEntryVisible(false, true));
}

TEST_CASE("BOTH4 DLL 收发的号段与 exe 的转交规则一致", "[bothost]")
{
    for (uint32_t cmd = 2790; cmd <= 2830; ++cmd)
    {
        INFO("cmd " << cmd);
        CHECK(flamingobot::IsBotDllCommand(cmd) == net::BotProtocolClient::isDllFrame(static_cast<uint16_t>(cmd)));
    }
    CHECK(flamingobot::kApiVersion >= 1);
}
