/**
 *  flamingoAdmin —— 文件共享管理界面的单元测试（docs/文件共享方案.md 第 10 节、第 12.3 节）。
 *
 *    - AT-5：带操作链接的表格按行隐藏链接：隐藏的链接点不中、不触发回调，其余链接位置不变；
 *    - KV-1：键值网格：默认取值「-」、设置与清空、高度、最后一行留空列后与满行对齐；
 *    - FSP-1：功能开关面板读设置：请求里企业的带法、读取中开关不可用、默认值与非默认值的状态文字、
 *             读取失败与缺少设置项；
 *    - FSP-2：拨动开关：先弹确认框（关闭为危险样式），取消时开关恢复、不提交；确认后提交并按应答更新；
 *    - FSP-3：提交失败时开关恢复并提示错误；别的企业晚到的应答被丢弃；
 *    - FS-1：「文件共享」页进入：企业管理员不显示企业筛选、请求不带企业；平台超管拉企业列表，默认全部企业；
 *            默认只看有效的共享；
 *    - FS-2：列表结果：行数、覆盖提示、各列文字、「强制取消」只在有效的行显示；
 *    - FS-3：选中行：详情卡各项、拉取成员、成员结果与标题；快速换行时只认最后一次的成员应答；
 *    - FS-4：强制取消：确认框正文，取消不提交，确认后提交，成功后提示并重新拉取；失败提示原因；
 *    - FS-5：筛选与翻页：搜索、状态、企业筛选（下标与企业 ID 的对应）、翻页；
 *    - FS-6：最小窗口尺寸下（放在文件共享页里，页头之下）表格至少显示三行；
 *    - FAU-1：「共享审计」页进入：默认近 7 天、全部动作、不限共享；平台超管拉企业列表；
 *    - FAU-2：筛选：时间范围、操作人、动作、共享 ID（非法输入按不限处理）；
 *    - FAU-3：结果与详情：各列文字（时间到分钟、管理员标注、路径带目标、失败结果）、详情各项；
 *    - FAU-4：最小窗口尺寸下（放在文件共享页里）筛选行不越界、表格至少显示三行；
 *    - FSC1～FSC3：文件共享页（「文件共享」「共享审计」两个页签的外层页面，2026-10-08 起「共享审计」不再是侧栏里单独的一项）：
 *             页签切换与「刷新」都在页头右侧（不另占一行）、默认在「文件共享」页签、副标题随页签；切换页签时显隐与页签控件同步、新页签按当前
 *             矩形排好并刷新、「刷新」只刷新当前页签；同一次登录内再次进入停在上次的页签，换了登录身份回到第一个页签；
 *    - FSN1：所有开关紧凑样式的短名称都不超过五个字（名称列按五个字定宽）；
 *    - SSS1、SSS3、SSS4：平台设置页的滚动（2026-10-05）：默认窗口下放得下、不滚动；最小窗口下「修改密码」
 *             按钮能滚进可视区；滚出本界面的控件点不到。2026-10-08 起本页的「功能开关」卡移到企业详情的「功能开关」子 tab，
 *             原核对该卡的 FSL-2（含 RSP2、GCSW2、CAPSW3、SCSW2 / TGSW2 的平台设置页部分）与 SSS2 随之删除；
 *    - RSP1：「允许用户自助注册」的面板：键为 register.enabled、默认开启、关闭不用危险样式、提交与提示；
 *    - GCSW1：「群语音 / 群视频通话」的面板（docs/群音视频通话方案.md 第 6 期）：键为 groupcall.enabled、默认开启、
 *             关闭不用危险样式、确认框说明进行中的群通话不受影响、提交与提示；
 *    - CAPSW2：「语音字幕」的面板（docs/语音字幕方案.md 第 10 节）：键为 caption.enabled、默认开启、关闭不用危险样式、
 *             确认框说明识别在本机进行与已生成的字幕文件不受影响、提交与提示；
 *    - CRSW1：「云端录制」的面板（docs/屏幕共享与录制方案.md 12.6.5，第 27 期 27g）：键为 cloudrecord.enabled、默认关闭、
 *             关闭不用危险样式、确认框说明已有录像不受影响、提交与提示；
 *    - CRQ1～CRQ3：「云端录像空间」面板：输入校验与状态文字；读设置（配额、保留天数、已用空间，企业对不上的应答丢弃，
 *             缺项按读取失败）；保存（没有修改不提交、不合法提示、只提交有变化的项、两个应答都回来才提示、失败后重新读取）；
 *    - SCSW1：「单聊密聊」的面板（docs/密聊方案.md 4.9，2026-10-07）：键为 secretchat.enabled、默认开启、关闭用危险样式
 *             （正在进行的密聊会被结束）、确认框说明双方只能查看已有内容、提交与提示；
 *    - TGSW1：「帖子群」的面板（docs/帖子群方案.md 4.7，2026-10-07）：键为 threadgroup.enabled、默认开启、关闭不用危险样式、
 *             确认框说明帖子与留言保留、重新打开后恢复、提交与提示；
 *    - ESS1～ESS5：企业设置弹窗的主体区滚动（2026-10-04）：2026-10-08 起弹窗里的「功能开关」块
 *             移到企业详情的「功能开关」子 tab，最小窗口下两位管理员时内容全部放得下、不滚动；十二位管理员时卡片不超出
 *             窗口、底部按钮可见、内容可滚动；滚到底 / 顶时首尾控件完整可见、滚动量不越界；滚出主体区的子控件点不到、
 *             标题栏与按钮栏的按钮始终点得到。原核对弹窗里功能开关块的 FSL-1 与核对两处位置的 CRL1 已删除，
 *             功能开关页的排列见 Tests/FeatureSwitchesPageTests.cpp 的 FSW 系列。
 *
 *  组件的请求出口与界面请求处理函数都换成记录下来，测试不碰网络、不弹窗口。
 *
 *  balloonwj@qq.com   2026-10-03
 */
#include "stdafx.h"
#include "Tests/TestFramework.h"
#include "Screens/FeatureSwitchPanel.h"
#include "Screens/CloudRecordQuotaPanel.h"
#include "Controls/Basic/DuiButton.h"
#include "Screens/FileSharesScreen.h"
#include "Screens/FileShareAuditScreen.h"
#include "Screens/FileShareCenterScreen.h"
#include "Widgets/CommonWidgets.h"
#include "Screens/EnterpriseSettingsModal.h"
#include "Screens/SettingsScreen.h"
#include "UiIds.h"
#include "Widgets/ActionTable.h"
#include "Widgets/KeyValueGrid.h"
#include "Util/FileShareText.h"
#include "Theme/AdminTheme.h"
#include "Controls/Input/DuiEdit.h"
#include "Controls/Input/DuiSwitch.h"
#include "json.h"

#include <string>
#include <vector>

