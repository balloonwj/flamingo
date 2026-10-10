/**
 *  flamingoAdmin —— 服务号绑定范围的读取与翻译单元测试（SVD 系列）。
 *
 *  服务号列表接口（service/list.do）不带绑定范围，绑定只能从 service/detail.do 读。
 *  早先控制台从不调用详情接口，「修改绑定」弹窗总是从空集合开始，而服务端
 *  setbindings.do 是整体替换 —— 点一次保存就把原有绑定全部清掉。本文件覆盖修复用到
 *  的四个纯函数：
 *    - SVD1：详情应答解析出三种类型的绑定；
 *    - SVD2：错误码、JSON 损坏、缺 service 对象时解析结果为失败；
 *    - SVD3：按组织树把原始绑定翻译成带显示名的绑定项，查不到时有兜底名；
 *    - SVD4：重拉列表时已读到的绑定按服务号 ID 保留，新服务号保持「未读到」。
 *  另含 SVK2：服务号类型「集成型」与服务端类型 2 双向对应（2026-10-04 服务端加入类型 2，
 *  此前控制台把集成型映射成 0，保存后变成通知型）。
 *
 *  全部为纯函数测试，不发起真实网络请求。
 *
 *  balloonwj@qq.com   2026-10-04
 */
#include "stdafx.h"
#include "Tests/TestFramework.h"
#include "Util/AdminApi.h"

#include <string>
#include <vector>

