/**
 *  群组存储管理器，GroupManager.h
 *
 *  负责 v2 群组（t_group 表）与群成员（t_group_member 表）的落库与查询。
 *  每次操作单独建立一条数据库连接（与 MessageManager / UserManager 的
 *  好友申请表操作同一风格），群与成员不在内存缓存。
 *
 *  典型用法：main.cpp 启动时 Singleton<GroupManager>::Instance().init(...)；
 *  会话层在 cmd_create_group 等处理中调用本类方法。
 *
 *  balloonwj@qq.com   2026-05-21
 */
#pragma once

#include <stdint.h>
#include <string>
#include <vector>
#include <atomic>
#include <map>        //getUserMemberships 的出参
#include <utility>    //std::pair，refreshAutoGroupNamesOfMember 的出参
#include "GroupProtocol.h"

//本人所在的一个群及能看到的历史下界，定义在 MessageManager.h；这里只用作引用参数，前向声明即可，
//免得让包含本头文件的编译单元都跟着引入 MessageManager.h
struct GroupSearchScope;

//群组存储管理器；全局单例，经 Singleton<GroupManager> 访问
class GroupManager final
{
public:
    GroupManager();
    ~GroupManager();

    GroupManager(const GroupManager& rhs) = delete;
    GroupManager& operator=(const GroupManager& rhs) = delete;

    /**
     *  初始化：把群ID分配器 seed 到当前 t_group 的
     *  最大 f_groupid（库为空时 seed 到 GROUPID_BOUBDARY）。
     *  @return 数据库可访问返回 true
     */
    bool init();

    /**
     *  创建群：分配群ID，写入 t_group，并写入群主与初始成员的 t_group_member 行。
     *  @param name       群名称。非空时作为用户设定的群名（f_name_custom = 1），此后不随成员
     *                    变化；为空串时按成员昵称生成自动群名（f_name_custom = 0），调用方
     *                    须再用 getGroup 读回实际群名
     *  @param ownerid    群主userid
     *  @param memberids  初始成员userid列表（不含群主；调用方须已去重 / 去掉无效项）
     *  @param createTime 创建时间，Unix毫秒
     *  @param groupType  群类型，取值见 Protocol.h 的 group_type（docs/帖子群方案.md）；缺省为普通群
     *  @return 成功返回新群ID；失败返回 0
     */
    int64_t createGroup(const std::string& name, int32_t ownerid,
                        const std::vector<int64_t>& memberids, int64_t createTime, int groupType = 0);

    /**
     *  创建群（admin 控制台版）：分配群ID，写入 t_group，并按角色写入群主、管理员、
     *  普通成员的 t_group_member 行。与上面四参版本的区别是支持在建群时直接指定管理员，
     *  且群主可以不是发起本次操作的后台管理员。
     *  调用方须保证 adminids / memberids 已去重、彼此互斥、且都不含 ownerid
     *  （见 GroupProtocol::validateAdminCreateGroup）。
     *  @param name       群名称；为空串时与上面四参版本一样生成自动群名
     *  @param ownerid    群主userid
     *  @param adminids   管理员userid列表（落库角色 group_role_admin）
     *  @param memberids  普通成员userid列表（落库角色 group_role_member）
     *  @param createTime 创建时间，Unix毫秒
     *  @return 成功返回新群ID；失败返回 0
     */
    int64_t createGroup(const std::string& name, int32_t ownerid,
                        const std::vector<int64_t>& adminids,
                        const std::vector<int64_t>& memberids, int64_t createTime);

    /**
     *  解散群：在一个事务里删除该群的 t_group 行、全部成员行、群公告、加群申请、群文件条目、关注名单
     *  （t_group_follow），以及各成员对该群的收藏行（t_favorite）与会话设置行（t_conversation），
     *  任何一步失败都整体回滚。
     *  群消息（t_chatmsg）保留不删，供企业审计追溯；被钉住的消息由调用方另行清理。
     *  @param groupid 目标群ID
     *  @return 全部删除成功并提交时返回 true；失败时库里的数据保持原样
     */
    bool dismissGroup(int64_t groupid);

