/**
 *  flamingoAdmin —— 内存态数据层 DataStore。
 *
 *  DataStore 是全应用唯一的数据来源（单例）：构造时从 Mock/MockData 的种子
 *  数据装载全部企业、组织树、服务号、公告与审计日志，并对外暴露读取访问器
 *  与变更 API。所有界面只通过 DataStore 读写数据，界面层不内嵌数据。
 *
 *  设计目标——数据层与 UI 解耦：未来接入真实 chatserver 时，只需替换本类的
 *  实现（改为走网络协议），界面层代码零改动。
 *
 *  线程模型：flamingoAdmin 是单线程 UI 程序，DataStore 只在 UI 线程访问，
 *  故内部不加锁。
 *
 *  balloonwj@qq.com   2026.05.21
 */
#pragma once

#include "Mock/MockData.h"

namespace flamingoadmin {

class DataStore
{
public:
    // 取全局唯一实例。首次调用时从种子数据完成构建。
    static DataStore& Instance();

    // ---- 企业 ----------------------------------------------------------

    // 返回全部企业列表（最新创建的在前）。
    const std::vector<Enterprise>& Enterprises() const { return m_enterprises; }

    // 按企业 ID 查找企业。
    //   id：企业 ID，如 "ent-001"。
    //   返回：命中返回指向内部记录的指针；未命中返回 nullptr。指针在下一次
    //         CreateEnterprise 之前有效。
    const Enterprise* FindEnterprise(const CString& id) const;

    // 新增一家企业。会把企业插到列表最前、为其建一棵只含根部门的空组织树、
    // 初始化空服务号列表，并写一条 enterprise.create 审计日志。
    //   e：待新增的企业记录；调用方需保证 m_id 唯一。
    void CreateEnterprise(const Enterprise& e);

    // 用从服务端拉取的真实企业列表整体覆盖企业缓存。仪表盘加载时由
    // AdminFrame 调用，使「活跃企业」卡片显示真实数据。
    //   list：企业记录列表。
    void SetEnterprises(const std::vector<Enterprise>& list);

    // 用从服务端拉取的真实数据，整体覆盖某企业的「企业记录 + 组织树 +
    // 服务号列表」。企业详情屏打开前由 AdminFrame 调用，使详情屏的各控件
    // （仍读 DataStore）直接显示真实数据。
    //   e：企业记录（其 m_id 即企业 ID，用作组织树 / 服务号的键）。
    //   orgTree：该企业的组织树根节点。
    //   services：该企业的服务号列表。
    void InjectEnterpriseDetail(const Enterprise& e, const OrgNode& orgTree,
                                const std::vector<Service>& services);

    // 用从服务端重新拉取的组织树整体覆盖某企业的组织树。组织写操作（增删
    // 部门 / 员工）成功后，重拉 dept/tree.do 并据此刷新详情屏。
    //   enterpriseId：企业 ID；orgTree：新的组织树根节点。
    void SetOrgTree(const CString& enterpriseId, const OrgNode& orgTree);

    // 用从服务端重新拉取的服务号列表整体覆盖某企业的服务号。服务号写操作
    // 成功后，重拉 service/list.do 并据此刷新详情屏。
    //   enterpriseId：企业 ID；services：新的服务号列表。
    void SetServices(const CString& enterpriseId,
                     const std::vector<Service>& services);

    // ---- 组织架构 ------------------------------------------------------

    // 取某企业的组织树根节点（只读）。
    //   enterpriseId：企业 ID。
    //   返回：命中返回根（部门）节点指针；该企业无组织树时返回 nullptr。
    const OrgNode* OrgTree(const CString& enterpriseId) const;

    // 取某企业的组织树根节点（可写，供组织架构界面增删节点用）。
    //   enterpriseId：企业 ID。
    //   返回：命中返回根节点指针；无组织树时返回 nullptr。
    OrgNode* OrgTree(const CString& enterpriseId);

    // 在某企业组织树里按节点 ID 查找节点（可写）。
    //   注意：返回的指针在下一次组织树增删之前有效 —— 增删可能令子节点
    //   vector 重新分配，使旧指针失效；调用方增删后须重新查找。
    OrgNode* FindOrgNode(const CString& enterpriseId, const CString& nodeId);

    // 在 parentId 部门下新增一个子部门。会写一条 dept.create 审计日志。
    //   enterpriseId：企业 ID；parentId：父部门节点 ID（须为部门）；
    //   name：部门名（非空）；leader：负责人姓名（可空）。
    //   返回：成功返回新部门节点 ID；失败返回空串。
    CString AddDept(const CString& enterpriseId, const CString& parentId,
                    const CString& name, const CString& leader);

    // 在 parentId 部门下新增一个员工。入职日期取当天、状态取在职、工号
    //   自动生成。会写一条 user.create 审计日志。
    //   返回：成功返回新员工节点 ID；失败返回空串。
    CString AddUser(const CString& enterpriseId, const CString& parentId,
                    const CString& name, const CString& title,
                    const CString& email, const CString& phone,
                    const CString& gender);

    // 从某企业组织树删除一个节点（连同其子树）。根节点不可删。
    //   会写一条 node.delete 审计日志。
    //   返回：true 删除成功；false 未找到 / 是根节点。
    bool DeleteOrgNode(const CString& enterpriseId, const CString& nodeId);

