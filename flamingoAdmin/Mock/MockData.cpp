/**
 *  flamingoAdmin —— mock 种子数据实现。
 *
 *  数据内容（企业、组织树、服务号、公告、审计日志）逐条移植自设计稿
 *  design/FlamingoIM Enterprise Admin.html 的 SEED_* 数组，用于在无服务端
 *  连接时驱动整个管理控制台界面。
 *
 *  balloonwj@qq.com   2026.05.21
 */
#include "stdafx.h"
#include "Mock/MockData.h"

namespace flamingoadmin {

namespace {

// ---------------------------------------------------------------------------
// 工厂小助手 —— 让下面的种子数据块尽量短、可读。
// ---------------------------------------------------------------------------

// 构造一条企业记录。参数顺序与 Enterprise 字段顺序一致。
Enterprise MakeEnterprise(LPCTSTR id, LPCTSTR name, LPCTSTR shortName,
                          LPCTSTR industry, LPCTSTR region,
                          int members, int depts, EnterpriseStatus status,
                          LPCTSTR plan, LPCTSTR createdAt,
                          LPCTSTR admin, LPCTSTR contact, COLORREF logoColor)
{
    Enterprise e;
    e.m_id        = id;
    e.m_name      = name;
    e.m_shortName = shortName;
    e.m_industry  = industry;
    e.m_region    = region;
    e.m_members   = members;
    e.m_depts     = depts;
    e.m_status    = status;
    e.m_plan      = plan;
    e.m_createdAt = createdAt;
    e.m_admin     = admin;
    e.m_contact   = contact;
    e.m_logoColor = logoColor;
    return e;
}

// 由节点 id 派生一个稳定的小整数，用于生成 mock 的工号 / 工位 / 座机，
// 使同一员工每次运行都得到一致的虚构数据。
unsigned DeriveSeed(const CString& id)
{
    unsigned h = 0;
    for (int i = 0; i < id.GetLength(); ++i)
    {
        h = h * 31u + static_cast<unsigned>(id[i]);
    }
    return h;
}

// 构造一个员工节点。workstation / landline / employeeNo 三个字段在设计稿里
// 由 id 自动派生，这里同样按 id 确定性生成，保证可复现。
//   status：在职状态，默认在职。
//   gender：性别，默认 "男"。
//   signature：个性签名，默认空。
OrgNode MakeUser(LPCTSTR id, LPCTSTR name, LPCTSTR title, LPCTSTR email,
                 LPCTSTR phone, LPCTSTR joined,
                 UserStatus status = UserStatus::Active,
                 LPCTSTR gender = _T("男"),
                 LPCTSTR signature = _T(""))
{
    OrgNode n;
    n.m_kind       = OrgNodeKind::User;
    n.m_id         = id;
    n.m_name       = name;
    n.m_title      = title;
    n.m_email      = email;
    n.m_phone      = phone;
    n.m_joined     = joined;
    n.m_userStatus = status;
    n.m_gender     = gender;
    n.m_signature  = signature;

    // 楼层取 3~8F，座机分机取 1000~1899，工号取 M1000~M9999。
    const int kFloorBase = 3;     // 工位起始楼层
    const int kFloorSpan = 6;     // 工位楼层跨度（3F~8F）
    const int kZoneSpan  = 24;    // 工位区位编号跨度
    const int kExtBase   = 1000;  // 座机分机号起始
    const int kExtSpan   = 900;   // 座机分机号跨度
    const int kEmpBase   = 1000;  // 工号数字部分起始
    const int kEmpSpan   = 9000;  // 工号数字部分跨度

    unsigned seed = DeriveSeed(n.m_id);
    n.m_workstation.Format(_T("上海·张江园区 %dF 西区 B-%02d"),
                           kFloorBase + static_cast<int>(seed % kFloorSpan),
                           12 + static_cast<int>(seed % kZoneSpan));
    n.m_landline.Format(_T("021-6868-8888 转 %d"),
                        kExtBase + static_cast<int>(seed % kExtSpan));
    n.m_employeeNo.Format(_T("M%04d"),
                          kEmpBase + static_cast<int>(seed % kEmpSpan));
    return n;
}

// 构造一个部门节点。
//   children：子节点列表（子部门 + 员工）。
OrgNode MakeDept(LPCTSTR id, LPCTSTR name, LPCTSTR leader,
                 std::vector<OrgNode> children)
{
    OrgNode n;
    n.m_kind     = OrgNodeKind::Dept;
    n.m_id       = id;
    n.m_name     = name;
    n.m_leader   = leader;
    n.m_children = std::move(children);
    return n;
}

// 构造一条服务号绑定记录。
ServiceBinding MakeBinding(BindingKind kind, LPCTSTR id, LPCTSTR label)
{
    ServiceBinding b;
    b.m_kind  = kind;
    b.m_id    = id;
    b.m_label = label;
    return b;
}

// 构造一个服务号（绑定列表由调用方随后赋值，便于书写多条绑定）。
Service MakeService(LPCTSTR id, LPCTSTR name, LPCTSTR glyph, COLORREF color,
                    ServiceKind kind, LPCTSTR desc, ServiceStatus status,
                    LPCTSTR owner, LPCTSTR createdAt, int msgCount, int subs)
{
    Service s;
    s.m_id        = id;
    s.m_name      = name;
    s.m_glyph     = glyph;
    s.m_color     = color;
    s.m_kind      = kind;
    s.m_desc      = desc;
    s.m_status    = status;
    s.m_owner     = owner;
    s.m_createdAt = createdAt;
    s.m_msgCount  = msgCount;
    s.m_subs      = subs;
    return s;
}

// 构造一条审计日志记录。
AuditLog MakeAudit(int id, LPCTSTR ts, LPCTSTR actor, LPCTSTR ip,
                   LPCTSTR action, LPCTSTR target, Severity severity,
                   LPCTSTR detail)
{
    AuditLog a;
    a.m_id       = id;
    a.m_ts       = ts;
    a.m_actor    = actor;
    a.m_ip       = ip;
    a.m_action   = action;
    a.m_target   = target;
    a.m_severity = severity;
    a.m_detail   = detail;
    return a;
}

} // anonymous namespace

// ===========================================================================
// 种子企业（5 家）
// ===========================================================================
std::vector<Enterprise> BuildSeedEnterprises()
{
    std::vector<Enterprise> list;
    list.push_back(MakeEnterprise(
        _T("ent-001"), _T("明远科技有限公司"), _T("明远科技"),
        _T("软件与信息技术"), _T("上海"), 248, 14, EnterpriseStatus::Active,
        _T("旗舰版"), _T("2025-09-12"), _T("李正阳"),
        _T("admin@mingyuan.tech"), RGB(0xFF, 0x75, 0x63)));
    list.push_back(MakeEnterprise(
        _T("ent-002"), _T("青鸟物流集团"), _T("青鸟物流"),
        _T("运输与仓储"), _T("深圳"), 1820, 42, EnterpriseStatus::Active,
        _T("企业版"), _T("2024-11-03"), _T("王怡然"),
        _T("it@qingniao-logi.com"), RGB(0x28, 0x64, 0xD4)));
    list.push_back(MakeEnterprise(
        _T("ent-003"), _T("禾川生物医药"), _T("禾川生物"),
        _T("生物医药"), _T("苏州"), 96, 8, EnterpriseStatus::Pending,
        _T("专业版"), _T("2026-04-22"), _T("陈思源"),
        _T("hr@hechuan-bio.com"), RGB(0x1F, 0x9D, 0x55)));
    list.push_back(MakeEnterprise(
        _T("ent-004"), _T("灰象设计工作室"), _T("灰象设计"),
        _T("广告与设计"), _T("杭州"), 34, 4, EnterpriseStatus::Active,
        _T("专业版"), _T("2025-06-18"), _T("周雨彤"),
        _T("studio@huixiang.co"), RGB(0x8E, 0x5B, 0xD9)));
    list.push_back(MakeEnterprise(
        _T("ent-005"), _T("北辰金融控股"), _T("北辰金控"),
        _T("金融服务"), _T("北京"), 512, 21, EnterpriseStatus::Suspended,
        _T("企业版"), _T("2024-03-08"), _T("赵承宇"),
        _T("platform@beichen-fin.com"), RGB(0x0E, 0xA5, 0xA4)));
    return list;
}

// ===========================================================================
// 种子组织树（仅 ent-001 明远科技有完整组织树）
// ===========================================================================
std::map<CString, OrgNode> BuildSeedOrgTrees()
{
    std::map<CString, OrgNode> trees;

    //根节点代表公司本身，标记为公司根：界面据此隐藏删除动作、并用公司样式绘制。
    OrgNode ent001Root = MakeDept(_T("root"), _T("明远科技有限公司"), _T("李正阳"),
    {
        MakeDept(_T("d-tech"), _T("技术中心"), _T("张鹤鸣"),
        {
            MakeDept(_T("d-front"), _T("前端组"), _T("蒋灵"),
            {
                MakeUser(_T("u-101"), _T("蒋灵"), _T("前端组组长"),
                         _T("jiang.l@mingyuan.tech"), _T("138-0011-2233"),
                         _T("2023-04-10"), UserStatus::Active, _T("男"),
                         _T("好设计自带说明书。")),
                MakeUser(_T("u-102"), _T("沈砚"), _T("高级前端工程师"),
                         _T("shen.y@mingyuan.tech"), _T("139-2233-1100"),
                         _T("2024-07-01")),
                MakeUser(_T("u-103"), _T("顾清"), _T("前端工程师"),
                         _T("gu.q@mingyuan.tech"), _T("177-8888-1209"),
                         _T("2025-03-15"), UserStatus::Active, _T("女")),
            }),
            MakeDept(_T("d-back"), _T("后端组"), _T("韩奕"),
            {
                MakeUser(_T("u-104"), _T("韩奕"), _T("后端组组长"),
                         _T("han.y@mingyuan.tech"), _T("188-7766-2210"),
                         _T("2022-09-21")),
                MakeUser(_T("u-105"), _T("邢野"), _T("后端工程师"),
                         _T("xing.y@mingyuan.tech"), _T("139-1100-6677"),
                         _T("2024-01-09")),
                MakeUser(_T("u-106"), _T("陆禾"), _T("后端工程师"),
                         _T("lu.h@mingyuan.tech"), _T("135-0099-8812"),
                         _T("2025-08-30"), UserStatus::Active, _T("女")),
                MakeUser(_T("u-107"), _T("梁川"), _T("SRE 工程师"),
                         _T("liang.c@mingyuan.tech"), _T("187-2211-3344"),
                         _T("2023-12-04")),
            }),
            MakeUser(_T("u-108"), _T("邵远"), _T("架构师"),
                     _T("shao.y@mingyuan.tech"), _T("186-0099-2200"),
                     _T("2022-02-11")),
        }),
        MakeDept(_T("d-product"), _T("产品中心"), _T("姜微"),
        {
            MakeUser(_T("u-201"), _T("姜微"), _T("产品总监"),
                     _T("jiang.w@mingyuan.tech"), _T("139-7878-2200"),
                     _T("2022-05-30"), UserStatus::Active, _T("女")),
            MakeUser(_T("u-202"), _T("骆秋"), _T("高级产品经理"),
                     _T("luo.q@mingyuan.tech"), _T("188-1122-3344"),
                     _T("2024-02-18"), UserStatus::Active, _T("女")),
            MakeUser(_T("u-203"), _T("余昭"), _T("产品经理"),
                     _T("yu.z@mingyuan.tech"), _T("135-3344-5566"),
                     _T("2025-10-08")),
        }),
        MakeDept(_T("d-design"), _T("设计中心"), _T("安朵"),
        {
            MakeUser(_T("u-301"), _T("安朵"), _T("设计总监"),
                     _T("an.d@mingyuan.tech"), _T("139-0011-9988"),
                     _T("2023-08-01"), UserStatus::Active, _T("女")),
            MakeUser(_T("u-302"), _T("卫书"), _T("视觉设计师"),
                     _T("wei.s@mingyuan.tech"), _T("185-3322-1100"),
                     _T("2024-11-12")),
        }),
        MakeDept(_T("d-ops"), _T("商务与运营"), _T("宋朗"),
        {
            MakeDept(_T("d-sales"), _T("销售部"), _T("宋朗"),
            {
                MakeUser(_T("u-401"), _T("宋朗"), _T("销售总监"),
                         _T("song.l@mingyuan.tech"), _T("139-9876-5432"),
                         _T("2022-03-10")),
                MakeUser(_T("u-402"), _T("邓简"), _T("销售经理(华东)"),
                         _T("deng.j@mingyuan.tech"), _T("188-7777-3322"),
                         _T("2023-11-20")),
                MakeUser(_T("u-403"), _T("屈言"), _T("销售经理(华南)"),
                         _T("qu.y@mingyuan.tech"), _T("135-5555-4321"),
                         _T("2024-05-06")),
            }),
            MakeDept(_T("d-market"), _T("市场部"), _T("傅清"),
            {
                MakeUser(_T("u-404"), _T("傅清"), _T("市场经理"),
                         _T("fu.q@mingyuan.tech"), _T("139-6666-9090"),
                         _T("2024-08-22"), UserStatus::Active, _T("女")),
                MakeUser(_T("u-405"), _T("岑玥"), _T("内容运营"),
                         _T("cen.y@mingyuan.tech"), _T("177-2233-9090"),
                         _T("2025-12-01"), UserStatus::Active, _T("女")),
            }),
        }),
        MakeDept(_T("d-hr"), _T("人力行政"), _T("闻舟"),
        {
            MakeUser(_T("u-501"), _T("闻舟"), _T("HR 总监"),
                     _T("wen.z@mingyuan.tech"), _T("139-2222-1010"),
                     _T("2022-07-14"), UserStatus::Active, _T("女")),
            MakeUser(_T("u-502"), _T("穆河"), _T("HRBP"),
                     _T("mu.h@mingyuan.tech"), _T("188-3344-5566"),
                     _T("2024-04-17")),
            MakeUser(_T("u-503"), _T("简宁"), _T("行政专员"),
                     _T("jian.n@mingyuan.tech"), _T("135-0011-7788"),
                     _T("2025-02-09"), UserStatus::OnLeave, _T("女")),
        }),
        // 直接挂在根部门下、不属于任何子部门的高管。
        MakeUser(_T("u-001"), _T("李正阳"), _T("CEO / 创始人"),
                 _T("li.zy@mingyuan.tech"), _T("138-0000-0001"),
                 _T("2021-01-01"), UserStatus::Active, _T("男"),
                 _T("把简单的事做到极致，就是不简单。")),
        MakeUser(_T("u-002"), _T("秦昭"), _T("CTO"),
                 _T("qin.z@mingyuan.tech"), _T("138-0000-0002"),
                 _T("2021-01-15"), UserStatus::Active, _T("男"),
                 _T("Stay curious.")),
    });
    ent001Root.m_isCompanyRoot = true;
    trees[_T("ent-001")] = ent001Root;

    return trees;
}

// ===========================================================================
// 种子服务号（ent-001 共 8 个，ent-002 共 2 个）
// ===========================================================================
std::map<CString, std::vector<Service>> BuildSeedServices()
{
    std::map<CString, std::vector<Service>> services;

    // ---- ent-001 明远科技 ----
    {
        std::vector<Service> list;

        Service hr = MakeService(_T("svc-001"), _T("明远 HR 助手"), _T("HR"),
            RGB(0xFF, 0x75, 0x63), ServiceKind::Notification,
            _T("入离职流程、薪资单、社保公积金、年假提醒等推送"),
            ServiceStatus::Active, _T("闻舟"), _T("2024-08-12"), 1284, 248);
        hr.m_bindings.push_back(MakeBinding(BindingKind::Enterprise,
            _T("root"), _T("全员（明远科技）")));
        list.push_back(hr);

        Service attendance = MakeService(_T("svc-002"), _T("打卡考勤"), _T("打卡"),
            RGB(0x1F, 0x9D, 0x55), ServiceKind::Assistant,
            _T("外勤打卡、补卡审批、月度考勤汇总"),
            ServiceStatus::Active, _T("闻舟"), _T("2024-08-12"), 6420, 248);
        attendance.m_bindings.push_back(MakeBinding(BindingKind::Enterprise,
            _T("root"), _T("全员（明远科技）")));
        list.push_back(attendance);

        Service itbot = MakeService(_T("svc-003"), _T("IT 支持机器人"), _T("IT"),
            RGB(0x28, 0x64, 0xD4), ServiceKind::Assistant,
            _T("账号问题、VPN 故障、设备申领的自助式工单"),
            ServiceStatus::Active, _T("梁川"), _T("2024-09-04"), 842, 248);
        itbot.m_bindings.push_back(MakeBinding(BindingKind::Enterprise,
            _T("root"), _T("全员（明远科技）")));
        list.push_back(itbot);

        Service alert = MakeService(_T("svc-004"), _T("线上事故告警"), _T("告警"),
            RGB(0xD6, 0x34, 0x2A), ServiceKind::Integration,
            _T("对接 Prometheus / Sentry，P0–P2 告警实时推送"),
            ServiceStatus::Active, _T("梁川"), _T("2025-02-18"), 512, 14);
        alert.m_bindings.push_back(MakeBinding(BindingKind::Dept,
            _T("d-tech"), _T("技术中心")));
        list.push_back(alert);

        Service finance = MakeService(_T("svc-005"), _T("财务通知"), _T("财"),
            RGB(0x8E, 0x5B, 0xD9), ServiceKind::Notification,
            _T("报销审批、对公付款进度、月度财报"),
            ServiceStatus::Active, _T("李正阳"), _T("2025-04-22"), 284, 248);
        finance.m_bindings.push_back(MakeBinding(BindingKind::Enterprise,
            _T("root"), _T("全员（明远科技）")));
        list.push_back(finance);

        Service frontDaily = MakeService(_T("svc-006"), _T("前端组日报"), _T("前"),
            RGB(0x0E, 0xA5, 0xA4), ServiceKind::Notification,
            _T("每日自动汇总前端组提交、构建、Bug 修复情况"),
            ServiceStatus::Active, _T("蒋灵"), _T("2025-06-09"), 182, 3);
        frontDaily.m_bindings.push_back(MakeBinding(BindingKind::Dept,
            _T("d-front"), _T("技术中心 / 前端组")));
        list.push_back(frontDaily);

        Service ceoBrief = MakeService(_T("svc-007"), _T("CEO 内参"), _T("C"),
            RGB(0x1C, 0x22, 0x30), ServiceKind::Notification,
            _T("每周一份的核心高管简报，含经营关键指标"),
            ServiceStatus::Active, _T("李正阳"), _T("2025-11-30"), 24, 5);
        ceoBrief.m_bindings.push_back(MakeBinding(BindingKind::User, _T("u-001"), _T("李正阳")));
        ceoBrief.m_bindings.push_back(MakeBinding(BindingKind::User, _T("u-002"), _T("秦昭")));
        ceoBrief.m_bindings.push_back(MakeBinding(BindingKind::User, _T("u-108"), _T("邵远")));
        ceoBrief.m_bindings.push_back(MakeBinding(BindingKind::User, _T("u-201"), _T("姜微")));
        ceoBrief.m_bindings.push_back(MakeBinding(BindingKind::User, _T("u-301"), _T("安朵")));
        list.push_back(ceoBrief);

        // 草稿态服务号，尚无绑定。
        Service designReview = MakeService(_T("svc-008"), _T("设计评审请求"), _T("设"),
            RGB(0xD8, 0x8A, 0x17), ServiceKind::Assistant,
            _T("设计稿评审排期、反馈聚合"),
            ServiceStatus::Draft, _T("安朵"), _T("2026-05-18"), 0, 0);
        list.push_back(designReview);

        services[_T("ent-001")] = std::move(list);
    }

    // ---- ent-002 青鸟物流 ----
    {
        std::vector<Service> list;

        Service waybill = MakeService(_T("svc-101"), _T("青鸟运单通知"), _T("单"),
            RGB(0x28, 0x64, 0xD4), ServiceKind::Notification,
            _T("实时运单状态变更、异常拦截"),
            ServiceStatus::Active, _T("王怡然"), _T("2024-12-01"), 54820, 1820);
        waybill.m_bindings.push_back(MakeBinding(BindingKind::Enterprise,
            _T("root"), _T("全员（青鸟物流）")));
        list.push_back(waybill);

        Service warehouse = MakeService(_T("svc-102"), _T("仓储库存告警"), _T("库"),
            RGB(0xD8, 0x8A, 0x17), ServiceKind::Integration,
            _T("低库存、超容、温区异常告警"),
            ServiceStatus::Active, _T("王怡然"), _T("2025-01-08"), 2810, 84);
        warehouse.m_bindings.push_back(MakeBinding(BindingKind::Dept,
            _T("d-wh"), _T("仓储管理部")));
        list.push_back(warehouse);

        services[_T("ent-002")] = std::move(list);
    }

    return services;
}

// ===========================================================================
// 种子公告（5 条，最新在前）
// ===========================================================================
std::vector<Announcement> BuildSeedAnnouncements()
{
    std::vector<Announcement> list;

    {
        Announcement a;
        a.m_id        = _T("a-000");
        a.m_title     = _T("5 月 28 日（周四）全员异步周会预告");
        a.m_body      = _T("本周采用异步形式。请在周三 18:00 前将各小组进展更新至")
                        _T("「周会」群文档。届时将统一汇总并 @相关负责人。");
        a.m_audience  = _T("全部企业");
        a.m_severity  = Severity::Info;
        a.m_sender    = _T("李正阳");
        a.m_pinned    = false;
        a.m_scheduled = true;
        a.m_sentAt    = _T("2026-05-27 09:00");
        a.m_read      = 0;
        a.m_total     = 2710;
        list.push_back(a);
    }
    {
        Announcement a;
        a.m_id        = _T("a-001");
        a.m_title     = _T("FlamingoIM v4.2 客户端发布说明");
        a.m_body      = _T("本次版本更新包含端到端加密通道升级、消息撤回时限延长至 5 ")
                        _T("分钟，以及全新的「会议室」功能。请各企业管理员通知员工于 ")
                        _T("5 月 25 日前完成升级。");
        a.m_audience  = _T("全部企业");
        a.m_severity  = Severity::Normal;
        a.m_sender    = _T("平台运营");
        a.m_pinned    = true;
        a.m_scheduled = false;
        a.m_sentAt    = _T("2026-05-18 10:00");
        a.m_read      = 1832;
        a.m_total     = 2710;
        list.push_back(a);
    }
    {
        Announcement a;
        a.m_id        = _T("a-002");
        a.m_title     = _T("5 月 24 日 02:00-04:00 数据库滚动维护");
        a.m_body      = _T("届时消息收发将出现短暂抖动（< 30 秒），不会影响数据。")
                        _T("建议在此时间窗口前完成大文件传输。");
        a.m_audience  = _T("明远科技 / 青鸟物流");
        a.m_severity  = Severity::Warn;
        a.m_sender    = _T("平台运营");
        a.m_pinned    = false;
        a.m_scheduled = false;
        a.m_sentAt    = _T("2026-05-17 18:42");
        a.m_read      = 412;
        a.m_total     = 520;
        list.push_back(a);
    }
    {
        Announcement a;
        a.m_id        = _T("a-003");
        a.m_title     = _T("安全公告：禁止在群组中分发外链下载安装包");
        a.m_body      = _T("近期检测到伪装为客户端升级包的钓鱼链接，请提醒员工仅通过")
                        _T("官方渠道下载。FlamingoIM 已在客户端侧加入 URL 信誉校验。");
        a.m_audience  = _T("全部企业");
        a.m_severity  = Severity::Danger;
        a.m_sender    = _T("安全团队");
        a.m_pinned    = false;
        a.m_scheduled = false;
        a.m_sentAt    = _T("2026-05-15 09:21");
        a.m_read      = 2310;
        a.m_total     = 2710;
        list.push_back(a);
    }
    {
        Announcement a;
        a.m_id        = _T("a-004");
        a.m_title     = _T("欢迎「禾川生物医药」加入 FlamingoIM");
        a.m_body      = _T("已开通专业版账户，初始管理员：chen.sy@hechuan-bio.com。");
        a.m_audience  = _T("内部 / 运营组");
        a.m_severity  = Severity::Normal;
        a.m_sender    = _T("李正阳");
        a.m_pinned    = false;
        a.m_scheduled = false;
        a.m_sentAt    = _T("2026-04-22 14:08");
        a.m_read      = 8;
        a.m_total     = 8;
        list.push_back(a);
    }

    return list;
}

// ===========================================================================
// 种子审计日志（14 条，最新在前）
// ===========================================================================
std::vector<AuditLog> BuildSeedAuditLogs()
{
    const TCHAR* const kAdminLi = _T("admin / 李正阳");  // 出现频繁的操作者，抽出常量
    const TCHAR* const kIpLi    = _T("10.12.4.88");      // 该操作者的固定来源 IP

    std::vector<AuditLog> list;
    list.push_back(MakeAudit(1,  _T("2026-05-21 09:42:11"), kAdminLi, kIpLi,
        _T("login.success"), _T("-"), Severity::Info, _T("通过双因子登录")));
    list.push_back(MakeAudit(2,  _T("2026-05-21 09:43:02"), kAdminLi, kIpLi,
        _T("enterprise.view"), _T("ent-001 明远科技"), Severity::Info, _T("查看企业详情")));
    list.push_back(MakeAudit(3,  _T("2026-05-21 09:45:17"), kAdminLi, kIpLi,
        _T("dept.create"), _T("明远科技 / 技术中心 / 前端组"), Severity::Info,
        _T("新增部门节点 d-front")));
    list.push_back(MakeAudit(4,  _T("2026-05-21 09:48:34"), kAdminLi, kIpLi,
        _T("user.create"), _T("明远科技 / 顾清 (u-103)"), Severity::Info,
        _T("新增员工，分配至 前端组")));
    list.push_back(MakeAudit(5,  _T("2026-05-21 09:51:02"), kAdminLi, kIpLi,
        _T("user.role.update"), _T("明远科技 / 蒋灵 (u-101)"), Severity::Warn,
        _T("角色: 工程师 → 组长")));
    list.push_back(MakeAudit(6,  _T("2026-05-21 10:02:48"), kAdminLi, kIpLi,
        _T("announcement.send"), _T("全部企业"), Severity::Info,
        _T("发布 \"v4.2 客户端发布说明\"")));
    list.push_back(MakeAudit(7,  _T("2026-05-20 22:31:09"), _T("system"), _T("-"),
        _T("backup.completed"), _T("cluster-prod-cn-east-2"), Severity::Info,
        _T("增量备份 142.3 GB")));
    list.push_back(MakeAudit(8,  _T("2026-05-20 19:11:55"), _T("admin / 王怡然"),
        _T("58.247.10.22"), _T("user.disable"), _T("青鸟物流 / 黄某 (u-9931)"),
        Severity::Danger, _T("账号已停用，原因: 离职")));
    list.push_back(MakeAudit(9,  _T("2026-05-20 16:08:23"), kAdminLi, kIpLi,
        _T("enterprise.create"), _T("ent-003 禾川生物医药"), Severity::Warn,
        _T("新增企业，初始管理员 chen.sy@hechuan-bio.com")));
    list.push_back(MakeAudit(10, _T("2026-05-20 14:47:30"), kAdminLi, kIpLi,
        _T("login.failed"), _T("-"), Severity::Danger, _T("密码错误 (1/5)")));
    list.push_back(MakeAudit(11, _T("2026-05-20 09:30:00"), kAdminLi, kIpLi,
        _T("login.success"), _T("-"), Severity::Info, _T("通过双因子登录")));
    list.push_back(MakeAudit(12, _T("2026-05-19 17:22:14"), _T("admin / 周雨彤"),
        _T("114.88.32.7"), _T("announcement.draft"), _T("灰象设计 / 内部公告"),
        Severity::Info, _T("保存草稿")));
    list.push_back(MakeAudit(13, _T("2026-05-19 11:09:48"), kAdminLi, kIpLi,
        _T("dept.rename"), _T("明远科技 / 商务部 → 商务与运营"), Severity::Warn,
        _T("重命名部门")));
    list.push_back(MakeAudit(14, _T("2026-05-19 10:01:33"), _T("system"), _T("-"),
        _T("license.renew"), _T("青鸟物流"), Severity::Info,
        _T("企业版授权续期 12 个月")));
    return list;
}

} // namespace flamingoadmin
