/**
 *  邀请入群时「入群后可查看的聊天记录」选项（docs/邀请入群方案.md 第 2 期）的单元测试，标签 [invitehistory]，
 *  用例 IH1 ~ IH4。
 *
 *    - IH1 邀请请求体：选了 N 条或不同步时带 historylimit，选全部时不带；应答里的三项统计读得出，老服务端读不到时为 0
 *    - IH2 选项的取值规则（2026-10-04 起为三个单选项）：选「全部」为全部、选「不同步」为 0、选「最近 N 条」按输入框取
 *          1 ~ 10000，空串、非数字、0、超过上限判为不合法；三项互斥，只有选「最近」时输入框可编辑；
 *          群不允许看历史时一律按全部，选项组整组置灰
 *    - IH3 邀请应答的提示选择：全部直接入群不提示；转为审批、发出卡片各有一句，两者都有时分两行
 *    - IH4 选人对话框的布局：加了选项组之后，在最小尺寸与设计尺寸下已选成员列表、选项组与底部按钮都排得下；
 *          「最近」单选按钮按文字宽度占位、输入框排在它右边；其他用途不构建选项组
 *
 *  框架 Catch2 v3。用例里的界面文字按未加载语言包时的简体中文核对。
 *  balloonwj@qq.com   2026-10-04
 */
#include "stdafx.h"
#include "catch_amalgamated.hpp"

#include <memory>
#include <string>
#include <vector>

#include "../UI/BalloonMain/MemberPickerLayout.h"
#include "../UI/BalloonMain/MemberPickerTreeView.h"
#include "../net/GroupProtocolClient.h"
#include "../Common/I18n/I18n.h"
#include "LayoutFitCheck.h"                              // layoutfit::MeasureAt
#include "../third-party/jsoncpp-1.9.0/json.h"
#include "../../../third-party/balloonui/balloonui/Controls/Layout/DuiLayout.h"      // DuiVBox：BuildMemberPickerLayout 返回的树根
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiButton.h"
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiLabel.h"
#include "../../../third-party/balloonui/balloonui/Controls/Input/DuiEdit.h"
#include "../../../third-party/balloonui/balloonui/Controls/Window/DuiScrollBar.h"

using namespace balloonmain;

namespace {

// 设计尺寸与最小尺寸下的内容区（整窗扣掉标题栏）
const int kContentW = kPickerDlgWidth;
const int kContentH = kPickerDlgHeight - kPickerDlgTitleBarH;
const int kMinContentW = kPickerDlgMinWidth;
const int kMinContentH = kPickerDlgMinHeight - kPickerDlgTitleBarH;

// 已选成员列表在最小尺寸下至少要放得下的行数，以及每行的高度（与 CreateGroupDlg.cpp 的 kChipRowH 一致）
const int kMinVisibleChips = 2;
const int kChipRowH = 36;

// IH1 用的群与被邀请人
const int64_t kGroupId = 268435463;
const int64_t kInvitee = 5308;

// IH1 用的条数
const int kSomeLimit = 100;

// IH4：单选按钮文字之前的留白、圆点与间距之和（与 balloonui DuiButton.cpp 的 kGlyphPadPx * 2 + kGlyphSizePx 一致），
// 以及「最近」两个汉字在 9pt 默认字体下至少有的宽度（逻辑像素）
const int kRadioGlyphStripW = 28;
const int kTwoCjkCharsMinW = 16;

// 把 JSON 文本解析成对象
Json::Value ParseJson(const std::string& text)
{
    Json::Value root;
    Json::CharReaderBuilder b;
    JSONCPP_STRING errs;
    std::unique_ptr<Json::CharReader> reader(b.newCharReader());
    reader->parse(text.c_str(), text.c_str() + text.size(), &root, &errs);
    return root;
}

// 构造一次邀请请求并解析回来
Json::Value InviteBody(int historyLimit)
{
    std::vector<int64_t> ids(1, kInvitee);
    return ParseJson(net::GroupProtocolClient::buildGroupMembersOpRequest(kGroupId, ids, historyLimit));
}

// 按用途搭一棵选人对话框的控件树
std::unique_ptr<balloonwjui::DuiVBox> BuildTree(bool invite, bool allowsHistory, bool inviteConfirm,
                                                MemberPickerLayoutRefs& refs)
{
    MemberPickerLayoutSpec spec;
    spec.m_showNameInput = !invite;
    spec.m_showInviteHistory = invite;
    spec.m_groupAllowsHistory = allowsHistory;
    spec.m_inviteConfirm = inviteConfirm;
    spec.m_confirmText = invite ? L"邀请入群" : L"创建群聊";
    return BuildMemberPickerLayout(spec, refs);
}

// 某个控件在给定内容区下排得下：没有被压扁，也没有越出内容区
void CheckFits(balloonwjui::DuiControl* root, UINT ctrlId, int w, int h)
{
    const layoutfit::FitResult r = layoutfit::MeasureAt(root, ctrlId, w, h);
    INFO("ctrlId=" << ctrlId << " size=" << w << "x" << h);
    REQUIRE(r.m_found);
    CHECK(r.m_height == r.m_roomyHeight);
    CHECK(r.m_bottom <= h);
}

} // namespace

