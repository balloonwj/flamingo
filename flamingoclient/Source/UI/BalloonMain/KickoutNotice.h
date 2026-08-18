/**
 *  KickoutNotice.h
 *
 *  被服务器踢下线（cmd_kickout 1006 推送）后，提示框该显示什么文字的<u>纯数据判定</u>。
 *
 *  单独成一个文件而不是塞进 CMainDlg，有两个理由：
 *    · 文案要能单测 —— 尤其"异地登录"那一条是安全提示，措辞不能被后来的改动悄悄改掉，
 *      故有一条用例逐字比对它。
 *    · 取值<u>另抄</u>一份常量，而不直接用 net/Protocol.h 的 kickout_reason。原先是因为
 *      CMainDlg 所在的编译单元包含 v1 的 net/Msg.h、无法再 include v2 的 Protocol.h
 *      （两者 msg_type / error_code 枚举名冲突）；v1 协议头已于 2026-08-14 删除，该限制
 *      不复存在，现在保留另一份的理由是本模块要能被测试工程独立编译。两边取值由
 *      KickoutNoticeTests 的一组用例逐条验证，防止走岔。
 *
 *  本文件不依赖窗口、不依赖网络对象，可被 Flamingo.vcxproj 与 FlamingoUnit.vcxproj
 *  两个工程逐字共用。在 Flamingo.vcxproj 里本文件需标记 PrecompiledHeader=NotUsing。
 *
 *  balloonwj@qq.com   2026-08-04
 */

#pragma once

namespace balloonmain {

/**
 *  KickoutReasonValue —— 被踢下线的原因。
 *
 *  取值<u>逐一镜像</u> v2 协议 net/Protocol.h 的 enum kickout_reason，值必须保持一致；
 *  KickoutNoticeTests 的 KICK1 用例会逐条断言两边相等，协议侧改了值这里没跟上会立刻失败。
 */
enum KickoutReasonValue
{
    kKickoutReasonUnknown         = 0,   // 未知原因：包体解析不出 reason、或服务端下发了本端还不认识的新值，走兜底文案
    kKickoutReasonOtherDevice     = 1,   // 账号在其他同类型设备上登录，本端被顶下线（最常见：两台 PC 用同一账号）
    kKickoutReasonDisabled        = 2,   // 账号被管理员在 admin 控制台停用
    kKickoutReasonMaintenance     = 3,   // 服务器进入维护，主动断开全部连接
    kKickoutReasonPasswordChanged = 4,   // 账号密码被修改（本人在别处改密或走了找回密码），其余设备须用新密码重登
    kKickoutReasonKickedByUser    = 5,   // 本人在另一台设备上从设备列表里主动踢掉了本设备（cmd_kick_device）
    kKickoutReasonEnterpriseDisabled = 6,// 所属企业被平台管理员停用，该企业全员下线；企业恢复后可再登录
    kKickoutReasonEnterpriseDeleted  = 7,// 所属企业被平台管理员删除，该企业全员下线；账号已无企业归属，无法再登录
    kKickoutReasonAccountDeleted     = 8,// 账号被管理员删除，不可恢复（与 2 停用可恢复相区分）
    kKickoutReasonKickedByAdmin      = 9,// 本设备被管理员在控制台强制注销（与 5 本人从别的设备踢的相区分）
};

/**
 *  取被踢下线提示框的标题（同时用作窗口标题栏文字）。
 *
 *  @return 静态常量串，进程级生命周期，调用方可直接持有；恒不为空。
 *
 *  标题不随原因变化 —— 对用户而言这几种情况都是"你被下线了"，差别在正文里说清楚即可。
 *  本函数无副作用。
 */
const wchar_t* GetKickoutNoticeTitle();

/**
 *  取被踢下线提示框的正文。
 *
 *    reason：被踢原因，取值见 KickoutReasonValue（即协议的 kickout_reason）。
 *
 *  @return 静态常量串，进程级生命周期，调用方可直接持有；恒不为空 —— 未知与一切越界
 *          取值都返回一句通用兜底文案。宁可文案笼统，也不能因为多了个没登记的原因就
 *          让用户看不到任何提示（他只会看到自己莫名其妙被下线了）。
 *
 *  本函数无副作用。
 */
const wchar_t* GetKickoutNoticeText(int reason);

/**
 *  LoginExitKind —— 这次退出登录态是怎么发生的。
 *
 *  主动登出与被踢下线共用同一套收尾序列（CMainDlg::ReturnToLoginWindow），但在
 *  "要不要顺手关掉自动登录"这件事上必须区分开，故单立一个枚举把两者传下去。
 */
enum LoginExitKind
{
    kLoginExitUserLogout = 0,   // 用户自己在菜单里点了"登出"，是一次明确的主观意愿表达
    kLoginExitKickedOut  = 1,   // 被服务器踢下线（cmd_kickout 推送），用户没有表达任何意愿
};

/**
 *  这次退出登录之后，是否应当把本机存下的"自动登录"标志清掉。
 *
 *  背景：自动登录标志存在 Bin\Users\LoginAccountList.dat 里，勾上之后每次启动都会
 *  跳过登录窗直接登进去。此前登出并不清这个标志，于是用户点了"登出"、下次开机却
 *  照样被自动登进去，而自动登录成功后又没有界面可以取消勾选，等于绕不出来。
 *
 *  口径：
 *    · 主动登出 —— 清。用户点"登出"就是在说"我不想自动进去了"。
 *    · 被踢下线且原因是"密码已修改" —— 清。本机存的密码已经失效，留着自动登录
 *      只会让用户每次开机都被弹一次"密码错误"。
 *    · 其余被踢原因（异地登录、账号停用、服务器维护、被本人从别的设备踢掉）—— 不清。
 *      这些都是被动发生的，用户并没有表达"以后别自动登录"的意思，替他关掉反而突兀。
 *
 *    exitKind       退出方式，取值见 LoginExitKind；越界值按"被踢下线"这一侧保守处理。
 *    kickoutReason  被踢原因，取值见 KickoutReasonValue；exitKind 为主动登出时本参数被忽略。
 *  @return 应当清掉自动登录标志返回 true；应当原样保留返回 false
 *
 *  纯判定，无副作用（真正的清除与落盘由 CMainDlg 负责）。
 */
bool ShouldClearAutoLoginOnExit(int exitKind, int kickoutReason);

} // namespace balloonmain
