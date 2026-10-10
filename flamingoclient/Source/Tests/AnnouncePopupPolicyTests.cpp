/**
 *  AnnouncePopupPolicyTests.cpp
 *
 *  「有新群公告」提示窗弹不弹的判定（BalloonGroupManage/AnnouncePopupPolicy）
 *  的单元测试。
 *
 *  这一层值得单独测：拒绝弹窗的理由有四条（非法推送 / 撤回 / 自己发的 / 已经弹过），
 *  而其中"已经弹过"这条要同时挡住"断线重连后重复推来的同一条"与"推送乱序时补到的
 *  更旧的一条"—— 后者靠手工点界面基本不可能复现。
 *
 *  AP11（2026-10-08）：入群必读弹窗以群聊窗为所有者，不会被随后激活的群聊窗压在下面（实测出过）；
 *  有所有者、风格为 WS_OVERLAPPEDWINDOW 时 GetParent 为空，控件点击通知仍发回弹窗自身（UI 约定第 11 条）。
 *  主面板的「新群公告」弹窗仍不设所有者，主面板收在托盘里时它照样能弹出来。
 *
 *  balloonwj@qq.com   2026-08-03
 */

#include "stdafx.h"   // AP11 引入的弹窗头文件依赖 ATL/WTL（balloonui 头文件约定）
#include "catch_amalgamated.hpp"

#include "../UI/BalloonGroupManage/AnnouncePopupPolicy.h"
#include "../UI/BalloonGroupManage/BalloonAnnouncePopupDlg.h"   // AP11：弹窗的所有者

using namespace balloongroupmanage;

// changetype 取值，与服务端 group_announcement_change_type 一致。
static const int kChangePublish = 1;   // 新发布
static const int kChangeRevoke  = 2;   // 已撤回

// 造一份"应当弹窗"的基准输入，各用例在它之上只改自己关心的那一项。
static AnnouncePopupInput MakeBaseInput()
{
    AnnouncePopupInput in;
    in.m_changetype   = kChangePublish;
    in.m_announceId   = 15;
    in.m_publisherId  = 100023;   //别人发的
    in.m_selfUserId   = 100001;   //我自己
    in.m_lastPoppedId = 0;        //这个群还没弹过
    return in;
}

TEST_CASE("AP1 别人新发布的公告要弹窗", "[announcepopup]")
{
    CHECK(ShouldPopupAnnouncement(MakeBaseInput()));
}

TEST_CASE("AP2 自己发的公告不弹给自己", "[announcepopup]")
{
    //刚点完"发布"就被自己的公告拦一下，只会让人以为出错了
    AnnouncePopupInput in = MakeBaseInput();
    in.m_publisherId = in.m_selfUserId;
    CHECK_FALSE(ShouldPopupAnnouncement(in));
}

TEST_CASE("AP3 未登录时不因发布人相同而误判", "[announcepopup]")
{
    //selfUserId 为 0 表示取不到当前登录者。此时不能把"publisher == 0"当成
    //"是我自己发的"—— 那会把一条 publisher 缺失的公告静默吞掉。
    AnnouncePopupInput in = MakeBaseInput();
    in.m_selfUserId  = 0;
    in.m_publisherId = 0;
    CHECK(ShouldPopupAnnouncement(in));
}

TEST_CASE("AP4 撤回推送不弹窗", "[announcepopup]")
{
    //撤回是管理动作，不该反过来打扰全群一次
    AnnouncePopupInput in = MakeBaseInput();
    in.m_changetype = kChangeRevoke;
    CHECK_FALSE(ShouldPopupAnnouncement(in));

    //不认识的 changetype 同样不弹（只认"新发布"这一种）
    in.m_changetype = 99;
    CHECK_FALSE(ShouldPopupAnnouncement(in));
    in.m_changetype = 0;
    CHECK_FALSE(ShouldPopupAnnouncement(in));
}

TEST_CASE("AP5 公告ID非正的推送不弹窗", "[announcepopup]")
{
    //这条推送本身就不合法，界面上也无从定位它
    AnnouncePopupInput in = MakeBaseInput();
    in.m_announceId = 0;
    CHECK_FALSE(ShouldPopupAnnouncement(in));

    in.m_announceId = -1;
    CHECK_FALSE(ShouldPopupAnnouncement(in));
}

TEST_CASE("AP6 同一条公告重复推来不再弹（断线重连场景）", "[announcepopup]")
{
    //断线重连后服务端会把这条再推一次；已经弹过就不该再弹
    AnnouncePopupInput in = MakeBaseInput();
    in.m_announceId   = 15;
    in.m_lastPoppedId = 15;
    CHECK_FALSE(ShouldPopupAnnouncement(in));
}

