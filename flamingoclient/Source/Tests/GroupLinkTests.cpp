/**
 *  加群链接客户端的单测（docs/邀请入群方案.md §12），GroupLinkTests.cpp
 *
 *  [grouplink]
 *    GL-C1 协议：1340~1344 的请求与应答；链接文本的识别与随机码的取出
 *    GL-C2 流程：查到群后的下一步、确认与失败提示、有效期选项与到期说明、命令行里找链接、协议注册命令
 *    GL-C3 入口：聊天消息里识别加群链接；群列表、会话列表右键菜单的「生成加群链接」
 *    GL-C4 「生成加群链接」窗口按设计尺寸排得下；群管理窗「加群链接」页的可见性与数据映射
 *    GL-C5 生成窗口「我生成的链接」：只留本人生成的、保持服务端顺序、行文字、加载状态与占位文字；作废结果的提示
 *    GL-C6 1341 应答按群号分给生成窗口与群管理窗；1342 应答靠作废登记区分归属
 *    GL-C7 生成窗口「我生成的链接」的布局：排得下、能滚动，先排过版再增减行之后新加的行点得中
 *
 *  框架 Catch2 v3。全局 _Module 由 BalloonChatLayoutTests.cpp 统一定义。
 *  balloonwj@qq.com   2026-10-07
 */
#include "stdafx.h"
#include "catch_amalgamated.hpp"

#include <memory>
#include <string>
#include <vector>

#include "../net/GroupLinkProtocolClient.h"
#include "../net/Protocol.h"
#include "../UI/BalloonMain/GroupLinkFlow.h"
#include "../UI/BalloonMain/GroupLinkLayout.h"
#include "../UI/BalloonMain/MainPanelGroupMenu.h"
#include "../UI/BalloonMain/MainPanelConversationMenu.h"
#include "../UI/BalloonChatWnd/ChatLinkDetect.h"
#include "../UI/BalloonGroupManage/GroupManageMapping.h"
#include "LayoutFitCheck.h"
#include "../../../third-party/balloonui/balloonui/DuiControl.h"
#include "../../../third-party/balloonui/balloonui/Controls/Window/DuiScrollBar.h"   // DuiScrollView

namespace {

// 一个合规的随机码（12 位，去掉易混字符的小写字母与数字）
const char* const kCode = "abcdefghjkmn";
const wchar_t* const kUrl = L"flamingo://joingroup?code=abcdefghjkmn";

} // namespace

