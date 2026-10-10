/**
 *  flamingoAdmin —— 企业列表「更多筛选」的单元测试（ELF5）。
 *
 *  企业管理界面的「更多筛选」原先只弹「后续阶段开放」。2026-10-04 起服务端 enterprise/list.do
 *  与 export.do 增加三项可选条件（行业、套餐、开通起止日期），服务端的筛选结果由服务端测试
 *  ELF1～ELF4 覆盖。本文件覆盖控制台一侧的纯函数（Util/AdminApi.h）：
 *    - ELF5：请求里只带已设置的条件 —— 未设置的行业、套餐、日期不出现在请求体里，格式不对的
 *            日期同样不带；导出请求与列表请求带同一组条件；「更多筛选」按钮文字按已设置的
 *            条件数显示；日期格式的判断规则。
 *    - PLN1：套餐编号与名称双向映射。2026-10-04 起统一为服务端的三档编号，名称为 0 基础版 /
 *            1 专业版 / 2 旗舰版（产品本身就是企业版 IM，2 号套餐不再叫「企业版」；控制台原先
 *            自行约定的编号 3 不再使用）；列表外的编号显示成「套餐N」且能还原成 N，企业设置弹窗
 *            据此不会把它悄悄改成别的套餐。
 *    - IND1：企业设置弹窗的「行业」改为与新增企业共用选项的下拉框后，列表外的行业、空行业与
 *            列表外的套餐都原样取回，保存时不会被悄悄改掉；重复打开时不残留上一次的额外项。
 *    - ACP6（2026-10-06）：企业设置弹窗的账号前缀输入框按企业现有前缀填入、原样取回，换企业时不残留，
 *            规则不合格时校验不过。
 *
 *  balloonwj@qq.com   2026-10-04
 */
#include "stdafx.h"
#include "Tests/TestFramework.h"
#include "Util/AdminApi.h"
#include "Screens/EnterpriseSettingsModal.h"

#include <string>

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

// 请求体里是否出现某个键（形如 "industry":）。
//   body：UTF-8 请求体；key：键名。
bool HasKey(const std::string& body, const char* key)
{
    return body.find(std::string("\"") + key + "\":") != std::string::npos;
}

