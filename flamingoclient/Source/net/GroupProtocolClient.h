/**
 *  v2 群组报文编解码（客户端侧）：构造 cmd_create_group(1301) 创建群请求 JSON、
 *  解析其应答 JSON。
 *
 *  本模块与服务端 chatserversrc/GroupProtocol 互为镜像——服务端解析请求 / 构造
 *  应答，客户端构造请求 / 解析应答。纯逻辑，只依赖 jsoncpp，不触及网络与 UI，
 *  便于单元测试，见 flamingoclient/Source/Tests/GroupProtocolTests.cpp。
 *
 *  覆盖：创建群(1301) / 群资料(1303) / 群成员(1304) / 我的群列表(1320) 的请求构造与
 *  应答解析，以及写操作 改本人群名片(cmd_set_member_card 1315)、退出群聊
 *  (cmd_leave_group 1309，请求体与 buildGroupIdRequest 同形) 的请求构造。三类写操作
 *  应答均为通用 { code, msg } 形态，统一用 parseCodeMsgResponse 解析。
 *
 *  balloonwj@qq.com   2026-06-02
 */
#pragma once

#include <stdint.h>
#include <string>
#include <vector>

namespace net {

//创建群应答（cmd_create_group=1301 的应答）解析结果。
//成功（code==0）时 m_groupid 为新群ID；失败时仅 code/msg 有效。
struct CreateGroupResponse
{
    int         m_code;     //结果码，0 成功，非 0 见 enum error_code（如建群失败=300）
    std::string m_msg;      //结果描述（成功为 "ok"，失败为诊断信息）
    int64_t     m_groupid;  //新群的群ID（全局自增），仅 code==0 时有效
    uint32_t    m_seq;      //应答帧的 seq，与发出请求时 SendCreateGroup 的返回值相同；由接收线程填写，解析时不动它
                            //（主面板据此区分文件共享代为发起的建群与用户自己点的建群）

    CreateGroupResponse()
        : m_code(-1)        //-1：尚未解析出有效结果码，避免被误判为成功
        , m_groupid(0)
        , m_seq(0)
    {
    }
};

//群资料（cmd_get_group_info=1303 的应答）解析结果。
struct GroupInfoResult
{
    int         m_code;          //结果码，0 成功
    std::string m_msg;           //结果描述
    int64_t     m_groupid;       //群ID
    std::string m_name;          //群名称（UTF-8）
    std::string m_announcement;  //群公告（UTF-8）
    int64_t     m_ownerid;       //群主userid
    int         m_joinmode;      //加群方式：见 enum group_join_mode（0自由 1审批 2禁止）
    int         m_muteall;       //全员禁言：0否 1是
    int         m_membercount;   //群成员总数
    //本人在群内角色：见 group_member_role（0成员 1管理员 2群主）；-1=本条报文没带。
    //cmd_get_group_info(1303) 应答是发给某一个人的，服务端会带上他的角色；
    //cmd_group_info_changed_notify(1318) 推送则是同一份报文发给全群，角色因人而异，
    //服务端不可能在里面带，故解析后恒为 -1。使用方须区分这两种情形 —— 把 -1 当成
    //"普通成员"会把本人角色错误降级（见 GroupManageMapping.cpp 的 ApplyGroupInfo）。
    int         m_myrole;
    int         m_facetype;      //群头像类型
    std::string m_customface;    //自定义群头像标识

    //---- 以下六项为群管理页需要的群设置，与服务端 t_group 的同名列一一对应 ----
    //（原先还有群敏感词开关 m_sensitiveon，随群级敏感词于 2026-10-02 删除；旧服务端仍下发
    // sensitiveon 时按未知字段忽略）

    std::string m_intro;          //群简介
    int         m_atallscope;     //谁能 @ 全体：0 仅群主和管理员，1 所有人
    int         m_newseehistory;  //新成员可否查看入群前的历史消息：0 否 1 是
    int         m_memberinvite;   //普通成员邀请好友是否免审批：0 否 1 是
    int         m_welcomeon;      //入群欢迎语开关：0 关 1 开
    std::string m_welcometext;    //入群欢迎语正文

    //---- 以下三项为群公告功能新增，是"最新一条已发布公告"的冗余快照 ----
    //
    //公告本体要拉 cmd_get_group_announcements 才有，但群聊窗头部那行公告摘要
    //只需要标题，故服务端把它随群资料一起带回来了 —— 开个群聊窗不必为了一行
    //摘要额外发一条请求。上面的 m_announcement 此时存的是这条公告的正文。

    int64_t     m_announceid;     //最新已发布公告的ID；0 表示本群还没有公告
    std::string m_announcetitle;  //最新已发布公告的标题（UTF-8），头部摘要行显示它
    int64_t     m_announcetime;   //最新已发布公告的发布时间，Unix毫秒；0 表示没有公告

    //被设为入群必读的公告ID；0 表示本群没有设置（2026-10-07 起，docs/群公告接入方案.md §14）。
    //老服务端不带该字段时为 0
    int64_t     m_mustreadid;

    //群类型：见 group_type（0 普通群 1 帖子群，2026-10-07 起，docs/帖子群方案.md 6.3）。建群时确定、之后不变；
    //老服务端不带该字段时为 0
    int         m_grouptype;

