/**
 *  入群邀请卡片的纯逻辑（「邀请入群需被邀请人同意」开启时用，docs/邀请入群方案.md §3.3 ~ §3.4、§4.3 ~ §4.5）：
 *  卡片 content 的组装与下发时的状态合并、被邀请人点「加入群聊」时的判定、cmd_accept_group_invite（1327）与
 *  cmd_group_invite_changed_notify（1328）的报文编解码。
 *
 *  本模块只依赖 jsoncpp，不触及网络与数据库，便于单元测试（tests/test_groupinvite.cpp）。落库在 GroupInviteManager，
 *  发卡片、入群、推送在会话层（ChatSession）。
 *
 *  典型用法：
 *      GroupInviteRecord rec = ...;                       //刚写进 t_group_invite 的一行
 *      std::string content = GroupInviteProtocol::buildCardContent(rec, names);
 *      ...
 *      GroupInviteAcceptDecision d = GroupInviteProtocol::decideAccept(facts);
 *      if (d.m_action == group_invite_accept_join) { ...入群... }
 *
 *  balloonwj@qq.com   2026-10-04
 */
#pragma once

#include <stdint.h>
#include <string>
#include "Protocol.h"   //group_invite_state

//入群邀请的有效期：发出后 7 天（docs/邀请入群方案.md §3.3），由天数、每天秒数、每秒毫秒数相乘得出
static const int64_t kGroupInviteExpireDays = 7;
static const int64_t kGroupInviteSecondsPerDay = 86400;
static const int64_t kGroupInviteMillisPerSecond = 1000;
static const int64_t kGroupInviteExpireMs = kGroupInviteExpireDays * kGroupInviteSecondsPerDay * kGroupInviteMillisPerSecond;

//一张入群邀请，对应 t_group_invite 的一行
struct GroupInviteRecord
{
    int64_t m_id;            //邀请ID（自增主键）
    int64_t m_groupid;       //目标群ID
    int64_t m_inviter;       //邀请人userid
    int64_t m_invitee;       //被邀请人userid
    int     m_historyLimit;  //入群后可看的入群前消息条数：-1 全部，0 不同步，N 最近 N 条（kInviteHistoryLimitAll 等）
    int     m_state;         //落库的状态，取值见 group_invite_state 的 0 ~ 3
    int64_t m_msgid;         //卡片消息的 msgid（邀请人与被邀请人的单聊）；卡片落库之前为 0
    int64_t m_createTime;    //发出时间，Unix 毫秒
    int64_t m_expireTime;    //到期时间，Unix 毫秒
    int64_t m_handleTime;    //状态最后变化的时间，Unix 毫秒；0 表示还没变化过

    GroupInviteRecord()
        : m_id(0), m_groupid(0), m_inviter(0), m_invitee(0), m_historyLimit(-1), m_state(group_invite_pending)
        , m_msgid(0), m_createTime(0), m_expireTime(0), m_handleTime(0)
    {
    }
};

//生成卡片 content 时用到的展示信息（发出那一刻的值，下发时群名与头像会被群的当前资料覆盖）
struct GroupInviteCardNames
{
    std::string m_groupName;    //群名（UTF-8）
    int         m_facetype;     //群头像类型
    std::string m_customface;   //群自定义头像文件标识
    std::string m_inviterName;  //邀请人昵称（UTF-8）
    std::string m_inviteeName;  //被邀请人昵称（UTF-8）

    GroupInviteCardNames()
        : m_facetype(0)
    {
    }
};

//被邀请人点「加入群聊」时，判定要用到的全部事实（由会话层查好后传入 decideAccept）
struct GroupInviteAcceptFacts
{
    bool              m_found;            //邀请是否存在
    GroupInviteRecord m_invite;           //邀请本身；m_found 为 false 时无意义
    int64_t           m_selfId;           //点按钮的人
    int64_t           m_nowMs;            //当前时间，Unix 毫秒
    bool              m_groupExists;      //群是否还在
    bool              m_selfIsMember;     //点按钮的人此刻是否已在群里
    bool              m_inviterIsMember;  //邀请人此刻是否还在群里
    int               m_inviterRole;      //邀请人此刻在群里的角色，取值见 group_member_role；不在群里时无意义
    bool              m_memberInviteFree; //群设置 f_member_invite：普通成员邀请免审批

    GroupInviteAcceptFacts()
        : m_found(false), m_selfId(0), m_nowMs(0), m_groupExists(false), m_selfIsMember(false)
        , m_inviterIsMember(false), m_inviterRole(group_role_member), m_memberInviteFree(false)
    {
    }
};

//点「加入群聊」的判定结果里的动作
enum group_invite_accept_action
{
    group_invite_accept_join     = 0,   //直接入群：邀请人是群主或管理员，或群允许普通成员直接拉人
    group_invite_accept_approval = 1,   //转为加群申请：邀请人是普通成员且群需审批（先发卡片，后审批）
    group_invite_accept_reject   = 2,   //拒绝：错误码见 GroupInviteAcceptDecision::m_errorCode
};

//点「加入群聊」的判定结果
struct GroupInviteAcceptDecision
{
    int m_action;      //动作，取值见 group_invite_accept_action
    int m_errorCode;   //m_action 为拒绝时的错误码（313 / 314 / 305）；其余动作为 0

