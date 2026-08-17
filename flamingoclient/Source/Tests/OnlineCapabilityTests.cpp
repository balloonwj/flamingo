/**
 *  OnlineCapabilityTests.cpp
 *
 *  "当前状态下哪些功能可用"判定（OnlineCapability）的测试：
 *    · 三态推导 —— 在线且连接就绪 / 状态在线但连接未就绪（恢复中、断线重连中）/
 *      用户主动离线，以及越界 presence 的兜底。
 *    · 各类操作的放行矩阵 —— 在线放行、离线全拦、"连接未就绪"同样全拦。
 *    · 提示条文案与可点击性、被拦操作的提示文案完整性。
 *    · 托盘图标选择（离线用灰色图标）。
 *    · 托盘状态徽标叠加 —— 只有"连接就绪 + 离开 / 忙碌"才在图标右下角叠徽标。
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

// 全部操作类别，供放行矩阵逐类遍历。数组长度取 kActionKindCount；新增类别时必须
// 同时往这里补一行 —— 漏补只会被零填充（0 恰好是一个合法类别，看不出异常），
// 故另有 CAP20 一条专门核对"每个类别都确实被列了进来"。
const int kAllActions[kActionKindCount] =
{
    kActionSendMessage,
    kActionTransferFile,
    kActionShakeWindow,
    kActionRealtimeCall,
    kActionAccountOp,
    kActionChangePresence,
};

} // anonymous namespace

// =====================================================================
// CAP1 ~ CAP3 —— 三态推导
// =====================================================================

// 已作废的在线状态取值：4 曾经是「隐身」，本产品不提供该功能（见 net/Protocol.h）。
// 老服务端仍可能推上来，故保留这些用例，确认它被当作越界值归到"离线"。
static const int kRetiredPresence = 4;
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

    // 越界取值按离线处理，与 NormalizePresenceForMenu 的兜底口径一致。
    CHECK(ComputeConnectionUiState(kRetiredPresence, true) == kConnUiOffline);
    CHECK(ComputeConnectionUiState(999, true)                == kConnUiOffline);
    CHECK(ComputeConnectionUiState(-1, true)                 == kConnUiOffline);
}

// =====================================================================
// CAP4 ~ CAP6 —— 各类操作的放行矩阵
// =====================================================================

TEST_CASE("CAP4 在线且连接就绪时各类操作全部放行", "[onlinecap][action]")
{
    for (int i = 0; i < 3; ++i)
    {
        for (int a = 0; a < kActionKindCount; ++a)
        {
            INFO("presence = " << kOnlineLikePresences[i] << ", action = " << kAllActions[a]);
            CHECK(IsActionAllowed(kOnlineLikePresences[i], true, kAllActions[a]));
        }
    }
}

TEST_CASE("CAP5 离线时各类操作全部拦住", "[onlinecap][action]")
{
    for (int a = 0; a < kActionKindCount; ++a)
    {
        INFO("action = " << kAllActions[a]);
        CHECK(!IsActionAllowed(presence_offline, false, kAllActions[a]));
        // 即便连接侥幸还在（正在关闭的瞬间），用户选了离线就不该再放行。
        CHECK(!IsActionAllowed(presence_offline, true, kAllActions[a]));
        // 越界 presence 同样按离线拦住。
        CHECK(!IsActionAllowed(kRetiredPresence, true, kAllActions[a]));
        CHECK(!IsActionAllowed(999, true, kAllActions[a]));
    }
}

TEST_CASE("CAP6 状态是在线但连接未就绪时同样全部拦住", "[onlinecap][action]")
{
    // 这条覆盖"从离线切回在线、登录还没完成"以及"断线重连中"两个中间态 ——
    // 此时胶囊已经显示在线，但消息发出去也到不了服务器，必须拦。
    for (int i = 0; i < 3; ++i)
    {
        for (int a = 0; a < kActionKindCount; ++a)
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

TEST_CASE("CAP8 各类操作各有非空提示文案，且都点明离线", "[onlinecap][text]")
{
    // 新增类别时若漏配文案，本条会失败。
    for (int a = 0; a < kActionKindCount; ++a)
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

    // 越界取值按离线处理。
    CHECK(::wcscmp(GetTrayIconFileName(
        ComputeConnectionUiState(kRetiredPresence, true)), L"offline.ico") == 0);
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

    // 本菜单表达不了的 presence，在连接就绪时也退化为"离线"（沿用既有兜底口径）。
    CHECK(::wcscmp(
        GetConnectionPresenceVisual(kRetiredPresence, kConnUiOnline).text, L"离线") == 0);
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
    // 遍历所有可能的 presence（含越界值）× 连接是否就绪。
    const int kAllPresences[6] =
    {
        presence_online, presence_away, presence_busy,
        presence_offline, kRetiredPresence, 999,
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

// ---------------------------------------------------------------------------
// CAP17~CAP19  托盘图标右下角的在线状态徽标
//
// 离开与忙碌跟在线共用同一张彩色底图，靠叠加徽标区分。判定必须只认
// "连接就绪 + 离开 / 忙碌"这一种组合：底图是灰的时候贴徽标会自相矛盾。
// ---------------------------------------------------------------------------

TEST_CASE("CAP17 连接就绪时，只有离开与忙碌叠加徽标", "[onlinecap][trayoverlay]")
{
    // 离开 / 忙碌：叠加各自的徽标。
    CHECK(GetTrayOverlayPresence(presence_away, kConnUiOnline) == presence_away);
    CHECK(GetTrayOverlayPresence(presence_busy, kConnUiOnline) == presence_busy);

    // 在线不叠 —— 彩色底图本身就表示在线，再贴徽标是冗余。
    CHECK(GetTrayOverlayPresence(presence_online, kConnUiOnline) == kNoTrayOverlay);

    // 用户主动离线不叠 —— 那一路底图已经是灰的。
    CHECK(GetTrayOverlayPresence(presence_offline, kConnUiOnline) == kNoTrayOverlay);

    // 越界取值（含已作废的编号 4）一律不叠，与本模块其它函数的保守兜底一致。
    const int kProbes[5] = { kRetiredPresence, 999, -1, 7, -42 };
    for (int i = 0; i < 5; ++i)
    {
        CHECK(GetTrayOverlayPresence(kProbes[i], kConnUiOnline) == kNoTrayOverlay);
    }
}

TEST_CASE("CAP18 连接未就绪时一律不叠徽标", "[onlinecap][trayoverlay]")
{
    // 连接中与掉线两种状态下，无论用户选的是什么，都不叠 —— 此刻消息收发不了，
    // 灰底图上贴一枚"离开"徽标会让人以为还能正常用。
    const int kNotReady[2] = { kConnUiConnecting, kConnUiOffline };
    const int kPresences[5] =
    {
        presence_online, presence_away, presence_busy, presence_offline, kRetiredPresence
    };

    for (int s = 0; s < 2; ++s)
    {
        for (int p = 0; p < 5; ++p)
        {
            CHECK(GetTrayOverlayPresence(kPresences[p], kNotReady[s]) == kNoTrayOverlay);
        }
    }

    // 越界的连接态同样按"未就绪"处理。
    CHECK(GetTrayOverlayPresence(presence_away, 999) == kNoTrayOverlay);
    CHECK(GetTrayOverlayPresence(presence_busy, -1)  == kNoTrayOverlay);
}

TEST_CASE("CAP19 叠徽标时底图必然是彩色图标", "[onlinecap][trayoverlay]")
{
    // 这条锁的是两个函数之间的一致性：一旦有人改了其中一个的口径，
    // 就会出现"灰图标上贴着离开徽标"这种自相矛盾的组合。
    const int kStates[3]    = { kConnUiOnline, kConnUiConnecting, kConnUiOffline };
    const int kPresences[4] =
    {
        presence_online, presence_away, presence_busy, presence_offline
    };

    for (int s = 0; s < 3; ++s)
    {
        for (int p = 0; p < 4; ++p)
        {
            if (GetTrayOverlayPresence(kPresences[p], kStates[s]) == kNoTrayOverlay)
                continue;

            CHECK(::wcscmp(GetTrayIconFileName(kStates[s]), L"imonline.ico") == 0);
        }
    }
}

// ---------------------------------------------------------------------------
// CAP20 ~ CAP23  「切换在线状态」这一档能力判定，以及状态菜单当前项的文字
//
// 这几条针对的是"掉线时托盘右键菜单仍标着『在线（当前）』、四个状态项还能点"这个
// 缺陷。切状态要向服务端发一帧（在线态发 cmd_change_presence、离线态走重新登录），
// 连接未就绪时发不出去，故它与发消息、传文件同属"需要服务器配合"的操作，走同一层
// 判定；菜单当前项的文字则必须与主面板胶囊逐字同源。
// ---------------------------------------------------------------------------

TEST_CASE("CAP20 操作类别表覆盖完整，且每个类别都配了提示文案", "[onlinecap][action]")
{
    // kAllActions 漏列一项时只会被零填充（0 恰好是合法类别 kActionSendMessage），
    // 上面那些遍历用例照样全绿却漏测了新类别。这条逐个核对每个类别都出现过一次。
    for (int kind = 0; kind < kActionKindCount; ++kind)
    {
        INFO("action kind = " << kind);
        int occurrences = 0;
        for (int a = 0; a < kActionKindCount; ++a)
        {
            if (kAllActions[a] == kind)
            {
                ++occurrences;
            }
        }
        CHECK(occurrences == 1);
    }

    // 新增的这一档单独点名，防止将来有人把它从枚举里挪走却没同步这里。
    CHECK(kActionChangePresence < kActionKindCount);
    const wchar_t* tip = GetBlockedActionTip(kActionChangePresence);
    REQUIRE(tip != NULL);
    // 文案要点明"切换在线状态"这件事本身，不能沿用发消息那几条的通用措辞。
    CHECK(::wcsstr(tip, L"切换在线状态") != NULL);
}

TEST_CASE("CAP21 两副入口给出的答案永远一致", "[onlinecap][action]")
{
    // IsActionAllowed（在线状态 + 连接是否就绪）与 IsActionAllowedInState（连接态）
    // 是同一条规则的两副入口。三个窗口的 CheckOnlineFor 用的是后者、菜单模型也用它，
    // 一旦两者分叉，就会出现"聊天窗拦住了、托盘菜单却放行"这种局面。
    const int  kAllPresences[6] =
    {
        presence_online, presence_away, presence_busy,
        presence_offline, kRetiredPresence, 999,
    };
    const bool kConnectedValues[2] = { true, false };

    for (int i = 0; i < 6; ++i)
    {
        for (int j = 0; j < 2; ++j)
        {
            for (int a = 0; a < kActionKindCount; ++a)
            {
                const int  presence  = kAllPresences[i];
                const bool connected = kConnectedValues[j];
                const int  action    = kAllActions[a];
                INFO("presence = " << presence << ", connected = " << connected
                     << ", action = " << action);

                const int state = (int)ComputeConnectionUiState(presence, connected);
                CHECK(IsActionAllowed(presence, connected, action)
                      == IsActionAllowedInState(state, action));
            }
        }
    }

    // 连接态入口的三条口径：只有真正在线放行，连接中 / 主动离线 / 越界值都拦。
    CHECK(IsActionAllowedInState((int)kConnUiOnline, kActionChangePresence));
    CHECK(!IsActionAllowedInState((int)kConnUiConnecting, kActionChangePresence));
    CHECK(!IsActionAllowedInState((int)kConnUiOffline, kActionChangePresence));
    CHECK(!IsActionAllowedInState(999, kActionChangePresence));

    // 越界的操作类别在这一副入口下同样不放行。
    CHECK(!IsActionAllowedInState((int)kConnUiOnline, -1));
    CHECK(!IsActionAllowedInState((int)kConnUiOnline, kActionKindCount));
}

TEST_CASE("CAP22 菜单当前项的文字：连接就绪按状态取，未就绪按连接态取",
          "[onlinecap][presence]")
{
    // 连接就绪：与只看在线状态的 GetPresenceMenuLabel 完全一致，正常在线时的表现不变。
    CHECK(::wcscmp(GetConnectionMenuLabel(presence_online, kConnUiOnline), L"在线（当前）") == 0);
    CHECK(::wcscmp(GetConnectionMenuLabel(presence_away,   kConnUiOnline), L"离开（当前）") == 0);
    CHECK(::wcscmp(GetConnectionMenuLabel(presence_busy,   kConnUiOnline), L"忙碌（当前）") == 0);
    CHECK(::wcscmp(GetConnectionMenuLabel(presence_offline, kConnUiOnline), L"离线（当前）") == 0);

    // 掉线重连中：用户选的仍是在线 / 离开 / 忙碌，但菜单不能再写"在线（当前）"——
    // 这正是本次要修的现象。
    for (int i = 0; i < 3; ++i)
    {
        INFO("presence = " << kOnlineLikePresences[i]);
        CHECK(::wcscmp(GetConnectionMenuLabel(kOnlineLikePresences[i], kConnUiConnecting),
                       L"正在连接（当前）") == 0);
    }

    // 用户主动离线：显示"离线（当前）"，他并没有在等着连上。
    CHECK(::wcscmp(GetConnectionMenuLabel(presence_offline, kConnUiOffline), L"离线（当前）") == 0);

    // 越界的连接态按离线兜底，绝不能落回"在线（当前）"。
    CHECK(::wcscmp(GetConnectionMenuLabel(presence_online, 999), L"离线（当前）") == 0);
    CHECK(::wcscmp(GetConnectionMenuLabel(presence_online, -1),  L"离线（当前）") == 0);

    // 本菜单表达不了的 presence，在连接就绪时退化为"离线（当前）"（沿用既有兜底口径）。
    CHECK(::wcscmp(GetConnectionMenuLabel(kRetiredPresence, kConnUiOnline), L"离线（当前）") == 0);
}

TEST_CASE("CAP23 菜单当前项文字与胶囊文字同源（只多一个\"（当前）\"后缀）",
          "[onlinecap][presence]")
{
    // 这条锁的是两个函数之间的一致性：菜单与胶囊只要有一处被单独改动、口径分叉了，
    // 就会重新出现"胶囊写着正在连接、菜单里却标着在线（当前）"这种自相矛盾。
    const int kAllPresences[6] =
    {
        presence_online, presence_away, presence_busy,
        presence_offline, kRetiredPresence, 999,
    };
    const int kAllStates[4] =
    {
        (int)kConnUiOnline, (int)kConnUiConnecting, (int)kConnUiOffline, 999,
    };
    // 后缀"（当前）"共 4 个宽字符（全角括号各占 1 个）。
    const size_t kCurrentSuffixLen = 4;

    for (int s = 0; s < 4; ++s)
    {
        for (int p = 0; p < 6; ++p)
        {
            const int presence = kAllPresences[p];
            const int state    = kAllStates[s];
            INFO("presence = " << presence << ", connUiState = " << state);

            const wchar_t* menuLabel = GetConnectionMenuLabel(presence, state);
            const wchar_t* pillText  = GetConnectionPresenceVisual(presence, state).text;
            REQUIRE(menuLabel != NULL);
            REQUIRE(pillText != NULL);

            const size_t labelLen = ::wcslen(menuLabel);
            const size_t pillLen  = ::wcslen(pillText);
            REQUIRE(labelLen == pillLen + kCurrentSuffixLen);

            // 前半段逐字等于胶囊文字，后半段恰好是"（当前）"。
            CHECK(::wcsncmp(menuLabel, pillText, pillLen) == 0);
            CHECK(::wcscmp(menuLabel + pillLen, L"（当前）") == 0);
        }
    }
}
