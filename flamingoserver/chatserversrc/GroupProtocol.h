/**
 *  v2 群组相关报文（cmd_create_group / cmd_dismiss_group / cmd_get_group_info /
 *  cmd_get_group_members / cmd_update_group_info / cmd_group_info_changed_notify /
 *  cmd_group_member_changed_notify）的编解码。
 *
 *  本模块是纯逻辑——只依赖 jsoncpp，不触及网络与数据库，便于单元测试
 *  （见 tests/test_groupprotocol.cpp）。建群落库、权限校验、推送等均在会话层。
 *
 *  balloonwj@qq.com   2026-05-21
 */
#pragma once

#include <stdint.h>
#include <set>        //buildUserGroupsResponse 的置顶群ID集合
#include <string>
#include <vector>
#include "Protocol.h"   //群相关枚举（group_member_role / group_join_mode / 两个 changetype）

//群公告标题与正文的长度上限，单位为<u>字符</u>（不是字节）——与
//t_group_announcement 的列宽 varchar(128) / varchar(2048) 逐字对齐（MySQL 的
//varchar(N) 在 utf8 字符集下按字符计）。按字符校验而不是按字节，是为了让纯中文
//的标题不至于刚写十几个字就被判超长（一个汉字在 UTF-8 里占 3 字节）。
static const int kAnnouncementTitleMaxChars   = 128;
static const int kAnnouncementContentMaxChars = 2048;

//群公告卡片（message_type_announcement）的 clientmsgid 前缀，完整写法为「前缀 + 群id + "-" + 公告id」
//（docs/群公告接入方案.md §13.2）。公告 id 全局唯一，重新发布得到的是新 id，所以每条公告恰好对应一张卡片；
//删除公告时按「发布人 + 这个 clientmsgid」找回卡片的 msgid（MessageManager::getMsgIdByClientId）再改写
static const char* const kAnnouncementCardClientMsgIdPrefix = "announce-card-";

//入群系统提示（kSystemTipMemberJoin）里最多列出几位新成员的昵称。超出的只计人数，正文写「等 N 人」；
//tipargs 的 members 仍带全部新成员的 userid，但只有前这么多位带昵称（docs/邀请入群方案.md §3.1、§4.1）。
//一次拉几十上百人进群时，把所有名字都写进一行提示既看不完，也让这条消息无谓地变长。
static const size_t kMemberJoinTipMaxNames = 10;

//邀请入群时「入群后可查看的聊天记录」的条数（cmd_invite_to_group 的 historylimit，docs/邀请入群方案.md §4.2）：
//-1 表示同步全部（缺省值，老客户端不带这个字段），0 表示不同步，1 ~ kInviteHistoryLimitMax 表示最近 N 条。
//上限与客户端邀请对话框的输入上限一致，超出的请求判为非法
static const int kInviteHistoryLimitAll = -1;
static const int kInviteHistoryLimitMax = 10000;

//本人在某个群里能看到的历史消息范围：两个下界同时生效，早于任何一个的消息都不返回（docs/邀请入群方案.md §5.3）
struct GroupHistoryBound
{
    int64_t m_minCreateTime;  //时间下界（Unix 毫秒）：群设为「新成员不可查看入群前的历史消息」时取本人的入群时间；0 表示不限
    int64_t m_minMsgId;       //msgid 下界：取本人成员行的 f_history_from（邀请时选择同步最近 N 条得出）；0 表示不限

    GroupHistoryBound()
        : m_minCreateTime(0), m_minMsgId(0)
    {
    }
};

//入群系统提示里的一位新成员（GroupProtocol::buildMemberJoinTip 的入参）
struct JoinTipMember
{
    int64_t     m_userid;    //新成员 userid
    std::string m_nickname;  //新成员的账号昵称（UTF-8）；取不到时为空串，正文里跳过这个名字、只计人数

    JoinTipMember()
        : m_userid(0)
    {
    }

    JoinTipMember(int64_t userid, const std::string& nickname)
        : m_userid(userid), m_nickname(nickname)
    {
    }
};

//一条群记录，对应数据库 t_group 的一行（membercount 为派生字段）
struct GroupRecord
{
    int64_t     groupid;       //群ID
    std::string name;          //群名称
    int         facetype;      //群头像类型
    std::string customface;    //群自定义头像文件标识
    std::string announcement;  //群公告
    int64_t     ownerid;       //群主userid
    int         joinmode;      //加群方式，取值见 group_join_mode
    int         muteall;       //仅通知模式（原称全员禁言，只有群主和管理员能发言）：0 否 1 是
    int64_t     createtime;    //创建时间，Unix毫秒
    int         membercount;   //群成员数（由 t_group_member 计数派生，不落库）

    //---- 以下为群管理功能新增的群设置项，与 t_group 的同名列一一对应 ----

    std::string intro;         //群简介
    int         atallscope;    //谁能 @ 全体，取值见 group_at_all_scope
    int         newseehistory; //新成员可否查看入群前的历史消息：0 否 1 是
    int         memberinvite;  //普通成员邀请好友是否免审批：0 否（转待审批）1 是（直接入群）
    int         welcomeon;     //入群欢迎语开关：0 关 1 开
    std::string welcometext;   //入群欢迎语正文；welcomeon 为 0 时本字段无意义

    //---- 以下三项为群公告功能新增，是"本群最新一条已发布公告"的冗余快照 ----
    //
    //公告本体在 t_group_announcement，这三项加上上面的 announcement（存最新公告的
    //正文）由服务端在发布 / 撤回后同步刷新。冗余的收益是：所有只读展示都不必连表，
    //cmd_get_group_info 应答与 cmd_group_info_changed_notify 推送顺带就把最新公告
    //带出去了，老客户端也不受影响。

    int64_t     announceid;    //最新已发布公告的ID；0 表示本群还没有公告
    std::string announcetitle; //最新已发布公告的标题（群聊窗头部摘要行显示它）
    int64_t     announcetime;  //最新已发布公告的发布时间，Unix毫秒；0 表示没有公告

    //群名是否由用户设定，对应 t_group.f_name_custom：1 是，群名固定不变；0 否，是按成员
    //昵称生成的自动群名，成员增减、转让群主、成员改昵称后由服务端重算（见 GroupAutoName.h）。
    //只在服务端内部使用，不下发给客户端。
    int         namecustom;

