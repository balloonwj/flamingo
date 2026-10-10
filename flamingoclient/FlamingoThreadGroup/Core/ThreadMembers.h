/**
 *  帖子群的群成员名单（纯逻辑，不含界面，docs/帖子群方案.md 14.2 第 5a 步）：宿主经 IThreadGroupModule::OnGroupMembers
 *  交来的成员（userid、群角色、显示名）存在这里，帖子群窗口持有一份，发帖窗口与留言框的「@ 成员」都从这里取名单与名字。
 *
 *  显示名由宿主按普通群 @ 的口径算好：群名片，没有时用昵称，不含本人给对方起的备注。备注只是本人看的记号，@ 的文字会
 *  随正文发给全群，不能带出去（2026-10-07 实机联调发现的问题 P1）。
 *
 *  典型用法：
 *      CThreadMemberBook book;
 *      book.Set(members);                                         //名单到达
 *      std::vector<ThreadMember> pick = book.PickOrder(myUserId); //选人浮层：去掉本人，群主、管理员在前
 *      std::wstring name = book.NameOf(userId);                    //插入「@名字 」
 *
 *  balloonwj@qq.com   2026-10-08
 */
#pragma once

#include <stdint.h>
#include <string>
#include <vector>

namespace threadgroup {

//群角色（与服务端 Protocol.h 的 group_member_role 一致）
enum ThreadMemberRole
{
    thread_member_role_member = 0,   //普通成员
    thread_member_role_admin  = 1,   //管理员
    thread_member_role_owner  = 2,   //群主
};

//名单里的一个人
struct ThreadMember
{
    int64_t      m_userId;   //用户 ID，大于 0
    int          m_role;     //群角色，取值见 ThreadMemberRole；不认识的值按普通成员排序
    std::wstring m_name;     //显示名：群名片，没有时为昵称（宿主算好）；可能为空，此时由调用方兜底

    ThreadMember()
        : m_userId(0)
        , m_role(thread_member_role_member)
    {
    }
};

/**
 *  CThreadMemberBook —— 一个帖子群的成员名单。只在界面线程上使用。
 */
class CThreadMemberBook
{
public:
    CThreadMemberBook();

    /**
     *  换上一份新名单。userId 不大于 0 的项丢弃；同一个人出现多次时只保留第一次。调用后 IsLoaded 为 true。
     *  @param members 宿主交来的名单，按宿主给的顺序
     */
    void Set(const std::vector<ThreadMember>& members);

    //清空名单并回到「还没拉到」的状态（退出登录、群成员变化需要重拉时）
    void Clear();

    //名单是否已拉到过（拉到的名单可以为空）
    bool IsLoaded() const;

    //名单里的人数
    size_t Count() const;

    /**
     *  某人的显示名。
     *  @return 名单里有这个人且名字不空时返回名字；否则返回空串，由调用方兜底
     */
    std::wstring NameOf(int64_t userId) const;

    /**
     *  点正文里的 @某人弹成员菜单时，对方的群角色（docs/帖子群方案.md §15.1 第 4 条）。
     *  @return 名单里有这个人时为其群角色；名单已拉到但没有这个人时为 kMemberRoleLeft（已退群或被移出）；
     *          名单还没拉到时为 kMemberRoleUnknown（ThreadGroupApi.h）
     */
    int MenuRoleOf(int64_t userId) const;

    /**
     *  选人浮层的顺序：去掉 excludeUserId（通常是本人），群主在前、其次管理员、最后普通成员，同一角色内保持宿主给的顺序。
     *  @param excludeUserId 要去掉的人；传 0 时不去掉任何人
     */
    std::vector<ThreadMember> PickOrder(int64_t excludeUserId) const;

private:
    std::vector<ThreadMember> m_members;   //名单，按宿主给的顺序、已去重
    bool                      m_loaded;    //是否已拉到过名单
};

/**
 *  排序用的角色权重：群主 2、管理员 1、其余（含不认识的值）0。
 */
int ThreadMemberRoleRank(int role);

//群成员面板在帖子群窗口客户区里的位置（docs/帖子群方案.md 14.3）
struct ThreadPanelRect
{
    int m_left;     //左
    int m_top;      //上
    int m_right;    //右
    int m_bottom;   //下

    ThreadPanelRect()
        : m_left(0)
        , m_top(0)
        , m_right(0)
        , m_bottom(0)
    {
    }
};

/**
 *  群成员面板的位置：贴着客户区右边，从 top（标题栏与头部之下，「成员」按钮仍看得见）到底部；客户区比面板窄时
 *  面板占满客户区宽度。
 *  @param clientWidth  帖子群窗口客户区的宽，单位：像素
 *  @param clientHeight 客户区的高
 *  @param top          面板的上边
 *  @param panelWidth   面板的设计宽度
 *  @return 面板的矩形；客户区比 top 还矮时高度为 0
 */
ThreadPanelRect ComputeMemberPanelRect(int clientWidth, int clientHeight, int top, int panelWidth);

} // namespace threadgroup
