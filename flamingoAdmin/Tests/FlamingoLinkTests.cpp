/**
 *  flamingoAdmin —— FlamingoLink.dll 纯函数部分的单元测试（FLK1~FLK7、FLK9）。
 *
 *  测试工程直接编译 DLL 里与网络、窗口无关的三个源文件（FlamingoLink/Core 下的 LinkProtocol、
 *  OnlineTable、LinkText），它们以 #include "json.h" 取 jsoncpp，在本工程里取到的是控制台那份。
 *  连接线程与重连的行为另由 FlamingoLinkLiveTests.cpp（FLK8）加载编好的 DLL 做黑盒测试。
 *    - FLK1：重连间隔 1、2、4、8、16、30、30 秒，各在 ±20% 之内；
 *    - FLK2：2501 应答解析与归类：心跳与超时的默认值和取值范围，1100 归为会话过期；
 *    - FLK3：2504 推送解析：用户、设备、reset；非法内容的处理；
 *    - FLK4：名单合并：reset 清空、按用户整体替换、设备为空即删除、每台设备一行、别的企业的推送忽略；
 *    - FLK5：筛选与排序：按姓名 / 账号 / 部门不区分大小写，状态顺序与同名排序；
 *    - FLK6：汇总与界面文字：人数、设备数、各类设备台数，连接状态、倒计时与表格提示；
 *    - FLK7：订阅意图：页面活动且有企业才订阅、隐藏退订、换企业改订、换走又换回时重订；企业ID文本解析；
 *    - FLK9：工程守卫：FlamingoLink.vcxproj 四个配置都定义 WIN32、SODIUM_STATIC 并使用静态 CRT。
 *
 *  balloonwj@qq.com   2026-10-04
 */
#include "stdafx.h"
#include "Tests/TestFramework.h"
#include "FlamingoLink/FlamingoLinkApi.h"
#include "FlamingoLink/Core/LinkProtocol.h"
#include "FlamingoLink/Core/OnlineTable.h"
#include "FlamingoLink/Core/LinkText.h"
#include "json.h"

#include <fstream>
#include <sstream>