TEST_CASE("IH1 邀请请求体带同步条数，应答带三项统计", "[invitehistory]")
{
    //全部：不带 historylimit，与老客户端的请求相同
    Json::Value all = InviteBody(kInviteHistoryAll);
    CHECK(all["groupid"].asString() == "268435463");
    CHECK(all["userids"][0].asString() == "5308");
    CHECK_FALSE(all.isMember("historylimit"));

    //不同步与最近 N 条：原样带上
    CHECK(InviteBody(0)["historylimit"].asInt() == 0);
    CHECK(InviteBody(kSomeLimit)["historylimit"].asInt() == kSomeLimit);

    //移出成员与邀请共用构造器：不传条数时同样不带
    std::vector<int64_t> ids(1, kInvitee);
    CHECK_FALSE(ParseJson(net::GroupProtocolClient::buildGroupMembersOpRequest(kGroupId, ids)).isMember("historylimit"));

    //应答统计
    net::GroupOpResult r;
    net::GroupProtocolClient::parseInviteToGroupCounts("{\"code\":0,\"msg\":\"ok\",\"added\":1,\"invited\":2,\"pending\":3}", r);
    CHECK(r.m_added == 1);
    CHECK(r.m_invited == 2);
    CHECK(r.m_pending == 3);
    //老服务端只回 code / msg：三项为 0
    net::GroupOpResult old;
    old.m_added = 9;
    net::GroupProtocolClient::parseInviteToGroupCounts("{\"code\":0,\"msg\":\"ok\"}", old);
    CHECK(old.m_added == 0);
    CHECK(old.m_invited == 0);
    CHECK(old.m_pending == 0);
}

