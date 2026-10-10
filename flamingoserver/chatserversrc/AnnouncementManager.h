/**
 *  平台公告管理器，AnnouncementManager.h
 *  balloonwj@qq.com   2026.05.21
 *
 *  面向 admin 控制台的「平台公告」模块：管理 t_announcement（公告主表）、
 *  t_announcement_target（投放目标）、t_announcement_read（已读回执）。
 *  公告是平台级运营能力，仅平台超级管理员可操作；按请求直查数据库，不做缓存。
 *  设计参照 EnterpriseManager / EmployeeManager。
 */
#ifndef __ANNOUNCEMENT_MANAGER_H__
#define __ANNOUNCEMENT_MANAGER_H__

#include <stdint.h>
#include <string>
#include <vector>
#include <mutex>
#include <atomic>

//投递给 IM 客户端的公告条目 AnnouncementPushItem 定义在此（投递侧的数据结构，
//与本文件下方管理侧的 AnnouncementInfo 是两个不同用途的结构，别混用）。
#include "AnnouncementProtocol.h"

//定时公告调度器的扫描周期，单位：微秒（EventLoop 定时器以微秒计时）。
//取 60 秒：定时发布是分钟级精度的运营动作，每分钟扫一次足够及时，
//且单次扫描只是一条带索引的 UPDATE，频率低、开销可忽略。
#define ANNOUNCEMENT_SCHEDULER_INTERVAL_US   (60LL * 1000 * 1000)

//前向声明：数据库连接类，仅在 .cpp 中需要其完整定义。
class IDatabase;

//一个公告投放目标（t_announcement_target 的一行）。
struct AnnouncementTarget
{
    AnnouncementTarget() : type(0), id(0) {}

    int32_t type;  //目标类型：1=企业 2=部门 3=员工
    int64_t id;    //目标ID（企业ID/部门ID/用户ID）
};

//公告信息（list 取摘要、detail 取全量，公用此结构）。
struct AnnouncementInfo
{
    AnnouncementInfo()
        : announcementId(0), severity(0), senderAdminId(0), audienceType(0)
        , pinned(0), scheduled(0), status(0), readCount(0), totalCount(0)
    {
    }

    int64_t     announcementId;  //公告业务ID
    std::string title;           //标题
    std::string body;            //正文（仅 detail 填充）
    int32_t     severity;        //级别：0=普通 1=提醒 2=警告
    int64_t     senderAdminId;   //发布者管理员ID
    std::string senderName;      //发布者名称
    int32_t     audienceType;    //受众类型：0=全部企业 1=指定企业 2=指定部门 3=指定员工 4=平台内部
    int32_t     pinned;          //是否置顶：0=否 1=是
    int32_t     scheduled;       //是否定时发布：0=否 1=是
    std::string scheduledTime;   //定时发布时间
    std::string sentTime;        //实际发布时间
    int32_t     status;          //状态：0=草稿 1=已发布 2=已撤回
    std::string createTime;      //创建时间
    std::string updateTime;      //更新时间
    int32_t     readCount;       //已读人数（list/detail 填充）
    int32_t     totalCount;      //受众总人数（仅 detail 填充）
    std::vector<AnnouncementTarget> targets;  //投放目标（仅 detail 填充）
};

//新建/编辑公告请求（save.do 的输入载体）。
struct SaveAnnouncementReq
{
    SaveAnnouncementReq() : announcementId(0), severity(0), audienceType(0), pinned(0) {}

    int64_t     announcementId;  //公告ID；0表示新建，非0表示编辑既有公告
    std::string title;           //标题
    std::string body;            //正文
    int32_t     severity;        //级别：0=普通 1=提醒 2=警告
    int32_t     audienceType;    //受众类型：0=全部企业 1=指定企业 2=指定部门 3=指定员工 4=平台内部
    int32_t     pinned;          //是否置顶：0=否 1=是
    std::vector<AnnouncementTarget> targets;  //投放目标（audienceType 为 1/2/3 时有值）
};

//平台公告管理器（服务器级单例，经 Singleton<AnnouncementManager> 使用）。
class AnnouncementManager final
{
public:
    AnnouncementManager();
    ~AnnouncementManager();

    AnnouncementManager(const AnnouncementManager& rhs) = delete;
    AnnouncementManager& operator=(const AnnouncementManager& rhs) = delete;

    /**
     *  初始化：据库中最大 f_announcement_id 确定ID分配基数。
     *  @return true 表示初始化成功。
     */
    bool init();

