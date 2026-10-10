/**
 *  flamingoAdmin —— 敏感词管理界面（Screens/SensitiveWordsScreen）单元测试。
 *
 *    - SB-1：企业管理员进入页面不显示企业选择框、直接拉本企业词库；平台超管进入页面显示企业选择框，
 *            未选企业时提示「请先选择企业」、工具行不可用，选了企业后按该企业拉词库；
 *    - SB-2：列表结果交给页面后，行数、「已用 N / 10000」、总开关状态、覆盖提示都正确；
 *    - SB-3：点击某一行的「编辑」「删除」，界面请求里是正确的操作与词条；点在链接以外不触发；
 *    - SB-4：共 120 条、每页 50 条时翻页：共 3 页，越界不翻，翻页时按新页码请求；
 *    - SB-5：添加 / 编辑弹窗的本地校验与预填；服务端回 122 时请宿主在弹窗里显示「词条已存在」、不关弹窗，
 *            成功时关闭弹窗并重新拉列表；提交中再点不重复提交；
 *    - SB-6：关闭总开关后在确认框里点取消，开关拨回打开、不发请求；确认时发出 enabled=false；
 *    - SB-7：一页词条多到表格出现滚动条时，表头右侧让出同样的宽度，与数据行对齐；词条减少到
 *            不需要滚动条时恢复；
 *    - SB-8：换页、换筛选条件之后表格回到顶部；删除、修改之后按同样条件重新加载时保持滚动位置；
 *            添加成功后回到第 1 页顶部（新词条排在最前）。
 *
 *  页面的请求出口与界面请求处理函数都换成记录下来，测试不碰网络、不弹窗口。
 *
 *  balloonwj@qq.com   2026-10-02
 */
#include "stdafx.h"
#include "Tests/TestFramework.h"
#include "Screens/SensitiveWordsScreen.h"
#include "Screens/SensitiveWordModal.h"
#include "Widgets/ActionTable.h"
#include "Widgets/ColumnHeader.h"
#include "Util/SensitiveWordText.h"
#include "Controls/Input/DuiSwitch.h"
#include "json.h"

#include <string>
#include <vector>