    //入群必读公告的 ID（t_group.f_must_read_id，docs/群公告接入方案.md §14）；0 表示本群没有入群必读
    int64_t     mustreadid;

    //群类型（t_group.f_group_type，取值见 Protocol.h 的 group_type，docs/帖子群方案.md）：0 普通群，1 帖子群。
    //建群时确定，之后不变；帖子群没有普通聊天，客户端据此打开帖子群窗口而不是群聊窗
    int         grouptype;

    GroupRecord()
        : groupid(0), facetype(0), ownerid(0)
        , joinmode(group_join_approval), muteall(0), createtime(0), membercount(0)
        //以下默认值与 t_group 各列的 DEFAULT 保持一致，使"未从库里读出来的
        //GroupRecord"（如单测里手工构造的）与真实新建群的初始状态相同。
        , atallscope(group_at_all_owner_admin), newseehistory(1), memberinvite(0)
        , welcomeon(0)
        , announceid(0), announcetime(0)
        , namecustom(1)
        , mustreadid(0)
        , grouptype(0)
    {
    }
};

//一条群成员记录，对应数据库 t_group_member 的一行；nickname/facetype/customface
//为展示用字段，由会话层从 UserManager 补全（数据库层不填）
struct GroupMemberRecord
{
    int64_t     userid;      //成员userid
    int         role;        //角色，取值见 group_member_role
    std::string card;        //群名片（群内昵称）；空串表示用账号昵称
    int         muted;       //是否被禁言：0 否 1 是
    int64_t     jointime;    //入群时间，Unix毫秒
    std::string nickname;    //账号昵称（展示用，会话层补）
    int         facetype;    //账号头像类型（展示用，会话层补）
    std::string customface;  //账号自定义头像（展示用，会话层补）

    //禁言到期时间，Unix毫秒。与 muted 一起表达禁言态：
    //  muted == 0            —— 未禁言，本字段无意义
    //  muted == 1 且本字段 0 —— 永久禁言
    //  muted == 1 且本字段 >0 —— 禁言至该时刻，到点后由 isMuteExpired 判定为已失效
    //取 0 表示永久，使升级前只有 0/1 的老数据自动落在"永久禁言"上，无需迁移。
    int64_t     muteuntil;
    std::string mutereason;  //禁言原因；muted 为 0 时为空串
    int64_t     historyfrom; //本人可见的最早一条群消息 msgid（t_group_member.f_history_from），0 表示不限
    int         mustread;    //入群必读：0 不需要或已确认，1 待确认（t_group_member.f_must_read）

    GroupMemberRecord()
        : userid(0), role(group_role_member), muted(0), jointime(0), facetype(0)
        , muteuntil(0), historyfrom(0), mustread(0)
    {
    }
};

//一条加群申请记录，对应数据库 t_group_join_request 的一行；nickname/facetype/
//customface 为展示用字段，由会话层从 UserManager 补全（数据库层不填）
struct GroupJoinRequestRecord
{
    int64_t     id;          //申请ID
    int64_t     groupid;     //目标群ID
    int64_t     userid;      //申请人userid
    std::string verifymsg;   //验证留言
    int         status;      //状态：0 待处理 1 已通过 2 已拒绝
    int64_t     createtime;  //申请时间，Unix毫秒
    int64_t     handletime;  //处理时间，Unix毫秒，0 表示未处理
    int64_t     handler;     //处理人userid，0 表示未处理
    std::string nickname;    //申请人账号昵称（展示用，会话层补）
    int         facetype;    //申请人头像类型（展示用，会话层补）
    std::string customface;  //申请人自定义头像（展示用，会话层补）

    int         source;      //申请来源，取值见 group_join_source；供审批页显示"来源：xxx"
    int64_t     recommender; //推荐人userid，0 表示无；仅 source 为好友邀请时可能非 0
    int         historylimit; //通过后可看的入群前消息条数（t_group_join_request.f_history_limit），由邀请转来的申请
                              //记邀请人的选择，本人主动申请为 kInviteHistoryLimitAll（全部）
    int64_t     inviteid;    //由入群邀请卡片转来时为 t_group_invite 的 id，审批结果据此回写卡片；0 表示不是

    GroupJoinRequestRecord()
        : id(0), groupid(0), userid(0), status(0)
        , createtime(0), handletime(0), handler(0), facetype(0)
        , source(group_join_source_search), recommender(0), historylimit(kInviteHistoryLimitAll), inviteid(0)
    {
    }
};

//一条群公告记录，对应数据库 t_group_announcement 的一行；publishername 为展示用
//字段，由会话层从 UserManager 补全（数据库层不填）——发布人可能已经退群，客户端
//的成员列表里未必找得到他，故昵称必须由服务端下发。
struct GroupAnnouncementRecord
{
    int64_t     id;            //公告ID（自增主键），撤回 / 重新发布时按它定位
    int64_t     groupid;       //所属群ID
    std::string title;         //公告标题（必填）
    std::string content;       //公告正文
    int         severity;      //优先级，取值见 group_announcement_severity
    int64_t     publisher;     //发布人userid
    int64_t     publishtime;   //发布时间，Unix毫秒
    std::string publishername; //发布人昵称（展示用，会话层补）

    GroupAnnouncementRecord()
        : id(0), groupid(0)
        , severity(group_announcement_normal)
        , publisher(0), publishtime(0)
    {
    }
};

//cmd_get_group_announcements 请求
struct GetAnnouncementsRequest
{
    int64_t groupid;   //目标群ID
    int     offset;    //分页起始下标，从 0 起
    int     count;     //本页最多几条；缺省或越界由数据层夹到 kAnnouncementPageMax

    GetAnnouncementsRequest()
        : groupid(0), offset(0), count(0)
    {
    }
};

//cmd_publish_group_announcement 请求。
//两种用法共用一个结构：
//  · 发布新公告 —— 带 title / content / severity，id 为 0；
//  · 重新发布   —— 只带 id，会话层据它读回原件、把标题正文原样再发一条。
//    之所以是"再发一条"而不是改旧那条的时间：历史里应当留下"这条被重发过"的
//    痕迹，改时间会让先前那次发布凭空消失。
struct PublishAnnouncementRequest
{
    int64_t     groupid;   //目标群ID
    int64_t     id;        //重新发布时的原公告ID；发布新公告时为 0
    std::string title;     //公告标题，发布新公告时必填
    std::string content;   //公告正文，可空
    int         severity;  //优先级，取值见 group_announcement_severity