TEST_CASE("IH2 同步选项的取值规则、互斥与置灰", "[invitehistory]")
{
    int limit = 12345;
    //选「全部」：请求里不带条数，输入框里是什么都不看
    CHECK(ParseInviteHistoryLimit(true, kInviteHistoryModeAll, L"abc", limit) == kInviteHistoryOk);
    CHECK(limit == kInviteHistoryAll);
    //选「不同步」：0，输入框里是什么都不看
    CHECK(ParseInviteHistoryLimit(true, kInviteHistoryModeNone, L"abc", limit) == kInviteHistoryOk);
    CHECK(limit == 0);
    //选「最近 N 条」：按输入框取 1 ~ 10000，首尾空白忽略
    CHECK(ParseInviteHistoryLimit(true, kInviteHistoryModeRecent, L" 100 ", limit) == kInviteHistoryOk);
    CHECK(limit == 100);
    CHECK(ParseInviteHistoryLimit(true, kInviteHistoryModeRecent, L"1", limit) == kInviteHistoryOk);
    CHECK(limit == 1);
    CHECK(ParseInviteHistoryLimit(true, kInviteHistoryModeRecent, L"10000", limit) == kInviteHistoryOk);
    CHECK(limit == kInviteHistoryMax);
    //不合法：出参不变
    limit = 7;
    CHECK(ParseInviteHistoryLimit(true, kInviteHistoryModeRecent, L"", limit) == kInviteHistoryEmpty);
    CHECK(ParseInviteHistoryLimit(true, kInviteHistoryModeRecent, L"  ", limit) == kInviteHistoryEmpty);
    CHECK(ParseInviteHistoryLimit(true, kInviteHistoryModeRecent, L"12a", limit) == kInviteHistoryNotNumber);
    CHECK(ParseInviteHistoryLimit(true, kInviteHistoryModeRecent, L"-1", limit) == kInviteHistoryNotNumber);
    CHECK(ParseInviteHistoryLimit(true, kInviteHistoryModeRecent, L"0", limit) == kInviteHistoryZero);
    CHECK(ParseInviteHistoryLimit(true, kInviteHistoryModeRecent, L"000", limit) == kInviteHistoryZero);
    CHECK(ParseInviteHistoryLimit(true, kInviteHistoryModeRecent, L"10001", limit) == kInviteHistoryTooLarge);
    CHECK(ParseInviteHistoryLimit(true, kInviteHistoryModeRecent, L"99999999999", limit) == kInviteHistoryTooLarge);
    CHECK(limit == 7);
    //群不允许看历史：以群设置为准，一律按全部（请求里不带）
    CHECK(ParseInviteHistoryLimit(false, kInviteHistoryModeRecent, L"5", limit) == kInviteHistoryOk);
    CHECK(limit == kInviteHistoryAll);
    CHECK(ParseInviteHistoryLimit(false, kInviteHistoryModeNone, L"", limit) == kInviteHistoryOk);
    CHECK(limit == kInviteHistoryAll);

    //错误提示与说明文字
    CHECK(InviteHistoryErrorText(kInviteHistoryOk).IsEmpty());
    CHECK(InviteHistoryErrorText(kInviteHistoryTooLarge) == CString(L"消息条数不能超过 10000"));
    CHECK(InviteHistoryErrorText(kInviteHistoryZero) == CString(L"条数至少为 1，不想同步请选「不同步」"));
    CHECK(InviteHistoryHintText(true, false).IsEmpty());
    CHECK(InviteHistoryHintText(false, false) == CString(L"本群已设置为新成员不可查看入群前的聊天记录"));
    CHECK(InviteHistoryHintText(true, true) == CString(L"对方同意后才会加入群聊"));
    CHECK(InviteHistoryHintText(false, true)
          == CString(L"本群已设置为新成员不可查看入群前的聊天记录\n对方同意后才会加入群聊"));

    //默认选中「全部」，条数输入框不可编辑、初值 100
    MemberPickerLayoutRefs refs;
    std::unique_ptr<balloonwjui::DuiVBox> root = BuildTree(true, true, false, refs);
    const InviteHistoryControls& c = refs.m_history;
    REQUIRE(c.IsBuilt());
    REQUIRE(c.m_hint != NULL);
    CHECK(c.m_all->IsChecked());
    CHECK_FALSE(c.m_recent->IsChecked());
    CHECK_FALSE(c.m_none->IsChecked());
    CHECK(InviteHistoryModeOf(c) == kInviteHistoryModeAll);
    CHECK(c.m_all->IsEnabled());
    CHECK(c.m_recent->IsEnabled());
    CHECK(c.m_none->IsEnabled());
    CHECK_FALSE(c.m_count->IsEnabled());
    CHECK(c.m_count->GetText() == CString(L"100"));
    CHECK(c.m_recent->GetText() == CString(L"最近"));

    //选「最近 N 条」：另外两项取消，条数输入框可编辑
    SelectInviteHistoryMode(c, kInviteHistoryModeRecent, true);
    CHECK_FALSE(c.m_all->IsChecked());
    CHECK(c.m_recent->IsChecked());
    CHECK_FALSE(c.m_none->IsChecked());
    CHECK(InviteHistoryModeOf(c) == kInviteHistoryModeRecent);
    CHECK(c.m_count->IsEnabled());

    //再选「不同步」：只有它选中，条数输入框又不可编辑
    SelectInviteHistoryMode(c, kInviteHistoryModeNone, true);
    CHECK_FALSE(c.m_all->IsChecked());
    CHECK_FALSE(c.m_recent->IsChecked());
    CHECK(c.m_none->IsChecked());
    CHECK(InviteHistoryModeOf(c) == kInviteHistoryModeNone);
    CHECK_FALSE(c.m_count->IsEnabled());

    //认不出的取值按「全部」处理
    const int kUnknownMode = 9;
    SelectInviteHistoryMode(c, kUnknownMode, true);
    CHECK(InviteHistoryModeOf(c) == kInviteHistoryModeAll);

    //群不允许看历史：三项与条数输入框都不可操作
    SelectInviteHistoryMode(c, kInviteHistoryModeRecent, false);
    CHECK_FALSE(c.m_all->IsEnabled());
    CHECK_FALSE(c.m_recent->IsEnabled());
    CHECK_FALSE(c.m_none->IsEnabled());
    CHECK_FALSE(c.m_count->IsEnabled());
}

