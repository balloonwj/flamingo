/**
 *  KickoutNotice.h
 *
 *  被服务器踢下线（cmd_kickout 1006 推送）后，提示框该显示什么文字的<u>纯数据判定</u>。
 *
 *  单独成一个文件而不是塞进 CMainDlg，有两个理由：
 *    · 文案要能单测 —— 尤其"异地登录"那一条是安全提示，措辞不能被后来的改动悄悄改掉，
 *      故有一条用例逐字比对它。
 *    · CMainDlg 所在的编译单元包含 v1 的 net/Msg.h，无法再 include v2 的 net/Protocol.h
 *      （msg_type / error_code 枚举名会冲突），却又要按被踢原因分支给文案。于是本文件
 *      按 UserSessionData.h 里 MyPresenceValue 的老办法，把 kickout_reason 的取值<u>镜像</u>
 *      一份出来，两边靠 KickoutNoticeTests 的一条用例保证不走样。
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

} // namespace balloonmain