    GroupInfoResult()
        : m_code(-1)
        , m_groupid(0)
        , m_ownerid(0)
        , m_joinmode(0)
        , m_muteall(0)
        , m_membercount(0)
        , m_myrole(-1)          //-1=未知，见上面对本字段的说明
        , m_facetype(0)
        //默认值与服务端各列的 DEFAULT 一致，使连老服务端（不回这些字段）时
        //界面显示的也是"该群按默认设置"，而不是一堆 0。
        , m_atallscope(0)
        , m_newseehistory(1)
        , m_memberinvite(0)
        , m_welcomeon(0)
        , m_announceid(0)
        , m_announcetime(0)
        , m_mustreadid(0)
        , m_grouptype(0)
    {
    }
};

//一个群成员（cmd_get_group_members=1304 应答里 members 数组的一项）。
struct GroupMemberItem
{
    int64_t     m_userid;      //成员userid
    int         m_role;        //角色：见 group_member_role（0成员 1管理员 2群主）
    std::string m_card;        //群名片（UTF-8，空则用账号昵称）
    int         m_muted;       //是否被禁言：0否 1是
    std::string m_nickname;    //账号昵称（UTF-8）
    int         m_facetype;    //头像类型
    std::string m_customface;  //自定义头像标识

    int64_t     m_jointime;    //入群时间，Unix毫秒；群管理页的成员排序与"新成员"判断用

    //禁言到期时间，Unix毫秒。与 m_muted 一起表达禁言态：
    //  m_muted == 0             —— 未禁言
    //  m_muted == 1 且本字段 0   —— 永久禁言
    //  m_muted == 1 且本字段 >0  —— 禁言至该时刻
    //服务端组装应答时已按当时时刻折算过，已过期的禁言回来时 m_muted 就是 0。
    int64_t     m_muteuntil;
    std::string m_mutereason;  //禁言原因；未禁言时为空串

    //入群必读的确认状态：1 尚未确认（不能发言），0 无须确认或已确认（2026-10-07 起，docs/群公告接入方案.md §14）。
    //老服务端不带该字段时为 0
    int         m_mustread;

    GroupMemberItem()
        : m_userid(0)
        , m_role(0)
        , m_muted(0)
        , m_facetype(0)
        , m_jointime(0)
        , m_muteuntil(0)
        , m_mustread(0)
    {
    }
};

//我加入的一个群（cmd_get_user_groups=1320 应答里 groups 数组的一项）。字段与
//cmd_get_group_info 同口径，"群"Tab 用到 groupid / name / membercount 与群头像两项。
struct UserGroupItem
{
    int64_t     m_groupid;       //群ID
    std::string m_name;          //群名称（UTF-8）
    int64_t     m_ownerid;       //群主userid
    int         m_membercount;   //群成员总数
    int         m_facetype;      //群头像类型（见 BalloonMain/AvatarSpec.h AvatarKind：0 自动 1 字形 2 自定义图）
    std::string m_customface;    //群头像标识（UTF-8）：字形头像为 "glyph|colorIdx"，自定义图为 fileid；空表示自动
    int         m_pinned;        //本人是否置顶了该群：1 是 0 否。老服务端不带该字段时为 0。
                                 //登录时随群列表下发，之后由本端切换置顶与多端同步推送更新
    std::string m_intro;         //群简介（UTF-8，原文，可含换行）；老服务端不带该字段或群主没填时为空串。
                                 //主面板据此写群信息缓存，新开的群聊窗头部立即显示，不必等 1303 应答
    int         m_muteall;       //仅通知模式（原称全员禁言）：1 开 0 关。服务端一直随群列表下发（与群资料同一个 fillGroupValue），
                                 //2026-10-06 起客户端才解析，主面板据此画「仅通知」徽章（docs/群管理接入方案.md §14）；老服务端不带时为 0
    int         m_grouptype;     //群类型：见 group_type（0 普通群 1 帖子群，2026-10-07 起，docs/帖子群方案.md 6.3）；
                                 //主面板据此决定帖子群显示与否、打开时交给帖子群模块。老服务端不带时为 0

    UserGroupItem()
        : m_groupid(0)
        , m_ownerid(0)
        , m_membercount(0)
        , m_facetype(0)
        , m_pinned(0)
        , m_muteall(0)
        , m_grouptype(0)
    {
    }
};

//我的群列表（cmd_get_user_groups=1320 的应答）解析结果。
struct UserGroupsResult
{
    int                        m_code;    //结果码，0 成功
    std::string                m_msg;     //结果描述
    std::vector<UserGroupItem> m_groups;  //本人加入的全部群（含无消息空群）

    UserGroupsResult()
        : m_code(-1)
    {
    }
};

//群成员列表（cmd_get_group_members=1304 的应答）解析结果。
struct GroupMembersResult
{
    int                          m_code;     //结果码，0 成功
    std::string                  m_msg;      //结果描述
    int64_t                      m_groupid;  //群ID
    std::vector<GroupMemberItem> m_members;  //群成员列表