    PublishAnnouncementRequest()
        : groupid(0), id(0), severity(group_announcement_normal)
    {
    }
};

//cmd_invite_to_group / cmd_kick_group_member 请求（两者同构：群ID + 用户ID列表）
struct GroupMembersOpRequest
{
    int64_t              groupid;  //目标群ID
    std::vector<int64_t> userids;  //被邀请 / 被移出的用户userid列表
    int                  historylimit; //仅邀请用：新成员入群后可看的入群前消息条数，取值见 kInviteHistoryLimitAll /
                                       //kInviteHistoryLimitMax；请求不带时为 kInviteHistoryLimitAll（全部）

    GroupMembersOpRequest()
        : groupid(0), historylimit(kInviteHistoryLimitAll)
    {
    }
};

//cmd_join_group_request 请求
struct JoinGroupRequest
{
    int64_t     groupid;    //目标群ID
    std::string verifymsg;  //验证留言

    JoinGroupRequest()
        : groupid(0)
    {
    }
};

//cmd_set_member_role / cmd_mute_member 请求（两者同构：群ID + 成员ID + 一个整型值，
//set_member_role 的整型值为角色、mute_member 的为禁言标记）
struct GroupMemberValueRequest
{
    int64_t groupid;  //目标群ID
    int64_t userid;   //目标成员userid
    int     value;    //整型值：角色（0成员 1管理员）或禁言标记（0解禁 1禁言）

    //以下两项仅 cmd_mute_member 用，且都是可选字段 —— 老客户端发来的三字段请求
    //解析后 duration 为 0、reason 为空，即"永久禁言、不填原因"，与扩展前行为一致。
    int         duration;  //禁言时长，单位秒；0 表示永久禁言。value 为 0（解禁）时忽略
    std::string reason;    //禁言原因，可为空串。value 为 0（解禁）时忽略

    GroupMemberValueRequest()
        : groupid(0), userid(0), value(0), duration(0)
    {
    }
};

//cmd_handle_join_group 请求
struct HandleJoinGroupRequest
{
    int64_t requestid;  //被处理的加群申请ID
    int     action;     //处理动作：1 同意 0 拒绝

    HandleJoinGroupRequest()
        : requestid(0), action(0)
    {
    }
};

//cmd_create_group 请求
struct CreateGroupRequest
{
    std::string name;                //群名称；空串表示由服务端生成自动群名
    std::vector<int64_t> memberids;  //初始成员userid列表（不含创建者）
    int grouptype;                   //群类型，取值见 group_type；请求不带时为 group_type_normal（docs/帖子群方案.md 6.3）

    CreateGroupRequest()
        : grouptype(0)
    {
    }
};

//admin 控制台建群请求（走 HTTP /admin/group/create.do，区别于 IM 客户端的 cmd_create_group）。
//与 CreateGroupRequest 的差异：群主由后台显式指定（未必是操作者本人），并可同时指定若干管理员。
struct AdminCreateGroupRequest
{
    std::string          name;       //群名称（必填、非空）
    int64_t              ownerid;    //群主userid（必填、须为该企业成员）
    std::vector<int64_t> adminids;   //管理员userid列表（可空；每个都须出现在 memberids 内）
    std::vector<int64_t> memberids;  //普通成员userid列表（不含群主；可含管理员，规整时再拆分）

    AdminCreateGroupRequest()
        : ownerid(0)
    {
    }
};

//cmd_update_group_info 请求；has_* 标记该项本次是否需要更新
struct UpdateGroupInfoRequest
{
    int64_t     groupid;          //目标群ID
    bool        has_name;         //本次是否更新群名称
    std::string name;             //群名称目标值；空串表示恢复为自动群名
    bool        has_facetype;     //本次是否更新头像类型
    int         facetype;         //头像类型目标值
    bool        has_customface;   //本次是否更新自定义头像
    std::string customface;       //自定义头像目标值
    bool        has_announcement; //本次是否更新群公告
    std::string announcement;     //群公告目标值
    bool        has_joinmode;     //本次是否更新加群方式
    int         joinmode;         //加群方式目标值
    bool        has_muteall;      //本次是否更新全员禁言
    int         muteall;          //仅通知模式（原称全员禁言）的目标值

    //---- 以下六组为群管理功能新增的群设置项，与 GroupRecord 的同名字段对应 ----

    bool        has_intro;         //本次是否更新群简介
    std::string intro;             //群简介目标值
    bool        has_atallscope;    //本次是否更新"谁能@全体"
    int         atallscope;        //"谁能@全体"目标值，取值见 group_at_all_scope
    bool        has_newseehistory; //本次是否更新"新成员看历史消息"
    int         newseehistory;     //"新成员看历史消息"目标值：0 否 1 是
    bool        has_memberinvite;  //本次是否更新"成员邀请好友免审批"
    int         memberinvite;      //"成员邀请好友免审批"目标值：0 否 1 是
    bool        has_welcomeon;     //本次是否更新入群欢迎语开关
    int         welcomeon;         //入群欢迎语开关目标值：0 关 1 开
    bool        has_welcometext;   //本次是否更新入群欢迎语正文
    std::string welcometext;       //入群欢迎语正文目标值

    UpdateGroupInfoRequest()
        : groupid(0), has_name(false), has_facetype(false), facetype(0)
        , has_customface(false), has_announcement(false)
        , has_joinmode(false), joinmode(group_join_approval)
        , has_muteall(false), muteall(0)
        , has_intro(false)
        , has_atallscope(false), atallscope(group_at_all_owner_admin)
        , has_newseehistory(false), newseehistory(1)
        , has_memberinvite(false), memberinvite(0)
        , has_welcomeon(false), welcomeon(0)
        , has_welcometext(false)
    {
    }
};

//本人在一个群里最多关注多少位成员（cmd_set_group_follow，docs/消息提醒模式与消息盒子方案.md §12.1）。
//关注的人多了等于没有关注，模式 5 退化成「接收不提醒」之外处处提醒；上限同时防止刷表。
//客户端选人时按同一个值限制，超出的请求以 error_code_bad_request 拒绝
static const size_t kGroupFollowMax = 20;