namespace flamingoadmin { namespace test {

namespace {

using namespace flamingoadmin::adminapi;

// 构造一份测试用的服务器配置。主机与端口不影响被测的纯函数，取固定值即可。
ServerConfig MakeServer()
{
    ServerConfig s;
    s.m_host = _T("127.0.0.1");
    s.m_port = 12345;
    s.m_useTls = false;
    return s;
}

// 构造一个「传输成功、HTTP 200、body 为给定 JSON」的应答。
//   json：应答体（UTF-8 JSON 文本）。
HttpResult MakeHttpOk(const std::string& json)
{
    HttpResult r;
    r.m_transportOk = true;
    r.m_statusCode = 200;
    r.m_body = json;
    return r;
}

// 构造一个组织节点。
//   kind：部门 / 员工；id：节点 ID；name：显示名。
OrgNode MakeNode(OrgNodeKind kind, LPCTSTR id, LPCTSTR name)
{
    OrgNode n;
    n.m_kind = kind;
    n.m_id   = id;
    n.m_name = name;
    return n;
}

// 构造样例组织树：公司根（1）下有研发部（11），研发部下有员工张三（501）；
// 公司根下直接挂员工李四（502）。
OrgNode MakeSampleOrg()
{
    OrgNode root = MakeNode(OrgNodeKind::Dept, _T("1"), _T("明远科技"));
    root.m_isCompanyRoot = true;
    OrgNode rd = MakeNode(OrgNodeKind::Dept, _T("11"), _T("研发部"));
    rd.m_children.push_back(MakeNode(OrgNodeKind::User, _T("501"), _T("张三")));
    root.m_children.push_back(rd);
    root.m_children.push_back(MakeNode(OrgNodeKind::User, _T("502"), _T("李四")));
    return root;
}

// 构造一条原始绑定。
//   bindType：0 全员 / 1 部门 / 2 员工；targetId：目标 ID。
RawServiceBinding MakeRaw(int bindType, long long targetId)
{
    RawServiceBinding b;
    b.m_bindType = bindType;
    b.m_targetId = targetId;
    return b;
}

// 构造一个服务号。
//   id：服务号 ID；loaded：绑定是否已读到；bindingId：非空时放一条员工绑定。
Service MakeService(LPCTSTR id, bool loaded, LPCTSTR bindingId)
{
    Service s;
    s.m_id = id;
    s.m_bindingsLoaded = loaded;
    if (bindingId != nullptr)
    {
        ServiceBinding b;
        b.m_kind  = BindingKind::User;
        b.m_id    = bindingId;
        b.m_label = _T("张三");
        s.m_bindings.push_back(b);
    }
    return s;
}

// SVD1：请求路径与请求体；详情应答解析出资料与三种类型的绑定。
void TestServiceDetailParsesBindings()
{
    BeginCase("SVD1 服务号详情：请求构造与三种类型绑定的解析");

    const HttpRequest req = MakeServiceDetailRequest(MakeServer(), _T("tk"), 9001);
    FA_EXPECT_TRUE(req.m_path == L"/admin/service/detail.do");
    FA_EXPECT_TRUE(req.m_bodyUtf8.find("\"serviceid\":9001") != std::string::npos);
    FA_EXPECT_TRUE(req.m_bodyUtf8.find("\"token\":\"tk\"") != std::string::npos);

    const ServiceDetailResult r = ParseServiceDetailResponse(MakeHttpOk(
        "{\"code\":0,\"msg\":\"ok\",\"service\":{\"serviceid\":9001,\"name\":\"HR\","
        "\"kind\":1,\"status\":0,\"bindings\":["
        "{\"bindtype\":0,\"targetid\":0},"
        "{\"bindtype\":1,\"targetid\":11},"
        "{\"bindtype\":2,\"targetid\":501}]}}"));
    FA_EXPECT_TRUE(r.Succeeded());
    FA_EXPECT_STREQ(r.m_service.m_id, _T("9001"));
    FA_EXPECT_STREQ(r.m_service.m_name, _T("HR"));
    FA_EXPECT_EQ(r.m_bindings.size(), 3);
    if (r.m_bindings.size() == 3)
    {
        FA_EXPECT_EQ(r.m_bindings[0].m_bindType, 0);
        FA_EXPECT_EQ(r.m_bindings[1].m_bindType, 1);
        FA_EXPECT_EQ(r.m_bindings[1].m_targetId, 11);
        FA_EXPECT_EQ(r.m_bindings[2].m_bindType, 2);
        FA_EXPECT_EQ(r.m_bindings[2].m_targetId, 501);
    }

    // 没有任何绑定：成功，且绑定列表为空（这才是真正的「尚未绑定」）。
    const ServiceDetailResult empty = ParseServiceDetailResponse(MakeHttpOk(
        "{\"code\":0,\"service\":{\"serviceid\":9002,\"bindings\":[]}}"));
    FA_EXPECT_TRUE(empty.Succeeded());
    FA_EXPECT_EQ(empty.m_bindings.size(), 0);
}

// SVD2：失败路径一律判失败并带中文提示，调用方据此不打开绑定弹窗。
void TestServiceDetailFailurePaths()
{
    BeginCase("SVD2 服务号详情：错误码、JSON 损坏、缺 service 对象都判失败");

    // 服务端业务错误码（无权限）。
    const ServiceDetailResult denied = ParseServiceDetailResponse(MakeHttpOk(
        "{\"code\":116,\"msg\":\"permission denied\"}"));
    FA_EXPECT_FALSE(denied.Succeeded());
    FA_EXPECT_EQ(denied.m_code, 116);
    FA_EXPECT_FALSE(denied.m_message.IsEmpty());

    // JSON 损坏。
    const ServiceDetailResult broken = ParseServiceDetailResponse(MakeHttpOk("{\"code\":0,"));
    FA_EXPECT_FALSE(broken.Succeeded());
    FA_EXPECT_FALSE(broken.m_message.IsEmpty());

    // 成功码却没有 service 对象：不能当作「没有绑定」。
    const ServiceDetailResult noService = ParseServiceDetailResponse(MakeHttpOk(
        "{\"code\":0,\"msg\":\"ok\"}"));
    FA_EXPECT_FALSE(noService.Succeeded());
    FA_EXPECT_FALSE(noService.m_message.IsEmpty());

    // 传输失败。
    HttpResult down;
    down.m_transportOk = false;
    down.m_error = _T("无法连接服务器");
    const ServiceDetailResult offline = ParseServiceDetailResponse(down);
    FA_EXPECT_FALSE(offline.Succeeded());
    FA_EXPECT_STREQ(offline.m_message, _T("无法连接服务器"));
}

// SVD3：按组织树翻译显示名；查不到的有兜底名；非法类型被跳过。
void TestResolveServiceBindings()
{
    BeginCase("SVD3 原始绑定按组织树翻译成带显示名的绑定项");

    const OrgNode org = MakeSampleOrg();
    std::vector<RawServiceBinding> raw;
    raw.push_back(MakeRaw(0, 0));      // 全员
    raw.push_back(MakeRaw(1, 11));     // 研发部
    raw.push_back(MakeRaw(2, 501));    // 张三（在研发部下）
    raw.push_back(MakeRaw(2, 502));    // 李四（直接挂在公司根下）
    raw.push_back(MakeRaw(1, 99));     // 不存在的部门
    raw.push_back(MakeRaw(2, 777));    // 不存在的员工
    raw.push_back(MakeRaw(5, 1));      // 非法类型，应被跳过

    const std::vector<ServiceBinding> out = ResolveServiceBindings(raw, &org, _T("明远"));
    FA_EXPECT_EQ(out.size(), 6);
    if (out.size() == 6)
    {
        FA_EXPECT_TRUE(out[0].m_kind == BindingKind::Enterprise);
        FA_EXPECT_STREQ(out[0].m_id, _T("root"));
        FA_EXPECT_STREQ(out[0].m_label, _T("全员（明远）"));

        FA_EXPECT_TRUE(out[1].m_kind == BindingKind::Dept);
        FA_EXPECT_STREQ(out[1].m_id, _T("11"));
        FA_EXPECT_STREQ(out[1].m_label, _T("研发部"));

        FA_EXPECT_TRUE(out[2].m_kind == BindingKind::User);
        FA_EXPECT_STREQ(out[2].m_id, _T("501"));
        FA_EXPECT_STREQ(out[2].m_label, _T("张三"));

        FA_EXPECT_STREQ(out[3].m_label, _T("李四"));
        FA_EXPECT_STREQ(out[4].m_label, _T("部门 #99"));
        FA_EXPECT_STREQ(out[5].m_label, _T("员工 #777"));
    }

    // 部门 ID 与员工 ID 撞号时按类型区分：部门 501 不应被当成员工张三。
    std::vector<RawServiceBinding> clash;
    clash.push_back(MakeRaw(1, 501));
    const std::vector<ServiceBinding> c = ResolveServiceBindings(clash, &org, _T("明远"));
    FA_EXPECT_EQ(c.size(), 1);
    if (c.size() == 1)
    {
        FA_EXPECT_STREQ(c[0].m_label, _T("部门 #501"));
    }

    // 组织树尚未加载：全部用兜底名，但条目一个不少。
    const std::vector<ServiceBinding> noTree = ResolveServiceBindings(raw, nullptr, _T("明远"));
    FA_EXPECT_EQ(noTree.size(), 6);
    if (noTree.size() == 6)
    {
        FA_EXPECT_STREQ(noTree[1].m_label, _T("部门 #11"));
        FA_EXPECT_STREQ(noTree[2].m_label, _T("员工 #501"));
    }
}

// SVD4：重拉列表时保留已读到的绑定。
void TestCarryOverLoadedBindings()
{
    BeginCase("SVD4 重拉服务号列表后已读到的绑定与覆盖人数按 ID 保留");

    // 覆盖人数 m_subs 按绑定算出，列表接口同样不返回，须与绑定一起沿用。
    const int kKnownReach = 7;
    std::vector<Service> oldList;
    oldList.push_back(MakeService(_T("1"), true, _T("501")));   // 已读到、有一条绑定
    oldList.back().m_subs = kKnownReach;
    oldList.push_back(MakeService(_T("2"), true, nullptr));     // 已读到、确实没有绑定
    oldList.push_back(MakeService(_T("3"), false, nullptr));    // 还没读到

    std::vector<Service> newList;
    newList.push_back(MakeService(_T("2"), false, nullptr));
    newList.push_back(MakeService(_T("1"), false, nullptr));
    newList.push_back(MakeService(_T("3"), false, nullptr));
    newList.push_back(MakeService(_T("4"), false, nullptr));    // 新出现的服务号

    CarryOverLoadedBindings(oldList, newList);

    // 服务号 2：已读到且为空 —— 保持「已读到」，界面应显示「尚未绑定」。
    FA_EXPECT_TRUE(newList[0].m_bindingsLoaded);
    FA_EXPECT_EQ(newList[0].m_bindings.size(), 0);
    // 服务号 1：顺序变了也按 ID 对上，覆盖人数一并沿用。
    FA_EXPECT_TRUE(newList[1].m_bindingsLoaded);
    FA_EXPECT_EQ(newList[1].m_bindings.size(), 1);
    FA_EXPECT_EQ(newList[1].m_subs, kKnownReach);
    if (newList[1].m_bindings.size() == 1)
    {
        FA_EXPECT_STREQ(newList[1].m_bindings[0].m_id, _T("501"));
    }
    // 服务号 3：旧项本来就没读到，不能误标成「已读到」。
    FA_EXPECT_FALSE(newList[2].m_bindingsLoaded);
    // 服务号 4：新出现的，保持「未读到」。
    FA_EXPECT_FALSE(newList[3].m_bindingsLoaded);
    FA_EXPECT_EQ(newList[3].m_bindings.size(), 0);
}

// SVK2：集成型与类型 2 双向对应。
void TestIntegrationKindRoundTrip()
{
    BeginCase("SVK2 服务号「集成型」与服务端类型 2 双向对应");

    const HttpRequest save = MakeServiceSaveRequest(
        MakeServer(), _T("tk"), 0, 7, _T("告警"), ServiceKind::Integration,
        CString(), RGB(0, 0, 0), _T("警"), true);
    FA_EXPECT_TRUE(save.m_bodyUtf8.find("\"kind\":2") != std::string::npos);

    const HttpRequest assistant = MakeServiceSaveRequest(
        MakeServer(), _T("tk"), 0, 7, _T("IT"), ServiceKind::Assistant,
        CString(), RGB(0, 0, 0), _T("IT"), true);
    FA_EXPECT_TRUE(assistant.m_bodyUtf8.find("\"kind\":1") != std::string::npos);

    const ServiceListResult list = ParseServiceListResponse(MakeHttpOk(
        "{\"code\":0,\"list\":[{\"serviceid\":1,\"kind\":2},{\"serviceid\":2,\"kind\":0},"
        "{\"serviceid\":3,\"kind\":1}]}"));
    FA_EXPECT_EQ(list.m_list.size(), 3);
    if (list.m_list.size() == 3)
    {
        FA_EXPECT_TRUE(list.m_list[0].m_kind == ServiceKind::Integration);
        FA_EXPECT_TRUE(list.m_list[1].m_kind == ServiceKind::Notification);
        FA_EXPECT_TRUE(list.m_list[2].m_kind == ServiceKind::Assistant);
    }
}

} // anonymous namespace

// 运行本文件全部用例。
void RunAllServiceBindingTests()
{
    TestServiceDetailParsesBindings();
    TestServiceDetailFailurePaths();
    TestResolveServiceBindings();
    TestCarryOverLoadedBindings();
    TestIntegrationKindRoundTrip();
}

}} // namespace flamingoadmin::test
