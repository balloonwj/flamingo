/**
 *  flamingoAdmin —— 编辑定时公告时回填定时状态的单元测试（ANE 系列）。
 *
 *  早先「编辑公告」只回填标题、正文、级别、受众、置顶与投放目标，发送方式被 Reset
 *  复位成「立即」。编辑一条尚未到点的定时公告后提交，send.do 带的是 scheduled=false，
 *  服务端随即把它立即发布。本文件覆盖修复：
 *    - ANE1：公告详情应答解析出定时标志与定时时间；
 *    - ANE2：用定时公告调 LoadForEdit 后弹窗处于「定时」且时间与原值一致（去掉秒）；
 *    - ANE3：编辑过定时公告之后再新建，发送方式恢复为「立即」，不残留上一次的状态；
 *    - ANE4：定时时间文字的规整规则（去秒、保留已是分钟精度的、拒绝无法识别的）。
 *  2026-10-04 起公告中心的「复制」与企业详情的「发布公告」接到这个弹窗上，同文件加两条：
 *    - CPY1：按复制回填后是新建（编辑 ID 为 0），内容照搬，发送方式为「立即」；
 *    - CPY2：预设企业受众后，受众为「指定企业」，目标只有该企业，其余字段为新建时的默认值。
 *
 *  ComposeModal 在测试里只构造、不挂到宿主窗口，也不绘制。
 *
 *  balloonwj@qq.com   2026-10-04
 */
#include "stdafx.h"
#include "Tests/TestFramework.h"
#include "Screens/ComposeModal.h"
#include "Util/AdminApi.h"

#include <string>
#include <vector>

