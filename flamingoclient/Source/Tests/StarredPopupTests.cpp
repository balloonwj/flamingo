/**
 *  StarredPopupTests.cpp
 *
 *  「特别关注」上下线提示相关纯逻辑的测试：
 *    · 提示窗的行模型 BuildStarredPopupRows —— 去重、封顶、「等 N 人」、顺序、脏数据。
 *    · 单行文字 FormatStarredPresenceText —— 含显示名缺失时的回落。
 *    · 上下线跳变判定 IsBuddyJustWentOffline —— 与既有的 IsBuddyJustCameOnline 对称。
 *    · 右下角浮窗的落点计算 ComputeCornerOrigin —— 含窗口比工作区还大时的兜底。
 *
 *  用 Catch2 v3（amalgamated）作框架，纯内存断言，不创建窗口 / 不依赖消息循环。
 *  全局 _Module 由 BalloonChatLayoutTests.cpp 统一定义，本文件只引用。
 *
 *  balloonwj@qq.com   2026-08-12
 */

#include "stdafx.h"

#include "catch_amalgamated.hpp"

#include "../BalloonMain/StarredPopupModel.h"
#include "../BalloonMain/CornerPopupPlacement.h"
#include "../BalloonMain/NotificationSound.h"
#include "../net/Protocol.h"   // presence_offline / presence_online / presence_away / presence_busy

using namespace balloonmain;

namespace {

//造一条上下线事件。
StarredPresenceEvent MakeEvent(int64_t userid, LPCTSTR name, bool online)
{
    StarredPresenceEvent e;
    e.m_userid = userid;
    e.m_name   = name;
    e.m_online = online;
    return e;
}

} // anonymous namespace

// =====================================================================
// SPM 组：提示窗的行模型
// =====================================================================

// 已作废的在线状态取值：4 曾经是「隐身」，本产品不提供该功能（见 net/Protocol.h）。
// 老服务端仍可能推上来，故保留这些用例，确认它被当作越界值归到"离线"。
static const int kRetiredPresence = 4;

TEST_CASE("SPM1 单条事件排成一行", "[starred][popup]")
{
    std::vector<StarredPresenceEvent> events;
    events.push_back(MakeEvent(100007, _T("张三"), true));

    const std::vector<StarredPopupRow> rows = BuildStarredPopupRows(events, kStarredPopupMaxRows);
    REQUIRE(rows.size() == 1);
    CHECK(rows[0].m_text == _T("张三 已上线"));
    CHECK(rows[0].m_userid == 100007);
    CHECK_FALSE(rows[0].m_isSummary);
}

TEST_CASE("SPM2 下线用「已离线」，显示名缺失时回落成用户ID", "[starred][popup]")
{
    std::vector<StarredPresenceEvent> events;
    events.push_back(MakeEvent(100031, _T("李四"), false));
    //昵称与备注都取不到时不能只剩一句孤零零的「已上线」
    events.push_back(MakeEvent(100077, _T(""), true));

    const std::vector<StarredPopupRow> rows = BuildStarredPopupRows(events, kStarredPopupMaxRows);
    REQUIRE(rows.size() == 2);
    CHECK(rows[0].m_text == _T("李四 已离线"));
    CHECK(rows[1].m_text == _T("用户100077 已上线"));

    //两个入口拼的是同一份措辞
    CHECK(FormatStarredPresenceText(_T("王五"), 1, true) == _T("王五 已上线"));
    CHECK(FormatStarredPresenceText(_T("王五"), 1, false) == _T("王五 已离线"));
    CHECK(FormatStarredPresenceText(_T(""), 42, true) == _T("用户42 已上线"));
}

TEST_CASE("SPM3 同一个人反复变化只留最后一次，位置留在第一次出现处", "[starred][popup]")
{
    std::vector<StarredPresenceEvent> events;
    events.push_back(MakeEvent(100007, _T("张三"), true));    //张三先上线
    events.push_back(MakeEvent(100031, _T("李四"), true));    //李四上线
    events.push_back(MakeEvent(100007, _T("张三"), false));   //张三又下线了

    const std::vector<StarredPopupRow> rows = BuildStarredPopupRows(events, kStarredPopupMaxRows);
    //张三上线又下线，用户要看的是"他现在不在"，不该列成两行让人以为是两个人
    REQUIRE(rows.size() == 2);
    //位置仍在第一位 —— 跟着最后一次跑的话，用户眼看着行在列表里跳来跳去
    CHECK(rows[0].m_userid == 100007);
    CHECK(rows[0].m_text == _T("张三 已离线"));
    CHECK(rows[1].m_userid == 100031);
}