    /**
     *  取单个群的资料（含派生的 membercount）。
     *  @param groupid 目标群ID
     *  @param out     [出参] 群记录
     *  @return 群存在返回 true
     */
    bool getGroup(int64_t groupid, GroupRecord& out);

    /**
     *  取某群的成员列表（只填 t_group_member 的字段；nickname/facetype/
     *  customface 等展示字段由会话层从 UserManager 补全）。
     *  @param groupid 目标群ID
     *  @param out     [出参] 成员列表，按入群时间升序
     *  @return 数据库访问成功返回 true
     */
    bool getGroupMembers(int64_t groupid, std::vector<GroupMemberRecord>& out);

    /**
     *  取某用户在某群的成员行（只填 t_group_member 的字段）。拉历史、会话内搜索、Pin 列表据此算本人能看到的
     *  历史消息范围（GroupProtocol::computeHistoryBound），一次主键查询，比取整个成员列表再查找省事。
     *  @param groupid 群ID
     *  @param userid  用户ID
     *  @param out     [出参] 成员行；返回 false 时不修改
     *  @return 是该群成员且查询成功返回 true；不是成员或查询失败返回 false
     */
    bool getMember(int64_t groupid, int32_t userid, GroupMemberRecord& out);

    /**
     *  一次取出某用户在他所在的全部群里的成员行（只填 t_group_member 的字段）。全局搜索要按群算本人能看到的
     *  历史消息范围，逐群调用 getMember 会随群数线性增加查询次数，故一次查完。
     *  @param userid 用户ID
     *  @param out    [出参] 群ID → 本人在该群的成员行；先清空
     *  @return 查询成功返回 true（一个群都不在也算成功）
     */
    bool getUserMemberships(int32_t userid, std::map<int64_t, GroupMemberRecord>& out);

    /**
     *  取某用户<u>当前</u>所在的全部群，以及他在每个群里能看到的历史消息下界（入群时间下界、邀请时选择
     *  「同步最近 N 条」得出的 msgid 下界，规则见 GroupProtocol::computeHistoryBound）。跨会话按本人视角取消息的
     *  地方都用它划定群聊的范围：全局搜索（cmd_global_search）、按改写序号同步（cmd_sync_rewritten_messages）。
     *  内部各查一次群列表与本人的成员行，不随群数逐个查询。
     *  @param userid 用户ID
     *  @param out    [出参] 每个群一项；先清空。成员行缺失（并发退群）的群仍列出，不设下界，与原全局搜索一致
     *  @return 群列表查询成功返回 true（一个群都不在也算成功）；成员行查询失败时不设下界，不影响返回值
     */
    bool getUserHistoryScopes(int32_t userid, std::vector<GroupSearchScope>& out);

    /**
     *  取某用户加入的全部群（cmd_get_contacts / 群列表用）。
     *
     *  实现：以 t_group_member 为驱动表，WHERE f_userid = userid 命中
     *  idx_user 索引，直接定位该用户的全部成员行，再 JOIN t_group 取群资料；
     *  membercount 用相关子查询对每个群计数，避免逐群再查（N+1）。等价 SQL：
     *
     *      SELECT g.f_groupid, g.f_name, g.f_facetype, g.f_customface,
     *             g.f_announcement, g.f_ownerid, g.f_join_mode, g.f_mute_all,
     *             g.f_create_time,
     *             (SELECT COUNT(*) FROM t_group_member c
     *                WHERE c.f_groupid = g.f_groupid) AS membercount
     *      FROM   t_group_member m
     *      JOIN   t_group g ON m.f_groupid = g.f_groupid
     *      WHERE  m.f_userid = <userid>
     *
     *  与旧模型（遍历好友关系链挑 friendid>=GROUPID_BOUBDARY 再逐群查）相比，
     *  这里"用户→群列表"是一次走 idx_user 的索引查询。
     *
     *  @param userid 目标用户ID
     *  @param out    [出参] 该用户加入的群列表
     *  @return 数据库访问成功返回 true
     */
    bool getUserGroups(int32_t userid, std::vector<GroupRecord>& out);

