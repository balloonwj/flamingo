/**
 *  帖子群 exe 一侧建群窗口的单元测试（标签 [threadgroup]），对应 docs/帖子群方案.md 第 8 节：
 *    - TGE6 建群窗口：帖子群功能可用时在群名下方构建「群类型：普通群 / 帖子群」，默认普通群；不可用时整组不构建、
 *           一律建普通群；选中帖子群后请求带 "grouptype": 1，普通群的请求与原来完全相同；加了这组之后设计尺寸与
 *           最小尺寸下都排得下；本企业关闭帖子群（1300）时的失败提示。
 *    - CGB1 建群确认按钮（2026-10-08 建大群时界面像卡住了）：请求发出后按钮不可点、文字为「正在创建…」，
 *           恢复后回到原文字并可点；按钮还没建出来（NULL）时什么也不做。标签 [creategroup]。
 *  框架 Catch2 v3。用例里的界面文字按未加载语言包时的简体中文核对。
 *  balloonwj@qq.com   2026-10-07
 */
#include "stdafx.h"
#include "catch_amalgamated.hpp"

#include <memory>
#include <string>
#include <vector>

#include "../Feature/ThreadGroup/ThreadGroupGate.h"
#include "../UI/BalloonMain/MemberPickerLayout.h"
#include "../UI/BalloonMain/MemberPickerTreeView.h"
#include "../UI/BalloonMain/SensitiveWordTips.h"
#include "../net/GroupProtocolClient.h"
#include "../net/Protocol.h"
#include "LayoutFitCheck.h"                              // layoutfit::MeasureAt
#include "../third-party/jsoncpp-1.9.0/json.h"
#include "../../../third-party/balloonui/balloonui/Controls/Layout/DuiLayout.h"
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiButton.h"
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiLabel.h"
#include "../../../third-party/balloonui/balloonui/Controls/Input/DuiEdit.h"
#include "../../../third-party/balloonui/balloonui/Controls/Window/DuiScrollBar.h"

using namespace balloonmain;

namespace {

//设计尺寸与最小尺寸下的内容区（整窗扣掉标题栏）
const int kContentW = kPickerDlgWidth;
const int kContentH = kPickerDlgHeight - kPickerDlgTitleBarH;
const int kMinContentW = kPickerDlgMinWidth;
const int kMinContentH = kPickerDlgMinHeight - kPickerDlgTitleBarH;

//已选成员列表在最小尺寸下至少要放得下的行数与每行高度（与 InviteHistoryTests 的 IH4 一致）
const int kMinVisibleChips = 2;
const int kChipRowH = 36;

//建群请求用的成员
const int64_t kMemberA = 10002;
const int64_t kMemberB = 10003;

//不认识的群类型取值
const int kUnknownGroupType = 7;

//按新建群聊用途搭一棵控件树
std::unique_ptr<balloonwjui::DuiVBox> BuildCreateTree(bool showGroupType, MemberPickerLayoutRefs& refs)
{
    MemberPickerLayoutSpec spec;
    spec.m_showNameInput = true;
    spec.m_showGroupType = showGroupType;
    spec.m_confirmText = _T("创建群聊");
    return BuildMemberPickerLayout(spec, refs);
}

//解析请求体
Json::Value ParseBody(const std::string& body)
{
    Json::Value root;
    Json::CharReaderBuilder builder;
    std::unique_ptr<Json::CharReader> reader(builder.newCharReader());
    std::string errs;
    reader->parse(body.data(), body.data() + body.size(), &root, &errs);
    return root;
}

//某个控件按给定尺寸排版后没有被压扁、没有越过内容区下缘
void CheckFits(balloonwjui::DuiControl* root, UINT id, int w, int h)
{
    INFO("ctrl id = " << id << ", size = " << w << "x" << h);
    const layoutfit::FitResult r = layoutfit::MeasureAt(root, id, w, h);
    REQUIRE(r.m_found);
    CHECK(r.m_bottom <= h);
    CHECK(r.m_height == r.m_roomyHeight);
}

} // namespace