TEST_CASE("GL-C1 协议：请求、应答与链接文本", "[grouplink]")
{
    CHECK(net::GroupLinkProtocolClient::buildCreateRequest(268435457LL, 7)
          == std::string("{\"groupid\":\"268435457\",\"validdays\":7}"));
    CHECK(net::GroupLinkProtocolClient::buildRevokeRequest(268435457LL, 3)
          == std::string("{\"groupid\":\"268435457\",\"id\":\"3\"}"));
    CHECK(net::GroupLinkProtocolClient::buildCodeRequest(kCode) == std::string("{\"code\":\"abcdefghjkmn\"}"));

    net::CreateGroupLinkResult created;
    REQUIRE(net::GroupLinkProtocolClient::parseCreateResponse(
        "{\"code\":0,\"msg\":\"ok\",\"link\":{\"id\":\"3\",\"code\":\"abcdefghjkmn\",\"url\":\"flamingo://joingroup?code=abcdefghjkmn\","
        "\"groupid\":\"268435457\",\"creator\":\"100001\",\"creatorname\":\"张三\",\"createtime\":1700000000000,\"expiretime\":0}}",
        created));
    CHECK(created.m_hasLink);
    CHECK(created.m_link.m_id == 3);
    CHECK(created.m_link.m_expiretime == 0);
    net::CreateGroupLinkResult denied;
    REQUIRE(net::GroupLinkProtocolClient::parseCreateResponse("{\"code\":6,\"msg\":\"forbid\"}", denied));
    CHECK(denied.m_code == 6);
    CHECK_FALSE(denied.m_hasLink);

    net::GroupLinksResult list;
    REQUIRE(net::GroupLinkProtocolClient::parseListResponse(
        "{\"code\":0,\"msg\":\"ok\",\"groupid\":\"268435457\",\"links\":[{\"id\":\"3\",\"code\":\"abcdefghjkmn\"},{\"id\":\"0\"}]}",
        list));
    CHECK(list.m_groupid == 268435457LL);
    CHECK(list.m_links.size() == 1);

    net::GroupLinkInfoResult info;
    REQUIRE(net::GroupLinkProtocolClient::parseInfoResponse(
        "{\"code\":0,\"msg\":\"ok\",\"groupid\":\"268435457\",\"name\":\"设计组\",\"membercount\":12,\"joinmode\":1,\"member\":false}",
        info));
    CHECK(info.m_joinmode == 1);
    CHECK(info.m_membercount == 12);
    CHECK_FALSE(info.m_member);
    net::GroupLinkInfoResult invalid;
    REQUIRE(net::GroupLinkProtocolClient::parseInfoResponse("{\"code\":316,\"msg\":\"link not usable\"}", invalid));
    CHECK(invalid.m_code == 316);

    net::JoinGroupByLinkResult join;
    REQUIRE(net::GroupLinkProtocolClient::parseJoinResponse(
        "{\"code\":0,\"msg\":\"ok\",\"groupid\":\"268435457\",\"joined\":true}", join));
    CHECK(join.m_joined);

    //链接文本：大小写、首尾引号与空白、末尾标点、多余参数
    std::string code;
    CHECK(net::GroupLinkProtocolClient::parseJoinUrl(kUrl, code));
    CHECK(code == kCode);
    CHECK(net::GroupLinkProtocolClient::parseJoinUrl(L"\"FLAMINGO://JoinGroup?Code=ABCDEFGHJKMN\"", code));
    CHECK(code == kCode);
    CHECK(net::GroupLinkProtocolClient::parseJoinUrl(L"flamingo://joingroup/?x=1&code=abcdefghjkmn&y=2", code));
    CHECK(net::GroupLinkProtocolClient::parseJoinUrl(L"flamingo://joingroup?code=abcdefghjkmn。", code));
    CHECK_FALSE(net::GroupLinkProtocolClient::parseJoinUrl(L"flamingo://joingroup?code=abc", code));
    CHECK_FALSE(net::GroupLinkProtocolClient::parseJoinUrl(L"flamingo://joingroupx?code=abcdefghjkmn", code));
    CHECK_FALSE(net::GroupLinkProtocolClient::parseJoinUrl(L"flamingo://screenshare/ABCDEFGHJK", code));
    CHECK_FALSE(net::GroupLinkProtocolClient::parseJoinUrl(L"https://joingroup?code=abcdefghjkmn", code));
    CHECK(net::GroupLinkProtocolClient::isJoinUrl(kUrl));
    CHECK_FALSE(net::GroupLinkProtocolClient::isJoinUrl(L"flamingo://screenshare/x"));
}

