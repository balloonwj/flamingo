/**
 *  文件共享的校验规则（服务端侧，纯函数），FileSharePolicy.h
 *
 *  会话层处理 2201 发起、2202 取消、2204 调整成员、2210 在线提供报告时，先把需要的事实查好
 *  （会话里有哪些人、每人属于哪个企业、分享者有几个有效共享、调用者在群里的角色……），
 *  再交给这里判定能不能做、回什么错误码。规则集中在一处，便于单元测试（tests/test_filesharepolicy.cpp，
 *  Suite SHP），也避免几个处理函数各写一份、彼此不一致。
 *
 *  本模块不触及数据库、网络与日志。
 *
 *  典型用法（会话层处理 cmd_share_create）：
 *      ShareCheckFacts facts;
 *      ...（查好 facts）
 *      std::vector<int64_t> invalid;
 *      int code = FileSharePolicy::checkCreate(req, myUserId, facts, invalid);
 *      if (code != error_code_ok)
 *          ...（回应答，821 / 819 时带上 invalid）
 *
 *  balloonwj@qq.com   2026-10-02
 */
#pragma once

#include <stdint.h>
#include <map>
#include <set>
#include <string>
#include <vector>
#include "FileShareProtocol.h"
#include "FileShareManager.h"

//校验所需、由会话层事先查好的事实
struct ShareCheckFacts
{
    int64_t                    m_ownerEnterpriseId;   //分享者所属企业 id；0 表示不属于任何企业
    std::set<int64_t>          m_conversationUsers;   //共享所在会话里的人：群聊为全体群成员，单聊为对端一人
    std::map<int64_t, int64_t> m_enterpriseOf;        //请求里涉及的每名成员所属企业 id；查不到的人按 0 处理
    int                        m_activeShareCount;    //分享者当前的有效共享数（只有发起共享时用到）
    int                        m_maxSharesPerOwner;   //每人最多同时持有的共享数 sharemaxpershareowner（只有发起共享时用到）
    bool                       m_featureEnabled;      //分享者所属企业的文件共享开关（第 3 期起由 FeatureSwitchManager 给出）

    ShareCheckFacts()
        : m_ownerEnterpriseId(0)
        , m_activeShareCount(0)
        , m_maxSharesPerOwner(0)
        , m_featureEnabled(true)
    {
    }
};

//计算「某个共享对某个人」的状态时，由会话层查好的、与看的人和分享者在线情况有关的事实
struct ShareViewerFacts
{
    int64_t m_viewerId;            //看的人 userid
    bool    m_isMember;            //看的人是否在 t_file_share_member 里（分享者本人不在其中）
    int     m_perms;               //看的人是成员时的写权限位；不是成员时为 0
    bool    m_inConversation;      //看的人是否仍在共享所在的群里；单聊共享不看这一项
    bool    m_featureEnabled;      //分享者所属企业的文件共享开关（第 3 期起由 FeatureSwitchManager 按企业给出）
    bool    m_ownerOnline;         //分享者是否有在线连接（会话层扫描在线会话得出）
    bool    m_ownerOnBoundDevice;  //分享者是否在共享绑定的那台电脑上在线（按登录时上报的 deviceid 比较）
    bool    m_hasProvideState;     //在线提供状态表里是否有这个共享（分享者从绑定电脑报告过且连接未断）
    bool    m_provideAvailable;    //有在线提供记录时，共享根能否提供

    ShareViewerFacts()
        : m_viewerId(0)
        , m_isMember(false)
        , m_perms(0)
        , m_inConversation(true)
        , m_featureEnabled(true)
        , m_ownerOnline(false)
        , m_ownerOnBoundDevice(false)
        , m_hasProvideState(false)
        , m_provideAvailable(false)
    {
    }
};

//打开共享与申请传输时再核对一次的企业归属（防止成员中途被调到别的企业，方案 5.1「企业校验」）
struct ShareEnterprisePair
{
    int64_t m_ownerEnterpriseId;    //分享者此刻所属企业 id；0 表示不属于任何企业
    int64_t m_viewerEnterpriseId;   //访问者此刻所属企业 id；0 表示不属于任何企业

    ShareEnterprisePair()
        : m_ownerEnterpriseId(0)
        , m_viewerEnterpriseId(0)
    {
    }
};

//某人离开群（退群或被踢）时，文件共享要做的处理
struct ShareLeavePlan
{
    std::vector<int64_t> m_cancelShareIds;       //他作为分享者的有效共享，要取消（原因 share_invalid_owner_left_group），升序
    std::vector<int64_t> m_removeFromShareIds;   //他作为成员的有效共享，要把他移出，升序
};

