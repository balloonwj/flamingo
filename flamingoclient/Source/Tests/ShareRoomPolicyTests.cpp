/**
 *  屏幕共享房间规则的单元测试（标签 [shareroom]），ShareRoomPolicyTests.cpp
 *
 *  覆盖 FlamingoMedia/Share/ShareRoomPolicy.h 与 SharePickerPolicy.h 的第 13 期部分（docs/屏幕共享与录制方案.md 第 13 期）：
 *    - SSDURC1 共享时长的文字（不满 1 小时 mm:ss、满 1 小时 h:mm:ss）与计时（以 elapsedms 为起点加本机流逝的时间）；
 *    - SSVL1   观看者名单：去掉发起人、按座位排序、显示名查不到时写「用户<id>」、状态文字、只有发起人且对方开着麦克风时
 *              「静音」可点、「N 人观看 ▾」的文字；
 *    - SSMUC2  被发起人单独静音：只有事件 9、被静音的是本人、本人麦克风开着时才关麦；
 *    - SSFMC1  强制模式：观看窗没有「离开」「退出全屏」，Esc 与关窗无效，置顶并拉回前台；加入前确认（回到观看时除外）；
 *              选择窗只有发起共享时提供「强制模式」「禁止观看者录制」；
 *    - SSRMC2  第 14 期的文字：强制模式下的右下角提示写明效果、被移出后的提示（区分能否再加入）、移出确认的提问；
 *    - SSRMC1b 第 14 期：名单里只有发起人有「移出」，1729 结束原因 3 / 4 表示本人被移出；
 *    - SSTIC1 / SSTIC2 / SSTIC4 第 15 期：共享标题的默认值与校验，带标题的提示、观看窗标题、控制条状态与强制加入确认；
 *    - SSTIC6  第 15 期：选择窗只有发起共享与通话中发起时提供标题与描述；
 *    - SSTIC8 / SSTIC9 / SSTIC10 第 15 期：标题转 UTF-8、各场共享的标题与描述缓存、右下角提示的行数。
 *  框架 Catch2 v3。
 *
 *  典型用法：FlamingoUnit.exe "[shareroom]"
 *
 *  balloonwj@qq.com   2026-10-03
 */
#include "stdafx.h"
#include "catch_amalgamated.hpp"

#include <map>
#include <set>
#include <string>
#include <vector>

#include "../../FlamingoMedia/Share/ShareRoomPolicy.h"
#include "../../FlamingoMedia/Share/SharePickerPolicy.h"

using namespace screenshare;

namespace {

//一个房间快照里的成员
ShareMember ssrMember(int64_t userId, int seat, bool mic)
{
    ShareMember m;
    m.m_userId = userId;
    m.m_seat = seat;
    m.m_mic = mic;
    return m;
}

//发起人与三个观看者
const int64_t kSsrHost = 5127;
const int64_t kSsrLi = 5308;
const int64_t kSsrWang = 5010;
const int64_t kSsrZhao = 5001;

//本机时刻的一个起点（GetTickCount64 毫秒）
const uint64_t kSsrTick0 = 1000000;

} // namespace

TEST_CASE("SSDURC1 共享时长的文字与计时", "[shareroom]")
{
    CHECK(FormatShareDuration(0) == L"00:00");
    CHECK(FormatShareDuration(65) == L"01:05");
    CHECK(FormatShareDuration(3599) == L"59:59");
    CHECK(FormatShareDuration(3600) == L"1:00:00");
    CHECK(FormatShareDuration(3725) == L"1:02:05");
    CHECK(FormatShareDuration(36000 + 59) == L"10:00:59");
    CHECK(FormatShareDuration(-3) == L"00:00");

    ShareDurationClock clock;
    CHECK_FALSE(clock.IsRunning());
    CHECK(clock.ElapsedSeconds(kSsrTick0) == 0);
    //加入时共享已进行 65.5 秒，本机又过了 2 秒
    clock.Start(65500, kSsrTick0);
    CHECK(clock.IsRunning());
    CHECK(clock.ElapsedSeconds(kSsrTick0) == 65);
    CHECK(clock.ElapsedSeconds(kSsrTick0 + 2000) == 67);
    //本机时刻早于开始时刻（不应出现）：按开始时刻算
    CHECK(clock.ElapsedSeconds(kSsrTick0 - 500) == 65);
    //宽限期内重新登录后收到新的 1726：以最近一次为准
    clock.Start(120000, kSsrTick0 + 10000);
    CHECK(clock.ElapsedSeconds(kSsrTick0 + 11000) == 121);
    clock.Stop();
    CHECK_FALSE(clock.IsRunning());
    CHECK(clock.ElapsedSeconds(kSsrTick0 + 20000) == 0);
    //负数按 0
    clock.Start(-100, kSsrTick0);
    CHECK(clock.ElapsedSeconds(kSsrTick0 + 1000) == 1);
}