    // 统计某企业组织树的员工数与部门数（部门数不含根节点）。
    //   enterpriseId：企业 ID；outUsers / outDepts：接收统计结果。
    void CountOrg(const CString& enterpriseId, int& outUsers, int& outDepts) const;

    // ---- 服务号 --------------------------------------------------------

    // 取某企业的服务号列表。
    //   enterpriseId：企业 ID。
    //   返回：该企业的服务号列表；无服务号时返回一个静态空表的引用。
    const std::vector<Service>& Services(const CString& enterpriseId) const;

    // 新增一个服务号（插到该企业服务号列表最前）。m_id / m_createdAt /
    // m_subs 由本方法自动填充，其余字段由调用方给出。会写一条
    // service.create 审计日志。返回新服务号 ID。
    CString AddService(const CString& enterpriseId, const Service& svc);

    // 设置某服务号的状态（启用 / 停用 / 草稿）。会写一条审计日志。
    void SetServiceStatus(const CString& enterpriseId, const CString& serviceId,
                          ServiceStatus status);

    // 删除某服务号。会写一条 service.delete 审计日志。
    //   返回：true 删除成功；false 未找到。
    bool DeleteService(const CString& enterpriseId, const CString& serviceId);

    // 按一组绑定计算去重后触达的员工数（企业 = 全员，部门 = 子树员工，
    // 员工 = 其本人）。
    int ComputeReach(const CString& enterpriseId,
                     const std::vector<ServiceBinding>& bindings) const;

    // 设置某服务号的绑定列表，并按新绑定重算覆盖人数 m_subs。会写一条
    // service.bind 审计日志。
    void SetServiceBindings(const CString& enterpriseId,
                            const CString& serviceId,
                            const std::vector<ServiceBinding>& bindings);

    // ---- 公告 ----------------------------------------------------------

    // 返回全部公告（最新在前）。
    const std::vector<Announcement>& Announcements() const { return m_announcements; }

    // 用从服务端拉取的真实公告列表整体覆盖公告缓存。公告中心加载时由
    // AdminFrame 调用。
    //   list：公告列表。
    void SetAnnouncements(const std::vector<Announcement>& list);

    // 用公告详情整体替换缓存中同 ID 的那条公告（detail.do 回来后补全正文 /
    // 受众总数等）。缓存中找不到同 ID 则不做改动。
    //   a：公告详情记录（m_id 为匹配键）。
    void UpdateAnnouncement(const Announcement& a);

    // 返回「近 14 天消息量」趋势（仪表盘柱状图用）。
    const std::vector<MessageTrendPoint>& MessageTrend() const
    {
        return m_messageTrend;
    }

    // 用 dashboard/messagetrend.do 的结果覆盖消息趋势缓存。
    //   trend：近 14 天每日消息量。
    void SetMessageTrend(const std::vector<MessageTrendPoint>& trend);

    // 发布一条公告。会把公告插到列表最前，并写一条 announcement.send /
    // announcement.schedule 审计日志（按 m_scheduled 区分）。
    //   a：待发布的公告记录。
    void SendAnnouncement(const Announcement& a);

    // 删除（撤回）一条公告。会写一条 announcement.delete 审计日志。
    //   announcementId：公告 ID。
    //   返回：true 删除成功；false 未找到。
    bool DeleteAnnouncement(const CString& announcementId);

    // ---- 审计 ----------------------------------------------------------

    // 返回全部审计日志（最新在前）。
    const std::vector<AuditLog>& AuditLogs() const { return m_auditLogs; }

    // 用从服务端拉取的真实审计日志整体覆盖审计缓存。审计界面加载时由
    // AdminFrame 调用。
    //   list：审计记录列表（服务端已按时间倒序）。
    void SetAuditLogs(const std::vector<AuditLog>& list);

    // 追加一条审计日志。id、时间戳、操作者、来源 IP 由本方法自动填充，
    // 调用方只需给出动作语义部分。
    //   action：动作代码，如 "user.create"。
    //   target：操作目标描述。
    //   severity：严重级别。
    //   detail：详情描述。
    void PushAudit(const CString& action, const CString& target,
                   Severity severity, const CString& detail);

private:
    // 单例：构造私有，禁止拷贝。
    DataStore();
    DataStore(const DataStore&) = delete;
    DataStore& operator=(const DataStore&) = delete;

    std::vector<Enterprise>                 m_enterprises;   // 全部企业（最新在前）
    std::map<CString, OrgNode>              m_orgTrees;      // 企业 ID → 组织树根
    std::map<CString, std::vector<Service>> m_services;      // 企业 ID → 服务号列表
    std::vector<Announcement>               m_announcements; // 全部公告（最新在前）
    std::vector<AuditLog>                   m_auditLogs;     // 全部审计日志（最新在前）
    std::vector<MessageTrendPoint>          m_messageTrend;  // 近 14 天消息量趋势
    int                                     m_nextAuditId;   // 下一条审计日志的自增 id
    int                                     m_nextNodeId;    // 下一个组织节点 id 的自增数字部分
};

} // namespace flamingoadmin