TEST_CASE("TGE6 建群窗口：群类型选项组的显隐、请求带 grouptype、排得下", "[threadgroup]")
{
    SECTION("建群请求：只有帖子群带 grouptype，普通群与原来相同")
    {
        std::vector<int64_t> members;
        members.push_back(kMemberA);
        members.push_back(kMemberB);
        const std::string normal = net::GroupProtocolClient::buildCreateGroupRequest("g", members);
        CHECK(normal.find("grouptype") == std::string::npos);
        CHECK(net::GroupProtocolClient::buildCreateGroupRequest("g", members, group_type_normal) == normal);
        CHECK(net::GroupProtocolClient::buildCreateGroupRequest("g", members, kUnknownGroupType) == normal);

        const Json::Value thread = ParseBody(net::GroupProtocolClient::buildCreateGroupRequest("g", members, group_type_thread));
        REQUIRE(thread.isMember("grouptype"));
        CHECK(thread["grouptype"].asInt() == group_type_thread);
        CHECK(thread["groupname"].asString() == "g");
        REQUIRE(thread["memberids"].size() == members.size());
    }

    SECTION("功能可用：群名下方有群类型，默认普通群，选中帖子群读出 1")
    {
        MemberPickerLayoutRefs refs;
        std::unique_ptr<balloonwjui::DuiVBox> root = BuildCreateTree(true, refs);
        REQUIRE(refs.m_groupTypeNormal != NULL);
        REQUIRE(refs.m_groupTypeThread != NULL);
        CHECK(refs.m_groupTypeNormal->IsChecked());
        CHECK_FALSE(refs.m_groupTypeThread->IsChecked());
        CHECK(SelectedGroupType(refs.m_groupTypeNormal, refs.m_groupTypeThread) == balloonthread::kGroupTypeNormal);
        refs.m_groupTypeThread->SetCheck(true, false);
        refs.m_groupTypeNormal->SetCheck(false, false);
        CHECK(SelectedGroupType(refs.m_groupTypeNormal, refs.m_groupTypeThread) == balloonthread::kGroupTypeThread);
        CHECK(root->FindCtrlById(kPickerIdGroupTypeTitle) != NULL);
        CHECK(root->FindCtrlById(kPickerIdGroupTypeHint) != NULL);

        //排在群名输入框下方、已选成员列表上方
        RECT whole = { 0, 0, kContentW, kContentH };
        root->ForceLayout(whole);
        const RECT rcName = root->FindCtrlById(kPickerIdName)->GetRect();
        const RECT rcTitle = root->FindCtrlById(kPickerIdGroupTypeTitle)->GetRect();
        const RECT rcThread = root->FindCtrlById(kPickerIdGroupTypeThread)->GetRect();
        const RECT rcChips = root->FindCtrlById(kPickerIdChipScroll)->GetRect();
        CHECK(rcTitle.top >= rcName.bottom);
        CHECK(rcChips.top >= rcThread.bottom);
        //两个单选按钮按文字宽度占位，文字不会被挤成省略号
        CHECK(rcThread.right - rcThread.left > 0);
    }

    SECTION("功能不可用：不构建群类型，一律普通群")
    {
        MemberPickerLayoutRefs refs;
        std::unique_ptr<balloonwjui::DuiVBox> root = BuildCreateTree(false, refs);
        CHECK(refs.m_groupTypeNormal == NULL);
        CHECK(refs.m_groupTypeThread == NULL);
        CHECK(root->FindCtrlById(kPickerIdGroupTypeTitle) == NULL);
        CHECK(SelectedGroupType(refs.m_groupTypeNormal, refs.m_groupTypeThread) == balloonthread::kGroupTypeNormal);
    }

    SECTION("加了群类型之后设计尺寸与最小尺寸下都排得下")
    {
        const int sizes[2][2] = { { kMinContentW, kMinContentH }, { kContentW, kContentH } };
        for (int s = 0; s < 2; ++s)
        {
            MemberPickerLayoutRefs refs;
            std::unique_ptr<balloonwjui::DuiVBox> root = BuildCreateTree(true, refs);
            CheckFits(root.get(), kPickerIdConfirm, sizes[s][0], sizes[s][1]);
            CheckFits(root.get(), kPickerIdGroupTypeThread, sizes[s][0], sizes[s][1]);
            CheckFits(root.get(), kPickerIdGroupTypeHint, sizes[s][0], sizes[s][1]);
            const layoutfit::FitResult chips = layoutfit::MeasureAt(root.get(), kPickerIdChipScroll, sizes[s][0], sizes[s][1]);
            REQUIRE(chips.m_found);
            CHECK(chips.m_height >= kMinVisibleChips * kChipRowH);
        }
    }

    SECTION("本企业关闭帖子群时的失败提示")
    {
        CHECK(CreateGroupFailureText(error_code_thread_disabled) == CString(_T("本企业已关闭帖子群，请改建普通群。")));
    }
}

TEST_CASE("CGB1 建群确认按钮：请求在途时不可点并显示「正在创建…」，恢复后回到原文字", "[creategroup][threadgroup]")
{
    MemberPickerLayoutRefs refs;
    std::unique_ptr<balloonwjui::DuiVBox> root = BuildCreateTree(false, refs);
    REQUIRE(refs.m_confirmButton != NULL);
    CHECK(refs.m_confirmButton->GetText() == CString(_T("创建群聊")));
    CHECK(refs.m_confirmButton->IsEnabled());

    //请求发出：不可点，文字换成等待提示
    SetPickerConfirmBusy(refs.m_confirmButton, true, _T("创建群聊"));
    CHECK(refs.m_confirmButton->GetText() == CString(_T("正在创建…")));
    CHECK_FALSE(refs.m_confirmButton->IsEnabled());

    //失败或重新打开：回到原文字并可点
    SetPickerConfirmBusy(refs.m_confirmButton, false, _T("创建群聊"));
    CHECK(refs.m_confirmButton->GetText() == CString(_T("创建群聊")));
    CHECK(refs.m_confirmButton->IsEnabled());

    //按钮还没建出来：什么也不做，不崩溃
    SetPickerConfirmBusy(NULL, true, _T("创建群聊"));
}
