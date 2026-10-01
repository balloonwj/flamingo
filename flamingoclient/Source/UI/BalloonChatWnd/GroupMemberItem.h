/**
 *  GroupMemberItem.h
 *
 *  群聊窗右侧边栏成员列表里的一行 —— 头像 + 昵称（+ 群主徽标）。
 *  悬停时显示浅灰底；右键弹成员操作菜单，双击打开与该成员的单聊窗。
 *
 *  本文件遵循 balloonui 头文件约定：包含它之前调用方须已引入 ATL/WTL。
 *
 *  balloonwj@qq.com   2026-05-22
 */

#pragma once

#include "../../../third-party/balloonui/balloonui/DuiControl.h"

//真图头像解码用的 GDI+ 位图前置声明，避免在头文件引入 <gdiplus.h>。
namespace Gdiplus { class Bitmap; }

namespace balloonchatwnd {

// 成员行头像的边长（像素），以及头像与昵称之间的间距（像素）。
const int kGroupMemberAvatarPx    = 26;
const int kGroupMemberAvatarGapPx = 8;

// 成员在线状态的"未知"取值：成员不在本人通讯录里、查不到在线状态时取它，此时头像上不画
// 状态徽标。其余取值同协议的 presence_type（0 离线 / 1 在线 / 2 离开 / 3 忙碌）。
const int kMemberPresenceUnknown = -1;

/**
 *  算成员行头像右下角在线状态徽标的绘制方框（交给 balloonmain::DrawPresenceBadge 的区域）。
 *  徽标中心距头像右、下边缘各 6 像素，加上外圈白描边后仍收在头像以内，不会压到右侧的昵称。
 *    avatarLeft / avatarTop：头像左上角（设备坐标）。
 *    avatarSize：           头像边长（像素）。
 *  @return 徽标绘制方框（设备坐标）。纯函数，无副作用。
 */
RECT ComputeMemberPresenceBadgeBox(int avatarLeft, int avatarTop, int avatarSize);

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

    // 读取本行记录的登录者本人角色（SetMyNetRole 设入的值），取值同上。
    int MyNetRole() const { return m_myNetRole; }

    /**
     *  设置本行成员的在线状态，头像右下角据此画状态徽标（与主面板通讯录同一套图形）。
     *    presence：取值同协议的 presence_type（0 离线画灰色空心圆环 / 1 在线 / 2 离开 /
     *              3 忙碌）；kMemberPresenceUnknown 表示未知，不画徽标；其它值按未知处理。
     *  取值变化时请求重绘。
     */
    void SetPresence(int presence);

    // 读取本行成员的在线状态，取值同 SetPresence。
    int Presence() const { return m_presence; }

    // 读取本行成员显示的名字（SetMember 设入的 name）。成员侧栏的搜索按它匹配关键词。
    const CString& Name() const { return m_name; }

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

    /**
     *  DuiControl 覆写：双击打开与本行成员的单聊窗。投给宿主群聊窗的消息与右键菜单
     *  选「与 TA 聊天」完全相同（WM_CHAT_MENTION_ACTION + kMentionMenuChat），后续由
     *  群聊窗转请主面板打开或激活单聊窗。
     *  本人那一行、未绑定 userid 的行、没有可用宿主窗口时不投递，交还基类处理。
     *  需宿主对 DuiHost 开启 EnableDoubleClick(true) 才会被调用（群聊窗已开启）。
     *    pt：      落点（宿主客户区坐标），未使用。
     *    mkFlags： 修饰键状态，未使用。
     *  @return 已投递返回 true；未处理时返回基类的结果。
     */
    bool OnLButtonDblClk(POINT pt, UINT mkFlags) override;

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

    // 把"对本行成员执行某个动作"打包成堆上的 MentionActionData，经
    // WM_CHAT_MENTION_ACTION 投给宿主群聊窗；右键菜单与双击共用，保证两处投出的
    // 负载一致。投递失败时就地释放负载。
    //   hwnd：  宿主窗口句柄，调用方须已确认 ::IsWindow(hwnd)。
    //   action：动作编号，取值见 MentionMenuId。
    // @return 投递成功返回 true。
    bool PostMemberAction(HWND hwnd, int action);

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
    int      m_presence;         // 成员在线状态，取值同 presence_type；kMemberPresenceUnknown=未知，不画徽标

    // 真图解码缓存：首次绘制按 m_avatarImagePath 解码、之后复用；换路径 / 析构释放。
    Gdiplus::Bitmap* m_avatarBmp;        // 解码后的头像真图；NULL=未解码 / 无图 / 失败
    bool             m_avatarLoadTried;  // 是否已尝试解码（失败也置位，避免每帧重试）
};

} // namespace balloonchatwnd