    /**
     *  判断某用户是否为某群成员。
     *  @param groupid 群ID
     *  @param userid  用户ID
     *  @return 是成员返回 true
     */
    bool isGroupMember(int64_t groupid, int32_t userid);

    /**
     *  取某用户在某群的角色。
     *  @param groupid 群ID
     *  @param userid  用户ID
     *  @return 命中返回角色（见 group_member_role）；非成员返回 -1
     */
    int getMemberRole(int64_t groupid, int32_t userid);

    /**
     *  一次取某成员的角色与禁言状态（发群消息前的校验用，省一次查询）。
     *  出参 muted 已按 muteUntil 做过到期判定 —— 定时禁言过期后这里直接返回 0，
     *  调用方不必自己再比一次时间。
     *  @param groupid   群ID
     *  @param userid    成员userid
     *  @param role      [出参] 角色（见 group_member_role）
     *  @param muted     [出参] 生效中的禁言标记：0 否 1 是（已扣除已过期的定时禁言）
     *  @param muteUntil [出参] 库里原始的禁言到期时间，Unix毫秒；可传 NULL 表示不关心。
     *                   调用方据它区分"本来就没禁言"（0）与"定时禁言刚过期"（>0 且已过），
     *                   后者需要把库里的 f_muted 清掉，见 clearExpiredMute。
     *  @return 是群成员返回 true（此时各出参才有效）
     */
    bool getMemberRoleAndMuted(int64_t groupid, int32_t userid, int& role, int& muted,
                               int64_t* muteUntil = NULL, int* mustRead = NULL);

    /**
     *  设置或取消本群的入群必读（docs/群公告接入方案.md §14）。取消（announcementId 为 0）时把全部待确认的成员清零，
     *  被清零的人记进 cleared，调用方据此推成员变更。设置与清零在一个事务里。
     *  @param groupid        群ID
     *  @param announcementId 必读公告ID，0 表示取消；调用方已校验它属于本群
     *  @param cleared        [出参] 因取消而解除限制的成员；设置时为空
     *  @return 全部写入成功返回 true
     */
    bool setMustRead(int64_t groupid, int64_t announcementId, std::vector<int64_t>& cleared);

    /**
     *  把一批成员标为入群必读待确认（新成员入群收尾时调用）。不是本群成员的 userid 不受影响。
     *  @return 数据库访问成功返回 true
     */
    bool markMustReadPending(int64_t groupid, const std::vector<int64_t>& userids);

    /**
     *  把一批成员的入群必读标为已确认（本人确认、群主或管理员代为确认）。只改其中待确认的人，
     *  实际被改的人记进 cleared。
     *  @return 数据库访问成功返回 true
     */
    bool clearMustReadPending(int64_t groupid, const std::vector<int64_t>& userids, std::vector<int64_t>& cleared);

    /**
     *  把某成员已过期的定时禁言从库里清掉（f_muted 置 0、到期时间与原因清空）。
     *  仅在发言校验发现禁言已过期时调用 —— 那条路径本来就要写库，顺手清理不额外
     *  增加开销。其余只读路径（如拉成员列表）靠 isMuteExpired 在内存里判定，不写库。
     *  @param groupid 群ID
     *  @param userid  成员userid
     *  @return 数据库访问成功返回 true
     */
    bool clearExpiredMute(int64_t groupid, int32_t userid);

    /**
     *  更新群资料：按 req 中 has_* 为 true 的项更新 t_group 对应列。
     *  群名一项的含义：name 非空时改为用户设定的群名（f_name_custom 置 1）；name 为空串
     *  时恢复为自动群名（f_name_custom 置 0）并立即按当前成员重算，调用方须再用 getGroup
     *  读回实际群名。
     *  @param req 已解析的 cmd_update_group_info 请求
     *  @return 数据库访问成功返回 true
     */
    bool updateGroupInfo(const UpdateGroupInfoRequest& req);