    GroupMembersResult()
        : m_code(-1)
        , m_groupid(0)
    {
    }
};

//群管理写操作的通用应答载体。
//
//邀请入群、移出成员、设管理员、禁言、全员禁言、改群资料、转让群主、解散群、
//审批加群、敏感词增删这十来个操作的应答体都是 { code, msg }，形态完全一致，
//故共用一个结构、共用一条回投消息号（FMG_MSG_V2_GROUP_OP_RESULT），由 wParam
//带上是哪条协议的应答。
struct GroupOpResult
{
    int         m_code;  //结果码，0 成功；非 0 见服务端 enum error_code
    std::string m_msg;   //结果描述（成功为 "ok"，失败为诊断信息）
    //以下三项只有邀请入群（cmd_invite_to_group）的应答带（docs/邀请入群方案.md §4.2），其余操作恒为 0
    int         m_added;   //直接入群的人数
    int         m_invited; //发出入群邀请卡片、等对方同意的人数
    int         m_pending; //转为加群申请、等群主或管理员审批的人数

    GroupOpResult()
        : m_code(-1)     //-1：尚未解析出有效结果码，避免被误判为成功
        , m_added(0), m_invited(0), m_pending(0)
    {
    }
};

//修改群资料请求（cmd_update_group_info=1311）的参数集合。
//
//每一项都配一个 m_has 开关：只有开关为 true 的项才会被写进请求 JSON、也才会
//被服务端更新。这样"用户只改了群公告"时发出去的请求就只带公告，不会把界面上
//没碰过的其它项按当前显示值一并回写 —— 后者在多端同时编辑时会互相覆盖。
struct UpdateGroupInfoParams
{
    int64_t     m_groupid;           //目标群ID，必填

    bool        m_hasName;           //本次是否修改群名称
    std::string m_name;              //群名称目标值（UTF-8）
    bool        m_hasFacetype;       //本次是否修改群头像类型
    int         m_facetype;          //群头像类型（见 BalloonMain/AvatarSpec.h AvatarKind：0 自动 1 字形 2 自定义图）
    bool        m_hasCustomface;     //本次是否修改群头像标识
    std::string m_customface;        //群头像标识（UTF-8）：字形头像为 "glyph|colorIdx"，自定义图为 fileid，恢复默认时为空串
    bool        m_hasAnnouncement;   //本次是否修改群公告
    std::string m_announcement;      //群公告目标值（UTF-8）
    bool        m_hasIntro;          //本次是否修改群简介
    std::string m_intro;             //群简介目标值（UTF-8）
    bool        m_hasJoinMode;       //本次是否修改加群方式
    int         m_joinMode;          //加群方式：0 自由 1 需审批 2 禁止
    bool        m_hasAtAllScope;     //本次是否修改"谁能@全体"
    int         m_atAllScope;        //谁能@全体：0 仅群主和管理员 1 所有人
    bool        m_hasNewSeeHistory;  //本次是否修改"新成员看历史消息"
    int         m_newSeeHistory;     //新成员看历史消息：0 否 1 是
    bool        m_hasMemberInvite;   //本次是否修改"成员邀请好友免审批"
    int         m_memberInvite;      //成员邀请好友免审批：0 否 1 是
    bool        m_hasWelcomeOn;      //本次是否修改入群欢迎语开关
    int         m_welcomeOn;         //入群欢迎语开关：0 关 1 开
    bool        m_hasWelcomeText;    //本次是否修改入群欢迎语正文
    std::string m_welcomeText;       //入群欢迎语正文（UTF-8）

    UpdateGroupInfoParams()
        : m_groupid(0)
        , m_hasName(false)
        , m_hasFacetype(false), m_facetype(0)
        , m_hasCustomface(false)
        , m_hasAnnouncement(false)
        , m_hasIntro(false)
        , m_hasJoinMode(false), m_joinMode(1)
        , m_hasAtAllScope(false), m_atAllScope(0)
        , m_hasNewSeeHistory(false), m_newSeeHistory(1)
        , m_hasMemberInvite(false), m_memberInvite(0)
        , m_hasWelcomeOn(false), m_welcomeOn(0)
        , m_hasWelcomeText(false)
    {
    }
};

//一条待审批的加群申请（cmd_get_group_join_requests=1319 应答里 requests 数组的一项，
//以及 cmd_join_group_request_notify=1307 推送体）。
struct GroupJoinRequestItem
{
    int64_t     m_requestid;    //申请ID，审批时按它定位
    int64_t     m_groupid;      //目标群ID；1319 应答里的项不带此字段，取应答外层的 groupid
    int64_t     m_userid;       //申请人userid
    std::string m_nickname;     //申请人昵称（UTF-8）
    int         m_facetype;     //申请人头像类型
    std::string m_customface;   //申请人自定义头像标识
    std::string m_verifymsg;    //验证留言（UTF-8）
    int64_t     m_timestamp;    //申请时间，Unix毫秒
    int         m_source;       //申请来源：0 好友邀请 1 群搜索 2 扫码加群
    int64_t     m_recommender;  //推荐人userid，0 表示无；仅好友邀请时可能非 0

    GroupJoinRequestItem()
        : m_requestid(0)
        , m_groupid(0)
        , m_userid(0)
        , m_facetype(0)
        , m_timestamp(0)
        , m_source(1)      //缺省按"群搜索"，与服务端 f_source 的默认值一致
        , m_recommender(0)
    {
    }
};

//待审批加群申请列表（cmd_get_group_join_requests=1319 的应答）解析结果。
struct GroupJoinRequestsResult
{
    int                               m_code;     //结果码，0 成功
    std::string                       m_msg;      //结果描述
    int64_t                           m_groupid;  //群ID
    std::vector<GroupJoinRequestItem> m_requests; //待处理申请，按申请时间倒序