TEST_CASE("SSVL1 观看者名单", "[shareroom]")
{
    ShareStateNotify state;
    state.m_hostId = kSsrHost;
    //故意打乱顺序：名单按座位号排序
    state.m_members.push_back(ssrMember(kSsrZhao, 3, true));
    state.m_members.push_back(ssrMember(kSsrHost, 0, true));
    state.m_members.push_back(ssrMember(kSsrLi, 1, true));
    state.m_members.push_back(ssrMember(kSsrWang, 2, false));

    std::map<int64_t, std::wstring> names;
    names[kSsrLi] = L"李四";
    names[kSsrWang] = L"王五";
    std::set<int> speaking;
    speaking.insert(1);

    std::vector<ShareViewerRow> rows;
    BuildShareViewerRows(state, names, speaking, true, rows);
    REQUIRE(rows.size() == 3);
    //不含发起人，按座位排序
    CHECK(rows[0].m_userId == kSsrLi);
    CHECK(rows[1].m_userId == kSsrWang);
    CHECK(rows[2].m_userId == kSsrZhao);
    CHECK(rows[0].m_seat == 1);
    CHECK(rows[0].m_name == L"李四");
    //查不到显示名
    CHECK(rows[2].m_name == L"用户5001");
    //状态文字与「静音」能否点
    CHECK(rows[0].m_speaking);
    CHECK(ShareViewerStatusText(rows[0]) == L"正在说话");
    CHECK(rows[0].m_canMute);
    CHECK_FALSE(rows[1].m_micOn);
    CHECK(ShareViewerStatusText(rows[1]) == L"麦克风已关");
    CHECK_FALSE(rows[1].m_canMute);
    CHECK(ShareViewerStatusText(rows[2]) == L"麦克风开着");
    CHECK(rows[2].m_canMute);

    //观看者看到的名单：没有「静音」
    BuildShareViewerRows(state, names, speaking, false, rows);
    REQUIRE(rows.size() == 3);
    CHECK_FALSE(rows[0].m_canMute);
    CHECK_FALSE(rows[2].m_canMute);

    //只有发起人：名单为空
    ShareStateNotify alone;
    alone.m_hostId = kSsrHost;
    alone.m_members.push_back(ssrMember(kSsrHost, 0, false));
    BuildShareViewerRows(alone, names, speaking, true, rows);
    CHECK(rows.empty());

    CHECK(ShareViewerCountText(3) == L"3 人观看 ▾");
    CHECK(ShareViewerCountText(1) == L"1 人观看 ▾");
    CHECK(ShareViewerCountText(0) == L"暂无观看者 ▾");
    CHECK(ShareViewerCountText(-1) == L"暂无观看者 ▾");
}

TEST_CASE("SSMUC2 被发起人单独静音时才关掉本人麦克风", "[shareroom]")
{
    CHECK(ShouldMuteSelfByHost(share_event_member_muted, kSsrLi, kSsrLi, true));
    //静音的是别人
    CHECK_FALSE(ShouldMuteSelfByHost(share_event_member_muted, kSsrWang, kSsrLi, true));
    //本人麦克风本来就关着
    CHECK_FALSE(ShouldMuteSelfByHost(share_event_member_muted, kSsrLi, kSsrLi, false));
    //其它事件（含全员静音，由原有逻辑处理）
    CHECK_FALSE(ShouldMuteSelfByHost(share_event_muted_all, kSsrLi, kSsrLi, true));
    CHECK_FALSE(ShouldMuteSelfByHost(share_event_media_changed, kSsrLi, kSsrLi, true));
    CHECK(share_event_member_muted == 9);
}