//启动清扫时要取消的一个共享
struct ShareCancelAction
{
    int64_t m_shareId;   //共享 id
    int     m_reason;    //取消原因，取值见 share_invalid_reason

    ShareCancelAction()
        : m_shareId(0)
        , m_reason(0)
    {
    }
};

//chatserver 启动时一致性清扫的处理计划（只涉及群共享）
struct ShareSweepPlan
{
    std::vector<ShareCancelAction>          m_cancels;         //要取消的共享，按 shareid 升序
    std::map<int64_t, std::vector<int64_t> > m_removeMembers;  //共享 id → 已不在群里、要移出的成员（升序）
};

/**
 *  FileSharePolicy —— 文件共享各项操作的判定规则；全部为静态方法，不可实例化。
 */
class FileSharePolicy
{
public:
    //审计表路径列（t_file_share_audit.f_path，varchar(1024)）写入时的字节数上限。按 UTF-8 字节计，
    //留出余量后不会超出列的字符数上限
    static const size_t kAuditPathMaxBytes = 1000;

    /**
     *  判定能否发起共享（cmd_share_create）。请求已经过 FileShareProtocol::parseCreateRequest，
     *  格式、单聊成员恰好是对端、单文件共享权限清零都已处理。判定顺序：本企业的文件共享已关闭（800）→
     *  分享者不属于任何企业（821）→
     *  有成员不在会话里或就是分享者本人（819）→ 有成员与分享者不在同一个企业（821）→ 有效共享数已满（815）。
     *  @param req            发起请求
     *  @param ownerId        分享者 userid（当前登录用户）
     *  @param facts          事先查好的事实
     *  @param invalidMembers [出参] 回 819 或因成员企业不符回 821 时，列出不满足条件的成员（按 userid 升序）；其余情况为空
     *  @return error_code_ok 或上述错误码
     */
    static int checkCreate(const ShareCreateRequest& req, int64_t ownerId, const ShareCheckFacts& facts,
                           std::vector<int64_t>& invalidMembers);

    /**
     *  判定能否取消共享（cmd_share_cancel），并给出取消原因。
     *  @param share     共享记录
     *  @param actorId   发起取消的用户 userid
     *  @param groupRole 发起者在共享所在群里的角色（group_role_*）；单聊、或发起者不在群里时传 -1
     *  @param reason    [出参] 允许时的取消原因：分享者本人为 share_invalid_owner_canceled，
     *                   群主或管理员为 share_invalid_group_admin_canceled
     *  @return error_code_ok；共享已取消为 error_code_share_canceled；无权取消为 error_code_permission_denied
     */
    static int checkCancel(const ShareRecord& share, int64_t actorId, int groupRole, int& reason);

    /**
     *  判定能否调整成员（cmd_share_update_members）。单文件共享时会把 req 里追加与改权限的写权限清零。
     *  只有新增的成员要求在会话里、与分享者同属一个企业；移除与改权限不查这两项（已离群的残留成员也要能清理）。
     *  @param share          共享记录
     *  @param actorId        发起调整的用户 userid
     *  @param req            [入出参] 调整请求，已经过 FileShareProtocol::parseUpdateMembersRequest
     *  @param facts          事先查好的事实（m_activeShareCount 与 m_maxSharesPerOwner 不用）
     *  @param invalidMembers [出参] 回 819 或 821 时列出不满足条件的新成员（按 userid 升序）；其余情况为空
     *  @return error_code_ok；本企业的文件共享已关闭为 error_code_share_disabled；共享已取消为 error_code_share_canceled；
     *          不是分享者为 error_code_permission_denied；单聊共享要移除对方为 error_code_bad_request（要停止共享请用 cmd_share_cancel）；
     *          新成员不在会话里或是分享者本人为 error_code_share_invalid_member；分享者不属于任何企业、
     *          或新成员与分享者不在同一个企业为 error_code_share_cross_enterprise
     */
    static int checkUpdateMembers(const ShareRecord& share, int64_t actorId, ShareUpdateMembersRequest& req,
                                  const ShareCheckFacts& facts, std::vector<int64_t>& invalidMembers);

    /**
     *  判定是否接受一条在线提供报告（cmd_share_serve_report 里的一项）。只接受分享者本人、
     *  从共享绑定的那台电脑发来的报告，其余一律忽略（会话层记日志）。
     *  @param share            共享记录
     *  @param reporterId       报告方 userid
     *  @param reporterDeviceId 报告方这条连接登录时上报的设备标识
     *  @return 共享有效、报告方是分享者、设备标识非空且与共享绑定的设备一致时返回 true
     */
    static bool acceptServeReport(const ShareRecord& share, int64_t reporterId, const std::string& reporterDeviceId);

