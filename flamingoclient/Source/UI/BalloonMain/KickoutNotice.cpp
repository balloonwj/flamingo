/**
 *  KickoutNotice.cpp
 *
 *  被踢下线提示框文案的实现。仅做纯计算，不碰窗口 / 网络对象，故可被 Flamingo.vcxproj
 *  与 FlamingoUnit.vcxproj 两个工程逐字共用。在 Flamingo.vcxproj 里本文件需标记
 *  PrecompiledHeader=NotUsing。
 *
 *  本文件含中文宽字符字面量（提示文案）。两个工程均已开启 /utf-8 编译选项，源码按
 *  UTF-8 解析，L"中文" 字面量正常，无需 BOM。
 *
 *  balloonwj@qq.com   2026-08-04
 */

#include "KickoutNotice.h"
#include "../../Common/I18n/I18n.h"   // TR：界面文字的多语言查找

namespace balloonmain {

const wchar_t* GetKickoutNoticeTitle()
{
    return TR(L"下线通知");
}

const wchar_t* GetKickoutNoticeText(int reason)
{
    switch (reason)
    {
    case kKickoutReasonOtherDevice:
        // 安全提示，措辞由产品定稿，KickoutNoticeTests 的 KICK2 用例逐字比对，改动请先对齐需求。
        return TR(L"您的账号在其他地方登录，如果这不是您的行为，密码可能已经泄露，请及时修改密码。");
    case kKickoutReasonDisabled:
        return TR(L"您的账号已被管理员停用，如需继续使用请联系管理员。");
    case kKickoutReasonMaintenance:
        return TR(L"服务器正在维护，连接已断开，请稍后重新上线。");
    case kKickoutReasonPasswordChanged:
        return TR(L"账号密码已修改，请用新密码重新登录。");
    case kKickoutReasonKickedByUser:
        return TR(L"您在另一台设备上下线了本设备。");
    case kKickoutReasonEnterpriseDisabled:
        // 企业被停用是可恢复的，措辞留出"恢复后还能用"的余地，别让用户以为账号废了。
        return TR(L"您所在的企业已被停用，暂时无法使用，请联系管理员。");
    case kKickoutReasonEnterpriseDeleted:
        // 企业已删除则不可恢复：该账号已无企业归属，再登录会被服务端以"企业不存在"回绝，
        // 故直接告诉用户无法再登录，免得他反复尝试。
        return TR(L"您所在的企业已被删除，该账号无法再登录，请联系管理员。");
    case kKickoutReasonAccountDeleted:
        // 与"账号被停用"分开：停用可由管理员恢复，删除则不可逆，明确告知登不上了。
        return TR(L"您的账号已被管理员删除，无法再登录，请联系管理员。");
    case kKickoutReasonKickedByAdmin:
        // 与"本人在另一台设备上下线了本设备"分开：这次是管理员干的，说清楚是谁做的，
        // 否则用户会以为自己的账号被别人登了。
        return TR(L"您的登录设备已被管理员注销，如需继续使用请重新登录。");
    case kKickoutReasonPasswordResetByAdmin:
        // 与"账号密码已修改"分开：这次是管理员重置的，用户手里没有新密码，要告诉他去找管理员要
        return TR(L"您的密码已被管理员重置，请向管理员获取新密码后重新登录。");
    case kKickoutReasonUnknown:
    default:
        // 未知原因与一切越界取值的兜底：不解释缘由，但必须让用户知道自己已经下线了。
        return TR(L"您的账号已在别处下线，请重新登录。");
    }
}

bool ShouldClearAutoLoginOnExit(int exitKind, int kickoutReason)
{
    // 主动登出：用户明确表达了"我不想再自动进去"，清。
    if (exitKind == kLoginExitUserLogout)
    {
        return true;
    }

    // 被踢下线：只有"这个账号已经登不上了、且不可恢复"的几种要清，理由是同一个 ——
    // 留着自动登录只会让用户每次开机都被弹一次失败提示，白白卡在那里：
    //   · 密码已修改、密码被管理员重置 —— 本机存的密码已经失效；
    //   · 所属企业已删除 —— 账号虽在但已无企业归属，服务端登录校验会以"企业不存在"
    //     回绝（见 ChatSession::onLogin 的企业可用性校验）；
    //   · 账号已删除 —— 账号本身没了，再登录会被判为未注册。
    // 其余原因（异地登录 / 账号停用 / 服务器维护 / 被本人或管理员踢掉某台设备 /
    // 企业被停用）都是被动发生【且可恢复】的，用户没有表达任何意愿，不替他做主：
    // 账号停用与企业停用都可能被管理员改回来，改回来之后用户开机就该照常自动登进去。
    // 越界的 exitKind 也走到这里，按保守的一侧处理（不清）。
    if (kickoutReason == kKickoutReasonPasswordChanged ||
        kickoutReason == kKickoutReasonPasswordResetByAdmin ||
        kickoutReason == kKickoutReasonEnterpriseDeleted ||
        kickoutReason == kKickoutReasonAccountDeleted)
    {
        return true;
    }

    return false;
}

} // namespace balloonmain