    GroupJoinRequestsResult()
        : m_code(-1)
        , m_groupid(0)
    {
    }
};

//一条群公告（cmd_get_group_announcements=1323 应答里 announcements 数组的一项，
//也是 cmd_group_announcement_notify=1326 推送体里 announcement 的内容）。
struct GroupAnnouncementItem
{
    int64_t     m_id;            //公告ID，撤回 / 重新发布时按它定位
    int64_t     m_groupid;       //所属群ID
    std::string m_title;         //公告标题（UTF-8）
    std::string m_content;       //公告正文（UTF-8）
    int         m_severity;      //优先级：见 group_announcement_severity
    int64_t     m_publisher;     //发布人userid

    //发布人昵称（UTF-8）。由服务端下发而不是本地从成员列表里查 —— 发布人可能
    //已经退群，本地成员列表里未必找得到他。为空时界面退回显示 userid。
    std::string m_publishername;

    int64_t     m_publishtime;   //发布时间，Unix毫秒

    //severity 的默认值直接写字面量而不引用 Protocol.h 的枚举：本头文件是纯编解码层，
    //只依赖 std，被单测工程直接包含。0 即"通知"，与 Protocol.h 的
    //group_announcement_severity 对齐。
    GroupAnnouncementItem()
        : m_id(0)
        , m_groupid(0)
        , m_severity(0)
        , m_publisher(0)
        , m_publishtime(0)
    {
    }
};

//群公告列表（cmd_get_group_announcements=1323 的应答）解析结果。
struct GroupAnnouncementsResult
{
    int                                m_code;     //结果码，0 成功
    std::string                        m_msg;      //结果描述
    int64_t                            m_groupid;  //群ID

    //本群公告总条数，不受本次分页限制。界面上"第 x/y 页"的页数、以及群管理窗
    //左侧导航那个计数徽章都用它，不能拿 m_announcements.size() 顶替 —— 那只是
    //本页的条数。
    int                                m_total;

    //服务端回显的本次请求分页参数。同一个客户端可能有两处同时在拉公告（群管理窗
    //按页拉、群聊窗边栏一次拉一条），而应答是广播给两处的；靠这两项各自认领属于
    //自己的那一份，否则后到的应答会把另一处的列表覆盖掉。
    //老服务端不回显这两项，此时保持 -1，调用方据此退回"来者不拒"的旧行为。
    int                                m_offset;
    int                                m_count;

    //本页公告，按发布时间倒序（最新的在前）
    std::vector<GroupAnnouncementItem> m_announcements;

    GroupAnnouncementsResult()
        : m_code(-1)
        , m_groupid(0)
        , m_total(0)
        , m_offset(-1)   //-1=服务端没回显（老服务端）
        , m_count(-1)
    {
    }

    /**
     *  这份应答是不是对某次请求的回应。
     *
     *  两处同时在拉公告时，主面板会把每一份应答都投给两个窗口；各窗口用本方法
     *  比对"这是不是我要的那一份"，不是就直接丢掉，免得被对方的分页参数带偏。
     *    expectedOffset：本窗口发出去的 offset；
     *    expectedCount： 本窗口发出去的 count。
     *  返回：匹配返回 true。服务端没回显分页参数（老服务端）时一律返回 true ——
     *        退回"来者不拒"的旧行为，至少不比接入前更差。
     */
    bool MatchesRequest(int expectedOffset, int expectedCount) const
    {
        if (m_offset < 0 || m_count < 0)
        {
            return true;
        }
        return m_offset == expectedOffset && m_count == expectedCount;
    }
};

//群公告变更推送（cmd_group_announcement_notify=1326）解析结果。
struct GroupAnnouncementNotify
{
    int64_t               m_groupid;     //群ID
    int                   m_changetype;  //取值见 group_announcement_change_type
    GroupAnnouncementItem m_announcement; //变更涉及的那条公告（内容完整）
    int64_t               m_cardMsgId;   //删除推送带的公告卡片 msgid（2026-10-05 起，docs/群公告接入方案.md §13.3）；
                                         //发布推送、本功能上线前发布的公告、或老服务端都为 0，此时不改写任何卡片
    std::string           m_cardContent; //删除推送带的改写后的卡片 content（紧凑 JSON，已删除形态）；没有时为空串

    GroupAnnouncementNotify()
        : m_groupid(0)
        , m_changetype(0)
        , m_cardMsgId(0)
    {
    }
};

//群成员变动推送（cmd_group_member_changed_notify=1317）解析结果。
struct GroupMemberChangedNotify
{
    int64_t              m_groupid;     //发生变动的群ID
    int                  m_changetype;  //1加入 2主动退出 3被移出 4角色变更 5禁言状态变更 6群名片变更
    std::vector<int64_t> m_userids;     //本次发生变动的成员userid列表
    int64_t              m_operatorid;  //触发本次变动的操作者userid

    GroupMemberChangedNotify()
        : m_groupid(0)
        , m_changetype(0)
        , m_operatorid(0)
    {
    }
};

//入群邀请卡片的状态变化推送（cmd_group_invite_changed_notify=1328）解析结果（docs/邀请入群方案.md §4.5）。
//推给邀请人与被邀请人的全部在线设备；客户端按两个 userid 里不是本人的那一方定位单聊会话，再按 msgid 找到卡片。
struct GroupInviteChangedNotify
{
    int64_t m_inviteid;    //邀请 id
    int64_t m_msgid;       //卡片消息的 msgid
    int64_t m_inviterid;   //邀请人 userid
    int64_t m_inviteeid;   //被邀请人 userid
    int     m_state;       //新的显示状态，取值见 group_invite_state