    /**
     *  计算一张共享卡片的可见范围：给定的成员全体（按 userid 升序、去重），不含分享者本人。
     *  分享者是卡片的发送者，按消息可见范围的规则本来就看得到。
     *  @param members 成员（发起时为全体成员，追加时只传新成员）
     *  @param ownerId 分享者 userid
     *  @return 可见范围内的 userid
     */
    static std::vector<int64_t> cardAudience(const std::vector<ShareMemberSpec>& members, int64_t ownerId);

    /**
     *  计算一张共享卡片的实时推送名单：可见范围加分享者本人（按 userid 升序、去重），
     *  分享者的其他在线设备也要收到这张卡片。
     *  @param audience 可见范围（cardAudience 的结果）
     *  @param ownerId  分享者 userid
     *  @return 推送名单
     */
    static std::vector<int64_t> pushTargets(const std::vector<int64_t>& audience, int64_t ownerId);

    /**
     *  由共享记录与看的人的事实组装 FileShareProtocol::computeShareState 的输入。两处换算：
     *  一是有在线提供记录就说明分享者正在绑定的电脑上在线（记录随连接断开而清除），即便会话扫描没有找到；
     *  二是群共享里，成员表里还有他、但他已不在群里时按「不是成员」处理（退群后的成员清理尚未完成）。
     *  @param share 共享记录
     *  @param facts 看的人的事实
     *  @return 状态输入
     */
    static ShareStateInput buildStateInput(const ShareRecord& share, const ShareViewerFacts& facts);

    /**
     *  计算某个共享对某个人的状态（buildStateInput 加 FileShareProtocol::computeShareState）。
     *  @param share 共享记录
     *  @param facts 看的人的事实
     *  @return 状态与原因，用于 2203 推送与 2205 列表
     */
    static ShareStateView viewFor(const ShareRecord& share, const ShareViewerFacts& facts);

    /**
     *  把某人看到的状态与权限合并进共享卡片正文（同步、实时推送时调用）。看的人是分享者本人、或已不是
     *  有效成员时不写 myperms。真实路径（displaypath）只有看的人是分享者本人时才写入，其余的人一律去掉。
     *  @param content 卡片正文（入库的那份）
     *  @param share   共享记录
     *  @param facts   看的人的事实
     *  @return 合并后的正文；正文解析不了时原样返回
     */
    static std::string augmentCardFor(const std::string& content, const ShareRecord& share,
                                      const ShareViewerFacts& facts);

    /**
     *  判定能否打开共享（cmd_share_open）。判定顺序：分享者本人（回 3，本机直接打开，不经中转）→
     *  已取消（802）→ 已不是有效成员（803）→ 访问者与分享者不在同一企业（821）→ 功能已关闭（800）→
     *  暂不可用（804）。
     *  @param share      共享记录
     *  @param facts      访问者的事实（loadShareViewerFacts 查得）
     *  @param enterprise 分享者与访问者此刻的企业归属
     *  @return error_code_ok 或上述错误码
     */
    static int checkOpen(const ShareRecord& share, const ShareViewerFacts& facts, const ShareEnterprisePair& enterprise);

    /**
     *  判定能否申请一次传输（cmd_share_request_transfer）。先按 checkOpen 判共享状态，再判方向：
     *  下载一律允许；上传要求共享不是单文件（否则 820）且访问者有上传权限（否则 806）。
     *  @param share      共享记录
     *  @param facts      访问者的事实
     *  @param enterprise 分享者与访问者此刻的企业归属
     *  @param direction  传输方向，取值见 share_transfer_direction（调用方已校验过取值）
     *  @return error_code_ok 或错误码
     */
    static int checkTransfer(const ShareRecord& share, const ShareViewerFacts& facts,
                             const ShareEnterprisePair& enterprise, int direction);

