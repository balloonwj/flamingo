/**
 *  OnlineCapabilityTests.cpp
 *
 *  "当前状态下哪些功能可用"判定（OnlineCapability）的测试：
 *    · 三态推导 —— 在线且连接就绪 / 状态在线但连接未就绪（恢复中、断线重连中）/
 *      用户主动离线，以及隐身与越界 presence 的兜底。
 *    · 五类操作的放行矩阵 —— 在线放行、离线全拦、"连接未就绪"同样全拦。
 *    · 提示条文案与可点击性、被拦操作的提示文案完整性。
 *    · 托盘图标选择（离线用灰色图标）。
 *    · 自动重连守卫 —— 用户主动离线时恒不重连。
 *
 *  用 Catch2 v3（amalgamated）作框架，纯内存断言，不创建窗口 / 不依赖消息循环。
 *  全局 _Module 由 BalloonChatLayoutTests.cpp 统一定义，本文件只引用。
 *
 *  balloonwj@qq.com   2026-08-03
 */

#include "stdafx.h"

#include "catch_amalgamated.hpp"

#include <cwchar>          // wcscmp / wcsstr —— 比对中文提示文案

#include "../BalloonMain/OnlineCapability.h"
#include "../net/Protocol.h"   // presence_type

using namespace balloonmain;

// 除"离线"以外的三种在线状态 —— 它们在本模块里的行为完全一致，逐一遍历以防
// 将来只改了其中一种。
namespace {

const int kOnlineLikePresences[3] = { presence_online, presence_away, presence_busy };

// 五种操作类别，供放行矩阵逐类遍历。
const int kAllActions[5] =
{
    kActionSendMessage,
    kActionTransferFile,
    kActionShakeWindow,
    kActionRealtimeCall,
    kActionAccountOp,
};

} // anonymous namespace

// =====================================================================
// CAP1 ~ CAP3 —— 三态推导
// =====================================================================

TEST_CASE("CAP1 在线/离开/忙碌 + 连接就绪 = 在线态", "[onlinecap]")
{
    for (int i = 0; i < 3; ++i)
    {
        INFO("presence = " << kOnlineLikePresences[i]);
        CHECK(ComputeConnectionUiState(kOnlineLikePresences[i], true) == kConnUiOnline);
    }
}

TEST_CASE("CAP2 在线/离开/忙碌 + 连接未就绪 = 连接中态", "[onlinecap]")
{
    // 覆盖两种情形：刚从离线切回在线、正在重新登录；以及网络掉了正在自动重连。
    // 对本模块而言两者输入相同，都归到"连接中"。
    for (int i = 0; i < 3; ++i)
    {
        INFO("presence = " << kOnlineLikePresences[i]);
        CHECK(ComputeConnectionUiState(kOnlineLikePresences[i], false) == kConnUiConnecting);
    }
}

TEST_CASE("CAP3 离线态与未知取值的兜底", "[onlinecap]")
{
    // 用户主动离线：connected 无论真假都是离线态 —— 离线是用户的选择，不是连接的结果。
    CHECK(ComputeConnectionUiState(presence_offline, false) == kConnUiOffline);
    CHECK(ComputeConnectionUiState(presence_offline, true)  == kConnUiOffline);

    // 隐身与越界取值按离线处理，与 NormalizePresenceForMenu 的兜底口径一致。
    CHECK(ComputeConnectionUiState(presence_invisible, true) == kConnUiOffline);
    CHECK(ComputeConnectionUiState(999, true)                == kConnUiOffline);
    CHECK(ComputeConnectionUiState(-1, true)                 == kConnUiOffline);
}

// =====================================================================
// CAP4 ~ CAP6 —— 五类操作的放行矩阵
// =====================================================================

TEST_CASE("CAP4 在线且连接就绪时五类操作全部放行", "[onlinecap][action]")
{
    for (int i = 0; i < 3; ++i)
    {
        for (int a = 0; a < 5; ++a)
        {
            INFO("presence = " << kOnlineLikePresences[i] << ", action = " << kAllActions[a]);
            CHECK(IsActionAllowed(kOnlineLikePresences[i], true, kAllActions[a]));
        }
    }
}

