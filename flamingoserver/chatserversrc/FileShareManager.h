/**
 *  文件共享的共享记录、成员与审计的存储（服务端侧），FileShareManager.h
 *
 *  负责三张表的读写（方案见 docs/文件共享方案.md 6.3）：
 *    - t_file_share：一行 = 一个共享，只在被主动取消时把 f_state 置 2，记录本身不删；
 *    - t_file_share_member：一行 = 一名成员及其写权限，被移出的成员直接删行；
 *    - t_file_share_audit：一行 = 一次被记录的动作（服务端亲眼所见的，或分享者客户端上报的）。
 *  与 MessageManager 同一风格：每次操作单独建立一条数据库连接，不在内存缓存共享记录；
 *  需要多条语句一起成败的（发起共享、调整成员）放在一个事务里。
 *  另外维护一张只在内存里的「在线提供状态」表（共享 id → 能否提供、原因、提供它的连接），
 *  由分享者从绑定电脑发来的 cmd_share_serve_report 建立，连接断开或共享取消时清除。
 *
 *  校验（成员是否在会话里、是否同属一个企业、名额是否已满、调用者有没有权限）不在本类，由会话层
 *  先做完再调用这里；本类只保证写进去的数据自身一致。
 *
 *  典型用法（会话层处理 cmd_share_create）：
 *      FileShareManager& mgr = Singleton<FileShareManager>::Instance();
 *      int64_t shareId = 0;
 *      bool created = false;
 *      if (mgr.createShare(record, members, shareId, created) == error_code_ok && created)
 *          ...（生成卡片、推送）
 *
 *  单元测试见 tests/test_filesharemanager.cpp（Suite SHM）。
 *
 *  balloonwj@qq.com   2026-10-02
 */
#pragma once

#include <stdint.h>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <vector>
#include "FileShareProtocol.h"         //ShareMemberSpec、ShareUpdateMembersRequest
#include "FileShareSessionManager.h"   //ShareConnKey

//t_file_share 的一行
struct ShareRecord
{
    int64_t     m_shareId;        //共享 id（自增主键）；createShare 时忽略，由数据库生成
    std::string m_clientShareId;  //客户端生成的 UUID，与分享者一起用于重发去重
    int64_t     m_ownerId;        //分享者 userid
    int64_t     m_enterpriseId;   //分享者所属企业 id（冗余一份，供后台按企业查询）
    std::string m_deviceId;       //发起时分享者的设备标识；共享只由这台电脑提供
    int         m_ctype;          //所在会话类型：1 单聊 2 群聊
    int64_t     m_cid;            //所在会话 id：单聊为对端 userid（分享者视角），群聊为群 id
    int         m_kind;           //共享类型，取值见 share_kind
    std::string m_name;           //显示名
    std::string m_displayPath;    //分享者发起时的显示路径，仅供显示
    bool        m_includeHidden;  //是否包含隐藏文件与系统文件
    int         m_state;          //1 有效 2 已取消，取值见 kShareRecordActive / kShareRecordCanceled
    int         m_cancelReason;   //取消原因，取值见 share_invalid_reason（1~5）；未取消为 0
    int64_t     m_canceledBy;     //取消操作人：后台强制取消时为管理员 id，其余为用户 userid；未取消为 0
    int64_t     m_createTime;     //发起时间（Unix 毫秒）
    int64_t     m_cancelTime;     //取消时间（Unix 毫秒）；未取消为 0

    ShareRecord()
        : m_shareId(0)
        , m_ownerId(0)
        , m_enterpriseId(0)
        , m_ctype(0)
        , m_cid(0)
        , m_kind(0)
        , m_includeHidden(false)
        , m_state(0)
        , m_cancelReason(0)
        , m_canceledBy(0)
        , m_createTime(0)
        , m_cancelTime(0)
    {
    }
};