TEST_CASE("SPM4 人数超过上限时末行收敛成「等 N 人」", "[starred][popup]")
{
    std::vector<StarredPresenceEvent> events;
    for (int i = 0; i < 8; ++i)
    {
        CString name;
        name.Format(_T("同事%d"), i);
        events.push_back(MakeEvent(100000 + i, name, true));
    }

    const std::vector<StarredPopupRow> rows = BuildStarredPopupRows(events, 5);
    REQUIRE(rows.size() == 5);

    //前 4 行照常列人
    for (int i = 0; i < 4; ++i)
    {
        INFO("行 index = " << i);
        CHECK(rows[(size_t)i].m_userid == 100000 + i);
        CHECK_FALSE(rows[(size_t)i].m_isSummary);
    }

    //末行是汇总：N 是被折叠掉的人数（8 - 4 = 4），不是总人数
    CHECK(rows[4].m_isSummary);
    CHECK(rows[4].m_text == _T("等 4 人"));
    CHECK(rows[4].m_userid == 0);   //汇总行没有对应的人，点它不该开窗
}

TEST_CASE("SPM5 人数正好等于上限时不出现汇总行", "[starred][popup]")
{
    std::vector<StarredPresenceEvent> events;
    for (int i = 0; i < 5; ++i)
    {
        events.push_back(MakeEvent(100000 + i, _T("同事"), true));
    }

    const std::vector<StarredPopupRow> rows = BuildStarredPopupRows(events, 5);
    REQUIRE(rows.size() == 5);
    //装得下就全列出来，不该白白拿一行去写「等 0 人」
    for (size_t i = 0; i < rows.size(); ++i)
    {
        INFO("行 index = " << i);
        CHECK_FALSE(rows[i].m_isSummary);
    }
}

TEST_CASE("SPM6 去重之后才算人数，不该被重复事件顶出汇总行", "[starred][popup]")
{
    //同一个人变化 10 次，实际只有 1 个人 —— 若先截断再去重就会错列出汇总行
    std::vector<StarredPresenceEvent> events;
    for (int i = 0; i < 10; ++i)
    {
        events.push_back(MakeEvent(100007, _T("张三"), (i % 2) == 0));
    }

    const std::vector<StarredPopupRow> rows = BuildStarredPopupRows(events, 5);
    REQUIRE(rows.size() == 1);
    CHECK_FALSE(rows[0].m_isSummary);
    //最后一次是 i=9，(9 % 2) == 0 为假，即下线
    CHECK(rows[0].m_text == _T("张三 已离线"));
}

TEST_CASE("SPM7 边界：空事件、脏数据、maxRows 非法", "[starred][popup]")
{
    //一条都没有：没有行
    CHECK(BuildStarredPopupRows(std::vector<StarredPresenceEvent>(), 5).empty());

    //userid 非法的事件直接丢掉，不占一行
    std::vector<StarredPresenceEvent> dirty;
    dirty.push_back(MakeEvent(0, _T("脏数据"), true));
    dirty.push_back(MakeEvent(-3, _T("脏数据"), false));
    dirty.push_back(MakeEvent(100007, _T("张三"), true));
    const std::vector<StarredPopupRow> rows = BuildStarredPopupRows(dirty, 5);
    REQUIRE(rows.size() == 1);
    CHECK(rows[0].m_userid == 100007);

    //全是脏数据时一行都不出，窗口也就不该弹
    std::vector<StarredPresenceEvent> allDirty;
    allDirty.push_back(MakeEvent(0, _T("x"), true));
    CHECK(BuildStarredPopupRows(allDirty, 5).empty());

    //maxRows 非法：按"一行都不显示"处理，而不是崩在 maxRows-1 的下标上
    std::vector<StarredPresenceEvent> some;
    some.push_back(MakeEvent(100007, _T("张三"), true));
    CHECK(BuildStarredPopupRows(some, 0).empty());
    CHECK(BuildStarredPopupRows(some, -1).empty());

    //maxRows 为 1 且人数超了：唯一那一行就是汇总行
    std::vector<StarredPresenceEvent> two;
    two.push_back(MakeEvent(100007, _T("张三"), true));
    two.push_back(MakeEvent(100031, _T("李四"), true));
    const std::vector<StarredPopupRow> one = BuildStarredPopupRows(two, 1);
    REQUIRE(one.size() == 1);
    CHECK(one[0].m_isSummary);
    CHECK(one[0].m_text == _T("等 2 人"));
}