    /**
     *  批量添加群成员（角色固定为普通成员）。已是成员的 userid 用 INSERT IGNORE
     *  静默跳过，故可安全用于"邀请 / 审批通过"。
     *  @param groupid  目标群ID
     *  @param userids  待加入的用户userid列表
     *  @param joinTime 入群时间，Unix毫秒
     *  @param historyFrom 这批新成员能看到的最早一条群消息 msgid（写进 f_history_from），由
     *                     MessageManager::computeGroupHistoryFrom 按邀请时选择的条数算出；0 表示不限
     *  @return 数据库访问成功返回 true
     */
    bool addMembers(int64_t groupid, const std::vector<int64_t>& userids, int64_t joinTime,
                    int64_t historyFrom = 0);

    /**
     *  批量移除群成员（用于退群 / 踢人）：在一个事务里删除这些人的成员行，他们对该群的收藏行
     *  （t_favorite）与会话设置行（t_conversation），以及本群关注名单（t_group_follow）里由他们发起
     *  或以他们为对象的行，任何一步失败都整体回滚。其他成员的其余数据不动。
     *  @param groupid 目标群ID
     *  @param userids 待移除的用户userid列表；为空时直接返回 true；其中已不在群里的人不影响结果
     *  @return 全部删除成功并提交时返回 true；失败时库里的数据保持原样
     */
    bool removeMembers(int64_t groupid, const std::vector<int64_t>& userids);

    /**
     *  新增一条加群申请，返回新申请ID（自增主键）；失败返回 0。
     *  @param groupid     目标群ID
     *  @param userid      申请人userid
     *  @param verifymsg   验证留言
     *  @param createTime  申请时间，Unix毫秒
     *  @param source      申请来源，取值见 group_join_source；缺省为群搜索
     *  @param recommender 推荐人userid，0 表示无；仅 source 为好友邀请时才该非 0
     *  @param historyLimit 通过后可看的入群前消息条数（写进 f_history_limit），由邀请转来的申请传邀请人的选择，
     *                      取值见 kInviteHistoryLimitAll / kInviteHistoryLimitMax；缺省为全部
     *  @param inviteId    由入群邀请卡片转来时传邀请 id（写进 f_invite_id，审批结果据此回写卡片）；缺省 0 表示不是
     */
    int64_t addJoinRequest(int64_t groupid, int32_t userid,
                           const std::string& verifymsg, int64_t createTime,
                           int source = group_join_source_search, int64_t recommender = 0,
                           int historyLimit = kInviteHistoryLimitAll, int64_t inviteId = 0);

    /**
     *  按申请ID取一条加群申请。
     *  @param requestid 申请ID
     *  @param out       [出参] 申请记录
     *  @return 申请存在返回 true
     */
    bool getJoinRequestById(int64_t requestid, GroupJoinRequestRecord& out);

    /**
     *  处理一条加群申请：仅当它仍是待处理时，更新状态、处理时间与处理人。
     *  两位管理员同时处理同一条申请时，两个请求都可能通过「仍是待处理」的检查，这里在 SQL 里再判一次并看实际改到的行数，
     *  只有一个请求能改到（2026-10-04 用例 JQ1 复现过后写的覆盖先写的，见 docs/邀请入群方案.md 第 4 期）。
     *  @param requestid  申请ID
     *  @param status     新状态：1 已通过 2 已拒绝
     *  @param handleTime 处理时间，Unix毫秒
     *  @param handler    处理人userid
     *  @param changed    [出参] 是否真的改到了这条申请；申请已被处理过或不存在时为 false，调用方据此判定本次处理无效
     *  @return 数据库访问成功返回 true（含没改到的情况）
     */
    bool updateJoinRequestStatus(int64_t requestid, int status,
                                 int64_t handleTime, int32_t handler, bool& changed);