    GroupInviteChangedNotify()
        : m_inviteid(0), m_msgid(0), m_inviterid(0), m_inviteeid(0), m_state(0)
    {
    }
};

//「加群申请已处理」推送（cmd_join_request_handled_notify=1329）解析结果（docs/邀请入群方案.md 第 4 期）。
//推给该群全体群主与管理员（含处理人本人的其他设备），群管理窗据此重拉待审批列表
struct JoinRequestHandledNotify
{
    int64_t     m_groupid;      //群ID
    int64_t     m_requestid;    //申请ID
    int         m_status;       //处理结果：1 已通过 2 已拒绝
    int64_t     m_handler;      //处理人 userid
    std::string m_handlerName;  //处理人昵称（UTF-8），可为空

    JoinRequestHandledNotify()
        : m_groupid(0), m_requestid(0), m_status(0), m_handler(0)
    {
    }
};

//点入群邀请卡片上的「加入群聊」（cmd_accept_group_invite=1327）的应答解析结果（docs/邀请入群方案.md §4.4）
struct AcceptGroupInviteResult
{
    int         m_code;      //结果码，0 成功；305 已在群中、313 邀请已过期、314 邀请已失效等
    std::string m_msg;       //结果描述
    int64_t     m_inviteid;  //服务端回带的邀请 id，客户端据此找到点的是哪张卡片；请求非法时为 0
    int64_t     m_groupid;   //目标群ID；未知时为 0
    bool        m_joined;    //成功时是否已入群：true 已入群，false 已转为加群申请、等待审批

    AcceptGroupInviteResult()
        : m_code(-1), m_inviteid(0), m_groupid(0), m_joined(false)
    {
    }
};

//查询入群必读（cmd_get_must_read=1335）的应答解析结果（docs/群公告接入方案.md §14）
struct MustReadResult
{
    int                   m_code;            //结果码，0 成功；302 不是群成员
    std::string           m_msg;             //结果描述
    int64_t               m_groupid;         //群ID
    int64_t               m_mustreadid;      //被设为入群必读的公告ID；0 表示没有设置
    bool                  m_pending;         //本人是否尚未确认：true 时不能发言，界面要求先阅读并点「我已阅读」
    bool                  m_hasAnnouncement; //应答是否带了必读公告的内容；没有设置或公告已不存在时为 false
    GroupAnnouncementItem m_announcement;    //必读公告（m_hasAnnouncement 为 true 时有效）

    MustReadResult()
        : m_code(-1), m_groupid(0), m_mustreadid(0), m_pending(false), m_hasAnnouncement(false)
    {
    }
};

//群资料变更推送（cmd_group_info_changed_notify=1318）解析结果。
//推送体即一份完整的群资料 + changetype，故直接复用 GroupInfoResult 承载资料部分。
struct GroupInfoChangedNotify
{
    GroupInfoResult m_info;        //变更后的完整群资料（其 m_code / m_msg 不使用）
    int             m_changetype;  //1 资料被修改 2 群已被解散

    GroupInfoChangedNotify()
        : m_changetype(0)
    {
    }
};

//群组报文编解码工具类；全部为静态方法，不可实例化。
class GroupProtocolClient
{
public:
    /**
     *  构造创建群聊的发送请求 JSON 包体（cmd_create_group=1301，紧凑 UTF-8）。
     *  与服务端 GroupProtocol::parseCreateGroupRequest 对齐：群名承载在 groupname
     *  字段，该字段总是带上；为空串时由服务端按成员昵称生成自动群名，此后群名随
     *  成员变化重算。初始成员 userid 列表承载在 memberids 数组，每个 id 以字符串
     *  编码（避免 64 位精度丢失），不含创建者自己。
     *  @param groupname   群名称（UTF-8）；空串表示使用自动群名
     *  @param memberids   初始成员 userid 列表（不含创建者；可为空）
     *  @param grouptype   群类型：取 group_type_thread（1）时请求带 "grouptype": 1 建帖子群；其余取值一律按普通群，
     *                     请求里不带该字段（与 2026-10-07 之前的请求完全相同）
     *  @return 紧凑格式的请求 JSON 文本
     */
    static std::string buildCreateGroupRequest(const std::string& groupname,
                                               const std::vector<int64_t>& memberids,
                                               int grouptype = 0);

    /**
     *  把用户在建群对话框、群管理基本信息页输入的群名规整成要发给服务端的值：
     *  去掉首尾的空白（半角空格、制表符、回车换行，以及全角空格 U+3000），中间的
     *  空白保留。未填写或只填了空白时返回空串，发给服务端即表示使用自动群名。
     *  @param utf8Input 输入框里的原始文字（UTF-8）
     *  @return 规整后的群名（UTF-8），可能为空串。纯函数，无副作用
     */
    static std::string normalizeGroupNameInput(const std::string& utf8Input);

    /**
     *  解析创建群聊应答 JSON 包体（cmd_create_group=1301 的应答）。
     *  只要 JSON 合法且含整型 code 字段即返回 true，调用方据 resp.m_code 判定结果；
     *  JSON 非法或缺 code 返回 false。
     *  @param jsonbody 应答包体（UTF-8 JSON 文本）
     *  @param resp     [出参] 解析结果；返回 true 时 m_code/m_msg 有效，
     *                  m_code==0 时 m_groupid 亦有效
     *  @return JSON 合法且含 code 返回 true，否则 false
     */
    static bool parseCreateGroupResponse(const std::string& jsonbody, CreateGroupResponse& resp);