namespace flamingoadmin { namespace test {

namespace {

using namespace flamingoadmin::adminapi;

// 测试用的界面矩形（默认窗口大小下内容区的量级）
const RECT kScreenRect = { 0, 0, 1200, 800 };

// 最小窗口（1100×720）下内容区的矩形：宽减去侧栏 224，高减去顶栏 46
const RECT kMinScreenRect = { 0, 0, 1100 - theme::kSidebarWidth, 720 - theme::kTopBarHeight };

// 面板测试用的矩形
const RECT kPanelRect = { 0, 0, 500, FeatureSwitchPanel::kPanelHeight };

// 测试用的企业 ID
const long long kEntA = 9001;
const long long kEntB = 9002;

// 一天的毫秒数
const long long kDayMs = 24LL * 60 * 60 * 1000;

// 时间范围的容差：测试取当前时间与界面取当前时间之间允许相差的毫秒数
const long long kClockToleranceMs = 60 * 1000;

// 一条记录的时间（Unix 毫秒），只用于比对格式
const long long kSampleTimeMs = 1790900000000LL;

// 共享表格与审计表格的行高（与界面里的常量相同），用于检查最小尺寸下的可见行数
const int kShareRowH = 48;
const int kAuditRowH = 44;
const int kMinVisibleRows = 3;

// 筛选行的左右外边距（与界面里的常量相同）
const int kSideMargin = 24;

// 平台设置页卡片的内边距（与界面里的常量相同）：输入框左边 = 卡片左边 + 内边距
const int kSettingsCardPad = 24;

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

// 某个标签的请求个数
int Count(const Captured& c, ApiTag tag)
{
    int n = 0;
    for (size_t i = 0; i < c.m_requests.size(); ++i)
    {
        if (c.m_requests[i].m_tag == tag)
            ++n;
    }
    return n;
}

// 某个标签最后一个请求的请求体；没有时返回 false
bool LastBody(const Captured& c, ApiTag tag, Json::Value& body)
{
    for (size_t i = c.m_requests.size(); i > 0; --i)
    {
        if (c.m_requests[i - 1].m_tag == tag)
            return ParseBody(c.m_requests[i - 1], body);
    }
    return false;
}

// 某种界面请求的个数
int CountUi(const Captured& c, int kind)
{
    int n = 0;
    for (size_t i = 0; i < c.m_ui.size(); ++i)
    {
        if (c.m_ui[i].m_kind == kind)
            ++n;
    }
    return n;
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

POINT CenterOf(const RECT& rc)
{
    POINT pt = { (rc.left + rc.right) / 2, (rc.top + rc.bottom) / 2 };
    return pt;
}

void LoginEnterpriseAdmin()
{
    AdminSession::Instance().Set(_T("tok-ent"), 1, _T("企业管理员"), _T("ent@test.com"), kEntA);
}

void LoginPlatformAdmin()
{
    AdminSession::Instance().Set(_T("tok-plat"), 2, _T("平台超管"), _T("plat@test.com"), 0);
}

// 读设置的应答
std::string SettingsJson(long long enterpriseId, const char* value, int isDefault)
{
    return "{\"code\":0,\"msg\":\"ok\",\"enterpriseid\":" + std::to_string(enterpriseId)
         + ",\"settings\":[{\"key\":\"fileshare.enabled\",\"value\":\"" + value + "\",\"isdefault\":"
         + std::to_string(isDefault) + "}]}";
}

// 读设置应答：文件共享与客户端点对点直连两项，都不是默认值
std::string TwoSettingsJson(long long enterpriseId, const char* fileShare, const char* p2p)
{
    return "{\"code\":0,\"msg\":\"ok\",\"enterpriseid\":" + std::to_string(enterpriseId)
         + ",\"settings\":[{\"key\":\"fileshare.enabled\",\"value\":\"" + fileShare + "\",\"isdefault\":0},"
         + "{\"key\":\"p2p.enabled\",\"value\":\"" + p2p + "\",\"isdefault\":0}]}";
}

// 企业列表应答：A、B 两个企业
std::string EnterprisesJson()
{
    return "{\"code\":0,\"msg\":\"ok\",\"total\":2,\"list\":["
           "{\"enterpriseid\":9001,\"name\":\"EntA\",\"status\":1},"
           "{\"enterpriseid\":9002,\"name\":\"EntB\",\"status\":1}]}";
}

// 共享列表应答：count 条，单数下标的共享已取消，总数为 total
std::string SharesJson(int count, int total)
{
    std::string json = "{\"code\":0,\"msg\":\"ok\",\"total\":" + std::to_string(total) + ",\"list\":[";
    for (int i = 0; i < count; ++i)
    {
        if (i > 0)
            json += ",";
        const bool canceled = (i % 2) == 1;
        json += "{\"shareid\":" + std::to_string(100 + i) + ",\"enterpriseid\":9001,\"ownerid\":5308,"
                "\"ownername\":\"owner\",\"owneraccount\":\"MY0308\",\"ctype\":2,\"cid\":268435470,"
                "\"conversationname\":\"grp\",\"kind\":2,\"name\":\"share" + std::to_string(i) + "\","
                "\"displaypath\":\"D:\\\\data\\\\share" + std::to_string(i) + "\",\"membercount\":2,"
                "\"state\":" + (canceled ? "2" : "1") + ",\"cancelreason\":" + (canceled ? "3" : "0") + ","
                "\"createtime\":" + std::to_string(kSampleTimeMs) + ",\"canceltime\":"
                + (canceled ? std::to_string(kSampleTimeMs + 1000) : std::string("0")) + "}";
    }
    json += "]}";
    return json;
}

// 成员应答：两个成员，一个可上传与删除，一个只读
std::string MembersJson()
{
    return "{\"code\":0,\"msg\":\"ok\",\"members\":[{\"userid\":5309,\"name\":\"m1\",\"account\":\"MY0309\","
           "\"perms\":9},{\"userid\":5310,\"name\":\"m2\",\"account\":\"MY0310\",\"perms\":0}]}";
}

// 审计应答：两条，第一条是成员重命名，第二条是管理员强制取消且失败
std::string AuditJson()
{
    return "{\"code\":0,\"msg\":\"ok\",\"total\":2,\"list\":["
           "{\"id\":11,\"time\":" + std::to_string(kSampleTimeMs) + ",\"shareid\":100,\"sharename\":\"docs\","
           "\"ownerid\":5308,\"ownername\":\"owner\",\"actorid\":5309,\"actorname\":\"m1\",\"actorisadmin\":0,"
           "\"action\":7,\"source\":2,\"path\":\"sub/a.txt\",\"topath\":\"b.txt\",\"size\":0,\"result\":0,"
           "\"accessid\":\"acc-1\"},"
           "{\"id\":12,\"time\":" + std::to_string(kSampleTimeMs) + ",\"shareid\":101,\"sharename\":\"x\","
           "\"ownerid\":5308,\"ownername\":\"owner\",\"actorid\":9901,\"actorname\":\"admin\",\"actorisadmin\":1,"
           "\"action\":11,\"source\":1,\"path\":\"\",\"topath\":\"\",\"size\":2048,\"result\":810,"
           "\"accessid\":\"\"}]}";
}

// 测试自己取的当前时间（Unix 毫秒），与界面的 NowMs 相互独立
long long TestNowMs()
{
    FILETIME ft;
    ::GetSystemTimeAsFileTime(&ft);
    ULARGE_INTEGER t;
    t.LowPart = ft.dwLowDateTime;
    t.HighPart = ft.dwHighDateTime;
    const unsigned long long kEpochDiff100ns = 116444736000000000ULL;
    const unsigned long long k100nsPerMs = 10000ULL;
    return static_cast<long long>((t.QuadPart - kEpochDiff100ns) / k100nsPerMs);
}

// 表格回调记录
struct ActionRecord
{
    int m_calls;   // 回调次数
    int m_row;     // 最近一次的行
};

void OnAction(void* user, int rowIndex, int actionIndex)
{
    (void)actionIndex;
    ActionRecord* rec = static_cast<ActionRecord*>(user);
    ++rec->m_calls;
    rec->m_row = rowIndex;
}

// 第 0 行隐藏第 1 个链接，其余都显示
bool HideRow0Action1(void* user, int rowIndex, int actionIndex)
{
    (void)user;
    return !(rowIndex == 0 && actionIndex == 1);
}

} // namespace

// AT-5：按行隐藏链接
void TestActionTableVisibility()
{
    BeginCase("AT-5 带操作链接的表格：按行隐藏链接");

    const RECT rc = { 0, 0, 800, 400 };
    const int rowH = 40;
    ui::ActionTable table;
    table.SetRowHeight(rowH);
    table.SetRect(rc);
    table.SetRowCount(3);
    table.SetActionAreaWidth(160);
    table.AddAction(_T("编辑"), theme::kColorInfo);
    table.AddAction(_T("删除"), theme::kColorDanger);
    ActionRecord rec = { 0, -1 };
    table.SetActionCallback(&OnAction, &rec);

    //没有回调时全部显示
    FA_EXPECT_TRUE(table.IsActionVisible(0, 1));

    table.SetActionVisibility(&HideRow0Action1, nullptr);
    FA_EXPECT_FALSE(table.IsActionVisible(0, 1));
    FA_EXPECT_TRUE(table.IsActionVisible(0, 0));
    FA_EXPECT_TRUE(table.IsActionVisible(1, 1));

    //隐藏的链接点不中、不触发回调
    const POINT hidden = CenterOf(table.GetLinkRect(0, 1));
    FA_EXPECT_EQ(table.ActionFromPoint(hidden, nullptr), -1);
    table.OnLButtonUp(hidden, 0);
    FA_EXPECT_EQ(rec.m_calls, 0);

    //别的行同一位置的链接照常可点，且位置不因第 0 行的隐藏而变化
    const RECT row1 = table.GetLinkRect(1, 1);
    FA_EXPECT_EQ(row1.left, table.GetLinkRect(0, 1).left);
    table.OnLButtonUp(CenterOf(row1), 0);
    FA_EXPECT_EQ(rec.m_calls, 1);
    FA_EXPECT_EQ(rec.m_row, 1);
}

// KV-1：键值网格
void TestKeyValueGrid()
{
    BeginCase("KV-1 键值网格：取值、高度与列对齐");

    const int lineH = 26;
    ui::KeyValueGrid grid;
    grid.SetColumnCount(3);
    grid.SetLineHeight(lineH);
    const int a = grid.AddItem(_T("甲"));
    const int b = grid.AddItem(_T("乙"));
    const int c = grid.AddItem(_T("丙"));
    const int d = grid.AddItem(_T("丁"));   //第二行只有一项
    const int w = grid.AddWideItem(_T("路径"), 2);
    grid.EndRow();

    FA_EXPECT_EQ(grid.ItemCount(), 5);
    FA_EXPECT_EQ(a, 0);
    FA_EXPECT_EQ(d, 3);
    FA_EXPECT_EQ(w, 4);
    //两个单行行 + 一个两行高的行
    FA_EXPECT_EQ(grid.ContentHeight(), lineH * 4);

    FA_EXPECT_STREQ(grid.GetValue(b), _T("-"));
    grid.SetValue(b, _T("值"));
    FA_EXPECT_STREQ(grid.GetValue(b), _T("值"));
    grid.SetValue(c, CString());
    FA_EXPECT_STREQ(grid.GetValue(c), _T("-"));
    grid.SetValue(99, _T("越界"));
    FA_EXPECT_STREQ(grid.GetValue(99), _T(""));
    grid.ClearValues();
    FA_EXPECT_STREQ(grid.GetValue(b), _T("-"));

    //第二行只有一项：它与第一行第一项同宽（留空的列用空白占住）
    const RECT gridRc = { 0, 0, 900, grid.ContentHeight() };
    grid.SetRect(gridRc);
    const RECT ra = grid.ValueRect(a);
    const RECT rd = grid.ValueRect(d);
    FA_EXPECT_EQ(rd.left, ra.left);
    FA_EXPECT_EQ(rd.right, ra.right);
    //独占一行的条目比单列宽、高为两行
    const RECT rw = grid.ValueRect(w);
    FA_EXPECT_TRUE(rw.right > grid.ValueRect(c).left);
    FA_EXPECT_EQ(rw.bottom - rw.top, lineH * 2);
}

// FSP-1：读设置
void TestFeaturePanelLoad()
{
    BeginCase("FSP-1 功能开关面板：读设置");

    LoginPlatformAdmin();
    Captured cap;
    FeatureSwitchPanel panel(FileShareSwitchSpec(), kApiTagFeatureGet, kApiTagFeatureSet, 600);
    panel.SetRequestSink(&CaptureRequest, &cap);
    panel.SetUiRequestHandler(&CaptureUi, &cap);
    panel.SetRect(kPanelRect);

    FA_EXPECT_EQ(panel.GetState(), static_cast<int>(FeatureSwitchPanel::kStateIdle));
    FA_EXPECT_FALSE(panel.IsSwitchEnabled());

    panel.Load(kEntA);
    Json::Value body;
    FA_EXPECT_TRUE(LastBody(cap, kApiTagFeatureGet, body));
    FA_EXPECT_TRUE(body["enterpriseid"].asInt64() == kEntA);
    FA_EXPECT_FALSE(panel.IsSwitchEnabled());
    FA_EXPECT_STREQ(panel.StatusText(), _T("正在读取…"));

    panel.OnApiResult(kApiTagFeatureGet, MakeHttpOk(SettingsJson(kEntA, "1", 1)));
    FA_EXPECT_EQ(panel.GetState(), static_cast<int>(FeatureSwitchPanel::kStateLoaded));
    FA_EXPECT_TRUE(panel.IsSwitchEnabled());
    FA_EXPECT_TRUE(panel.IsSwitchChecked());
    FA_EXPECT_STREQ(panel.StatusText(), _T("当前为默认设置：开启"));

    //重新为同一企业读：有设置记录、取值为关闭
    panel.Load(kEntA);
    panel.OnApiResult(kApiTagFeatureGet, MakeHttpOk(SettingsJson(kEntA, "0", 0)));
    FA_EXPECT_FALSE(panel.IsSwitchChecked());
    FA_EXPECT_STREQ(panel.StatusText(), _T("已由管理员设置为关闭"));

    //企业管理员：请求里不带 enterpriseid
    LoginEnterpriseAdmin();
    Captured own;
    FeatureSwitchPanel ownPanel(FileShareSwitchSpec(), kApiTagFeatureGet, kApiTagFeatureSet, kIdFeatSwitchFileShare);
    ownPanel.SetRequestSink(&CaptureRequest, &own);
    ownPanel.SetUiRequestHandler(&CaptureUi, &own);
    ownPanel.Load(0);
    FA_EXPECT_TRUE(LastBody(own, kApiTagFeatureGet, body));
    FA_EXPECT_FALSE(body.isMember("enterpriseid"));
    //别的面板的标签不处理
    ownPanel.OnApiResult(kApiTagP2PSwitchGet, MakeHttpOk(SettingsJson(kEntA, "1", 1)));
    FA_EXPECT_EQ(ownPanel.GetState(), static_cast<int>(FeatureSwitchPanel::kStateLoading));
    //读取失败：开关不可用，状态行给出原因
    ownPanel.OnApiResult(kApiTagFeatureGet, MakeHttpOk("{\"code\":2,\"msg\":\"x\"}"));
    FA_EXPECT_EQ(ownPanel.GetState(), static_cast<int>(FeatureSwitchPanel::kStateFailed));
    FA_EXPECT_FALSE(ownPanel.IsSwitchEnabled());
    FA_EXPECT_STREQ(ownPanel.StatusText(), _T("读取失败：登录已过期，请重新登录"));
    //应答里没有 fileshare.enabled：按读取失败处理
    ownPanel.Load(0);
    ownPanel.OnApiResult(kApiTagFeatureGet, MakeHttpOk("{\"code\":0,\"msg\":\"ok\",\"settings\":[]}"));
    FA_EXPECT_EQ(ownPanel.GetState(), static_cast<int>(FeatureSwitchPanel::kStateFailed));

    FA_EXPECT_STREQ(FeatureSwitchPanel::StatusFor(true, false), _T("已由管理员设置为开启"));
    FA_EXPECT_STREQ(FeatureSwitchPanel::StatusFor(false, true), _T("当前为默认设置：关闭"));
}

// FSP-2：拨动开关
void TestFeaturePanelToggle()
{
    BeginCase("FSP-2 功能开关面板：确认后才提交");

    LoginPlatformAdmin();
    Captured cap;
    FeatureSwitchPanel panel(FileShareSwitchSpec(), kApiTagFeatureGet, kApiTagFeatureSet, 600);
    panel.SetRequestSink(&CaptureRequest, &cap);
    panel.SetUiRequestHandler(&CaptureUi, &cap);
    panel.SetRect(kPanelRect);
    panel.Load(kEntA);
    panel.OnApiResult(kApiTagFeatureGet, MakeHttpOk(SettingsJson(kEntA, "1", 1)));

    //用户把开关拨到关：先弹确认框，危险样式
    panel.Switch()->SetChecked(false, false, false);
    panel.OnSwitchToggled(false);
    FA_EXPECT_EQ(CountUi(cap, fsui::kUiConfirm), 1);
    fsui::UiRequest confirm = LastUi(cap, fsui::kUiConfirm);
    FA_EXPECT_STREQ(confirm.m_title, _T("关闭文件共享"));
    FA_EXPECT_TRUE(confirm.m_danger);
    FA_EXPECT_TRUE(confirm.m_target == &panel);
    FA_EXPECT_TRUE(confirm.m_text.Find(_T("暂不可用")) >= 0);
    FA_EXPECT_EQ(Count(cap, kApiTagFeatureSet), 0);

    //取消：开关回到开，不提交
    confirm.m_target->OnConfirmResult(false);
    FA_EXPECT_TRUE(panel.IsSwitchChecked());
    FA_EXPECT_EQ(Count(cap, kApiTagFeatureSet), 0);

    //再拨一次并确认：提交 value=0
    panel.Switch()->SetChecked(false, false, false);
    panel.OnSwitchToggled(false);
    LastUi(cap, fsui::kUiConfirm).m_target->OnConfirmResult(true);
    FA_EXPECT_EQ(Count(cap, kApiTagFeatureSet), 1);
    Json::Value body;
    FA_EXPECT_TRUE(LastBody(cap, kApiTagFeatureSet, body));
    FA_EXPECT_TRUE(body["key"].asString() == "fileshare.enabled");
    FA_EXPECT_TRUE(body["value"].asString() == "0");
    FA_EXPECT_TRUE(body["enterpriseid"].asInt64() == kEntA);
    FA_EXPECT_EQ(panel.GetState(), static_cast<int>(FeatureSwitchPanel::kStateSaving));
    FA_EXPECT_FALSE(panel.IsSwitchEnabled());
    FA_EXPECT_FALSE(panel.IsSwitchChecked());

    //提交中再拨：不弹第二个确认框
    panel.OnSwitchToggled(true);
    FA_EXPECT_EQ(CountUi(cap, fsui::kUiConfirm), 2);

    panel.OnApiResult(kApiTagFeatureSet, MakeHttpOk("{\"code\":0,\"msg\":\"ok\",\"enterpriseid\":9001,\"enabled\":0}"));
    FA_EXPECT_EQ(panel.GetState(), static_cast<int>(FeatureSwitchPanel::kStateLoaded));
    FA_EXPECT_FALSE(panel.IsSwitchChecked());
    FA_EXPECT_TRUE(panel.IsSwitchEnabled());
    FA_EXPECT_STREQ(panel.StatusText(), _T("已由管理员设置为关闭"));
    FA_EXPECT_STREQ(LastUi(cap, fsui::kUiToast).m_text, _T("已关闭文件共享"));

    //打开：确认框不是危险样式
    panel.Switch()->SetChecked(true, false, false);
    panel.OnSwitchToggled(true);
    confirm = LastUi(cap, fsui::kUiConfirm);
    FA_EXPECT_STREQ(confirm.m_title, _T("打开文件共享"));
    FA_EXPECT_FALSE(confirm.m_danger);
    confirm.m_target->OnConfirmResult(true);
    FA_EXPECT_TRUE(LastBody(cap, kApiTagFeatureSet, body));
    FA_EXPECT_TRUE(body["value"].asString() == "1");
}

// FSP-4：客户端点对点直连的面板（docs/P2P通信方案.md 9.5，第 2 期 2g）
void TestFeaturePanelP2P()
{
    BeginCase("FSP-4 功能开关面板：客户端点对点直连");

    LoginPlatformAdmin();
    Captured cap;
    FeatureSwitchPanel panel(P2PSwitchSpec(), kApiTagP2PSwitchGet, kApiTagP2PSwitchSet, kIdFeatSwitchP2P);
    panel.SetRequestSink(&CaptureRequest, &cap);
    panel.SetUiRequestHandler(&CaptureUi, &cap);
    panel.SetRect(kPanelRect);
    panel.Load(kEntA);
    Json::Value body;
    FA_EXPECT_TRUE(LastBody(cap, kApiTagP2PSwitchGet, body));

    //同一份应答里两项都有（文件共享开、点对点直连关）：只取 p2p.enabled
    panel.OnApiResult(kApiTagP2PSwitchGet, MakeHttpOk(TwoSettingsJson(kEntA, "1", "0")));
    FA_EXPECT_EQ(panel.GetState(), static_cast<int>(FeatureSwitchPanel::kStateLoaded));
    FA_EXPECT_FALSE(panel.IsSwitchChecked());
    FA_EXPECT_STREQ(panel.StatusText(), _T("已由管理员设置为关闭"));

    //打开：确认框标题带全称、不是危险样式；确认后提交 p2p.enabled=1，成功后提示
    panel.Switch()->SetChecked(true, false, false);
    panel.OnSwitchToggled(true);
    fsui::UiRequest confirm = LastUi(cap, fsui::kUiConfirm);
    FA_EXPECT_STREQ(confirm.m_title, _T("打开客户端点对点直连"));
    FA_EXPECT_FALSE(confirm.m_danger);
    //没有弹出确认框时 m_target 为空：记一次失败后结束本用例，不让空指针让整个测试程序崩溃
    FA_EXPECT_NOTNULL(confirm.m_target);
    if (confirm.m_target == nullptr)
        return;
    confirm.m_target->OnConfirmResult(true);
    FA_EXPECT_TRUE(LastBody(cap, kApiTagP2PSwitchSet, body));
    FA_EXPECT_TRUE(body["key"].asString() == "p2p.enabled");
    FA_EXPECT_TRUE(body["value"].asString() == "1");
    FA_EXPECT_TRUE(body["enterpriseid"].asInt64() == kEntA);
    panel.OnApiResult(kApiTagP2PSwitchSet, MakeHttpOk("{\"code\":0,\"msg\":\"ok\",\"enterpriseid\":9001,\"enabled\":1}"));
    FA_EXPECT_TRUE(panel.IsSwitchChecked());
    FA_EXPECT_STREQ(LastUi(cap, fsui::kUiToast).m_text, _T("已打开客户端点对点直连"));

    //关闭：已建立的直连不会中断，所以不用危险样式，正文说明这一点；取消后开关回到开
    panel.Switch()->SetChecked(false, false, false);
    panel.OnSwitchToggled(false);
    confirm = LastUi(cap, fsui::kUiConfirm);
    FA_EXPECT_STREQ(confirm.m_title, _T("关闭客户端点对点直连"));
    FA_EXPECT_FALSE(confirm.m_danger);
    FA_EXPECT_TRUE(confirm.m_text.Find(_T("不会中断")) >= 0);
    FA_EXPECT_NOTNULL(confirm.m_target);
    if (confirm.m_target == nullptr)
        return;
    confirm.m_target->OnConfirmResult(false);
    FA_EXPECT_TRUE(panel.IsSwitchChecked());

    //应答里只有文件共享一项（服务端版本较旧）：按读取失败处理，原因写明是点对点直连
    panel.Load(kEntA);
    panel.OnApiResult(kApiTagP2PSwitchGet, MakeHttpOk(SettingsJson(kEntA, "1", 1)));
    FA_EXPECT_EQ(panel.GetState(), static_cast<int>(FeatureSwitchPanel::kStateFailed));
    FA_EXPECT_STREQ(panel.StatusText(), _T("读取失败：服务端没有返回客户端点对点直连的设置"));

    //文件共享的面板在同一份应答里取 fileshare.enabled
    Captured fsCap;
    FeatureSwitchPanel fsPanel(FileShareSwitchSpec(), kApiTagFeatureGet, kApiTagFeatureSet, kIdFeatSwitchFileShare);
    fsPanel.SetRequestSink(&CaptureRequest, &fsCap);
    fsPanel.SetUiRequestHandler(&CaptureUi, &fsCap);
    fsPanel.Load(kEntA);
    fsPanel.OnApiResult(kApiTagFeatureGet, MakeHttpOk(TwoSettingsJson(kEntA, "1", "0")));
    FA_EXPECT_TRUE(fsPanel.IsSwitchChecked());

    //描述本身：设置项名与紧凑样式的短名称
    FA_EXPECT_STREQ(P2PSwitchSpec().m_key, _T("p2p.enabled"));
    FA_EXPECT_STREQ(P2PSwitchSpec().m_compactName, _T("点对点直连"));
    FA_EXPECT_STREQ(FileShareSwitchSpec().m_key, _T("fileshare.enabled"));
}

// FA1：「邀请入群需被邀请人同意」的面板（docs/邀请入群方案.md §3.5，第 3 期 3b）
void TestFeaturePanelInviteConfirm()
{
    BeginCase("FA1 功能开关面板：邀请入群需被邀请人同意，键为 group.inviteconfirm，默认关闭");

    LoginPlatformAdmin();
    Captured cap;
    FeatureSwitchPanel panel(InviteConfirmSwitchSpec(), kApiTagInviteConfirmGet, kApiTagInviteConfirmSet,
                             kIdFeatSwitchInviteConfirm, true);
    panel.SetRequestSink(&CaptureRequest, &cap);
    panel.SetUiRequestHandler(&CaptureUi, &cap);
    panel.SetRect(kPanelRect);
    panel.Load(kEntA);
    Json::Value body;
    FA_EXPECT_TRUE(LastBody(cap, kApiTagInviteConfirmGet, body));

    //三项都在同一份应答里、本项为内置默认值（关闭）：只取 group.inviteconfirm，开关为关、写明是默认设置
    const std::string threeSettings =
        "{\"code\":0,\"msg\":\"ok\",\"enterpriseid\":" + std::to_string(kEntA) + ",\"settings\":["
        "{\"key\":\"fileshare.enabled\",\"value\":\"1\",\"isdefault\":1},"
        "{\"key\":\"p2p.enabled\",\"value\":\"1\",\"isdefault\":1},"
        "{\"key\":\"group.inviteconfirm\",\"value\":\"0\",\"isdefault\":1}]}";
    panel.OnApiResult(kApiTagInviteConfirmGet, MakeHttpOk(threeSettings));
    FA_EXPECT_EQ(panel.GetState(), static_cast<int>(FeatureSwitchPanel::kStateLoaded));
    FA_EXPECT_FALSE(panel.IsSwitchChecked());
    FA_EXPECT_STREQ(panel.StatusText(), _T("当前为默认设置：关闭"));

    //打开：确认框标题带全称、不是危险样式；确认后提交 group.inviteconfirm=1，成功后提示
    panel.Switch()->SetChecked(true, false, false);
    panel.OnSwitchToggled(true);
    fsui::UiRequest confirm = LastUi(cap, fsui::kUiConfirm);
    FA_EXPECT_STREQ(confirm.m_title, _T("打开邀请入群需被邀请人同意"));
    FA_EXPECT_FALSE(confirm.m_danger);
    FA_EXPECT_NOTNULL(confirm.m_target);
    if (confirm.m_target == nullptr)
        return;
    confirm.m_target->OnConfirmResult(true);
    FA_EXPECT_TRUE(LastBody(cap, kApiTagInviteConfirmSet, body));
    FA_EXPECT_TRUE(body["key"].asString() == "group.inviteconfirm");
    FA_EXPECT_TRUE(body["value"].asString() == "1");
    FA_EXPECT_TRUE(body["enterpriseid"].asInt64() == kEntA);
    panel.OnApiResult(kApiTagInviteConfirmSet,
                      MakeHttpOk("{\"code\":0,\"msg\":\"ok\",\"enterpriseid\":9001,\"enabled\":1}"));
    FA_EXPECT_TRUE(panel.IsSwitchChecked());
    FA_EXPECT_STREQ(LastUi(cap, fsui::kUiToast).m_text, _T("已打开邀请入群需被邀请人同意"));

    //关闭：已发出的卡片照样可以入群，不用危险样式，正文说明这一点
    panel.Switch()->SetChecked(false, false, false);
    panel.OnSwitchToggled(false);
    confirm = LastUi(cap, fsui::kUiConfirm);
    FA_EXPECT_STREQ(confirm.m_title, _T("关闭邀请入群需被邀请人同意"));
    FA_EXPECT_FALSE(confirm.m_danger);
    FA_EXPECT_TRUE(confirm.m_text.Find(_T("照样可以点击入群")) >= 0);

    //描述本身：设置项名与紧凑样式的短名称
    FA_EXPECT_STREQ(InviteConfirmSwitchSpec().m_key, _T("group.inviteconfirm"));
    FA_EXPECT_STREQ(InviteConfirmSwitchSpec().m_compactName, _T("入群需同意"));
}

// RSP1：「允许用户自助注册」的面板（register.enabled，2026-10-04）
void TestFeaturePanelRegister()
{
    BeginCase("RSP1 功能开关面板：允许用户自助注册，键为 register.enabled，默认开启，关闭不用危险样式");

    //描述本身：设置项名、全称与紧凑样式的短名称，关闭不中断任何业务
    const FeatureSwitchSpec& spec = RegisterSwitchSpec();
    FA_EXPECT_STREQ(spec.m_key, _T("register.enabled"));
    FA_EXPECT_STREQ(spec.m_name, _T("允许用户自助注册"));
    FA_EXPECT_STREQ(spec.m_compactName, _T("自助注册"));
    FA_EXPECT_FALSE(spec.m_offIsDanger);

    LoginPlatformAdmin();
    Captured cap;
    FeatureSwitchPanel panel(spec, kApiTagRegisterSwitchGet, kApiTagRegisterSwitchSet, kIdFeatSwitchRegister, true);
    panel.SetRequestSink(&CaptureRequest, &cap);
    panel.SetUiRequestHandler(&CaptureUi, &cap);
    panel.SetRect(kPanelRect);
    panel.Load(kEntA);
    Json::Value body;
    FA_EXPECT_TRUE(LastBody(cap, kApiTagRegisterSwitchGet, body));

    //四项都在同一份应答里、本项为内置默认值（开启）：只取 register.enabled，开关为开、写明是默认设置
    const std::string fourSettings =
        "{\"code\":0,\"msg\":\"ok\",\"enterpriseid\":" + std::to_string(kEntA) + ",\"settings\":["
        "{\"key\":\"fileshare.enabled\",\"value\":\"0\",\"isdefault\":0},"
        "{\"key\":\"p2p.enabled\",\"value\":\"1\",\"isdefault\":1},"
        "{\"key\":\"group.inviteconfirm\",\"value\":\"0\",\"isdefault\":1},"
        "{\"key\":\"register.enabled\",\"value\":\"1\",\"isdefault\":1}]}";
    panel.OnApiResult(kApiTagRegisterSwitchGet, MakeHttpOk(fourSettings));
    FA_EXPECT_EQ(panel.GetState(), static_cast<int>(FeatureSwitchPanel::kStateLoaded));
    FA_EXPECT_TRUE(panel.IsSwitchChecked());
    FA_EXPECT_STREQ(panel.StatusText(), _T("当前为默认设置：开启"));

    //关闭：确认框标题带全称、不是危险样式，正文说明只能由管理员新建账号；确认后提交 register.enabled=0
    panel.Switch()->SetChecked(false, false, false);
    panel.OnSwitchToggled(false);
    fsui::UiRequest confirm = LastUi(cap, fsui::kUiConfirm);
    FA_EXPECT_STREQ(confirm.m_title, _T("关闭允许用户自助注册"));
    FA_EXPECT_FALSE(confirm.m_danger);
    FA_EXPECT_TRUE(confirm.m_text.Find(_T("只能由管理员")) >= 0);
    FA_EXPECT_NOTNULL(confirm.m_target);
    if (confirm.m_target == nullptr)
        return;
    confirm.m_target->OnConfirmResult(true);
    FA_EXPECT_TRUE(LastBody(cap, kApiTagRegisterSwitchSet, body));
    FA_EXPECT_TRUE(body["key"].asString() == "register.enabled");
    FA_EXPECT_TRUE(body["value"].asString() == "0");
    FA_EXPECT_TRUE(body["enterpriseid"].asInt64() == kEntA);
    panel.OnApiResult(kApiTagRegisterSwitchSet,
                      MakeHttpOk("{\"code\":0,\"msg\":\"ok\",\"enterpriseid\":9001,\"enabled\":0}"));
    FA_EXPECT_FALSE(panel.IsSwitchChecked());
    FA_EXPECT_STREQ(LastUi(cap, fsui::kUiToast).m_text, _T("已关闭允许用户自助注册"));
    FA_EXPECT_STREQ(panel.StatusText(), _T("已由管理员设置为关闭"));
}

// GCSW1：「群语音 / 群视频通话」的面板（groupcall.enabled，docs/群音视频通话方案.md 第 6 期）
void TestFeaturePanelGroupCall()
{
    BeginCase("GCSW1 功能开关面板：群语音 / 群视频通话，键为 groupcall.enabled，默认开启，关闭不用危险样式");

    //描述本身：设置项名、全称与紧凑样式的短名称；关闭只拦新的发起与加入，不中断进行中的群通话
    const FeatureSwitchSpec& spec = GroupCallSwitchSpec();
    FA_EXPECT_STREQ(spec.m_key, _T("groupcall.enabled"));
    FA_EXPECT_STREQ(spec.m_name, _T("群语音 / 群视频通话"));
    FA_EXPECT_STREQ(spec.m_compactName, _T("群通话"));
    FA_EXPECT_FALSE(spec.m_offIsDanger);

    LoginPlatformAdmin();
    Captured cap;
    FeatureSwitchPanel panel(spec, kApiTagGroupCallSwitchGet, kApiTagGroupCallSwitchSet, kIdFeatSwitchGroupCall,
                             true);
    panel.SetRequestSink(&CaptureRequest, &cap);
    panel.SetUiRequestHandler(&CaptureUi, &cap);
    panel.SetRect(kPanelRect);
    panel.Load(kEntA);
    Json::Value body;
    FA_EXPECT_TRUE(LastBody(cap, kApiTagGroupCallSwitchGet, body));

    //几项都在同一份应答里、本项为内置默认值（开启）：只取 groupcall.enabled，开关为开、写明是默认设置
    const std::string settings =
        "{\"code\":0,\"msg\":\"ok\",\"enterpriseid\":" + std::to_string(kEntA) + ",\"settings\":["
        "{\"key\":\"fileshare.enabled\",\"value\":\"0\",\"isdefault\":0},"
        "{\"key\":\"register.enabled\",\"value\":\"0\",\"isdefault\":0},"
        "{\"key\":\"groupcall.enabled\",\"value\":\"1\",\"isdefault\":1}]}";
    panel.OnApiResult(kApiTagGroupCallSwitchGet, MakeHttpOk(settings));
    FA_EXPECT_EQ(panel.GetState(), static_cast<int>(FeatureSwitchPanel::kStateLoaded));
    FA_EXPECT_TRUE(panel.IsSwitchChecked());
    FA_EXPECT_STREQ(panel.StatusText(), _T("当前为默认设置：开启"));

    //关闭：确认框标题带全称、不是危险样式，正文说明进行中的群通话不受影响；确认后提交 groupcall.enabled=0
    panel.Switch()->SetChecked(false, false, false);
    panel.OnSwitchToggled(false);
    fsui::UiRequest confirm = LastUi(cap, fsui::kUiConfirm);
    FA_EXPECT_STREQ(confirm.m_title, _T("关闭群语音 / 群视频通话"));
    FA_EXPECT_FALSE(confirm.m_danger);
    FA_EXPECT_TRUE(confirm.m_text.Find(_T("正在进行的群通话不受影响")) >= 0);
    FA_EXPECT_NOTNULL(confirm.m_target);
    if (confirm.m_target == nullptr)
        return;
    confirm.m_target->OnConfirmResult(true);
    FA_EXPECT_TRUE(LastBody(cap, kApiTagGroupCallSwitchSet, body));
    FA_EXPECT_TRUE(body["key"].asString() == "groupcall.enabled");
    FA_EXPECT_TRUE(body["value"].asString() == "0");
    FA_EXPECT_TRUE(body["enterpriseid"].asInt64() == kEntA);
    panel.OnApiResult(kApiTagGroupCallSwitchSet,
                      MakeHttpOk("{\"code\":0,\"msg\":\"ok\",\"enterpriseid\":9001,\"enabled\":0}"));
    FA_EXPECT_FALSE(panel.IsSwitchChecked());
    FA_EXPECT_STREQ(LastUi(cap, fsui::kUiToast).m_text, _T("已关闭群语音 / 群视频通话"));
    FA_EXPECT_STREQ(panel.StatusText(), _T("已由管理员设置为关闭"));

    //再打开：确认框正文说明打开后的效果，提交 groupcall.enabled=1
    panel.Switch()->SetChecked(true, false, false);
    panel.OnSwitchToggled(true);
    confirm = LastUi(cap, fsui::kUiConfirm);
    FA_EXPECT_STREQ(confirm.m_title, _T("打开群语音 / 群视频通话"));
    FA_EXPECT_TRUE(confirm.m_text.Find(_T("发起或加入")) >= 0);
    if (confirm.m_target == nullptr)
        return;
    confirm.m_target->OnConfirmResult(true);
    FA_EXPECT_TRUE(LastBody(cap, kApiTagGroupCallSwitchSet, body));
    FA_EXPECT_TRUE(body["value"].asString() == "1");
}

// CAPSW2：「语音字幕」的面板（caption.enabled，docs/语音字幕方案.md 第 10 节）
void TestFeaturePanelCaption()
{
    BeginCase("CAPSW2 功能开关面板：语音字幕，键为 caption.enabled，默认开启，关闭不用危险样式");

    //描述本身：设置项名、全称与紧凑样式的短名称；识别在成员本机完成，关闭不中断任何业务
    const FeatureSwitchSpec& spec = CaptionSwitchSpec();
    FA_EXPECT_STREQ(spec.m_key, _T("caption.enabled"));
    FA_EXPECT_STREQ(spec.m_name, _T("语音字幕"));
    FA_EXPECT_STREQ(spec.m_compactName, _T("语音字幕"));
    FA_EXPECT_FALSE(spec.m_offIsDanger);

    LoginPlatformAdmin();
    Captured cap;
    FeatureSwitchPanel panel(spec, kApiTagCaptionSwitchGet, kApiTagCaptionSwitchSet, kIdFeatSwitchCaption, true);
    panel.SetRequestSink(&CaptureRequest, &cap);
    panel.SetUiRequestHandler(&CaptureUi, &cap);
    panel.SetRect(kPanelRect);
    panel.Load(kEntA);
    Json::Value body;
    FA_EXPECT_TRUE(LastBody(cap, kApiTagCaptionSwitchGet, body));

    //几项都在同一份应答里、本项为内置默认值（开启）：只取 caption.enabled，开关为开、写明是默认设置
    const std::string settings =
        "{\"code\":0,\"msg\":\"ok\",\"enterpriseid\":" + std::to_string(kEntA) + ",\"settings\":["
        "{\"key\":\"groupcall.enabled\",\"value\":\"0\",\"isdefault\":0},"
        "{\"key\":\"caption.enabled\",\"value\":\"1\",\"isdefault\":1}]}";
    panel.OnApiResult(kApiTagCaptionSwitchGet, MakeHttpOk(settings));
    FA_EXPECT_EQ(panel.GetState(), static_cast<int>(FeatureSwitchPanel::kStateLoaded));
    FA_EXPECT_TRUE(panel.IsSwitchChecked());
    FA_EXPECT_STREQ(panel.StatusText(), _T("当前为默认设置：开启"));

    //关闭：确认框标题带全称、不是危险样式，正文说明已生成的字幕文件不受影响；确认后提交 caption.enabled=0
    panel.Switch()->SetChecked(false, false, false);
    panel.OnSwitchToggled(false);
    fsui::UiRequest confirm = LastUi(cap, fsui::kUiConfirm);
    FA_EXPECT_STREQ(confirm.m_title, _T("关闭语音字幕"));
    FA_EXPECT_FALSE(confirm.m_danger);
    FA_EXPECT_TRUE(confirm.m_text.Find(_T("已经生成的字幕文件不受影响")) >= 0);
    FA_EXPECT_NOTNULL(confirm.m_target);
    if (confirm.m_target == nullptr)
        return;
    confirm.m_target->OnConfirmResult(true);
    FA_EXPECT_TRUE(LastBody(cap, kApiTagCaptionSwitchSet, body));
    FA_EXPECT_TRUE(body["key"].asString() == "caption.enabled");
    FA_EXPECT_TRUE(body["value"].asString() == "0");
    FA_EXPECT_TRUE(body["enterpriseid"].asInt64() == kEntA);
    panel.OnApiResult(kApiTagCaptionSwitchSet,
                      MakeHttpOk("{\"code\":0,\"msg\":\"ok\",\"enterpriseid\":9001,\"enabled\":0}"));
    FA_EXPECT_FALSE(panel.IsSwitchChecked());
    FA_EXPECT_STREQ(LastUi(cap, fsui::kUiToast).m_text, _T("已关闭语音字幕"));
    FA_EXPECT_STREQ(panel.StatusText(), _T("已由管理员设置为关闭"));

    //再打开：确认框正文说明识别在本机进行、声音不上传，提交 caption.enabled=1
    panel.Switch()->SetChecked(true, false, false);
    panel.OnSwitchToggled(true);
    confirm = LastUi(cap, fsui::kUiConfirm);
    FA_EXPECT_STREQ(confirm.m_title, _T("打开语音字幕"));
    FA_EXPECT_TRUE(confirm.m_text.Find(_T("声音不会上传")) >= 0);
    if (confirm.m_target == nullptr)
        return;
    confirm.m_target->OnConfirmResult(true);
    FA_EXPECT_TRUE(LastBody(cap, kApiTagCaptionSwitchSet, body));
    FA_EXPECT_TRUE(body["value"].asString() == "1");
}

// SCSW1：「单聊密聊」的面板（secretchat.enabled，docs/密聊方案.md 4.9）
void TestFeaturePanelSecretChat()
{
    BeginCase("SCSW1 功能开关面板：单聊密聊，键为 secretchat.enabled，默认开启，关闭用危险样式");

    //描述本身：设置项名、全称与紧凑样式的短名称；关闭会结束正在进行的密聊，所以关闭用危险样式
    const FeatureSwitchSpec& spec = SecretChatSwitchSpec();
    FA_EXPECT_STREQ(spec.m_key, _T("secretchat.enabled"));
    FA_EXPECT_STREQ(spec.m_name, _T("单聊密聊"));
    FA_EXPECT_STREQ(spec.m_compactName, _T("单聊密聊"));
    FA_EXPECT_TRUE(CString(spec.m_description).Find(_T("「密聊」按钮")) >= 0);
    FA_EXPECT_TRUE(spec.m_offIsDanger);

    LoginPlatformAdmin();
    Captured cap;
    FeatureSwitchPanel panel(spec, kApiTagSecretChatSwitchGet, kApiTagSecretChatSwitchSet, kIdFeatSwitchSecretChat,
                             true);
    panel.SetRequestSink(&CaptureRequest, &cap);
    panel.SetUiRequestHandler(&CaptureUi, &cap);
    panel.SetRect(kPanelRect);
    panel.Load(kEntA);
    Json::Value body;
    FA_EXPECT_TRUE(LastBody(cap, kApiTagSecretChatSwitchGet, body));
    FA_EXPECT_TRUE(body["enterpriseid"].asInt64() == kEntA);

    //几项都在同一份应答里、本项为内置默认值（开启）：只取 secretchat.enabled，开关为开、写明是默认设置
    const std::string settings =
        "{\"code\":0,\"msg\":\"ok\",\"enterpriseid\":" + std::to_string(kEntA) + ",\"settings\":["
        "{\"key\":\"caption.enabled\",\"value\":\"0\",\"isdefault\":0},"
        "{\"key\":\"secretchat.enabled\",\"value\":\"1\",\"isdefault\":1},"
        "{\"key\":\"threadgroup.enabled\",\"value\":\"0\",\"isdefault\":0}]}";
    panel.OnApiResult(kApiTagSecretChatSwitchGet, MakeHttpOk(settings));
    FA_EXPECT_EQ(panel.GetState(), static_cast<int>(FeatureSwitchPanel::kStateLoaded));
    FA_EXPECT_TRUE(panel.IsSwitchChecked());
    FA_EXPECT_STREQ(panel.StatusText(), _T("当前为默认设置：开启"));

    //关闭：确认框标题带全称、是危险样式，正文说明正在进行的密聊会被结束、双方只能查看已有内容；先取消，开关恢复为开、不提交
    const int setsBefore = Count(cap, kApiTagSecretChatSwitchSet);
    panel.Switch()->SetChecked(false, false, false);
    panel.OnSwitchToggled(false);
    fsui::UiRequest confirm = LastUi(cap, fsui::kUiConfirm);
    FA_EXPECT_STREQ(confirm.m_title, _T("关闭单聊密聊"));
    FA_EXPECT_TRUE(confirm.m_danger);
    FA_EXPECT_TRUE(confirm.m_text.Find(_T("正在进行的密聊会被立即结束")) >= 0);
    FA_EXPECT_TRUE(confirm.m_text.Find(_T("只能查看已有内容")) >= 0);
    FA_EXPECT_NOTNULL(confirm.m_target);
    if (confirm.m_target == nullptr)
        return;
    confirm.m_target->OnConfirmResult(false);
    FA_EXPECT_TRUE(panel.IsSwitchChecked());
    FA_EXPECT_EQ(Count(cap, kApiTagSecretChatSwitchSet), setsBefore);

    //再拨一次并确认：提交 secretchat.enabled=0，按应答更新开关、状态与提示
    panel.Switch()->SetChecked(false, false, false);
    panel.OnSwitchToggled(false);
    confirm = LastUi(cap, fsui::kUiConfirm);
    if (confirm.m_target == nullptr)
        return;
    confirm.m_target->OnConfirmResult(true);
    FA_EXPECT_TRUE(LastBody(cap, kApiTagSecretChatSwitchSet, body));
    FA_EXPECT_TRUE(body["key"].asString() == "secretchat.enabled");
    FA_EXPECT_TRUE(body["value"].asString() == "0");
    FA_EXPECT_TRUE(body["enterpriseid"].asInt64() == kEntA);
    panel.OnApiResult(kApiTagSecretChatSwitchSet,
                      MakeHttpOk("{\"code\":0,\"msg\":\"ok\",\"enterpriseid\":9001,\"enabled\":0}"));
    FA_EXPECT_FALSE(panel.IsSwitchChecked());
    FA_EXPECT_STREQ(LastUi(cap, fsui::kUiToast).m_text, _T("已关闭单聊密聊"));
    FA_EXPECT_STREQ(panel.StatusText(), _T("已由管理员设置为关闭"));

    //再打开：确认框不是危险样式，正文说明恢复显示「密聊」按钮，提交 secretchat.enabled=1
    panel.Switch()->SetChecked(true, false, false);
    panel.OnSwitchToggled(true);
    confirm = LastUi(cap, fsui::kUiConfirm);
    FA_EXPECT_STREQ(confirm.m_title, _T("打开单聊密聊"));
    FA_EXPECT_FALSE(confirm.m_danger);
    FA_EXPECT_TRUE(confirm.m_text.Find(_T("恢复显示「密聊」按钮")) >= 0);
    if (confirm.m_target == nullptr)
        return;
    confirm.m_target->OnConfirmResult(true);
    FA_EXPECT_TRUE(LastBody(cap, kApiTagSecretChatSwitchSet, body));
    FA_EXPECT_TRUE(body["value"].asString() == "1");
}

// TGSW1：「帖子群」的面板（threadgroup.enabled，docs/帖子群方案.md 4.7）
void TestFeaturePanelThreadGroup()
{
    BeginCase("TGSW1 功能开关面板：帖子群，键为 threadgroup.enabled，默认开启，关闭不用危险样式");

    //描述本身：设置项名、全称与紧凑样式的短名称；关闭只隐藏帖子群，帖子与留言保留，不结束任何会话
    const FeatureSwitchSpec& spec = ThreadGroupSwitchSpec();
    FA_EXPECT_STREQ(spec.m_key, _T("threadgroup.enabled"));
    FA_EXPECT_STREQ(spec.m_name, _T("帖子群"));
    FA_EXPECT_STREQ(spec.m_compactName, _T("帖子群"));
    FA_EXPECT_TRUE(CString(spec.m_description).Find(_T("重新打开后恢复")) >= 0);
    FA_EXPECT_FALSE(spec.m_offIsDanger);

    //企业管理员在企业详情「功能开关」页设置本企业：完整样式，请求不带企业 ID
    LoginEnterpriseAdmin();
    Captured cap;
    FeatureSwitchPanel panel(spec, kApiTagThreadGroupSwitchGet, kApiTagThreadGroupSwitchSet,
                             kIdFeatSwitchThreadGroup);
    panel.SetRequestSink(&CaptureRequest, &cap);
    panel.SetUiRequestHandler(&CaptureUi, &cap);
    panel.SetRect(kPanelRect);
    FA_EXPECT_EQ(panel.PanelHeight(), FeatureSwitchPanel::kPanelHeight);
    panel.Load(0);
    Json::Value body;
    FA_EXPECT_TRUE(LastBody(cap, kApiTagThreadGroupSwitchGet, body));
    FA_EXPECT_FALSE(body.isMember("enterpriseid"));

    //几项都在同一份应答里、本项为内置默认值（开启）：只取 threadgroup.enabled，开关为开、写明是默认设置
    const std::string settings =
        "{\"code\":0,\"msg\":\"ok\",\"enterpriseid\":" + std::to_string(kEntA) + ",\"settings\":["
        "{\"key\":\"secretchat.enabled\",\"value\":\"0\",\"isdefault\":0},"
        "{\"key\":\"threadgroup.enabled\",\"value\":\"1\",\"isdefault\":1}]}";
    panel.OnApiResult(kApiTagThreadGroupSwitchGet, MakeHttpOk(settings));
    FA_EXPECT_EQ(panel.GetState(), static_cast<int>(FeatureSwitchPanel::kStateLoaded));
    FA_EXPECT_TRUE(panel.IsSwitchChecked());
    FA_EXPECT_STREQ(panel.StatusText(), _T("当前为默认设置：开启"));

    //关闭：确认框标题带全称、不是危险样式，正文说明帖子与留言保留、重新打开后恢复；确认后提交 threadgroup.enabled=0
    panel.Switch()->SetChecked(false, false, false);
    panel.OnSwitchToggled(false);
    fsui::UiRequest confirm = LastUi(cap, fsui::kUiConfirm);
    FA_EXPECT_STREQ(confirm.m_title, _T("关闭帖子群"));
    FA_EXPECT_FALSE(confirm.m_danger);
    FA_EXPECT_TRUE(confirm.m_text.Find(_T("隐藏全部帖子群")) >= 0);
    FA_EXPECT_TRUE(confirm.m_text.Find(_T("已有的帖子与留言保留")) >= 0);
    FA_EXPECT_NOTNULL(confirm.m_target);
    if (confirm.m_target == nullptr)
        return;
    confirm.m_target->OnConfirmResult(true);
    FA_EXPECT_TRUE(LastBody(cap, kApiTagThreadGroupSwitchSet, body));
    FA_EXPECT_TRUE(body["key"].asString() == "threadgroup.enabled");
    FA_EXPECT_TRUE(body["value"].asString() == "0");
    FA_EXPECT_FALSE(body.isMember("enterpriseid"));
    panel.OnApiResult(kApiTagThreadGroupSwitchSet,
                      MakeHttpOk("{\"code\":0,\"msg\":\"ok\",\"enterpriseid\":9001,\"enabled\":0}"));
    FA_EXPECT_FALSE(panel.IsSwitchChecked());
    FA_EXPECT_STREQ(LastUi(cap, fsui::kUiToast).m_text, _T("已关闭帖子群"));
    FA_EXPECT_STREQ(panel.StatusText(), _T("已由管理员设置为关闭"));

    //再打开：确认框正文说明恢复显示帖子群，提交 threadgroup.enabled=1
    panel.Switch()->SetChecked(true, false, false);
    panel.OnSwitchToggled(true);
    confirm = LastUi(cap, fsui::kUiConfirm);
    FA_EXPECT_STREQ(confirm.m_title, _T("打开帖子群"));
    FA_EXPECT_TRUE(confirm.m_text.Find(_T("恢复显示帖子群")) >= 0);
    if (confirm.m_target == nullptr)
        return;
    confirm.m_target->OnConfirmResult(true);
    FA_EXPECT_TRUE(LastBody(cap, kApiTagThreadGroupSwitchSet, body));
    FA_EXPECT_TRUE(body["value"].asString() == "1");
}

// FSP-3：失败与过时的应答
void TestFeaturePanelFailures()
{
    BeginCase("FSP-3 功能开关面板：提交失败与过时的应答");

    LoginPlatformAdmin();
    Captured cap;
    FeatureSwitchPanel panel(FileShareSwitchSpec(), kApiTagFeatureGet, kApiTagFeatureSet, 600);
    panel.SetRequestSink(&CaptureRequest, &cap);
    panel.SetUiRequestHandler(&CaptureUi, &cap);
    panel.SetRect(kPanelRect);
    panel.Load(kEntA);
    panel.OnApiResult(kApiTagFeatureGet, MakeHttpOk(SettingsJson(kEntA, "1", 1)));

    panel.Switch()->SetChecked(false, false, false);
    panel.OnSwitchToggled(false);
    LastUi(cap, fsui::kUiConfirm).m_target->OnConfirmResult(true);
    panel.OnApiResult(kApiTagFeatureSet, MakeHttpOk("{\"code\":133,\"msg\":\"x\"}"));
    //失败：开关回到服务端的状态（开），提示原因，仍是默认值
    FA_EXPECT_TRUE(panel.IsSwitchChecked());
    FA_EXPECT_TRUE(panel.IsSwitchEnabled());
    FA_EXPECT_STREQ(panel.StatusText(), _T("当前为默认设置：开启"));
    const fsui::UiRequest toast = LastUi(cap, fsui::kUiToast);
    FA_EXPECT_TRUE(toast.m_isError);
    FA_EXPECT_STREQ(toast.m_text, _T("切换失败：功能设置项或取值无效"));

    //为企业 B 重新加载后，企业 A 晚到的读设置应答被丢弃
    panel.Load(kEntB);
    panel.OnApiResult(kApiTagFeatureGet, MakeHttpOk(SettingsJson(kEntA, "0", 0)));
    FA_EXPECT_EQ(panel.GetState(), static_cast<int>(FeatureSwitchPanel::kStateLoading));
    panel.OnApiResult(kApiTagFeatureGet, MakeHttpOk(SettingsJson(kEntB, "0", 0)));
    FA_EXPECT_EQ(panel.GetState(), static_cast<int>(FeatureSwitchPanel::kStateLoaded));
    FA_EXPECT_FALSE(panel.IsSwitchChecked());
    FA_EXPECT_TRUE(panel.EnterpriseId() == kEntB);
}

// FS-1：进入「文件共享」页
void TestFileSharesEnter()
{
    BeginCase("FS-1 文件共享页：按角色进入");

    LoginEnterpriseAdmin();
    {
        Captured cap;
        FileSharesScreen screen;
        screen.SetRequestSink(&CaptureRequest, &cap);
        screen.SetUiRequestHandler(&CaptureUi, &cap);
        screen.SetRect(kScreenRect);
        screen.OnEnter();
        FA_EXPECT_FALSE(screen.IsEnterprisePickerVisible());
        FA_EXPECT_EQ(Count(cap, kApiTagFsEnterprises), 0);
        FA_EXPECT_EQ(Count(cap, kApiTagFsList), 1);
        Json::Value body;
        FA_EXPECT_TRUE(LastBody(cap, kApiTagFsList, body));
        FA_EXPECT_FALSE(body.isMember("enterpriseid"));
        //默认只看有效的共享、第 1 页
        FA_EXPECT_EQ(body["state"].asInt(), 1);
        FA_EXPECT_EQ(body["page"].asInt(), 1);
        FA_EXPECT_EQ(body["pagesize"].asInt(), FileSharesScreen::kPageSize);
        FA_EXPECT_STREQ(screen.OverlayText(), _T("加载中…"));
        FA_EXPECT_STREQ(screen.DetailTitle(), _T("选择一个共享查看详情"));
        //企业管理员拉不到企业列表：详情里的所属企业显示「本企业」
        screen.OnApiResult(kApiTagFsList, MakeHttpOk(SharesJson(1, 1)));
        screen.OnRowSelected(0);
        FA_EXPECT_STREQ(screen.DetailGrid()->GetValue(FileSharesScreen::kDetailEnterprise), _T("本企业"));
    }

    LoginPlatformAdmin();
    {
        Captured cap;
        FileSharesScreen screen;
        screen.SetRequestSink(&CaptureRequest, &cap);
        screen.SetUiRequestHandler(&CaptureUi, &cap);
        screen.SetRect(kScreenRect);
        screen.OnEnter();
        FA_EXPECT_TRUE(screen.IsEnterprisePickerVisible());
        FA_EXPECT_EQ(Count(cap, kApiTagFsEnterprises), 1);
        //平台超管默认看全部企业：不带 enterpriseid
        Json::Value body;
        FA_EXPECT_TRUE(LastBody(cap, kApiTagFsList, body));
        FA_EXPECT_FALSE(body.isMember("enterpriseid"));
        FA_EXPECT_TRUE(screen.CurrentEnterpriseId() == 0);
        //企业筛选框是可输入的下拉框：企业列表到达前后都显示「全部企业」，选了企业后显示企业名
        FA_EXPECT_STREQ(screen.EnterprisePickerText(), _T("全部企业"));
        screen.OnApiResult(kApiTagFsEnterprises, MakeHttpOk(EnterprisesJson()));
        FA_EXPECT_STREQ(screen.EnterprisePickerText(), _T("全部企业"));
    }
}

// FS-2：列表结果
void TestFileSharesResults()
{
    BeginCase("FS-2 文件共享页：列表结果与强制取消链接");

    LoginEnterpriseAdmin();
    Captured cap;
    FileSharesScreen screen;
    screen.SetRequestSink(&CaptureRequest, &cap);
    screen.SetUiRequestHandler(&CaptureUi, &cap);
    screen.SetRect(kScreenRect);
    screen.OnEnter();
    screen.OnApiResult(kApiTagFsList, MakeHttpOk(SharesJson(3, 3)));
    FA_EXPECT_EQ(screen.RowCount(), 3);
    FA_EXPECT_STREQ(screen.OverlayText(), _T(""));
    FA_EXPECT_EQ(screen.PageCount(), 1);
    //第 0、2 行有效，第 1 行已取消：「强制取消」只在有效的行显示
    FA_EXPECT_TRUE(screen.Table()->IsActionVisible(0, FileSharesScreen::kActionCancel));
    FA_EXPECT_FALSE(screen.Table()->IsActionVisible(1, FileSharesScreen::kActionCancel));
    FA_EXPECT_TRUE(screen.Table()->IsActionVisible(2, FileSharesScreen::kActionCancel));

    adminapi::FileShareItem item;
    item.m_ownerName = _T("张三");
    item.m_ownerAccount = _T("MY0308");
    item.m_displayPath = _T("D:\\data");
    item.m_ctype = 1;
    item.m_conversationName = _T("李四");
    item.m_memberCount = 1;
    item.m_state = fstext::kStateActive;
    item.m_createTime = kSampleTimeMs;
    std::vector<CString> cells;
    FileSharesScreen::RowCells(item, cells);
    FA_EXPECT_EQ(static_cast<int>(cells.size()), static_cast<int>(FileSharesScreen::kColumnCount));
    FA_EXPECT_STREQ(cells[FileSharesScreen::kColOwner], _T("张三（MY0308）"));
    FA_EXPECT_STREQ(cells[FileSharesScreen::kColPath], _T("D:\\data"));
    FA_EXPECT_STREQ(cells[FileSharesScreen::kColConversation], _T("单聊：李四"));
    FA_EXPECT_STREQ(cells[FileSharesScreen::kColMembers], _T("1"));
    FA_EXPECT_STREQ(cells[FileSharesScreen::kColState], _T("有效"));
    FA_EXPECT_EQ(cells[FileSharesScreen::kColCreateTime].GetLength(), 19);

    //空结果：默认筛选（只看有效）属于有筛选条件
    screen.Refresh();
    screen.OnApiResult(kApiTagFsList, MakeHttpOk(SharesJson(0, 0)));
    FA_EXPECT_STREQ(screen.OverlayText(), _T("没有符合条件的共享"));
    screen.OnStateChanged(FileSharesScreen::kFilterAll);
    screen.OnApiResult(kApiTagFsList, MakeHttpOk(SharesJson(0, 0)));
    FA_EXPECT_STREQ(screen.OverlayText(), _T("暂无文件共享"));
    //失败
    screen.Refresh();
    screen.OnApiResult(kApiTagFsList, MakeHttpOk("{\"code\":1,\"msg\":\"x\"}"));
    FA_EXPECT_STREQ(screen.OverlayText(), _T("加载失败：操作失败，请稍后重试"));
}

// FS-3：选中行与成员
void TestFileSharesDetail()
{
    BeginCase("FS-3 文件共享页：选中行、详情与成员");

    LoginPlatformAdmin();
    Captured cap;
    FileSharesScreen screen;
    screen.SetRequestSink(&CaptureRequest, &cap);
    screen.SetUiRequestHandler(&CaptureUi, &cap);
    screen.SetRect(kScreenRect);
    screen.OnEnter();
    screen.OnApiResult(kApiTagFsEnterprises, MakeHttpOk(EnterprisesJson()));
    screen.OnApiResult(kApiTagFsList, MakeHttpOk(SharesJson(3, 3)));

    screen.OnRowSelected(1);
    FA_EXPECT_TRUE(screen.SelectedShareId() == 101);
    FA_EXPECT_EQ(Count(cap, kApiTagFsMembers), 1);
    Json::Value body;
    FA_EXPECT_TRUE(LastBody(cap, kApiTagFsMembers, body));
    FA_EXPECT_TRUE(body["shareid"].asInt64() == 101);
    FA_EXPECT_STREQ(screen.DetailTitle(), _T("share1"));
    ui::KeyValueGrid* grid = screen.DetailGrid();
    FA_EXPECT_STREQ(grid->GetValue(FileSharesScreen::kDetailOwner), _T("owner（MY0308）"));
    FA_EXPECT_STREQ(grid->GetValue(FileSharesScreen::kDetailConversation), _T("群聊：grp"));
    FA_EXPECT_STREQ(grid->GetValue(FileSharesScreen::kDetailKind), _T("文件夹"));
    FA_EXPECT_STREQ(grid->GetValue(FileSharesScreen::kDetailState), _T("已取消"));
    FA_EXPECT_STREQ(grid->GetValue(FileSharesScreen::kDetailCancelReason), _T("管理员在后台强制取消"));
    //企业列表已到：所属企业显示企业名
    FA_EXPECT_STREQ(grid->GetValue(FileSharesScreen::kDetailEnterprise), _T("EntA"));
    FA_EXPECT_STREQ(grid->GetValue(FileSharesScreen::kDetailPath), _T("D:\\data\\share1"));
    FA_EXPECT_STREQ(screen.MemberOverlayText(), _T("加载中…"));

    screen.OnApiResult(kApiTagFsMembers, MakeHttpOk(MembersJson()));
    FA_EXPECT_EQ(screen.MemberRowCount(), 2);
    FA_EXPECT_STREQ(screen.MemberOverlayText(), _T(""));

    //快速换两次行：第一份成员应答（属于第 0 行）被丢弃，只认最后一份
    screen.OnRowSelected(0);
    screen.OnRowSelected(2);
    FA_EXPECT_EQ(Count(cap, kApiTagFsMembers), 3);
    screen.OnApiResult(kApiTagFsMembers, MakeHttpOk(MembersJson()));
    FA_EXPECT_EQ(screen.MemberRowCount(), 0);
    FA_EXPECT_STREQ(screen.MemberOverlayText(), _T("加载中…"));
    screen.OnApiResult(kApiTagFsMembers, MakeHttpOk("{\"code\":0,\"msg\":\"ok\",\"members\":[]}"));
    FA_EXPECT_EQ(screen.MemberRowCount(), 0);
    FA_EXPECT_STREQ(screen.MemberOverlayText(), _T("没有成员"));

    //刷新后选中的共享仍在本页：保持选中并重新拉取成员
    screen.Refresh();
    FA_EXPECT_EQ(Count(cap, kApiTagFsMembers), 4);
    screen.OnApiResult(kApiTagFsList, MakeHttpOk(SharesJson(3, 3)));
    FA_EXPECT_TRUE(screen.SelectedShareId() == 102);
    //选中的共享不在新的一页里：清空详情
    screen.OnApiResult(kApiTagFsMembers, MakeHttpOk(MembersJson()));
    screen.Refresh();
    screen.OnApiResult(kApiTagFsList, MakeHttpOk(SharesJson(1, 1)));
    FA_EXPECT_TRUE(screen.SelectedShareId() == 0);
    FA_EXPECT_STREQ(screen.DetailTitle(), _T("选择一个共享查看详情"));
}

// FS-4：强制取消
void TestFileSharesCancel()
{
    BeginCase("FS-4 文件共享页：强制取消");

    LoginEnterpriseAdmin();
    Captured cap;
    FileSharesScreen screen;
    screen.SetRequestSink(&CaptureRequest, &cap);
    screen.SetUiRequestHandler(&CaptureUi, &cap);
    screen.SetRect(kScreenRect);
    screen.OnEnter();
    screen.OnApiResult(kApiTagFsList, MakeHttpOk(SharesJson(3, 3)));

    //已取消的行：不做任何事
    screen.OnCancelClicked(1);
    FA_EXPECT_EQ(CountUi(cap, fsui::kUiConfirm), 0);

    screen.OnCancelClicked(0);
    FA_EXPECT_EQ(CountUi(cap, fsui::kUiConfirm), 1);
    fsui::UiRequest confirm = LastUi(cap, fsui::kUiConfirm);
    FA_EXPECT_STREQ(confirm.m_title, _T("强制取消共享"));
    FA_EXPECT_TRUE(confirm.m_danger);
    FA_EXPECT_TRUE(confirm.m_text.Find(_T("share0")) >= 0);
    FA_EXPECT_TRUE(confirm.m_text.Find(_T("不能撤销")) >= 0);
    //两位成员：说「全部 2 位成员」；一位成员时说「这位成员」
    FA_EXPECT_TRUE(confirm.m_text.Find(_T("全部 2 位成员")) >= 0);
    adminapi::FileShareItem one;
    one.m_ownerName = _T("o");
    one.m_name = _T("n");
    one.m_memberCount = 1;
    FA_EXPECT_TRUE(FileSharesScreen::CancelConfirmText(one).Find(_T("这位成员")) >= 0);

    //取消：不提交；之后可以再点
    confirm.m_target->OnConfirmResult(false);
    FA_EXPECT_EQ(Count(cap, kApiTagFsCancel), 0);

    screen.OnCancelClicked(0);
    LastUi(cap, fsui::kUiConfirm).m_target->OnConfirmResult(true);
    FA_EXPECT_EQ(Count(cap, kApiTagFsCancel), 1);
    Json::Value body;
    FA_EXPECT_TRUE(LastBody(cap, kApiTagFsCancel, body));
    FA_EXPECT_TRUE(body["shareid"].asInt64() == 100);

    //提交中再点别的行：提示稍候，不弹确认框
    const int confirms = CountUi(cap, fsui::kUiConfirm);
    screen.OnCancelClicked(2);
    FA_EXPECT_EQ(CountUi(cap, fsui::kUiConfirm), confirms);

    const int listBefore = Count(cap, kApiTagFsList);
    screen.OnApiResult(kApiTagFsCancel, MakeHttpOk("{\"code\":0,\"msg\":\"ok\"}"));
    FA_EXPECT_STREQ(LastUi(cap, fsui::kUiToast).m_text, _T("已强制取消「share0」"));
    FA_EXPECT_FALSE(LastUi(cap, fsui::kUiToast).m_isError);
    FA_EXPECT_EQ(Count(cap, kApiTagFsList), listBefore + 1);

    //失败：提示原因并重新拉取
    screen.OnApiResult(kApiTagFsList, MakeHttpOk(SharesJson(3, 3)));
    screen.OnCancelClicked(2);
    LastUi(cap, fsui::kUiConfirm).m_target->OnConfirmResult(true);
    screen.OnApiResult(kApiTagFsCancel, MakeHttpOk("{\"code\":131,\"msg\":\"x\"}"));
    FA_EXPECT_TRUE(LastUi(cap, fsui::kUiToast).m_isError);
    FA_EXPECT_STREQ(LastUi(cap, fsui::kUiToast).m_text, _T("强制取消失败：该共享已经取消过了"));
    FA_EXPECT_EQ(Count(cap, kApiTagFsList), listBefore + 2);
}

// FS-5：筛选与翻页
void TestFileSharesFilters()
{
    BeginCase("FS-5 文件共享页：搜索、状态、企业筛选与翻页");

    LoginPlatformAdmin();
    Captured cap;
    FileSharesScreen screen;
    screen.SetRequestSink(&CaptureRequest, &cap);
    screen.SetUiRequestHandler(&CaptureUi, &cap);
    screen.SetRect(kScreenRect);
    screen.OnEnter();
    screen.OnApiResult(kApiTagFsEnterprises, MakeHttpOk(EnterprisesJson()));
    screen.OnApiResult(kApiTagFsList, MakeHttpOk(SharesJson(50, 120)));
    FA_EXPECT_EQ(screen.PageCount(), 3);

    //翻页
    screen.GoToPage(3);
    Json::Value body;
    FA_EXPECT_TRUE(LastBody(cap, kApiTagFsList, body));
    FA_EXPECT_EQ(body["page"].asInt(), 3);
    screen.OnApiResult(kApiTagFsList, MakeHttpOk(SharesJson(20, 120)));

    //企业筛选：下标 2 是企业列表的第二项（EntB），从第 1 页查
    screen.OnEnterpriseSelected(2);
    FA_EXPECT_TRUE(screen.CurrentEnterpriseId() == kEntB);
    FA_EXPECT_TRUE(LastBody(cap, kApiTagFsList, body));
    FA_EXPECT_TRUE(body["enterpriseid"].asInt64() == kEntB);
    FA_EXPECT_EQ(body["page"].asInt(), 1);
    screen.OnApiResult(kApiTagFsList, MakeHttpOk(SharesJson(1, 1)));
    //下标 0 回到全部企业
    screen.OnEnterpriseSelected(0);
    FA_EXPECT_TRUE(screen.CurrentEnterpriseId() == 0);
    screen.OnApiResult(kApiTagFsList, MakeHttpOk(SharesJson(1, 1)));
    //越界不做任何事
    const int before = Count(cap, kApiTagFsList);
    screen.OnEnterpriseSelected(9);
    FA_EXPECT_EQ(Count(cap, kApiTagFsList), before);

    //状态筛选
    screen.OnStateChanged(FileSharesScreen::kFilterCanceled);
    FA_EXPECT_TRUE(LastBody(cap, kApiTagFsList, body));
    FA_EXPECT_EQ(body["state"].asInt(), 2);
    //加载中又改条件：结果回来后按最新条件补查一次
    screen.OnStateChanged(FileSharesScreen::kFilterAll);
    FA_EXPECT_EQ(Count(cap, kApiTagFsList), before + 1);
    screen.OnApiResult(kApiTagFsList, MakeHttpOk(SharesJson(1, 1)));
    FA_EXPECT_EQ(Count(cap, kApiTagFsList), before + 2);
    FA_EXPECT_TRUE(LastBody(cap, kApiTagFsList, body));
    FA_EXPECT_EQ(body["state"].asInt(), 0);
    screen.OnApiResult(kApiTagFsList, MakeHttpOk(SharesJson(1, 1)));

    //搜索（没有主窗口时不防抖，立即查询）：去掉首尾空白，从第 1 页查
    screen.SearchBox()->SetText(_T(" MY0308 "));
    screen.OnSearchChanged();
    FA_EXPECT_TRUE(LastBody(cap, kApiTagFsList, body));
    FA_EXPECT_TRUE(body["keyword"].asString() == "MY0308");
    FA_EXPECT_EQ(body["page"].asInt(), 1);
    screen.OnApiResult(kApiTagFsList, MakeHttpOk(SharesJson(1, 1)));
    //文字没变：不再查询
    const int afterSearch = Count(cap, kApiTagFsList);
    screen.OnSearchChanged();
    FA_EXPECT_EQ(Count(cap, kApiTagFsList), afterSearch);
}

// FS-6：最小窗口尺寸
void TestFileSharesMinLayout()
{
    BeginCase("FS-6 文件共享页的「文件共享」页签：最小窗口尺寸下（页头之下）表格至少显示三行");

    LoginPlatformAdmin();
    FileShareCenterScreen center;
    center.SetRect(kMinScreenRect);
    center.OnEnter();
    FileSharesScreen& screen = *center.SharesPage();
    const RECT rc = screen.Table()->GetRect();
    FA_EXPECT_TRUE(rc.bottom - rc.top >= kShareRowH * kMinVisibleRows);
    //详情卡的成员表格也有高度
    const RECT mr = screen.MemberTable()->GetRect();
    FA_EXPECT_TRUE(mr.bottom - mr.top > 0);
}

// FAU-1：进入「共享审计」页
void TestAuditEnter()
{
    BeginCase("FAU-1 共享审计页：按角色进入与默认条件");

    LoginEnterpriseAdmin();
    {
        Captured cap;
        FileShareAuditScreen screen;
        screen.SetRequestSink(&CaptureRequest, &cap);
        screen.SetRect(kScreenRect);
        screen.OnEnter();
        FA_EXPECT_FALSE(screen.IsEnterprisePickerVisible());
        FA_EXPECT_EQ(Count(cap, kApiTagFsAuditEnterprises), 0);
        Json::Value body;
        FA_EXPECT_TRUE(LastBody(cap, kApiTagFsAudit, body));
        FA_EXPECT_FALSE(body.isMember("enterpriseid"));
        FA_EXPECT_EQ(body["action"].asInt(), 0);
        FA_EXPECT_TRUE(body["shareid"].asInt64() == 0);
        FA_EXPECT_TRUE(body["endtime"].asInt64() == 0);
        const long long expected = TestNowMs() - 7 * kDayMs;
        const long long diff = body["starttime"].asInt64() - expected;
        FA_EXPECT_TRUE(diff > -kClockToleranceMs && diff < kClockToleranceMs);
        FA_EXPECT_STREQ(screen.DetailTitle(), _T("选择一条记录查看详情"));
    }
    LoginPlatformAdmin();
    {
        Captured cap;
        FileShareAuditScreen screen;
        screen.SetRequestSink(&CaptureRequest, &cap);
        screen.SetRect(kScreenRect);
        screen.OnEnter();
        FA_EXPECT_TRUE(screen.IsEnterprisePickerVisible());
        FA_EXPECT_EQ(Count(cap, kApiTagFsAuditEnterprises), 1);
        screen.OnApiResult(kApiTagFsAuditEnterprises, MakeHttpOk(EnterprisesJson()));
        FA_EXPECT_STREQ(screen.EnterprisePickerText(), _T("全部企业"));
        screen.OnApiResult(kApiTagFsAudit, MakeHttpOk(AuditJson()));
        screen.OnEnterpriseSelected(1);
        Json::Value body;
        FA_EXPECT_TRUE(LastBody(cap, kApiTagFsAudit, body));
        FA_EXPECT_TRUE(body["enterpriseid"].asInt64() == kEntA);
    }
}

// FAU-2：筛选
void TestAuditFilters()
{
    BeginCase("FAU-2 共享审计页：时间范围、操作人、动作与共享 ID");

    LoginEnterpriseAdmin();
    Captured cap;
    FileShareAuditScreen screen;
    screen.SetRequestSink(&CaptureRequest, &cap);
    screen.SetRect(kScreenRect);
    screen.OnEnter();
    screen.OnApiResult(kApiTagFsAudit, MakeHttpOk(AuditJson()));

    Json::Value body;
    screen.OnRangeChanged(fstext::kRangeAll);
    FA_EXPECT_TRUE(LastBody(cap, kApiTagFsAudit, body));
    FA_EXPECT_TRUE(body["starttime"].asInt64() == 0);
    screen.OnApiResult(kApiTagFsAudit, MakeHttpOk(AuditJson()));

    screen.OnActionChanged(fstext::kActionDownloadFile);
    FA_EXPECT_TRUE(LastBody(cap, kApiTagFsAudit, body));
    FA_EXPECT_EQ(body["action"].asInt(), fstext::kActionDownloadFile);
    screen.OnApiResult(kApiTagFsAudit, MakeHttpOk(AuditJson()));
    //越界不做任何事
    const int before = Count(cap, kApiTagFsAudit);
    screen.OnActionChanged(fstext::kActionCount + 1);
    screen.OnRangeChanged(fstext::kRangeCount);
    FA_EXPECT_EQ(Count(cap, kApiTagFsAudit), before);

    //操作人（没有主窗口时不防抖）
    screen.SearchBox()->SetText(_T(" m1 "));
    screen.OnSearchChanged();
    FA_EXPECT_TRUE(LastBody(cap, kApiTagFsAudit, body));
    FA_EXPECT_TRUE(body["actorkeyword"].asString() == "m1");
    screen.OnApiResult(kApiTagFsAudit, MakeHttpOk(AuditJson()));

    //共享 ID
    screen.ShareIdBox()->SetText(_T("100"));
    screen.OnShareIdChanged();
    FA_EXPECT_TRUE(screen.CurrentShareIdFilter() == 100);
    FA_EXPECT_TRUE(LastBody(cap, kApiTagFsAudit, body));
    FA_EXPECT_TRUE(body["shareid"].asInt64() == 100);
    screen.OnApiResult(kApiTagFsAudit, MakeHttpOk(AuditJson()));
    //非法输入按不限处理
    screen.ShareIdBox()->SetText(_T("abc"));
    screen.OnShareIdChanged();
    FA_EXPECT_TRUE(screen.CurrentShareIdFilter() == 0);

    FA_EXPECT_TRUE(FileShareAuditScreen::ParseShareId(_T(" 12 ")) == 12);
    FA_EXPECT_TRUE(FileShareAuditScreen::ParseShareId(_T("-3")) == 0);
    FA_EXPECT_TRUE(FileShareAuditScreen::ParseShareId(_T("1a")) == 0);
    FA_EXPECT_TRUE(FileShareAuditScreen::ParseShareId(_T("")) == 0);
    FA_EXPECT_TRUE(FileShareAuditScreen::ParseShareId(_T("0")) == 0);
}

// FAU-3：结果与详情
void TestAuditResults()
{
    BeginCase("FAU-3 共享审计页：各列文字与详情");

    LoginEnterpriseAdmin();
    Captured cap;
    FileShareAuditScreen screen;
    screen.SetRequestSink(&CaptureRequest, &cap);
    screen.SetRect(kScreenRect);
    screen.OnEnter();
    screen.OnApiResult(kApiTagFsAudit, MakeHttpOk(AuditJson()));
    FA_EXPECT_EQ(screen.RowCount(), 2);
    FA_EXPECT_STREQ(screen.OverlayText(), _T(""));

    adminapi::FileShareAuditItem item;
    item.m_time = kSampleTimeMs;
    item.m_actorName = _T("admin");
    item.m_actorIsAdmin = true;
    item.m_action = fstext::kActionRename;
    item.m_path = _T("sub/a.txt");
    item.m_toPath = _T("b.txt");
    item.m_size = 0;
    item.m_result = 806;
    item.m_source = 2;
    std::vector<CString> cells;
    FileShareAuditScreen::RowCells(item, cells);
    FA_EXPECT_EQ(static_cast<int>(cells.size()), static_cast<int>(FileShareAuditScreen::kColumnCount));
    //表格里的时间到分钟
    FA_EXPECT_EQ(cells[FileShareAuditScreen::kColTime].GetLength(), 16);
    FA_EXPECT_STREQ(cells[FileShareAuditScreen::kColActor], _T("admin（管理员）"));
    FA_EXPECT_STREQ(cells[FileShareAuditScreen::kColOwner], _T("-"));
    FA_EXPECT_STREQ(cells[FileShareAuditScreen::kColAction], _T("重命名"));
    FA_EXPECT_STREQ(cells[FileShareAuditScreen::kColPath], _T("sub/a.txt → b.txt"));
    FA_EXPECT_STREQ(cells[FileShareAuditScreen::kColSize], _T("-"));
    FA_EXPECT_STREQ(cells[FileShareAuditScreen::kColResult], _T("失败"));
    FA_EXPECT_STREQ(cells[FileShareAuditScreen::kColSource], _T("分享者"));

    //选中第二行（管理员强制取消、失败）
    screen.OnRowSelected(1);
    FA_EXPECT_TRUE(screen.SelectedId() == 12);
    FA_EXPECT_STREQ(screen.DetailTitle(), _T("取消共享 · x"));
    ui::KeyValueGrid* grid = screen.DetailGrid();
    FA_EXPECT_EQ(grid->GetValue(FileShareAuditScreen::kDetailTime).GetLength(), 19);
    FA_EXPECT_STREQ(grid->GetValue(FileShareAuditScreen::kDetailActor), _T("admin（管理员）"));
    FA_EXPECT_STREQ(grid->GetValue(FileShareAuditScreen::kDetailShareId), _T("101"));
    FA_EXPECT_STREQ(grid->GetValue(FileShareAuditScreen::kDetailSize), _T("2.0 KB"));
    FA_EXPECT_STREQ(grid->GetValue(FileShareAuditScreen::kDetailResult), _T("失败：无法放入回收站（810）"));
    FA_EXPECT_STREQ(grid->GetValue(FileShareAuditScreen::kDetailSource), _T("服务端记录"));
    FA_EXPECT_STREQ(grid->GetValue(FileShareAuditScreen::kDetailAccessId), _T("-"));
    FA_EXPECT_STREQ(grid->GetValue(FileShareAuditScreen::kDetailPath), _T("-"));

    //选中第一行：访问会话与目标
    screen.OnRowSelected(0);
    FA_EXPECT_STREQ(grid->GetValue(FileShareAuditScreen::kDetailAccessId), _T("acc-1"));
    FA_EXPECT_STREQ(grid->GetValue(FileShareAuditScreen::kDetailToPath), _T("b.txt"));
    FA_EXPECT_STREQ(grid->GetValue(FileShareAuditScreen::kDetailResult), _T("成功"));

    //空结果与失败
    screen.Refresh();
    screen.OnApiResult(kApiTagFsAudit, MakeHttpOk("{\"code\":0,\"msg\":\"ok\",\"total\":0,\"list\":[]}"));
    FA_EXPECT_STREQ(screen.OverlayText(), _T("这段时间内没有审计记录"));
    FA_EXPECT_TRUE(screen.SelectedId() == 0);
    FA_EXPECT_STREQ(screen.DetailTitle(), _T("选择一条记录查看详情"));
    screen.Refresh();
    screen.OnApiResult(kApiTagFsAudit, MakeHttpOk("{\"code\":2,\"msg\":\"x\"}"));
    FA_EXPECT_STREQ(screen.OverlayText(), _T("加载失败：登录已过期，请重新登录"));
}

// FAU-4：最小窗口尺寸
void TestAuditMinLayout()
{
    BeginCase("FAU-4 文件共享页的「共享审计」页签：最小窗口尺寸下筛选行不越界、表格至少显示三行");

    LoginPlatformAdmin();
    FileShareCenterScreen center;
    center.SetRect(kMinScreenRect);
    center.SelectTab(FileShareCenterScreen::kTabAudit);
    FileShareAuditScreen& screen = *center.AuditPage();
    //筛选行最右边的共享 ID 输入框不越出右外边距
    const RECT idRc = screen.ShareIdBox()->GetRect();
    FA_EXPECT_TRUE(idRc.right <= kMinScreenRect.right - kSideMargin);
    FA_EXPECT_TRUE(idRc.right > idRc.left);
    const RECT rc = screen.Table()->GetRect();
    FA_EXPECT_TRUE(rc.bottom - rc.top >= kAuditRowH * kMinVisibleRows);
}

// 企业设置弹窗用例的公共部分：默认窗口与最小窗口（弹窗铺满主窗口客户区），以及造一份企业与若干管理员。
const RECT kDefaultWindow = { 0, 0, 1440, 900 };
const RECT kMinWindow = { 0, 0, 1100, 720 };

// 管理员很多时的人数：最小窗口与默认窗口下弹窗内容都放不下，需要滚动
const int kManyAdmins = 12;

// 打开企业设置弹窗：填入企业 EntA 与 adminCount 位管理员，可见，并按 window 排版。
//   modal：被测弹窗；adminCount：管理员人数；window：主窗口客户区。
static void OpenSettingsModal(EnterpriseSettingsModal& modal, int adminCount, const RECT& window)
{
    Enterprise ent;
    ent.m_id = _T("9001");
    ent.m_name = _T("EntA");
    ent.m_plan = _T("专业版");
    std::vector<AdminItem> admins(static_cast<size_t>(adminCount));
    for (int i = 0; i < adminCount; ++i)
    {
        admins[i].m_adminId = i + 1;
        admins[i].m_name.Format(_T("a%d"), i + 1);
        admins[i].m_email.Format(_T("a%d@test.com"), i + 1);
    }
    modal.SetData(ent, admins);
    modal.SetVisible(true);
    modal.SetRect(window);
}

// 矩形 inner 是否完整落在 outer 之内。
static bool RectInside(const RECT& inner, const RECT& outer)
{
    return inner.left >= outer.left && inner.right <= outer.right && inner.top >= outer.top && inner.bottom <= outer.bottom;
}

// 矩形的中心点。
static POINT RectCenter(const RECT& rc)
{
    POINT pt = { (rc.left + rc.right) / 2, (rc.top + rc.bottom) / 2 };
    return pt;
}

// ESS1：内容放得下时不滚动
void TestSettingsModalFits()
{
    BeginCase("ESS1 企业设置弹窗：功能开关移走后，最小窗口下两位管理员时内容全部放得下，不能滚动");

    EnterpriseSettingsModal modal;
    OpenSettingsModal(modal, 2, kMinWindow);
    FA_EXPECT_EQ(modal.GetScrollMax(), 0);
    FA_EXPECT_EQ(modal.GetScroll(), 0);
    //主体区容下全部内容：第一个输入框与最下面的新增管理员行都在主体区之内，底部按钮在窗口之内
    const RECT body = modal.GetBodyRect();
    FA_EXPECT_TRUE(RectInside(modal.FindCtrlById(kIdEntSetName)->GetRect(), body));
    FA_EXPECT_TRUE(RectInside(modal.FindCtrlById(kIdEntSetNewEmail)->GetRect(), body));
    FA_EXPECT_TRUE(RectInside(modal.FindCtrlById(kIdEntSetAddAdmin)->GetRect(), body));
    FA_EXPECT_TRUE(RectInside(modal.FindCtrlById(kIdEntSetSave)->GetRect(), kMinWindow));
    //滚轮与 ScrollTo 都不改变滚动量；滚轮仍由弹窗处理掉，不传给下层页面
    FA_EXPECT_TRUE(modal.OnMouseWheel(RectCenter(body), -WHEEL_DELTA, 0));
    FA_EXPECT_EQ(modal.GetScroll(), 0);
    modal.ScrollTo(100);
    FA_EXPECT_EQ(modal.GetScroll(), 0);
}

// ESS2：管理员很多时卡片不超出窗口、底部按钮可见、内容可以滚动
void TestSettingsModalMinWindow()
{
    BeginCase("ESS2 企业设置弹窗：最小窗口下十二位管理员时卡片不超出窗口，取消与保存完整可见，内容可滚动");

    EnterpriseSettingsModal modal;
    OpenSettingsModal(modal, kManyAdmins, kMinWindow);
    const RECT close = modal.FindCtrlById(kIdEntSetClose)->GetRect();
    const RECT cancel = modal.FindCtrlById(kIdEntSetCancel)->GetRect();
    const RECT save = modal.FindCtrlById(kIdEntSetSave)->GetRect();
    FA_EXPECT_TRUE(RectInside(close, kMinWindow));
    FA_EXPECT_TRUE(RectInside(cancel, kMinWindow));
    FA_EXPECT_TRUE(RectInside(save, kMinWindow));
    //主体区在标题栏之下、按钮之上，内容放不下，可以滚动
    const RECT body = modal.GetBodyRect();
    FA_EXPECT_TRUE(body.top > close.bottom);
    FA_EXPECT_TRUE(body.bottom <= save.top);
    FA_EXPECT_TRUE(modal.GetScrollMax() > 0);
    FA_EXPECT_EQ(modal.GetScroll(), 0);
}

// ESS3：滚到底 / 顶
void TestSettingsModalScrollEnds()
{
    BeginCase("ESS3 企业设置弹窗：滚到底时新增管理员行完整可见，滚到顶时第一个输入框完整可见，滚动量不越界");

    EnterpriseSettingsModal modal;
    OpenSettingsModal(modal, kManyAdmins, kMinWindow);
    const RECT body = modal.GetBodyRect();
    const int maxScroll = modal.GetScrollMax();
    FA_EXPECT_TRUE(maxScroll > 0);
    //一开始在顶部：第一个输入框可见，最下面的新增管理员行在主体区之外
    FA_EXPECT_TRUE(RectInside(modal.FindCtrlById(kIdEntSetName)->GetRect(), body));
    FA_EXPECT_FALSE(RectInside(modal.FindCtrlById(kIdEntSetNewEmail)->GetRect(), body));

    //滚轮向下一格：滚动量增加，不超过最大值
    FA_EXPECT_TRUE(modal.OnMouseWheel(RectCenter(body), -WHEEL_DELTA, 0));
    FA_EXPECT_TRUE(modal.GetScroll() > 0);
    FA_EXPECT_TRUE(modal.GetScroll() <= maxScroll);

    //滚到底（给一个超出范围的值）：夹取到最大值，新增管理员行与「添加」完整可见
    modal.ScrollTo(maxScroll + 1000);
    FA_EXPECT_EQ(modal.GetScroll(), maxScroll);
    FA_EXPECT_TRUE(RectInside(modal.FindCtrlById(kIdEntSetNewEmail)->GetRect(), body));
    FA_EXPECT_TRUE(RectInside(modal.FindCtrlById(kIdEntSetAddAdmin)->GetRect(), body));
    //主体区与底部按钮的位置不随滚动变化
    FA_EXPECT_TRUE(modal.GetBodyRect().top == body.top && modal.GetBodyRect().bottom == body.bottom);

    //滚到顶（给一个负数）：夹取到 0，第一个输入框完整可见
    modal.ScrollTo(-50);
    FA_EXPECT_EQ(modal.GetScroll(), 0);
    FA_EXPECT_TRUE(RectInside(modal.FindCtrlById(kIdEntSetName)->GetRect(), body));
}

// 命中的控件 hit 是否就是 ancestor 或在它之内（沿父控件链向上查找）。hit 为空时返回 false。
static bool IsWithinControl(const balloonwjui::DuiControl* hit, const balloonwjui::DuiControl* ancestor)
{
    for (const balloonwjui::DuiControl* c = hit; c != nullptr; c = c->GetParent())
    {
        if (c == ancestor)
        {
            return true;
        }
    }
    return false;
}

// ESS4：命中测试
void TestSettingsModalHitTest()
{
    BeginCase("ESS4 企业设置弹窗：滚出主体区的子控件点不到，标题栏与按钮栏的按钮始终点得到");

    EnterpriseSettingsModal modal;
    OpenSettingsModal(modal, kManyAdmins, kMinWindow);
    balloonwjui::DuiControl* addAdmin = modal.FindCtrlById(kIdEntSetAddAdmin);
    balloonwjui::DuiControl* save = modal.FindCtrlById(kIdEntSetSave);
    balloonwjui::DuiControl* close = modal.FindCtrlById(kIdEntSetClose);

    //顶部：「添加」按钮在主体区之外，点它所在的位置命中不到它
    FA_EXPECT_FALSE(IsWithinControl(modal.HitTest(RectCenter(addAdmin->GetRect())), addAdmin));
    //按钮始终点得到
    FA_EXPECT_TRUE(modal.HitTest(RectCenter(save->GetRect())) == save);
    FA_EXPECT_TRUE(modal.HitTest(RectCenter(close->GetRect())) == close);

    //滚到底：「添加」点得到，第一个输入框（已滚出顶边）点不到，按钮仍然点得到
    modal.ScrollTo(modal.GetScrollMax());
    FA_EXPECT_TRUE(IsWithinControl(modal.HitTest(RectCenter(addAdmin->GetRect())), addAdmin));
    balloonwjui::DuiControl* nameEdit = modal.FindCtrlById(kIdEntSetName);
    const RECT nameRc = nameEdit->GetRect();
    if (nameRc.bottom <= modal.GetBodyRect().top)
    {
        FA_EXPECT_TRUE(modal.HitTest(RectCenter(nameRc)) != nameEdit);
    }
    FA_EXPECT_TRUE(modal.HitTest(RectCenter(save->GetRect())) == save);
    FA_EXPECT_TRUE(modal.HitTest(RectCenter(close->GetRect())) == close);
}

// ESS5：管理员很多时
void TestSettingsModalManyAdmins()
{
    BeginCase("ESS5 企业设置弹窗：默认窗口下十二位管理员时同样能滚到底，卡片不超出窗口");

    EnterpriseSettingsModal modal;
    OpenSettingsModal(modal, kManyAdmins, kDefaultWindow);
    FA_EXPECT_TRUE(RectInside(modal.FindCtrlById(kIdEntSetSave)->GetRect(), kDefaultWindow));
    FA_EXPECT_TRUE(RectInside(modal.FindCtrlById(kIdEntSetClose)->GetRect(), kDefaultWindow));
    FA_EXPECT_TRUE(modal.GetScrollMax() > 0);
    modal.ScrollTo(modal.GetScrollMax());
    FA_EXPECT_TRUE(RectInside(modal.FindCtrlById(kIdEntSetAddAdmin)->GetRect(), modal.GetBodyRect()));
    //新增管理员行在顶部时或在主体区之外、或完整可见
    modal.ScrollTo(0);
    const RECT body = modal.GetBodyRect();
    const RECT email = modal.FindCtrlById(kIdEntSetNewEmail)->GetRect();
    FA_EXPECT_TRUE(email.top >= body.bottom || RectInside(email, body));
}

// FSN1：紧凑样式的短名称长度
void TestFeatureSwitchCompactNames()
{
    BeginCase("FSN1 所有功能开关紧凑样式的短名称都不超过五个字");

    //紧凑样式的名称列宽 kCompactNameW 按「点对点直连」五个字定，超过就被截成省略号
    const int kCompactNameMaxChars = 5;
    const FeatureSwitchSpec* const specs[] = { &FileShareSwitchSpec(), &P2PSwitchSpec(), &InviteConfirmSwitchSpec(),
                                               &RegisterSwitchSpec(), &GroupCallSwitchSpec(), &CaptionSwitchSpec(),
                                               &SecretChatSwitchSpec(), &ThreadGroupSwitchSpec(), &CloudRecordSwitchSpec() };
    for (size_t i = 0; i < sizeof(specs) / sizeof(specs[0]); ++i)
    {
        const CString name(specs[i]->m_compactName);
        FA_EXPECT_TRUE(!name.IsEmpty());
        FA_EXPECT_TRUE(name.GetLength() <= kCompactNameMaxChars);
    }
}

// 平台设置页用例的公共部分：默认窗口（1440×900）下的内容区。2026-10-08 起「功能开关」卡移到企业详情的「功能开关」
// 子 tab，本页只剩「个人资料」与「修改密码」两张卡，默认窗口下放得下
const RECT kSettingsWideRect = { 0, 0, 1440 - theme::kSidebarWidth, 900 - theme::kTopBarHeight };

// 矩形 inner 是否完整落在 outer 之内（平台设置页用例用）。
static bool SettingsRectInside(const RECT& inner, const RECT& outer)
{
    return inner.left >= outer.left && inner.right <= outer.right && inner.top >= outer.top && inner.bottom <= outer.bottom;
}

// 矩形的中心点（平台设置页用例用）。
static POINT SettingsRectCenter(const RECT& rc)
{
    POINT pt = { (rc.left + rc.right) / 2, (rc.top + rc.bottom) / 2 };
    return pt;
}

// SSS1：内容放得下时不滚动
void TestSettingsScreenFits()
{
    BeginCase("SSS1 平台设置页：默认窗口下两张卡放得下，不滚动，滚轮交给上层");

    SettingsScreen screen;
    screen.SetRect(kSettingsWideRect);
    FA_EXPECT_EQ(screen.GetScrollMax(), 0);
    FA_EXPECT_FALSE(screen.OnMouseWheel(SettingsRectCenter(kSettingsWideRect), -WHEEL_DELTA, 0));
    FA_EXPECT_EQ(screen.GetScroll(), 0);
    screen.ScrollTo(100);
    FA_EXPECT_EQ(screen.GetScroll(), 0);
    //最下面的「修改密码」按钮在可视区内
    FA_EXPECT_TRUE(SettingsRectInside(screen.FindCtrlById(kIdSettingsChangePw)->GetRect(), kSettingsWideRect));
}

// SSS3：最小窗口下的「修改密码」按钮能滚进可视区
void TestSettingsScreenMinPlatformAdmin()
{
    BeginCase("SSS3 平台设置页：最小窗口下可滚动，「修改密码」按钮能滚进可视区，滚动量不越界");

    SettingsScreen screen;
    screen.SetRect(kMinScreenRect);
    const int maxScroll = screen.GetScrollMax();
    FA_EXPECT_TRUE(maxScroll > 0);
    const RECT before = screen.FindCtrlById(kIdSettingsChangePw)->GetRect();
    FA_EXPECT_FALSE(SettingsRectInside(before, kMinScreenRect));
    FA_EXPECT_TRUE(screen.OnMouseWheel(SettingsRectCenter(kMinScreenRect), -WHEEL_DELTA, 0));
    FA_EXPECT_TRUE(screen.GetScroll() > 0 && screen.GetScroll() <= maxScroll);
    screen.ScrollTo(maxScroll + 1000);
    FA_EXPECT_EQ(screen.GetScroll(), maxScroll);
    FA_EXPECT_TRUE(SettingsRectInside(screen.FindCtrlById(kIdSettingsChangePw)->GetRect(), kMinScreenRect));
    screen.ScrollTo(-10);
    FA_EXPECT_EQ(screen.GetScroll(), 0);
}

// SSS4：滚出本界面的控件点不到
void TestSettingsScreenHitTest()
{
    BeginCase("SSS4 平台设置页：滚出本界面的控件点不到，滚进来的控件点得到");

    SettingsScreen screen;
    screen.SetRect(kMinScreenRect);
    balloonwjui::DuiControl* nameEdit = screen.FindCtrlById(kIdSettingsName);
    balloonwjui::DuiControl* changePw = screen.FindCtrlById(kIdSettingsChangePw);
    //顶部：「修改密码」按钮在本界面之外，点不到
    FA_EXPECT_FALSE(IsWithinControl(screen.HitTest(SettingsRectCenter(changePw->GetRect())), changePw));
    //滚到底：「修改密码」点得到，姓名输入框（已滚出顶边时）点不到
    screen.ScrollTo(screen.GetScrollMax());
    FA_EXPECT_TRUE(IsWithinControl(screen.HitTest(SettingsRectCenter(changePw->GetRect())), changePw));
    const RECT nameRc = nameEdit->GetRect();
    if (nameRc.bottom <= kMinScreenRect.top)
    {
        FA_EXPECT_TRUE(screen.HitTest(SettingsRectCenter(nameRc)) != nameEdit);
    }
}

// CRSW1：「云端录制」的面板（cloudrecord.enabled，docs/屏幕共享与录制方案.md 12.6.5）
void TestFeaturePanelCloudRecord()
{
    BeginCase("CRSW1 功能开关面板：云端录制，键为 cloudrecord.enabled，默认关闭，关闭不用危险样式");

    const FeatureSwitchSpec& spec = CloudRecordSwitchSpec();
    FA_EXPECT_STREQ(spec.m_key, _T("cloudrecord.enabled"));
    FA_EXPECT_STREQ(spec.m_name, _T("云端录制"));
    FA_EXPECT_STREQ(spec.m_compactName, _T("云端录制"));
    FA_EXPECT_FALSE(spec.m_offIsDanger);

    LoginPlatformAdmin();
    Captured cap;
    FeatureSwitchPanel panel(spec, kApiTagCloudRecordSwitchGet, kApiTagCloudRecordSwitchSet,
                             kIdFeatSwitchCloudRecord, true);
    panel.SetRequestSink(&CaptureRequest, &cap);
    panel.SetUiRequestHandler(&CaptureUi, &cap);
    panel.SetRect(kPanelRect);
    panel.Load(kEntA);
    Json::Value body;
    FA_EXPECT_TRUE(LastBody(cap, kApiTagCloudRecordSwitchGet, body));

    //内置默认值为关闭；同一份应答里的数值项不影响开关
    const std::string settings =
        "{\"code\":0,\"msg\":\"ok\",\"enterpriseid\":" + std::to_string(kEntA) + ",\"settings\":["
        "{\"key\":\"cloudrecord.enabled\",\"value\":\"0\",\"isdefault\":1},"
        "{\"key\":\"cloudrecord.quotagb\",\"value\":\"50\",\"isdefault\":1}]}";
    panel.OnApiResult(kApiTagCloudRecordSwitchGet, MakeHttpOk(settings));
    FA_EXPECT_EQ(panel.GetState(), static_cast<int>(FeatureSwitchPanel::kStateLoaded));
    FA_EXPECT_FALSE(panel.IsSwitchChecked());
    FA_EXPECT_STREQ(panel.StatusText(), _T("当前为默认设置：关闭"));

    //打开：确认框说明录像占用云端录像空间，确认后提交 cloudrecord.enabled=1
    panel.Switch()->SetChecked(true, false, false);
    panel.OnSwitchToggled(true);
    fsui::UiRequest confirm = LastUi(cap, fsui::kUiConfirm);
    FA_EXPECT_STREQ(confirm.m_title, _T("打开云端录制"));
    FA_EXPECT_TRUE(confirm.m_text.Find(_T("云端录像空间")) >= 0);
    FA_EXPECT_NOTNULL(confirm.m_target);
    if (confirm.m_target == nullptr)
        return;
    confirm.m_target->OnConfirmResult(true);
    FA_EXPECT_TRUE(LastBody(cap, kApiTagCloudRecordSwitchSet, body));
    FA_EXPECT_TRUE(body["key"].asString() == "cloudrecord.enabled");
    FA_EXPECT_TRUE(body["value"].asString() == "1");
    panel.OnApiResult(kApiTagCloudRecordSwitchSet,
                      MakeHttpOk("{\"code\":0,\"msg\":\"ok\",\"enterpriseid\":9001,\"enabled\":1}"));
    FA_EXPECT_TRUE(panel.IsSwitchChecked());
    FA_EXPECT_STREQ(LastUi(cap, fsui::kUiToast).m_text, _T("已打开云端录制"));

    //关闭：不是危险样式，正文说明已有的云端录像照常可以观看
    panel.Switch()->SetChecked(false, false, false);
    panel.OnSwitchToggled(false);
    confirm = LastUi(cap, fsui::kUiConfirm);
    FA_EXPECT_STREQ(confirm.m_title, _T("关闭云端录制"));
    FA_EXPECT_FALSE(confirm.m_danger);
    FA_EXPECT_TRUE(confirm.m_text.Find(_T("已有的云端录像照常可以观看")) >= 0);
    if (confirm.m_target != nullptr)
        confirm.m_target->OnConfirmResult(false);
    FA_EXPECT_TRUE(panel.IsSwitchChecked());
}

// 「云端录像空间」面板读设置的应答：配额、保留天数与已用空间（usedBytes 小于 0 时不带这一项）
static std::string CloudQuotaSettingsJson(long long enterpriseId, const char* quota, const char* days, long long usedBytes)
{
    std::string json = "{\"code\":0,\"msg\":\"ok\",\"enterpriseid\":" + std::to_string(enterpriseId);
    if (usedBytes >= 0)
        json += ",\"cloudrecordusedbytes\":" + std::to_string(usedBytes);
    json += ",\"settings\":[{\"key\":\"cloudrecord.enabled\",\"value\":\"1\",\"isdefault\":0},"
            "{\"key\":\"cloudrecord.quotagb\",\"value\":\"" + std::string(quota) + "\",\"isdefault\":1},"
            "{\"key\":\"cloudrecord.retaindays\",\"value\":\"" + std::string(days) + "\",\"isdefault\":1}]}";
    return json;
}

// 请求出口收到的、标签为 tag 的请求个数
static int CountRequests(const Captured& c, ApiTag tag)
{
    int n = 0;
    for (size_t i = 0; i < c.m_requests.size(); ++i)
    {
        if (c.m_requests[i].m_tag == static_cast<UINT_PTR>(tag))
            ++n;
    }
    return n;
}

// 1.2 GB 的字节数（状态文字核对用）
const long long kCrqUsedBytes = 1288490188LL;

// CRQ1：输入校验与状态文字
void TestCloudQuotaPure()
{
    BeginCase("CRQ1 云端录像空间：输入校验与状态文字");

    long long quota = 0;
    long long days = 0;
    CString error;
    FA_EXPECT_TRUE(CloudRecordQuotaPanel::ParseInputs(_T(" 50 "), _T("30"), quota, days, error));
    FA_EXPECT_TRUE(quota == 50 && days == 30);
    FA_EXPECT_TRUE(CloudRecordQuotaPanel::ParseInputs(_T("0"), _T("0"), quota, days, error));
    FA_EXPECT_TRUE(CloudRecordQuotaPanel::ParseInputs(_T("1000000"), _T("3650"), quota, days, error));
    //超出范围、负数、小数、空串、带字母：不合法，并写明哪一项
    FA_EXPECT_FALSE(CloudRecordQuotaPanel::ParseInputs(_T("1000001"), _T("30"), quota, days, error));
    FA_EXPECT_TRUE(error.Find(_T("配额")) >= 0);
    FA_EXPECT_FALSE(CloudRecordQuotaPanel::ParseInputs(_T("50"), _T("3651"), quota, days, error));
    FA_EXPECT_TRUE(error.Find(_T("保留天数")) >= 0);
    FA_EXPECT_FALSE(CloudRecordQuotaPanel::ParseInputs(_T("-1"), _T("30"), quota, days, error));
    FA_EXPECT_FALSE(CloudRecordQuotaPanel::ParseInputs(_T("1.5"), _T("30"), quota, days, error));
    FA_EXPECT_FALSE(CloudRecordQuotaPanel::ParseInputs(_T(""), _T("30"), quota, days, error));
    FA_EXPECT_FALSE(CloudRecordQuotaPanel::ParseInputs(_T("50"), _T("3a"), quota, days, error));
    FA_EXPECT_FALSE(CloudRecordQuotaPanel::ParseInputs(_T("99999999999999999999"), _T("30"), quota, days, error));

    FA_EXPECT_STREQ(CloudRecordQuotaPanel::UsageText(kCrqUsedBytes, 50), _T("已用 1.2 GB / 共 50 GB"));
    FA_EXPECT_STREQ(CloudRecordQuotaPanel::UsageText(0, 50), _T("已用 0 B / 共 50 GB"));
    FA_EXPECT_STREQ(CloudRecordQuotaPanel::UsageText(0, 0), _T("已用 0 B，配额为 0，不能再新增"));
    FA_EXPECT_STREQ(CloudRecordQuotaPanel::UsageText(-1, 50), _T("共 50 GB"));
}

// CRQ2：读设置
void TestCloudQuotaLoad()
{
    BeginCase("CRQ2 云端录像空间：读设置，企业对不上的应答丢弃，缺项按读取失败");

    LoginPlatformAdmin();
    Captured cap;
    CloudRecordQuotaPanel panel(kApiTagCloudQuotaGet, kApiTagCloudQuotaSet, kIdCloudQuotaSave,
                                kIdCloudQuotaQuota, kIdCloudQuotaDays);
    panel.SetRequestSink(&CaptureRequest, &cap);
    panel.SetUiRequestHandler(&CaptureUi, &cap);
    panel.SetRect(RECT{ 0, 0, 500, CloudRecordQuotaPanel::kPanelHeight });
    FA_EXPECT_EQ(panel.GetState(), static_cast<int>(CloudRecordQuotaPanel::kStateIdle));
    FA_EXPECT_FALSE(panel.SaveButton()->IsEnabled());

    panel.Load(kEntA);
    Json::Value body;
    FA_EXPECT_TRUE(LastBody(cap, kApiTagCloudQuotaGet, body));
    FA_EXPECT_TRUE(body["enterpriseid"].asInt64() == kEntA);
    FA_EXPECT_STREQ(panel.StatusText(), _T("正在读取…"));
    FA_EXPECT_FALSE(panel.QuotaEdit()->IsEnabled());

    //别的企业的应答：丢弃，仍在读取
    panel.OnApiResult(kApiTagCloudQuotaGet, MakeHttpOk(CloudQuotaSettingsJson(kEntB, "10", "7", 0)));
    FA_EXPECT_EQ(panel.GetState(), static_cast<int>(CloudRecordQuotaPanel::kStateLoading));

    //本企业的应答：填进输入框，状态写已用与配额
    panel.OnApiResult(kApiTagCloudQuotaGet, MakeHttpOk(CloudQuotaSettingsJson(kEntA, "50", "30", kCrqUsedBytes)));
    FA_EXPECT_EQ(panel.GetState(), static_cast<int>(CloudRecordQuotaPanel::kStateLoaded));
    FA_EXPECT_TRUE(panel.QuotaGb() == 50 && panel.RetainDays() == 30 && panel.UsedBytes() == kCrqUsedBytes);
    FA_EXPECT_STREQ(panel.QuotaEdit()->GetText(), _T("50"));
    FA_EXPECT_STREQ(panel.DaysEdit()->GetText(), _T("30"));
    FA_EXPECT_STREQ(panel.StatusText(), _T("已用 1.2 GB / 共 50 GB"));
    FA_EXPECT_TRUE(panel.QuotaEdit()->IsEnabled() && panel.DaysEdit()->IsEnabled() && panel.SaveButton()->IsEnabled());

    //服务端没有给出两个数值项（老服务端）：按读取失败处理，不能修改
    CloudRecordQuotaPanel old(kApiTagCloudQuotaGet, kApiTagCloudQuotaSet, kIdCloudQuotaSave,
                              kIdCloudQuotaQuota, kIdCloudQuotaDays);
    old.SetRequestSink(&CaptureRequest, &cap);
    old.SetUiRequestHandler(&CaptureUi, &cap);
    old.Load(kEntA);
    old.OnApiResult(kApiTagCloudQuotaGet, MakeHttpOk(SettingsJson(kEntA, "1", 1)));
    FA_EXPECT_EQ(old.GetState(), static_cast<int>(CloudRecordQuotaPanel::kStateFailed));
    FA_EXPECT_TRUE(old.StatusText().Find(_T("读取失败")) == 0);
    FA_EXPECT_FALSE(old.SaveButton()->IsEnabled());
}

// CRQ3：保存
void TestCloudQuotaSave()
{
    BeginCase("CRQ3 云端录像空间：没有修改不提交，不合法提示，只提交有变化的项，两个应答都回来才提示，失败后重新读取");

    LoginPlatformAdmin();
    Captured cap;
    CloudRecordQuotaPanel panel(kApiTagCloudQuotaGet, kApiTagCloudQuotaSet, kIdCloudQuotaSave,
                                kIdCloudQuotaQuota, kIdCloudQuotaDays);
    panel.SetRequestSink(&CaptureRequest, &cap);
    panel.SetUiRequestHandler(&CaptureUi, &cap);
    panel.Load(kEntA);
    panel.OnApiResult(kApiTagCloudQuotaGet, MakeHttpOk(CloudQuotaSettingsJson(kEntA, "50", "30", 0)));

    //没有修改：只提示，不发请求
    panel.OnSaveClicked();
    FA_EXPECT_EQ(CountRequests(cap, kApiTagCloudQuotaSet), 0);
    FA_EXPECT_STREQ(LastUi(cap, fsui::kUiToast).m_text, _T("云端录像空间没有修改"));

    //不合法：错误提示，不发请求，仍可修改
    panel.QuotaEdit()->SetText(_T("abc"));
    panel.OnSaveClicked();
    FA_EXPECT_EQ(CountRequests(cap, kApiTagCloudQuotaSet), 0);
    FA_EXPECT_TRUE(LastUi(cap, fsui::kUiToast).m_isError);
    FA_EXPECT_EQ(panel.GetState(), static_cast<int>(CloudRecordQuotaPanel::kStateLoaded));

    //只改天数：只提交 retaindays
    panel.QuotaEdit()->SetText(_T("50"));
    panel.DaysEdit()->SetText(_T("0"));
    panel.OnSaveClicked();
    FA_EXPECT_EQ(CountRequests(cap, kApiTagCloudQuotaSet), 1);
    Json::Value body;
    FA_EXPECT_TRUE(LastBody(cap, kApiTagCloudQuotaSet, body));
    FA_EXPECT_TRUE(body["key"].asString() == "cloudrecord.retaindays");
    FA_EXPECT_TRUE(body["value"].asString() == "0");
    FA_EXPECT_EQ(panel.GetState(), static_cast<int>(CloudRecordQuotaPanel::kStateSaving));
    FA_EXPECT_FALSE(panel.SaveButton()->IsEnabled());
    panel.OnApiResult(kApiTagCloudQuotaSet, MakeHttpOk("{\"code\":0,\"msg\":\"ok\",\"enterpriseid\":9001,\"enabled\":0}"));
    FA_EXPECT_EQ(panel.GetState(), static_cast<int>(CloudRecordQuotaPanel::kStateLoaded));
    FA_EXPECT_TRUE(panel.RetainDays() == 0 && panel.QuotaGb() == 50);
    FA_EXPECT_STREQ(LastUi(cap, fsui::kUiToast).m_text, _T("已保存云端录像空间"));

    //两项都改：连发两个请求，第一个应答回来时仍在保存、不提示
    const size_t toastsBefore = cap.m_ui.size();
    panel.QuotaEdit()->SetText(_T("80"));
    panel.DaysEdit()->SetText(_T("60"));
    panel.OnSaveClicked();
    FA_EXPECT_EQ(CountRequests(cap, kApiTagCloudQuotaSet), 3);
    panel.OnApiResult(kApiTagCloudQuotaSet, MakeHttpOk("{\"code\":0,\"msg\":\"ok\",\"enterpriseid\":9001,\"enabled\":0}"));
    FA_EXPECT_EQ(panel.GetState(), static_cast<int>(CloudRecordQuotaPanel::kStateSaving));
    FA_EXPECT_EQ(cap.m_ui.size(), toastsBefore);
    //第二个失败：提示失败原因，重新读一次设置
    const int getsBefore = CountRequests(cap, kApiTagCloudQuotaGet);
    panel.OnApiResult(kApiTagCloudQuotaSet, MakeHttpOk("{\"code\":133,\"msg\":\"bad value\"}"));
    FA_EXPECT_TRUE(LastUi(cap, fsui::kUiToast).m_isError);
    FA_EXPECT_TRUE(LastUi(cap, fsui::kUiToast).m_text.Find(_T("保存失败")) == 0);
    FA_EXPECT_EQ(CountRequests(cap, kApiTagCloudQuotaGet), getsBefore + 1);
    FA_EXPECT_EQ(panel.GetState(), static_cast<int>(CloudRecordQuotaPanel::kStateLoading));
}

// FSC1：文件共享页的结构与默认页签
void TestCenterDefaults()
{
    BeginCase("FSC1 文件共享页：页签切换与「刷新」都在页头右侧，默认在「文件共享」页签，副标题随页签");

    FileShareCenterScreen center;
    FA_EXPECT_EQ(center.ActiveTab(), static_cast<int>(FileShareCenterScreen::kTabShares));
    FA_EXPECT_NOTNULL(center.SharesPage());
    FA_EXPECT_NOTNULL(center.AuditPage());
    if (center.SharesPage() == nullptr || center.AuditPage() == nullptr)
        return;
    FA_EXPECT_TRUE(center.SharesPage()->IsVisible());
    FA_EXPECT_FALSE(center.AuditPage()->IsVisible());
    FA_EXPECT_STREQ(center.Subtitle(), FileShareCenterScreen::SubtitleFor(FileShareCenterScreen::kTabShares));
    FA_EXPECT_TRUE(center.Subtitle().Find(_T("强制取消")) >= 0);
    FA_EXPECT_TRUE(CString(FileShareCenterScreen::SubtitleFor(FileShareCenterScreen::kTabAudit)).Find(_T("操作记录")) >= 0);
    FA_EXPECT_STREQ(FileShareCenterScreen::SubtitleFor(FileShareCenterScreen::kTabCount), _T(""));
    //页签切换控件与「刷新」各只有一个，都属于外层页面（两个页签不再自带页头）
    ui::SegControl* tabs = static_cast<ui::SegControl*>(center.FindCtrlById(kIdFsTabs));
    FA_EXPECT_NOTNULL(tabs);
    if (tabs != nullptr)
    {
        FA_EXPECT_EQ(tabs->GetActiveIndex(), static_cast<int>(FileShareCenterScreen::kTabShares));
    }
    FA_EXPECT_NOTNULL(center.FindCtrlById(kIdFsRefresh));
    FA_EXPECT_NULL(center.SharesPage()->FindCtrlById(kIdFsRefresh));
    FA_EXPECT_NULL(center.AuditPage()->FindCtrlById(kIdFsRefresh));

    //排版后：页签切换在页头（高 100）之内、「刷新」左边，不另占一行；页签内容紧接在页头之下
    const int kCenterHeaderH = 100;
    center.SetRect(kScreenRect);
    balloonwjui::DuiControl* refresh = center.FindCtrlById(kIdFsRefresh);
    if (tabs != nullptr && refresh != nullptr)
    {
        const RECT tabsRc = tabs->GetRect();
        FA_EXPECT_TRUE(tabsRc.top >= kScreenRect.top && tabsRc.bottom <= kScreenRect.top + kCenterHeaderH);
        FA_EXPECT_TRUE(tabsRc.right <= refresh->GetRect().left);
        FA_EXPECT_TRUE(tabsRc.right > tabsRc.left);
    }
    FA_EXPECT_EQ(center.SharesPage()->GetRect().top, kScreenRect.top + kCenterHeaderH);
}

// FSC2：切换页签与刷新
void TestCenterSwitchTabs()
{
    BeginCase("FSC2 文件共享页：切换页签时显隐、副标题与页签控件同步，进入新页签刷新它，「刷新」只刷新当前页签");

    LoginPlatformAdmin();
    Captured cap;
    FileShareCenterScreen center;
    center.SetRequestSink(&CaptureRequest, &cap);
    center.SetUiRequestHandler(&CaptureUi, &cap);
    center.SetRect(kScreenRect);
    center.OnEnter();
    FA_EXPECT_EQ(Count(cap, kApiTagFsList), 1);
    FA_EXPECT_EQ(Count(cap, kApiTagFsAudit), 0);
    //页签在加载中时只记下「待会儿再查一次」、不重复发请求，所以每次核对下一个请求之前先让应答回来
    center.SharesPage()->OnApiResult(kApiTagFsList, MakeHttpOk(SharesJson(2, 2)));
    const RECT sharesSearch = center.SharesPage()->SearchBox()->GetRect();

    //切到「共享审计」：它显示并刷新，「文件共享」隐藏
    center.OnTabChanged(FileShareCenterScreen::kTabAudit);
    FA_EXPECT_EQ(center.ActiveTab(), static_cast<int>(FileShareCenterScreen::kTabAudit));
    FA_EXPECT_TRUE(center.AuditPage()->IsVisible());
    FA_EXPECT_FALSE(center.SharesPage()->IsVisible());
    FA_EXPECT_STREQ(center.Subtitle(), FileShareCenterScreen::SubtitleFor(FileShareCenterScreen::kTabAudit));
    ui::SegControl* tabs = static_cast<ui::SegControl*>(center.FindCtrlById(kIdFsTabs));
    if (tabs != nullptr)
    {
        FA_EXPECT_EQ(tabs->GetActiveIndex(), static_cast<int>(FileShareCenterScreen::kTabAudit));
    }
    FA_EXPECT_EQ(Count(cap, kApiTagFsAudit), 1);
    center.AuditPage()->OnApiResult(kApiTagFsAudit, MakeHttpOk(AuditJson()));
    //新显示的页签已按当前矩形排好：筛选行与「文件共享」页签的筛选行在同一高度，表格有高度
    FA_EXPECT_EQ(center.AuditPage()->SearchBox()->GetRect().top, sharesSearch.top);
    const RECT auditTable = center.AuditPage()->Table()->GetRect();
    FA_EXPECT_TRUE(auditTable.bottom > auditTable.top);

    //「刷新」只刷新当前页签
    center.Refresh();
    FA_EXPECT_EQ(Count(cap, kApiTagFsAudit), 2);
    FA_EXPECT_EQ(Count(cap, kApiTagFsList), 1);
    center.AuditPage()->OnApiResult(kApiTagFsAudit, MakeHttpOk(AuditJson()));

    //越界的页签下标不做任何事
    center.OnTabChanged(FileShareCenterScreen::kTabCount);
    FA_EXPECT_EQ(center.ActiveTab(), static_cast<int>(FileShareCenterScreen::kTabAudit));
    FA_EXPECT_EQ(Count(cap, kApiTagFsAudit), 2);

    //切回「文件共享」：再刷新一次它的列表
    center.OnTabChanged(FileShareCenterScreen::kTabShares);
    FA_EXPECT_TRUE(center.SharesPage()->IsVisible());
    FA_EXPECT_FALSE(center.AuditPage()->IsVisible());
    FA_EXPECT_EQ(Count(cap, kApiTagFsList), 2);
    center.SharesPage()->OnApiResult(kApiTagFsList, MakeHttpOk(SharesJson(2, 2)));
    center.Refresh();
    FA_EXPECT_EQ(Count(cap, kApiTagFsList), 3);
    FA_EXPECT_EQ(Count(cap, kApiTagFsAudit), 2);
}

// FSC3：页签在同一次登录内保持
void TestCenterRemembersTab()
{
    BeginCase("FSC3 文件共享页：同一次登录内再次进入停在上次的页签，换了登录身份回到第一个页签");

    LoginPlatformAdmin();
    Captured cap;
    FileShareCenterScreen center;
    center.SetRequestSink(&CaptureRequest, &cap);
    center.SetUiRequestHandler(&CaptureUi, &cap);
    center.SetRect(kScreenRect);
    //直接打开「共享审计」页签（Screen::FileShareAudit 走这条路）
    center.SelectTab(FileShareCenterScreen::kTabAudit);
    FA_EXPECT_EQ(center.ActiveTab(), static_cast<int>(FileShareCenterScreen::kTabAudit));
    FA_EXPECT_EQ(Count(cap, kApiTagFsAudit), 1);
    //让应答回来（加载中时页签不重复发请求）
    center.AuditPage()->OnApiResult(kApiTagFsAudit, MakeHttpOk(AuditJson()));
    //同一身份再次进入：仍在「共享审计」，并刷新它
    center.OnEnter();
    FA_EXPECT_EQ(center.ActiveTab(), static_cast<int>(FileShareCenterScreen::kTabAudit));
    FA_EXPECT_EQ(Count(cap, kApiTagFsAudit), 2);
    FA_EXPECT_EQ(Count(cap, kApiTagFsList), 0);
    //换了登录身份：回到「文件共享」页签
    LoginEnterpriseAdmin();
    center.OnEnter();
    FA_EXPECT_EQ(center.ActiveTab(), static_cast<int>(FileShareCenterScreen::kTabShares));
    FA_EXPECT_TRUE(center.SharesPage()->IsVisible());
    FA_EXPECT_FALSE(center.AuditPage()->IsVisible());
    FA_EXPECT_EQ(Count(cap, kApiTagFsList), 1);
}

void RunAllFileShareScreensTests()
{
    TestActionTableVisibility();
    TestKeyValueGrid();
    TestFeaturePanelLoad();
    TestFeaturePanelToggle();
    TestFeaturePanelFailures();
    TestFeaturePanelP2P();
    TestFeaturePanelInviteConfirm();
    TestFeaturePanelRegister();
    TestFeaturePanelGroupCall();
    TestFeaturePanelCaption();
    TestFeaturePanelSecretChat();
    TestFeaturePanelThreadGroup();
    TestFeaturePanelCloudRecord();
    TestCloudQuotaPure();
    TestCloudQuotaLoad();
    TestCloudQuotaSave();
    TestFileSharesEnter();
    TestFileSharesResults();
    TestFileSharesDetail();
    TestFileSharesCancel();
    TestFileSharesFilters();
    TestFileSharesMinLayout();
    TestAuditEnter();
    TestAuditFilters();
    TestAuditResults();
    TestAuditMinLayout();
    TestSettingsModalFits();
    TestSettingsModalMinWindow();
    TestSettingsModalScrollEnds();
    TestSettingsModalHitTest();
    TestSettingsModalManyAdmins();
    TestFeatureSwitchCompactNames();
    TestSettingsScreenFits();
    TestSettingsScreenMinPlatformAdmin();
    TestSettingsScreenHitTest();
    TestCenterDefaults();
    TestCenterSwitchTabs();
    TestCenterRemembersTab();
    //恢复为未登录，免得影响之后的用例
    AdminSession::Instance().Clear();
}

}} // namespace flamingoadmin::test