TEST_CASE("AP7 乱序补到的更旧的一条不再弹", "[announcepopup]")
{
    //已经弹过 20 号，此时补到 15 号 —— 用户看到的应该是更新的那条，
    //不该被一条旧公告顶回去
    AnnouncePopupInput in = MakeBaseInput();
    in.m_announceId   = 15;
    in.m_lastPoppedId = 20;
    CHECK_FALSE(ShouldPopupAnnouncement(in));
}

TEST_CASE("AP8 比弹过的更新的一条要弹", "[announcepopup]")
{
    AnnouncePopupInput in = MakeBaseInput();
    in.m_announceId   = 21;
    in.m_lastPoppedId = 20;
    CHECK(ShouldPopupAnnouncement(in));
}

TEST_CASE("AP9 四条拒绝理由的优先级：任一成立即不弹", "[announcepopup]")
{
    //拒绝理由之间是"或"的关系，不该出现"因为是新公告所以绕过了自己发的判断"这种事
    AnnouncePopupInput selfAndNew = MakeBaseInput();
    selfAndNew.m_publisherId = selfAndNew.m_selfUserId;
    selfAndNew.m_announceId  = 999;      //足够新
    CHECK_FALSE(ShouldPopupAnnouncement(selfAndNew));

    AnnouncePopupInput revokeAndNew = MakeBaseInput();
    revokeAndNew.m_changetype = kChangeRevoke;
    revokeAndNew.m_announceId = 999;
    CHECK_FALSE(ShouldPopupAnnouncement(revokeAndNew));
}

// =====================================================================
// 记账的读写
// =====================================================================

TEST_CASE("AP10 记账：路径为空或群ID非正时安全跳过", "[announcepopup]")
{
    //未登录时拿不到配置文件路径。此时读回 0（当作"没弹过"），写入静默跳过 ——
    //不能因此崩溃，也不能往一个空路径写文件。
    CHECK(LoadLastPoppedAnnounceId(NULL, 10000000001LL) == 0);
    CHECK(LoadLastPoppedAnnounceId(_T(""), 10000000001LL) == 0);
    CHECK(LoadLastPoppedAnnounceId(_T("C:\\nonexistent\\x.ini"), 0) == 0);

    //这几个调用只要不崩、不抛就算通过
    SaveLastPoppedAnnounceId(NULL, 10000000001LL, 15);
    SaveLastPoppedAnnounceId(_T(""), 10000000001LL, 15);
    SUCCEED();
}

TEST_CASE("AP11 记账：从没弹过的群读回 0", "[announcepopup]")
{
    //"读回 0"与"确实弹过第 0 条"不会混淆 —— 公告ID 从 1 起，0 不是合法ID
    CHECK(LoadLastPoppedAnnounceId(_T("C:\\nonexistent\\flamingo-test.ini"),
                                   10000000001LL) == 0);
}

TEST_CASE("AP11 入群必读弹窗以宿主窗口为所有者，点击通知仍发回自身；主面板的新群公告弹窗不设所有者", "[announcepopup][mustread]")
{
    //宿主：一个不显示的普通顶层窗口，代替群聊窗 / 主面板
    HWND host = ::CreateWindowExW(0, L"STATIC", L"announce popup host", WS_OVERLAPPEDWINDOW,
                                  0, 0, 200, 200, NULL, NULL, ::GetModuleHandle(NULL), NULL);
    REQUIRE(host != NULL);

    //入群必读：以宿主为所有者 —— 有所有者的窗口总在所有者之上
    {
        CBalloonAnnouncePopupDlg dlg;
        dlg.SetOwnedByHost(true);
        dlg.ShowAnnouncement(host, 268435456, L"讨论小组", L"开会", L"大家来501开会。", 0, L"韩晓燕888", L"刚刚");
        REQUIRE(dlg.IsWindow());
        CHECK(::GetWindow(dlg.m_hWnd, GW_OWNER) == host);
        //有所有者的重叠窗口 GetParent 为空，DuiHost 的控件通知因此发回弹窗自身（UI 约定第 11 条）
        CHECK(::GetParent(dlg.m_hWnd) == NULL);
        CHECK((dlg.GetStyle() & WS_CHILD) == 0);
        dlg.DestroyWindow();
    }

    //主面板的新群公告：不设所有者（主面板收在托盘里时，有所有者的窗口会跟着隐藏）
    {
        CBalloonAnnouncePopupDlg dlg;
        dlg.ShowAnnouncement(host, 268435456, L"讨论小组", L"开会", L"大家来501开会。", 0, L"韩晓燕888", L"刚刚");
        REQUIRE(dlg.IsWindow());
        CHECK(::GetWindow(dlg.m_hWnd, GW_OWNER) == NULL);
        dlg.DestroyWindow();
    }

    ::DestroyWindow(host);
}