    /**
     *  构造一个会话（访问会话本身或其名下的传输子会话）推给两端的中转参数（cmd_share_relay_info）。
     *  令牌经 sessions.signTokens 签发，中转 sessionid 就是会话 id；访问会话只有控制通道（lanes 为 0），
     *  传输子会话的数据通道条数取配置。
     *  @param sessions    会话管理器（取配置、签令牌），只读
     *  @param access      访问会话快照
     *  @param transferId  传输子会话 id；为空表示构造访问会话本身的中转参数
     *  @param perms       访问者此刻的写权限位（只写进给分享者的那一帧）
     *  @param visitorName 访问者显示名（只写进给分享者的那一帧）
     *  @param nowMs       当前时间（Unix 毫秒），令牌有效期从此起算
     *  @param direction   传输方向（share_transfer_direction），访问会话本身传 0
     *  @param toOwner     [出参] 推给分享者提供该共享的那条连接
     *  @param toVisitor   [出参] 推给访问者打开共享的那条连接
     */
    static void makeRelayInfos(const FileShareSessionManager& sessions, const ShareAccessInfo& access,
                               const std::string& transferId, int direction, int perms, const std::string& visitorName,
                               int64_t nowMs, ShareRelayInfo& toOwner, ShareRelayInfo& toVisitor);

    /**
     *  判定是否接受审计上报（cmd_share_audit_report）里的一条记录：只接受该共享分享者本人的上报。
     *  共享已取消时照样接受，执行发生在取消之前，上报只是晚到。
     *  @param share      该条记录 shareid 对应的共享记录
     *  @param reporterId 上报者 userid
     *  @return 上报者是分享者本人时返回 true
     */
    static bool acceptAuditItem(const ShareRecord& share, int64_t reporterId);

    /**
     *  把分享者上报的一条执行结果转成审计记录：来源记为分享者客户端上报，企业与分享者取自共享记录，
     *  时间用服务端时间（客户端时间只作参考，不入库）。
     *  @param share 共享记录
     *  @param item  上报的一条执行结果
     *  @param nowMs 服务端当前时间（Unix 毫秒）
     *  @return 审计记录
     */
    static ShareAuditRecord buildClientAuditRecord(const ShareRecord& share, const ShareAuditItem& item, int64_t nowMs);

    /**
     *  构造申请传输时服务端自己记的审计：下载记 share_audit_request_download、上传记 share_audit_request_upload；
     *  路径一栏只有一个条目时写它的路径，有多个时写首个条目的路径加「等 N 项」，超过 kAuditPathMaxBytes
     *  时按 UTF-8 字符边界截断首个条目的路径；字节数取 totalbytes。
     *  @param share     共享记录
     *  @param visitorId 申请者 userid
     *  @param req       申请内容（已经过 parseTransferRequest）
     *  @param nowMs     服务端当前时间（Unix 毫秒）
     *  @return 审计记录
     */
    static ShareAuditRecord buildTransferAuditRecord(const ShareRecord& share, int64_t visitorId,
                                                     const ShareTransferRequest& req, int64_t nowMs);

    /**
     *  计算某人离开群（退群或被踢）时对该群文件共享的处理：他作为分享者的有效共享全部取消，
     *  他作为成员的有效共享把他移出。已取消的共享与别的会话里的共享不受影响。
     *  @param shares    候选共享（通常是该群的有效共享，混入别的也会被筛掉）
     *  @param membersOf 共享 id → 成员；缺项按没有成员处理
     *  @param groupId   离开的群
     *  @param userId    离开的人
     *  @return 处理计划
     */
    static ShareLeavePlan planUserLeftGroup(const std::vector<ShareRecord>& shares,
                                            const std::map<int64_t, std::vector<ShareMemberSpec> >& membersOf,
                                            int64_t groupId, int64_t userId);

    /**
     *  计算 chatserver 启动时的一致性清扫：群退出、踢人、解散时的联动处理是在群操作成功之后另起的，
     *  中途失败会留下不一致，启动时统一补齐。只看有效的群共享：群已不存在的取消（原因
     *  share_invalid_group_dismissed）；分享者已不在群里的取消（原因 share_invalid_owner_left_group）；
     *  其余共享里已不在群里的成员移出。单聊共享与已取消的共享不参与。
     *  @param shares         候选共享
     *  @param membersOf      共享 id → 成员
     *  @param groupMembersOf 群 id → 当前群成员；不在表里的群视为已不存在
     *  @return 处理计划
     */
    static ShareSweepPlan planGroupSweep(const std::vector<ShareRecord>& shares,
                                         const std::map<int64_t, std::vector<ShareMemberSpec> >& membersOf,
                                         const std::map<int64_t, std::set<int64_t> >& groupMembersOf);

private:
    FileSharePolicy() = delete;
    ~FileSharePolicy() = delete;
    FileSharePolicy(const FileSharePolicy&) = delete;
    FileSharePolicy& operator=(const FileSharePolicy&) = delete;
};