    /**
     *  构造"仅含群ID"的请求 JSON 包体（cmd_get_group_info=1303 /
     *  cmd_get_group_members=1304 共用）：{ "groupid":"..." }。群ID 以字符串编码。
     *  @param groupid 目标群ID（>0）
     *  @return 紧凑格式的请求 JSON 文本
     */
    static std::string buildGroupIdRequest(int64_t groupid);

    /**
     *  构造"修改本人群名片"的请求 JSON 包体（cmd_set_member_card=1315）：
     *  { "groupid":"...", "card":"..." }。groupid 以字符串编码（避免大整数精度丢失），
     *  card 为本人在该群的新名片（UTF-8）；允许空串，表示清空名片（届时服务端 / 各端
     *  回退显示账号昵称）。
     *  @param groupid 目标群ID（>0）
     *  @param card    新群名片（UTF-8，可为空）
     *  @return 紧凑格式的请求 JSON 文本
     */
    static std::string buildSetMemberCardRequest(int64_t groupid, const std::string& card);

    /**
     *  解析通用 { code, msg } 应答 JSON 包体。供本轮三个写命令——会话设置
     *  (cmd_set_conversation=1513)、改群名片(cmd_set_member_card=1315)、退群
     *  (cmd_leave_group=1309)——共用，它们应答体只有结果码与描述，无额外字段。
     *  JSON 合法且含整型 code 即返回 true；否则返回 false（code/msg 出参不被写入）。
     *  @param jsonbody 应答包体（UTF-8 JSON 文本）
     *  @param code     [出参] 结果码，0 成功，非 0 见 enum error_code
     *  @param msg      [出参] 结果描述（UTF-8；缺失时为空串）
     *  @return JSON 合法且含 code 返回 true，否则 false
     */
    static bool parseCodeMsgResponse(const std::string& jsonbody, int& code, std::string& msg);

    /**
     *  解析群资料应答 JSON 包体（cmd_get_group_info=1303 的应答）。
     *  JSON 合法且含整型 code 即返回 true；code==0 时各群资料字段有效。
     *  @param jsonbody 应答包体（UTF-8 JSON 文本）
     *  @param out      [出参] 解析结果
     *  @return JSON 合法且含 code 返回 true，否则 false
     */
    static bool parseGroupInfoResponse(const std::string& jsonbody, GroupInfoResult& out);

    /**
     *  解析群成员应答 JSON 包体（cmd_get_group_members=1304 的应答）。
     *  JSON 合法且含整型 code 即返回 true；code==0 时 m_groupid / m_members 有效。
     *  @param jsonbody 应答包体（UTF-8 JSON 文本）
     *  @param out      [出参] 解析结果
     *  @return JSON 合法且含 code 返回 true，否则 false
     */
    static bool parseGroupMembersResponse(const std::string& jsonbody, GroupMembersResult& out);

    /**
     *  解析"我的群列表"应答 JSON 包体（cmd_get_user_groups=1320 的应答）。
     *  JSON 合法且含整型 code 即返回 true；code==0 时 m_groups 有效。groupid<=0
     *  的脏数据项跳过。
     *  @param jsonbody 应答包体（UTF-8 JSON 文本）
     *  @param out      [出参] 解析结果
     *  @return JSON 合法且含 code 返回 true，否则 false
     */
    static bool parseUserGroupsResponse(const std::string& jsonbody, UserGroupsResult& out);

    // ================================================================
    // 群管理：请求构造
    // ================================================================

    /**
     *  构造"群ID + 成员ID列表"形态的请求（邀请入群 cmd_invite_to_group=1305 与
     *  移出成员 cmd_kick_group_member=1310 两者同构，共用本构造器）。
     *  @param groupid 目标群ID
     *  @param userids 被邀请 / 被移出的 userid 列表，不可为空（服务端会拒绝空列表）
     *  @param historyLimit 仅邀请用：新成员入群后可看的入群前消息条数（docs/邀请入群方案.md §4.2）。
     *                      -1 表示全部，此时请求里不带这个字段；0 表示不同步；1 ~ 10000 表示最近 N 条
     *  @return 紧凑 JSON：{ "groupid":"...", "userids":["...","..."][, "historylimit": N] }
     */
    static std::string buildGroupMembersOpRequest(int64_t groupid,
                                                  const std::vector<int64_t>& userids,
                                                  int historyLimit = -1);

    /**
     *  从邀请入群（cmd_invite_to_group）的应答里读出三项统计：added / invited / pending。
     *  老服务端的应答没有这三项，读不到的按 0 处理。
     *  @param jsonbody 应答包体
     *  @param out      [出参] 填 m_added / m_invited / m_pending；不修改 m_code / m_msg
     */
    static void parseInviteToGroupCounts(const std::string& jsonbody, GroupOpResult& out);

    /**
     *  构造修改群资料请求（cmd_update_group_info=1311）。
     *
     *  与服务端 parseUpdateGroupInfoRequest 对齐：除 groupid 外全部字段可选，
     *  只有 has_* 为 true 的项才会被写进 JSON、也才会被服务端更新。调用方按
     *  "用户在界面上改动了哪几项"来置 has_*，避免把没碰过的项一起覆盖。
     *  @param req 待发送的修改项集合
     *  @return 紧凑 JSON
     */
    static std::string buildUpdateGroupInfoRequest(const UpdateGroupInfoParams& req);