namespace flamingoadmin { namespace test {

namespace {

using namespace flamingoadmin::adminapi;

// 测试用的界面矩形：足够把页头、页签、工具行、表格与分页条都排开
const RECT kScreenRect = { 0, 0, 1200, 800 };

// 测试用的两个企业 ID
const long long kEntA = 9001;
const long long kEntB = 9002;

// 记录页面发出的请求与界面请求
struct Captured
{
    std::vector<HttpRequest>                       m_requests;  // 请求出口收到的请求
    std::vector<SensitiveWordsScreen::UiRequest>   m_ui;        // 界面请求
};

void CaptureRequest(void* user, HttpRequest& request)
{
    static_cast<Captured*>(user)->m_requests.push_back(request);
}

void CaptureUi(void* user, const SensitiveWordsScreen::UiRequest& request)
{
    static_cast<Captured*>(user)->m_ui.push_back(request);
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
    const bool ok = reader->parse(req.m_bodyUtf8.c_str(), req.m_bodyUtf8.c_str() + req.m_bodyUtf8.size(),
                                  &out, &errs);
    delete reader;
    return ok && errs.empty() && out.isObject();
}

// 把页面接到记录器上，并排好版
void Setup(SensitiveWordsScreen& screen, Captured& captured)
{
    screen.SetRequestSink(&CaptureRequest, &captured);
    screen.SetUiRequestHandler(&CaptureUi, &captured);
    screen.SetRect(kScreenRect);
}

// 以企业管理员身份登录（企业 kEntA）
void LoginAsEnterpriseAdmin()
{
    AdminSession::Instance().Set(_T("tok-ent"), 1, _T("企业管理员"), _T("ent@test.com"), kEntA);
}

// 以平台超管身份登录
void LoginAsPlatformAdmin()
{
    AdminSession::Instance().Set(_T("tok-plat"), 2, _T("平台超管"), _T("plat@test.com"), 0);
}

// 一页词库列表应答：两条词条
std::string TwoWordsJson(int total, int enabled)
{
    return std::string("{\"code\":0,\"msg\":\"ok\",\"total\":") + std::to_string(total)
        + ",\"enabled\":" + std::to_string(enabled) + ",\"wordcount\":1234,\"list\":["
        + u8"{\"id\":11,\"word\":\"敏感\",\"category\":1,\"remark\":\"备注\",\"creatorname\":\"管理员\","
          u8"\"createtime\":1700000000000,\"updatetime\":1700000000000},"
          u8"{\"id\":12,\"word\":\"广告\",\"category\":5,\"remark\":\"\",\"creatorname\":\"管理员\","
          u8"\"createtime\":1700000000000,\"updatetime\":1700000000000}]}";
}

// ManyWordsJson 造出的第一条词条的 ID，其后依次加 1
const long long kManyFirstId = 100;

// 分类筛选框里「色情低俗」的下标：第 0 项是「全部分类」，其后按分类取值 1～6 排列
const int kFilterIndexVulgar = 2;

// SB-7 用的矮界面：放不下两行数据，表格必然出现滚动条
const RECT kShortScreenRect = { 0, 0, 1200, 360 };

// 一页词库列表应答：count 条词条，总数为 total
std::string ManyWordsJson(int count, int total)
{
    std::string json = std::string("{\"code\":0,\"msg\":\"ok\",\"total\":") + std::to_string(total)
        + ",\"enabled\":1,\"wordcount\":" + std::to_string(count) + ",\"list\":[";
    for (int i = 0; i < count; ++i)
    {
        if (i > 0)
            json += ",";
        json += "{\"id\":" + std::to_string(kManyFirstId + i) + ",\"word\":\"w" + std::to_string(i)
            + "\",\"category\":" + std::to_string(swtext::kCategoryOther)
            + ",\"remark\":\"\",\"creatorname\":\"a\","
              "\"createtime\":1700000000000,\"updatetime\":1700000000000}";
    }
    json += "]}";
    return json;
}

POINT CenterOf(const RECT& rc)
{
    POINT pt = { (rc.left + rc.right) / 2, (rc.top + rc.bottom) / 2 };
    return pt;
}

} // namespace

void TestSwScreenRoles()
{
    BeginCase("SB-1 企业管理员不显示企业选择框；平台超管先选企业再拉词库");

    LoginAsEnterpriseAdmin();
    {
        SensitiveWordsScreen screen;
        Captured c;
        Setup(screen, c);
        screen.OnEnter();
        FA_EXPECT_FALSE(screen.IsEnterprisePickerVisible());
        FA_EXPECT_TRUE(screen.IsToolbarEnabled());
        FA_EXPECT_STREQ(screen.OverlayText(), _T("加载中…"));
        FA_EXPECT_EQ(c.m_requests.size(), 1);
        if (c.m_requests.size() == 1)
        {
            Json::Value body;
            FA_EXPECT_TRUE(c.m_requests[0].m_path == L"/admin/sensitive/list.do");
            FA_EXPECT_EQ(c.m_requests[0].m_tag, kApiTagSwList);
            FA_EXPECT_TRUE(ParseBody(c.m_requests[0], body));
            //企业管理员不带 enterpriseid，由服务端改写为本企业
            FA_EXPECT_FALSE(body.isMember("enterpriseid"));
            FA_EXPECT_EQ(body["page"].asInt(), 1);
            FA_EXPECT_EQ(body["pagesize"].asInt(), SensitiveWordsScreen::kPageSize);
        }
    }

    LoginAsPlatformAdmin();
    {
        SensitiveWordsScreen screen;
        Captured c;
        Setup(screen, c);
        screen.OnEnter();
        FA_EXPECT_TRUE(screen.IsEnterprisePickerVisible());
        FA_EXPECT_FALSE(screen.IsToolbarEnabled());
        FA_EXPECT_STREQ(screen.OverlayText(), _T("请先选择企业"));
        //先拉企业列表，不拉词库
        FA_EXPECT_EQ(c.m_requests.size(), 1);
        if (c.m_requests.size() == 1)
        {
            FA_EXPECT_TRUE(c.m_requests[0].m_path == L"/admin/enterprise/list.do");
            FA_EXPECT_EQ(c.m_requests[0].m_tag, kApiTagSwEnterprises);
        }

        screen.OnApiResult(kApiTagSwEnterprises, MakeHttpOk(
            u8"{\"code\":0,\"msg\":\"ok\",\"total\":2,\"list\":[{\"enterpriseid\":9001,\"name\":\"甲公司\"},"
            u8"{\"enterpriseid\":9002,\"name\":\"乙公司\"}]}"));
        FA_EXPECT_EQ(screen.EnterpriseCount(), 2);
        FA_EXPECT_STREQ(screen.OverlayText(), _T("请先选择企业"));
        FA_EXPECT_EQ(c.m_requests.size(), 1);

        //越界的下标不做任何事
        screen.OnEnterpriseSelected(5);
        FA_EXPECT_EQ(c.m_requests.size(), 1);

        screen.OnEnterpriseSelected(1);
        FA_EXPECT_TRUE(screen.CurrentEnterpriseId() == kEntB);
        FA_EXPECT_TRUE(screen.IsToolbarEnabled());
        FA_EXPECT_STREQ(screen.OverlayText(), _T("加载中…"));
        FA_EXPECT_EQ(c.m_requests.size(), 2);
        if (c.m_requests.size() == 2)
        {
            Json::Value body;
            FA_EXPECT_TRUE(c.m_requests[1].m_path == L"/admin/sensitive/list.do");
            FA_EXPECT_TRUE(ParseBody(c.m_requests[1], body));
            FA_EXPECT_TRUE(body["enterpriseid"].asInt64() == kEntB);
        }
    }
}

void TestSwScreenListResult()
{
    BeginCase("SB-2 列表结果：行数、「已用 N / 10000」、总开关、覆盖提示");

    LoginAsEnterpriseAdmin();
    SensitiveWordsScreen screen;
    Captured c;
    Setup(screen, c);
    screen.OnEnter();

    screen.OnApiResult(kApiTagSwList, MakeHttpOk(TwoWordsJson(2, 1)));
    FA_EXPECT_EQ(screen.RowCount(), 2);
    FA_EXPECT_STREQ(screen.UsageText(), _T("已用 1234 / 10000"));
    FA_EXPECT_TRUE(screen.IsSwitchChecked());
    FA_EXPECT_STREQ(screen.OverlayText(), _T(""));

    //总开关关闭、没有词条
    screen.OnApiResult(kApiTagSwList, MakeHttpOk(
        "{\"code\":0,\"msg\":\"ok\",\"total\":0,\"enabled\":0,\"wordcount\":0,\"list\":[]}"));
    FA_EXPECT_EQ(screen.RowCount(), 0);
    FA_EXPECT_FALSE(screen.IsSwitchChecked());
    FA_EXPECT_STREQ(screen.OverlayText(), _T("暂无敏感词"));

    //加载失败：覆盖提示说明失败
    screen.OnApiResult(kApiTagSwList, MakeHttpOk("{\"code\":1,\"msg\":\"x\"}"));
    FA_EXPECT_TRUE(screen.OverlayText().Find(_T("加载失败")) == 0);
}

void TestSwScreenRowActions()
{
    BeginCase("SB-3 点击行内「编辑」「删除」：界面请求里是正确的操作与词条");

    LoginAsEnterpriseAdmin();
    SensitiveWordsScreen screen;
    Captured c;
    Setup(screen, c);
    screen.OnEnter();
    screen.OnApiResult(kApiTagSwList, MakeHttpOk(TwoWordsJson(2, 1)));

    ui::ActionTable* table = screen.WordTable();
    FA_EXPECT_NOTNULL(table);
    if (table == nullptr)
        return;

    //第 2 行的「编辑」
    table->OnLButtonUp(CenterOf(table->GetLinkRect(1, 0)), 0);
    FA_EXPECT_EQ(c.m_ui.size(), 1);
    if (c.m_ui.size() == 1)
    {
        FA_EXPECT_EQ(c.m_ui[0].m_kind, SensitiveWordsScreen::kUiOpenEditModal);
        FA_EXPECT_TRUE(c.m_ui[0].m_word.m_id == 12);
        FA_EXPECT_STREQ(c.m_ui[0].m_word.m_word, _T("广告"));
    }

    //第 1 行的「删除」：先请宿主弹确认框，不直接发请求
    const size_t requestsBefore = c.m_requests.size();
    table->OnLButtonUp(CenterOf(table->GetLinkRect(0, 1)), 0);
    FA_EXPECT_EQ(c.m_ui.size(), 2);
    if (c.m_ui.size() == 2)
    {
        FA_EXPECT_EQ(c.m_ui[1].m_kind, SensitiveWordsScreen::kUiConfirmDelete);
        FA_EXPECT_TRUE(c.m_ui[1].m_word.m_id == 11);
    }
    FA_EXPECT_EQ(c.m_requests.size(), requestsBefore);

    //点在链接以外：不触发
    const RECT edit = table->GetLinkRect(0, 0);
    const POINT outside = { table->GetRect().left + 20, CenterOf(edit).y };
    table->OnLButtonUp(outside, 0);
    FA_EXPECT_EQ(c.m_ui.size(), 2);
}

void TestSwScreenPaging()
{
    BeginCase("SB-4 翻页：共 120 条、每页 50 条时共 3 页，越界不翻");

    LoginAsEnterpriseAdmin();
    SensitiveWordsScreen screen;
    Captured c;
    Setup(screen, c);
    screen.OnEnter();
    screen.OnApiResult(kApiTagSwList, MakeHttpOk(TwoWordsJson(120, 1)));
    FA_EXPECT_EQ(screen.Page(), 1);
    FA_EXPECT_EQ(screen.PageCount(), 3);

    //第 1 页不能往前翻
    size_t before = c.m_requests.size();
    screen.GoToPage(0);
    FA_EXPECT_EQ(c.m_requests.size(), before);

    //翻到第 2 页：按新页码请求
    screen.GoToPage(2);
    FA_EXPECT_EQ(c.m_requests.size(), before + 1);
    if (c.m_requests.size() == before + 1)
    {
        Json::Value body;
        FA_EXPECT_TRUE(ParseBody(c.m_requests.back(), body));
        FA_EXPECT_EQ(body["page"].asInt(), 2);
    }
    screen.OnApiResult(kApiTagSwList, MakeHttpOk(TwoWordsJson(120, 1)));
    FA_EXPECT_EQ(screen.Page(), 2);

    screen.GoToPage(3);
    screen.OnApiResult(kApiTagSwList, MakeHttpOk(TwoWordsJson(120, 1)));
    FA_EXPECT_EQ(screen.Page(), 3);

    //第 3 页不能往后翻
    before = c.m_requests.size();
    screen.GoToPage(4);
    FA_EXPECT_EQ(c.m_requests.size(), before);
}

void TestSwModalAndSave()
{
    BeginCase("SB-5 弹窗：词条为空时本地提示、不发请求；服务端回 122 时弹窗不关、显示「词条已存在」");

    //弹窗的本地校验
    SensitiveWordModal modal;
    modal.ShowForAdd();
    CString error;
    FA_EXPECT_FALSE(modal.Validate(&error));
    FA_EXPECT_STREQ(error, _T("请输入词条"));
    FA_EXPECT_EQ(modal.GetCategory(), 6);
    FA_EXPECT_FALSE(modal.IsEditing());

    SensitiveWordItem item;
    item.m_id = 12;
    item.m_word = _T("广告");
    item.m_category = 5;
    item.m_remark = _T("  备注  ");
    modal.ShowForEdit(item);
    FA_EXPECT_TRUE(modal.Validate(&error));
    FA_EXPECT_TRUE(modal.IsEditing());
    FA_EXPECT_TRUE(modal.EditingId() == 12);
    FA_EXPECT_STREQ(modal.GetWord(), _T("广告"));
    FA_EXPECT_EQ(modal.GetCategory(), 5);
    FA_EXPECT_STREQ(modal.GetRemark(), _T("备注"));

    //提交中「保存」按钮不可用；错误提示可以设置与清除
    modal.SetBusy(true);
    FA_EXPECT_TRUE(modal.IsBusy());
    modal.SetError(_T("词条已存在"));
    FA_EXPECT_STREQ(modal.ErrorText(), _T("词条已存在"));
    modal.ShowForAdd();
    FA_EXPECT_FALSE(modal.IsBusy());
    FA_EXPECT_TRUE(modal.ErrorText().IsEmpty());

    //页面：提交后服务端回 122 —— 请宿主在弹窗里显示错误，不关闭弹窗
    LoginAsEnterpriseAdmin();
    SensitiveWordsScreen screen;
    Captured c;
    Setup(screen, c);
    screen.OnEnter();
    screen.OnApiResult(kApiTagSwList, MakeHttpOk(TwoWordsJson(2, 1)));

    const size_t before = c.m_requests.size();
    screen.SubmitWord(0, _T("重复"), swtext::kCategoryOther, CString());
    FA_EXPECT_EQ(c.m_requests.size(), before + 1);
    if (c.m_requests.size() == before + 1)
        FA_EXPECT_TRUE(c.m_requests.back().m_path == L"/admin/sensitive/add.do");

    //提交还没回来时再点一次：忽略
    screen.SubmitWord(0, _T("重复"), swtext::kCategoryOther, CString());
    FA_EXPECT_EQ(c.m_requests.size(), before + 1);

    c.m_ui.clear();
    screen.OnApiResult(kApiTagSwAdd, MakeHttpOk("{\"code\":122,\"msg\":\"x\"}"));
    bool sawError = false;
    bool sawClose = false;
    for (size_t i = 0; i < c.m_ui.size(); ++i)
    {
        if (c.m_ui[i].m_kind == SensitiveWordsScreen::kUiModalError && c.m_ui[i].m_text == _T("词条已存在"))
            sawError = true;
        if (c.m_ui[i].m_kind == SensitiveWordsScreen::kUiCloseModal)
            sawClose = true;
    }
    FA_EXPECT_TRUE(sawError);
    FA_EXPECT_FALSE(sawClose);

    //改了再提交、这次成功：关闭弹窗并重新拉列表
    screen.SubmitWord(0, _T("新词"), swtext::kCategoryOther, CString());
    c.m_ui.clear();
    const size_t beforeOk = c.m_requests.size();
    screen.OnApiResult(kApiTagSwAdd, MakeHttpOk("{\"code\":0,\"msg\":\"ok\",\"id\":13}"));
    sawClose = false;
    for (size_t i = 0; i < c.m_ui.size(); ++i)
    {
        if (c.m_ui[i].m_kind == SensitiveWordsScreen::kUiCloseModal)
            sawClose = true;
    }
    FA_EXPECT_TRUE(sawClose);
    FA_EXPECT_EQ(c.m_requests.size(), beforeOk + 1);
    if (c.m_requests.size() == beforeOk + 1)
        FA_EXPECT_TRUE(c.m_requests.back().m_path == L"/admin/sensitive/list.do");
}

void TestSwSwitchCancel()
{
    BeginCase("SB-6 关闭总开关后在确认框里点取消：开关拨回打开，不发请求");

    LoginAsEnterpriseAdmin();
    SensitiveWordsScreen screen;
    Captured c;
    Setup(screen, c);
    screen.OnEnter();
    screen.OnApiResult(kApiTagSwList, MakeHttpOk(TwoWordsJson(2, 1)));
    FA_EXPECT_TRUE(screen.IsSwitchChecked());

    balloonwjui::DuiSwitch* sw = screen.EnabledSwitch();
    FA_EXPECT_NOTNULL(sw);
    if (sw == nullptr)
        return;

    //用户把开关拨到关：开关自己先翻转，再通知页面
    const size_t before = c.m_requests.size();
    sw->SetChecked(false, false, false);
    c.m_ui.clear();
    screen.OnSwitchToggled(false);
    FA_EXPECT_EQ(c.m_ui.size(), 1);
    if (c.m_ui.size() == 1)
        FA_EXPECT_EQ(c.m_ui[0].m_kind, SensitiveWordsScreen::kUiConfirmDisable);
    FA_EXPECT_EQ(c.m_requests.size(), before);

    screen.OnConfirmResult(SensitiveWordsScreen::kUiConfirmDisable, false);
    FA_EXPECT_TRUE(screen.IsSwitchChecked());
    FA_EXPECT_EQ(c.m_requests.size(), before);

    //再拨一次、这次确认：发出 enabled=false 的请求
    sw->SetChecked(false, false, false);
    screen.OnSwitchToggled(false);
    screen.OnConfirmResult(SensitiveWordsScreen::kUiConfirmDisable, true);
    FA_EXPECT_EQ(c.m_requests.size(), before + 1);
    if (c.m_requests.size() == before + 1)
    {
        Json::Value body;
        FA_EXPECT_TRUE(c.m_requests.back().m_path == L"/admin/sensitive/setenabled.do");
        FA_EXPECT_TRUE(ParseBody(c.m_requests.back(), body));
        FA_EXPECT_TRUE(body["enabled"].isBool() && !body["enabled"].asBool());
    }
}

void TestSwScreenHeaderAlignment()
{
    BeginCase("SB-7 词条多到出现滚动条时，表头让出滚动条宽度；词条减少后恢复");

    LoginAsEnterpriseAdmin();
    SensitiveWordsScreen screen;
    Captured c;
    Setup(screen, c);
    screen.OnEnter();

    ui::ActionTable* table = screen.WordTable();
    ui::ColumnHeader* header = screen.WordHeader();
    FA_EXPECT_NOTNULL(table);
    FA_EXPECT_NOTNULL(header);
    if (table == nullptr || header == nullptr)
        return;

    //一页 50 条：远超界面高度能放下的行数
    screen.OnApiResult(kApiTagSwList, MakeHttpOk(ManyWordsJson(SensitiveWordsScreen::kPageSize, SensitiveWordsScreen::kPageSize)));
    FA_EXPECT_EQ(screen.RowCount(), SensitiveWordsScreen::kPageSize);
    FA_EXPECT_TRUE(table->ScrollBarWidth() > 0);
    FA_EXPECT_EQ(header->TrailingInset(), table->ScrollBarWidth());

    //只剩两条：不需要滚动条，表头恢复整宽
    screen.OnApiResult(kApiTagSwList, MakeHttpOk(TwoWordsJson(2, 1)));
    FA_EXPECT_EQ(table->ScrollBarWidth(), 0);
    FA_EXPECT_EQ(header->TrailingInset(), 0);

    //界面变矮到放不下两行时同样要让出：滚动条由排版决定显隐，表头随之更新
    screen.SetRect(kShortScreenRect);
    FA_EXPECT_TRUE(table->ScrollBarWidth() > 0);
    FA_EXPECT_EQ(header->TrailingInset(), table->ScrollBarWidth());
}

void TestSwScreenScrollReset()
{
    BeginCase("SB-8 换页、换筛选条件、添加成功后表格回到顶部；删除、修改之后保持滚动位置");

    //共 120 条、每页 50 条；滚动到第 300 像素处模拟管理员已经往下翻了一段
    const int kTotal = 120;
    const int kScrolled = 300;
    const std::string page = ManyWordsJson(SensitiveWordsScreen::kPageSize, kTotal);

    LoginAsEnterpriseAdmin();
    SensitiveWordsScreen screen;
    Captured c;
    Setup(screen, c);
    screen.OnEnter();
    screen.OnApiResult(kApiTagSwList, MakeHttpOk(page));
    ui::ActionTable* table = screen.WordTable();
    FA_EXPECT_NOTNULL(table);
    if (table == nullptr)
        return;
    table->SetScrollPos(kScrolled);
    FA_EXPECT_EQ(table->GetScrollPos(), kScrolled);

    //删除第 1 行：确认、删除成功后按同样条件重新加载，滚动位置不变
    table->OnLButtonUp(CenterOf(table->GetLinkRect(0, 1)), 0);
    screen.OnConfirmResult(SensitiveWordsScreen::kUiConfirmDelete, true);
    screen.OnApiResult(kApiTagSwDelete, MakeHttpOk("{\"code\":0,\"msg\":\"ok\",\"deleted\":1}"));
    FA_EXPECT_TRUE(!c.m_requests.empty() && c.m_requests.back().m_tag == kApiTagSwList);
    screen.OnApiResult(kApiTagSwList, MakeHttpOk(page));
    FA_EXPECT_EQ(table->GetScrollPos(), kScrolled);

    //修改词条后同样保持
    screen.SubmitWord(kManyFirstId, _T("w0x"), swtext::kCategoryOther, CString());
    screen.OnApiResult(kApiTagSwUpdate, MakeHttpOk("{\"code\":0,\"msg\":\"ok\"}"));
    screen.OnApiResult(kApiTagSwList, MakeHttpOk(page));
    FA_EXPECT_EQ(table->GetScrollPos(), kScrolled);

    //翻到第 2 页：回到顶部
    screen.GoToPage(2);
    screen.OnApiResult(kApiTagSwList, MakeHttpOk(page));
    FA_EXPECT_EQ(screen.Page(), 2);
    FA_EXPECT_EQ(table->GetScrollPos(), 0);

    //换分类筛选：回到顶部
    table->SetScrollPos(kScrolled);
    screen.OnCategoryChanged(kFilterIndexVulgar);
    screen.OnApiResult(kApiTagSwList, MakeHttpOk(page));
    FA_EXPECT_EQ(table->GetScrollPos(), 0);

    //在第 2 页添加成功：回到第 1 页并回到顶部
    screen.GoToPage(2);
    screen.OnApiResult(kApiTagSwList, MakeHttpOk(page));
    table->SetScrollPos(kScrolled);
    screen.SubmitWord(0, _T("新词"), swtext::kCategoryOther, CString());
    screen.OnApiResult(kApiTagSwAdd, MakeHttpOk("{\"code\":0,\"msg\":\"ok\",\"id\":999}"));
    FA_EXPECT_TRUE(!c.m_requests.empty() && c.m_requests.back().m_tag == kApiTagSwList);
    if (!c.m_requests.empty())
    {
        Json::Value body;
        FA_EXPECT_TRUE(ParseBody(c.m_requests.back(), body));
        FA_EXPECT_EQ(body["page"].asInt(), 1);
    }
    screen.OnApiResult(kApiTagSwList, MakeHttpOk(page));
    FA_EXPECT_EQ(screen.Page(), 1);
    FA_EXPECT_EQ(table->GetScrollPos(), 0);
}

void RunAllSensitiveWordsScreenTests()
{
    TestSwScreenRoles();
    TestSwScreenListResult();
    TestSwScreenRowActions();
    TestSwScreenPaging();
    TestSwModalAndSave();
    TestSwSwitchCancel();
    TestSwScreenHeaderAlignment();
    TestSwScreenScrollReset();
    //恢复为未登录，免得影响之后的用例
    AdminSession::Instance().Clear();
}

}} // namespace flamingoadmin::test