//本人在一个群里「关注的成员」名单：cmd_set_group_follow 请求、cmd_group_follow_changed_notify 推送，
//以及 cmd_sync_conversations 应答顶层 groupfollows 数组的一项（t_group_follow 的一组行）
struct GroupFollowList
{
    int64_t              groupid;  //群ID
    std::vector<int32_t> targets;  //关注的成员 userid；解析请求时已去重并保持原顺序，读库时为升序；空表示清空

    GroupFollowList()
        : groupid(0)
    {
    }
};

//群组报文编解码工具类；全部为静态方法，不可实例化
class GroupProtocol
{
public:
    /**
     *  解析 cmd_create_group 请求。
     *  @param json 请求包体（UTF-8 JSON）：{ groupname, memberids:[...] }
     *  @param req  [出参] 解析结果
     *  @return 带有字符串类型的 groupname 时返回 true。groupname 可以为空串，表示使用
     *          自动群名；memberids 允许为空
     */
    static bool parseCreateGroupRequest(const std::string& json, CreateGroupRequest& req);

    /**
     *  解析 admin 控制台建群请求（HTTP /admin/group/create.do 的包体）。
     *  @param json 请求包体（UTF-8 JSON）：
     *              { "groupname":"...", "ownerid":"...", "adminids":["..."], "memberids":["..."] }
     *  @param req  [出参] 解析结果（仅做字段提取，不校验角色约束）
     *  @return groupname 非空时返回 true；ownerid 与角色约束的校验交给 validateAdminCreateGroup
     */
    static bool parseAdminCreateGroupRequest(const std::string& json, AdminCreateGroupRequest& req);

    /**
     *  校验并规整 admin 建群的角色分配，得到落库所需、去重且互斥的管理员 / 普通成员列表。
     *  规则：
     *    - 群主 ownerid 必须为正，否则拒绝；
     *    - 群主自动成为群成员（群主行由 createGroup 单独写，不进 outAdmins / outMembers）；
     *    - 每个管理员必须是群主、或出现在 memberids 中，否则拒绝（防止传入未选中的成员）；
     *    - 角色优先级 群主 > 管理员 > 普通成员：同一 userid 只按最高角色落一次；
     *    - outAdmins / outMembers 均按首次出现顺序去重，且与群主彼此互不相交。
     *  本函数为纯逻辑（不触库）；成员的企业归属校验由会话层另行完成。
     *  @param req        已解析的建群请求
     *  @param outAdmins  [出参] 去重后的管理员userid列表（不含群主）
     *  @param outMembers [出参] 去重后的普通成员userid列表（不含群主与管理员）
     *  @return 通过校验返回 true；群主非正、或存在不在成员集内的管理员时返回 false
     */
    static bool validateAdminCreateGroup(const AdminCreateGroupRequest& req,
                                         std::vector<int64_t>& outAdmins,
                                         std::vector<int64_t>& outMembers);

    /**
     *  解析仅含群ID的请求（cmd_dismiss_group / cmd_get_group_info /
     *  cmd_get_group_members 共用，请求体为 { "groupid": "..." }）。
     *  @param groupid [出参] 目标群ID
     *  @return groupid 缺失或非正返回 false
     */
    static bool parseGroupIdRequest(const std::string& json, int64_t& groupid);

    /**
     *  解析 cmd_update_group_info 请求；name/facetype/customface/announcement/
     *  joinmode/muteall 均为可选字段，仅出现的字段对应 has_* 置 true。
     *  @return groupid 缺失或非正返回 false
     */
    static bool parseUpdateGroupInfoRequest(const std::string& json, UpdateGroupInfoRequest& req);

    /**
     *  取出一次改群资料（cmd_update_group_info）里需要做敏感词检查的文字，追加到 out
     *  （docs/敏感词方案.md 第 6.3 节）。只取群名、简介、欢迎语、旧版公告字段四项中本次提交、
     *  与当前值不同、且不为空串的那几项：客户端可能把整份群资料一起提交，原样带上来的旧值
     *  属于加词之前就有的内容，不检查；群名为空串表示恢复为由成员昵称拼成的自动群名，没有
     *  用户输入的文字。取的顺序为群名、简介、欢迎语、公告。
     *  @param req     已解析的改群资料请求
     *  @param current 该群当前的资料（只用到上述四项）
     *  @param out     [出参] 取出的文字（UTF-8）追加到末尾；原有内容保留
     */
    static void collectChangedGroupInfoTexts(const UpdateGroupInfoRequest& req, const GroupRecord& current,
                                             std::vector<std::string>& out);

    /**
     *  构造 cmd_create_group 应答 JSON：{ code, msg, groupid }。
     */
    static std::string buildCreateGroupResponse(int code, const std::string& msg, int64_t groupid);

    /**
     *  构造 cmd_get_group_info 应答 JSON：群资料 + 本人在群内的角色。
     *  @param myrole 请求者在该群的角色，取值见 group_member_role
     */
    static std::string buildGroupInfoResponse(int code, const std::string& msg,
                                              const GroupRecord& group, int myrole);

    /**
     *  构造 cmd_get_group_members 应答 JSON：{ code, msg, groupid, members:[...] }。
     */
    static std::string buildGroupMembersResponse(int code, const std::string& msg,
                                                 int64_t groupid,
                                                 const std::vector<GroupMemberRecord>& members);

    /**
     *  构造 cmd_get_user_groups 应答 JSON：{ code, msg, groups:[ {群资料, pinned}... ] }。
     *  groups 每项字段与 cmd_get_group_info 一致（groupid/name/ownerid/membercount 等），
     *  另加 pinned：本人是否置顶了该群（1 是 0 否）。
     *  供客户端"群"Tab 显示本人加入的完整群列表（含无消息的空群），并把置顶的群排在最前。
     *  @param groups         本人加入的群
     *  @param pinnedGroupIds 本人置顶的群ID集合（见 MessageManager::getPinnedConversationIds）
     */
    static std::string buildUserGroupsResponse(int code, const std::string& msg,
                                               const std::vector<GroupRecord>& groups,
                                               const std::set<int64_t>& pinnedGroupIds);

    /**
     *  构造 cmd_group_info_changed_notify 推送 JSON。
     *  @param changetype 变更类型，取值见 group_info_change_type
     */
    static std::string buildGroupInfoChangedNotify(const GroupRecord& group, int changetype);

