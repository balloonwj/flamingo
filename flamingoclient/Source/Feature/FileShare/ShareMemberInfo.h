/**
 *  文件共享宿主回送会话成员时的转换（纯逻辑），ShareMemberInfo.h
 *
 *  DLL 经 IFileShareHost::RequestConversationMembers 要会话成员（发起共享、调整成员的候选），宿主取到后要把它们写成
 *  接口约定的定长结构体 FileShareMemberInfo 交回去（FlamingoFileShare/FileShareApi.h）。本文件做这一步转换：
 *    - 群聊：群成员列表（cmd 1304 的应答）逐项转换，显示名依次取群名片、昵称、通讯录显示名中第一个非空的
 *      （与群聊窗成员列表的口径一致）；
 *    - 单聊：对方一人，显示名取通讯录显示名；
 *    - 显示名超出定长缓冲区时按 UTF-8 字符边界截断，不会截出半个汉字；
 *    - 头像（2026-10-03 起）：主面板算好的 ShareUserAvatar 写进 FileShareAvatarInfo（FillAvatarInfo）。
 *  通讯录显示名与头像经 IFileShareDirectory 取（主面板实现，通讯录缓存在主面板里）。
 *
 *  单元测试见 Source/Tests/ShareMemberInfoTests.cpp（标签 [sharemembers]）。
 *
 *  典型用法（CFileShareHost::OnGroupMembersArrived）：
 *      std::vector<fileshare::FileShareMemberInfo> infos = BuildGroupMemberInfos(result, m_directory);
 *      module->OnConversationMembers(conversation_group, gid, 0, infos.data(), (uint32_t)infos.size());
 *
 *  balloonwj@qq.com   2026-10-03
 */
#pragma once

#include <stdint.h>
#include <string>
#include <vector>

#include "../../../FlamingoFileShare/FileShareApi.h"

namespace net {
struct GroupMembersResult;
}

namespace balloonfileshare {

/**
 *  一个用户的头像该怎么画（2026-10-03 起）：主面板按通讯录里该用户的头像设置算好，经文件共享宿主交给 DLL。
 */
struct ShareUserAvatar
{
    std::wstring m_glyph;       //字形头像上画的字（1~2 个字符）；照片头像也填，照片不在本机时用它占位
    COLORREF     m_color;       //字形头像的底色
    std::wstring m_imagePath;   //照片头像在本机的完整路径；空串表示画字形（没有上传照片，或照片还没下载到本机）

    ShareUserAvatar()
        : m_color(0)
    {
    }
};

/**
 *  宿主查询用户显示名所需的主程序能力，由主面板实现（通讯录缓存在主面板里）。只在 UI 线程上调用。
 */
class IFileShareDirectory
{
public:
    virtual ~IFileShareDirectory()
    {
    }

    /**
     *  取一个用户的显示名：与会话列表、消息记录同一口径（通讯录里有备注时带备注），通讯录里没有时为「用户<id>」。
     *  @param userId 用户 userid
     *  @return 显示名
     */
    virtual std::wstring GetShareUserDisplayName(int64_t userId) = 0;

    /**
     *  某个共享卡片的状态变了（DLL 收到 2203、推给本人的 2206 后经宿主转来）：刷新已打开聊天窗里这个共享的卡片。
     *  @param shareId 共享 id
     *  @param state   新状态（share_state）
     *  @param reason  新原因
     *  @param myPerms 本人的写权限位；不知道时为 -1（卡片目前不显示权限，留给以后用）
     */
    virtual void OnShareCardStateChanged(int64_t shareId, int state, int reason, int myPerms) = 0;

    /**
     *  取一个会话的显示名：群聊为群名（群资料还没到时为空串），单聊为对方的显示名。
     *  @param ctype 会话类型：1 单聊 2 群聊
     *  @param cid   会话 id
     */
    virtual std::wstring GetShareConversationName(int ctype, int64_t cid) = 0;

    /**
     *  打开（或前置）一个会话的聊天窗。
     *  @param ctype 会话类型
     *  @param cid   会话 id
     */
    virtual void OpenShareConversation(int ctype, int64_t cid) = 0;

    /**
     *  一个会话里可用的共享个数变了（DLL 经宿主转来，2026-10-03 起）：刷新该会话聊天窗头部文件共享按钮上的角标。
     *  @param ctype 会话类型：1 单聊 2 群聊
     *  @param cid   会话 id（本人视角）
     *  @param count 可用共享的个数；0 时角标消失
     */
    virtual void OnConversationShareCount(int ctype, int64_t cid, int count) = 0;

