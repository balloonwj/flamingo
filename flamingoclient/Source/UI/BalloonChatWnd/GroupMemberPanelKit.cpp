/**
 *  群成员列表的共用部分，见 GroupMemberPanelKit.h。
 *  balloonwj@qq.com   2026-10-08
 */
#include "stdafx.h"
#include "GroupMemberPanelKit.h"

#include <string.h>

#include "GroupMemberList.h"
#include "GroupMemberOrder.h"        // OrderMemberIndicesByRole：平铺视图的顺序（群主 → 管理员 → 成员）
#include "GroupMemberDeptTree.h"     // BuildGroupMemberDeptTree：按部门视图的行序列
#include "GroupDeptHeaderItem.h"     // 按部门视图的部门标题行
#include "GroupMentionMenu.h"        // MentionMenuId
#include "GroupMustRead.h"           // ShouldShowMustReadMark：未确认入群必读的标记
#include "GroupFollowTarget.h"       // FMG_MSG_TOGGLE_GROUP_FOLLOW 的负载
#include "ChatAvatarResolve.h"       // ResolveAvatarRender：按成员的头像设置解析字形、底色与真图路径
#include "../BalloonMain/AvatarPalette.h"   // AvatarColorForUser：查不到成员信息时的头像底色
#include "../BalloonMain/GroupExitFlow.h"   // TransferOwnerRequest / BuildTransferConfirmText
#include "../../Session/UserSessionData.h"  // FMG_MSG_OPEN_CHAT 等发给主面板的消息
#include "../../net/GroupProtocolClient.h"  // net::GroupMemberItem / net::DepartmentItem
#include "../../net/IUSocket.h"             // SendSetMemberRole / SendKickGroupMembers
#include "../../Common/Log/IULog.h"
#include "../../Common/I18n/I18n.h"         // TR / TRF

