/**
 *  flamingoAdmin —— Mock 数据层单元测试。
 *
 *  覆盖 19 条用例（见 docs/UI设计计划.md 与计划确认记录），分 6 组：
 *  种子完整性、DataStore 构造、FindEnterprise、CreateEnterprise、
 *  OrgTree / Services、SendAnnouncement / PushAudit。
 *
 *  DataStore 是单例：变更类用例采用「变更前后计数差值」断言，不依赖执行
 *  顺序；种子构建函数 BuildSeed* 是纯函数、天然隔离。RunAllDataStoreTests
 *  把只读用例排在变更用例之前，保证构造用例看到的是纯种子状态。
 *
 *  balloonwj@qq.com   2026.05.21
 */
#include "stdafx.h"
#include "Tests/TestFramework.h"
#include "Mock/MockData.h"
#include "Mock/DataStore.h"

namespace flamingoadmin { namespace test {

namespace {

// 递归统计组织树里的部门数与员工数。
//   node：当前节点。
//   deptCount / userCount：累加输出。
void CountOrgNodes(const OrgNode& node, int& deptCount, int& userCount)
{
    if (node.m_kind == OrgNodeKind::Dept)
    {
        ++deptCount;
    }
    else
    {
        ++userCount;
    }
    for (const auto& child : node.m_children)
    {
        CountOrgNodes(child, deptCount, userCount);
    }
}

// 取审计日志列表里的最大 id；空列表返回 0。
int MaxAuditId(const std::vector<AuditLog>& logs)
{
    int maxId = 0;
    for (const auto& log : logs)
    {
        if (log.m_id > maxId)
        {
            maxId = log.m_id;
        }
    }
    return maxId;
}

// 用例①：种子企业 —— 5 家，ent-001 居首、末位为 ent-005。
void Test_SeedEnterprises()
{
    BeginCase("种子企业：数量与顺序");
    std::vector<Enterprise> list = BuildSeedEnterprises();
    FA_EXPECT_EQ(list.size(), 5);
    FA_EXPECT_STREQ(list[0].m_id, _T("ent-001"));
    FA_EXPECT_STREQ(list[0].m_name, _T("明远科技有限公司"));
    FA_EXPECT_STREQ(list[4].m_id, _T("ent-005"));
}

// 用例②：种子组织树 —— ent-001 树存在，根为部门 id=root，部门/员工计数正确。
void Test_SeedOrgTrees()
{
    BeginCase("种子组织树：ent-001 结构");
    std::map<CString, OrgNode> trees = BuildSeedOrgTrees();

    auto it = trees.find(_T("ent-001"));
    FA_EXPECT_TRUE(it != trees.end());
    if (it == trees.end())
    {
        return;
    }

    const OrgNode& root = it->second;
    FA_EXPECT_TRUE(root.m_kind == OrgNodeKind::Dept);
    FA_EXPECT_STREQ(root.m_id, _T("root"));

    int deptCount = 0;
    int userCount = 0;
    CountOrgNodes(root, deptCount, userCount);
    FA_EXPECT_EQ(deptCount, 10);   // root + 9 个部门
    FA_EXPECT_EQ(userCount, 23);   // 23 名员工
}

// 用例③：种子服务号 —— ent-001 共 8 个，ent-002 共 2 个。
void Test_SeedServices()
{
    BeginCase("种子服务号：各企业数量");
    std::map<CString, std::vector<Service>> svc = BuildSeedServices();
    FA_EXPECT_EQ(svc[_T("ent-001")].size(), 8);
    FA_EXPECT_EQ(svc[_T("ent-002")].size(), 2);
}

// 用例④：种子公告 —— 5 条，最新的 a-000 居首。
void Test_SeedAnnouncements()
{
    BeginCase("种子公告：数量与顺序");
    std::vector<Announcement> list = BuildSeedAnnouncements();
    FA_EXPECT_EQ(list.size(), 5);
    FA_EXPECT_STREQ(list[0].m_id, _T("a-000"));
}

// 用例⑤：种子审计日志 —— 14 条，id 从 1 到 14。
void Test_SeedAuditLogs()
{
    BeginCase("种子审计日志：数量与 id");
    std::vector<AuditLog> list = BuildSeedAuditLogs();
    FA_EXPECT_EQ(list.size(), 14);
    FA_EXPECT_EQ(list[0].m_id, 1);
    FA_EXPECT_EQ(list[13].m_id, 14);
}

// 用例⑥：DataStore 构造 —— 单例可取，三类数据初始计数为 5 / 5 / 14。
// 注意：本用例必须在任何变更类用例之前执行，断言的是纯种子状态。
void Test_DataStoreConstruction()
{
    BeginCase("DataStore：构造与初始计数");
    DataStore& store = DataStore::Instance();
    FA_EXPECT_EQ(store.Enterprises().size(), 5);
    FA_EXPECT_EQ(store.Announcements().size(), 5);
    FA_EXPECT_EQ(store.AuditLogs().size(), 14);
}

// 用例⑦⑧：FindEnterprise —— 命中返回正确记录，未命中返回 nullptr。
void Test_FindEnterprise()
{
    BeginCase("DataStore：FindEnterprise");
    DataStore& store = DataStore::Instance();

    const Enterprise* found = store.FindEnterprise(_T("ent-003"));
    FA_EXPECT_NOTNULL(found);
    if (found != nullptr)
    {
        FA_EXPECT_STREQ(found->m_name, _T("禾川生物医药"));
    }
    FA_EXPECT_NULL(store.FindEnterprise(_T("ent-999")));
}

// 用例⑫⑬：OrgTree —— 有树企业命中且根为部门，无树企业返回 nullptr。
void Test_OrgTreeAccessor()
{
    BeginCase("DataStore：OrgTree");
    DataStore& store = DataStore::Instance();

    const OrgNode* tree = store.OrgTree(_T("ent-001"));
    FA_EXPECT_NOTNULL(tree);
    if (tree != nullptr)
    {
        FA_EXPECT_TRUE(tree->m_kind == OrgNodeKind::Dept);
    }
    FA_EXPECT_NULL(store.OrgTree(_T("ent-002")));
}

// 用例⑭：Services —— 各企业服务号数量正确，不存在企业返回空表且不崩溃。
void Test_ServicesAccessor()
{
    BeginCase("DataStore：Services");
    DataStore& store = DataStore::Instance();
    FA_EXPECT_EQ(store.Services(_T("ent-001")).size(), 8);
    FA_EXPECT_EQ(store.Services(_T("ent-002")).size(), 2);
    FA_EXPECT_EQ(store.Services(_T("ent-999")).size(), 0);
}

// 用例⑨⑩⑪：CreateEnterprise —— 企业 +1 且居首、自动建空组织树、自动记审计。
void Test_CreateEnterprise()
{
    BeginCase("DataStore：CreateEnterprise");
    DataStore& store = DataStore::Instance();

    const size_t entBefore   = store.Enterprises().size();
    const size_t auditBefore = store.AuditLogs().size();

    Enterprise e;
    e.m_id        = _T("ent-test-001");
    e.m_name      = _T("测试企业一号");
    e.m_shortName = _T("测试一号");
    e.m_admin     = _T("测试管理员");
    e.m_contact   = _T("test@example.com");
    e.m_status    = EnterpriseStatus::Pending;
    store.CreateEnterprise(e);

    // ⑨ 企业数 +1，新企业在最前。
    FA_EXPECT_EQ(store.Enterprises().size(), entBefore + 1);
    FA_EXPECT_STREQ(store.Enterprises()[0].m_id, _T("ent-test-001"));

    // ⑩ 自动建了空组织树：根为部门、无子节点。
    const OrgNode* tree = store.OrgTree(_T("ent-test-001"));
    FA_EXPECT_NOTNULL(tree);
    if (tree != nullptr)
    {
        FA_EXPECT_TRUE(tree->m_kind == OrgNodeKind::Dept);
        FA_EXPECT_EQ(tree->m_children.size(), 0);
    }

    // ⑪ 自动追加一条 enterprise.create 审计。
    FA_EXPECT_EQ(store.AuditLogs().size(), auditBefore + 1);
    FA_EXPECT_STREQ(store.AuditLogs()[0].m_action, _T("enterprise.create"));
}

// 用例⑮⑯：SendAnnouncement —— 公告 +1 且居首；定时 / 立即分别记不同审计动作。
void Test_SendAnnouncement()
{
    BeginCase("DataStore：SendAnnouncement");
    DataStore& store = DataStore::Instance();

    const size_t annBefore = store.Announcements().size();

    // 立即发布：审计动作应为 announcement.send。
    Announcement now;
    now.m_id        = _T("a-test-now");
    now.m_title     = _T("测试公告-立即");
    now.m_audience  = _T("全部企业");
    now.m_scheduled = false;
    store.SendAnnouncement(now);
    FA_EXPECT_EQ(store.Announcements().size(), annBefore + 1);
    FA_EXPECT_STREQ(store.Announcements()[0].m_id, _T("a-test-now"));
    FA_EXPECT_STREQ(store.AuditLogs()[0].m_action, _T("announcement.send"));

    // 定时发布：审计动作应为 announcement.schedule。
    Announcement scheduled;
    scheduled.m_id        = _T("a-test-sched");
    scheduled.m_title     = _T("测试公告-定时");
    scheduled.m_audience  = _T("全部企业");
    scheduled.m_scheduled = true;
    store.SendAnnouncement(scheduled);
    FA_EXPECT_EQ(store.Announcements().size(), annBefore + 2);
    FA_EXPECT_STREQ(store.AuditLogs()[0].m_action, _T("announcement.schedule"));
}

// 用例⑰⑱⑲：PushAudit —— 字段写入正确；新 id 大于全部种子 id；多次调用 id 单调递增。
void Test_PushAudit()
{
    BeginCase("DataStore：PushAudit");
    DataStore& store = DataStore::Instance();

    const size_t auditBefore = store.AuditLogs().size();
    const int    maxIdBefore = MaxAuditId(store.AuditLogs());

    store.PushAudit(_T("test.action"), _T("测试目标"),
                    Severity::Warn, _T("测试详情"));

    // ⑰ 审计 +1，字段写入正确。
    FA_EXPECT_EQ(store.AuditLogs().size(), auditBefore + 1);
    const AuditLog& log1 = store.AuditLogs()[0];
    FA_EXPECT_STREQ(log1.m_action, _T("test.action"));
    FA_EXPECT_STREQ(log1.m_target, _T("测试目标"));
    FA_EXPECT_STREQ(log1.m_detail, _T("测试详情"));
    FA_EXPECT_TRUE(log1.m_severity == Severity::Warn);
    FA_EXPECT_FALSE(log1.m_ts.IsEmpty());
    FA_EXPECT_FALSE(log1.m_actor.IsEmpty());

    // ⑱ 新 id 大于全部种子 id（种子最大 id 为 14），且等于此前最大 id + 1。
    FA_EXPECT_TRUE(log1.m_id > 14);
    FA_EXPECT_EQ(log1.m_id, maxIdBefore + 1);

    // 先把第一条的 id 存成值：下面第二次 PushAudit 会 insert 到 vector 头部，
    // 令 log1 这个引用失效（其指向的槽位被新元素占据），不能再读 log1。
    const int firstPushId = log1.m_id;

    // ⑲ 再推一条，id 单调递增。
    store.PushAudit(_T("test.action2"), _T("-"), Severity::Info, _T("第二条"));
    FA_EXPECT_EQ(store.AuditLogs()[0].m_id, firstPushId + 1);
}

} // anonymous namespace

void RunAllDataStoreTests()
{
    PrintLine(_T("======== flamingoAdmin Mock 数据层单元测试 ========\n"));

    // 只读用例（不改变单例状态）。Test_DataStoreConstruction 断言纯种子计数，
    // 必须排在下方变更类用例之前。
    Test_SeedEnterprises();
    Test_SeedOrgTrees();
    Test_SeedServices();
    Test_SeedAnnouncements();
    Test_SeedAuditLogs();
    Test_DataStoreConstruction();
    Test_FindEnterprise();
    Test_OrgTreeAccessor();
    Test_ServicesAccessor();

    // 变更类用例（改变单例状态，全部采用前后差值断言）。
    Test_CreateEnterprise();
    Test_SendAnnouncement();
    Test_PushAudit();
}

}} // namespace flamingoadmin::test
