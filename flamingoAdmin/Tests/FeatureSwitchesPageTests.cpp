/**
 *  flamingoAdmin —— 企业详情「功能开关」子 tab（FeatureSwitchesPage，2026-10-08）的单元测试。
 *
 *    - FSW1：结构：十三个开关依次为自助注册、入群需同意、单聊密聊、帖子群、文件共享、点对点直连、群通话、语音字幕、
 *            云端录制、机器人总开关与三个机器人开关；各自的设置项、读写标签与控件编号；全部为完整样式；四张卡片的
 *            归属与标题；云端录像空间面板的标签与「保存」按钮；
 *    - FSW2：宽时排成两栏：左栏「账号与会话」「文件与传输」、右栏「音视频」「机器人」；面板都在所属卡片之内、
 *            自上而下排列；云端录像空间紧跟云端录制、机器人开关排在云端录像空间之后（docs/机器人方案.md §11 Q18）；
 *    - FSW3：窄时排成一栏，四张卡片自上而下依次排列；卡片宽度不超过上限；
 *    - FSW4：滚动：放得下时不滚动、滚轮交给上层；最小窗口下可滚动，滚动量夹取到合法范围，滚出本页的面板点不到，
 *            重新 Load 时回到顶部；
 *    - FSW5：Load：每个面板与云端录像空间各发一个读设置的请求，平台超管带企业 ID，企业管理员不带；
 *            EnterpriseIdForLoad 的取值规则；
 *    - FSW6：分发：接口应答按标签交给对应的面板，不属于本页的标签返回 false；开关通知按控件编号交给对应的面板
 *            弹确认框；「云端录像空间」的「保存」；
 *    - FSW7：企业详情屏的第四个子 tab「功能开关」：位置（有无「在线用户」两种情况）、切换后各子控件的显隐、
 *            点击切换、页面铺满主体区；
 *    - FSW8：企业设置弹窗与平台设置页里已经没有任何功能开关。
 *
 *  组件的请求出口与界面请求处理函数都换成记录下来，测试不碰网络、不弹窗口。
 *
 *  balloonwj@qq.com   2026-10-08
 */
#include "stdafx.h"
#include "Tests/TestFramework.h"
#include "Screens/FeatureSwitchesPage.h"
#include "Screens/FeatureSwitchPanel.h"
#include "Screens/CloudRecordQuotaPanel.h"
#include "Screens/EnterpriseDetailScreen.h"
#include "Screens/ServicesPanel.h"
#include "Widgets/LinkPageSlot.h"
#include "Screens/EnterpriseSettingsModal.h"
#include "Screens/SettingsScreen.h"
#include "UiIds.h"
#include "Theme/AdminTheme.h"
#include "Controls/Basic/DuiButton.h"
#include "Controls/Input/DuiSwitch.h"
#include "Controls/Input/DuiEdit.h"
#include "json.h"

#include <string>
#include <vector>