TEST_CASE("GL-C2 流程：下一步、提示文字、有效期、命令行与协议注册", "[grouplink]")
{
    using namespace balloonmain;
    CHECK(DecideLinkInfoAction(0, true, 2) == kLinkActionOpenGroup);
    CHECK(DecideLinkInfoAction(0, false, 0) == kLinkActionConfirmJoin);
    CHECK(DecideLinkInfoAction(0, false, 1) == kLinkActionConfirmJoin);
    CHECK(DecideLinkInfoAction(0, false, 2) == kLinkActionForbidden);
    CHECK(DecideLinkInfoAction(error_code_group_link_invalid, false, 0) == kLinkActionError);

    //确认文字带群名与人数；自由加入与需审批的说法不同
    const std::wstring free = JoinLinkConfirmText(L"设计组", 12, 0);
    const std::wstring approve = JoinLinkConfirmText(L"设计组", 12, 1);
    CHECK(free.find(L"设计组") != std::wstring::npos);
    CHECK(free.find(L"12") != std::wstring::npos);
    CHECK(free != approve);

    //失败提示各不相同
    const std::wstring invalid = GroupLinkErrorText(error_code_group_link_invalid);
    const std::wstring enterprise = GroupLinkErrorText(error_code_group_link_enterprise);
    const std::wstring forbid = GroupLinkErrorText(error_code_permission_denied);
    CHECK(invalid != enterprise);
    CHECK(enterprise != forbid);
    CHECK(GroupLinkErrorText(9999).find(L"9999") != std::wstring::npos);
    CHECK_FALSE(JoinLinkPendingText().empty());

    //有效期：三项依次是 1 天、7 天、永久，默认 7 天，越界按 7 天
    CHECK(GroupLinkDaysByChoice(0) == net::kGroupLinkDaysOne);
    CHECK(GroupLinkDaysByChoice(1) == net::kGroupLinkDaysSeven);
    CHECK(GroupLinkDaysByChoice(2) == net::kGroupLinkDaysForever);
    CHECK(GroupLinkDaysByChoice(kGroupLinkValidityDefaultIndex) == net::kGroupLinkDaysSeven);
    CHECK(GroupLinkDaysByChoice(9) == net::kGroupLinkDaysSeven);
    for (int i = 0; i < kGroupLinkValidityChoiceCount; ++i)
    {
        CHECK_FALSE(GroupLinkChoiceText(i).empty());
    }
    CHECK(GroupLinkChoiceText(kGroupLinkValidityChoiceCount).empty());
    CHECK(GroupLinkExpireText(0) == GroupLinkChoiceText(2));
    CHECK(GroupLinkExpireText(1700000000000LL) != GroupLinkExpireText(0));

    //命令行：系统按「"exe" "%1"」启动时链接带引号；没有链接时为空
    CHECK(FindJoinUrlInCommandLine(L"\"C:\\Flamingo\\Flamingo.exe\" \"flamingo://joingroup?code=abcdefghjkmn\"") == kUrl);
    CHECK(FindJoinUrlInCommandLine(L"Flamingo.exe flamingo://joingroup?code=abcdefghjkmn --x") == kUrl);
    CHECK(FindJoinUrlInCommandLine(L"\"C:\\Flamingo\\Flamingo.exe\"").empty());
    CHECK(FindJoinUrlInCommandLine(L"Flamingo.exe --mainpanel").empty());
    CHECK(BuildProtocolOpenCommand(L"C:\\Flamingo\\Flamingo.exe") == L"\"C:\\Flamingo\\Flamingo.exe\" \"%1\"");
}

TEST_CASE("GL-C3 入口：消息里的加群链接与右键菜单", "[grouplink]")
{
    //聊天消息里识别加群链接（站内链接，不受「识别链接」开关影响）
    const std::wstring text = L"进群点这里：flamingo://joingroup?code=abcdefghjkmn，过期再找我";
    std::vector<balloonchatwnd::LinkSpan> spans = balloonchatwnd::DetectLinks(text, false, true);
    REQUIRE(spans.size() == 1);
    CHECK(spans[0].m_scheme == balloonchatwnd::kLinkSchemeJoinGroup);
    CHECK(spans[0].m_url == kUrl);
    std::string code;
    CHECK(balloonchatwnd::IsJoinGroupLinkUrl(spans[0].m_url, &code));
    CHECK(code == kCode);
    //随机码不合规的不识别；关掉站内链接时也不识别
    CHECK(balloonchatwnd::DetectLinks(L"flamingo://joingroup?code=abc", false, true).empty());
    CHECK(balloonchatwnd::DetectLinks(text, true, false).empty());
    //加群链接不是可以交给浏览器的网址
    CHECK_FALSE(balloonchatwnd::IsSafeLinkUrl(kUrl));

    //群列表右键菜单：打开开关后多出「生成加群链接」，默认没有
    balloonmain::GroupMenuState gs;
    std::vector<balloonmain::GroupMenuEntry> rows = balloonmain::BuildGroupMenuModel(gs);
    for (size_t i = 0; i < rows.size(); ++i)
    {
        CHECK(rows[i].m_id != (UINT)balloonmain::kGroupMenuJoinLink);
    }
    gs.m_canCreateLink = true;
    rows = balloonmain::BuildGroupMenuModel(gs);
    bool hasGroupItem = false;
    for (size_t i = 0; i < rows.size(); ++i)
    {
        if (!rows[i].m_isSeparator && rows[i].m_id == balloonmain::kGroupMenuJoinLink)
            hasGroupItem = true;
    }
    CHECK(hasGroupItem);

    //会话列表右键菜单：只有群会话有
    balloonmain::ConversationMenuState cs;
    cs.m_canCreateLink = true;
    cs.m_isGroup = true;
    std::vector<balloonmain::ConversationMenuEntry> convRows = balloonmain::BuildConversationMenuModel(cs);
    bool hasConvItem = false;
    for (size_t i = 0; i < convRows.size(); ++i)
    {
        if (!convRows[i].m_isSeparator && convRows[i].m_id == balloonmain::kConvMenuJoinLink)
            hasConvItem = true;
    }
    CHECK(hasConvItem);
    cs.m_isGroup = false;
    convRows = balloonmain::BuildConversationMenuModel(cs);
    for (size_t i = 0; i < convRows.size(); ++i)
    {
        CHECK(convRows[i].m_id != (UINT)balloonmain::kConvMenuJoinLink);
    }
}