    /**
     *  构造 cmd_group_member_changed_notify 推送 JSON。
     *  @param groupid    发生成员变动的群ID
     *  @param changetype 变更类型，取值见 group_member_change_type
     *  @param userids    本次发生变动的成员userid列表
     *  @param operatorId 触发本次变动的操作者userid
     */
    static std::string buildGroupMemberChangedNotify(int64_t groupid, int changetype,
                                                     const std::vector<int64_t>& userids,
                                                     int64_t operatorId);

    /**
     *  解析 cmd_invite_to_group / cmd_kick_group_member 请求（两者同构）。
     *  请求体为 { "groupid": "...", "userids": ["...","..."] }。
     *  @return groupid 为正且 userids 非空时返回 true
     */
    static bool parseGroupMembersOpRequest(const std::string& json, GroupMembersOpRequest& req);

    /**
     *  解析 cmd_join_group_request 请求；groupid 缺失或非正返回 false。
     */
    static bool parseJoinGroupRequest(const std::string& json, JoinGroupRequest& req);

    /**
     *  解析 cmd_handle_join_group 请求；requestid 缺失/非正或 action 缺失返回 false。
     */
    static bool parseHandleJoinGroupRequest(const std::string& json, HandleJoinGroupRequest& req);

    /**
     *  构造 cmd_join_group_request 应答 JSON：{ code, msg, joined }。
     *  @param joined true=已直接入群（自由加入的群）；false=申请已提交、待审批
     */
    static std::string buildJoinGroupResponse(int code, const std::string& msg, bool joined);

    /**
     *  构造 cmd_join_group_request_notify 推送 JSON（推给群主 / 管理员）。
     */
    static std::string buildJoinGroupRequestNotify(const GroupJoinRequestRecord& item);

    /**
     *  构造 cmd_join_request_handled_notify（1329）推送 JSON：一条加群申请已被处理，推给该群全体群主与管理员，
     *  他们的待审批列表据此去掉这一条（docs/邀请入群方案.md 第 4 期）。形如
     *  { groupid, requestid, status, handler, handlername }，id 写成字符串。
     *  @param groupid     群ID
     *  @param requestid   申请ID
     *  @param status      处理结果：1 已通过 2 已拒绝
     *  @param handler     处理人userid
     *  @param handlerName 处理人昵称（展示用，可为空）
     *  @return 紧凑 JSON 文本
     */
    static std::string buildJoinRequestHandledNotify(int64_t groupid, int64_t requestid, int status,
                                                     int64_t handler, const std::string& handlerName);

    /**
     *  构造 cmd_get_group_join_requests 应答 JSON：
     *  { code, msg, groupid, requests:[ {requestid,userid,nickname,facetype,
     *    customface,verifymsg,timestamp} ] }。
     */
    static std::string buildGetJoinRequestsResponse(int code, const std::string& msg,
                                                    int64_t groupid,
                                                    const std::vector<GroupJoinRequestRecord>& items);

    /**
     *  解析 cmd_set_member_role / cmd_mute_member 请求（两者同构）。
     *  请求体为 { "groupid": "...", "userid": "...", "<valueKey>": <int> }。
     *  @param valueKey 整型值字段名：set_member_role 传 "role"、mute_member 传 "muted"
     *  @return groupid/userid 为正且整型值字段存在时返回 true
     */
    static bool parseGroupMemberValueRequest(const std::string& json, const char* valueKey,
                                             GroupMemberValueRequest& req);

    /**
     *  解析 cmd_mute_all 请求（请求体为 { "groupid": "...", "muteall": <int> }）。
     *  @return groupid 为正且 muteall 字段存在时返回 true
     */
    static bool parseMuteAllRequest(const std::string& json, int64_t& groupid, int& muteall);

    /**
     *  解析 cmd_set_member_card 请求（请求体为 { "groupid": "...", "card": "..." }）。
     *  card 可为空串（表示清除群名片）。
     *  @return groupid 为正返回 true
     */
    static bool parseSetMemberCardRequest(const std::string& json,
                                          int64_t& groupid, std::string& card);

    /**
     *  解析 cmd_transfer_owner 请求（请求体为 { "groupid": "...", "userid": "..." }）。
     *  @param userid [出参] 新群主的userid
     *  @return groupid/userid 均为正返回 true
     */
    static bool parseTransferOwnerRequest(const std::string& json,
                                          int64_t& groupid, int64_t& userid);

    // ================================================================
    // 群公告
    // ================================================================

    /**
     *  解析 cmd_get_group_announcements 请求。
     *  请求体为 { "groupid":"...", "offset":<int>, "count":<int> }，
     *  除 groupid 外均可缺省（缺省取 0，由数据层夹到合理范围）。
     *  @return groupid 为正返回 true
     */
    static bool parseGetAnnouncementsRequest(const std::string& json,
                                             GetAnnouncementsRequest& req);

    /**
     *  解析 cmd_publish_group_announcement 请求。
     *  请求体为 { "groupid":"...", "title":"...", "content":"...", "severity":<int> }（新发布）
     *  或        { "groupid":"...", "id":"..." }（重新发布）。
     *
     *  校验：groupid 为正；重新发布时 id 为正即可；新发布时标题去掉首尾空白后
     *  非空、且长度不超过 kAnnouncementTitleMaxChars，正文不超过
     *  kAnnouncementContentMaxChars；severity 越界时归为"通知"而不是判非法
     *  （多一个未知档位不值得让整条请求失败）。
     *  @return 上述校验全部通过返回 true
     */
    static bool parsePublishAnnouncementRequest(const std::string& json,
                                                PublishAnnouncementRequest& req);

    /**
     *  解析 cmd_delete_group_announcement 请求（请求体为 { "groupid":"...", "id":"..." }）。
     *  @param groupid [出参] 目标群ID
     *  @param id      [出参] 待删除的公告ID
     *  @return groupid 与 id 均为正返回 true
     */
    static bool parseDeleteAnnouncementRequest(const std::string& json,
                                               int64_t& groupid, int64_t& id);

