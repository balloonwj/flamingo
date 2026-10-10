/**
 *  flamingoAdmin —— 企业问答（docs/群问答方案.md，接口见 docs/Admin控制台接口文档.md 模块 13）单元测试。
 *
 *    - EQA-1：请求构造（路径、新增与修改分开、应用范围为全部群时不带 groupids、ID 写成字符串、enterpriseid 为 0 时不带）；
 *    - EQA-2：应答解析（列表、群、写操作；160～163 的中文提示；传输失败与格式不对）与本地校验、应用范围文字；
 *    - EQA-3：编辑弹窗（新增时清空、编辑时预填并勾选、群列表晚到时保留勾选、所选的群才带群）；
 *    - EQA-4：企业问答页（企业管理员进入即拉问答与群、本地搜索、行内「编辑」「删除」与确认后发删除请求、保存结果）。
 *
 *  只调用纯函数与控件的数据接口，不发起网络请求、不调 OnPaint。
 *
 *  balloonwj@qq.com   2026-10-07
 */
#include "stdafx.h"
#include "Tests/TestFramework.h"
#include "Util/QaAdminApi.h"
#include "Screens/EnterpriseQaScreen.h"
#include "Screens/EnterpriseQaModal.h"
#include "json.h"

#include <string>
#include <vector>