// =====================================================================
// SPT 组：上下线跳变判定
// =====================================================================

TEST_CASE("SPT1 下线判定与上线判定严格对称", "[starred][presence]")
{
    //可见在线 → 不可见：算下线
    CHECK(IsBuddyJustWentOffline(presence_online, presence_offline));
    CHECK(IsBuddyJustWentOffline(presence_away, presence_offline));
    CHECK(IsBuddyJustWentOffline(presence_busy, presence_offline));
    //已作废的取值按不可见态处理，与真下线没有区别
    CHECK(IsBuddyJustWentOffline(presence_online, kRetiredPresence));

    //不可见 → 可见在线：是上线，不是下线
    CHECK_FALSE(IsBuddyJustWentOffline(presence_offline, presence_online));
    CHECK(IsBuddyJustCameOnline(presence_offline, presence_online));
}

TEST_CASE("SPT2 在线态之间来回切换既不算上线也不算下线", "[starred][presence]")
{
    //对方锁个屏（在线→离开）就提示一次的话，一上午能弹十几次
    const int visible[] = { presence_online, presence_away, presence_busy };
    for (size_t i = 0; i < sizeof(visible) / sizeof(visible[0]); ++i)
    {
        for (size_t k = 0; k < sizeof(visible) / sizeof(visible[0]); ++k)
        {
            INFO("old = " << visible[i] << ", new = " << visible[k]);
            CHECK_FALSE(IsBuddyJustCameOnline(visible[i], visible[k]));
            CHECK_FALSE(IsBuddyJustWentOffline(visible[i], visible[k]));
        }
    }

    //不可见态之间同理（离线↔已作废取值）
    CHECK_FALSE(IsBuddyJustCameOnline(presence_offline, kRetiredPresence));
    CHECK_FALSE(IsBuddyJustWentOffline(presence_offline, kRetiredPresence));
}

// =====================================================================
// SCP 组：右下角浮窗的落点
// =====================================================================

TEST_CASE("SCP1 贴工作区右下角并留出边距", "[starred][placement]")
{
    RECT work = { 0, 0, 1920, 1040 };   //1080 的屏减去任务栏
    SIZE sz;
    sz.cx = 300;
    sz.cy = 160;

    const POINT at = ComputeCornerOrigin(work, sz, 16, 16);
    CHECK(at.x == 1920 - 300 - 16);
    CHECK(at.y == 1040 - 160 - 16);

    //整窗必须落在工作区内（开发约定文档 UI 约定第 3 条）
    CHECK(at.x >= work.left);
    CHECK(at.y >= work.top);
    CHECK(at.x + sz.cx <= work.right);
    CHECK(at.y + sz.cy <= work.bottom);
}

TEST_CASE("SCP2 副屏（工作区原点非零）也要贴对角", "[starred][placement]")
{
    //主屏右侧接一块副屏：工作区的左上角不是 (0,0)
    RECT work = { 1920, 0, 3840, 1040 };
    SIZE sz;
    sz.cx = 300;
    sz.cy = 160;

    const POINT at = ComputeCornerOrigin(work, sz, 16, 16);
    CHECK(at.x == 3840 - 300 - 16);
    CHECK(at.y == 1040 - 160 - 16);
    CHECK(at.x >= work.left);
}

TEST_CASE("SCP3 任务栏在上 / 在左时按工作区反推，不会被压住", "[starred][placement]")
{
    //任务栏停靠在屏幕上方：工作区 top 不为 0
    RECT work = { 0, 40, 1920, 1080 };
    SIZE sz;
    sz.cx = 300;
    sz.cy = 160;

    const POINT at = ComputeCornerOrigin(work, sz, 16, 16);
    CHECK(at.y == 1080 - 160 - 16);
    CHECK(at.y >= work.top);   //没有越到任务栏底下
}

TEST_CASE("SCP4 窗口比工作区还大时退到左上角，保证关得掉", "[starred][placement]")
{
    RECT work = { 0, 0, 200, 120 };   //极端小分辨率 / 超大缩放
    SIZE sz;
    sz.cx = 300;
    sz.cy = 160;

    const POINT at = ComputeCornerOrigin(work, sz, 16, 16);
    //宁可右下角被截掉一块，也要让左上角那部分（标题栏、关闭按钮）可见
    CHECK(at.x == work.left);
    CHECK(at.y == work.top);
}