TEST_CASE("GL-C4 「生成加群链接」窗口排得下；群管理窗「加群链接」页", "[grouplink][layoutfit]")
{
    std::unique_ptr<balloonwjui::DuiControl> root = balloonmain::BuildGroupLinkBody();
    REQUIRE(root != nullptr);
    const int contentH = balloonmain::kGroupLinkWindowHeight - balloonmain::kGroupLinkTitleBarH;
    const UINT ids[] = { balloonmain::kGroupLinkIdCopy, balloonmain::kGroupLinkIdClose, balloonmain::kGroupLinkIdCreate,
                         balloonmain::kGroupLinkIdStatus, balloonmain::kGroupLinkIdUrl };
    for (size_t i = 0; i < sizeof(ids) / sizeof(ids[0]); ++i)
    {
        INFO("control id = " << ids[i]);
        const layoutfit::FitResult r = layoutfit::MeasureAt(root.get(), ids[i], balloonmain::kGroupLinkWindowWidth, contentH);
        REQUIRE(r.m_found);
        CHECK(r.m_bottom <= contentH);
        CHECK(r.m_height == r.m_roomyHeight);
    }

    //「加群链接」页只给群主与管理员
    using namespace balloongroupmanage;
    const int kPageLinksId = 1009;
    CHECK(IsPageVisibleForRole(kPageLinksId, kRoleOwner));
    CHECK(IsPageVisibleForRole(kPageLinksId, kRoleAdmin));
    CHECK_FALSE(IsPageVisibleForRole(kPageLinksId, kRoleMember));

    //列表映射：成功时替换并置为已加载；失败时列表不变
    const int64_t kNow = 1700000000000LL;
    net::GroupLinksResult result;
    result.m_code = 0;
    result.m_groupid = 268435457LL;
    net::GroupLinkItem link;
    link.m_id = 3;
    link.m_url = "flamingo://joingroup?code=abcdefghjkmn";
    link.m_creatorname = "张三";
    link.m_createtime = kNow;
    link.m_expiretime = 0;
    result.m_links.push_back(link);
    GroupManageModel model;
    CHECK(model.m_linksState == kLoadPending);
    ApplyGroupLinks(result, kNow, model);
    REQUIRE(model.m_links.size() == 1);
    CHECK(model.m_linksState == kLoadDone);
    CHECK(model.m_links[0].url == CString(kUrl));
    CHECK(model.m_links[0].expireText == CString(balloonmain::GroupLinkExpireText(0).c_str()));
    net::GroupLinksResult failed;
    failed.m_code = 1;
    ApplyGroupLinks(failed, kNow, model);
    CHECK(model.m_links.size() == 1);
}

namespace {

// 造一条 1341 应答里的链接
net::GroupLinkItem MakeLinkItem(int64_t id, int64_t creator, int64_t createTimeMs, int64_t expireTimeMs)
{
    net::GroupLinkItem item;
    item.m_id = id;
    item.m_groupid = 268435457LL;
    item.m_creator = creator;
    item.m_url = "flamingo://joingroup?code=abcdefghjkmn";
    item.m_createtime = createTimeMs;
    item.m_expiretime = expireTimeMs;
    return item;
}

} // namespace