    /**
     *  构造 cmd_get_group_announcements 应答 JSON：
     *  { code, msg, groupid, total, offset, count, announcements:[ {id,title,content,
     *    severity,publisher,publishername,publishtime} ] }。
     *
     *  offset / count 是<u>原样回显</u>本次请求的分页参数（与 cmd_get_group_files
     *  的做法一致）。同一个客户端可能有两处同时在拉公告 —— 群管理窗按每页 8 条拉、
     *  群聊窗边栏一次只拉 1 条 —— 而应答里没有请求身份的话，两处只能各自把收到的
     *  任何一份都当成自己的，后到的那份会把另一处的列表覆盖掉。回显这两项，各处
     *  就能只认自己要的那一份。
     *
     *  @param total  本群公告总条数（不受分页限制），供客户端画"第 x/y 页"
     *  @param offset 回显请求里的 offset
     *  @param count  回显请求里的 count
     */
    static std::string buildAnnouncementsResponse(int code, const std::string& msg,
                                                  int64_t groupid, int total,
                                                  int offset, int count,
                                                  const std::vector<GroupAnnouncementRecord>& items);

    /**
     *  构造 cmd_publish_group_announcement 应答 JSON：{ code, msg, id }。
     *  @param id 新公告ID；失败时传 0
     */
    static std::string buildPublishAnnouncementResponse(int code, const std::string& msg,
                                                        int64_t id);

    /**
     *  构造 cmd_group_announcement_notify 推送 JSON：
     *  { changetype, groupid, announcement:{...}, cardmsgid?, card? }。
     *  推送体里带完整公告，客户端收到即可直接刷新边栏 / 弹窗，不必再拉一次。
     *  删除推送另带这条公告的卡片（2026-10-05 起，docs/群公告接入方案.md §13.3）：cardmsgid 是卡片的 msgid，
     *  card 是改写后的卡片 content，在线客户端据此把已显示的卡片与本地库里那一条换成已删除形态。
     *  @param item        变更涉及的那条公告
     *  @param changetype  取值见 group_announcement_change_type
     *  @param cardMsgId   公告卡片的 msgid；0 表示没有卡片（本功能上线前发布的公告、或发布推送），不写 cardmsgid
     *  @param cardContent 改写后的卡片 content（紧凑 JSON）；空串或解析不了时不写 card
     */
    static std::string buildAnnouncementNotify(const GroupAnnouncementRecord& item,
                                               int changetype,
                                               int64_t cardMsgId = 0,
                                               const std::string& cardContent = std::string());

    /**
     *  拼一条公告对应的卡片的 clientmsgid：kAnnouncementCardClientMsgIdPrefix + 群id + "-" + 公告id。
     *  @param groupid        群 id
     *  @param announcementId 公告 id
     *  @return 形如 announce-card-268435463-15 的字符串
     */
    static std::string announcementCardClientMsgId(int64_t groupid, int64_t announcementId);

    /**
     *  构造群公告卡片（message_type_announcement）的 content：
     *  { announcementid, groupid, title, body, severity, publisherid, publisher, publishtime, republish, text }，
     *  字段含义见 docs/群公告接入方案.md §13.2。text 为「[群公告] 标题」，只给不认识本类型的老客户端兜底显示。
     *  @param item      刚发布的那条公告，publishername 须已由会话层补好（取不到时为空串，照写空串）
     *  @param republish 是否由「重新发布」产生
     *  @return content 的紧凑 JSON 文本
     */
    static std::string buildAnnouncementCardContent(const GroupAnnouncementRecord& item, bool republish);

    /**
     *  把一张公告卡片的 content 改写为已删除形态：去掉 body，加 deleted:true，text 改为「[群公告已删除] 标题」，
     *  其余字段原样保留。公告被删除时由会话层调用，结果经 MessageManager::updateMessageContent 写回。
     *  @param cardContent 卡片当前的 content（紧凑 JSON）
     *  @param out         [出参] 改写后的 content；失败时不修改
     *  @return cardContent 不是 JSON 对象时返回 false
     */
    static bool buildDeletedAnnouncementCardContent(const std::string& cardContent, std::string& out);

    /**
     *  构造通用 { code, msg } 应答。
     */
    static std::string buildSimpleResponse(int code, const std::string& msg);

    /**
     *  判定一条禁言记录此刻是否已经过期。
     *
     *  禁言态由 t_group_member 的 f_muted 与 f_mute_until 两列共同表达：
     *    muted == 0                   —— 本来就没禁言，返回 false（谈不上过期）
     *    muted != 0 且 muteUntil <= 0 —— 永久禁言，永不过期，返回 false
     *    muted != 0 且 muteUntil > 0  —— 定时禁言，nowMs >= muteUntil 时返回 true
     *
     *  之所以让"到期时间为 0"表示永久：升级前只有 f_muted 一列、没有时长概念，
     *  新加的 f_mute_until 列默认值为 0，于是老数据里 f_muted=1 的行自动落在
     *  "永久禁言"上，语义与升级前一致，无需任何数据迁移。
     *
     *  放在本类而不是 GroupManager，是因为 GroupManager 依赖数据库、不进单测
     *  工程；本类是纯逻辑，这条判定得以被直接测到。
     *
     *  @param muted     库里的 f_muted
     *  @param muteUntil 库里的 f_mute_until，Unix毫秒；0 表示永久
     *  @param nowMs     判定时刻，Unix毫秒
     *  @return 该禁言已过期、应按未禁言处理时返回 true
     */
    static bool isMuteExpired(int muted, int64_t muteUntil, int64_t nowMs);

    /**
     *  由禁言时长算出到期时刻，供 cmd_mute_member 落库前调用。
     *  @param muted           本次要写入的禁言标记：0 解禁 1 禁言
     *  @param durationSeconds 禁言时长，单位秒；<= 0 表示永久禁言
     *  @param nowMs           当前时刻，Unix毫秒
     *  @return 到期时刻，Unix毫秒。解禁（muted 为 0）或永久禁言时返回 0
     */
    static int64_t computeMuteUntil(int muted, int durationSeconds, int64_t nowMs);

    /**
     *  拼"群主已转让"这条群系统提示的正文，转让群主成功后由服务端插进群里。
     *  @param fromNickname 原群主昵称（UTF-8），可为空
     *  @param toNickname   新群主昵称（UTF-8），可为空
     *  @return 两端都有昵称时形如「张三」已将群主转让给「李四」；缺哪一端就省掉哪一端的名字，
     *          两端都缺时为"群主已变更"
     */
    static std::string buildOwnerTransferTip(const std::string& fromNickname,
                                             const std::string& toNickname);