TEST_CASE("SSFMC1 强制模式的观看窗、加入确认与选择窗选项", "[shareroom]")
{
    const ShareViewerChrome normal = MakeShareViewerChrome(false);
    CHECK(normal.m_showLeave);
    CHECK(normal.m_showExitFullscreen);
    CHECK(normal.m_escExitsFullscreen);
    CHECK(normal.m_closeLeaves);
    CHECK_FALSE(normal.m_topmost);
    CHECK_FALSE(normal.m_pullBackToFront);

    const ShareViewerChrome forced = MakeShareViewerChrome(true);
    CHECK_FALSE(forced.m_showLeave);
    CHECK_FALSE(forced.m_showExitFullscreen);
    CHECK_FALSE(forced.m_escExitsFullscreen);
    CHECK_FALSE(forced.m_closeLeaves);
    CHECK(forced.m_topmost);
    CHECK(forced.m_pullBackToFront);

    //加入前确认：强制模式要确认；掉线后回到观看不确认；普通共享不确认
    CHECK(ShouldConfirmForcedJoin(true, false));
    CHECK_FALSE(ShouldConfirmForcedJoin(true, true));
    CHECK_FALSE(ShouldConfirmForcedJoin(false, false));

    //选择窗：只有发起共享时提供「强制模式」「禁止观看者录制」
    CHECK(SharePickerOffersForced(share_picker_start));
    CHECK_FALSE(SharePickerOffersForced(share_picker_switch));
    CHECK_FALSE(SharePickerOffersForced(share_picker_record));
    CHECK_FALSE(SharePickerOffersForced(share_picker_start_in_call));
    CHECK(SharePickerOffersRecordBlock(share_picker_start));
    CHECK_FALSE(SharePickerOffersRecordBlock(share_picker_switch));
    CHECK_FALSE(SharePickerOffersRecordBlock(share_picker_record));
    CHECK_FALSE(SharePickerOffersRecordBlock(share_picker_start_in_call));
}

TEST_CASE("SSRMC2 强制模式提示、被移出提示与移出确认的文字", "[shareroom]")
{
    //右下角提示：普通、需要密码、强制模式、两者都有、不知道发起人名
    CHECK(MakeShareStartedToastText(L"韩雪飞", false, false) == L"韩雪飞 正在共享屏幕");
    CHECK(MakeShareStartedToastText(L"韩雪飞", true, false) == L"韩雪飞 正在共享屏幕（需要密码）");
    CHECK(MakeShareStartedToastText(L"韩雪飞", false, true) ==
          L"韩雪飞 正在共享屏幕。这是强制模式：加入后全屏观看、不能中途退出，直到对方结束共享。");
    CHECK(MakeShareStartedToastText(L"韩雪飞", true, true) ==
          L"韩雪飞 正在共享屏幕（需要密码）。这是强制模式：加入后全屏观看、不能中途退出，直到对方结束共享。");
    CHECK(MakeShareStartedToastText(L"", false, false) == L"对方 正在共享屏幕");

    //被移出：能否再加入
    CHECK(MakeShareRemovedNotice(L"韩雪飞", true) == L"你已被 韩雪飞 移出屏幕共享。");
    CHECK(MakeShareRemovedNotice(L"韩雪飞", false) == L"你已被 韩雪飞 移出屏幕共享，不能再加入这场共享。");
    CHECK(MakeShareRemovedNotice(L"", true) == L"你已被 发起人 移出屏幕共享。");

    //移出确认
    CHECK(MakeShareRemovePrompt(L"李四") == L"确定将 李四 移出屏幕共享吗？");
    CHECK(MakeShareRemovePrompt(L"") == L"确定将 这位观看者 移出屏幕共享吗？");
}