TEST_CASE("GL-C5 生成窗口的「我生成的链接」与作废结果的提示", "[grouplink]")
{
    using namespace balloonmain;
    const int64_t kGroup = 268435457LL;
    const int64_t kSelf = 100001;
    const int64_t kOther = 100002;
    const int64_t kCreate = 1700000000000LL;
    const int64_t kExpire = 1700604800000LL;

    //刚打开窗口：正在加载
    MyGroupLinkList list;
    CHECK(list.m_state == kMyLinksLoading);
    CHECK_FALSE(MyGroupLinksPlaceholderText(list).empty());

    //群主、管理员收到本群全部链接：只留本人生成的，保持服务端给出的顺序（生成时间倒序）
    net::GroupLinksResult all;
    all.m_code = 0;
    all.m_groupid = kGroup;
    all.m_links.push_back(MakeLinkItem(9, kSelf, kCreate + 2000, 0));
    all.m_links.push_back(MakeLinkItem(8, kOther, kCreate + 1000, kExpire));
    all.m_links.push_back(MakeLinkItem(7, kSelf, kCreate, kExpire));
    REQUIRE(ApplyMyGroupLinks(all, kGroup, kSelf, list));
    CHECK(list.m_state == kMyLinksLoaded);
    REQUIRE(list.m_rows.size() == 2);
    CHECK(list.m_rows[0].m_id == 9);
    CHECK(list.m_rows[1].m_id == 7);
    CHECK(list.m_rows[0].m_url == kUrl);
    CHECK(MyGroupLinksPlaceholderText(list).empty());

    //行文字：带到期说明；永久与有期限的说法不同，生成时间不同则文字不同
    CHECK(list.m_rows[0].m_metaText.find(GroupLinkExpireText(0)) != std::wstring::npos);
    CHECK(list.m_rows[1].m_metaText.find(GroupLinkExpireText(kExpire)) != std::wstring::npos);
    CHECK(list.m_rows[0].m_metaText != list.m_rows[1].m_metaText);

    //别的群的应答不改列表
    net::GroupLinksResult otherGroup = all;
    otherGroup.m_groupid = kGroup + 1;
    otherGroup.m_links.clear();
    CHECK_FALSE(ApplyMyGroupLinks(otherGroup, kGroup, kSelf, list));
    CHECK(list.m_rows.size() == 2);

    //加载成功过之后再失败：保留原内容
    net::GroupLinksResult failed;
    failed.m_code = error_code_server_internal;
    CHECK(ApplyMyGroupLinks(failed, kGroup, kSelf, list));
    CHECK(list.m_state == kMyLinksLoaded);
    CHECK(list.m_rows.size() == 2);

    //从没加载成功过就失败：显示「没有加载出来」，与「正在加载」不同
    MyGroupLinkList fresh;
    const std::wstring loadingText = MyGroupLinksPlaceholderText(fresh);
    CHECK(ApplyMyGroupLinks(failed, kGroup, kSelf, fresh));
    CHECK(fresh.m_state == kMyLinksFailed);
    CHECK_FALSE(MyGroupLinksPlaceholderText(fresh).empty());
    CHECK(MyGroupLinksPlaceholderText(fresh) != loadingText);

    //全是别人生成的（或本人一条都没有）：已加载、列表为空，显示「还没有」
    net::GroupLinksResult othersOnly;
    othersOnly.m_code = 0;
    othersOnly.m_groupid = kGroup;
    othersOnly.m_links.push_back(MakeLinkItem(8, kOther, kCreate, kExpire));
    REQUIRE(ApplyMyGroupLinks(othersOnly, kGroup, kSelf, list));
    CHECK(list.m_state == kMyLinksLoaded);
    CHECK(list.m_rows.empty());
    const std::wstring emptyText = MyGroupLinksPlaceholderText(list);
    CHECK_FALSE(emptyText.empty());
    CHECK(emptyText != loadingText);

    //超过上限的截掉
    net::GroupLinksResult many;
    many.m_code = 0;
    many.m_groupid = kGroup;
    for (int i = 0; i < kMyGroupLinkRowMax + 5; ++i)
    {
        many.m_links.push_back(MakeLinkItem(1000 - i, kSelf, kCreate - i, 0));
    }
    REQUIRE(ApplyMyGroupLinks(many, kGroup, kSelf, list));
    CHECK(list.m_rows.size() == (size_t)kMyGroupLinkRowMax);

    //作废结果：成功、不是本人的、已失效、其它错误各不相同；其它错误带上原因
    const std::wstring ok = RevokeResultStatusText(error_code_ok);
    const std::wstring denied = RevokeResultStatusText(error_code_permission_denied);
    const std::wstring invalid = RevokeResultStatusText(error_code_group_link_invalid);
    const std::wstring notMember = RevokeResultStatusText(error_code_not_group_member);
    CHECK_FALSE(ok.empty());
    CHECK(ok != denied);
    CHECK(denied != invalid);
    CHECK(invalid != notMember);
    //6 在作废时表示「不是本人生成的」，不能沿用生成链接时「群禁止加入」的说法
    CHECK(denied != GroupLinkErrorText(error_code_permission_denied));
    CHECK(notMember.find(GroupLinkErrorText(error_code_not_group_member)) != std::wstring::npos);
    CHECK(RevokeResultStatusText(9999).find(L"9999") != std::wstring::npos);
}