namespace flamingoadmin { namespace test {

namespace {

using namespace flamingoadmin::adminapi;

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

// 以给定的定时状态把弹窗切到编辑模式，其余字段取固定值。
//   modal：被测弹窗；scheduled：是否定时；scheduledTime：服务端给的定时时间。
void LoadSample(ComposeModal& modal, bool scheduled, const CString& scheduledTime)
{
    std::vector<AnnTarget> targets;
    modal.LoadForEdit(4321, _T("周会通知"), _T("周五下午三点"), 0, 0, false, targets,
                      scheduled, scheduledTime);
}

// ANE1：详情应答里的定时标志与定时时间。
void TestDetailCarriesSchedule()
{
    BeginCase("ANE1 公告详情解析出定时标志与定时时间");

    const AnnouncementDetailResult pending = ParseAnnDetailResponse(MakeHttpOk(
        "{\"code\":0,\"announcement\":{\"announcementid\":4321,\"title\":\"t\","
        "\"body\":\"b\",\"status\":0,\"scheduled\":1,"
        "\"scheduledtime\":\"2026-10-05 09:30:00\"}}"));
    FA_EXPECT_TRUE(pending.Succeeded());
    FA_EXPECT_TRUE(pending.m_announcement.m_scheduled);
    FA_EXPECT_EQ(pending.m_announcement.m_status, 0);
    FA_EXPECT_STREQ(pending.m_scheduledTime, _T("2026-10-05 09:30:00"));

    const AnnouncementDetailResult immediate = ParseAnnDetailResponse(MakeHttpOk(
        "{\"code\":0,\"announcement\":{\"announcementid\":4322,\"title\":\"t\","
        "\"body\":\"b\",\"status\":1,\"scheduled\":0,\"scheduledtime\":\"\"}}"));
    FA_EXPECT_TRUE(immediate.Succeeded());
    FA_EXPECT_FALSE(immediate.m_announcement.m_scheduled);
    FA_EXPECT_TRUE(immediate.m_scheduledTime.IsEmpty());
}

// ANE2：编辑定时公告后弹窗处于「定时」，时间与原值一致；编辑立即公告则为「立即」。
void TestLoadForEditRestoresSchedule()
{
    BeginCase("ANE2 编辑定时公告时弹窗回填「定时」与原定时时间");

    ComposeModal modal;
    LoadSample(modal, true, _T("2026-10-05 09:30:00"));
    FA_EXPECT_TRUE(modal.GetScheduled());
    FA_EXPECT_STREQ(modal.GetScheduledTime(), _T("2026-10-05 09:30"));
    FA_EXPECT_EQ(modal.GetEditAnnouncementId(), 4321);

    // 同一个弹窗再去编辑一条非定时公告：必须回到「立即」。
    LoadSample(modal, false, CString());
    FA_EXPECT_FALSE(modal.GetScheduled());
}

// ANE3：编辑过定时公告之后再新建，发送方式恢复为「立即」。
void TestResetAfterScheduledEdit()
{
    BeginCase("ANE3 编辑定时公告后再新建，发送方式恢复为「立即」");

    ComposeModal modal;
    LoadSample(modal, true, _T("2026-10-05 09:30:00"));
    FA_EXPECT_TRUE(modal.GetScheduled());

    modal.Reset();
    FA_EXPECT_FALSE(modal.GetScheduled());
    FA_EXPECT_EQ(modal.GetEditAnnouncementId(), 0);
}

// ANE4：定时时间文字的规整规则。
void TestNormalizeScheduleText()
{
    BeginCase("ANE4 定时时间文字规整为「YYYY-MM-DD HH:MM」");

    FA_EXPECT_STREQ(ComposeModal::NormalizeScheduleText(_T("2026-10-05 09:30:00")),
                    _T("2026-10-05 09:30"));
    FA_EXPECT_STREQ(ComposeModal::NormalizeScheduleText(_T("2026-10-05 09:30")),
                    _T("2026-10-05 09:30"));
    FA_EXPECT_STREQ(ComposeModal::NormalizeScheduleText(_T(" 2026-10-05 09:30:59 ")),
                    _T("2026-10-05 09:30"));
    // 无法识别的文字返回空串，由调用方保留默认时间。
    FA_EXPECT_TRUE(ComposeModal::NormalizeScheduleText(_T("")).IsEmpty());
    FA_EXPECT_TRUE(ComposeModal::NormalizeScheduleText(_T("明天上午")).IsEmpty());
    FA_EXPECT_TRUE(ComposeModal::NormalizeScheduleText(_T("2026/10/05 09:30")).IsEmpty());

    // 服务端给的时间无法识别时，弹窗仍处于「定时」，时间保留 Reset 填的默认值
    // （明天 09:00），而不是空串 —— 空串提交会被服务端当作非法时间。
    ComposeModal modal;
    LoadSample(modal, true, _T("bad"));
    FA_EXPECT_TRUE(modal.GetScheduled());
    FA_EXPECT_FALSE(modal.GetScheduledTime().IsEmpty());
    FA_EXPECT_TRUE(modal.GetScheduledTime().Right(5) == _T("09:00"));
}

// CPY1：复制公告。
void TestLoadForCopy()
{
    BeginCase("CPY1 复制公告后为新建，标题、正文、级别、受众、置顶与目标照搬，发送方式为立即");

    // 受众「指定部门」（编码 2），两个部门目标（目标类型 2）。
    const int kAudienceDept = 2;
    const int kTargetDept   = 2;
    const int kSeverityWarn = 2;
    std::vector<AnnTarget> targets;
    targets.push_back(AnnTarget(kTargetDept, 1610612739));
    targets.push_back(AnnTarget(kTargetDept, 1610612740));

    ComposeModal modal;
    // 先停在一条定时公告的编辑状态上，确认复制不会沿用它的编辑 ID 与定时状态。
    LoadSample(modal, true, _T("2026-10-05 09:30:00"));
    modal.LoadForCopy(_T("年底体检安排"), _T("请各部门按时参加"), kSeverityWarn,
                      kAudienceDept, true, targets);

    FA_EXPECT_EQ(modal.GetEditAnnouncementId(), 0);
    FA_EXPECT_FALSE(modal.GetScheduled());
    FA_EXPECT_STREQ(modal.GetTitle(), _T("年底体检安排"));
    FA_EXPECT_STREQ(modal.GetBody(), _T("请各部门按时参加"));
    FA_EXPECT_EQ(modal.GetSeverityCode(), kSeverityWarn);
    FA_EXPECT_EQ(modal.GetAudienceType(), kAudienceDept);
    FA_EXPECT_TRUE(modal.GetPinned());
    FA_EXPECT_EQ(modal.GetTargets().size(), 2);
    if (modal.GetTargets().size() == 2)
    {
        FA_EXPECT_EQ(modal.GetTargets()[0].m_type, kTargetDept);
        FA_EXPECT_EQ(modal.GetTargets()[1].m_id, 1610612740LL);
    }
}

// CPY2：从企业详情发布公告，预设受众为该企业。
void TestPresetEnterpriseAudience()
{
    BeginCase("CPY2 预设企业受众后，受众为「指定企业」，目标为该企业");

    const int kAudienceEnterprise = 1;
    const int kTargetEnterprise   = 1;
    const long long kEntId        = 2;

    ComposeModal modal;
    // 先回填过一条别的公告，确认预设前会复位，不残留上一次的内容。
    LoadSample(modal, true, _T("2026-10-05 09:30:00"));
    modal.PresetEnterpriseAudience(kEntId);

    FA_EXPECT_EQ(modal.GetEditAnnouncementId(), 0);
    FA_EXPECT_FALSE(modal.GetScheduled());
    FA_EXPECT_TRUE(modal.GetTitle().IsEmpty());
    FA_EXPECT_TRUE(modal.GetBody().IsEmpty());
    FA_EXPECT_EQ(modal.GetAudienceType(), kAudienceEnterprise);
    FA_EXPECT_EQ(modal.GetTargets().size(), 1);
    if (modal.GetTargets().size() == 1)
    {
        FA_EXPECT_EQ(modal.GetTargets()[0].m_type, kTargetEnterprise);
        FA_EXPECT_EQ(modal.GetTargets()[0].m_id, kEntId);
    }
    // 企业 ID 非法（0 或负数）时退化为普通的新建：受众为默认的「全部企业」，没有目标。
    modal.PresetEnterpriseAudience(0);
    FA_EXPECT_EQ(modal.GetAudienceType(), 0);
    FA_EXPECT_EQ(modal.GetTargets().size(), 0);
}

} // anonymous namespace

// 运行本文件全部用例。
void RunAllComposeEditScheduleTests()
{
    TestDetailCarriesSchedule();
    TestLoadForEditRestoresSchedule();
    TestResetAfterScheduledEdit();
    TestNormalizeScheduleText();
    TestLoadForCopy();
    TestPresetEnterpriseAudience();
}

}} // namespace flamingoadmin::test
