/**
 *  屏幕共享结束提示的单元测试（标签 [shareended]），ShareEndedTests.cpp
 *  背景：2026-10-05 用户要求共享结束时不再弹 MessageBox，改为右下角弹窗，显示时长、分享人与参与者。
 *  覆盖：
 *    - SSET1 文字：时长「时长 01:37」；分享人用顿号连接；没有参与者写「无」；参与者超过上限时只写前几个再加「等 N 人」；
 *    - SSET2 名单：发起人总在分享人最前；中途共享了屏幕的人从参与者移到分享人；中途离开的人保留；别的房间的推送不记；
 *      清空房间号后不再记录；
 *    - SSET3 弹窗：各行文字就位、标题栏写「屏幕共享」；投来任何定时器消息都不关闭；点标题栏关闭（WM_CLOSE）才关闭；
 *    - SSET4 参与者很多时窗口随行数加高，各行与「知道了」按钮都完整落在窗口内；
 *    - SSET5 名单里的本人写「你」，其他人写显示名；写进参与者一行后为「参与者：你、乙」。
 *  框架 Catch2 v3。
 *  balloonwj@qq.com   2026-10-05
 */
#include "stdafx.h"
#include "catch_amalgamated.hpp"

#include <string>
#include <vector>

#include "../../FlamingoMedia/Share/ShareEndedSummary.h"
#include "../../FlamingoMedia/Share/ShareEndedToast.h"
#include "../../FlamingoMedia/Share/ShareRoomPolicy.h"
#include "../Common/I18n/I18n.h"
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiLabel.h"

using namespace screenshare;

namespace {

//测试用的房间号与 userid
const char* const kSetRoom = "ss-5127-1-1";
const char* const kSetOtherRoom = "ss-5308-2-2";
const int64_t kSetHost = 5127;
const int64_t kSetB = 5308;
const int64_t kSetC = 5010;
const int64_t kSetD = 5001;
//时长样例（秒）：1 分 37 秒
const int64_t kSetDuration = 97;
//参与者最多写几个名字
const int kSetMaxNames = 3;
//SSET4 的参与者人数
const int kSetManyPeople = 40;

ShareMember SetMember(int64_t userId, bool sharing)
{
    ShareMember m;
    m.m_userId = userId;
    m.m_sharing = sharing;
    return m;
}

ShareStateNotify SetState(const char* roomId, const std::vector<ShareMember>& members)
{
    ShareStateNotify n;
    n.m_roomId = roomId;
    n.m_hostId = kSetHost;
    n.m_members = members;
    return n;
}

//连接名字：顿号
std::wstring SetJoin(const std::vector<std::wstring>& names)
{
    std::wstring out;
    for (size_t i = 0; i < names.size(); ++i)
    {
        if (i > 0)
            out += TR(L"、");
        out += names[i];
    }
    return out;
}

std::wstring SetLabelText(CShareEndedToast& toast, UINT id)
{
    balloonwjui::DuiControl* content = toast.GetClientContent();
    if (content == NULL)
        return std::wstring();
    balloonwjui::DuiLabel* label = dynamic_cast<balloonwjui::DuiLabel*>(content->FindCtrlById(id));
    if (label == NULL)
        return std::wstring();
    return std::wstring(label->GetText().GetString());
}

int SetWindowHeight(CShareEndedToast& toast)
{
    RECT rc;
    toast.GetWindowRect(&rc);
    return rc.bottom - rc.top;
}

} // namespace