    /**
     *  分页查询公告列表（按置顶优先、创建时间倒序）。
     *  @param status   状态过滤：传 -1 表示不过滤，否则只取该状态。
     *  @param page     页码，从 1 起。
     *  @param pageSize 每页条数。
     *  @param outTotal 输出满足条件的总条数。
     *  @param outList  输出当前页的公告摘要（不含正文/目标，含已读人数）。
     */
    void listAnnouncements(int32_t status, int32_t page, int32_t pageSize,
                           int32_t& outTotal, std::vector<AnnouncementInfo>& outList);

    /**
     *  取公告详情，含正文、投放目标、已读人数与受众总人数。
     *  @param announcementId 公告ID。
     *  @param outInfo        输出的详情，仅在返回 true 时有效。
     *  @return true 表示公告存在；false 表示不存在。
     */
    bool getAnnouncement(int64_t announcementId, AnnouncementInfo& outInfo);

    /**
     *  新建或编辑公告（含其投放目标，整体替换目标集）。
     *  @param req           公告内容；announcementId 为 0 表示新建。
     *  @param senderAdminId 操作管理员ID（新建时记为发布者）。
     *  @param senderName    操作管理员姓名。
     *  @param outId         输出公告ID（新建时为新分配的ID）。
     *  @return 错误码，0 表示成功，取值见 AdminApiCodes.h 的 enum error_code。
     */
    int32_t saveAnnouncement(const SaveAnnouncementReq& req, int64_t senderAdminId,
                             const std::string& senderName, int64_t& outId);

    /**
     *  发布公告：立即发布或登记定时发布。
     *  立即发布把状态置为「已发布」并记发布时间；定时发布仅登记定时信息、状态仍为草稿
     *  （实际到点自动发布需后台调度器，当前未实现）。
     *  @param announcementId 公告ID。
     *  @param scheduled      true=定时发布 false=立即发布。
     *  @param scheduledTime  定时发布时间（scheduled 为 true 时有效）。
     *  @return 错误码，0 表示成功。
     */
    int32_t sendAnnouncement(int64_t announcementId, bool scheduled, const std::string& scheduledTime);

    //物理删除公告及其投放目标、已读回执。返回错误码。
    int32_t deleteAnnouncement(int64_t announcementId);

    //撤回已发布的公告（状态 1→2）；公告非「已发布」状态时返回错误码。
    int32_t recallAnnouncement(int64_t announcementId);

    //切换公告置顶状态：pinned 0=取消置顶 1=置顶。返回错误码。
    int32_t setPinned(int64_t announcementId, int32_t pinned);

    /**
     *  扫描并发布所有到期的定时公告：把「已登记定时发布(f_scheduled=1)、仍为草稿、
     *  且 f_scheduled_time 已到」的公告状态置为「已发布」并记录发布时间。
     *  供 chatserver 的后台定时器按 ANNOUNCEMENT_SCHEDULER_INTERVAL_US 周期调用。
     *  @param outPublishedIds [出参] 本次实际转为「已发布」的公告ID列表，供调用方据此
     *                         向在线用户推送 cmd_announcement_notify；传 NULL 表示不需要。
     *  @return 本次发布的公告条数。
     */
    int32_t publishDueScheduled(std::vector<int64_t>* outPublishedIds = NULL);

    // ================================================================
    // 投递侧：把已发布的公告送进 IM 客户端的「公告中心」
    //
    // 上面那些方法都是<u>管理控制台</u>视角——按状态分页列出全部公告、改内容与状态。
    // 下面三个是 <u>IM 普通用户</u>视角：他能看到哪些公告、他读了哪一条、以及一条新
    // 公告该推给哪些在线用户。
    //
    // 两组不要混用，尤其注意数据结构不同：管理侧用 AnnouncementInfo（带草稿状态、受众
    // 类型、投放目标、已读人数等运营字段），投递侧用 AnnouncementPushItem（只装普通用户
    // 该看到的内容）。详见 docs/公告中心接入方案.md。
    // ================================================================

    /**
     *  取发给某个 IM 用户的平台公告，即「公告中心」列表的数据源。
     *
     *  只返回<u>已发布</u>（f_status=1）且<u>受众命中</u>该用户的公告；草稿、已撤回、
     *  以及受众类型为「平台内部」的公告一律不返回——最后这条尤其重要，平台内部公告是
     *  给运营看的，漏投给普通用户是事故。
     *
     *  结果按「置顶优先、再按发布时间倒序」排列，与公告中心列表的展示顺序一致。
     *
     *    userid      目标用户的 userid。
     *    sinceId     增量游标：只返回公告ID大于它的；传 0 表示全量拉取。
     *    limit       本次最多返回多少条；调用方应已用 AnnouncementProtocol 规整过。
     *    outList     [出参] 公告列表，先清空再填充。
     *    outHasMore  [出参] 是否还有更多——命中条数超出 limit 时为 true，客户端据此
     *                用最后一条的ID作新的 sinceId 继续拉。
     */
    void getAnnouncementsForUser(int32_t userid, int64_t sinceId, int32_t limit,
                                 std::vector<AnnouncementPushItem>& outList, bool& outHasMore);

