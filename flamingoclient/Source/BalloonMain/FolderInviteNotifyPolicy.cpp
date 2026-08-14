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

namespace balloonmain {

//"[文件夹] " —— 源文件为 UTF-8 且四个配置都带 /utf-8，故字面量按 UTF-8 落入常量区，
//与会话列表其余预览文字（"[图片]" / "[文件]"）同编码。
const char* const kFolderInviteDigestPrefix = u8"[文件夹] ";

//没有文件夹名字时用的预览文字：把上面那个前缀末尾的分隔空格去掉。
static const char* const kFolderInviteDigestNoName = u8"[文件夹]";

std::string BuildFolderInviteDigest(const std::string& folderNameUtf8)
{
    if (folderNameUtf8.empty())
    {
        return std::string(kFolderInviteDigestNoName);
    }
    return std::string(kFolderInviteDigestPrefix) + folderNameUtf8;
}

bool ShouldCountFolderInviteAsUnread(bool fromSelf, bool userWatchingChatWindow)
{
    if (fromSelf)
    {
        return false;
    }
    return !userWatchingChatWindow;
}

bool ShouldNotifyFolderInvite(bool chatWindowOpen, bool fromSelf, bool conversationMuted)
{
    if (chatWindowOpen)
    {
        return false;
    }
    if (fromSelf)
    {
        return false;
    }
    return !conversationMuted;
}

} // namespace balloonmain