TEST_CASE("IH3 邀请应答的提示选择", "[invitehistory]")
{
    //全部直接入群：不提示
    CHECK(InviteResultTipText(0, 0).IsEmpty());
    //转为审批
    CHECK(InviteResultTipText(0, 2) == CString(L"已提交邀请，等待群主或管理员审批"));
    //发出入群邀请卡片
    CHECK(InviteResultTipText(3, 0) == CString(L"已向 3 人发出入群邀请，对方同意后加入群聊"));
    //两者都有：分两行
    CHECK(InviteResultTipText(1, 1)
          == CString(L"已向 1 人发出入群邀请，对方同意后加入群聊\n已提交邀请，等待群主或管理员审批"));
}

TEST_CASE("IH4 选人对话框加了选项组之后排得下", "[invitehistory][layout]")
{
    const int sizes[2][2] = { { kMinContentW, kMinContentH }, { kContentW, kContentH } };
    for (int s = 0; s < 2; ++s)
    {
        const int w = sizes[s][0];
        const int h = sizes[s][1];

        //说明最长的情况：群不允许看历史，且开启了同意开关，说明占两行
        MemberPickerLayoutRefs refs;
        std::unique_ptr<balloonwjui::DuiVBox> root = BuildTree(true, false, true, refs);
        CheckFits(root.get(), kPickerIdConfirm, w, h);
        CheckFits(root.get(), kPickerIdHistoryAll, w, h);
        CheckFits(root.get(), kPickerIdHistoryRecent, w, h);
        CheckFits(root.get(), kPickerIdHistoryCount, w, h);
        CheckFits(root.get(), kPickerIdHistoryNone, w, h);
        CheckFits(root.get(), kPickerIdHistoryHint, w, h);

        //底部按钮的下方留白与空间充裕时相同：没有占用下内边距
        const layoutfit::FitResult confirm = layoutfit::MeasureAt(root.get(), kPickerIdConfirm, w, h);
        CHECK(confirm.m_bottomGap == confirm.m_roomyBottomGap);

        //已选成员列表在最小尺寸下也至少放得下两行
        const layoutfit::FitResult chips = layoutfit::MeasureAt(root.get(), kPickerIdChipScroll, w, h);
        REQUIRE(chips.m_found);
        CHECK(chips.m_height >= kMinVisibleChips * kChipRowH);

        //选项组排在已选成员列表下方，不与它重叠
        const RECT rcChips = root->FindCtrlById(kPickerIdChipScroll)->GetRect();
        const RECT rcTitle = root->FindCtrlById(kPickerIdHistoryTitle)->GetRect();
        CHECK(rcTitle.top >= rcChips.bottom);

        //三个单选项自上而下：全部、最近 N 条、不同步
        RECT whole = { 0, 0, w, h };
        root->ForceLayout(whole);
        const RECT rcAll = root->FindCtrlById(kPickerIdHistoryAll)->GetRect();
        const RECT rcRecent = root->FindCtrlById(kPickerIdHistoryRecent)->GetRect();
        const RECT rcNone = root->FindCtrlById(kPickerIdHistoryNone)->GetRect();
        const RECT rcCount = root->FindCtrlById(kPickerIdHistoryCount)->GetRect();
        CHECK(rcAll.bottom <= rcRecent.top);
        CHECK(rcRecent.bottom <= rcNone.top);
        //「最近」单选按钮按文字宽度占位（2026-10-04 实测曾因 Auto() 宽度为 0 而文字整段不显示），条数输入框在它右边、不重叠
        CHECK(rcRecent.right - rcRecent.left == InviteRecentRadioWidth());
        CHECK(InviteRecentRadioWidth() >= kRadioGlyphStripW + kTwoCjkCharsMinW);
        CHECK(rcCount.left >= rcRecent.right);
        CHECK(rcCount.top >= rcRecent.top);
        CHECK(rcCount.bottom <= rcRecent.bottom);
    }

    //其他用途（新建群聊）不构建选项组，群名输入框照旧
    MemberPickerLayoutRefs createRefs;
    std::unique_ptr<balloonwjui::DuiVBox> createRoot = BuildTree(false, true, false, createRefs);
    CHECK_FALSE(createRefs.m_history.IsBuilt());
    CHECK(createRefs.m_history.m_hint == NULL);
    CHECK(createRefs.m_nameInput != NULL);
    CHECK(createRoot->FindCtrlById(kPickerIdHistoryAll) == NULL);
    CHECK(createRoot->FindCtrlById(kPickerIdHistoryRecent) == NULL);
    CheckFits(createRoot.get(), kPickerIdConfirm, kMinContentW, kMinContentH);
}