    /**
     *  弹出选择联系人窗口，选择共享对象（「文件共享」窗口的「发起共享」，2026-10-03 起）：候选为通讯录（不含本人），
     *  可以多选；确认后把已选的人交给文件共享宿主（IMemberPickSink）。
     *  @param owner 发起的窗口，可为 NULL
     *  @return 已弹出返回 true；离线或通讯录还没加载时返回 false
     */
    virtual bool OpenShareTargetPicker(HWND owner) = 0;

    /**
     *  代文件共享新建一个由本人与这些人组成的群。应答到达后主面板经 CFileShareHost::OnShareGroupCreated 交回，
     *  不经新建群对话框。
     *  @param groupName 群名（UTF-8，DLL 拼好的「共享 <共享名> 给 <成员名>」）
     *  @param memberIds 成员 userid（不含本人）
     *  @return 请求已发出返回 true；离线时返回 false
     */
    virtual bool CreateShareGroup(const std::string& groupName, const std::vector<int64_t>& memberIds) = 0;

    /**
     *  取一个用户的头像（2026-10-03 起，发起共享对话框与「文件共享」窗口的成员行）：与主面板联系人列表同一口径。
     *  照片头像还没下载到本机时同时安排下载，此时返回的 m_imagePath 为空；下载完成、或联系人修改头像后主面板调
     *  CFileShareHost::OnAvatarsUpdated。
     *  @param userId 用户 userid
     *  @return 头像
     */
    virtual ShareUserAvatar GetShareUserAvatar(int64_t userId) = 0;

    /**
     *  提示音是否关着（2026-10-07 起，文件共享的弹窗通知响不响）：系统设置的「提示音」与托盘「关闭所有声音」写的是同一个位
     *  （CUserConfig::IsEnableMute）。
     *  @return 关着（静音）返回 true
     */
    virtual bool IsShareNoticeMuted() = 0;

    /**
     *  系统设置里是否打开了「勿扰时段」（2026-10-07 起，文件共享的弹窗通知在时段内不弹）。
     *  @return 打开了返回 true
     */
    virtual bool IsDoNotDisturbEnabled() = 0;
};

/**
 *  把 UTF-8 文本写进定长缓冲区：写不下时在 UTF-8 字符边界处截断，结果始终以 '\0' 结尾。
 *  @param text  UTF-8 文本
 *  @param buf   [出参] 缓冲区
 *  @param bytes 缓冲区字节数，至少为 1
 */
void CopyUtf8Truncated(const std::string& text, char* buf, size_t bytes);

/**
 *  群成员列表转成接口结构体。
 *  @param result    群成员列表（cmd 1304 的应答解析结果）
 *  @param directory 取通讯录显示名（借用，可为 NULL，此时名片与昵称都为空的成员显示为「用户<id>」）
 *  @return 与 result.m_members 一一对应的成员（含本人，由 DLL 自行排除）
 */
std::vector<fileshare::FileShareMemberInfo> BuildGroupMemberInfos(const net::GroupMembersResult& result,
                                                                  IFileShareDirectory* directory);

/**
 *  单聊对方一人转成接口结构体。
 *  @param peerUserId 对方 userid
 *  @param directory  取通讯录显示名（借用，可为 NULL）
 */
fileshare::FileShareMemberInfo BuildPeerMemberInfo(int64_t peerUserId, IFileShareDirectory* directory);

/**
 *  没有主面板可问时（未登记 IFileShareDirectory）的头像：与主面板对没设置过头像的人的画法相同——显示名首字，底色按 userid 取。
 *  @param userId 用户 userid
 *  @param name   显示名；为空时字形为「?」
 *  @return 字形头像（m_imagePath 为空）
 */
ShareUserAvatar DefaultShareUserAvatar(int64_t userId, const std::wstring& name);

/**
 *  头像写进接口约定的定长结构体。字形超出容量时按字符截断，不拆开 UTF-16 代理对；照片路径放不下时整体留空、改画字形，
 *  不截断（截断后的路径指向的是另一个文件）。
 *  @param avatar 头像
 *  @param out    [出参] 调用方分配，m_cbSize 须等于 sizeof(FileShareAvatarInfo)
 *  @return 已写入返回 true；out 为 NULL 或 m_cbSize 不符时返回 false，不写入
 */
bool FillAvatarInfo(const ShareUserAvatar& avatar, fileshare::FileShareAvatarInfo* out);

} // namespace balloonfileshare