    /**
     *  取某群全部待处理（status=0）的加群申请，按申请时间倒序。
     *  @param groupid 目标群ID
     *  @param out     [出参] 待处理申请列表
     *  @return 数据库访问成功返回 true
     */
    bool getPendingJoinRequests(int64_t groupid, std::vector<GroupJoinRequestRecord>& out);

    /**
     *  设置某成员的群角色（cmd_set_member_role 用）。
     *  @param groupid 群ID
     *  @param userid  目标成员userid
     *  @param role    新角色（见 group_member_role）
     *  @return 数据库访问成功返回 true
     */
    bool updateMemberRole(int64_t groupid, int32_t userid, int role);

    /**
     *  设置某成员的禁言状态（cmd_mute_member 用）。
     *  @param groupid   群ID
     *  @param userid    目标成员userid
     *  @param muted     禁言标记：0 解禁 1 禁言
     *  @param muteUntil 禁言到期时间，Unix毫秒；0 表示永久禁言。muted 为 0 时本参数
     *                   被忽略，库里的到期时间会被一并清零。
     *  @param reason    禁言原因，可为空串。muted 为 0 时本参数被忽略，库里的原因
     *                   会被一并清空 —— 否则下一次禁言会读到上一轮残留的原因。
     *  @return 数据库访问成功返回 true
     */
    bool updateMemberMuted(int64_t groupid, int32_t userid, int muted,
                           int64_t muteUntil, const std::string& reason);

    /**
     *  设置某成员的群名片（cmd_set_member_card 用）。
     *  @param groupid 群ID
     *  @param userid  目标成员userid
     *  @param card    新群名片（空串表示清除）
     *  @return 数据库访问成功返回 true
     */
    bool updateMemberCard(int64_t groupid, int32_t userid, const std::string& card);

    /**
     *  转让群主（cmd_transfer_owner 用）：把群主从 oldOwner 转给 newOwner。
     *  在一个事务里更新 t_group.f_ownerid、把新群主角色置为群主、把原群主角色降为普通成员，
     *  任何一步失败都整体回滚。
     *  @param groupid  群ID
     *  @param oldOwner 原群主userid
     *  @param newOwner 新群主userid（须为该群现有成员，由调用方校验）
     *  @return 三处都更新成功并提交时返回 true；失败时库里的数据保持原样
     */
    bool transferOwner(int64_t groupid, int32_t oldOwner, int32_t newOwner);

    // ================================================================
    // 自动群名
    // ================================================================
    //
    // f_name_custom = 0 的群，群名由服务端按成员昵称生成（规则见 GroupAutoName.h）。
    // 本类的加人、删人、转让群主等方法<u>不会</u>自行重算群名 —— 会话层在这些操作之后
    // 调 refreshAutoGroupName，据返回值决定要不要向全群推送 cmd_group_info_changed_notify。

    /**
     *  按当前成员与昵称重算一个群的自动群名，与库中群名不同时写库。
     *  用户设定了群名的群（f_name_custom = 1）不做任何改动。
     *  @param groupid 目标群ID
     *  @param newName [出参] 返回 true 时为写入库中的新群名（UTF-8）；返回 false 时不改动
     *  @return 群名确实改变并已写库返回 true；群不存在、群名是用户设定的、重算结果与原名
     *          相同、或数据库访问失败时返回 false
     */
    bool refreshAutoGroupName(int64_t groupid, std::string& newName);

    /**
     *  某成员改昵称后，重算他所在的全部自动命名群的群名。
     *  @param userid  改了昵称的成员userid
     *  @param changes [出参] 群名确实改变的群，每项为群ID与新群名；调用前不会被清空
     *  @return 数据库访问成功返回 true（即便没有任何群改名）
     */
    bool refreshAutoGroupNamesOfMember(int32_t userid,
                                       std::vector<std::pair<int64_t, std::string> >& changes);