TEST_CASE("CAP5 离线时五类操作全部拦住", "[onlinecap][action]")
{
    for (int a = 0; a < 5; ++a)
    {
        INFO("action = " << kAllActions[a]);
        CHECK(!IsActionAllowed(presence_offline, false, kAllActions[a]));
        // 即便连接侥幸还在（正在关闭的瞬间），用户选了离线就不该再放行。
        CHECK(!IsActionAllowed(presence_offline, true, kAllActions[a]));
        // 隐身与越界 presence 同样按离线拦住。
        CHECK(!IsActionAllowed(presence_invisible, true, kAllActions[a]));
        CHECK(!IsActionAllowed(999, true, kAllActions[a]));
    }
}

TEST_CASE("CAP6 状态是在线但连接未就绪时同样全部拦住", "[onlinecap][action]")
{
    // 这条覆盖"从离线切回在线、登录还没完成"以及"断线重连中"两个中间态 ——
    // 此时胶囊已经显示在线，但消息发出去也到不了服务器，必须拦。
    for (int i = 0; i < 3; ++i)
    {
        for (int a = 0; a < 5; ++a)
        {
            INFO("presence = " << kOnlineLikePresences[i] << ", action = " << kAllActions[a]);
            CHECK(!IsActionAllowed(kOnlineLikePresences[i], false, kAllActions[a]));
        }
    }
}

TEST_CASE("CAP7 越界操作类别一律不放行", "[onlinecap][action]")
{
    // 保守兜底：新增了操作类别却忘了登记时，宁可误拦也不要默默放出去。
    CHECK(!IsActionAllowed(presence_online, true, -1));
    CHECK(!IsActionAllowed(presence_online, true, kActionKindCount));
    CHECK(!IsActionAllowed(presence_online, true, 999));
}

// =====================================================================
// CAP8 ~ CAP10 —— 提示文案
// =====================================================================

TEST_CASE("CAP8 五类操作各有非空提示文案，且都点明离线", "[onlinecap][text]")
{
    // 新增类别时若漏配文案，本条会失败。
    for (int a = 0; a < 5; ++a)
    {
        INFO("action = " << kAllActions[a]);
        const wchar_t* tip = GetBlockedActionTip(kAllActions[a]);
        REQUIRE(tip != NULL);
        CHECK(::wcslen(tip) > 0);
        CHECK(::wcsstr(tip, L"离线") != NULL);
    }

    // 越界类别也要有通用兜底文案，不能返回空指针。
    const wchar_t* fallback = GetBlockedActionTip(999);
    REQUIRE(fallback != NULL);
    CHECK(::wcslen(fallback) > 0);
}

TEST_CASE("CAP9 提示条文案：在线不显示，离线与连接中各有文案", "[onlinecap][text]")
{
    // 在线态返回 NULL，表示不显示提示条。
    CHECK(GetConnectionBannerText(kConnUiOnline) == NULL);

    const wchar_t* offlineText = GetConnectionBannerText(kConnUiOffline);
    REQUIRE(offlineText != NULL);
    CHECK(::wcsstr(offlineText, L"离线") != NULL);
    // 离线文案要告诉用户离线期间的消息不会丢，免得他以为这段时间的消息没了。
    CHECK(::wcsstr(offlineText, L"恢复在线后收到") != NULL);
    // 提示带不可点击，文案里不得再有"点此"这类诱导点击的措辞。
    CHECK(::wcsstr(offlineText, L"点此") == NULL);

    const wchar_t* connectingText = GetConnectionBannerText(kConnUiConnecting);
    REQUIRE(connectingText != NULL);
    CHECK(::wcslen(connectingText) > 0);
    // 连接中不能说"已离线"，否则用户以为要自己动手恢复。
    CHECK(::wcsstr(connectingText, L"正在连接") != NULL);
}

// CAP10 原先验证"只有离线态的提示条可点击"。提示带改为纯告知、整条不可点之后，
// IsConnectionBannerClickable 连同两个聊天窗里的点击分支一并删除，该用例随之移除。

// =====================================================================
// CAP11 —— 托盘图标选择
// =====================================================================

TEST_CASE("CAP11 只有真正在线才用彩色托盘图标", "[onlinecap][tray]")
{
    CHECK(::wcscmp(GetTrayIconFileName(kConnUiOnline), L"imonline.ico") == 0);

    // 主动离线固然是灰的；"连接尚未就绪"（从离线恢复中 / 断线重连中）同样要灰 ——
    // 此刻收发不了消息，显示彩色图标会误导用户以为一切正常。
    CHECK(::wcscmp(GetTrayIconFileName(kConnUiOffline), L"offline.ico") == 0);
    CHECK(::wcscmp(GetTrayIconFileName(kConnUiConnecting), L"offline.ico") == 0);
}

