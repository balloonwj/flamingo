/**
 *  flamingoAdmin —— 内存态数据层 DataStore 实现。
 *
 *  balloonwj@qq.com   2026.05.21
 */
#include "stdafx.h"
#include "Mock/DataStore.h"
#include "Util/Logger.h"
#include "Util/OrgTreeBuilder.h"   // kSyntheticRootId（组织树尚未从服务端拉回时的占位根）

#include <set>

namespace flamingoadmin {

namespace {

// PushAudit 自动填充字段用的 mock 常量。Phase 2 接入登录后，操作者与来源 IP
// 应改为实际登录会话的值。
const TCHAR* const kMockActor = _T("admin / 李正阳");  // 当前管理员（mock）
const TCHAR* const kMockIp    = _T("10.12.4.88");      // 来源 IP（mock）

} // anonymous namespace

DataStore& DataStore::Instance()
{
    // Meyers 单例：C++11 起，函数内 static 局部对象的初始化由编译器保证
    // 「恰好一次、并发安全」。
    static DataStore s_instance;
    return s_instance;
}

namespace {

// 在以 node 为根的子树里按 id 深度优先查找节点。命中返回指针，否则 nullptr。
OrgNode* FindNodeRecursive(OrgNode* node, const CString& id)
{
    if (node == nullptr)
    {
        return nullptr;
    }
    if (node->m_id == id)
    {
        return node;
    }
    if (node->m_kind == OrgNodeKind::Dept)
    {
        for (auto& child : node->m_children)
        {
            if (OrgNode* hit = FindNodeRecursive(&child, id))
            {
                return hit;
            }
        }
    }
    return nullptr;
}

// 在以 node 为根的子树里删除 id 节点（在其父的 m_children 中移除）。
//   返回：true 删除成功。
bool DeleteNodeRecursive(OrgNode* node, const CString& id)
{
    if (node == nullptr || node->m_kind != OrgNodeKind::Dept)
    {
        return false;
    }
    for (size_t i = 0; i < node->m_children.size(); ++i)
    {
        if (node->m_children[i].m_id == id)
        {
            node->m_children.erase(node->m_children.begin() + i);
            return true;
        }
    }
    for (auto& child : node->m_children)
    {
        if (DeleteNodeRecursive(&child, id))
        {
            return true;
        }
    }
    return false;
}

// 统计以 node 为根的子树里员工 / 部门数量（部门数不含 node 自身）。
void CountSubtree(const OrgNode& node, int& users, int& depts)
{
    if (node.m_kind == OrgNodeKind::User)
    {
        ++users;
        return;
    }
    for (const auto& child : node.m_children)
    {
        if (child.m_kind == OrgNodeKind::Dept)
        {
            ++depts;
        }
        CountSubtree(child, users, depts);
    }
}

// 把以 node 为根的子树里所有员工 ID 收集进 set。
void CollectUserIds(const OrgNode& node, std::set<CString>& out)
{
    if (node.m_kind == OrgNodeKind::User)
    {
        out.insert(node.m_id);
        return;
    }
    for (const auto& child : node.m_children)
    {
        CollectUserIds(child, out);
    }
}

// 在子树里按 id 查找节点（只读版）。
const OrgNode* FindNodeConst(const OrgNode* node, const CString& id)
{
    if (node == nullptr)
    {
        return nullptr;
    }
    if (node->m_id == id)
    {
        return node;
    }
    if (node->m_kind == OrgNodeKind::Dept)
    {
        for (const auto& c : node->m_children)
        {
            if (const OrgNode* hit = FindNodeConst(&c, id))
            {
                return hit;
            }
        }
    }
    return nullptr;
}

} // anonymous namespace

DataStore::DataStore()
    : m_nextAuditId(1)
    , m_nextNodeId(1000)   // 新建节点 id 从 1000 起，避开种子里的 d-xxx / u-xxx
{
    // 从种子数据装载全部内存态数据。
    m_enterprises   = BuildSeedEnterprises();
    m_orgTrees      = BuildSeedOrgTrees();
    m_services      = BuildSeedServices();
    m_announcements = BuildSeedAnnouncements();
    m_auditLogs     = BuildSeedAuditLogs();

    // 审计日志自增 id 游标取「种子里最大 id + 1」，避免新日志与种子冲突。
    for (const auto& log : m_auditLogs)
    {
        if (log.m_id >= m_nextAuditId)
        {
            m_nextAuditId = log.m_id + 1;
        }
    }

    FA_LOGI(_T("DataStore 初始化完成：企业 %d 家、公告 %d 条、审计 %d 条"),
            static_cast<int>(m_enterprises.size()),
            static_cast<int>(m_announcements.size()),
            static_cast<int>(m_auditLogs.size()));
}

const Enterprise* DataStore::FindEnterprise(const CString& id) const
{
    for (const auto& e : m_enterprises)
    {
        if (e.m_id == id)
        {
            return &e;
        }
    }
    return nullptr;
}

void DataStore::CreateEnterprise(const Enterprise& e)
{
    // 新企业插到列表最前（与设计稿 App.createEnterprise 行为一致）。
    m_enterprises.insert(m_enterprises.begin(), e);

    // 为新企业建一棵只含根部门的空组织树。真实数据从服务端拉回来时根节点会换成
    // 该企业的公司根部门，这里先用合成节点占位，同样标记为公司根（不可删除）。
    OrgNode root;
    root.m_kind          = OrgNodeKind::Dept;
    root.m_id            = CString(orgtree::kSyntheticRootId);
    root.m_name          = e.m_name;
    root.m_leader        = e.m_admin;
    root.m_isCompanyRoot = true;
    m_orgTrees[e.m_id] = root;

    // 新企业暂无服务号，先放一个空列表占位。
    m_services[e.m_id] = std::vector<Service>();

    PushAudit(_T("enterprise.create"),
              e.m_id + _T(" ") + e.m_shortName,
              Severity::Warn,
              CString(_T("新增企业，初始管理员 ")) + e.m_contact);

    FA_LOGI(_T("创建企业 %s（%s）"),
            e.m_id.GetString(), e.m_shortName.GetString());
}

void DataStore::SetEnterprises(const std::vector<Enterprise>& list)
{
    m_enterprises = list;
}

void DataStore::SetAuditLogs(const std::vector<AuditLog>& list)
{
    m_auditLogs = list;
}

void DataStore::SetAnnouncements(const std::vector<Announcement>& list)
{
    m_announcements = list;
}

void DataStore::UpdateAnnouncement(const Announcement& a)
{
    for (size_t i = 0; i < m_announcements.size(); ++i)
    {
        if (m_announcements[i].m_id == a.m_id)
        {
            m_announcements[i] = a;
            return;
        }
    }
}

void DataStore::SetMessageTrend(const std::vector<MessageTrendPoint>& trend)
{
    m_messageTrend = trend;
}

void DataStore::InjectEnterpriseDetail(const Enterprise& e,
                                       const OrgNode& orgTree,
                                       const std::vector<Service>& services)
{
    // 企业记录：已存在则替换，不存在则插到列表最前。
    bool found = false;
    for (size_t i = 0; i < m_enterprises.size(); ++i)
    {
        if (m_enterprises[i].m_id == e.m_id)
        {
            m_enterprises[i] = e;
            found = true;
            break;
        }
    }
    if (!found)
    {
        m_enterprises.insert(m_enterprises.begin(), e);
    }

    // 组织树与服务号列表：用真实数据整体覆盖。
    m_orgTrees[e.m_id] = orgTree;
    m_services[e.m_id] = services;

    FA_LOGI(_T("注入企业详情数据：%s"), e.m_id.GetString());
}

void DataStore::SetOrgTree(const CString& enterpriseId, const OrgNode& orgTree)
{
    m_orgTrees[enterpriseId] = orgTree;
    FA_LOGI(_T("刷新企业组织树：%s"), enterpriseId.GetString());
}

void DataStore::SetServices(const CString& enterpriseId,
                            const std::vector<Service>& services)
{
    m_services[enterpriseId] = services;
    FA_LOGI(_T("刷新企业服务号：%s"), enterpriseId.GetString());
}

const OrgNode* DataStore::OrgTree(const CString& enterpriseId) const
{
    auto it = m_orgTrees.find(enterpriseId);
    return it != m_orgTrees.end() ? &it->second : nullptr;
}

OrgNode* DataStore::OrgTree(const CString& enterpriseId)
{
    auto it = m_orgTrees.find(enterpriseId);
    return it != m_orgTrees.end() ? &it->second : nullptr;
}

OrgNode* DataStore::FindOrgNode(const CString& enterpriseId, const CString& nodeId)
{
    return FindNodeRecursive(OrgTree(enterpriseId), nodeId);
}

CString DataStore::AddDept(const CString& enterpriseId, const CString& parentId,
                           const CString& name, const CString& leader)
{
    OrgNode* parent = FindOrgNode(enterpriseId, parentId);
    if (parent == nullptr || parent->m_kind != OrgNodeKind::Dept || name.IsEmpty())
    {
        FA_LOGW(_T("AddDept 失败：父部门无效或部门名为空"));
        return CString();
    }

    OrgNode dept;
    dept.m_kind   = OrgNodeKind::Dept;
    dept.m_id.Format(_T("d-%d"), m_nextNodeId++);
    dept.m_name   = name;
    dept.m_leader = leader.IsEmpty() ? CString(_T("—")) : leader;
    // 新部门插到子节点最前（与设计稿 addDept 的 unshift 行为一致）。
    parent->m_children.insert(parent->m_children.begin(), dept);

    PushAudit(_T("dept.create"), parent->m_name + _T(" / ") + name,
              Severity::Info, CString(_T("新增部门 ")) + name);
    FA_LOGI(_T("新增部门 %s（%s）于 %s"),
            name.GetString(), dept.m_id.GetString(), parent->m_name.GetString());
    return dept.m_id;
}

CString DataStore::AddUser(const CString& enterpriseId, const CString& parentId,
                           const CString& name, const CString& title,
                           const CString& email, const CString& phone,
                           const CString& gender)
{
    OrgNode* parent = FindOrgNode(enterpriseId, parentId);
    if (parent == nullptr || parent->m_kind != OrgNodeKind::Dept || name.IsEmpty())
    {
        FA_LOGW(_T("AddUser 失败：父部门无效或姓名为空"));
        return CString();
    }

    OrgNode user;
    user.m_kind       = OrgNodeKind::User;
    user.m_id.Format(_T("u-%d"), m_nextNodeId++);
    user.m_name       = name;
    user.m_title      = title;
    user.m_email      = email;
    user.m_phone      = phone;
    user.m_gender     = gender.IsEmpty() ? CString(_T("—")) : gender;
    user.m_userStatus = UserStatus::Active;

    // 入职日期取当天。
    SYSTEMTIME st = {};
    ::GetLocalTime(&st);
    user.m_joined.Format(_T("%04d-%02d-%02d"), st.wYear, st.wMonth, st.wDay);

    // 工号自动生成（M + 4 位数字，数字部分取自节点 id 自增值）。
    user.m_employeeNo.Format(_T("M%04d"), (m_nextNodeId % 9000) + 1000);

    // 新员工追加到子节点末尾（与设计稿 addUser 的 push 行为一致）。
    parent->m_children.push_back(user);

    PushAudit(_T("user.create"), parent->m_name + _T(" / ") + name,
              Severity::Info,
              CString(_T("新增员工 ")) + name + _T(" 至 ") + parent->m_name);
    FA_LOGI(_T("新增员工 %s（%s）于 %s"),
            name.GetString(), user.m_id.GetString(), parent->m_name.GetString());
    return user.m_id;
}

bool DataStore::DeleteOrgNode(const CString& enterpriseId, const CString& nodeId)
{
    OrgNode* root = OrgTree(enterpriseId);
    if (root == nullptr)
    {
        return false;
    }
    //代表公司本身的根节点不允许删除。这里比对的是树根自身的 ID，而不是某个固定的
    //字符串：企业完成迁移后根节点是一个真实部门，它的 ID 不再是历史上的合成值。
    if (nodeId == root->m_id)
    {
        FA_LOGW(_T("DeleteOrgNode 拒绝：根节点不可删除"));
        return false;
    }
    // 先取节点名用于审计，再删除。
    OrgNode* target = FindNodeRecursive(root, nodeId);
    const CString nodeName = (target != nullptr) ? target->m_name : nodeId;

    if (!DeleteNodeRecursive(root, nodeId))
    {
        FA_LOGW(_T("DeleteOrgNode 失败：未找到节点 %s"), nodeId.GetString());
        return false;
    }
    PushAudit(_T("node.delete"), nodeName, Severity::Danger,
              CString(_T("删除组织节点 ")) + nodeName);
    FA_LOGI(_T("删除组织节点 %s（%s）"), nodeName.GetString(), nodeId.GetString());
    return true;
}

void DataStore::CountOrg(const CString& enterpriseId,
                         int& outUsers, int& outDepts) const
{
    outUsers = 0;
    outDepts = 0;
    const OrgNode* root = OrgTree(enterpriseId);
    if (root != nullptr)
    {
        CountSubtree(*root, outUsers, outDepts);
    }
}

const std::vector<Service>& DataStore::Services(const CString& enterpriseId) const
{
    auto it = m_services.find(enterpriseId);
    if (it != m_services.end())
    {
        return it->second;
    }
    // 该企业无服务号：返回一个静态空表，避免返回悬空引用。
    static const std::vector<Service> s_empty;
    return s_empty;
}

int DataStore::ComputeReach(const CString& enterpriseId,
                            const std::vector<ServiceBinding>& bindings) const
{
    const OrgNode* root = OrgTree(enterpriseId);
    std::set<CString> ids;
    for (const auto& b : bindings)
    {
        if (b.m_kind == BindingKind::Enterprise)
        {
            if (root != nullptr)
            {
                CollectUserIds(*root, ids);
            }
        }
        else if (b.m_kind == BindingKind::Dept)
        {
            const OrgNode* d = FindNodeConst(root, b.m_id);
            if (d != nullptr)
            {
                CollectUserIds(*d, ids);
            }
        }
        else  // BindingKind::User
        {
            ids.insert(b.m_id);
        }
    }
    return static_cast<int>(ids.size());
}

CString DataStore::AddService(const CString& enterpriseId, const Service& svc)
{
    Service s = svc;
    s.m_id.Format(_T("svc-%d"), m_nextNodeId++);

    SYSTEMTIME st = {};
    ::GetLocalTime(&st);
    s.m_createdAt.Format(_T("%04d-%02d-%02d"), st.wYear, st.wMonth, st.wDay);
    s.m_subs = ComputeReach(enterpriseId, s.m_bindings);

    // 插到该企业服务号列表最前。
    m_services[enterpriseId].insert(m_services[enterpriseId].begin(), s);

    PushAudit(_T("service.create"), s.m_name, Severity::Info,
              CString(_T("新增服务号 ")) + s.m_name);
    FA_LOGI(_T("新增服务号 %s（%s）"),
            s.m_name.GetString(), s.m_id.GetString());
    return s.m_id;
}

void DataStore::SetServiceStatus(const CString& enterpriseId,
                                 const CString& serviceId, ServiceStatus status)
{
    auto it = m_services.find(enterpriseId);
    if (it == m_services.end())
    {
        return;
    }
    for (auto& s : it->second)
    {
        if (s.m_id == serviceId)
        {
            s.m_status = status;
            const bool disabled = (status == ServiceStatus::Disabled);
            PushAudit(_T("service.update"), s.m_name, Severity::Warn,
                      CString(_T("服务号状态变更：")) + s.m_name);
            FA_LOGI(_T("服务号 %s 状态变更（disabled=%d）"),
                    s.m_name.GetString(), disabled ? 1 : 0);
            return;
        }
    }
}

bool DataStore::DeleteService(const CString& enterpriseId,
                              const CString& serviceId)
{
    auto it = m_services.find(enterpriseId);
    if (it == m_services.end())
    {
        return false;
    }
    for (size_t i = 0; i < it->second.size(); ++i)
    {
        if (it->second[i].m_id == serviceId)
        {
            const CString name = it->second[i].m_name;
            it->second.erase(it->second.begin() + i);
            PushAudit(_T("service.delete"), name, Severity::Danger,
                      CString(_T("删除服务号 ")) + name);
            FA_LOGI(_T("删除服务号 %s"), name.GetString());
            return true;
        }
    }
    return false;
}

void DataStore::SetServiceBindings(const CString& enterpriseId,
                                   const CString& serviceId,
                                   const std::vector<ServiceBinding>& bindings)
{
    auto it = m_services.find(enterpriseId);
    if (it == m_services.end())
    {
        return;
    }
    for (auto& s : it->second)
    {
        if (s.m_id == serviceId)
        {
            s.m_bindings = bindings;
            s.m_subs     = ComputeReach(enterpriseId, bindings);
            CString detail;
            detail.Format(_T("修改服务号「%s」绑定范围，覆盖 %d 名员工"),
                          static_cast<LPCTSTR>(s.m_name), s.m_subs);
            PushAudit(_T("service.bind"), s.m_name, Severity::Warn, detail);
            FA_LOGI(_T("服务号 %s 绑定变更，覆盖 %d 人"),
                    s.m_name.GetString(), s.m_subs);
            return;
        }
    }
}

void DataStore::SendAnnouncement(const Announcement& a)
{
    // 公告列表最新在前。
    m_announcements.insert(m_announcements.begin(), a);

    // 定时公告记 announcement.schedule，立即发布记 announcement.send。
    const CString detailPrefix = a.m_scheduled
        ? CString(_T("计划发布 \""))
        : CString(_T("发布 \""));
    PushAudit(a.m_scheduled ? _T("announcement.schedule") : _T("announcement.send"),
              a.m_audience,
              Severity::Info,
              detailPrefix + a.m_title + _T("\""));

    FA_LOGI(_T("发布公告 \"%s\"（受众 %s，定时=%d）"),
            a.m_title.GetString(), a.m_audience.GetString(),
            a.m_scheduled ? 1 : 0);
}

bool DataStore::DeleteAnnouncement(const CString& announcementId)
{
    for (size_t i = 0; i < m_announcements.size(); ++i)
    {
        if (m_announcements[i].m_id == announcementId)
        {
            const CString title = m_announcements[i].m_title;
            m_announcements.erase(m_announcements.begin() + i);
            PushAudit(_T("announcement.delete"), title, Severity::Warn,
                      CString(_T("撤回公告 ")) + title);
            FA_LOGI(_T("删除公告 %s"), title.GetString());
            return true;
        }
    }
    return false;
}

void DataStore::PushAudit(const CString& action, const CString& target,
                          Severity severity, const CString& detail)
{
    AuditLog log;
    log.m_id = m_nextAuditId++;

    // 时间戳取当前本地时间，格式与种子数据一致："YYYY-MM-DD HH:MM:SS"。
    SYSTEMTIME st = {};
    ::GetLocalTime(&st);
    log.m_ts.Format(_T("%04d-%02d-%02d %02d:%02d:%02d"),
                    st.wYear, st.wMonth, st.wDay,
                    st.wHour, st.wMinute, st.wSecond);

    log.m_actor    = kMockActor;
    log.m_ip       = kMockIp;
    log.m_action   = action;
    log.m_target   = target;
    log.m_severity = severity;
    log.m_detail   = detail;

    // 审计列表最新在前。
    m_auditLogs.insert(m_auditLogs.begin(), log);

    FA_LOGI(_T("审计[%d]：%s | %s | %s"),
            log.m_id, action.GetString(), target.GetString(), detail.GetString());
}

} // namespace flamingoadmin