    GroupInviteAcceptDecision()
        : m_action(group_invite_accept_reject), m_errorCode(0)
    {
    }
};

/**
 *  GroupInviteProtocol —— 入群邀请卡片的纯逻辑，全部为静态函数。
 */
class GroupInviteProtocol
{
public:
    /**
     *  组装卡片落库的 content：{ inviteid, groupid, groupname, facetype, customface, inviterid, inviter, inviteeid,
     *  invitee, expiretime }。id 一律写成字符串，与群组段其它报文一致。
     *  @param rec   邀请（m_id 须已分配）
     *  @param names 发出那一刻的群名、群头像与双方昵称
     *  @return content 的紧凑 JSON 文本
     */
    static std::string buildCardContent(const GroupInviteRecord& rec, const GroupInviteCardNames& names);

    /**
     *  从卡片 content 里取邀请 id。
     *  @param content 卡片 content 的 JSON 文本
     *  @return 邀请 id；解析失败或没有该字段时返回 0
     */
    static int64_t parseCardInviteId(const std::string& content);

    /**
     *  算卡片此刻应显示的状态（docs/邀请入群方案.md §4.3）。落库的待加入、等待审批在被邀请人已在群里时按已加入显示；
     *  待加入时群已解散或邀请人已退群为已失效，超过到期时间为已过期。已加入、未通过审批原样返回。
     *  @param storedState     落库的状态（group_invite_state 的 0 ~ 3）
     *  @param expireTime      到期时间，Unix 毫秒
     *  @param nowMs           当前时间，Unix 毫秒
     *  @param groupExists     群是否还在
     *  @param inviterIsMember 邀请人是否还在群里
     *  @param inviteeIsMember 被邀请人是否已在群里
     *  @return 显示的状态，取值见 group_invite_state
     */
    static int computeDisplayState(int storedState, int64_t expireTime, int64_t nowMs, bool groupExists,
                                   bool inviterIsMember, bool inviteeIsMember);

    /**
     *  下发卡片时把状态与群的当前资料合并进 content（docs/邀请入群方案.md §4.3）：写 state；群还在时写 membercount，
     *  并用群当前的名字与头像覆盖 groupname / facetype / customface；群已解散时保留卡片里的原值。
     *  @param content     落库的 content
     *  @param state       显示的状态（computeDisplayState 的结果）
     *  @param groupExists 群是否还在
     *  @param groupName   群当前的名字（UTF-8）；群已解散时忽略
     *  @param facetype    群当前的头像类型；群已解散时忽略
     *  @param customface  群当前的自定义头像；群已解散时忽略
     *  @param memberCount 群当前的成员数；群已解散时忽略
     *  @return 合并后的 content；落库的 content 解析失败时原样返回
     */
    static std::string augmentCardContent(const std::string& content, int state, bool groupExists,
                                          const std::string& groupName, int facetype, const std::string& customface,
                                          int memberCount);

    /**
     *  被邀请人点「加入群聊」时的判定（docs/邀请入群方案.md §3.4、§4.4）。依次检查：邀请不存在或不属于本人、已处理过
     *  → 314；群已解散 → 314；本人已在群中 → 305；超过到期时间 → 313；邀请人已退群 → 314；邀请人是群主或管理员、
     *  或群允许普通成员直接拉人 → 入群；否则转为加群申请。
     *  @param facts 由会话层查好的事实
     *  @return 判定结果
     */
    static GroupInviteAcceptDecision decideAccept(const GroupInviteAcceptFacts& facts);

    /**
     *  解析 cmd_accept_group_invite 的请求 { inviteid }。
     *  @param json     请求包体
     *  @param inviteid [出参] 邀请 id
     *  @return 合法（inviteid 为正）返回 true
     */
    static bool parseAcceptRequest(const std::string& json, int64_t& inviteid);

    /**
     *  构造 cmd_accept_group_invite 的应答 { code, msg, inviteid, groupid, joined }。回带邀请 id，客户端据此找到
     *  点的是哪张卡片：失败时把卡片从「正在加入」恢复原状。
     *  @param code     错误码，0 表示成功
     *  @param msg      说明文字
     *  @param inviteid 请求里的邀请 id；请求解析失败时传 0（不写这个字段）
     *  @param groupid  目标群ID；未知时传 0（不写这个字段）
     *  @param joined   成功时是否已入群：true 已入群，false 已转为加群申请
     *  @return 应答的紧凑 JSON 文本
     */
    static std::string buildAcceptResponse(int code, const std::string& msg, int64_t inviteid, int64_t groupid,
                                           bool joined);

    /**
     *  构造 cmd_group_invite_changed_notify 的推送 { inviteid, msgid, inviterid, inviteeid, state }，推给邀请人与
     *  被邀请人的全部在线设备；客户端按两个 userid 里不是本人的那一方定位单聊会话，再按 msgid 找到卡片。
     *  @param rec   邀请（用其中的 id、msgid、邀请人与被邀请人）
     *  @param state 新的显示状态，取值见 group_invite_state
     *  @return 推送的紧凑 JSON 文本
     */
    static std::string buildChangedNotify(const GroupInviteRecord& rec, int state);

private:
    GroupInviteProtocol() = delete;
    ~GroupInviteProtocol() = delete;
    GroupInviteProtocol(const GroupInviteProtocol&) = delete;
    GroupInviteProtocol& operator=(const GroupInviteProtocol&) = delete;
};