namespace flamingoadmin { namespace test {

namespace {

using namespace flamingoadmin::adminapi;

// 企业详情屏头部的高度（与 EnterpriseDetailScreen.cpp 的 kHeaderH 相同），功能开关页从它下面开始
const int kDetailHeaderH = 152;

// 默认窗口（1440×900）与最小窗口（1100×720）下功能开关页的矩形：内容区减去企业详情屏的头部
const RECT kPageDefault = { 0, 0, 1440 - theme::kSidebarWidth, 900 - theme::kTopBarHeight - kDetailHeaderH };
const RECT kPageMin = { 0, 0, 1100 - theme::kSidebarWidth, 720 - theme::kTopBarHeight - kDetailHeaderH };

// 足够高的矩形：两栏时内容全部放得下
const RECT kPageTall = { 0, 0, 1440 - theme::kSidebarWidth, 1400 };

// 窄到只能排一栏、且一栏的宽度（减去两侧外边距）不超过卡片宽度上限的矩形，以及排不下两栏、但一栏的宽度
// 超过上限、卡片宽度会被夹到上限的矩形
const RECT kPageNarrow = { 0, 0, 600, 1600 };
const RECT kPageMedium = { 0, 0, 760, 1600 };

// 卡片宽度上限（与 FeatureSwitchesPage.cpp 的 kCardMaxW 相同）与页面外边距（kPagePad）
const int kCardMaxW = 600;
const int kPagePad = 24;

// 企业详情屏在默认窗口下的矩形
const RECT kDetailRect = { 0, 0, 1440 - theme::kSidebarWidth, 900 - theme::kTopBarHeight };

// 测试用的企业 ID
const long long kEntA = 9001;

struct Captured
{
    std::vector<HttpRequest>      m_requests;  // 请求出口收到的请求
    std::vector<fsui::UiRequest>  m_ui;        // 界面请求
};

void CaptureRequest(void* user, HttpRequest& request)
{
    static_cast<Captured*>(user)->m_requests.push_back(request);
}

void CaptureUi(void* user, const fsui::UiRequest& request)
{
    static_cast<Captured*>(user)->m_ui.push_back(request);
}

HttpResult MakeHttpOk(const std::string& body)
{
    HttpResult r;
    r.m_transportOk = true;
    r.m_statusCode = 200;
    r.m_body = body;
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

// 某个标签最后一个请求的请求体；没有时返回 false
bool LastBody(const Captured& c, ApiTag tag, Json::Value& body)
{
    for (size_t i = c.m_requests.size(); i > 0; --i)
    {
        if (c.m_requests[i - 1].m_tag == static_cast<UINT_PTR>(tag))
            return ParseBody(c.m_requests[i - 1], body);
    }
    return false;
}

// 最后一个某种界面请求；没有时返回默认值
fsui::UiRequest LastUi(const Captured& c, int kind)
{
    for (size_t i = c.m_ui.size(); i > 0; --i)
    {
        if (c.m_ui[i - 1].m_kind == kind)
            return c.m_ui[i - 1];
    }
    return fsui::UiRequest();
}

void LoginEnterpriseAdmin()
{
    AdminSession::Instance().Set(_T("tok-ent"), 1, _T("企业管理员"), _T("ent@test.com"), kEntA);
}

void LoginPlatformAdmin()
{
    AdminSession::Instance().Set(_T("tok-plat"), 2, _T("平台超管"), _T("plat@test.com"), 0);
}

// 矩形 inner 是否完整落在 outer 之内
bool RectInside(const RECT& inner, const RECT& outer)
{
    return inner.left >= outer.left && inner.right <= outer.right && inner.top >= outer.top && inner.bottom <= outer.bottom;
}

// 矩形是否为空
bool RectEmpty(const RECT& rc)
{
    return rc.right <= rc.left || rc.bottom <= rc.top;
}

// 矩形的中心点
POINT CenterOf(const RECT& rc)
{
    POINT pt = { (rc.left + rc.right) / 2, (rc.top + rc.bottom) / 2 };
    return pt;
}

// 命中的控件 hit 是否就是 ancestor 或在它之内（沿父控件链向上查找）。hit 为空时返回 false。
bool IsWithinControl(const balloonwjui::DuiControl* hit, const balloonwjui::DuiControl* ancestor)
{
    for (const balloonwjui::DuiControl* c = hit; c != nullptr; c = c->GetParent())
    {
        if (c == ancestor)
            return true;
    }
    return false;
}

// 一个开关在页面上应有的配置（与 FeatureSwitchesPage::Item 的顺序一致）
struct ExpectedItem
{
    const TCHAR* m_key;      // 设置项名
    ApiTag       m_getTag;   // 读设置的结果标签
    ApiTag       m_setTag;   // 改设置的结果标签
    int          m_ctrlId;   // 开关的控件编号
    int          m_group;    // 所属卡片
};

const ExpectedItem kExpected[FeatureSwitchesPage::kItemCount] =
{
    { _T("register.enabled"),    kApiTagRegisterSwitchGet,    kApiTagRegisterSwitchSet,    kIdFeatSwitchRegister,         FeatureSwitchesPage::kGroupAccount },
    { _T("group.inviteconfirm"), kApiTagInviteConfirmGet,     kApiTagInviteConfirmSet,     kIdFeatSwitchInviteConfirm,    FeatureSwitchesPage::kGroupAccount },
    { _T("secretchat.enabled"),  kApiTagSecretChatSwitchGet,  kApiTagSecretChatSwitchSet,  kIdFeatSwitchSecretChat,       FeatureSwitchesPage::kGroupAccount },
    { _T("threadgroup.enabled"), kApiTagThreadGroupSwitchGet, kApiTagThreadGroupSwitchSet, kIdFeatSwitchThreadGroup,      FeatureSwitchesPage::kGroupAccount },
    { _T("fileshare.enabled"),   kApiTagFeatureGet,           kApiTagFeatureSet,           kIdFeatSwitchFileShare,        FeatureSwitchesPage::kGroupTransfer },
    { _T("p2p.enabled"),         kApiTagP2PSwitchGet,         kApiTagP2PSwitchSet,         kIdFeatSwitchP2P,              FeatureSwitchesPage::kGroupTransfer },
    { _T("groupcall.enabled"),   kApiTagGroupCallSwitchGet,   kApiTagGroupCallSwitchSet,   kIdFeatSwitchGroupCall,        FeatureSwitchesPage::kGroupMedia },
    { _T("caption.enabled"),     kApiTagCaptionSwitchGet,     kApiTagCaptionSwitchSet,     kIdFeatSwitchCaption,          FeatureSwitchesPage::kGroupMedia },
    { _T("cloudrecord.enabled"), kApiTagCloudRecordSwitchGet, kApiTagCloudRecordSwitchSet, kIdFeatSwitchCloudRecord,      FeatureSwitchesPage::kGroupMedia },
    { _T("bot.enabled"),         kApiTagBotSwitchGet,         kApiTagBotSwitchSet,         kIdFeatSwitchBot,              FeatureSwitchesPage::kGroupBot },
    { _T("bot.groupbot"),        kApiTagBotGroupSwitchGet,    kApiTagBotGroupSwitchSet,    kIdFeatSwitchBotGroup,         FeatureSwitchesPage::kGroupBot },
    { _T("bot.personal"),        kApiTagBotPersonalSwitchGet, kApiTagBotPersonalSwitchSet, kIdFeatSwitchBotPersonal,      FeatureSwitchesPage::kGroupBot },
    { _T("bot.groupexternal"),   kApiTagBotExternalSwitchGet, kApiTagBotExternalSwitchSet, kIdFeatSwitchBotGroupExternal, FeatureSwitchesPage::kGroupBot },
};

// 把只含 ASCII 字符的界面字符串转成 std::string（设置项名都是 ASCII）
std::string AsciiOf(const TCHAR* text)
{
    std::string out;
    for (const TCHAR* p = text; *p != 0; ++p)
    {
        out.push_back(static_cast<char>(*p));
    }
    return out;
}

// 读设置的应答：十三个开关全部为开启、都是默认值，另带云端录像空间的两个数值项
std::string AllSettingsJson(long long enterpriseId)
{
    std::string json = "{\"code\":0,\"msg\":\"ok\",\"enterpriseid\":" + std::to_string(enterpriseId)
                     + ",\"cloudrecordusedbytes\":0,\"settings\":[";
    for (int i = 0; i < FeatureSwitchesPage::kItemCount; ++i)
    {
        json += "{\"key\":\"" + AsciiOf(kExpected[i].m_key) + "\",\"value\":\"1\",\"isdefault\":1},";
    }
    json += "{\"key\":\"cloudrecord.quotagb\",\"value\":\"50\",\"isdefault\":1},"
            "{\"key\":\"cloudrecord.retaindays\",\"value\":\"30\",\"isdefault\":1}]}";
    return json;
}

// FSW1：结构
void TestStructure()
{
    BeginCase("FSW1 功能开关页：十三个开关的设置项、标签、控件编号、完整样式与所属卡片");

    FeatureSwitchesPage page;
    for (int i = 0; i < FeatureSwitchesPage::kItemCount; ++i)
    {
        FeatureSwitchPanel* panel = page.Panel(i);
        FA_EXPECT_NOTNULL(panel);
        if (panel == nullptr)
            return;
        FA_EXPECT_STREQ(panel->Spec().m_key, kExpected[i].m_key);
        FA_EXPECT_EQ(static_cast<int>(panel->GetTag()), static_cast<int>(kExpected[i].m_getTag));
        FA_EXPECT_EQ(static_cast<int>(panel->SetTag()), static_cast<int>(kExpected[i].m_setTag));
        //开关的控件编号与 AdminFrame 分发用的编号一致
        FA_EXPECT_TRUE(page.FindCtrlById(kExpected[i].m_ctrlId) == panel->Switch());
        //完整样式：名称、说明与取值来历都显示
        FA_EXPECT_EQ(panel->PanelHeight(), FeatureSwitchPanel::kPanelHeight);
        FA_EXPECT_EQ(FeatureSwitchesPage::GroupOf(i), kExpected[i].m_group);
    }
    FA_EXPECT_NULL(page.Panel(-1));
    FA_EXPECT_NULL(page.Panel(FeatureSwitchesPage::kItemCount));
    FA_EXPECT_EQ(FeatureSwitchesPage::GroupOf(-1), -1);
    FA_EXPECT_EQ(FeatureSwitchesPage::GroupOf(FeatureSwitchesPage::kItemCount), -1);

    FA_EXPECT_STREQ(FeatureSwitchesPage::GroupTitle(FeatureSwitchesPage::kGroupAccount), _T("账号与会话"));
    FA_EXPECT_STREQ(FeatureSwitchesPage::GroupTitle(FeatureSwitchesPage::kGroupTransfer), _T("文件与传输"));
    FA_EXPECT_STREQ(FeatureSwitchesPage::GroupTitle(FeatureSwitchesPage::kGroupMedia), _T("音视频"));
    FA_EXPECT_STREQ(FeatureSwitchesPage::GroupTitle(FeatureSwitchesPage::kGroupBot), _T("机器人"));
    FA_EXPECT_STREQ(FeatureSwitchesPage::GroupTitle(FeatureSwitchesPage::kGroupCount), _T(""));

    //云端录像空间：读写标签与三个控件的编号
    CloudRecordQuotaPanel* quota = page.GetCloudQuotaPanel();
    FA_EXPECT_NOTNULL(quota);
    if (quota == nullptr)
        return;
    FA_EXPECT_EQ(static_cast<int>(quota->GetTag()), static_cast<int>(kApiTagCloudQuotaGet));
    FA_EXPECT_EQ(static_cast<int>(quota->SetTag()), static_cast<int>(kApiTagCloudQuotaSet));
    FA_EXPECT_TRUE(page.FindCtrlById(kIdCloudQuotaSave) == quota->SaveButton());
    FA_EXPECT_TRUE(page.FindCtrlById(kIdCloudQuotaQuota) == quota->QuotaEdit());
    FA_EXPECT_TRUE(page.FindCtrlById(kIdCloudQuotaDays) == quota->DaysEdit());
}

// 核对每个面板都在所属卡片之内、同一卡片里的面板自上而下排列，云端录像空间紧跟云端录制
void ExpectPanelsInsideCards(const FeatureSwitchesPage& page)
{
    for (int i = 0; i < FeatureSwitchesPage::kItemCount; ++i)
    {
        const RECT rc = page.Panel(i)->GetRect();
        const RECT card = page.GroupRect(kExpected[i].m_group);
        FA_EXPECT_TRUE(RectInside(rc, card));
        FA_EXPECT_EQ(rc.bottom - rc.top, FeatureSwitchPanel::kPanelHeight);
        //同一张卡片里的后一个在前一个之下
        if (i > 0 && kExpected[i - 1].m_group == kExpected[i].m_group)
        {
            FA_EXPECT_TRUE(rc.top >= page.Panel(i - 1)->GetRect().bottom);
        }
        //开关在面板右端、有实际尺寸
        const RECT sw = page.Panel(i)->Switch()->GetRect();
        FA_EXPECT_TRUE(sw.right <= rc.right && sw.right > sw.left);
    }
    const RECT quota = page.GetCloudQuotaPanel()->GetRect();
    FA_EXPECT_TRUE(RectInside(quota, page.GroupRect(FeatureSwitchesPage::kGroupMedia)));
    FA_EXPECT_EQ(quota.bottom - quota.top, CloudRecordQuotaPanel::kPanelHeight);
    FA_EXPECT_TRUE(quota.top >= page.Panel(FeatureSwitchesPage::kItemCloudRecord)->GetRect().bottom);
    FA_EXPECT_EQ(quota.left, page.Panel(FeatureSwitchesPage::kItemCloudRecord)->GetRect().left);
    FA_EXPECT_EQ(quota.right, page.Panel(FeatureSwitchesPage::kItemCloudRecord)->GetRect().right);
    //Q18：四个机器人开关排在云端录像空间之后
    FA_EXPECT_TRUE(page.Panel(FeatureSwitchesPage::kItemBot)->GetRect().top > quota.bottom);
}

// FSW2：两栏
void TestTwoColumns()
{
    BeginCase("FSW2 功能开关页：宽时两栏，左栏账号与会话、文件与传输，右栏音视频、机器人");

    FeatureSwitchesPage page;
    page.SetRect(kPageDefault);
    FA_EXPECT_TRUE(page.IsTwoColumns());
    const RECT account = page.GroupRect(FeatureSwitchesPage::kGroupAccount);
    const RECT transfer = page.GroupRect(FeatureSwitchesPage::kGroupTransfer);
    const RECT media = page.GroupRect(FeatureSwitchesPage::kGroupMedia);
    const RECT bot = page.GroupRect(FeatureSwitchesPage::kGroupBot);
    //左栏两张卡上下排列、左边对齐；右栏同理；两栏顶边对齐，右栏在左栏右边
    FA_EXPECT_EQ(account.left, kPageDefault.left + kPagePad);
    FA_EXPECT_EQ(transfer.left, account.left);
    FA_EXPECT_TRUE(transfer.top > account.bottom);
    FA_EXPECT_EQ(bot.left, media.left);
    FA_EXPECT_TRUE(bot.top > media.bottom);
    FA_EXPECT_EQ(media.top, account.top);
    FA_EXPECT_TRUE(media.left > account.right);
    //卡片宽度一致，不超过上限，也不超出本页右边
    FA_EXPECT_EQ(account.right - account.left, media.right - media.left);
    FA_EXPECT_TRUE(account.right - account.left <= kCardMaxW);
    FA_EXPECT_TRUE(media.right <= kPageDefault.right - kPagePad);
    ExpectPanelsInsideCards(page);
    //默认窗口下右栏放不下，需要滚动；足够高时不需要
    FA_EXPECT_TRUE(page.GetScrollMax() > 0);
    page.SetRect(kPageTall);
    FA_EXPECT_TRUE(page.IsTwoColumns());
    FA_EXPECT_EQ(page.GetScrollMax(), 0);
    ExpectPanelsInsideCards(page);

    //最小窗口下仍然排得下两栏
    page.SetRect(kPageMin);
    FA_EXPECT_TRUE(page.IsTwoColumns());
    ExpectPanelsInsideCards(page);
}

// FSW3：一栏
void TestOneColumn()
{
    BeginCase("FSW3 功能开关页：窄时一栏，四张卡片自上而下依次排列，卡片宽度不超过上限");

    FeatureSwitchesPage page;
    page.SetRect(kPageNarrow);
    FA_EXPECT_FALSE(page.IsTwoColumns());
    for (int g = 0; g < FeatureSwitchesPage::kGroupCount; ++g)
    {
        const RECT card = page.GroupRect(g);
        FA_EXPECT_EQ(card.left, kPageNarrow.left + kPagePad);
        FA_EXPECT_EQ(card.right, kPageNarrow.right - kPagePad);
        if (g > 0)
        {
            FA_EXPECT_TRUE(card.top > page.GroupRect(g - 1).bottom);
        }
    }
    ExpectPanelsInsideCards(page);
    FA_EXPECT_TRUE(RectEmpty(page.GroupRect(FeatureSwitchesPage::kGroupCount)));

    //排不下两栏、但一栏时宽度超过上限：卡片宽度夹到上限
    page.SetRect(kPageMedium);
    FA_EXPECT_FALSE(page.IsTwoColumns());
    const RECT card = page.GroupRect(FeatureSwitchesPage::kGroupAccount);
    FA_EXPECT_EQ(card.right - card.left, kCardMaxW);
}

// FSW4：滚动
void TestScroll()
{
    BeginCase("FSW4 功能开关页：放得下时不滚动；最小窗口下可滚动、夹取滚动量、滚出本页的面板点不到、Load 后回到顶部");

    FeatureSwitchesPage page;
    page.SetRect(kPageTall);
    FA_EXPECT_EQ(page.GetScrollMax(), 0);
    FA_EXPECT_FALSE(page.OnMouseWheel(CenterOf(kPageTall), -WHEEL_DELTA, 0));
    page.ScrollTo(100);
    FA_EXPECT_EQ(page.GetScroll(), 0);

    page.SetRect(kPageMin);
    const int maxScroll = page.GetScrollMax();
    FA_EXPECT_TRUE(maxScroll > 0);
    FeatureSwitchPanel* first = page.Panel(FeatureSwitchesPage::kItemRegister);
    FeatureSwitchPanel* last = page.Panel(FeatureSwitchesPage::kItemBotGroupExternal);
    //顶部：第一个开关完整可见，最后一个在本页之外、点不到
    FA_EXPECT_TRUE(RectInside(first->GetRect(), kPageMin));
    FA_EXPECT_FALSE(RectInside(last->GetRect(), kPageMin));
    FA_EXPECT_FALSE(IsWithinControl(page.HitTest(CenterOf(last->Switch()->GetRect())), last));

    //滚轮向下一格：滚动量增加且不越界
    FA_EXPECT_TRUE(page.OnMouseWheel(CenterOf(kPageMin), -WHEEL_DELTA, 0));
    FA_EXPECT_TRUE(page.GetScroll() > 0 && page.GetScroll() <= maxScroll);

    //滚到底：夹取到最大值，最后一个开关完整可见、点得到
    page.ScrollTo(maxScroll + 1000);
    FA_EXPECT_EQ(page.GetScroll(), maxScroll);
    FA_EXPECT_TRUE(RectInside(last->GetRect(), kPageMin));
    FA_EXPECT_TRUE(IsWithinControl(page.HitTest(CenterOf(last->Switch()->GetRect())), last));

    //滚到顶：夹取到 0
    page.ScrollTo(-10);
    FA_EXPECT_EQ(page.GetScroll(), 0);

    //重新 Load（换了企业或重新进入）：回到顶部
    LoginPlatformAdmin();
    Captured cap;
    page.SetRequestSink(&CaptureRequest, &cap);
    page.SetUiRequestHandler(&CaptureUi, &cap);
    page.ScrollTo(maxScroll);
    FA_EXPECT_EQ(page.GetScroll(), maxScroll);
    page.Load(kEntA);
    FA_EXPECT_EQ(page.GetScroll(), 0);
    FA_EXPECT_TRUE(RectInside(first->GetRect(), kPageMin));
}

// FSW5：Load
void TestLoad()
{
    BeginCase("FSW5 功能开关页：Load 为每个面板发读设置的请求，平台超管带企业 ID、企业管理员不带");

    LoginPlatformAdmin();
    Captured cap;
    FeatureSwitchesPage page;
    page.SetRequestSink(&CaptureRequest, &cap);
    page.SetUiRequestHandler(&CaptureUi, &cap);
    page.Load(kEntA);
    FA_EXPECT_TRUE(page.EnterpriseId() == kEntA);
    //十三个开关加云端录像空间，各一个请求
    FA_EXPECT_EQ(static_cast<int>(cap.m_requests.size()), FeatureSwitchesPage::kItemCount + 1);
    Json::Value body;
    for (int i = 0; i < FeatureSwitchesPage::kItemCount; ++i)
    {
        FA_EXPECT_TRUE(LastBody(cap, kExpected[i].m_getTag, body));
        FA_EXPECT_TRUE(body["enterpriseid"].asInt64() == kEntA);
        FA_EXPECT_EQ(page.Panel(i)->GetState(), static_cast<int>(FeatureSwitchPanel::kStateLoading));
    }
    FA_EXPECT_TRUE(LastBody(cap, kApiTagCloudQuotaGet, body));
    FA_EXPECT_TRUE(body["enterpriseid"].asInt64() == kEntA);

    //企业管理员：传 0，请求里不带 enterpriseid
    LoginEnterpriseAdmin();
    Captured own;
    FeatureSwitchesPage ownPage;
    ownPage.SetRequestSink(&CaptureRequest, &own);
    ownPage.SetUiRequestHandler(&CaptureUi, &own);
    ownPage.Load(0);
    FA_EXPECT_TRUE(ownPage.EnterpriseId() == 0);
    FA_EXPECT_EQ(static_cast<int>(own.m_requests.size()), FeatureSwitchesPage::kItemCount + 1);
    FA_EXPECT_TRUE(LastBody(own, kApiTagInviteConfirmGet, body));
    FA_EXPECT_FALSE(body.isMember("enterpriseid"));
    FA_EXPECT_TRUE(LastBody(own, kApiTagCloudQuotaGet, body));
    FA_EXPECT_FALSE(body.isMember("enterpriseid"));

    //取值规则：平台超管用详情屏的企业 ID，企业管理员一律为 0，ID 文字不合法时为 0
    FA_EXPECT_TRUE(FeatureSwitchesPage::EnterpriseIdForLoad(true, _T("9001")) == kEntA);
    FA_EXPECT_TRUE(FeatureSwitchesPage::EnterpriseIdForLoad(false, _T("9001")) == 0);
    FA_EXPECT_TRUE(FeatureSwitchesPage::EnterpriseIdForLoad(true, _T("")) == 0);
    FA_EXPECT_TRUE(FeatureSwitchesPage::EnterpriseIdForLoad(true, _T("abc")) == 0);
    FA_EXPECT_TRUE(FeatureSwitchesPage::EnterpriseIdForLoad(true, _T("-3")) == 0);
}

// FSW6：分发
void TestDispatch()
{
    BeginCase("FSW6 功能开关页：应答按标签、开关通知按控件编号交给对应的面板，不属于本页的返回 false");

    LoginPlatformAdmin();
    Captured cap;
    FeatureSwitchesPage page;
    page.SetRequestSink(&CaptureRequest, &cap);
    page.SetUiRequestHandler(&CaptureUi, &cap);
    page.SetRect(kPageDefault);
    page.Load(kEntA);

    //每个面板只认自己的读标签：逐个转交后各自变为已读到
    const std::string settings = AllSettingsJson(kEntA);
    for (int i = 0; i < FeatureSwitchesPage::kItemCount; ++i)
    {
        FA_EXPECT_TRUE(page.OnApiResult(kExpected[i].m_getTag, MakeHttpOk(settings)));
        FA_EXPECT_EQ(page.Panel(i)->GetState(), static_cast<int>(FeatureSwitchPanel::kStateLoaded));
        FA_EXPECT_TRUE(page.Panel(i)->IsSwitchChecked());
        //后面的面板还没收到自己的应答
        if (i + 1 < FeatureSwitchesPage::kItemCount)
        {
            FA_EXPECT_EQ(page.Panel(i + 1)->GetState(), static_cast<int>(FeatureSwitchPanel::kStateLoading));
        }
    }
    FA_EXPECT_TRUE(page.OnApiResult(kApiTagCloudQuotaGet, MakeHttpOk(settings)));
    FA_EXPECT_EQ(page.GetCloudQuotaPanel()->GetState(), static_cast<int>(CloudRecordQuotaPanel::kStateLoaded));
    //不属于本页的标签
    FA_EXPECT_FALSE(page.OnApiResult(kApiTagFsList, MakeHttpOk(settings)));

    //拨动「单聊密聊」：交给它的面板弹确认框，关闭用危险样式；取消后开关恢复
    FeatureSwitchPanel* secret = page.Panel(FeatureSwitchesPage::kItemSecretChat);
    secret->Switch()->SetChecked(false, false, false);
    FA_EXPECT_TRUE(page.OnSwitchToggled(kIdFeatSwitchSecretChat, false));
    fsui::UiRequest confirm = LastUi(cap, fsui::kUiConfirm);
    FA_EXPECT_STREQ(confirm.m_title, _T("关闭单聊密聊"));
    FA_EXPECT_TRUE(confirm.m_danger);
    FA_EXPECT_TRUE(confirm.m_target == secret);
    if (confirm.m_target != nullptr)
        confirm.m_target->OnConfirmResult(false);
    FA_EXPECT_TRUE(secret->IsSwitchChecked());

    //拨动机器人总开关：同样交给自己的面板
    FA_EXPECT_TRUE(page.OnSwitchToggled(kIdFeatSwitchBot, false));
    confirm = LastUi(cap, fsui::kUiConfirm);
    FA_EXPECT_TRUE(confirm.m_target == page.Panel(FeatureSwitchesPage::kItemBot));
    if (confirm.m_target != nullptr)
        confirm.m_target->OnConfirmResult(false);
    //不属于本页的控件编号
    FA_EXPECT_FALSE(page.OnSwitchToggled(kIdFsRefresh, true));

    //「云端录像空间」的「保存」：没有修改时只提示
    FA_EXPECT_TRUE(page.OnQuotaSaveClicked(kIdCloudQuotaSave));
    FA_EXPECT_STREQ(LastUi(cap, fsui::kUiToast).m_text, _T("云端录像空间没有修改"));
    FA_EXPECT_FALSE(page.OnQuotaSaveClicked(kIdFsRefresh));
}

// FSW7：企业详情屏的第四个子 tab
void TestDetailTab()
{
    BeginCase("FSW7 企业详情屏：功能开关子 tab 的位置、切换后的显隐、点击切换、页面铺满主体区");

    EnterpriseDetailScreen screen;
    screen.ForceLayout(kDetailRect);
    FA_EXPECT_NOTNULL(screen.GetFeaturesPage());
    if (screen.GetFeaturesPage() == nullptr)
        return;

    //没有「在线用户」时紧跟服务号；有时排在「在线用户」之后
    const RECT svc = screen.SubTabRect(EnterpriseDetailScreen::kTabSvc);
    RECT features = screen.SubTabRect(EnterpriseDetailScreen::kTabFeatures);
    FA_EXPECT_FALSE(RectEmpty(features));
    FA_EXPECT_TRUE(RectEmpty(screen.SubTabRect(EnterpriseDetailScreen::kTabOnline)));
    FA_EXPECT_EQ(features.left, svc.right);
    FA_EXPECT_EQ(features.top, svc.top);
    FA_EXPECT_EQ(features.bottom, svc.bottom);
    screen.SetOnlineTabAvailable(true);
    const RECT online = screen.SubTabRect(EnterpriseDetailScreen::kTabOnline);
    features = screen.SubTabRect(EnterpriseDetailScreen::kTabFeatures);
    FA_EXPECT_EQ(online.left, svc.right);
    FA_EXPECT_EQ(features.left, online.right);
    FA_EXPECT_TRUE(RectEmpty(screen.SubTabRect(-1)));

    //初始在组织架构：功能开关页隐藏
    FA_EXPECT_TRUE(screen.IsOrgTab());
    FA_EXPECT_FALSE(screen.GetFeaturesPage()->IsVisible());

    //点「功能开关」：切过去，页面铺满头部以下的主体区；主操作、新建群、组织树搜索框与在线用户占位控件都隐藏
    FA_EXPECT_TRUE(screen.OnLButtonUp(CenterOf(features), 0));
    FA_EXPECT_TRUE(screen.IsFeaturesTab());
    FA_EXPECT_EQ(screen.GetSubTab(), static_cast<int>(EnterpriseDetailScreen::kTabFeatures));
    FA_EXPECT_TRUE(screen.GetFeaturesPage()->IsVisible());
    const RECT page = screen.GetFeaturesPage()->GetRect();
    FA_EXPECT_EQ(page.left, kDetailRect.left);
    FA_EXPECT_EQ(page.top, kDetailRect.top + kDetailHeaderH);
    FA_EXPECT_EQ(page.right, kDetailRect.right);
    FA_EXPECT_EQ(page.bottom, kDetailRect.bottom);
    FA_EXPECT_FALSE(screen.FindCtrlById(kIdDetailPrimary)->IsVisible());
    FA_EXPECT_FALSE(screen.FindCtrlById(kIdDetailCreateGroup)->IsVisible());
    FA_EXPECT_FALSE(screen.FindCtrlById(kIdOrgSearch)->IsVisible());
    FA_EXPECT_FALSE(screen.GetOnlineSlot()->IsVisible());
    FA_EXPECT_FALSE(screen.GetServicesPanel()->IsVisible());

    //停在「功能开关」时「在线用户」变为不可用：仍停在功能开关，位置挪回服务号之后
    screen.SetOnlineTabAvailable(false);
    FA_EXPECT_TRUE(screen.IsFeaturesTab());
    FA_EXPECT_EQ(screen.SubTabRect(EnterpriseDetailScreen::kTabFeatures).left, svc.right);

    //切到服务号：功能开关页隐藏，主操作恢复
    screen.SelectSubTab(EnterpriseDetailScreen::kTabSvc);
    FA_EXPECT_FALSE(screen.GetFeaturesPage()->IsVisible());
    FA_EXPECT_TRUE(screen.FindCtrlById(kIdDetailPrimary)->IsVisible());
    //用 SelectSubTab 直接打开（行菜单「打开详情 → 功能开关」走这条路）
    screen.SelectSubTab(EnterpriseDetailScreen::kTabFeatures);
    FA_EXPECT_TRUE(screen.IsFeaturesTab());
    //非法序号不改变当前 tab
    screen.SelectSubTab(EnterpriseDetailScreen::kTabFeatures + 1);
    FA_EXPECT_TRUE(screen.IsFeaturesTab());
}

// FSW8：旧位置
void TestRemovedFromOldPlaces()
{
    BeginCase("FSW8 企业设置弹窗与平台设置页里已经没有任何功能开关");

    const int ids[] = { kIdFeatSwitchFileShare, kIdFeatSwitchP2P, kIdFeatSwitchInviteConfirm, kIdFeatSwitchRegister,
                        kIdFeatSwitchGroupCall, kIdFeatSwitchCaption, kIdFeatSwitchCloudRecord, kIdFeatSwitchSecretChat,
                        kIdFeatSwitchThreadGroup, kIdFeatSwitchBot, kIdFeatSwitchBotGroup, kIdFeatSwitchBotPersonal,
                        kIdFeatSwitchBotGroupExternal, kIdCloudQuotaSave, kIdCloudQuotaQuota, kIdCloudQuotaDays };
    EnterpriseSettingsModal modal;
    SettingsScreen settings;
    for (size_t i = 0; i < sizeof(ids) / sizeof(ids[0]); ++i)
    {
        FA_EXPECT_NULL(modal.FindCtrlById(ids[i]));
        FA_EXPECT_NULL(settings.FindCtrlById(ids[i]));
    }
}

} // anonymous namespace

// 运行本文件全部用例。
void RunAllFeatureSwitchesPageTests()
{
    TestStructure();
    TestTwoColumns();
    TestOneColumn();
    TestScroll();
    TestLoad();
    TestDispatch();
    TestDetailTab();
    TestRemovedFromOldPlaces();
    //恢复为未登录，免得影响之后的用例
    AdminSession::Instance().Clear();
}

}} // namespace flamingoadmin::test