TEST_CASE("GL-C6 1341 / 1342 应答的去向", "[grouplink]")
{
    using namespace balloonmain;
    const int64_t kGroup = 268435457LL;

    //1341：按群号分别匹配；两边看同一个群时都给，都没开时都不给
    GroupLinksDelivery d = DecideGroupLinksDelivery(kGroup, kGroup, 0);
    CHECK(d.m_toDialog);
    CHECK_FALSE(d.m_toManageWindow);
    d = DecideGroupLinksDelivery(kGroup, 0, kGroup);
    CHECK_FALSE(d.m_toDialog);
    CHECK(d.m_toManageWindow);
    d = DecideGroupLinksDelivery(kGroup, kGroup, kGroup);
    CHECK(d.m_toDialog);
    CHECK(d.m_toManageWindow);
    d = DecideGroupLinksDelivery(kGroup, kGroup + 1, kGroup + 2);
    CHECK_FALSE(d.m_toDialog);
    CHECK_FALSE(d.m_toManageWindow);
    //应答不带群号时谁都不给
    d = DecideGroupLinksDelivery(0, 0, 0);
    CHECK_FALSE(d.m_toDialog);
    CHECK_FALSE(d.m_toManageWindow);

    //1342：生成窗口登记过作废时，第一条应答归它，并清掉登记
    GroupLinkRevokeTracker tracker;
    CHECK_FALSE(tracker.IsWaiting());
    CHECK_FALSE(tracker.TakeResult());
    tracker.Begin(7);
    CHECK(tracker.IsWaiting());
    CHECK(tracker.LinkId() == 7);
    CHECK(tracker.TakeResult());
    CHECK_FALSE(tracker.IsWaiting());
    //没在等时的应答照旧归群管理窗
    CHECK_FALSE(tracker.TakeResult());

    //撤销登记之后到达的应答归群管理窗
    tracker.Begin(8);
    tracker.Cancel();
    CHECK_FALSE(tracker.IsWaiting());
    CHECK_FALSE(tracker.TakeResult());

    //链接ID 不合法时不登记
    tracker.Begin(0);
    CHECK_FALSE(tracker.IsWaiting());
}

namespace {

// 造有 rowCount 行的「我生成的链接」列表（已加载）
balloonmain::MyGroupLinkList MakeLoadedList(int rowCount)
{
    balloonmain::MyGroupLinkList list;
    list.m_state = balloonmain::kMyLinksLoaded;
    for (int i = 0; i < rowCount; ++i)
    {
        balloonmain::MyGroupLinkRow row;
        row.m_id = 100 + i;
        row.m_url = kUrl;
        row.m_metaText = L"10-07 14:32 生成 · 永久有效";
        list.m_rows.push_back(row);
    }
    return list;
}

// 控件矩形的中心点
POINT CenterOf(const RECT& rc)
{
    const POINT pt = { (rc.left + rc.right) / 2, (rc.top + rc.bottom) / 2 };
    return pt;
}

// 检查第 index 行的「复制」「作废」都在列表的可见区域内、并且点得中
void CheckRowClickable(balloonwjui::DuiControl* root, int index, const RECT& viewRc)
{
    const UINT ids[] = { (UINT)(balloonmain::kGroupLinkIdRowCopyBase + index),
                         (UINT)(balloonmain::kGroupLinkIdRowRevokeBase + index) };
    for (size_t i = 0; i < sizeof(ids) / sizeof(ids[0]); ++i)
    {
        INFO("row = " << index << ", control id = " << ids[i]);
        balloonwjui::DuiControl* button = root->FindCtrlById(ids[i]);
        REQUIRE(button != nullptr);
        const RECT rc = button->GetRect();
        CHECK(rc.right > rc.left);
        CHECK(rc.top >= viewRc.top);
        CHECK(rc.bottom <= viewRc.bottom);
        CHECK(root->HitTest(CenterOf(rc)) == button);
    }
}

} // namespace

