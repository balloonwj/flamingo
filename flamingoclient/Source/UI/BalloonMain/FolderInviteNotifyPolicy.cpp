/**
 *  收到文件夹传输邀请时的提醒判据与会话列表预览文字组装的实现，
 *  FolderInviteNotifyPolicy.cpp
 *
 *  设计与各函数契约见 FolderInviteNotifyPolicy.h。实现全部是不带状态的短函数，
 *  刻意不引入任何 Win32 / balloonui 头文件，使本编译单元能原样进 FlamingoUnit
 *  测试工程。
 *
 *  balloonwj@qq.com   2026-08-14
 */

#include "FolderInviteNotifyPolicy.h"
#include "NotifyModePolicy.h"   // DecideArrival：与普通单聊消息同一套判定
#include "../../Common/I18n/I18n.h"   // TR8 / TR_NOOP：UTF-8 界面文字的多语言查找

namespace balloonmain {

//预览文字的类型占位 "[文件夹]"，体例与会话列表既有的类型占位对齐 —— 文件消息在那里显示
//的是 "[文件]"、图片是 "[图片]"（见 net::MessageProtocolClient::digestForMsgType）。
//源文件为 UTF-8 且四个配置都带 /utf-8，故字面量按 UTF-8 落入常量区。
//这里只做标记（TR_NOOP），组装时用 TR8 取译文
static const char* const kFolderInviteDigestTag = TR_NOOP(u8"[文件夹]");

std::string BuildFolderInviteDigest(const std::string& folderNameUtf8)
{
    const std::string tag = TR8(kFolderInviteDigestTag);
    if (folderNameUtf8.empty())
    {
        return tag;
    }
    //有名字时在类型占位与名字之间加一个分隔空格
    return tag + " " + folderNameUtf8;
}

bool ShouldCountFolderInviteAsUnread(bool fromSelf, bool userWatchingChatWindow)
{
    if (fromSelf)
    {
        return false;
    }
    return !userWatchingChatWindow;
}

bool ShouldNotifyFolderInvite(bool chatWindowOpen, bool fromSelf, int notifyMode)
{
    //文件夹邀请按单聊消息对待：不是群、不会 @ 人、也不是窗口抖动。窗口没开时用户不可能
    //正看着它，所以「正在看」取窗口是否打开即可（窗口开着时无论在不在看都不闪托盘）
    ArrivalInput in;
    in.m_mode       = notifyMode;
    in.m_isGroup    = false;
    in.m_atMe       = false;
    in.m_countable  = !fromSelf;
    in.m_windowOpen = chatWindowOpen;
    in.m_watching   = false;
    in.m_isShake    = false;
    return DecideArrival(in).m_flashTray;
}

} // namespace balloonmain