//一次调整成员实际产生的变化；请求里与现状相同的项（追加已在的人、移除不在的人、权限没变）不计入
struct ShareMemberChanges
{
    std::vector<ShareMemberSpec> m_added;     //新加入的成员及其权限
    std::vector<int64_t>         m_removed;   //被移除的成员 userid
    std::vector<ShareMemberSpec> m_updated;   //权限确有变化的成员及其新权限（含「追加」了一个已在的人且权限不同的情况）
};

//t_file_share_audit 的一行
struct ShareAuditRecord
{
    int64_t     m_id;            //自增主键；addAudit 时忽略
    int64_t     m_shareId;       //共享 id
    int64_t     m_ownerId;       //分享者 userid
    int64_t     m_actorId;       //操作人：访问者、分享者的 userid，或后台管理员 id
    int64_t     m_enterpriseId;  //分享者所属企业 id
    int         m_action;        //动作，取值见 share_audit_action
    int         m_source;        //来源，取值见 share_audit_source
    std::string m_path;          //相对共享根的路径；申请传输时为条目清单的摘要
    std::string m_toPath;        //重命名后的新名字或移动的目标目录；其余动作为空
    int64_t     m_size;          //涉及的字节数；没有字节数的动作为 0
    int         m_result;        //0 成功，否则为错误码
    std::string m_accessId;      //所属访问会话 id；与访问会话无关的动作为空
    int64_t     m_createTime;    //记录时间（Unix 毫秒，服务端时间）

    ShareAuditRecord()
        : m_id(0)
        , m_shareId(0)
        , m_ownerId(0)
        , m_actorId(0)
        , m_enterpriseId(0)
        , m_action(0)
        , m_source(0)
        , m_size(0)
        , m_result(0)
        , m_createTime(0)
    {
    }
};

//后台「文件共享」页查共享的条件（/admin/fileshare/list.do，docs/文件共享方案.md 6.5）
struct ShareAdminQuery
{
    int64_t     m_enterpriseId;  //只看这个企业的共享（按分享者所属企业）；0 表示不限（平台管理员）
    std::string m_keyword;       //匹配分享者的账号或昵称（包含即可）；空串表示不限
    int         m_state;         //0 不限；kShareRecordActive 只看有效的；kShareRecordCanceled 只看已取消的
    int         m_offset;        //跳过的条数（>= 0）
    int         m_limit;         //最多返回的条数（> 0）

    ShareAdminQuery()
        : m_enterpriseId(0)
        , m_state(0)
        , m_offset(0)
        , m_limit(0)
    {
    }
};

//后台列表里的一个共享：共享记录加上显示用的名字与成员数
struct ShareAdminView
{
    ShareRecord m_share;             //共享记录
    std::string m_ownerName;         //分享者昵称
    std::string m_ownerAccount;      //分享者账号
    std::string m_conversationName;  //所在会话：群聊为群名，单聊为对方昵称；查不到时为空
    int         m_memberCount;       //当前成员数

    ShareAdminView()
        : m_memberCount(0)
    {
    }
};

//后台详情里的一名成员
struct ShareMemberView
{
    int64_t     m_userId;    //成员 userid
    int         m_perms;     //写权限位
    std::string m_name;      //昵称
    std::string m_account;   //账号

    ShareMemberView()
        : m_userId(0)
        , m_perms(0)
    {
    }
};

//后台「共享审计」页查审计的条件（/admin/fileshare/audit.do）
struct ShareAuditQuery
{
    int64_t     m_enterpriseId;  //只看这个企业的记录；0 表示不限（平台管理员）
    std::string m_actorKeyword;  //匹配操作人的账号或昵称（后台强制取消时匹配管理员姓名）；空串表示不限
    int         m_action;        //动作（share_audit_action）；0 表示不限
    int64_t     m_startMs;       //起始时间（含，Unix 毫秒）；0 表示不限
    int64_t     m_endMs;         //结束时间（含，Unix 毫秒）；0 表示不限
    int64_t     m_shareId;       //只看这个共享；0 表示不限
    int         m_offset;        //跳过的条数（>= 0）
    int         m_limit;         //最多返回的条数（> 0）