TEST_CASE("GL-C7 「我生成的链接」：排得下、能滚动、增减行之后点得中", "[grouplink][layoutfit]")
{
    using namespace balloonmain;
    std::unique_ptr<balloonwjui::DuiControl> root = BuildGroupLinkBody();
    REQUIRE(root != nullptr);
    const int width = kGroupLinkWindowWidth;
    const int contentH = kGroupLinkWindowHeight - kGroupLinkTitleBarH;

    //标题没有被压扁；列表区域至少能完整显示 kGroupLinkMinVisibleRows 行，并且在按钮行之上
    const layoutfit::FitResult title = layoutfit::MeasureAt(root.get(), kGroupLinkIdMyLinksTitle, width, contentH);
    REQUIRE(title.m_found);
    CHECK(title.m_height == title.m_roomyHeight);
    const layoutfit::FitResult list = layoutfit::MeasureAt(root.get(), kGroupLinkIdMyLinksScroll, width, contentH);
    REQUIRE(list.m_found);
    CHECK(list.m_height >= MyGroupLinkListContentHeight((size_t)kGroupLinkMinVisibleRows));
    balloonwjui::DuiControl* scrollCtrl = root->FindCtrlById(kGroupLinkIdMyLinksScroll);
    balloonwjui::DuiControl* buttonRow = root->FindCtrlById(kGroupLinkIdButtonRow);
    REQUIRE(scrollCtrl != nullptr);
    REQUIRE(buttonRow != nullptr);
    const RECT viewRc = scrollCtrl->GetRect();
    const RECT buttonRowRc = buttonRow->GetRect();
    CHECK(viewRc.bottom <= buttonRowRc.top);
    balloonwjui::DuiScrollView* scroll = dynamic_cast<balloonwjui::DuiScrollView*>(scrollCtrl);
    REQUIRE(scroll != nullptr);

    //刚建好：显示「正在加载…」
    balloonwjui::DuiControl* placeholder = root->FindCtrlById(kGroupLinkIdMyLinksPlaceholder);
    REQUIRE(placeholder != nullptr);
    CHECK(root->FindCtrlById(kGroupLinkIdRowRevokeBase) == nullptr);

    //先排过版、再填 3 行：占位文字消失，每行的按钮都点得中；按钮行不动
    FillMyGroupLinkList(root.get(), MakeLoadedList(3));
    CHECK(root->FindCtrlById(kGroupLinkIdMyLinksPlaceholder) == nullptr);
    for (int i = 0; i < 3; ++i)
    {
        CheckRowClickable(root.get(), i, viewRc);
    }
    CHECK(::EqualRect(&buttonRow->GetRect(), &buttonRowRc));

    //20 行：内容比列表区域高，可以滚动；第一行点得中，滚到底后最后一行点得中；按钮行不动
    const int kManyRows = 20;
    FillMyGroupLinkList(root.get(), MakeLoadedList(kManyRows));
    CHECK(scroll->GetContentHeight() == MyGroupLinkListContentHeight((size_t)kManyRows));
    CHECK(scroll->GetContentHeight() > viewRc.bottom - viewRc.top);
    CheckRowClickable(root.get(), 0, viewRc);
    scroll->SetScrollPos(scroll->GetContentHeight());
    CHECK(scroll->GetScrollPos() > 0);
    CheckRowClickable(root.get(), kManyRows - 1, viewRc);
    CHECK(::EqualRect(&buttonRow->GetRect(), &buttonRowRc));

    //减到 1 行：第 2 行的按钮不再存在，第 1 行点得中
    FillMyGroupLinkList(root.get(), MakeLoadedList(1));
    CHECK(root->FindCtrlById(kGroupLinkIdRowRevokeBase + 1) == nullptr);
    CheckRowClickable(root.get(), 0, viewRc);

    //全部作废之后：显示「还没有」占位文字
    FillMyGroupLinkList(root.get(), MakeLoadedList(0));
    CHECK(root->FindCtrlById(kGroupLinkIdRowRevokeBase) == nullptr);
    CHECK(root->FindCtrlById(kGroupLinkIdMyLinksPlaceholder) != nullptr);
}