    /**
     *  记录某用户已读某条平台公告，即往 t_announcement_read 写一行。
     *
     *  重复上报是安全的：该表有 (f_announcement_id, f_user_id) 唯一键，写入走
     *  INSERT IGNORE，第二次起静默跳过、不算失败。管理控制台的「已读进度」正是
     *  统计这张表得来的。
     *
     *    announcementId  公告ID。
     *    userid          上报者 userid。
     *  @return 错误码，0 表示成功；公告不存在或非「已发布」状态时返回 error_code_unknown
     *          （不写库——避免异常客户端往表里灌无效回执，污染已读统计）。
     */
    int32_t markAnnouncementRead(int64_t announcementId, int32_t userid);

    /**
     *  从一批候选用户里筛出某条公告的受众，供发布时决定推给谁。
     *
     *  之所以设计成「给一批候选、筛出命中的」而不是「列出该公告的全部受众」：推送只关心
     *  当前<u>在线</u>的用户，而在线人数远小于全库用户数。受众为「全部企业」时后者会把
     *  整张 t_user 拉进内存，前者不会。离线用户不需要在这里操心——他们下次登录时客户端
     *  会主动发 cmd_get_announcements 拉一遍。
     *
     *    announcementId  公告ID。
     *    candidates      候选 userid 列表（一般是当前全部在线用户）；为空时直接返回。
     *    outHitUserIds   [出参] 候选中命中该公告受众的 userid，先清空再填充。
     */
    void filterAudienceAmong(int64_t announcementId, const std::vector<int32_t>& candidates,
                             std::vector<int32_t>& outHitUserIds);

    /**
     *  与 filterAudienceAmong 相同，但只保留还没读过该公告的人（t_announcement_read 里没有
     *  该用户的回执），供管理控制台「重新推送」决定推给谁。
     *
     *    announcementId  公告ID；公告不是「已发布」时筛不到任何人。
     *    candidates      候选 userid 列表（一般是当前全部在线用户）；为空时直接返回。
     *    outHitUserIds   [出参] 候选中命中受众且未读的 userid，先清空再填充。
     */
    void filterUnreadAudienceAmong(int64_t announcementId, const std::vector<int32_t>& candidates,
                                   std::vector<int32_t>& outHitUserIds);

    /**
     *  检查一条公告能否「重新推送」：只有已发布的公告可以。
     *    announcementId  公告ID。
     *  @return error_code_ok 可以；error_code_announcement_notpublished 是草稿或已撤回；
     *          error_code_unknown 公告不存在、ID 非法或查库失败。
     */
    int32_t checkRepushable(int64_t announcementId);

private:
    //filterAudienceAmong 与 filterUnreadAudienceAmong 的共同实现：一条 SQL 从候选里筛出受众，
    //unreadOnly 为 true 时再排除已有已读回执的人。参数含义同两者。
    void filterAudienceImpl(int64_t announcementId, const std::vector<int32_t>& candidates,
                            bool unreadOnly, std::vector<int32_t>& outHitUserIds);

    //创建一个已 initialize 的数据库连接；失败返回 NULL，成功时所有权归调用方。
    IDatabase* createDbConnection();

    //执行一条返回单个计数值的 SQL（形如 SELECT COUNT(*) ...）；查询失败返回 0。
    int32_t queryCount(IDatabase* pConn, const char* sql);

    //把某公告的投放目标整体重写为 targets（先删后插）。调用方须保证公告已存在。
    bool replaceTargets(IDatabase* pConn, int64_t announcementId,
                        const std::vector<AnnouncementTarget>& targets);

    /**
     *  按受众类型与目标计算公告的受众总人数。
     *  @param pConn        数据库连接。
     *  @param audienceType 受众类型。
     *  @param targets      投放目标集。
     *  @return 受众覆盖的IM用户总数；平台内部（类型4）返回 0。
     */
    int32_t computeAudienceTotal(IDatabase* pConn, int32_t audienceType,
                                 const std::vector<AnnouncementTarget>& targets);

private:
    std::mutex           m_mutex;             //串行化新建/编辑/状态变更等写操作
    std::atomic<int64_t> m_baseAnnouncementId;//公告ID分配基数，取库中最大 f_announcement_id
};

#endif // __ANNOUNCEMENT_MANAGER_H__