    //原 buildAnnouncementTip（拼「某人发布了群公告：标题」）与 buildAnnouncementTipContent 于 2026-10-05 删除：
    //发布群公告改为在群里发群公告卡片（buildAnnouncementCardContent），不再插这条系统提示

    /**
     *  拼入群欢迎语这条群系统提示的正文：新成员昵称用"、"连接，后接一个半角空格与群主设置的欢迎词。
     *  @param names   新成员昵称（UTF-8）；其中的空串跳过，全部为空时只有欢迎词
     *  @param welcome 群主设置的欢迎词（UTF-8）
     *  @return 形如 张三、李四 欢迎加入本群 的 UTF-8 文本
     */
    static std::string buildWelcomeTip(const std::vector<std::string>& names, const std::string& welcome);

    /**
     *  构造"转让群主"系统提示的完整 content（多语言第 5 期）：
     *  { text: buildOwnerTransferTip 的中文, tipkind: kSystemTipOwnerTransfer, tipargs: { from, to } }。
     *  @param fromNickname 原群主昵称（UTF-8）；为空时 tipargs 不写 from
     *  @param toNickname   新群主昵称（UTF-8）；为空时 tipargs 不写 to
     *  @return content 的紧凑 JSON 文本
     */
    static std::string buildOwnerTransferTipContent(const std::string& fromNickname,
                                                    const std::string& toNickname);

    /**
     *  拼「开启 / 关闭仅通知模式」（原称全员禁言）这条群系统提示的中文正文，供不认识 tipkind 的老客户端显示
     *  （docs/群管理接入方案.md §14）。
     *  @param muteall          新的开关状态：非 0 为开启，0 为关闭
     *  @param operatorNickname 操作人昵称（UTF-8），可为空
     *  @return 有昵称时形如「张三」开启了仅通知模式，仅群主和管理员可以发言 /「张三」关闭了仅通知模式；
     *          没有昵称时为「已开启仅通知模式，仅群主和管理员可以发言」/「已关闭仅通知模式」
     */
    static std::string buildMuteAllTip(int muteall, const std::string& operatorNickname);

    /**
     *  构造「开启 / 关闭仅通知模式」系统提示的完整 content：
     *  { text: buildMuteAllTip 的中文, tipkind: kSystemTipMuteAllOn 或 kSystemTipMuteAllOff,
     *    tipargs: { operator, operatorid } }。operatorid 写成字符串，与其它 userid 字段口径一致。
     *  @param muteall          新的开关状态：非 0 为开启，0 为关闭
     *  @param operatorNickname 操作人昵称（UTF-8）；为空时 tipargs 不写 operator
     *  @param operatorId       操作人 userid；<= 0 时 tipargs 不写 operatorid
     *  @return content 的紧凑 JSON 文本
     */
    static std::string buildMuteAllTipContent(int muteall, const std::string& operatorNickname, int64_t operatorId);

    /**
     *  构造入群欢迎语系统提示的完整 content（多语言第 5 期）：
     *  { text: buildWelcomeTip 的中文, tipkind: kSystemTipGroupWelcome, tipargs: { names: [...], welcome } }。
     *  欢迎词是群主写的内容，客户端原样显示，只按界面语言换昵称之间的分隔符。
     *  @param names   新成员昵称（UTF-8）；其中的空串跳过，names 数组可能为空
     *  @param welcome 群主设置的欢迎词（UTF-8）
     *  @return content 的紧凑 JSON 文本
     */
    static std::string buildWelcomeTipContent(const std::vector<std::string>& names, const std::string& welcome);

    /**
     *  拼「有人入群」这条群系统提示的正文（docs/邀请入群方案.md §3.1），任何入群路径完成后由服务端插进群里。
     *  名字各加书名号、用「、」连接，只从 members 的前 kMemberJoinTipMaxNames 位里取，其中昵称为空的跳过；
     *  列出的名字少于实际人数时末尾写「等 N 人」，一个名字都列不出时写「N 位新成员」。
     *  @param inviterId       邀请人 userid；0 表示没有邀请人（自由加群、本人申请经审批），正文里不出现「某某邀请」
     *  @param inviterNickname 邀请人昵称（UTF-8）；inviterId 非 0 而昵称为空时写成「有人」
     *  @param members         全部新成员，按入群顺序排列；为空时返回空串
     *  @return 形如「张三」邀请「李四」、「王五」加入了群聊 的 UTF-8 文本
     */
    static std::string buildMemberJoinTip(int64_t inviterId, const std::string& inviterNickname,
                                          const std::vector<JoinTipMember>& members);

    /**
     *  构造「有人入群」系统提示的完整 content：
     *  { text: buildMemberJoinTip 的中文, tipkind: kSystemTipMemberJoin,
     *    tipargs: { inviter, inviterid, members: [{ userid, nickname }] } }。
     *  inviterId 为 0 时不写 inviter 与 inviterid；邀请人昵称为空时只写 inviterid。members 带全部新成员的 userid，
     *  只有前 kMemberJoinTipMaxNames 位中昵称非空的带 nickname —— 客户端取「带 nickname 的那些项」即得到与
     *  正文相同的名单，再按 userid 判断提示是否涉及本人（排在后面的新成员也要能看到「邀请你」）。
     *  id 一律写成字符串，与群组段其它报文一致。
     *  @param inviterId       同 buildMemberJoinTip
     *  @param inviterNickname 同 buildMemberJoinTip
     *  @param members         同 buildMemberJoinTip
     *  @return content 的紧凑 JSON 文本
     */
    static std::string buildMemberJoinTipContent(int64_t inviterId, const std::string& inviterNickname,
                                                 const std::vector<JoinTipMember>& members);

    /**
     *  判断一条群系统消息是不是「有人入群」提示、且新成员名单里有某人（2026-10-07 用户确认：新成员本人的
     *  入群提示计未读，其余系统提示仍不计）。会话未读数（MessageManager）与客户端实时计未读用同一口径。
     *  @param storedContent t_chatmsg.f_content 的原文：MessageProtocol::buildStoredContent 包装后的
     *                       { content: { tipkind, tipargs: { members: [{ userid }] } }, mentions, ... }；
     *                       没有外层包装、直接是 content 的也认
     *  @param userid        要判断的人；不大于 0 时返回 false
     *  @return tipkind 为 kSystemTipMemberJoin 且 tipargs.members 里有这个 userid 时返回 true；邀请人
     *          （tipargs.inviterid）不算；不是合法 JSON、字段缺失时返回 false
     */
    static bool isMemberJoinTipFor(const std::string& storedContent, int64_t userid);