    ShareAuditQuery()
        : m_enterpriseId(0)
        , m_action(0)
        , m_startMs(0)
        , m_endMs(0)
        , m_shareId(0)
        , m_offset(0)
        , m_limit(0)
    {
    }
};

//后台审计列表里的一条：审计记录加上显示用的名字
struct ShareAuditView
{
    ShareAuditRecord m_record;     //审计记录
    std::string      m_actorName;  //操作人：用户昵称；后台管理员强制取消时为管理员姓名
    bool             m_actorIsAdmin; //操作人是不是后台管理员（m_actorId 此时是管理员 id）
    std::string      m_ownerName;  //分享者昵称
    std::string      m_shareName;  //共享的显示名

    ShareAuditView()
        : m_actorIsAdmin(false)
    {
    }
};

//一个共享的在线提供状态：分享者从绑定的那台电脑报告（cmd_share_serve_report）后才有，
//那条连接断开或共享被取消时清除。只存在于内存
struct ShareProvideState
{
    bool         m_available;   //共享根能否提供
    int          m_reason;      //不能提供时的原因，取值见 share_unavailable_reason；能提供时为 0
    ShareConnKey m_conn;        //提供该共享的那条 chatserver 连接

    ShareProvideState()
        : m_available(false)
        , m_reason(0)
        , m_conn(0)
    {
    }
};

//启动一致性清扫的处理计划，定义在 FileSharePolicy.h；此处仅按引用传递（两个头文件互不包含）
struct ShareSweepPlan;

//一个因提供端连接断开而变为暂不可用的共享，以及要通知的人（全体成员加分享者，按 userid 升序）
struct ShareOfflineNotice
{
    ShareRecord          m_share;        //共享记录
    std::vector<int64_t> m_notifyUsers;  //要推 cmd_share_state_notify 的人
};

/**
 *  FileShareManager —— 共享记录、成员与审计的数据库读写，以及在线提供状态的内存表
 *  （进程内唯一，经 Singleton 访问）。
 *  全部方法可在任意线程调用：数据库方法每次调用各自建立连接；在线提供状态表由一把互斥量保护。
 */
class FileShareManager
{
public:
    //t_file_share.f_state：共享有效
    static const int kShareRecordActive = 1;
    //t_file_share.f_state：共享已被取消
    static const int kShareRecordCanceled = 2;
    //每人同时持有的有效共享数上限的默认值（配置项 sharemaxpershareowner，方案 6.4）
    static const int kDefaultMaxSharesPerOwner = 20;

    FileShareManager();
    ~FileShareManager();

    FileShareManager(const FileShareManager&) = delete;
    FileShareManager& operator=(const FileShareManager&) = delete;

    /**
     *  设置每人同时持有的有效共享数上限（main.cpp 读 sharemaxpershareowner 后调用一次）。
     *  @param maxSharesPerOwner 上限，单位：个；小于 1 时取 kDefaultMaxSharesPerOwner
     */
    void setMaxSharesPerOwner(int maxSharesPerOwner);

    /**
     *  取每人同时持有的有效共享数上限。
     *  @return 上限，单位：个
     */
    int getMaxSharesPerOwner() const;

    /**
     *  发起共享：在一个事务里写入共享记录与全部成员。同一分享者用同一个 clientShareId 重发时，
     *  不新建记录，直接返回第一次建出的 shareid（成员以第一次为准）。
     *  @param record  共享记录；m_shareId、m_state 与取消相关的字段被忽略，新记录一律为有效
     *  @param members 成员及其权限，调用方已去重并校验过
     *  @param shareId [出参] 新建或已有的共享 id
     *  @param created [出参] 本次是否新建；重发命中已有记录时为 false，调用方据此决定是否再发卡片
     *  @return error_code_ok；数据库失败为 error_code_server_internal（事务整体回滚）
     */
    int createShare(const ShareRecord& record, const std::vector<ShareMemberSpec>& members,
                    int64_t& shareId, bool& created);