// ELF5：请求只带已设置的条件。
void TestRequestCarriesOnlySetConditions()
{
    BeginCase("ELF5 企业列表请求只带已设置的筛选条件，按钮文字显示条件数");

    const int kPage = 1;
    const int kPageSize = 20;
    const int kPlanEnterprise = 2;
    const int kStatusActive = 1;

    // 没有设置任何「更多筛选」条件：请求与原来一样，只有关键字、状态与分页。
    EnterpriseListFilter none;
    const HttpRequest plain = MakeEnterpriseListRequest(MakeServer(), _T("tk"), none, kPage, kPageSize);
    FA_EXPECT_TRUE(plain.m_path == L"/admin/enterprise/list.do");
    FA_EXPECT_TRUE(HasKey(plain.m_bodyUtf8, "keyword"));
    FA_EXPECT_TRUE(plain.m_bodyUtf8.find("\"status\":-1") != std::string::npos);
    FA_EXPECT_FALSE(HasKey(plain.m_bodyUtf8, "industry"));
    FA_EXPECT_FALSE(HasKey(plain.m_bodyUtf8, "plan"));
    FA_EXPECT_FALSE(HasKey(plain.m_bodyUtf8, "openfrom"));
    FA_EXPECT_FALSE(HasKey(plain.m_bodyUtf8, "opento"));
    FA_EXPECT_EQ(CountMoreFilters(none), 0);
    FA_EXPECT_STREQ(MoreFilterButtonText(CountMoreFilters(none)), _T("更多筛选"));

    // 四项都设置：全部带上。
    EnterpriseListFilter all;
    all.m_keyword  = _T("科技");
    all.m_status   = kStatusActive;
    all.m_industry = _T("金融服务");
    all.m_plan     = kPlanEnterprise;
    all.m_openFrom = _T("2026-03-01");
    all.m_openTo   = _T("2026-03-31");
    const HttpRequest full = MakeEnterpriseListRequest(MakeServer(), _T("tk"), all, kPage, kPageSize);
    FA_EXPECT_TRUE(full.m_bodyUtf8.find("\"status\":1") != std::string::npos);
    FA_EXPECT_TRUE(HasKey(full.m_bodyUtf8, "industry"));
    FA_EXPECT_TRUE(full.m_bodyUtf8.find("\"plan\":2") != std::string::npos);
    FA_EXPECT_TRUE(full.m_bodyUtf8.find("\"openfrom\":\"2026-03-01\"") != std::string::npos);
    FA_EXPECT_TRUE(full.m_bodyUtf8.find("\"opento\":\"2026-03-31\"") != std::string::npos);
    FA_EXPECT_EQ(CountMoreFilters(all), 4);
    FA_EXPECT_STREQ(MoreFilterButtonText(CountMoreFilters(all)), _T("更多筛选 · 4"));

    // 导出请求带同一组条件。
    const HttpRequest exp = MakeEnterpriseExportRequest(MakeServer(), _T("tk"), all);
    FA_EXPECT_TRUE(exp.m_path == L"/admin/enterprise/export.do");
    FA_EXPECT_TRUE(HasKey(exp.m_bodyUtf8, "industry"));
    FA_EXPECT_TRUE(exp.m_bodyUtf8.find("\"plan\":2") != std::string::npos);
    FA_EXPECT_TRUE(exp.m_bodyUtf8.find("\"openfrom\":\"2026-03-01\"") != std::string::npos);
    FA_EXPECT_TRUE(exp.m_bodyUtf8.find("\"opento\":\"2026-03-31\"") != std::string::npos);

    // 格式不对的日期（输入到一半、分隔符不对、月份越界）不带，也不计入条件数。
    EnterpriseListFilter partial;
    partial.m_plan     = kPlanEnterprise;
    partial.m_openFrom = _T("2026-03");
    partial.m_openTo   = _T("2026/03/31");
    const HttpRequest half = MakeEnterpriseListRequest(MakeServer(), _T("tk"), partial, kPage, kPageSize);
    FA_EXPECT_FALSE(HasKey(half.m_bodyUtf8, "openfrom"));
    FA_EXPECT_FALSE(HasKey(half.m_bodyUtf8, "opento"));
    FA_EXPECT_TRUE(half.m_bodyUtf8.find("\"plan\":2") != std::string::npos);
    FA_EXPECT_EQ(CountMoreFilters(partial), 1);

    // 日期格式规则。
    FA_EXPECT_TRUE(IsValidDateText(_T("2026-10-04")));
    FA_EXPECT_TRUE(IsValidDateText(_T("2026-12-31")));
    FA_EXPECT_FALSE(IsValidDateText(_T("")));
    FA_EXPECT_FALSE(IsValidDateText(_T("2026-1-4")));
    FA_EXPECT_FALSE(IsValidDateText(_T("2026-13-01")));
    FA_EXPECT_FALSE(IsValidDateText(_T("2026-00-10")));
    FA_EXPECT_FALSE(IsValidDateText(_T("2026-10-32")));
    FA_EXPECT_FALSE(IsValidDateText(_T("2026-10-04 ")));

    // 行业选项与新增企业弹窗共用一份，至少包含新增企业时的默认行业。
    FA_EXPECT_TRUE(kIndustryOptionCount > 0);
    FA_EXPECT_STREQ(kIndustryOptions[0], _T("软件与信息技术"));
}

// PLN1：套餐编号与名称双向映射。
void TestPlanMapping()
{
    BeginCase("PLN1 套餐编号与名称双向映射：0 基础版 / 1 专业版 / 2 旗舰版，列表外的编号可还原");

    const int kPlanBasic        = 0;
    const int kPlanProfessional = 1;
    const int kPlanFlagship     = 2;
    const int kPlanUnknown      = 3;   // 控制台原先自行约定的编号，现已不在列表中

    FA_EXPECT_EQ(kPlanOptionCount, 3);
    FA_EXPECT_STREQ(PlanNameFromInt(kPlanBasic), _T("基础版"));
    FA_EXPECT_STREQ(PlanNameFromInt(kPlanProfessional), _T("专业版"));
    FA_EXPECT_STREQ(PlanNameFromInt(kPlanFlagship), _T("旗舰版"));
    for (int i = 0; i < kPlanOptionCount; ++i)
    {
        // 下拉选项的下标就是套餐编号。
        FA_EXPECT_STREQ(kPlanOptions[i], PlanNameFromInt(i));
        FA_EXPECT_EQ(PlanCodeFromName(kPlanOptions[i]), i);
    }

    // 列表外的编号显示成「套餐N」，并能还原成 N。
    FA_EXPECT_STREQ(PlanNameFromInt(kPlanUnknown), _T("套餐3"));
    FA_EXPECT_EQ(PlanCodeFromName(_T("套餐3")), kPlanUnknown);

    // 不认识的名称返回 -1，包括不再使用的「企业版」。
    FA_EXPECT_EQ(PlanCodeFromName(_T("企业版")), -1);
    FA_EXPECT_EQ(PlanCodeFromName(CString()), -1);
    FA_EXPECT_EQ(PlanCodeFromName(_T("套餐")), -1);
    FA_EXPECT_EQ(PlanCodeFromName(_T("套餐x")), -1);
}

