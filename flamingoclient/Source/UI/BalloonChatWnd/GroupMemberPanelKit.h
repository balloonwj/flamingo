/**
 *  GroupMemberPanelKit.h
 *
 *  群成员列表的共用部分（docs/帖子群方案.md 14.2 第 5e 步）：群聊窗右侧边栏的「成员」页签与帖子群窗口的群成员面板
 *  显示同一份成员列表，下面这些步骤从 CBalloonGroupChatWindow 抽出来，两处共用，保证两边的行为一致：
 *    - BuildGroupMemberRowInfo：由 1304 应答算出每个人的显示名（群名片 → 昵称 → 「用户<id>」）与头像；
 *    - MakeGroupMemberRow：建一行成员（头像、名字、角色徽标、在线状态、右键菜单需要的角色与查询钩子）；
 *    - FillGroupMemberList：按平铺（群主、管理员、成员）或按部门把成员行挂到 GroupMemberList 上；
 *    - ExecuteGroupMemberAction：执行成员右键菜单的一项（发消息、资料、复制名字、特别关注、关注发言、
 *      设为 / 撤销管理员、转让群主、移出）；「@TA」要往各窗口自己的输入框里插字，不在这里处理。
 *
 *  本文件遵循 balloonui 头文件约定：包含它之前调用方须已引入 ATL/WTL（Flamingo 侧由 stdafx.h 保证）。
 *
 *  典型用法（群聊窗收到成员名单后）：
 *      m_memberRowInfo = balloonchatwnd::BuildGroupMemberRowInfo(members.m_members, selfId);
 *      balloonchatwnd::FillGroupMemberList(list, members.m_members, m_memberRowInfo, treeMode, depts, ctx);
 *
 *  balloonwj@qq.com   2026-10-08
 */
#pragma once

#include <stdint.h>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "GroupMemberItem.h"

namespace net {
struct GroupMemberItem;
struct DepartmentItem;
}

namespace balloonchatwnd {

class GroupMemberList;

/**
 *  成员行要用到的一个人的显示信息（由 1304 应答算出，群聊窗另用它给消息气泡取名字与头像）。
 */
struct GroupMemberRowInfo
{
    CString     m_name;        // 显示名：群名片优先，其次账号昵称，都没有时为「用户<id>」
    CString     m_glyph;       // 头像字形（按成员的 facetype / customface 解出；字形头像为其选定值，否则为首字）
    COLORREF    m_color;       // 头像底色
    int         m_facetype;    // 头像类型（0 自动 / 1 字形 / 2 真图），见 AvatarSpec.h
    std::string m_customface;  // 头像标识（UTF-8）：字形为 "glyph|colorIdx"，真图为 fileid
    CString     m_imagePath;   // 真图头像在本机的路径；空表示画字形（真图还没下载时也为空，下载完成后由宿主回填）
    int         m_role;        // 该成员在本群的角色（0 成员 1 管理员 2 群主）；-1 表示未知（群聊窗调试入口的模拟成员）

    GroupMemberRowInfo()
        : m_color(0)
        , m_facetype(0)
        , m_role(-1)
    {
    }
};

/**
 *  建成员行时用到的、与具体窗口有关的东西。
 */
struct GroupMemberRowContext
{
    int64_t m_selfId;           // 本人 userid；0 表示未知
    int     m_myRole;           // 本人在本群的角色（grouprole::NetGroupRole 取值），决定右键菜单里的管理项
    int     m_connUiState;      // 本端连接状态（掉线时本人那一行的头像画成灰度），取值见 GroupMemberItem
    const std::map<int64_t, int>* m_presence;   // userid → 在线状态（借用，可为 NULL）；查不到时不画状态徽标
    GroupMemberItem::StarredQuery m_starredFn;  // 右键菜单前问「是否特别关注」的钩子；可为 NULL
    void*   m_starredUser;      // m_starredFn 的 userdata（借用）
    GroupMemberItem::StarredQuery m_followFn;   // 右键菜单前问「是否关注其在本群的发言」的钩子；可为 NULL
    void*   m_followUser;       // m_followFn 的 userdata（借用）
    bool    m_allowMention;     // 右键菜单里有没有「@TA」：群聊窗有输入框时为 true，帖子群的成员面板为 false

    GroupMemberRowContext()
        : m_selfId(0)
        , m_myRole(0)
        , m_connUiState(0)
        , m_presence(NULL)
        , m_starredFn(NULL)
        , m_starredUser(NULL)
        , m_followFn(NULL)
        , m_followUser(NULL)
        , m_allowMention(true)
    {
    }
};

/**
 *  由一份 1304 应答算出每个人的显示信息。
 *  @param members 成员（1304 应答的 members）
 *  @param selfId  本人 userid（头像解析时本人用自己的设置）
 *  @return userid → 显示信息；真图头像还没在本机时同时排队下载，完成后由宿主经各自的 OnAvatarDownloaded 回填路径
 */
std::map<int64_t, GroupMemberRowInfo> BuildGroupMemberRowInfo(const std::vector<net::GroupMemberItem>& members,
                                                              int64_t selfId);

/**
 *  建一行成员。
 *  @param m    这个成员（1304 应答里的一项）
 *  @param info BuildGroupMemberRowInfo 的结果；查不到这个人时用 userid 推出的底色与空名字兜底
 *  @param ctx  窗口相关的上下文
 *  @return 新建的成员行，所有权交给调用方
 */
std::unique_ptr<GroupMemberItem> MakeGroupMemberRow(const net::GroupMemberItem& m,
                                                   const std::map<int64_t, GroupMemberRowInfo>& info,
                                                   const GroupMemberRowContext& ctx);

/**
 *  清空成员列表并按当前视图重新挂上全部成员行，再按搜索框里的关键词重新过滤、重新排版。
 *  @param list     成员列表控件（借用）；为 NULL 时什么也不做
 *  @param members  成员，按服务端给的顺序
 *  @param info     BuildGroupMemberRowInfo 的结果
 *  @param treeMode true 按部门显示（BuildGroupMemberDeptTree），false 平铺（群主、管理员、成员）
 *  @param depts    企业的部门数组（按部门显示时用）
 *  @param ctx      窗口相关的上下文
 *  @return 挂上的行数（含部门标题行）
 */
size_t FillGroupMemberList(GroupMemberList* list, const std::vector<net::GroupMemberItem>& members,
                           const std::map<int64_t, GroupMemberRowInfo>& info, bool treeMode,
                           const std::vector<net::DepartmentItem>& depts, const GroupMemberRowContext& ctx);

/**
 *  执行成员右键菜单（或双击）的一项。发消息、资料、特别关注、关注发言、转让群主经主面板执行；设为 / 撤销管理员与移出
 *  直接发协议（服务端还会再校验权限）；转让群主与移出先弹确认框。
 *  @param action    菜单项，取值见 MentionMenuId
 *  @param groupId   所在的群
 *  @param uid       目标成员
 *  @param name      目标成员的显示名（复制名字、确认框文字用）
 *  @param owner     确认框的所有者窗口
 *  @param hostPanel 主面板窗口；为 NULL 或已销毁时经它执行的几项不做
 *  @return 已处理返回 true；kMentionMenuMentionMore 与不认识的项返回 false，由调用方处理
 */
bool ExecuteGroupMemberAction(int action, int64_t groupId, int64_t uid, const CString& name, HWND owner,
                              HWND hostPanel);

} // namespace balloonchatwnd
