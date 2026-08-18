/**
 *  KickoutNoticeTests.cpp
 *
 *  被踢下线提示框文案（KickoutNotice）的测试：
 *    · 镜像常量与 v2 协议 enum kickout_reason 逐条一致 —— 协议侧改了值这边没跟上会立刻失败。
 *    · "异地登录"那条安全提示逐字比对，防止措辞被后来的改动悄悄改掉。
 *    · 五种已知原因文案互不相同且均非空；未知 / 越界原因有非空兜底。
 *    · 提示框标题非空。
 *
 *  用 Catch2 v3（amalgamated）作框架，纯内存断言，不创建窗口 / 不依赖消息循环。
 *  全局 _Module 由 BalloonChatLayoutTests.cpp 统一定义，本文件只引用。
 *
 *  balloonwj@qq.com   2026-08-04
 */

#include "stdafx.h"

#include "catch_amalgamated.hpp"

#include <cwchar>          // wcscmp / wcslen —— 比对中文提示文案

#include "../UI/BalloonMain/KickoutNotice.h"
#include "../net/Protocol.h"   // kickout_reason（被踢原因取值的单一事实来源）

using namespace balloonmain;

namespace {

// 全部已知的被踢原因，供逐条遍历。未知（0）不在此列，它单独由 KICK4 覆盖。
const int kAllKnownReasons[] =
{
    kKickoutReasonOtherDevice,
    kKickoutReasonDisabled,
    kKickoutReasonMaintenance,
    kKickoutReasonPasswordChanged,
    kKickoutReasonKickedByUser,
    kKickoutReasonEnterpriseDisabled,
    kKickoutReasonEnterpriseDeleted,
    kKickoutReasonAccountDeleted,
    kKickoutReasonKickedByAdmin,
};

// 已知原因的条数。由数组自身算出而不是写死 —— 以后再加新原因时只需往上面数组里
// 补一行，遍历用例会自动覆盖到，不会出现"加了原因却漏测"。
const int kKnownReasonCount =
    static_cast<int>(sizeof(kAllKnownReasons) / sizeof(kAllKnownReasons[0]));

} // anonymous namespace

// =====================================================================
// KICK1 —— 镜像常量与协议枚举一致
// =====================================================================

TEST_CASE("KICK1 KickoutReasonValue 与协议 kickout_reason 逐条相等", "[kickout]")
{
    CHECK((int)kKickoutReasonOtherDevice     == (int)kickout_reason_other_device);
    CHECK((int)kKickoutReasonDisabled        == (int)kickout_reason_disabled);
    CHECK((int)kKickoutReasonMaintenance     == (int)kickout_reason_maintenance);
    CHECK((int)kKickoutReasonPasswordChanged == (int)kickout_reason_password_changed);
    CHECK((int)kKickoutReasonKickedByUser    == (int)kickout_reason_kicked_by_user);
    CHECK((int)kKickoutReasonEnterpriseDisabled == (int)kickout_reason_enterprise_disabled);
    CHECK((int)kKickoutReasonEnterpriseDeleted  == (int)kickout_reason_enterprise_deleted);
    CHECK((int)kKickoutReasonAccountDeleted     == (int)kickout_reason_account_deleted);
    CHECK((int)kKickoutReasonKickedByAdmin      == (int)kickout_reason_kicked_by_admin);

    // 未知值不来自协议，它只是本端的兜底档位，必须与任何一个协议取值都不同，
    // 否则真有一个原因会被当成"未知"。
    CHECK((int)kKickoutReasonUnknown != (int)kickout_reason_other_device);
}

// =====================================================================
// KICK2 —— 异地登录的安全提示逐字比对
// =====================================================================

TEST_CASE("KICK2 异地登录的提示框文案与需求原文逐字相同", "[kickout]")
{
    const wchar_t* expected =
        L"您的账号在其他地方登录，如果这不是您的行为，密码可能已经泄露，请及时修改密码。";
    CHECK(wcscmp(GetKickoutNoticeText(kKickoutReasonOtherDevice), expected) == 0);
}

// =====================================================================
// KICK3 ~ KICK5 —— 文案完整性
// =====================================================================

TEST_CASE("KICK3 各已知原因的文案互不相同且均非空", "[kickout]")
{
    for (int i = 0; i < kKnownReasonCount; ++i)
    {
        INFO("reason = " << kAllKnownReasons[i]);
        const wchar_t* text = GetKickoutNoticeText(kAllKnownReasons[i]);
        REQUIRE(text != NULL);
        CHECK(wcslen(text) > 0);
    }

    // 两两比对：任意两种原因的文案都不该重复，否则等于少写了一条、用户看不出区别。
    for (int i = 0; i < kKnownReasonCount; ++i)
    {
        for (int j = i + 1; j < kKnownReasonCount; ++j)
        {
            INFO("reason " << kAllKnownReasons[i] << " vs " << kAllKnownReasons[j]);
            CHECK(wcscmp(GetKickoutNoticeText(kAllKnownReasons[i]),
                         GetKickoutNoticeText(kAllKnownReasons[j])) != 0);
        }
    }
}