    // ================================================================
    // 群公告
    // ================================================================
    //
    // 公告本体存在 t_group_announcement，一条一行；删除是真删（DELETE FROM），
    // 不做软删。t_group 上的 f_announce_id / f_announce_title / f_announce_time /
    // f_announcement 四列是"最新一条公告"的冗余快照，每次发布 / 删除后由
    // refreshLatestAnnouncement 统一刷新 —— 只读展示因此都不必连表，老客户端
    // 也能照常显示最新公告。

    /**
     *  取某群的公告列表，按发布时间倒序（最新的在前）。
     *  @param groupid  群ID
     *  @param offset   分页起始下标，从 0 起
     *  @param count    本页最多返回几条；<= 0 时按 kAnnouncementPageMax 处理
     *  @param out      [出参] 公告列表，调用前不会被清空；publishername 留空，
     *                  由会话层补
     *  @param totalOut [出参] 本群公告总条数（不受 offset / count 限制），
     *                  供客户端画分页；传 NULL 表示不需要
     *  @return 数据库访问成功返回 true
     */
    bool getAnnouncements(int64_t groupid, int offset, int count,
                          std::vector<GroupAnnouncementRecord>& out, int* totalOut);

    /**
     *  取单条公告。删除与"重新发布"都要先按ID读回原件校验归属。
     *  @param id  公告ID
     *  @param out [出参] 公告记录；publishername 留空，由会话层补
     *  @return 公告存在返回 true
     */
    bool getAnnouncement(int64_t id, GroupAnnouncementRecord& out);

    /**
     *  在若干个群的群公告里按关键字搜索（cmd_global_search 用）：标题或正文含关键字的公告。
     *  @param groupids 要搜的群（本人所在的群）；为空时直接返回成功、结果为空
     *  @param keyword  关键字（UTF-8，非空）
     *  @param limit    最多返回几条，须大于 0
     *  @param out      [出参] 命中的公告，按发布时间倒序（同一时刻按 id 倒序）；publishername 留空
     *  @return 数据库访问成功返回 true
     */
    bool searchAnnouncements(const std::vector<int64_t>& groupids, const std::string& keyword, int limit,
                             std::vector<GroupAnnouncementRecord>& out);

    /**
     *  发布一条新公告：插入 t_group_announcement，并同步刷新 t_group 上的快照四列。
     *  "重新发布"走的也是本方法 —— 调用方先按ID读回旧公告，再把它的标题 / 正文 /
     *  优先级原样传进来，于是历史里留下的是两条记录而不是一条被改过时间的。
     *  @param groupid     群ID
     *  @param title       标题，非空；长度由会话层校验
     *  @param content     正文，可空
     *  @param severity    优先级，取值见 group_announcement_severity
     *  @param publisher   发布人userid
     *  @param publishTime 发布时间，Unix毫秒
     *  @return 成功返回新公告ID；失败返回 0
     */
    int64_t publishAnnouncement(int64_t groupid, const std::string& title,
                                const std::string& content, int severity,
                                int64_t publisher, int64_t publishTime);

    /**
     *  删除一条公告：把该行真删掉，并同步刷新 t_group 上的快照四列（删掉的正好是
     *  最新那条时，快照回填为剩下的最新一条）。不可恢复。
     *  @param groupid 群ID；与 id 一起作为条件，防止跨群删掉别的群的公告
     *  @param id      公告ID
     *  @return 数据库访问成功返回 true（公告本就不存在也算成功，即幂等）
     */
    bool deleteAnnouncement(int64_t groupid, int64_t id);

    /**
     *  把 t_group 上的公告快照四列刷新成该群当前最新一条公告；一条都没有时清空
     *  （id 与时间置 0、标题与正文置空串）。
     *
     *  发布与删除内部都会调它，正常流程下调用方不需要单独调用；单列为 public 是
     *  为了在数据修复等场景下能手工对齐一次。
     *  @param groupid 群ID
     *  @return 数据库访问成功返回 true
     */
    bool refreshLatestAnnouncement(int64_t groupid);

private:
    std::atomic<int64_t> m_baseGroupId{ 0 };  //群ID分配器，取已有最大群ID，新建群在此基础上递增
};
