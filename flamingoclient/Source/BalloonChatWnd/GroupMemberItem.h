/**
 *  GroupMemberItem.h
 *
 *  群聊窗右侧边栏成员列表里的一行 —— 头像 + 昵称（+ 群主徽标）。
 *  纯绘制控件，悬停时显示浅灰底。
 *
 *  本文件遵循 balloonui 头文件约定：包含它之前调用方须已引入 ATL/WTL。
 *
 *  balloonwj@qq.com   2026-05-22
 */

#pragma once

#include "../../balloonui/balloonui/DuiControl.h"

//真图头像解码用的 GDI+ 位图前置声明，避免在头文件引入 <gdiplus.h>。
namespace Gdiplus { class Bitmap; }

namespace balloonchatwnd {

/**
 *  GroupMemberItem —— 群成员列表的一行。
 */
class GroupMemberItem : public balloonwjui::DuiControl
{
public:
    GroupMemberItem();
    ~GroupMemberItem();

    /**
     *  设置本行成员信息。
     *    name：            成员昵称。
     *    avatarText：      头像内显示的字（真图头像缺图回退时也用它）。
     *    avatarColor：     头像底色。
     *    netRole：         该成员在本群的角色，取值见 grouprole::NetGroupRole
     *                      （0 成员 / 1 管理员 / 2 群主）；群主行右侧显示「群主」
     *                      徽标、管理员行显示「管理员」徽标，普通成员无徽标。
     *    avatarImagePath： 成员真实图片头像本地路径；空=画字形，非空=画真图。
     */
    void SetMember(const CString& name, const CString& avatarText,
                   COLORREF avatarColor, int netRole,
                   const CString& avatarImagePath = CString());

    /**
     *  设置登录者本人在本群的角色，取值见 grouprole::NetGroupRole，负值表示未知。
     *  右键菜单据此决定要不要给出「设为管理员 / 撤销管理员」——只有群主能看到。
     */
    void SetMyNetRole(int netRole) { m_myNetRole = netRole; }

    // 设置 / 读取本行成员 userid（真图头像下载完成后宿主按它定位本行刷新；0=未设）。
    void SetUserId(int64_t userId) { m_userid = userId; }
    int64_t UserId() const { return m_userid; }

    // 设置本行是不是"本人"。右键菜单据此只留「查看资料」—— 自己跟自己聊天、
    // 自己 @ 自己都没意义，与气泡里 @ 到自己时的处理保持一致。
    void SetIsSelf(bool isSelf) { m_isSelf = isSelf; }

    /**
     *  查询某 userid 当前是否被本人「特别关注」。
     *    userdata：SetStarredQuery 时透传的用户数据（一般是群聊窗 this）。
     *    uid：     待查的 userid。
     *  @return 已关注返回 true。
     */
    typedef bool (*StarredQuery)(void* userdata, long long uid);

    /**
     *  装上"该联系人是否被特别关注"的查询钩子，右键菜单据它决定那一项的文案。
     *
     *  用钩子而不是 SetStarred(bool) 那样的状态位：关注状态可能在别处（主面板的
     *  联系人 / 会话右键菜单）被改掉，存一份在行里就会过期，而成员侧栏不会因此重建。
     *  与 ChatMessageList::SetStarredQuery 是同一套路子，两处共用群聊窗的同一个钩子函数。
     *    fn：      查询回调；传 NULL 表示卸载（此时菜单一律显示"特别关注"）。
     *    userdata：回调透传的用户数据（借用，不持有）。
     */
    void SetStarredQuery(StarredQuery fn, void* userdata)
    {
        m_starredQuery   = fn;
        m_starredQueryUd = userdata;
    }

    /**
     *  DuiControl 覆写：右键弹出成员操作菜单（与 TA 聊天 / 查看资料 / @TA /
     *  复制 TA 的昵称），与气泡里 @某人 片段的右键菜单共用同一套模型与分发消息。
     *  选中项经 WM_CHAT_MENTION_ACTION 投给宿主群聊窗执行。
     *    pt：      落点（宿主客户区坐标）。
     *    mkFlags： 修饰键状态，未使用。
     *  @return 已处理返回 true。
     */
    bool OnRButtonDown(POINT pt, UINT mkFlags) override;

    // 更新本行成员的真图头像本地路径（真图下载完成后宿主调用）。路径变化时丢弃旧位图
    // 缓存以重新解码并触发重绘；传空串回退画字形头像。
    void SetAvatarImage(const CString& path);

    // DuiControl 覆写：绘制头像 + 昵称（+ 群主徽标，+ 悬停底）。
    void OnPaint(HDC hdc, const RECT& rcDirty) override;

    // DuiControl 覆写：用普通箭头光标。
    bool OnSetCursor(POINT pt) override { return false; }

private:
    // 释放缓存的头像真图位图（换路径 / SetMember / 析构时调用）。
    void ReleaseAvatarBitmap();

private:
    bool     m_isSelf;           // 本行是不是本人（决定右键菜单形态）
    int64_t  m_userid;           // 成员 userid（真图下载完成后按它定位本行；0=未设）
    CString  m_name;             // 成员昵称
    CString  m_avatarText;       // 头像字
    COLORREF m_avatarColor;      // 头像底色
    int      m_netRole;          // 本行成员在本群的角色，取值见 grouprole::NetGroupRole，负值=未知
    int      m_myNetRole;        // 登录者本人在本群的角色，取值同上，负值=未知（决定菜单里有无管理项）
    StarredQuery m_starredQuery;   // "该联系人是否被特别关注"的查询钩子（借用，不持有）；NULL=未装，菜单按"未关注"显示
    void*        m_starredQueryUd; // 上面那个钩子的透传数据（借用，不持有）
    CString  m_avatarImagePath;  // 成员真图头像本地路径（空=画字形）

    // 真图解码缓存：首次绘制按 m_avatarImagePath 解码、之后复用；换路径 / 析构释放。
    Gdiplus::Bitmap* m_avatarBmp;        // 解码后的头像真图；NULL=未解码 / 无图 / 失败
    bool             m_avatarLoadTried;  // 是否已尝试解码（失败也置位，避免每帧重试）
};

} // namespace balloonchatwnd