TEST_CASE("SSRMC1b 名单里只有发起人有「移出」；结束原因 3 / 4 表示本人被移出", "[shareroom]")
{
    ShareStateNotify state;
    state.m_hostId = kSsrHost;
    state.m_members.push_back(ssrMember(kSsrHost, 0, true));
    state.m_members.push_back(ssrMember(kSsrLi, 1, true));
    state.m_members.push_back(ssrMember(kSsrWang, 2, false));
    std::map<int64_t, std::wstring> names;
    std::set<int> speaking;

    //发起人：每一行都能移出，与对方开没开麦克风无关
    std::vector<ShareViewerRow> rows;
    BuildShareViewerRows(state, names, speaking, true, rows);
    REQUIRE(rows.size() == 2);
    CHECK(rows[0].m_canRemove);
    CHECK(rows[1].m_canRemove);
    //观看者：都不能
    BuildShareViewerRows(state, names, speaking, false, rows);
    REQUIRE(rows.size() == 2);
    CHECK_FALSE(rows[0].m_canRemove);
    CHECK_FALSE(rows[1].m_canRemove);

    CHECK(IsShareRemovedReason(share_end_removed));
    CHECK(IsShareRemovedReason(share_end_removed_banned));
    CHECK_FALSE(IsShareRemovedReason(share_end_host_stopped));
    CHECK_FALSE(IsShareRemovedReason(share_end_host_timeout));
    CHECK_FALSE(IsShareRemovedReason(0));
}

TEST_CASE("SSTIC1 选择窗里共享标题的默认值", "[shareroom]")
{
    CHECK(MakeDefaultShareTitle(L"韩雪飞") == L"韩雪飞的屏幕共享");
    CHECK(MakeDefaultShareTitle(L"") == L"我的屏幕共享");
}

TEST_CASE("SSTIC2 共享标题必填、去掉首尾空白后 1～30 字，描述最多 100 字，表情按一个字算", "[shareroom]")
{
    std::wstring title = L"  周会演示\u3000";
    std::wstring desc = L" 汇报 ";
    CHECK(NormalizeShareInfo(title, desc));
    CHECK(title == L"周会演示");
    CHECK(desc == L"汇报");

    //空的、只有空白：不合格
    title.clear();
    desc.clear();
    CHECK_FALSE(NormalizeShareInfo(title, desc));
    title = L"\u3000 \t";
    CHECK_FALSE(NormalizeShareInfo(title, desc));

    //30 个汉字可以，31 个不行
    title = std::wstring(30, L'周');
    desc.clear();
    CHECK(NormalizeShareInfo(title, desc));
    title = std::wstring(31, L'周');
    CHECK_FALSE(NormalizeShareInfo(title, desc));

    //表情是 UTF-16 代理对，按一个字算：30 个表情可以
    std::wstring emoji;
    for (int i = 0; i < 30; ++i)
        emoji += L"\U0001F600";
    CHECK(emoji.size() == 60);
    CHECK(CountShareInfoChars(emoji) == 30);
    title = emoji;
    CHECK(NormalizeShareInfo(title, desc));

    //描述 100 字可以，101 字不行
    title = L"t";
    desc = std::wstring(100, L'述');
    CHECK(NormalizeShareInfo(title, desc));
    desc = std::wstring(101, L'述');
    CHECK_FALSE(NormalizeShareInfo(title, desc));

    CHECK(kShareTitleMaxChars == 30);
    CHECK(kShareDescMaxChars == 100);
}