    /**
     *  构造设置 / 取消管理员请求（cmd_set_member_role=1312）。
     *  @param groupid 群ID
     *  @param userid  目标成员userid
     *  @param role    目标角色：0 降为普通成员，1 设为管理员。
     *                 <u>不能</u>用它设群主 —— 转让群主走 cmd_transfer_owner。
     *  @return 紧凑 JSON：{ "groupid":"...", "userid":"...", "role":n }
     */
    static std::string buildSetMemberRoleRequest(int64_t groupid, int64_t userid, int role);

    /**
     *  构造禁言 / 解禁请求（cmd_mute_member=1313）。
     *  @param groupid  群ID
     *  @param userid   目标成员userid
     *  @param muted    1 禁言，0 解禁
     *  @param duration 禁言时长，单位秒；0 表示永久禁言。muted 为 0 时本参数被忽略
     *  @param reason   禁言原因（UTF-8），可为空串。muted 为 0 时本参数被忽略
     *  @return 紧凑 JSON
     */
    static std::string buildMuteMemberRequest(int64_t groupid, int64_t userid, int muted,
                                              int duration, const std::string& reason);

    /**
     *  构造全员禁言开关请求（cmd_mute_all=1314）。
     *  @param groupid 群ID
     *  @param muteall 1 开启全员禁言，0 关闭
     *  @return 紧凑 JSON：{ "groupid":"...", "muteall":n }
     */
    static std::string buildMuteAllRequest(int64_t groupid, int muteall);

    /**
     *  构造转让群主请求（cmd_transfer_owner=1316）。
     *  @param groupid 群ID
     *  @param userid  新群主userid，须为本群现有成员且不是自己
     *  @return 紧凑 JSON：{ "groupid":"...", "userid":"..." }
     */
    static std::string buildTransferOwnerRequest(int64_t groupid, int64_t userid);

    /**
     *  构造审批加群申请请求（cmd_handle_join_group=1308）。
     *  @param requestid 待处理的申请ID
     *  @param accept    true 同意，false 拒绝
     *  @return 紧凑 JSON：{ "requestid":"...", "action":n }
     */
    static std::string buildHandleJoinGroupRequest(int64_t requestid, bool accept);

    // ================================================================
    // 群管理：应答与推送解析
    // ================================================================

    /**
     *  解析待审批加群申请列表应答（cmd_get_group_join_requests=1319）。
     *  requestid<=0 的脏数据项跳过。
     *  @param jsonbody 应答包体（UTF-8 JSON 文本）
     *  @param out      [出参] 解析结果
     *  @return JSON 合法且含整型 code 返回 true，否则 false
     */
    static bool parseJoinRequestsResponse(const std::string& jsonbody,
                                          GroupJoinRequestsResult& out);

    /**
     *  解析加群申请推送（cmd_join_group_request_notify=1307）。
     *  推送体的字段名与 1319 应答里的项不同（前缀 from），故单独解析。
     *  @param jsonbody 推送包体（UTF-8 JSON 文本）
     *  @param out      [出参] 解析结果
     *  @return JSON 合法且 requestid / groupid 均为正返回 true，否则 false
     */
    static bool parseJoinGroupRequestNotify(const std::string& jsonbody,
                                            GroupJoinRequestItem& out);

    /**
     *  解析群成员变动推送（cmd_group_member_changed_notify=1317）。
     *  @param jsonbody 推送包体（UTF-8 JSON 文本）
     *  @param out      [出参] 解析结果
     *  @return JSON 合法且 groupid 为正、changetype 存在时返回 true，否则 false
     */
    static bool parseGroupMemberChangedNotify(const std::string& jsonbody,
                                              GroupMemberChangedNotify& out);

    /**
     *  解析入群邀请卡片的状态变化推送（cmd_group_invite_changed_notify=1328）。
     *  @param jsonbody 推送包体（UTF-8 JSON 文本）
     *  @param out      [出参] 解析结果
     *  @return JSON 合法且 inviteid、msgid 为正、state 为整数时返回 true
     */
    static bool parseGroupInviteChangedNotify(const std::string& jsonbody, GroupInviteChangedNotify& out);

    /**
     *  解析「加群申请已处理」推送（cmd_join_request_handled_notify=1329）。
     *  @param jsonbody 推送包体（UTF-8 JSON 文本）
     *  @param out      [出参] 解析结果；读不到的可选字段（handlername）保持默认值
     *  @return JSON 合法、groupid 与 requestid 为正、status 为整数时返回 true
     */
    static bool parseJoinRequestHandledNotify(const std::string& jsonbody, JoinRequestHandledNotify& out);

    /**
     *  构造点「加入群聊」（cmd_accept_group_invite=1327）的请求 { inviteid }，id 写成字符串。
     *  @param inviteid 卡片上的邀请 id
     *  @return 紧凑 JSON 文本
     */
    static std::string buildAcceptGroupInviteRequest(int64_t inviteid);