// 构造一个企业记录，只填企业设置弹窗用到的字段。
//   id：企业 ID；industry：行业；plan：套餐名称。
Enterprise MakeEnterprise(LPCTSTR id, LPCTSTR industry, LPCTSTR plan)
{
    Enterprise e;
    e.m_id       = id;
    e.m_name     = _T("测试企业");
    e.m_industry = industry;
    e.m_plan     = plan;
    return e;
}

// IND1：企业设置弹窗原样取回行业与套餐。
void TestSettingsModalKeepsIndustryAndPlan()
{
    BeginCase("IND1 企业设置弹窗：列表外的行业、空行业与列表外的套餐都原样取回");

    const int kPlanBasic    = 0;
    const int kPlanFlagship = 2;
    const int kPlanUnknown  = 3;
    const std::vector<AdminItem> noAdmins;
    EnterpriseSettingsModal modal;

    // 行业不在下拉选项里、套餐是列表外的编号：都作为额外项选中。
    modal.SetData(MakeEnterprise(_T("9"), _T("互联网"), _T("套餐3")), noAdmins);
    FA_EXPECT_STREQ(modal.GetIndustry(), _T("互联网"));
    FA_EXPECT_EQ(modal.GetPlan(), kPlanUnknown);

    // 再打开一个行业为空的企业：取回空串，不残留上一次的「互联网」。
    modal.SetData(MakeEnterprise(_T("10"), _T(""), _T("基础版")), noAdmins);
    FA_EXPECT_TRUE(modal.GetIndustry().IsEmpty());
    FA_EXPECT_EQ(modal.GetPlan(), kPlanBasic);

    // 行业与套餐都在选项里：直接选中对应项。
    modal.SetData(MakeEnterprise(_T("11"), _T("金融服务"), _T("旗舰版")), noAdmins);
    FA_EXPECT_STREQ(modal.GetIndustry(), _T("金融服务"));
    FA_EXPECT_EQ(modal.GetPlan(), kPlanFlagship);
}

// ACP6：企业设置弹窗的账号前缀输入框（2026-10-06，docs/登录账号与手机号登录方案.md）。
void TestSettingsModalAccountPrefix()
{
    BeginCase("ACP6 企业设置弹窗：账号前缀按企业现有前缀填入，取回原文，规则不合格时校验不过");

    const std::vector<AdminItem> noAdmins;
    EnterpriseSettingsModal modal;

    // 企业设了前缀：填入并原样取回，校验通过
    Enterprise withPrefix = MakeEnterprise(_T("21"), _T("金融服务"), _T("基础版"));
    withPrefix.m_accountPrefix = _T("HW.");
    modal.SetData(withPrefix, noAdmins);
    FA_EXPECT_STREQ(modal.GetAccountPrefix(), _T("HW."));
    FA_EXPECT_TRUE(modal.ValidateAccountPrefix());

    // 换一家没设前缀的企业：输入框清空（不残留上一家的前缀），空串表示用默认的 EMP，校验通过
    modal.SetData(MakeEnterprise(_T("22"), _T("金融服务"), _T("基础版")), noAdmins);
    FA_EXPECT_TRUE(modal.GetAccountPrefix().IsEmpty());
    FA_EXPECT_TRUE(modal.ValidateAccountPrefix());

    // 前缀含横线（不合格）：校验不过，保存前会提示、不发请求
    Enterprise badPrefix = MakeEnterprise(_T("23"), _T("金融服务"), _T("基础版"));
    badPrefix.m_accountPrefix = _T("H-W");
    modal.SetData(badPrefix, noAdmins);
    FA_EXPECT_FALSE(modal.ValidateAccountPrefix());
}

} // anonymous namespace

// 运行本文件全部用例。
void RunAllEnterpriseFilterTests()
{
    TestRequestCarriesOnlySetConditions();
    TestPlanMapping();
    TestSettingsModalKeepsIndustryAndPlan();
    TestSettingsModalAccountPrefix();
}

}} // namespace flamingoadmin::test