TEST_CASE("CAP11b 托盘图标与在线状态的组合结果", "[onlinecap][tray]")
{
    // 从"在线状态 + 连接是否就绪"一路推到图标，覆盖用户实际会遇到的几种组合。
    CHECK(::wcscmp(GetTrayIconFileName(
        ComputeConnectionUiState(presence_online, true)), L"imonline.ico") == 0);
    CHECK(::wcscmp(GetTrayIconFileName(
        ComputeConnectionUiState(presence_busy, true)), L"imonline.ico") == 0);

    // 用户主动选了离线 —— 这正是本次要求"托盘变灰"的场景。
    CHECK(::wcscmp(GetTrayIconFileName(
        ComputeConnectionUiState(presence_offline, false)), L"offline.ico") == 0);

    // 掉线重连中：状态还是在线，但连不上，图标仍为灰。
    CHECK(::wcscmp(GetTrayIconFileName(
        ComputeConnectionUiState(presence_online, false)), L"offline.ico") == 0);

    // 隐身与越界取值按离线处理。
    CHECK(::wcscmp(GetTrayIconFileName(
        ComputeConnectionUiState(presence_invisible, true)), L"offline.ico") == 0);
    CHECK(::wcscmp(GetTrayIconFileName(
        ComputeConnectionUiState(999, true)), L"offline.ico") == 0);
}

// =====================================================================
// CAP12 —— 自动重连守卫
// =====================================================================

TEST_CASE("CAP12 用户主动离线时恒不自动重连", "[onlinecap][reconnect]")
{
    // 这是本函数要修的漏洞：原先"掉线事件"处判了用户是否主动离线，"重连定时器触发"
    // 处没判，导致正在重连时选离线仍会被连回去。共用本函数即可杜绝。
    //
    // 目前共有三个调用点，新增第四个时也必须走本函数、不要再各判各的：
    //   · CMainDlg::OnNetError       —— 掉线事件到达，决定要不要起重连定时器；
    //   · CMainDlg::OnTimer          —— 重连定时器每次触发，决定要不要真的去登录；
    //   · CMainDlg::OnNetworkResumed —— 本机网络由断开恢复，决定要不要趁机连回服务器。
    // 第三个调用点是后补的：它原先不判登录态，网络一恢复就重新登录，于是用户主动离线、
    // 或账号在别处登录被顶下线之后，拔插一次网线就会自己连回去。
    //
    // "被踢下线"与"用户主动离线"在这里是同一种情形 —— 两者都会把面板状态置成
    // MAINPANEL_STATUS_USERGOOFFLINE，对"要不要连回去"的答案都是不要。
    CHECK(!ShouldAutoReconnect(true, true));    // 主动离线：配置允许重连也不连
    CHECK(!ShouldAutoReconnect(true, false));

    // 非主动离线（真掉线）时才看配置。
    CHECK(ShouldAutoReconnect(false, true));
    CHECK(!ShouldAutoReconnect(false, false));  // 配置里关掉了自动重连
}

// =====================================================================
// CAP13 ~ CAP15 —— 状态胶囊 / 托盘提示的文字，以及头像置灰
//
// 这三条针对的是"掉线时托盘图标已经转灰，但托盘悬停提示和主面板状态胶囊仍写着
// 在线"这个缺陷：图标按连接态渲染、文字按用户选的状态渲染，两个口径在掉线时分叉。
// =====================================================================

TEST_CASE("CAP13 连接就绪时胶囊显示用户自己选的状态", "[onlinecap][presence]")
{
    // 连接就绪这一路应当与只看在线状态的 GetPresenceVisual 完全一致 —— 不能因为
    // 引入连接态就把正常在线时的表现改掉了。
    for (int i = 0; i < 3; ++i)
    {
        const int presence = kOnlineLikePresences[i];
        INFO("presence = " << presence);

        const PresenceVisual expected = GetPresenceVisual(presence);
        const PresenceVisual actual   = GetConnectionPresenceVisual(presence, kConnUiOnline);

        CHECK(::wcscmp(actual.text, expected.text) == 0);
        CHECK(actual.dotColor == expected.dotColor);
    }

    // 三种状态的文字确实是三个不同的词，别被上面"与 GetPresenceVisual 一致"蒙混过去。
    CHECK(::wcscmp(GetConnectionPresenceVisual(presence_online, kConnUiOnline).text, L"在线") == 0);
    CHECK(::wcscmp(GetConnectionPresenceVisual(presence_away,   kConnUiOnline).text, L"离开") == 0);
    CHECK(::wcscmp(GetConnectionPresenceVisual(presence_busy,   kConnUiOnline).text, L"忙碌") == 0);
}