namespace flamingoadmin { namespace test {

namespace {

using namespace flamingolink;

// 示例里的企业与用户 ID
const int64_t kEnt = 123;
const int64_t kOtherEnt = 456;
const int64_t kUserA = 10001;
const int64_t kUserB = 10002;
const int64_t kUserC = 10003;
const int64_t kUserD = 10004;

// 客户端类型与在线状态取值（与协议一致）
const int kPc = 1;
const int kAndroid = 2;
const int kIos = 3;
const int kOnline = 1;
const int kAway = 2;
const int kBusy = 3;

// 示例登录时间（Unix 毫秒）
const int64_t kLoginMs = 1759550000000LL;

// 重连间隔的取值（毫秒）
const int kOneSecondMs = 1000;
const int kRetryCapMs = 30000;

// 构造一台设备
LinkDevice MakeDevice(int clientType, int presence)
{
    LinkDevice d;
    d.m_clientType = clientType;
    d.m_presence = presence;
    d.m_loginTimeMs = kLoginMs;
    d.m_ip = _T("10.0.0.1");
    return d;
}

// 构造一个用户
LinkUser MakeUser(int64_t id, LPCTSTR account, LPCTSTR nickname, LPCTSTR dept)
{
    LinkUser u;
    u.m_userId = id;
    u.m_account = account;
    u.m_nickname = nickname;
    if (dept != NULL)
        u.m_depts.push_back(dept);
    return u;
}

// 一条 2504 推送
OnlineNotify MakeNotify(int64_t ent, bool reset)
{
    OnlineNotify n;
    n.m_enterpriseId = ent;
    n.m_reset = reset;
    return n;
}

// FLK1：重连间隔
void TestRetryDelay()
{
    BeginCase("FLK1 重连间隔按 1、2、4、8、16、30 秒递增并封顶，随机浮动在 ±20% 之内");

    // 随机数取 0.5 时正好是基准值
    const int expected[] = { 1000, 2000, 4000, 8000, 16000, 30000, 30000 };
    for (int i = 0; i < 7; ++i)
        FA_EXPECT_EQ(ComputeRetryDelayMs(i + 1, 0.5), expected[i]);

    // 浮动范围：随机数 0 得到下限（基准值的 80%），接近 1 得到上限（不到 120%）
    FA_EXPECT_EQ(ComputeRetryDelayMs(1, 0.0), 800);
    FA_EXPECT_TRUE(ComputeRetryDelayMs(1, 0.999999) < 1200);
    FA_EXPECT_TRUE(ComputeRetryDelayMs(1, 0.999999) >= 1199);
    FA_EXPECT_EQ(ComputeRetryDelayMs(10, 0.0), kRetryCapMs * 8 / 10);
    FA_EXPECT_TRUE(ComputeRetryDelayMs(10, 0.999999) < kRetryCapMs * 12 / 10);

    // 次数小于 1 按第 1 次；随机数越界时夹到边界
    FA_EXPECT_EQ(ComputeRetryDelayMs(0, 0.5), kOneSecondMs);
    FA_EXPECT_EQ(ComputeRetryDelayMs(-3, 0.5), kOneSecondMs);
    FA_EXPECT_EQ(ComputeRetryDelayMs(1, -1.0), 800);
    FA_EXPECT_TRUE(ComputeRetryDelayMs(1, 5.0) < 1200);

    // 次数很大时不溢出，仍封顶
    FA_EXPECT_EQ(ComputeRetryDelayMs(1000, 0.5), kRetryCapMs);
}

// FLK2：鉴权请求与应答
void TestAuth()
{
    BeginCase("FLK2 2501 应答解析：心跳与超时取默认值并夹到范围内，1100 归为会话过期");

    // 请求：token 与版本号原样带上
    {
        Json::Value v;
        Json::CharReaderBuilder b;
        std::string errs;
        std::istringstream in(BuildAuthRequest("9f2c", "1.0.0"));
        FA_EXPECT_TRUE(Json::parseFromStream(b, in, &v, &errs));
        FA_EXPECT_TRUE(v["token"].asString() == "9f2c");
        FA_EXPECT_TRUE(v["clientversion"].asString() == "1.0.0");
    }

    AuthResult r;
    FA_EXPECT_TRUE(ParseAuthResponse(
        "{\"code\":0,\"msg\":\"ok\",\"adminid\":\"7\",\"enterpriseid\":\"0\",\"heartbeat\":10,\"timeout\":30}", r));
    FA_EXPECT_EQ(r.m_code, 0);
    FA_EXPECT_EQ(r.m_adminId, 7);
    FA_EXPECT_EQ(r.m_enterpriseId, 0);
    FA_EXPECT_EQ(r.m_heartbeatSec, 10);
    FA_EXPECT_EQ(r.m_timeoutSec, 30);

    // 企业管理员
    FA_EXPECT_TRUE(ParseAuthResponse("{\"code\":0,\"adminid\":\"8\",\"enterpriseid\":\"123\"}", r));
    FA_EXPECT_EQ(r.m_enterpriseId, kEnt);
    // 缺心跳与超时：取默认值
    FA_EXPECT_EQ(r.m_heartbeatSec, kDefaultHeartbeatSec);
    FA_EXPECT_EQ(r.m_timeoutSec, kDefaultTimeoutSec);

    // 越界：夹到范围内
    FA_EXPECT_TRUE(ParseAuthResponse("{\"code\":0,\"heartbeat\":0,\"timeout\":100000}", r));
    FA_EXPECT_EQ(r.m_heartbeatSec, kMinHeartbeatSec);
    FA_EXPECT_EQ(r.m_timeoutSec, kMaxTimeoutSec);
    FA_EXPECT_TRUE(ParseAuthResponse("{\"code\":0,\"heartbeat\":999,\"timeout\":1}", r));
    FA_EXPECT_EQ(r.m_heartbeatSec, kMaxHeartbeatSec);
    FA_EXPECT_EQ(r.m_timeoutSec, kMinTimeoutSec);

    // 失败应答
    FA_EXPECT_TRUE(ParseAuthResponse("{\"code\":1100,\"msg\":\"token invalid\"}", r));
    FA_EXPECT_EQ(r.m_code, 1100);

    // 非法内容
    FA_EXPECT_FALSE(ParseAuthResponse("not json", r));
    FA_EXPECT_FALSE(ParseAuthResponse("{\"msg\":\"ok\"}", r));
    FA_EXPECT_FALSE(ParseAuthResponse("{\"code\":\"0\"}", r));

    // 归类
    FA_EXPECT_EQ(static_cast<int>(ClassifyAuth(0)), static_cast<int>(auth_ok));
    FA_EXPECT_EQ(static_cast<int>(ClassifyAuth(1100)), static_cast<int>(auth_expired));
    FA_EXPECT_EQ(static_cast<int>(ClassifyAuth(3)), static_cast<int>(auth_retry));
    FA_EXPECT_EQ(static_cast<int>(ClassifyAuth(1)), static_cast<int>(auth_retry));
}

// FLK3：2504 推送解析
void TestNotifyParse()
{
    BeginCase("FLK3 2504 推送解析：用户、设备与 reset；非法内容的处理");

    const char* sample =
        "{\"enterpriseid\":\"123\",\"reset\":1,\"users\":["
        "{\"userid\":\"10001\",\"account\":\"zhangsan\",\"nickname\":\"\xE5\xBC\xA0\xE4\xB8\x89\","
        "\"depts\":[\"\xE7\xA0\x94\xE5\x8F\x91\xE9\x83\xA8\",\"QA\"],"
        "\"devices\":[{\"clienttype\":1,\"presence\":1,\"logintime\":1759550000000,\"ip\":\"192.168.1.10\"},"
        "{\"clienttype\":2,\"presence\":2,\"logintime\":1759551000000,\"ip\":\"10.0.0.8\"}]},"
        "{\"userid\":\"10002\",\"account\":\"lisi\",\"nickname\":\"Li\",\"depts\":[],\"devices\":[]},"
        "{\"userid\":10003,\"account\":\"bad\"},"
        "{\"userid\":\"0\",\"account\":\"bad\"}"
        "]}";

    OnlineNotify n;
    FA_EXPECT_TRUE(ParseOnlineNotify(sample, n));
    FA_EXPECT_EQ(n.m_enterpriseId, kEnt);
    FA_EXPECT_TRUE(n.m_reset);
    // userid 不是正整数文本的两条被忽略
    FA_EXPECT_EQ(static_cast<int>(n.m_users.size()), 2);
    if (n.m_users.size() == 2)
    {
        const LinkUser& a = n.m_users[0];
        FA_EXPECT_EQ(a.m_userId, kUserA);
        FA_EXPECT_STREQ(a.m_account, _T("zhangsan"));
        FA_EXPECT_STREQ(a.m_nickname, _T("张三"));
        FA_EXPECT_EQ(static_cast<int>(a.m_depts.size()), 2);
        FA_EXPECT_EQ(static_cast<int>(a.m_devices.size()), 2);
        if (a.m_devices.size() == 2)
        {
            FA_EXPECT_EQ(a.m_devices[0].m_clientType, kPc);
            FA_EXPECT_EQ(a.m_devices[0].m_presence, kOnline);
            FA_EXPECT_EQ(a.m_devices[0].m_loginTimeMs, kLoginMs);
            FA_EXPECT_STREQ(a.m_devices[0].m_ip, _T("192.168.1.10"));
            FA_EXPECT_EQ(a.m_devices[1].m_clientType, kAndroid);
            FA_EXPECT_EQ(a.m_devices[1].m_presence, kAway);
        }
        FA_EXPECT_TRUE(n.m_users[1].m_devices.empty());
    }

    // reset 缺省为 false
    FA_EXPECT_TRUE(ParseOnlineNotify("{\"enterpriseid\":\"123\",\"users\":[]}", n));
    FA_EXPECT_FALSE(n.m_reset);
    FA_EXPECT_TRUE(n.m_users.empty());

    // 非法：非 JSON、企业ID 为 0 或不是字符串、users 不是数组
    FA_EXPECT_FALSE(ParseOnlineNotify("oops", n));
    FA_EXPECT_FALSE(ParseOnlineNotify("{\"enterpriseid\":\"0\",\"users\":[]}", n));
    FA_EXPECT_FALSE(ParseOnlineNotify("{\"enterpriseid\":123,\"users\":[]}", n));
    FA_EXPECT_FALSE(ParseOnlineNotify("{\"enterpriseid\":\"123\",\"users\":{}}", n));

    // 订阅请求与应答
    FA_EXPECT_TRUE(BuildSubscribeRequest(kEnt) == "{\"enterpriseid\":\"123\"}");
    FA_EXPECT_TRUE(BuildUnsubscribeRequest() == "{}");
    int code = -1;
    int64_t ent = -1;
    FA_EXPECT_TRUE(ParseCodeResponse("{\"code\":0,\"msg\":\"ok\",\"enterpriseid\":\"123\"}", code, ent));
    FA_EXPECT_EQ(code, 0);
    FA_EXPECT_EQ(ent, kEnt);
    FA_EXPECT_TRUE(ParseCodeResponse("{\"code\":1101,\"msg\":\"subscribe rejected\"}", code, ent));
    FA_EXPECT_EQ(code, 1101);
    FA_EXPECT_EQ(ent, 0);
    FA_EXPECT_FALSE(ParseCodeResponse("{}", code, ent));
}

// FLK4：名单合并
void TestMerge()
{
    BeginCase("FLK4 名单合并：reset 清空、按用户整体替换、设备为空即删除、每台设备一行、别的企业忽略");

    OnlineTable t;

    // 完整名单：A 两台设备、B 一台
    OnlineNotify snap = MakeNotify(kEnt, true);
    LinkUser a = MakeUser(kUserA, _T("zhangsan"), _T("张三"), _T("研发部"));
    a.m_devices.push_back(MakeDevice(kPc, kOnline));
    a.m_devices.push_back(MakeDevice(kAndroid, kAway));
    LinkUser b = MakeUser(kUserB, _T("lisi"), _T("李四"), _T("销售部"));
    b.m_devices.push_back(MakeDevice(kPc, kOnline));
    snap.m_users.push_back(a);
    snap.m_users.push_back(b);
    FA_EXPECT_TRUE(t.Apply(snap, kEnt));
    FA_EXPECT_EQ(t.UserCount(), 2);
    FA_EXPECT_EQ(static_cast<int>(t.Rows().size()), 3);

    // B 下线：设备为空即删除
    OnlineNotify off = MakeNotify(kEnt, false);
    off.m_users.push_back(MakeUser(kUserB, _T("lisi"), _T("李四"), NULL));
    FA_EXPECT_TRUE(t.Apply(off, kEnt));
    FA_EXPECT_EQ(t.UserCount(), 1);
    FA_EXPECT_EQ(static_cast<int>(t.Rows().size()), 2);

    // A 的手机下线：整体替换为只剩 PC
    OnlineNotify change = MakeNotify(kEnt, false);
    LinkUser a2 = MakeUser(kUserA, _T("zhangsan"), _T("张三"), _T("研发部"));
    a2.m_devices.push_back(MakeDevice(kPc, kBusy));
    change.m_users.push_back(a2);
    FA_EXPECT_TRUE(t.Apply(change, kEnt));
    FA_EXPECT_EQ(static_cast<int>(t.Rows().size()), 1);
    if (t.Rows().size() == 1)
        FA_EXPECT_EQ(t.Rows()[0].m_presence, kBusy);

    // 别的企业的推送忽略
    OnlineNotify other = MakeNotify(kOtherEnt, true);
    FA_EXPECT_FALSE(t.Apply(other, kEnt));
    FA_EXPECT_EQ(static_cast<int>(t.Rows().size()), 1);

    // 新的完整名单：只剩 D
    OnlineNotify snap2 = MakeNotify(kEnt, true);
    LinkUser d = MakeUser(kUserD, _T("wangwu"), _T("王五"), NULL);
    d.m_devices.push_back(MakeDevice(kIos, kOnline));
    snap2.m_users.push_back(d);
    FA_EXPECT_TRUE(t.Apply(snap2, kEnt));
    FA_EXPECT_EQ(t.UserCount(), 1);
    FA_EXPECT_EQ(static_cast<int>(t.Rows().size()), 1);
    if (t.Rows().size() == 1)
        FA_EXPECT_EQ(t.Rows()[0].m_userId, kUserD);

    // 页面没有企业时一律忽略
    FA_EXPECT_FALSE(t.Apply(snap2, 0));

    t.Clear();
    FA_EXPECT_EQ(t.UserCount(), 0);
    FA_EXPECT_TRUE(t.Rows().empty());
}

// FLK5：筛选与排序
void TestFilterSort()
{
    BeginCase("FLK5 排序为在线、勿扰、离开，再按名字；筛选按姓名 / 账号 / 部门不区分大小写");

    OnlineTable t;
    OnlineNotify snap = MakeNotify(kEnt, true);
    LinkUser away = MakeUser(kUserA, _T("zhangsan"), _T("张三"), _T("研发部"));
    away.m_devices.push_back(MakeDevice(kPc, kAway));
    LinkUser busy = MakeUser(kUserB, _T("lisi"), _T("李四"), _T("销售部"));
    busy.m_devices.push_back(MakeDevice(kPc, kBusy));
    LinkUser online1 = MakeUser(kUserC, _T("bob"), _T("Bob"), _T("研发部"));
    online1.m_devices.push_back(MakeDevice(kAndroid, kOnline));
    online1.m_devices.push_back(MakeDevice(kPc, kOnline));
    LinkUser online2 = MakeUser(kUserD, _T("alice"), _T("Alice"), NULL);
    online2.m_devices.push_back(MakeDevice(kPc, kOnline));
    snap.m_users.push_back(away);
    snap.m_users.push_back(busy);
    snap.m_users.push_back(online1);
    snap.m_users.push_back(online2);
    FA_EXPECT_TRUE(t.Apply(snap, kEnt));

    const std::vector<OnlineRow>& rows = t.Rows();
    FA_EXPECT_EQ(static_cast<int>(rows.size()), 5);
    if (rows.size() == 5)
    {
        // 在线：Alice、Bob（PC 在 Android 之前），然后勿扰的李四，最后离开的张三
        FA_EXPECT_EQ(rows[0].m_userId, kUserD);
        FA_EXPECT_EQ(rows[1].m_userId, kUserC);
        FA_EXPECT_EQ(rows[1].m_clientType, kPc);
        FA_EXPECT_EQ(rows[2].m_userId, kUserC);
        FA_EXPECT_EQ(rows[2].m_clientType, kAndroid);
        FA_EXPECT_EQ(rows[3].m_userId, kUserB);
        FA_EXPECT_EQ(rows[4].m_userId, kUserA);
    }

    std::vector<int> view;
    t.Filter(CString(), view);
    FA_EXPECT_EQ(static_cast<int>(view.size()), 5);

    // 按账号，不区分大小写，首尾空白去掉
    t.Filter(_T("  ZHANG "), view);
    FA_EXPECT_EQ(static_cast<int>(view.size()), 1);
    if (view.size() == 1)
        FA_EXPECT_EQ(rows[static_cast<size_t>(view[0])].m_userId, kUserA);

    // 按部门：研发部有张三（1 行）与 Bob（2 行）
    t.Filter(_T("研发"), view);
    FA_EXPECT_EQ(static_cast<int>(view.size()), 3);

    // 按昵称
    t.Filter(_T("alice"), view);
    FA_EXPECT_EQ(static_cast<int>(view.size()), 1);

    // 没有匹配
    t.Filter(_T("nobody"), view);
    FA_EXPECT_TRUE(view.empty());

    // 名次与部门连接
    FA_EXPECT_TRUE(OnlineTable::PresenceRank(kOnline) < OnlineTable::PresenceRank(kBusy));
    FA_EXPECT_TRUE(OnlineTable::PresenceRank(kBusy) < OnlineTable::PresenceRank(kAway));
    FA_EXPECT_TRUE(OnlineTable::PresenceRank(kAway) < OnlineTable::PresenceRank(0));
    std::vector<CString> depts;
    depts.push_back(_T("研发部"));
    depts.push_back(_T("测试组"));
    FA_EXPECT_STREQ(OnlineTable::JoinDepts(depts), _T("研发部、测试组"));
    FA_EXPECT_TRUE(OnlineTable::JoinDepts(std::vector<CString>()).IsEmpty());
}

// FLK6：汇总与界面文字
void TestSummaryText()
{
    BeginCase("FLK6 汇总人数与设备台数，连接状态、倒计时与表格提示的文字");

    OnlineTable t;
    FA_EXPECT_STREQ(SummaryText(t.Summary()), _T("当前没有在线用户"));

    OnlineNotify snap = MakeNotify(kEnt, true);
    LinkUser a = MakeUser(kUserA, _T("zhangsan"), _T("张三"), NULL);
    a.m_devices.push_back(MakeDevice(kPc, kOnline));
    a.m_devices.push_back(MakeDevice(kAndroid, kOnline));
    LinkUser b = MakeUser(kUserB, _T("lisi"), _T("李四"), NULL);
    b.m_devices.push_back(MakeDevice(kPc, kAway));
    snap.m_users.push_back(a);
    snap.m_users.push_back(b);
    t.Apply(snap, kEnt);

    const OnlineSummary s = t.Summary();
    FA_EXPECT_EQ(s.m_users, 2);
    FA_EXPECT_EQ(s.m_devices, 3);
    FA_EXPECT_EQ(s.m_pc, 2);
    FA_EXPECT_EQ(s.m_android, 1);
    FA_EXPECT_EQ(s.m_ios, 0);
    FA_EXPECT_STREQ(SummaryText(s), _T("在线 2 人、3 台设备（PC 2 · Android 1）"));

    FA_EXPECT_STREQ(ClientTypeText(kPc), _T("PC"));
    FA_EXPECT_STREQ(ClientTypeText(kIos), _T("iOS"));
    FA_EXPECT_STREQ(ClientTypeText(99), _T("未知设备"));
    FA_EXPECT_STREQ(PresenceText(kOnline), _T("在线"));
    FA_EXPECT_STREQ(PresenceText(kAway), _T("离开"));
    FA_EXPECT_STREQ(PresenceText(kBusy), _T("勿扰"));
    FA_EXPECT_STREQ(FormatLoginTime(0), _T("—"));
    FA_EXPECT_EQ(FormatLoginTime(kLoginMs).GetLength(), 16);   // yyyy-MM-dd HH:mm

    FA_EXPECT_STREQ(ConnectionText(link_state_connected, 0, 0), _T("实时连接正常"));
    FA_EXPECT_STREQ(ConnectionText(link_state_connecting, 0, 0), _T("正在连接…"));
    FA_EXPECT_STREQ(ConnectionText(link_state_waiting, 2, 5), _T("连接已断开，5 秒后第 2 次重连"));
    FA_EXPECT_STREQ(ConnectionText(link_state_waiting, 3, 0), _T("连接已断开，正在第 3 次重连"));
    FA_EXPECT_STREQ(ConnectionText(link_state_disabled, 0, 0), _T("服务器未开启实时连接"));
    FA_EXPECT_STREQ(ConnectionText(link_state_expired, 0, 0), _T("登录已过期"));

    FA_EXPECT_EQ(RemainingSeconds(0, 1000), 1);
    FA_EXPECT_EQ(RemainingSeconds(999, 1000), 1);
    FA_EXPECT_EQ(RemainingSeconds(1000, 1000), 0);
    FA_EXPECT_EQ(RemainingSeconds(1, 2500), 3);
    FA_EXPECT_EQ(RemainingSeconds(5000, 1000), 0);

    // 表格提示：按判定顺序逐项
    OverlayInput in;
    FA_EXPECT_STREQ(OverlayText(in), _T("请选择企业"));
    in.m_hasEnterprise = true;
    in.m_state = link_state_disabled;
    FA_EXPECT_STREQ(OverlayText(in), _T("服务器未开启实时连接，无法查看在线用户"));
    in.m_state = link_state_stopped;
    FA_EXPECT_STREQ(OverlayText(in), _T("尚未连接服务器"));
    in.m_state = link_state_expired;
    FA_EXPECT_STREQ(OverlayText(in), _T("登录已过期，请重新登录"));
    in.m_state = link_state_connected;
    in.m_lastError = 1101;
    FA_EXPECT_STREQ(OverlayText(in), _T("无权查看该企业的在线用户"));
    in.m_lastError = 1102;
    FA_EXPECT_STREQ(OverlayText(in), _T("企业不存在"));
    in.m_lastError = 0;
    FA_EXPECT_STREQ(OverlayText(in), _T("正在加载在线名单…"));
    in.m_state = link_state_waiting;
    FA_EXPECT_STREQ(OverlayText(in), _T("正在连接服务器…"));
    in.m_received = true;
    FA_EXPECT_STREQ(OverlayText(in), _T("当前没有在线用户"));
    in.m_totalRows = 3;
    FA_EXPECT_STREQ(OverlayText(in), _T("没有符合条件的在线用户"));
    in.m_filteredRows = 2;
    FA_EXPECT_TRUE(OverlayText(in).IsEmpty());
}

// FLK7：订阅意图
void TestSubscriptionIntent()
{
    BeginCase("FLK7 页面活动且有企业才订阅，隐藏退订，换企业改订；企业ID文本解析");

    FA_EXPECT_EQ(DesiredEnterprise(false, kEnt), 0);
    FA_EXPECT_EQ(DesiredEnterprise(true, 0), 0);
    FA_EXPECT_EQ(DesiredEnterprise(true, kEnt), kEnt);

    FA_EXPECT_EQ(static_cast<int>(DecideSubscription(kEnt, 0, false)), static_cast<int>(subscribe_send));
    FA_EXPECT_EQ(static_cast<int>(DecideSubscription(0, kEnt, false)), static_cast<int>(subscribe_unsubscribe));
    FA_EXPECT_EQ(static_cast<int>(DecideSubscription(kOtherEnt, kEnt, false)), static_cast<int>(subscribe_send));
    FA_EXPECT_EQ(static_cast<int>(DecideSubscription(kEnt, kEnt, false)), static_cast<int>(subscribe_none));
    FA_EXPECT_EQ(static_cast<int>(DecideSubscription(0, 0, false)), static_cast<int>(subscribe_none));

    // 换过企业（甲 → 乙 → 甲）后期望与已发出的相同，但界面已清空名单：要重新订阅
    FA_EXPECT_EQ(static_cast<int>(DecideSubscription(kEnt, kEnt, true)), static_cast<int>(subscribe_send));
    // 不订阅时代次变化没有意义
    FA_EXPECT_EQ(static_cast<int>(DecideSubscription(0, 0, true)), static_cast<int>(subscribe_none));
    FA_EXPECT_EQ(static_cast<int>(DecideSubscription(0, kEnt, true)), static_cast<int>(subscribe_unsubscribe));

    int64_t id = -1;
    FA_EXPECT_TRUE(ParseEnterpriseIdText(L"123", id));
    FA_EXPECT_EQ(id, kEnt);
    FA_EXPECT_FALSE(ParseEnterpriseIdText(L"0", id));
    FA_EXPECT_EQ(id, 0);
    FA_EXPECT_FALSE(ParseEnterpriseIdText(L"", id));
    FA_EXPECT_FALSE(ParseEnterpriseIdText(NULL, id));
    FA_EXPECT_FALSE(ParseEnterpriseIdText(L"12a", id));
    FA_EXPECT_FALSE(ParseEnterpriseIdText(L"-1", id));

    // UTF-8 与 CString 往返
    const CString text = _T("研发部 R&D");
    FA_EXPECT_STREQ(Utf8ToCString(CStringToUtf8(text)), text);
}

// 取本测试文件所在目录（__FILE__ 的目录部分）
std::string ThisDir()
{
    std::string path(__FILE__);
    const size_t pos = path.find_last_of("\\/");
    return (pos == std::string::npos) ? std::string() : path.substr(0, pos);
}

// 读入整个文本文件；失败返回 false
bool ReadText(const std::string& path, std::string& out)
{
    std::ifstream in(path.c_str(), std::ios::in | std::ios::binary);
    if (!in)
        return false;
    std::ostringstream ss;
    ss << in.rdbuf();
    out = ss.str();
    return true;
}

// 从 text 里取出 begin 之后第一对 <tag>…</tag> 之间的内容；找不到返回空串
std::string ExtractTag(const std::string& text, size_t begin, size_t end, const char* tag)
{
    const std::string open = std::string("<") + tag + ">";
    const std::string close = std::string("</") + tag + ">";
    const size_t a = text.find(open, begin);
    if (a == std::string::npos || a >= end)
        return std::string();
    const size_t b = text.find(close, a);
    if (b == std::string::npos || b > end)
        return std::string();
    return text.substr(a + open.size(), b - a - open.size());
}

// FLK9：工程守卫
void TestProjectGuard()
{
    BeginCase("FLK9 FlamingoLink.vcxproj 四个配置都定义 WIN32、SODIUM_STATIC 并使用静态 CRT");

    std::string text;
    const std::string path = ThisDir() + "\\..\\FlamingoLink\\FlamingoLink.vcxproj";
    FA_EXPECT_TRUE(ReadText(path, text));
    if (text.empty())
        return;

    const char* configs[] = { "Debug|x64", "Release|x64", "Debug|Win32", "Release|Win32" };
    for (int i = 0; i < 4; ++i)
    {
        const std::string cond = std::string("<ItemDefinitionGroup Condition=\"'$(Configuration)|$(Platform)'=='")
                                 + configs[i] + "'\">";
        const size_t begin = text.find(cond);
        FA_EXPECT_TRUE(begin != std::string::npos);
        if (begin == std::string::npos)
            continue;
        size_t end = text.find("</ItemDefinitionGroup>", begin);
        if (end == std::string::npos)
            end = text.size();

        //只看 <ClCompile> 块里的宏，<ResourceCompile> 等块里的宏对 C++ 编译不生效
        const size_t clBegin = text.find("<ClCompile>", begin);
        const size_t clEnd = text.find("</ClCompile>", begin);
        FA_EXPECT_TRUE(clBegin != std::string::npos && clEnd != std::string::npos && clEnd < end);
        if (clBegin == std::string::npos || clEnd == std::string::npos || clEnd >= end)
            continue;

        const std::string defs = ";" + ExtractTag(text, clBegin, clEnd, "PreprocessorDefinitions") + ";";
        FA_EXPECT_TRUE(defs.find(";WIN32;") != std::string::npos);
        FA_EXPECT_TRUE(defs.find(";SODIUM_STATIC;") != std::string::npos);

        const std::string crt = ExtractTag(text, clBegin, clEnd, "RuntimeLibrary");
        const bool debug = (std::string(configs[i]).find("Debug") == 0);
        FA_EXPECT_TRUE(crt == (debug ? "MultiThreadedDebug" : "MultiThreaded"));
    }
}

} // anonymous namespace

// 运行本文件全部用例。
void RunAllFlamingoLinkTests()
{
    TestRetryDelay();
    TestAuth();
    TestNotifyParse();
    TestMerge();
    TestFilterSort();
    TestSummaryText();
    TestSubscriptionIntent();
    TestProjectGuard();
}

}} // namespace flamingoadmin::test