    /**
     *  解析点「加入群聊」（cmd_accept_group_invite=1327）的应答 { code, msg, inviteid, groupid, joined }。
     *  @param jsonbody 应答包体
     *  @param out      [出参] 解析结果；读不到的可选字段保持默认值
     *  @return JSON 合法且带整数 code 时返回 true
     */
    static bool parseAcceptGroupInviteResponse(const std::string& jsonbody, AcceptGroupInviteResult& out);

    /**
     *  解析群资料变更推送（cmd_group_info_changed_notify=1318）。
     *  @param jsonbody 推送包体（UTF-8 JSON 文本）
     *  @param out      [出参] 解析结果；m_info 里除 m_code / m_msg 外均有效
     *  @return JSON 合法且 groupid 为正、changetype 存在时返回 true，否则 false
     */
    static bool parseGroupInfoChangedNotify(const std::string& jsonbody,
                                            GroupInfoChangedNotify& out);

    // ================================================================
    // 群公告
    // ================================================================

    /**
     *  构造拉取群公告列表请求（cmd_get_group_announcements=1323）。
     *  @param groupId 目标群ID
     *  @param offset  分页起始下标，从 0 起
     *  @param count   本页最多几条；服务端按 100 封顶
     *  @return 请求包体（UTF-8 JSON 文本）
     */
    static std::string buildGetAnnouncementsRequest(int64_t groupId, int offset, int count);

    /**
     *  构造发布新公告请求（cmd_publish_group_announcement=1324）。
     *  @param groupId  目标群ID
     *  @param titleUtf8   公告标题（UTF-8），必填、非空
     *  @param contentUtf8 公告正文（UTF-8），可空
     *  @param severity    优先级，取值见 group_announcement_severity
     *  @return 请求包体（UTF-8 JSON 文本）
     */
    static std::string buildPublishAnnouncementRequest(int64_t groupId,
                                                       const std::string& titleUtf8,
                                                       const std::string& contentUtf8,
                                                       int severity);

    /**
     *  构造"重新发布"请求（cmd_publish_group_announcement=1324 的另一种用法）。
     *  只带原公告ID，标题正文由服务端读回原件补齐，结果是历史里多一条新记录 ——
     *  故不是"把旧那条的时间改一下"。
     *  @param groupId      目标群ID
     *  @param announceId   要重新发布的原公告ID
     *  @return 请求包体（UTF-8 JSON 文本）
     */
    static std::string buildRepublishAnnouncementRequest(int64_t groupId, int64_t announceId);

    /**
     *  构造删除公告请求（cmd_delete_group_announcement=1325）。删除不可恢复。
     *  @param groupId    目标群ID
     *  @param announceId 待删除的公告ID
     *  @return 请求包体（UTF-8 JSON 文本）
     */
    static std::string buildDeleteAnnouncementRequest(int64_t groupId, int64_t announceId);

    /**
     *  解析群公告列表应答（cmd_get_group_announcements=1323）。
     *  id<=0 的项跳过 —— 没有ID就撤不掉也重发不了，留在列表里只会让按钮点了没反应。
     *  @param jsonbody 应答包体（UTF-8 JSON 文本）
     *  @param out      [出参] 解析结果
     *  @return JSON 合法且含整型 code 返回 true，否则 false
     */
    static bool parseAnnouncementsResponse(const std::string& jsonbody,
                                           GroupAnnouncementsResult& out);

    /**
     *  解析发布公告应答（cmd_publish_group_announcement=1324）：{ code, msg, id }。
     *  @param jsonbody  应答包体（UTF-8 JSON 文本）
     *  @param code      [出参] 结果码
     *  @param msgUtf8   [出参] 结果描述（UTF-8）
     *  @param idOut     [出参] 新公告ID；失败时为 0
     *  @return JSON 合法且含整型 code 返回 true，否则 false
     */
    static bool parsePublishAnnouncementResponse(const std::string& jsonbody, int& code,
                                                 std::string& msgUtf8, int64_t& idOut);

    /**
     *  解析群公告变更推送（cmd_group_announcement_notify=1326）。
     *  推送体里带完整公告，收到即可直接刷新界面 / 弹窗，不必再拉一次列表。
     *  @param jsonbody 推送包体（UTF-8 JSON 文本）
     *  @param out      [出参] 解析结果
     *  @return JSON 合法、changetype 存在且公告ID为正返回 true，否则 false
     */
    static bool parseAnnouncementNotify(const std::string& jsonbody,
                                        GroupAnnouncementNotify& out);

    /**
     *  构造设置 / 取消入群必读的请求（cmd_set_must_read=1332）。
     *  @param groupId    群ID
     *  @param announceId 设为必读的公告ID；0 表示取消
     *  @return 紧凑 JSON：{ "groupid":"...", "announcementid":"..." }
     */
    static std::string buildSetMustReadRequest(int64_t groupId, int64_t announceId);

    /**
     *  解析查询入群必读的应答（cmd_get_must_read=1335）。
     *  @param jsonbody 应答包体
     *  @param out      [出参] 解析结果；announcement 子对象存在且公告ID为正时 m_hasAnnouncement 为 true
     *  @return JSON 合法且带 code 返回 true；成功应答还要求 groupid 为正
     */
    static bool parseGetMustReadResponse(const std::string& jsonbody, MustReadResult& out);

private:
    GroupProtocolClient() = delete;
    ~GroupProtocolClient() = delete;
    GroupProtocolClient(const GroupProtocolClient&) = delete;
    GroupProtocolClient& operator=(const GroupProtocolClient&) = delete;
};

} // namespace net