TEST_CASE("CAP14 掉线 / 重连中显示正在连接，主动离线显示离线", "[onlinecap][presence]")
{
    // 本次缺陷的核心用例：用户选的仍是在线 / 离开 / 忙碌，但连接没就绪，
    // 三者都该显示"正在连接"，而<u>不是</u>照抄用户选的那个状态。
    for (int i = 0; i < 3; ++i)
    {
        const int presence = kOnlineLikePresences[i];
        INFO("presence = " << presence);

        const PresenceVisual v = GetConnectionPresenceVisual(presence, kConnUiConnecting);
        CHECK(::wcscmp(v.text, L"正在连接") == 0);
        // 圆点色与"离线"同为灰，与灰色托盘图标观感一致。
        CHECK(v.dotColor == GetPresenceVisual(presence_offline).dotColor);
    }

    // 用户主动选了离线：显示"离线"，不是"正在连接"——他并没有在等着连上。
    const PresenceVisual off = GetConnectionPresenceVisual(presence_offline, kConnUiOffline);
    CHECK(::wcscmp(off.text, L"离线") == 0);
    CHECK(off.dotColor == GetPresenceVisual(presence_offline).dotColor);

    // 越界的连接态按离线兜底，绝不能落回"在线"。
    const PresenceVisual bad = GetConnectionPresenceVisual(presence_online, 999);
    CHECK(::wcscmp(bad.text, L"离线") == 0);

    // 隐身等本菜单表达不了的 presence，在连接就绪时也退化为"离线"（沿用既有兜底口径）。
    CHECK(::wcscmp(
        GetConnectionPresenceVisual(presence_invisible, kConnUiOnline).text, L"离线") == 0);
}

TEST_CASE("CAP15 头像置灰与托盘图标同一个口径", "[onlinecap][presence]")
{
    CHECK(!ShouldGrayAvatar(kConnUiOnline));
    CHECK(ShouldGrayAvatar(kConnUiConnecting));
    CHECK(ShouldGrayAvatar(kConnUiOffline));
    CHECK(ShouldGrayAvatar(999));              // 越界值保守置灰
}

// =====================================================================
// CAP16 —— 三处口径一致性
//
// 这条是专门用来防"这次这类回归"的：托盘图标、胶囊文字、头像置灰三处将来只要有
// 谁被单独改动、口径又分叉了，本用例立刻失败。缺陷当初正是从这个分叉来的。
// =====================================================================

TEST_CASE("CAP16 托盘彩色图标 / 胶囊在线文字 / 头像不灰，三者永远同真同假",
          "[onlinecap][presence][tray]")
{
    // 遍历所有可能的 presence（含隐身与越界）× 连接是否就绪。
    const int kAllPresences[6] =
    {
        presence_online, presence_away, presence_busy,
        presence_offline, presence_invisible, 999,
    };
    const bool kConnectedValues[2] = { true, false };

    for (int i = 0; i < 6; ++i)
    {
        for (int j = 0; j < 2; ++j)
        {
            const int  presence  = kAllPresences[i];
            const bool connected = kConnectedValues[j];
            INFO("presence = " << presence << ", connected = " << connected);

            const int state = (int)ComputeConnectionUiState(presence, connected);

            const bool bTrayColored = (::wcscmp(GetTrayIconFileName(state), L"imonline.ico") == 0);
            const bool bAvatarColor = !ShouldGrayAvatar(state);

            const wchar_t* pillText = GetConnectionPresenceVisual(presence, state).text;
            const bool bPillOnline  = (::wcscmp(pillText, L"在线") == 0)
                                   || (::wcscmp(pillText, L"离开") == 0)
                                   || (::wcscmp(pillText, L"忙碌") == 0);

            CHECK(bTrayColored == bAvatarColor);
            CHECK(bTrayColored == bPillOnline);
        }
    }
}