namespace balloonchatwnd {

namespace {

//UTF-8 → CString
CString Utf8ToCStr(const std::string& text)
{
    if (text.empty())
        return CString();
    return CString(CA2W(text.c_str(), CP_UTF8));
}

//把一段文字放进剪贴板（「复制名字」）；空串不动用户原有的剪贴板内容
void PutTextToClipboard(HWND owner, const CString& text)
{
    if (text.IsEmpty() || !::OpenClipboard(owner))
        return;
    ::EmptyClipboard();
    const size_t bytes = (static_cast<size_t>(text.GetLength()) + 1) * sizeof(WCHAR);
    HGLOBAL mem = ::GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (mem != NULL)
    {
        void* p = ::GlobalLock(mem);
        if (p != NULL)
        {
            ::memcpy(p, static_cast<LPCWSTR>(text), bytes);
            ::GlobalUnlock(mem);
            //交给剪贴板之后所有权归系统，不能再 GlobalFree
            ::SetClipboardData(CF_UNICODETEXT, mem);
        }
        else
        {
            ::GlobalFree(mem);
        }
    }
    ::CloseClipboard();
}

//主面板窗口是否可用
bool PanelAlive(HWND hostPanel)
{
    return hostPanel != NULL && ::IsWindow(hostPanel);
}

} // namespace

std::map<int64_t, GroupMemberRowInfo> BuildGroupMemberRowInfo(const std::vector<net::GroupMemberItem>& members,
                                                              int64_t selfId)
{
    std::map<int64_t, GroupMemberRowInfo> out;
    for (size_t i = 0; i < members.size(); ++i)
    {
        const net::GroupMemberItem& m = members[i];
        //显示名：群名片优先 → 账号昵称 → 「用户<id>」
        CString name = Utf8ToCStr(m.m_card);
        if (name.IsEmpty())
            name = Utf8ToCStr(m.m_nickname);
        if (name.IsEmpty())
            name.Format(TR(L"用户%lld"), static_cast<long long>(m.m_userid));
        //按成员的 (facetype, customface) 解析头像：字形 / 自动头像给字形与底色，真图缓存命中给路径、未命中排队下载
        const AvatarRender ar = ResolveAvatarRender(m.m_userid, m.m_facetype, m.m_customface, name, selfId);
        GroupMemberRowInfo info;
        info.m_name = name;
        info.m_glyph = ar.m_glyph;
        info.m_color = ar.m_color;
        info.m_facetype = m.m_facetype;
        info.m_customface = m.m_customface;
        info.m_imagePath = ar.m_imagePath;
        info.m_role = m.m_role;
        out[m.m_userid] = info;
    }
    return out;
}

std::unique_ptr<GroupMemberItem> MakeGroupMemberRow(const net::GroupMemberItem& m,
                                                   const std::map<int64_t, GroupMemberRowInfo>& info,
                                                   const GroupMemberRowContext& ctx)
{
    std::map<int64_t, GroupMemberRowInfo>::const_iterator mi = info.find(m.m_userid);
    const bool known = (mi != info.end());
    const CString name = known ? mi->second.m_name : CString();
    const CString glyph = (known && !mi->second.m_glyph.IsEmpty()) ? mi->second.m_glyph : name.Left(1);
    const COLORREF color = known ? mi->second.m_color : balloonmain::AvatarColorForUser(m.m_userid);
    const CString imagePath = known ? mi->second.m_imagePath : CString();
    std::unique_ptr<GroupMemberItem> item(new GroupMemberItem());
    item->SetUserId(m.m_userid);   // 真图下载完成后按它定位本行刷新
    // 本人那一行：右键菜单只留「查看资料」
    item->SetIsSelf(ctx.m_selfId != 0 && m.m_userid == ctx.m_selfId);
    // m_role 就是网络侧角色取值，与 grouprole::NetGroupRole 同一套，直接透传
    item->SetMember(name, glyph, color, m.m_role, imagePath);
    // 本人角色决定右键菜单里的管理项（设为 / 撤销管理员、转让群主、移出）
    item->SetMyNetRole(ctx.m_myRole);
    // 头像角上的在线状态徽标：查不到时不画
    int presence = kMemberPresenceUnknown;
    if (ctx.m_presence != NULL)
    {
        std::map<int64_t, int>::const_iterator pit = ctx.m_presence->find(m.m_userid);
        if (pit != ctx.m_presence->end())
            presence = pit->second;
    }
    item->SetPresence(presence);
    // 本端连接态：本人那一行据此决定头像是否画成灰度
    item->SetSelfConnectionState(ctx.m_connUiState);
    // 右键菜单前现问主面板的关注名单（别处改过之后这里不会重建，不能存状态位）
    item->SetStarredQuery(ctx.m_starredFn, ctx.m_starredUser);
    item->SetGroupFollowQuery(ctx.m_followFn, ctx.m_followUser);
    // 没有输入框的窗口（帖子群的成员面板）右键菜单里不出现「@TA」
    item->SetMentionAllowed(ctx.m_allowMention);
    // 尚未确认入群必读的成员：群主与管理员看得到「未确认必读」标记（docs/群公告接入方案.md §14）
    item->SetMustReadMark(ShouldShowMustReadMark(ctx.m_myRole, m.m_mustread));
    return item;
}

size_t FillGroupMemberList(GroupMemberList* list, const std::vector<net::GroupMemberItem>& members,
                           const std::map<int64_t, GroupMemberRowInfo>& info, bool treeMode,
                           const std::vector<net::DepartmentItem>& depts, const GroupMemberRowContext& ctx)
{
    if (list == NULL)
        return 0;
    size_t rowCount = 0;
    // 先清空既有行（成员刷新、切换视图、部门数组更新都会重建）
    list->ClearRows();
    list->SetTreeMode(treeMode);
    if (!treeMode)
    {
        // 平铺：群主 → 管理员 → 普通成员，同一角色内保持服务端返回的原顺序
        std::vector<int> roles;
        roles.reserve(members.size());
        for (size_t i = 0; i < members.size(); ++i)
            roles.push_back(members[i].m_role);
        const std::vector<std::size_t> order = OrderMemberIndicesByRole(roles);
        for (size_t k = 0; k < order.size(); ++k)
        {
            std::unique_ptr<GroupMemberItem> item = MakeGroupMemberRow(members[order[k]], info, ctx);
            list->AddRow(std::unique_ptr<balloonwjui::DuiControl>(item.release()), -1);
            ++rowCount;
        }
    }
    else
    {
        // 按部门：部门标题行与成员行按部门树的顺序挂上，每行记下父部门行（docs/群成员部门树方案.md）
        std::vector<DeptTreeMemberInput> input;
        std::map<int64_t, size_t> indexById;
        for (size_t i = 0; i < members.size(); ++i)
        {
            DeptTreeMemberInput in;
            in.m_userId = members[i].m_userid;
            in.m_role = members[i].m_role;
            input.push_back(in);
            if (indexById.count(in.m_userId) == 0)
                indexById[in.m_userId] = i;
        }
        const std::vector<DeptTreeRow> rows = BuildGroupMemberDeptTree(input, depts);
        for (size_t r = 0; r < rows.size(); ++r)
        {
            const DeptTreeRow& row = rows[r];
            if (row.m_kind == kDeptTreeRowDept)
            {
                std::unique_ptr<GroupDeptHeaderItem> head(new GroupDeptHeaderItem());
                const CString deptName = (row.m_deptId == kDeptTreeUnassignedId) ? CString(TR(L"未分配部门"))
                                                                                  : Utf8ToCStr(row.m_deptName);
                head->SetDept(row.m_deptId, deptName, row.m_memberCount, row.m_depth);
                list->AddRow(std::unique_ptr<balloonwjui::DuiControl>(head.release()), row.m_parent);
                ++rowCount;
                continue;
            }
            std::map<int64_t, size_t>::const_iterator it = indexById.find(row.m_userId);
            if (it == indexById.end())
                continue;
            std::unique_ptr<GroupMemberItem> item = MakeGroupMemberRow(members[it->second], info, ctx);
            item->SetTreeLayout(row.m_depth, row.m_secondary);
            list->AddRow(std::unique_ptr<balloonwjui::DuiControl>(item.release()), row.m_parent);
            ++rowCount;
        }
    }
    // 选中的人不在新名单里时取消选中，并按当前视图刷新各行的选中显示（平铺视图的选中，2026-10-08）
    list->CommitRows();
    // 按搜索框里的关键词重新决定各行显隐（部门树视图下还要按折叠状态），并重新排版
    list->ReapplyFilter();
    RECT rc = list->GetRect();
    if (rc.right > rc.left && rc.bottom > rc.top)
        list->ForceLayout(rc);
    return rowCount;
}

bool ExecuteGroupMemberAction(int action, int64_t groupId, int64_t uid, const CString& name, HWND owner,
                              HWND hostPanel)
{
    LOG_INFO("group member action: group=%lld, uid=%lld, action=%d.", static_cast<long long>(groupId),
             static_cast<long long>(uid), action);
    switch (action)
    {
    //发消息：打开与该成员的单聊窗，经主面板路由
    case kMentionMenuChat:
        if (PanelAlive(hostPanel))
            ::PostMessage(hostPanel, FMG_MSG_OPEN_CHAT, static_cast<WPARAM>(uid), 0);
        return true;

    //查看资料：打开该成员的联系人资料窗，经主面板路由
    case kMentionMenuProfile:
        if (PanelAlive(hostPanel))
            ::PostMessage(hostPanel, FMG_MSG_OPEN_CONTACT_PROFILE, static_cast<WPARAM>(uid), 0);
        return true;

    //复制名字
    case kMentionMenuCopyName:
        PutTextToClipboard(owner, name);
        return true;

    //特别关注 / 取消：名单在主面板手上，由它按现状取反后发协议
    case kMentionMenuStarred:
        if (PanelAlive(hostPanel))
            ::PostMessage(hostPanel, FMG_MSG_TOGGLE_STARRED, static_cast<WPARAM>(uid), 0);
        return true;

    //关注 / 取消关注 TA 在本群的发言：由主面板按缓存取反后发 cmd_set_group_follow（docs/消息提醒模式与消息盒子方案.md §12）
    case kMentionMenuGroupFollow:
        if (PanelAlive(hostPanel))
        {
            GroupFollowTarget* target = new GroupFollowTarget();
            target->m_groupId = groupId;
            target->m_userId = uid;
            if (!::PostMessage(hostPanel, FMG_MSG_TOGGLE_GROUP_FOLLOW, 0, reinterpret_cast<LPARAM>(target)))
                delete target;
        }
        return true;

    //设为管理员：菜单只对群主出现，服务端还会再校验；不预改界面，等成员变化推送回来统一刷新
    case kMentionMenuSetAdmin:
        CIUSocket::GetInstance().SendSetMemberRole(groupId, uid, true);
        return true;

    //撤销管理员
    case kMentionMenuCancelAdmin:
        CIUSocket::GetInstance().SendSetMemberRole(groupId, uid, false);
        return true;

    //转让群主：先确认，确认后交主面板发协议并登记等待应答，结果由主面板提示
    case kMentionMenuTransferOwner:
    {
        const std::wstring tip = balloonmain::BuildTransferConfirmText(static_cast<LPCTSTR>(name), false);
        if (::MessageBox(owner, tip.c_str(), TR(L"转让群主"), MB_OKCANCEL | MB_ICONWARNING | MB_DEFBUTTON2) != IDOK)
            return true;
        if (!PanelAlive(hostPanel))
        {
            LOG_WARNING("group member action: transfer owner ignored, no host panel, group=%lld.",
                        static_cast<long long>(groupId));
            return true;
        }
        balloonmain::TransferOwnerRequest* req = new balloonmain::TransferOwnerRequest();
        req->m_groupId = groupId;
        req->m_newOwner = uid;
        req->m_newOwnerName = static_cast<LPCTSTR>(name);
        req->m_thenLeave = false;
        if (!::PostMessage(hostPanel, FMG_MSG_TRANSFER_GROUP_OWNER, 0, reinterpret_cast<LPARAM>(req)))
        {
            delete req;
            LOG_ERROR("group member action: post transfer owner failed, group=%lld, uid=%lld.",
                      static_cast<long long>(groupId), static_cast<long long>(uid));
        }
        return true;
    }

    //移出不可撤销，先确认；文案与群管理窗的「移出成员」一致
    case kMentionMenuKick:
    {
        CString tip;
        tip.Format(TR(L"确定把「%s」移出本群吗？\n\n移出后该成员将无法再看到本群消息。"), static_cast<LPCTSTR>(name));
        if (::MessageBox(owner, tip, TR(L"移出成员"), MB_OKCANCEL | MB_ICONWARNING | MB_DEFBUTTON2) != IDOK)
            return true;
        std::vector<int64_t> ids(1, uid);
        CIUSocket::GetInstance().SendKickGroupMembers(groupId, ids);
        return true;
    }

    //「@TA」要往各窗口自己的输入框里插字；不认识的项也交回调用方
    default:
        return false;
    }
}

} // namespace balloonchatwnd