TEST_CASE("KICK4 未知与越界原因返回非空兜底文案", "[kickout]")
{
    // 未知档位本身、服务端将来新增的取值、以及负数 / 极端值都必须有话可说。
    // 这里的 10 是"本端尚未登记的下一个取值"——协议已用到 9（设备被管理员注销），
    // 将来协议加了 10 就把这个数往后挪，保持它始终指向一个未登记值。
    const int kOutOfRangeReasons[4] = { kKickoutReasonUnknown, 10, 999, -1 };
    for (int i = 0; i < 4; ++i)
    {
        INFO("reason = " << kOutOfRangeReasons[i]);
        const wchar_t* text = GetKickoutNoticeText(kOutOfRangeReasons[i]);
        REQUIRE(text != NULL);
        CHECK(wcslen(text) > 0);
    }

    // 越界取值与"未知"应当落到同一句兜底文案上。
    CHECK(wcscmp(GetKickoutNoticeText(999), GetKickoutNoticeText(kKickoutReasonUnknown)) == 0);
}

TEST_CASE("KICK5 提示框标题非空", "[kickout]")
{
    const wchar_t* title = GetKickoutNoticeTitle();
    REQUIRE(title != NULL);
    CHECK(wcslen(title) > 0);
}

// =====================================================================
// KO-A —— 退出登录时要不要清掉本机的"自动登录"标志
//
// 背景：此前登出并不清这个标志，用户点了"登出"、下次开机照样被自动登进去；而自动
// 登录成功后又直接进主面板、没有界面能取消勾选，等于绕不出来。这几条把口径钉死。
// =====================================================================

TEST_CASE("KO-A1 用户主动登出时清掉自动登录标志", "[kickout][autologin]")
{
    // 点"登出"是一次明确的主观意愿表达 —— 用户在说"我不想自动进去了"。
    // 第二个参数是被踢原因，主动登出这一侧应当被忽略，故随便给个值也不影响结果。
    CHECK(ShouldClearAutoLoginOnExit(kLoginExitUserLogout, kKickoutReasonUnknown));
    CHECK(ShouldClearAutoLoginOnExit(kLoginExitUserLogout, kKickoutReasonOtherDevice));
}

TEST_CASE("KO-A2 账号已登不上的两种被踢原因要清掉标志", "[kickout][autologin]")
{
    // 密码已修改：本机存的密码已经失效，留着自动登录只会让用户每次开机都被弹一次
    // "密码错误"。
    CHECK(ShouldClearAutoLoginOnExit(kLoginExitKickedOut, kKickoutReasonPasswordChanged));

    // 企业已删除：账号虽在但已无企业归属，服务端登录校验会以"企业不存在"回绝，
    // 且不可恢复 —— 留着自动登录同样只是每次开机弹一次失败。
    CHECK(ShouldClearAutoLoginOnExit(kLoginExitKickedOut, kKickoutReasonEnterpriseDeleted));

    // 账号已删除：账号本身没了，再登录会被判为未注册，同样不可恢复。
    CHECK(ShouldClearAutoLoginOnExit(kLoginExitKickedOut, kKickoutReasonAccountDeleted));
}

TEST_CASE("KO-A3 其余被踢原因一律不动自动登录标志", "[kickout][autologin]")
{
    // 这些都是被动发生的，用户没有表达"以后别自动登录"的意思，不替他做主。
    CHECK_FALSE(ShouldClearAutoLoginOnExit(kLoginExitKickedOut, kKickoutReasonOtherDevice));
    CHECK_FALSE(ShouldClearAutoLoginOnExit(kLoginExitKickedOut, kKickoutReasonDisabled));
    CHECK_FALSE(ShouldClearAutoLoginOnExit(kLoginExitKickedOut, kKickoutReasonMaintenance));
    CHECK_FALSE(ShouldClearAutoLoginOnExit(kLoginExitKickedOut, kKickoutReasonKickedByUser));

    // 企业被停用是<u>可恢复</u>的：管理员把企业改回活跃后，用户开机就该照常自动
    // 登进去，所以这一条必须留着标志 —— 与上面同为"企业"场景的"已删除"分道扬镳。
    CHECK_FALSE(ShouldClearAutoLoginOnExit(kLoginExitKickedOut, kKickoutReasonEnterpriseDisabled));

    // 被管理员注销某台设备：账号本身好好的，重新登录即可，不该动自动登录标志。
    CHECK_FALSE(ShouldClearAutoLoginOnExit(kLoginExitKickedOut, kKickoutReasonKickedByAdmin));
}

TEST_CASE("KO-A4 未知原因与越界取值按保守一侧处理", "[kickout][autologin]")
{
    // 服务端将来加了本端还不认识的新原因时，宁可保留自动登录（用户下次照常进得去），
    // 也不要莫名其妙把他的勾选清掉。exitKind 越界同理，按"被踢下线"这一侧处理。
    const int kUnknownReason = 999;   // 本端尚未登记的被踢原因
    const int kUnknownExit   = 42;    // 越界的退出方式
    CHECK_FALSE(ShouldClearAutoLoginOnExit(kLoginExitKickedOut, kKickoutReasonUnknown));
    CHECK_FALSE(ShouldClearAutoLoginOnExit(kLoginExitKickedOut, kUnknownReason));
    CHECK_FALSE(ShouldClearAutoLoginOnExit(kUnknownExit, kUnknownReason));
}