    /**
     *  按「分享者 + clientShareId」查已建出的共享。会话层处理 2201 时先调它：重发的请求直接回已有的
     *  shareid，不再做名额等校验（否则第一次已建成、名额恰好被它占满时，重发会被误判为名额已满）。
     *  @param ownerId       分享者 userid
     *  @param clientShareId 客户端生成的 UUID
     *  @return 已有共享的 shareid；没有或数据库失败返回 0
     */
    int64_t findShareIdByClientShareId(int64_t ownerId, const std::string& clientShareId);

    /**
     *  按 shareid 取共享记录（含已取消的）。
     *  @param shareId 共享 id
     *  @param out     [出参] 共享记录
     *  @return 记录存在返回 true；不存在或数据库失败返回 false
     */
    bool getShare(int64_t shareId, ShareRecord& out);

    /**
     *  取某个共享的全部成员及其权限，按 userid 升序。
     *  @param shareId 共享 id
     *  @param out     [出参] 成员列表；调用前清空
     *  @return 数据库访问成功返回 true（没有成员也算成功）
     */
    bool getMembers(int64_t shareId, std::vector<ShareMemberSpec>& out);

    /**
     *  查某人是不是某个共享的成员，以及他的权限。
     *  @param shareId 共享 id
     *  @param userId  用户 userid
     *  @param perms   [出参] 是成员时为他的写权限位；不是成员时为 0
     *  @return 是成员返回 true；不是成员或数据库失败返回 false
     */
    bool getMemberPerms(int64_t shareId, int64_t userId, int& perms);

    /**
     *  统计某个分享者当前的有效共享数（已取消的不算），用于 sharemaxpershareowner 名额判断。
     *  @param ownerId 分享者 userid
     *  @return 有效共享数；数据库失败返回 -1
     */
    int countActiveShares(int64_t ownerId);

    /**
     *  列出某人作为分享者的有效共享（「我的共享」），按发起时间倒序。
     *  @param ownerId 分享者 userid
     *  @param ctype   0 表示不限会话；否则只列这个会话里的共享，与 cid 一起给出
     *  @param cid     会话 id（请求者视角：单聊为对端 userid，群聊为群 id）；ctype 为 0 时忽略
     *  @param out     [出参] 共享记录；调用前清空
     *  @return 数据库访问成功返回 true
     */
    bool listOwnerShares(int64_t ownerId, int ctype, int64_t cid, std::vector<ShareRecord>& out);

    /**
     *  列出某人作为成员的有效共享（「共享给我的」），按发起时间倒序。
     *  @param userId 成员 userid
     *  @param ctype  0 表示不限会话；否则只列这个会话里的共享，与 cid 一起给出
     *  @param cid    会话 id（请求者视角：单聊为对端即分享者的 userid，群聊为群 id）；ctype 为 0 时忽略
     *  @param out    [出参] 共享记录；调用前清空
     *  @return 数据库访问成功返回 true
     */
    bool listMemberShares(int64_t userId, int ctype, int64_t cid, std::vector<ShareRecord>& out);

    /**
     *  列出某个会话里的全部有效共享，按 shareid 升序（解散群、有人离开群时用）。
     *  @param ctype 会话类型
     *  @param cid   会话 id（分享者视角：单聊为对端 userid，群聊为群 id）
     *  @param out   [出参] 共享记录；调用前清空
     *  @return 数据库访问成功返回 true
     */
    bool listActiveConversationShares(int ctype, int64_t cid, std::vector<ShareRecord>& out);

    /**
     *  列出某个企业的全部有效共享（按分享者所属企业，即 f_enterprise_id），按 shareid 升序。
     *  企业的文件共享开关变化时用来结束访问、推送新状态。
     *  @param enterpriseId 企业 id
     *  @param out          [出参] 共享记录；调用前清空
     *  @return 数据库访问成功返回 true
     */
    bool listActiveEnterpriseShares(int64_t enterpriseId, std::vector<ShareRecord>& out);