TEST_CASE("SSET1 ended toast texts", "[shareended]")
{
    std::vector<std::wstring> sharers;
    sharers.push_back(L"Host");
    sharers.push_back(L"Bob");
    std::vector<std::wstring> people;
    people.push_back(L"Carol");
    people.push_back(L"Dave");
    ShareEndedTexts t = FormatShareEndedTexts(kSetDuration, sharers, people, kSetMaxNames);
    CHECK(t.m_duration == TRF(L"时长 {0}", FormatShareDuration(kSetDuration)));
    CHECK(t.m_sharers == TRF(L"分享人：{0}", SetJoin(sharers)));
    CHECK(t.m_participants == TRF(L"参与者：{0}", SetJoin(people)));
    //没有参与者
    t = FormatShareEndedTexts(kSetDuration, sharers, std::vector<std::wstring>(), kSetMaxNames);
    CHECK(t.m_participants == std::wstring(TR(L"参与者：无")));
    //参与者超过上限：只写前三个，再加总人数
    std::vector<std::wstring> many;
    for (int i = 0; i < 10; ++i)
        many.push_back(L"P" + std::to_wstring(i));
    t = FormatShareEndedTexts(kSetDuration, sharers, many, kSetMaxNames);
    std::vector<std::wstring> firstThree(many.begin(), many.begin() + kSetMaxNames);
    CHECK(t.m_participants == TRF(L"参与者：{0} 等 {1} 人", SetJoin(firstThree), static_cast<int>(many.size())));
    //负的时长按 0
    t = FormatShareEndedTexts(-5, sharers, people, kSetMaxNames);
    CHECK(t.m_duration == TRF(L"时长 {0}", FormatShareDuration(0)));
}

TEST_CASE("SSET2 participant log", "[shareended]")
{
    ShareParticipantLog log;
    log.Reset(kSetRoom, false);
    CHECK(log.RoomId() == kSetRoom);
    CHECK_FALSE(log.InCall());
    std::vector<ShareMember> first;
    first.push_back(SetMember(kSetHost, true));
    first.push_back(SetMember(kSetB, false));
    first.push_back(SetMember(kSetC, false));
    log.Observe(SetState(kSetRoom, first));
    std::vector<int64_t> sharers = log.Sharers();
    std::vector<int64_t> people = log.Participants();
    REQUIRE(sharers.size() == 1);
    CHECK(sharers[0] == kSetHost);
    REQUIRE(people.size() == 2);
    CHECK(people[0] == kSetB);
    CHECK(people[1] == kSetC);
    //C 共享了屏幕、B 离开了、D 加入
    std::vector<ShareMember> second;
    second.push_back(SetMember(kSetHost, true));
    second.push_back(SetMember(kSetC, true));
    second.push_back(SetMember(kSetD, false));
    log.Observe(SetState(kSetRoom, second));
    sharers = log.Sharers();
    people = log.Participants();
    REQUIRE(sharers.size() == 2);
    CHECK(sharers[0] == kSetHost);
    CHECK(sharers[1] == kSetC);
    REQUIRE(people.size() == 2);
    CHECK(people[0] == kSetB);
    CHECK(people[1] == kSetD);
    //别的房间的推送不记
    std::vector<ShareMember> other;
    other.push_back(SetMember(9999, false));
    log.Observe(SetState(kSetOtherRoom, other));
    CHECK(log.Participants().size() == 2);
    //发起人掉线（宽限期里不在成员列表）时仍排在分享人最前
    ShareParticipantLog late;
    late.Reset(kSetRoom, true);
    CHECK(late.InCall());
    std::vector<ShareMember> noHost;
    noHost.push_back(SetMember(kSetB, false));
    late.Observe(SetState(kSetRoom, noHost));
    REQUIRE(late.Sharers().size() == 1);
    CHECK(late.Sharers()[0] == kSetHost);
    //清空房间号后不再记录
    log.Reset(std::string(), false);
    log.Observe(SetState(kSetRoom, first));
    CHECK(log.Sharers().empty());
    CHECK(log.Participants().empty());
}