TEST_CASE("SSTIC4 带共享标题的文字：右下角提示、观看窗标题、控制条状态、强制加入确认", "[shareroom]")
{
    //右下角提示
    CHECK(MakeShareStartedToastText(L"韩雪飞", L"周会演示", false, false) == L"韩雪飞 正在共享：周会演示");
    CHECK(MakeShareStartedToastText(L"韩雪飞", L"周会演示", true, false) == L"韩雪飞 正在共享：周会演示（需要密码）");
    CHECK(MakeShareStartedToastText(L"韩雪飞", L"周会演示", false, true) ==
          L"韩雪飞 正在共享：周会演示。这是强制模式：加入后全屏观看、不能中途退出，直到对方结束共享。");
    CHECK(MakeShareStartedToastText(L"", L"周会演示", false, false) == L"对方 正在共享：周会演示");
    //没有标题（老服务端）：与第 14 期相同
    CHECK(MakeShareStartedToastText(L"韩雪飞", L"", false, false) == L"韩雪飞 正在共享屏幕");

    //观看窗标题
    CHECK(MakeShareViewerCaption(L"周会演示", L"韩雪飞") == L"周会演示");
    CHECK(MakeShareViewerCaption(L"", L"韩雪飞") == L"韩雪飞 的屏幕共享");
    CHECK(MakeShareViewerCaption(L"", L"") == L"屏幕共享");

    //控制条：共享标题写在标题栏（联调后由状态行挪来，长标题不再挤掉共享时长），状态行不带标题
    CHECK(MakeShareBarCaption(L"周会演示：第三季度路线图") == L"屏幕共享 · 周会演示：第三季度路线图");
    CHECK(MakeShareBarCaption(L"") == L"屏幕共享");
    CHECK(MakeShareBarStatusText(false, L"12:34", L"", false, L"") == L"正在共享 12:34");
    CHECK(MakeShareBarStatusText(true, L"12:34", L"482915", true, L"李四 正在说话") ==
          L"已暂停 12:34 · 密码 482915 · 强制模式 · 李四 正在说话");
    CHECK(MakeShareBarStatusText(false, L"", L"", false, L"") == L"正在共享");

    //强制加入确认
    const std::wstring forcedText =
        L"这是强制观看的屏幕共享：加入后要等发起人结束共享才能离开，观看窗会一直全屏显示在最前面。\n确定要加入吗？";
    CHECK(MakeForcedJoinConfirmText(L"周会演示", L"第三季度汇报") == L"「周会演示」\n第三季度汇报\n\n" + forcedText);
    CHECK(MakeForcedJoinConfirmText(L"周会演示", L"") == L"「周会演示」\n\n" + forcedText);
    CHECK(MakeForcedJoinConfirmText(L"", L"") == forcedText);
}

TEST_CASE("SSTIC6 选择窗只有发起共享与通话中发起时提供标题与描述", "[shareroom]")
{
    CHECK(SharePickerOffersInfo(share_picker_start));
    CHECK(SharePickerOffersInfo(share_picker_start_in_call));
    CHECK_FALSE(SharePickerOffersInfo(share_picker_switch));
    CHECK_FALSE(SharePickerOffersInfo(share_picker_record));
}

TEST_CASE("SSTIC8 共享标题转成协议里用的 UTF-8，表情不丢", "[shareroom]")
{
    CHECK(ShareTextToUtf8(L"周会演示") == u8"周会演示");
    CHECK(ShareTextToUtf8(L"abc") == "abc");
    CHECK(ShareTextToUtf8(L"\U0001F600") == "\xF0\x9F\x98\x80");
    CHECK(ShareTextToUtf8(L"").empty());
}

TEST_CASE("SSTIC9 各场共享的标题与描述缓存：不带标题时不覆盖，结束后忘掉", "[shareroom]")
{
    ShareRoomInfoCache cache;
    std::wstring title;
    std::wstring desc;
    CHECK_FALSE(cache.Find("r1", title, desc));
    cache.Remember("r1", L"周会演示", L"第三季度汇报");
    REQUIRE(cache.Find("r1", title, desc));
    CHECK(title == L"周会演示");
    CHECK(desc == L"第三季度汇报");
    //老服务端不带标题：不覆盖
    cache.Remember("r1", L"", L"");
    REQUIRE(cache.Find("r1", title, desc));
    CHECK(title == L"周会演示");
    //发起人改了标题与描述（描述可以改成空）
    cache.Remember("r1", L"新标题", L"");
    REQUIRE(cache.Find("r1", title, desc));
    CHECK(title == L"新标题");
    CHECK(desc.empty());
    //房间号为空：不记
    cache.Remember("", L"t", L"d");
    CHECK_FALSE(cache.Find("", title, desc));
    //结束后忘掉；其它场不受影响
    cache.Remember("r2", L"另一场", L"");
    cache.Forget("r1");
    CHECK_FALSE(cache.Find("r1", title, desc));
    CHECK(cache.Find("r2", title, desc));
    cache.Clear();
    CHECK_FALSE(cache.Find("r2", title, desc));
}

TEST_CASE("SSTIC10 右下角提示的行数：普通一行，强制模式三行，有描述再加两行", "[shareroom]")
{
    CHECK(ShareToastTextLines(false, false) == 1);
    CHECK(ShareToastTextLines(true, false) == 3);
    CHECK(ShareToastTextLines(false, true) == 3);
    CHECK(ShareToastTextLines(true, true) == 5);
}