    /**
     *  列出有效群共享涉及的全部群 id（启动清扫前用来确定要读哪些群）。
     *  @param out [出参] 群 id；调用前清空
     *  @return 数据库访问成功返回 true
     */
    bool listActiveShareGroupIds(std::set<int64_t>& out);

    /**
     *  读出若干群是否存在及其当前成员（启动清扫用）。直接查 t_group / t_group_member，而不是借用
     *  GroupManager::getGroup：后者在「群不存在」与「查询出错」时都返回 false，无法区分，借用它会让一次
     *  数据库故障被误判为群已解散，进而把共享批量取消，而取消不可恢复。
     *  @param groupIds 要读的群
     *  @param out      [出参] 存在的群 → 其当前成员；不存在的群不出现在结果里。调用前清空
     *  @return 全部查询成功返回 true；任何一条出错返回 false，调用方应放弃本次清扫
     */
    bool loadGroupMembership(const std::set<int64_t>& groupIds, std::map<int64_t, std::set<int64_t> >& out);

    /**
     *  chatserver 启动时的一致性清扫：读出全部有效的群共享与其成员，按 FileSharePolicy::planGroupSweep
     *  算出计划并落库（取消的操作人记 0，表示系统）。只改数据库，不推送（启动时没有人在线），也不清
     *  在线提供状态（启动时为空）。
     *  @param groupMembersOf 群 id → 当前群成员；须包含有效群共享涉及的每一个仍存在的群，不在表里的群视为已不存在
     *  @param nowMs          当前时间（Unix 毫秒），作为取消时间
     *  @param applied        [出参] 实际执行的计划
     *  @return 数据库访问全部成功返回 true；中途失败返回 false（已执行的部分保留，下次启动会继续补齐）
     */
    bool sweepGroupShares(const std::map<int64_t, std::set<int64_t> >& groupMembersOf, int64_t nowMs,
                          ShareSweepPlan& applied);

    /**
     *  把共享置为已取消，记下原因、操作人与时间。成员与审计记录不动。
     *  @param shareId    共享 id
     *  @param reason     取消原因，取值见 share_invalid_reason（1~5）
     *  @param canceledBy 操作人：用户 userid 或后台管理员 id
     *  @param nowMs      当前时间（Unix 毫秒）
     *  @return error_code_ok；共享不存在为 error_code_share_not_exist；已经取消过为
     *          error_code_share_canceled（第一次的记录保持不变）；数据库失败为 error_code_server_internal
     */
    int cancelShare(int64_t shareId, int reason, int64_t canceledBy, int64_t nowMs);

    /**
     *  调整成员：在一个事务里追加、移除、修改权限。追加一个已在的人按修改权限处理；移除一个不在的人、
     *  修改一个不在的人的权限都忽略。同一个人同时出现在追加和移除里时以移除为准。
     *  @param req     调整内容（m_shareId 为目标共享）；调用方已校验过调用者身份与新成员资格
     *  @param nowMs   当前时间（Unix 毫秒），作为新成员的加入时间
     *  @param changes [出参] 实际产生的变化，供调用方发卡片、推送 2203 / 2206 与记审计
     *  @return error_code_ok；数据库失败为 error_code_server_internal（事务整体回滚，changes 为空）
     */
    int updateMembers(const ShareUpdateMembersRequest& req, int64_t nowMs, ShareMemberChanges& changes);

    /**
     *  写入一条审计记录。
     *  @param rec 审计记录；m_id 被忽略
     *  @return 写入成功返回 true
     */
    bool addAudit(const ShareAuditRecord& rec);

    /**
     *  在一条连接、一个事务里写入一批审计记录（分享者经 2230 每批上报至多 200 条，逐条提交时
     *  每条都要单独落盘，一批要几秒，而调用方在网络线程上）。
     *  @param recs 审计记录；各条的 m_id 被忽略；为空时直接返回 true
     *  @return 全部写入成功返回 true；任一条失败时整批回滚并返回 false
     */
    bool addAudits(const std::vector<ShareAuditRecord>& recs);