namespace flamingoadmin { namespace test {

namespace {

using namespace flamingoadmin::qaapi;

// 测试用的企业ID与群ID
const long long kEnt = 9001;
const long long kGroupA = 268435457LL;
const long long kGroupB = 268435458LL;

// 测试用的界面矩形
const RECT kScreenRect = { 0, 0, 1200, 800 };

ServerConfig MakeServer()
{
    ServerConfig s;
    s.m_host = _T("127.0.0.1");
    s.m_port = 12345;
    s.m_useTls = false;
    return s;
}

HttpResult MakeHttpOk(const std::string& json)
{
    HttpResult r;
    r.m_transportOk = true;
    r.m_statusCode = 200;
    r.m_body = json;
    return r;
}

bool ParseBody(const HttpRequest& req, Json::Value& out)
{
    Json::CharReaderBuilder builder;
    std::string errs;
    Json::CharReader* reader = builder.newCharReader();
    const bool ok = reader->parse(req.m_bodyUtf8.c_str(), req.m_bodyUtf8.c_str() + req.m_bodyUtf8.size(), &out, &errs);
    delete reader;
    return ok && errs.empty() && out.isObject();
}

std::vector<QaGroup> TwoGroups()
{
    std::vector<QaGroup> groups(2);
    groups[0].m_id = kGroupA;
    groups[0].m_name = _T("研发群");
    groups[1].m_id = kGroupB;
    groups[1].m_name = _T("行政群");
    return groups;
}

// 记录页面发出的请求与界面请求
struct Captured
{
    std::vector<HttpRequest>                     m_requests;  // 请求出口收到的请求
    std::vector<EnterpriseQaScreen::UiRequest>   m_ui;        // 界面请求
};

void CaptureRequest(void* user, HttpRequest& request)
{
    static_cast<Captured*>(user)->m_requests.push_back(request);
}

void CaptureUi(void* user, const EnterpriseQaScreen::UiRequest& request)
{
    static_cast<Captured*>(user)->m_ui.push_back(request);
}

// 两条问答的列表应答
const char* const kTwoQaJson =
    u8"{\"code\":0,\"msg\":\"ok\",\"list\":["
    u8"{\"id\":\"11\",\"question\":\"WiFi 密码\",\"answer\":\"问前台\",\"scope\":0,\"groupids\":[],\"creatorname\":\"管理员\",\"updatetime\":1700000000000},"
    u8"{\"id\":\"12\",\"question\":\"报销流程\",\"answer\":\"见 https://oa.example.com\",\"scope\":1,\"groupids\":[\"268435457\"],\"creatorname\":\"管理员\",\"updatetime\":1700000000000}]}";

void TestQaRequests()
{
    BeginCase("EQA-1 企业问答的请求构造");
    const ServerConfig server = MakeServer();
    Json::Value body;

    HttpRequest list = MakeQaListRequest(server, _T("tok"), 0);
    FA_EXPECT_TRUE(list.m_path == L"/admin/qa/list.do");
    FA_EXPECT_TRUE(ParseBody(list, body));
    FA_EXPECT_FALSE(body.isMember("enterpriseid"));
    HttpRequest listPlat = MakeQaListRequest(server, _T("tok"), kEnt);
    FA_EXPECT_TRUE(ParseBody(listPlat, body));
    FA_EXPECT_TRUE(body["enterpriseid"].asInt64() == kEnt);

    QaItem item;
    item.m_question = _T("报销流程");
    item.m_answer = _T("见 OA");
    item.m_scope = kQaScopeAll;
    item.m_groupIds.push_back(kGroupA);   //全部群时即使带了群也不提交
    HttpRequest add = MakeQaSaveRequest(server, _T("tok"), 0, item);
    FA_EXPECT_TRUE(add.m_path == L"/admin/qa/add.do");
    FA_EXPECT_TRUE(ParseBody(add, body));
    FA_EXPECT_FALSE(body.isMember("id"));
    FA_EXPECT_FALSE(body.isMember("groupids"));
    FA_EXPECT_TRUE(body["question"].asString() == std::string(u8"报销流程"));
    FA_EXPECT_EQ(body["scope"].asInt(), kQaScopeAll);

    item.m_id = 12;
    item.m_scope = kQaScopeSelected;
    HttpRequest update = MakeQaSaveRequest(server, _T("tok"), 0, item);
    FA_EXPECT_TRUE(update.m_path == L"/admin/qa/update.do");
    FA_EXPECT_TRUE(ParseBody(update, body));
    FA_EXPECT_TRUE(body["id"].asString() == "12");
    FA_EXPECT_TRUE(body["groupids"].isArray());
    FA_EXPECT_EQ(static_cast<int>(body["groupids"].size()), 1);
    FA_EXPECT_TRUE(body["groupids"][0].asString() == std::to_string(kGroupA));

    HttpRequest del = MakeQaDeleteRequest(server, _T("tok"), 0, 12);
    FA_EXPECT_TRUE(del.m_path == L"/admin/qa/delete.do");
    FA_EXPECT_TRUE(ParseBody(del, body));
    FA_EXPECT_TRUE(body["id"].asString() == "12");
    FA_EXPECT_TRUE(MakeQaGroupsRequest(server, _T("tok"), 0).m_path == L"/admin/qa/groups.do");
}

void TestQaResponses()
{
    BeginCase("EQA-2 应答解析、错误提示、本地校验与应用范围文字");
    const QaListResult list = ParseQaListResponse(MakeHttpOk(kTwoQaJson));
    FA_EXPECT_TRUE(list.Succeeded());
    FA_EXPECT_EQ(static_cast<int>(list.m_items.size()), 2);
    FA_EXPECT_TRUE(list.m_items[1].m_scope == kQaScopeSelected);
    FA_EXPECT_EQ(static_cast<int>(list.m_items[1].m_groupIds.size()), 1);
    FA_EXPECT_STREQ(list.m_items[0].m_question, L"WiFi 密码");

    const QaGroupsResult groups = ParseQaGroupsResponse(MakeHttpOk(
        u8"{\"code\":0,\"msg\":\"ok\",\"list\":[{\"groupid\":\"268435457\",\"name\":\"研发群\"},{\"groupid\":\"0\"}]}"));
    FA_EXPECT_TRUE(groups.Succeeded());
    FA_EXPECT_EQ(static_cast<int>(groups.m_groups.size()), 1);

    const QaWriteResult added = ParseQaWriteResponse(MakeHttpOk("{\"code\":0,\"msg\":\"ok\",\"id\":\"15\"}"));
    FA_EXPECT_TRUE(added.Succeeded());
    FA_EXPECT_TRUE(added.m_id == 15);

    //160～163 各有说法
    const int codes[] = { 160, 161, 162, 163 };
    for (size_t i = 0; i < sizeof(codes) / sizeof(codes[0]); ++i)
    {
        const QaWriteResult r = ParseQaWriteResponse(MakeHttpOk("{\"code\":" + std::to_string(codes[i]) + ",\"msg\":\"x\"}"));
        FA_EXPECT_FALSE(r.Succeeded());
        FA_EXPECT_EQ(r.m_code, codes[i]);
        FA_EXPECT_TRUE(r.m_message.Find(_T("错误码")) < 0);
    }
    HttpResult broken;
    broken.m_transportOk = false;
    broken.m_error = _T("连不上");
    FA_EXPECT_FALSE(ParseQaListResponse(broken).m_transportOk);
    FA_EXPECT_FALSE(ParseQaListResponse(MakeHttpOk("not json")).Succeeded());

    //本地校验
    QaItem item;
    CString error;
    FA_EXPECT_FALSE(ValidateQa(item, &error));
    item.m_question = _T("问");
    FA_EXPECT_FALSE(ValidateQa(item, &error));
    item.m_answer = _T("答");
    FA_EXPECT_TRUE(ValidateQa(item, &error));
    item.m_scope = kQaScopeSelected;
    FA_EXPECT_FALSE(ValidateQa(item, &error));
    item.m_groupIds.push_back(kGroupA);
    FA_EXPECT_TRUE(ValidateQa(item, &error));
    CString longQ;
    for (int i = 0; i <= kQaQuestionMaxChars; ++i)
        longQ += _T("问");
    item.m_question = longQ;
    FA_EXPECT_FALSE(ValidateQa(item, &error));

    //应用范围文字
    QaItem all;
    FA_EXPECT_STREQ(QaScopeText(all, TwoGroups()), L"全部群");
    QaItem some;
    some.m_scope = kQaScopeSelected;
    some.m_groupIds.push_back(kGroupB);
    some.m_groupIds.push_back(999);
    const CString text = QaScopeText(some, TwoGroups());
    FA_EXPECT_TRUE(text.Find(_T("2 个群")) == 0);
    FA_EXPECT_TRUE(text.Find(_T("行政群")) > 0);
    FA_EXPECT_TRUE(text.Find(_T("999")) > 0);
}

void TestQaModal()
{
    BeginCase("EQA-3 编辑弹窗：新增、编辑预填、群列表晚到、所选的群");
    EnterpriseQaModal modal;
    modal.SetRect(kScreenRect);

    //新增：清空；群列表还没到
    modal.ShowForAdd(std::vector<QaGroup>());
    FA_EXPECT_FALSE(modal.IsEditing());
    FA_EXPECT_EQ(modal.GroupRowCount(), 0);
    CString error;
    FA_EXPECT_FALSE(modal.Validate(&error));

    //群列表到了：所选的群里勾一个
    modal.SetGroups(TwoGroups());
    FA_EXPECT_EQ(modal.GroupRowCount(), 2);
    modal.SetScope(kQaScopeSelected);
    FA_EXPECT_FALSE(modal.Validate(&error));   //问题答案都空
    modal.SetGroupRowChecked(1, true);
    QaItem item = modal.GetItem();
    FA_EXPECT_EQ(static_cast<int>(item.m_groupIds.size()), 1);
    FA_EXPECT_TRUE(item.m_groupIds[0] == kGroupB);
    //群列表再刷新一次，勾选保留
    modal.SetGroups(TwoGroups());
    FA_EXPECT_TRUE(modal.IsGroupRowChecked(1));
    FA_EXPECT_FALSE(modal.IsGroupRowChecked(0));
    //改回全部群：不带群
    modal.SetScope(kQaScopeAll);
    FA_EXPECT_TRUE(modal.GetItem().m_groupIds.empty());

    //编辑：预填并勾选
    QaItem edit;
    edit.m_id = 12;
    edit.m_question = _T("  报销流程 ");
    edit.m_answer = _T("见 OA");
    edit.m_scope = kQaScopeSelected;
    edit.m_groupIds.push_back(kGroupA);
    modal.ShowForEdit(edit, TwoGroups());
    FA_EXPECT_TRUE(modal.IsEditing());
    FA_EXPECT_TRUE(modal.EditingId() == 12);
    FA_EXPECT_TRUE(modal.IsGroupRowChecked(0));
    FA_EXPECT_FALSE(modal.IsGroupRowChecked(1));
    item = modal.GetItem();
    FA_EXPECT_STREQ(item.m_question, L"报销流程");
    FA_EXPECT_TRUE(modal.Validate(&error));
    modal.SetBusy(true);
    FA_EXPECT_TRUE(modal.IsBusy());
    modal.SetError(_T("出错了"));
    FA_EXPECT_STREQ(modal.ErrorText(), L"出错了");
}

void TestQaScreen()
{
    BeginCase("EQA-4 企业问答页：进入即拉取、本地搜索、编辑删除、保存结果");
    AdminSession::Instance().Set(_T("tok-qa"), 1, _T("企业管理员"), _T("ent@test.com"), kEnt);
    EnterpriseQaScreen screen;
    Captured captured;
    screen.SetRequestSink(&CaptureRequest, &captured);
    screen.SetUiRequestHandler(&CaptureUi, &captured);
    screen.SetRect(kScreenRect);

    //企业管理员进入：拉问答与群，不带 enterpriseid
    screen.OnEnter();
    FA_EXPECT_EQ(static_cast<int>(captured.m_requests.size()), 2);
    FA_EXPECT_TRUE(captured.m_requests[0].m_tag == static_cast<UINT_PTR>(kApiTagQaList));
    FA_EXPECT_TRUE(captured.m_requests[1].m_tag == static_cast<UINT_PTR>(kApiTagQaGroups));
    FA_EXPECT_EQ(screen.LoadStateValue(), static_cast<int>(EnterpriseQaScreen::kLoadLoading));

    screen.OnApiResult(kApiTagQaList, MakeHttpOk(kTwoQaJson));
    FA_EXPECT_EQ(screen.LoadStateValue(), static_cast<int>(EnterpriseQaScreen::kLoadLoaded));
    FA_EXPECT_EQ(static_cast<int>(screen.ShownRows().size()), 2);
    screen.OnApiResult(kApiTagQaGroups, MakeHttpOk(u8"{\"code\":0,\"msg\":\"ok\",\"list\":[{\"groupid\":\"268435457\",\"name\":\"研发群\"}]}"));
    FA_EXPECT_EQ(static_cast<int>(screen.Groups().size()), 1);

    //本地搜索：不区分大小写
    const std::vector<size_t> hit = EnterpriseQaScreen::FilterItems(screen.Items(), _T("wifi"));
    FA_EXPECT_EQ(static_cast<int>(hit.size()), 1);
    FA_EXPECT_EQ(static_cast<int>(EnterpriseQaScreen::FilterItems(screen.Items(), _T("OA.EXAMPLE")).size()), 1);
    FA_EXPECT_EQ(static_cast<int>(EnterpriseQaScreen::FilterItems(screen.Items(), _T(" ")).size()), 2);

    //行内「编辑」「删除」
    captured.m_ui.clear();
    screen.OnRowAction(1, 0);
    screen.OnRowAction(0, 1);
    FA_EXPECT_EQ(static_cast<int>(captured.m_ui.size()), 2);
    FA_EXPECT_EQ(captured.m_ui[0].m_kind, static_cast<int>(EnterpriseQaScreen::kUiOpenEditModal));
    FA_EXPECT_TRUE(captured.m_ui[0].m_item.m_id == 12);
    FA_EXPECT_EQ(captured.m_ui[1].m_kind, static_cast<int>(EnterpriseQaScreen::kUiConfirmDelete));

    //确认删除：发删除请求；取消则不发
    captured.m_requests.clear();
    screen.OnConfirmDelete(true);
    FA_EXPECT_EQ(static_cast<int>(captured.m_requests.size()), 1);
    FA_EXPECT_TRUE(captured.m_requests[0].m_path == L"/admin/qa/delete.do");
    captured.m_requests.clear();
    screen.OnRowAction(0, 1);
    screen.OnConfirmDelete(false);
    FA_EXPECT_TRUE(captured.m_requests.empty());

    //保存失败：弹窗里显示原因；成功：关弹窗并重拉
    QaItem item;
    item.m_question = _T("问");
    item.m_answer = _T("答");
    captured.m_ui.clear();
    screen.SubmitQa(item);
    FA_EXPECT_EQ(static_cast<int>(captured.m_requests.size()), 1);
    screen.OnApiResult(kApiTagQaSave, MakeHttpOk("{\"code\":162,\"msg\":\"full\"}"));
    FA_EXPECT_EQ(captured.m_ui.back().m_kind, static_cast<int>(EnterpriseQaScreen::kUiModalError));
    captured.m_requests.clear();
    captured.m_ui.clear();
    screen.SubmitQa(item);
    screen.OnApiResult(kApiTagQaSave, MakeHttpOk("{\"code\":0,\"msg\":\"ok\",\"id\":\"20\"}"));
    FA_EXPECT_EQ(captured.m_ui[0].m_kind, static_cast<int>(EnterpriseQaScreen::kUiCloseModal));
    //保存一次、重拉问答与群各一次
    FA_EXPECT_EQ(static_cast<int>(captured.m_requests.size()), 3);
}

} // namespace

void RunAllEnterpriseQaTests()
{
    TestQaRequests();
    TestQaResponses();
    TestQaModal();
    TestQaScreen();
    //恢复为未登录，免得影响之后的用例
    AdminSession::Instance().Clear();
}

}} // namespace flamingoadmin::test