    /**
     *  加群申请审批通过时，「有人入群」提示里写谁作邀请人（docs/邀请入群方案.md §3.4、§12.1）：
     *  由普通成员的邀请转来的申请（group_join_source_friend）与扫码 / 加群链接转来的申请
     *  （group_join_source_qrcode，推荐人是链接生成者）都取推荐人，与邀请直接入群、链接直接入群时的提示一致；
     *  本人搜索后主动申请的没有邀请人。
     *  @param source      申请来源，取值见 group_join_source
     *  @param recommender 申请记下的推荐人 userid；不大于 0 表示没有
     *  @return 邀请人 userid；0 表示提示里不写邀请人
     */
    static int64_t joinTipInviterForRequest(int source, int64_t recommender);

    /**
     *  算本人在某个群里能看到的历史消息范围（docs/邀请入群方案.md §5.3）。群主与管理员不设下界 —— 群级开关
     *  「新成员不可查看入群前的历史消息」与邀请时选择的同步条数都是拿来限制普通新成员的，管理者需要看全量记录
     *  才能处理纠纷；普通成员的时间下界只在群关闭该开关时取入群时间，msgid 下界直接取本人成员行的边界。
     *  @param role          本人在群里的角色，取值见 group_member_role
     *  @param newseehistory 群设置 f_new_see_history：0 新成员不可看入群前的历史，1 可以看
     *  @param jointime      本人的入群时间（Unix 毫秒）
     *  @param historyfrom   本人成员行的 f_history_from（0 表示不限）
     *  @return 两个下界；为 0 的一项表示不限
     */
    static GroupHistoryBound computeHistoryBound(int role, int newseehistory, int64_t jointime, int64_t historyfrom);

    /**
     *  判定本人能否在群里 @ 全体（普通群发消息与帖子群发帖、编辑、补充、留言共用，docs/帖子群方案.md §15.1）。
     *  群主与管理员恒可；普通成员只在群设置「谁可以 @ 全体」不是「仅群主和管理员」时可以。
     *  @param atallscope 群设置 f_at_all_scope，取值见 group_at_all_scope
     *  @param role       本人在群里的角色，取值见 group_member_role
     *  @return true 表示可以 @ 全体，false 表示应以 error_code_at_all_denied 拒绝
     */
    static bool canMentionAll(int atallscope, int role);

    /**
     *  构造 cmd_invite_to_group 的应答：{ code, msg, added, invited, pending }（docs/邀请入群方案.md §4.2）。
     *  老客户端只读 code 与 msg，多出的三项不影响它们。
     *  @param code    错误码，0 表示成功
     *  @param msg     说明文字
     *  @param added   直接入群的人数
     *  @param invited 发出入群邀请卡片、等对方同意的人数（「邀请入群需被邀请人同意」开启时）
     *  @param pending 转为加群申请、等群主或管理员审批的人数
     *  @return 应答的紧凑 JSON 文本
     */
    static std::string buildInviteToGroupResponse(int code, const std::string& msg, int added, int invited,
                                                  int pending);

    /**
     *  解析 cmd_set_group_follow 请求 { groupid:"群ID", targets:["userid", ...] }
     *  （docs/消息提醒模式与消息盒子方案.md §12.4）。targets 的元素接受字符串或整数；重复的
     *  userid 只留第一次出现的那个。条数上限、不能填自己、只能填本群成员由会话层校验。
     *  @param json 请求包体
     *  @param out  [出参] 解析结果；返回 false 时内容无意义
     *  @return groupid 缺失或非正、targets 缺失或不是数组、某个元素不是正整数时返回 false
     */
    static bool parseSetGroupFollowRequest(const std::string& json, GroupFollowList& out);

    /**
     *  构造 cmd_group_follow_changed_notify 推送 { groupid:"群ID", targets:["userid", ...] }，
     *  包体与 1330 的请求相同；id 一律写成字符串。
     *  @param list 读回的完整名单
     *  @return 推送的紧凑 JSON 文本
     */
    static std::string buildGroupFollowChangedNotify(const GroupFollowList& list);

    /**
     *  解析 cmd_set_must_read 请求 { groupid:"群ID", announcementid:"公告ID" }（docs/群公告接入方案.md §14）。
     *  @param json           请求包体
     *  @param groupid        [出参] 群ID
     *  @param announcementId [出参] 公告ID；0 表示取消入群必读
     *  @return groupid 为正、announcementid 不为负时返回 true（announcementid 缺省按 0）
     */
    static bool parseSetMustReadRequest(const std::string& json, int64_t& groupid, int64_t& announcementId);

    /**
     *  构造 cmd_get_must_read 应答：{ code, msg, groupid, mustreadid, pending, announcement:{...} }。
     *  @param code       结果码
     *  @param msg        说明
     *  @param groupid    群ID
     *  @param mustreadid 本群的入群必读公告ID，0 表示没有
     *  @param pending    本人是否待确认
     *  @param item       必读公告（mustreadid 为 0 或取不到时传 NULL，应答不带 announcement）
     */
    static std::string buildGetMustReadResponse(int code, const std::string& msg, int64_t groupid,
                                                int64_t mustreadid, bool pending,
                                                const GroupAnnouncementRecord* item);

    /**
     *  构造入群必读卡片的 content：群公告卡片的 content（buildAnnouncementCardContent）另加 "mustread": true。
     *  这张卡片以系统的名义写进群里、只登记那位新成员可见（docs/群公告接入方案.md §14.2）。
     *  @param item 必读公告
     *  @return content 的紧凑 JSON 文本
     */
    static std::string buildMustReadCardContent(const GroupAnnouncementRecord& item);

private:
    GroupProtocol() = delete;
    ~GroupProtocol() = delete;
    GroupProtocol(const GroupProtocol&) = delete;
    GroupProtocol& operator=(const GroupProtocol&) = delete;
};