    /**
     *  按 shareid 读出全部审计记录，按 f_id 升序（供单元测试与后台查询）。
     *  @param shareId 共享 id
     *  @param out     [出参] 审计记录；调用前清空
     *  @return 数据库访问成功返回 true
     */
    bool getAudits(int64_t shareId, std::vector<ShareAuditRecord>& out);

    //==== 后台查询（第 3 期第 2 步）====

    /**
     *  后台按条件分页查共享，按 shareid 倒序（新发起的在前）。
     *  @param q     查询条件（m_limit 须大于 0）
     *  @param out   [出参] 这一页的共享；调用前清空
     *  @param total [出参] 满足条件的总条数
     *  @return 数据库访问成功返回 true；条件非法（m_limit <= 0 或 m_offset < 0）返回 false
     */
    bool queryAdminShares(const ShareAdminQuery& q, std::vector<ShareAdminView>& out, int64_t& total);

    /**
     *  后台读一个共享的成员与各自的权限，按 userid 升序。
     *  @param shareId 共享 id
     *  @param out     [出参] 成员；调用前清空
     *  @return 数据库访问成功返回 true
     */
    bool getMemberViews(int64_t shareId, std::vector<ShareMemberView>& out);

    /**
     *  后台按条件分页查审计，按时间倒序（同一时间按 f_id 倒序）。
     *  @param q     查询条件（m_limit 须大于 0）
     *  @param out   [出参] 这一页的记录；调用前清空
     *  @param total [出参] 满足条件的总条数
     *  @return 数据库访问成功返回 true；条件非法返回 false
     */
    bool queryAdminAudits(const ShareAuditQuery& q, std::vector<ShareAuditView>& out, int64_t& total);

    //==== 在线提供状态（内存）====

    /**
     *  记下一个共享的在线提供状态（已被接受的 cmd_share_serve_report），覆盖旧值。
     *  @param shareId   共享 id
     *  @param conn      报告方那条 chatserver 连接
     *  @param available 共享根能否提供
     *  @param reason    不能提供时的原因（share_unavailable_reason）；能提供时传 0
     */
    void setProvideState(int64_t shareId, ShareConnKey conn, bool available, int reason);

    /**
     *  取一个共享的在线提供状态。
     *  @param shareId 共享 id
     *  @param out     [出参] 状态
     *  @return 有记录返回 true；分享者没有从绑定的电脑报告过（或已断开、已取消）返回 false
     */
    bool getProvideState(int64_t shareId, ShareProvideState& out) const;

    /**
     *  清除由某条连接提供的全部共享的在线提供状态（该连接断开或退出登录时调用）。
     *  @param conn 连接标识
     *  @return 被清除的共享 id（无序），调用方据此向各共享的成员推「暂不可用」
     */
    std::vector<int64_t> clearProvideStatesForConnection(ShareConnKey conn);

    /**
     *  提供端连接断开或退出登录时的收尾：清除这条连接提供的全部共享的在线提供状态，并读出这些共享
     *  的记录与要通知的人。已取消或已不存在的共享只清状态、不返回。
     *  @param conn 连接标识
     *  @return 要通知的共享，按 shareid 升序；调用方逐个向 m_notifyUsers 推送新状态
     */
    std::vector<ShareOfflineNotice> takeOfflineNotices(ShareConnKey conn);

    /**
     *  清除一个共享的在线提供状态（共享被取消时调用）。没有记录时什么也不做。
     *  @param shareId 共享 id
     */
    void clearProvideState(int64_t shareId);

private:
    int                                  m_maxSharesPerOwner; //每人有效共享数上限；启动阶段设置一次，之后只读
    mutable std::mutex                   m_provideMutex;      //保护 m_provideStates
    std::map<int64_t, ShareProvideState> m_provideStates;     //共享 id → 在线提供状态
};