TEST_CASE("SSET3 ended toast shows its lines and never closes by itself", "[shareended]")
{
    ShareEndedTexts t;
    t.m_duration = L"D-line";
    t.m_sharers = L"S-line";
    t.m_participants = L"P-line";
    CShareEndedToast toast;
    toast.Show(L"Ended", L"Title", t);
    REQUIRE(toast.IsWindow());
    wchar_t caption[64] = { 0 };
    toast.GetWindowText(caption, 64);
    CHECK(std::wstring(caption) == std::wstring(TR(L"屏幕共享")));
    CHECK(SetLabelText(toast, CShareEndedToast::kIdHeadline) == L"Ended");
    CHECK(SetLabelText(toast, CShareEndedToast::kIdShareTitle) == L"Title");
    CHECK(SetLabelText(toast, CShareEndedToast::kIdDuration) == L"D-line");
    CHECK(SetLabelText(toast, CShareEndedToast::kIdSharers) == L"S-line");
    CHECK(SetLabelText(toast, CShareEndedToast::kIdParticipants) == L"P-line");
    REQUIRE(toast.GetClientContent() != NULL);
    CHECK(toast.GetClientContent()->FindCtrlById(CShareEndedToast::kIdOk) != NULL);
    //任何定时器消息都不关闭
    for (UINT_PTR id = 1; id < 400; id += 37)
        toast.SendMessage(WM_TIMER, id, 0);
    CHECK(toast.IsWindow());
    //点标题栏关闭才关闭
    toast.SendMessage(WM_CLOSE, 0, 0);
    CHECK_FALSE(toast.IsWindow());
}

TEST_CASE("SSET4 ended toast grows for long participant lists", "[shareended]")
{
    ShareEndedTexts shortTexts;
    shortTexts.m_duration = TRF(L"时长 {0}", FormatShareDuration(kSetDuration));
    shortTexts.m_sharers = TRF(L"分享人：{0}", L"Host");
    shortTexts.m_participants = TRF(L"参与者：{0}", L"Carol");
    CShareEndedToast shortToast;
    shortToast.Show(TR(L"屏幕共享已结束"), L"Title", shortTexts);
    REQUIRE(shortToast.IsWindow());
    const int smallH = SetWindowHeight(shortToast);
    shortToast.Hide();

    std::vector<std::wstring> many;
    for (int i = 0; i < kSetManyPeople; ++i)
        many.push_back(L"Participant" + std::to_wstring(i));
    std::vector<std::wstring> sharers;
    sharers.push_back(L"Host");
    ShareEndedTexts longTexts = FormatShareEndedTexts(kSetDuration, sharers, many, kSetManyPeople);
    CShareEndedToast longToast;
    longToast.Show(TR(L"屏幕共享已结束"), L"Title", longTexts);
    REQUIRE(longToast.IsWindow());
    const int bigH = SetWindowHeight(longToast);
    CHECK(bigH > smallH);
    RECT rcWnd;
    longToast.GetWindowRect(&rcWnd);
    const int winW = rcWnd.right - rcWnd.left;
    balloonwjui::DuiControl* content = longToast.GetClientContent();
    REQUIRE(content != NULL);
    const UINT ids[] = { CShareEndedToast::kIdParticipants, CShareEndedToast::kIdOk };
    for (size_t i = 0; i < sizeof(ids) / sizeof(ids[0]); ++i)
    {
        balloonwjui::DuiControl* c = content->FindCtrlById(ids[i]);
        REQUIRE(c != NULL);
        const RECT rc = c->GetRect();
        INFO("control " << ids[i]);
        CHECK(rc.top >= 0);
        CHECK(rc.bottom <= bigH);
        CHECK(rc.right <= winW);
        CHECK(rc.bottom > rc.top);
    }
    longToast.Hide();
}

TEST_CASE("SSET5 self is written as you", "[shareended]")
{
    CHECK(ShareEndedNameOf(kSetB, kSetB, L"Bob") == std::wstring(TR(L"你")));
    CHECK(ShareEndedNameOf(kSetC, kSetB, L"Carol") == L"Carol");
    //本人在参与者里：按原来的先后写「你」
    std::vector<std::wstring> sharers;
    sharers.push_back(ShareEndedNameOf(kSetHost, kSetB, L"Host"));
    std::vector<std::wstring> people;
    people.push_back(ShareEndedNameOf(kSetB, kSetB, L"Bob"));
    people.push_back(ShareEndedNameOf(kSetC, kSetB, L"Carol"));
    const ShareEndedTexts t = FormatShareEndedTexts(kSetDuration, sharers, people, kSetMaxNames);
    CHECK(t.m_sharers == TRF(L"分享人：{0}", L"Host"));
    CHECK(t.m_participants == TRF(L"参与者：{0}", std::wstring(TR(L"你")) + TR(L"、") + L"Carol"));
}
