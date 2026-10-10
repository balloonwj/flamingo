/**
 *  收到文件夹传输邀请（cmd_folder_invite_notify=1811）时的<u>提醒判据</u>与会话列表
 *  预览文字组装，FolderInviteNotifyPolicy.h
 *
 *  存在理由 —— 文件夹邀请此前是"到达即建窗并弹到前台"，对接收方是一次强打扰（实测
 *  表现为凭空弹出一个空白聊天窗）。改成与普通单聊消息同一口径之后，"要不要闪托盘、
 *  要不要记未读、会话列表那行显示什么文字"就成了三条判据；把它们从
 *  CBalloonMainPanel::OnFolderInviteNotify 里抽到这里，一是能被 FlamingoUnit 直接
 *  断言（见 flamingoclient/Source/Tests/FolderInviteNotifyPolicyTests.cpp），二是与
 *  普通消息那条路径共用同一套措辞，不至于两处口径慢慢走偏。
 *
 *  本文件为纯逻辑：不依赖 HWND、不依赖 balloonui、不发协议，入参一律是已经翻译好的
 *  布尔量与 UTF-8 字符串，"把窗口句柄翻译成布尔量"是调用方的事。
 *
 *  典型用法（主面板收到 1811 推送时）：
 *      const std::string digest =
 *          balloonmain::BuildFolderInviteDigest(notify.m_folderName);
 *      BumpConversation(..., digest, ...,
 *          balloonmain::ShouldCountFolderInviteAsUnread(fromSelf, watching));
 *      if (balloonmain::ShouldNotifyFolderInvite(windowOpen, fromSelf, muted))
 *          ...（闪托盘 + 播提示音）
 *
 *  balloonwj@qq.com   2026-08-14
 */

#pragma once

#include <string>

namespace balloonmain {

/**
 *  组装一条文件夹传输在会话列表里的预览文字。
 *
 *  <u>邀请阶段与事后的传输记录条共用本函数</u>，两处措辞因而逐字相同：
 *    · 邀请阶段（cmd_folder_invite_notify=1811）不进服务端消息表、没有 msgtype，
 *      预览文字无法像普通消息那样由 digestForMsgType 按类型推导，只能就地拼；
 *    · 传输走到终态后服务端落的那条记录条（msgtype=message_type_folder=18）虽然是正经
 *      消息，但 digestForMsgType 只拿得到类型、拿不到文件夹名（名字在 content 里），
 *      故主面板收到它时同样改用本函数拼（见 CBalloonMainPanel::OnRecvChatMessage）。
 *  同一次传输在会话列表上因而从头到尾是同一种写法，不会先是一种、后变成另一种。
 *
 *    folderNameUtf8  根目录名（UTF-8，不含路径）。允许为空 —— 对端发来的字段缺失 /
 *                    为空串时按"没有名字"处理，只留类型占位。
 *  @return 形如 "[文件夹] 项目资料" 的预览文字（UTF-8）；folderNameUtf8 为空时返回
 *          去掉尾部空格的 "[文件夹]"，避免列表上出现一个多余的悬空空格
 */
std::string BuildFolderInviteDigest(const std::string& folderNameUtf8);

/**
 *  这条文件夹邀请要不要给会话列表记一条未读。
 *
 *  判据与普通单聊消息一致：自己在别的设备上发起的（理论上不会推回本端，仍按同一口径
 *  防一手）不计；用户此刻正看着与该对端的聊天窗时，邀请气泡已经当场画在他眼前，再记
 *  未读就会出现"邀请就在眼前、会话列表还亮着红点"。
 *    fromSelf                 邀请的发起方是否就是本端登录的这个账号。
 *    userWatchingChatWindow   与该对端的聊天窗此刻是否可见、未最小化且在前台，取值
 *                             来自 balloonmain::ShouldTreatAsReadOnArrival。
 *  @return 应当把该会话的未读数加一返回 true
 */
bool ShouldCountFolderInviteAsUnread(bool fromSelf, bool userWatchingChatWindow);

/**
 *  这条文件夹邀请要不要闪托盘头像并播新消息提示音。
 *
 *  判据与普通单聊消息完全相同，直接取 NotifyModePolicy 的 DecideArrival 的 m_flashTray：
 *  聊天窗已经打开时邀请气泡已渲染进去，无须再闪；自己发起的不闪（否则闪的是自己的头像）；
 *  会话设为接收不提醒、收进消息盒子、屏蔽时不闪不响；特别关注的会话不闪托盘，改为弹出聊天窗
 *  （由调用方按 DecideArrival 的 m_popupWindow 处理）。
 *    chatWindowOpen  与该对端的聊天窗此刻是否已经打开（窗口对象存在且句柄有效）。
 *    fromSelf        邀请的发起方是否就是本端登录的这个账号。
 *    notifyMode      该会话的消息提醒模式，取值见 Protocol.h 的 enum conversation_notify_mode
 *                    （2026-10-03 之前是「是否免打扰」的布尔值）。
 *  @return 应当闪托盘并播提示音返回 true
 */
bool ShouldNotifyFolderInvite(bool chatWindowOpen, bool fromSelf, int notifyMode);

} // namespace balloonmain
