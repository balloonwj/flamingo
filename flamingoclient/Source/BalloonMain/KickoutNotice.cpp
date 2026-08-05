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

namespace balloonmain {

const wchar_t* GetKickoutNoticeTitle()
{
    return L"下线通知";
}

const wchar_t* GetKickoutNoticeText(int reason)
{
    switch (reason)
    {
    case kKickoutReasonOtherDevice:
        // 安全提示，措辞由产品定稿，KickoutNoticeTests 的 KICK2 用例逐字比对，改动请先对齐需求。
        return L"您的账号在其他地方登录，如果这不是您的行为，密码可能已经泄露，请及时修改密码。";
    case kKickoutReasonDisabled:
        return L"您的账号已被管理员停用，如需继续使用请联系管理员。";
    case kKickoutReasonMaintenance:
        return L"服务器正在维护，连接已断开，请稍后重新上线。";
    case kKickoutReasonPasswordChanged:
        return L"账号密码已修改，请用新密码重新登录。";
    case kKickoutReasonKickedByUser:
        return L"您在另一台设备上下线了本设备。";
    case kKickoutReasonUnknown:
    default:
        // 未知原因与一切越界取值的兜底：不解释缘由，但必须让用户知道自己已经下线了。
        return L"您的账号已在别处下线，请重新登录。";
    }
}

} // namespace balloonmain
