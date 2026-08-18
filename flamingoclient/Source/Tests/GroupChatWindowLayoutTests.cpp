/**
 *  GroupChatWindowLayoutTests.cpp
 *
 *  G-P1a 测试：验证 GroupChatWindowLayout 的种子函数 BuildGroupChatWindow
 *  Body() 产出的 balloonui 控件树符合"控件点名契约" —— 群聊窗所有需要被
 *  窗口代码引用的控件都能按既定数字 id 找到、类型正确。
 *
 *  覆盖用例（与需求确认一致）：
 *    G1-G8  群聊窗布局契约
 *    GC1    GroupCtrlId enum 自洽
 *
 *  用 Catch2 v3（amalgamated 形态）作测试框架。本工程不创建任何窗口，
 *  纯粹在内存里 build 控件树 + 断言。
 *
 *  balloonwj@qq.com   2026-05-22
 */

#include "stdafx.h"

#include "catch_amalgamated.hpp"

#include "../UI/BalloonChatWnd/GroupChatWindowLayout.h"
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiButton.h"
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiLabel.h"      // 公告面板各行文本
#include "../../../third-party/balloonui/balloonui/Controls/Input/DuiEdit.h"
#include "../UI/BalloonChatWnd/ChatInputRichEdit.h"
#include "../../../third-party/balloonui/balloonui/Controls/Layout/DuiSplitter.h"   // 消息区/输入区可拖分隔

using namespace balloonwjui;
using namespace balloonchatwnd;

namespace {

// 递归收集子树里所有非 0 的 ctrlId。
void CollectGroupIds(const DuiControl* node, std::vector<UINT>& out)
{
    if (node == nullptr)
    {
        return;
    }
    UINT id = node->GetCtrlId();
    if (id != 0)
    {
        out.push_back(id);
    }
    for (const auto& child : node->Children())
    {
        CollectGroupIds(child.get(), out);
    }
}

// 判断一组 id 里是否存在重复值。
bool HasDuplicateGroupId(std::vector<UINT> ids)
{
    std::sort(ids.begin(), ids.end());
    return std::adjacent_find(ids.begin(), ids.end()) != ids.end();
}

} // anonymous namespace

// =====================================================================
// G. 群聊窗布局契约
// =====================================================================

TEST_CASE("G1 BuildGroupChatWindowBody 返回非空控件树", "[gp1a][groupchat]")
{
    auto root = BuildGroupChatWindowBody();
    REQUIRE(root != nullptr);
}

TEST_CASE("G2 群聊窗含消息区容器", "[gp1a][groupchat]")
{
    auto root = BuildGroupChatWindowBody();
    REQUIRE(root != nullptr);
    CHECK(root->FindCtrlById(kGIdMessageArea) != nullptr);
}

TEST_CASE("G3 群聊窗输入框已迁移为无窗口富文本控件", "[gp1a][groupchat]")
{
    auto root = BuildGroupChatWindowBody();
    REQUIRE(root != nullptr);

    DuiControl* input = root->FindCtrlById(kGIdInputEdit);
    REQUIRE(input != nullptr);
    // 群聊输入框与单聊窗共用同一个控件。
    CHECK(dynamic_cast<balloonchatwnd::ChatInputRichEdit*>(input) != nullptr);
    CHECK(dynamic_cast<DuiEdit*>(input) == nullptr);
}

TEST_CASE("G9 群聊窗输入框配置：多行 / 自动换行 / 有占位符", "[gp1a][groupchat]")
{
    auto root = BuildGroupChatWindowBody();
    REQUIRE(root != nullptr);

    balloonchatwnd::ChatInputRichEdit* input =
        dynamic_cast<balloonchatwnd::ChatInputRichEdit*>(root->FindCtrlById(kGIdInputEdit));
    REQUIRE(input != nullptr);
    CHECK(input->IsMultiLine());
    CHECK(input->IsWordWrap());
    CHECK_FALSE(input->GetPlaceholder().IsEmpty());
}

TEST_CASE("G10 群聊窗输入框文本 round-trip（缓存层）", "[gp1a][groupchat]")
{
    auto root = BuildGroupChatWindowBody();
    REQUIRE(root != nullptr);

    balloonchatwnd::ChatInputRichEdit* input =
        dynamic_cast<balloonchatwnd::ChatInputRichEdit*>(root->FindCtrlById(kGIdInputEdit));
    REQUIRE(input != nullptr);

    input->SetText(_T("群里你好"));
    CHECK(input->GetText() == CString(_T("群里你好")));
    input->SetText(_T(""));
    CHECK(input->GetText().IsEmpty());
}

TEST_CASE("G4 群聊窗含发送按钮", "[gp1a][groupchat]")
{
    auto root = BuildGroupChatWindowBody();
    REQUIRE(root != nullptr);
    CHECK(dynamic_cast<DuiButton*>(root->FindCtrlById(kGIdSendButton)) != nullptr);
}

TEST_CASE("G5 群聊窗含头部五个图标按钮", "[gp1a][groupchat]")
{
    auto root = BuildGroupChatWindowBody();
    REQUIRE(root != nullptr);

    const UINT hdrIds[] =
    {
        kGIdHdrVoice, kGIdHdrVideo, kGIdHdrHistory, kGIdHdrAdmin, kGIdHdrMore
    };
    for (UINT id : hdrIds)
    {
        INFO("header button id = " << id);
        CHECK(dynamic_cast<DuiButton*>(root->FindCtrlById(id)) != nullptr);
    }
}

TEST_CASE("G6 群聊窗含输入工具栏六图标按钮与 @提醒 按钮", "[gp1a][groupchat]")
{
    auto root = BuildGroupChatWindowBody();
    REQUIRE(root != nullptr);

    const UINT tbIds[] =
    {
        kGIdTbEmoji, kGIdTbImage, kGIdTbFile,
        kGIdTbScreenshot, kGIdTbVoice, kGIdTbPlus, kGIdMention
    };
    for (UINT id : tbIds)
    {
        INFO("toolbar button id = " << id);
        CHECK(dynamic_cast<DuiButton*>(root->FindCtrlById(id)) != nullptr);
    }
}

TEST_CASE("G7 群聊窗含右侧边栏三 Tab 与成员列表容器", "[gp1a][groupchat]")
{
    auto root = BuildGroupChatWindowBody();
    REQUIRE(root != nullptr);

    const UINT tabIds[] = { kGIdTabMembers, kGIdTabFiles, kGIdTabNotice };
    for (UINT id : tabIds)
    {
        INFO("sidebar tab id = " << id);
        CHECK(dynamic_cast<DuiButton*>(root->FindCtrlById(id)) != nullptr);
    }
    CHECK(root->FindCtrlById(kGIdMemberList) != nullptr);
    CHECK(root->FindCtrlById(kGIdMemberSearch) != nullptr);
}

TEST_CASE("G12 群聊窗含「群文件」面板的搜索框/筛选chips/列表/分页/上传", "[gp1a][groupchat][groupfile]")
{
    auto root = BuildGroupChatWindowBody();
    REQUIRE(root != nullptr);

    // 两个内容容器（成员 / 群文件）都在。
    CHECK(root->FindCtrlById(kGIdMembersPanel) != nullptr);
    CHECK(root->FindCtrlById(kGIdFilesPanel)   != nullptr);

    // 搜索框 / 文件列表滚动视口 / 列表容器 / 分页信息 都能按 id 找到。
    CHECK(root->FindCtrlById(kGIdFileSearch)   != nullptr);
    CHECK(root->FindCtrlById(kGIdFileScroll)   != nullptr);
    CHECK(root->FindCtrlById(kGIdFileList)     != nullptr);
    CHECK(root->FindCtrlById(kGIdFilePageInfo) != nullptr);

    // 5 个筛选 chips + 上一/下一页 + 上传，都是 DuiButton。
    const UINT btnIds[] =
    {
        kGIdFileChipAll, kGIdFileChipPdf, kGIdFileChipDoc, kGIdFileChipSheet, kGIdFileChipDesign,
        kGIdFilePagePrev, kGIdFilePageNext, kGIdFileUpload
    };
    for (UINT id : btnIds)
    {
        INFO("group-file button id = " << id);
        CHECK(dynamic_cast<DuiButton*>(root->FindCtrlById(id)) != nullptr);
    }
}

TEST_CASE("G13 起始：成员容器显示、群文件容器隐藏（成员 Tab 默认选中）", "[gp1a][groupchat][groupfile]")
{
    auto root = BuildGroupChatWindowBody();
    REQUIRE(root != nullptr);

    DuiControl* membersPanel = root->FindCtrlById(kGIdMembersPanel);
    DuiControl* filesPanel   = root->FindCtrlById(kGIdFilesPanel);
    REQUIRE(membersPanel != nullptr);
    REQUIRE(filesPanel   != nullptr);
    // 成员 Tab 默认选中：成员容器可见、群文件容器隐藏。切换由 SwitchSideTab 负责。
    CHECK(membersPanel->IsVisible());
    CHECK_FALSE(filesPanel->IsVisible());
}

TEST_CASE("G8 群聊窗布局所有控件 id 互不重复", "[gp1a][groupchat]")
{
    auto root = BuildGroupChatWindowBody();
    REQUIRE(root != nullptr);

    std::vector<UINT> ids;
    CollectGroupIds(root.get(), ids);
    CHECK_FALSE(ids.empty());
    CHECK_FALSE(HasDuplicateGroupId(ids));
}

// =====================================================================
// GC. GroupCtrlId enum 自洽
// =====================================================================

TEST_CASE("GC1 群聊窗 GroupCtrlId 各枚举值互不重复", "[gp1a][groupchat][ids]")
{
    const std::vector<UINT> allIds =
    {
        kGIdMessageArea, kGIdInputEdit, kGIdSendButton, kGIdMention, kGIdInputSplitter,
        kGIdHdrVoice, kGIdHdrVideo, kGIdHdrHistory, kGIdHdrAdmin, kGIdHdrMore,
        kGIdTbEmoji, kGIdTbImage, kGIdTbFile, kGIdTbScreenshot, kGIdTbVoice,
        kGIdTbPlus,
        kGIdTabMembers, kGIdTabFiles, kGIdTabNotice,
        kGIdMemberList, kGIdMemberSearch,
        // 群文件面板（550 段）
        kGIdMembersPanel, kGIdFilesPanel, kGIdFileSearch,
        kGIdFileChipAll, kGIdFileChipPdf, kGIdFileChipDoc, kGIdFileChipSheet, kGIdFileChipDesign,
        kGIdFileScroll, kGIdFileList, kGIdFilePageInfo, kGIdFilePagePrev, kGIdFilePageNext,
        kGIdFileUpload,
    };
    CHECK_FALSE(HasDuplicateGroupId(allIds));
}

// =====================================================================
// GS. 群聊窗左列消息区/输入区可拖分隔条（拖工具栏上边缘改两者高度）
// =====================================================================

TEST_CASE("G11 群聊窗左列消息区与输入区之间有可拖的横向分隔条", "[groupchat][splitter]")
{
    auto root = BuildGroupChatWindowBody(false);
    REQUIRE(root != nullptr);

    DuiSplitter* sp = dynamic_cast<DuiSplitter*>(root->FindCtrlById(kGIdInputSplitter));
    REQUIRE(sp != nullptr);
    CHECK(sp->GetOrientation() == DuiSplitter::Horizontal);

    // pane0 内含消息区气泡列表，pane1 内含输入框 —— 拖分隔条即在二者间重分高度。
    DuiControl* pane0 = sp->GetPane(0);
    DuiControl* pane1 = sp->GetPane(1);
    REQUIRE(pane0 != nullptr);
    REQUIRE(pane1 != nullptr);
    CHECK(pane0->FindCtrlById(kGIdMessageArea) != nullptr);
    CHECK(pane1->FindCtrlById(kGIdInputEdit)   != nullptr);

    // 布局到固定尺寸后：消息区在上、输入区在下，分隔线处可被命中（= 可拖）。
    sp->Layout(RECT{ 0, 0, 400, 500 });
    RECT rcMsg   = pane0->GetRect();
    RECT rcInput = pane1->GetRect();
    CHECK(rcMsg.bottom <= rcInput.top);
    RECT bar = sp->GetBarRect();
    POINT onBar = { 200, (bar.top + bar.bottom) / 2 };
    CHECK(sp->IsPointInBar(onBar));
}

// =====================================================================
// G. 顶部离线提示条：在线（默认）时不得占据高度
//
// 与单聊窗 W14 同一个问题、同一套做法：提示条 label 在布局 XML 里默认可见、
// 文字为空、固定高 26px，窗口刚建好又没人推连接态给它，于是标题栏下面凭空
// 多出一条空白带。
// =====================================================================

TEST_CASE("B6 群聊窗顶部提示带默认不可见，头部紧贴窗口顶端", "[groupchat][banner]")
{
    auto root = BuildGroupChatWindowBody(false);
    REQUIRE(root != nullptr);

    DuiControl* banner = root->FindCtrlById(kGIdOfflineBanner);
    REQUIRE(banner != nullptr);
    CHECK_FALSE(banner->IsVisible());

    // 布局之后，头部那一排图标按钮应当贴着客户区顶端所在的 60px 头部带内，
    // 中间不夹一条空白。取第一个图标按钮的矩形来验证头部起点。
    root->Layout(RECT{ 0, 0, 800, 700 });
    DuiControl* firstIcon = root->FindCtrlById(kGIdHdrVoice);
    REQUIRE(firstIcon != nullptr);
    RECT rcIcon = firstIcon->GetRect();
    CHECK(rcIcon.top < 26);
}

// =====================================================================
// 群公告边栏（design/群公告.png）
//
// 边栏下部现在是三个互斥的内容容器：成员 / 群文件 / 公告。这一组用例锁的是
// 「控件点名契约」与「起始态只有成员面板可见」—— 后者曾经出过事：容器默认可见、
// 三个叠在同一个槽位上，界面上会看到几层内容糊在一起。
// =====================================================================

TEST_CASE("B7 公告面板的控件按 id 都能取到且类型正确", "[groupchat][notice]")
{
    auto root = BuildGroupChatWindowBody(false);
    REQUIRE(root != nullptr);

    //容器本身
    DuiControl* panel = root->FindCtrlById(kGIdNoticePanel);
    REQUIRE(panel != nullptr);

    //顶部「群公告 · N 条」与「‹ x/y ›」翻页
    CHECK(dynamic_cast<DuiLabel*>(root->FindCtrlById(kGIdNoticeCount)) != nullptr);
    CHECK(dynamic_cast<DuiButton*>(root->FindCtrlById(kGIdNoticePrev)) != nullptr);
    CHECK(dynamic_cast<DuiLabel*>(root->FindCtrlById(kGIdNoticeIndex)) != nullptr);
    CHECK(dynamic_cast<DuiButton*>(root->FindCtrlById(kGIdNoticeNext)) != nullptr);

    //公告卡与卡内三行
    CHECK(root->FindCtrlById(kGIdNoticeCard) != nullptr);
    CHECK(dynamic_cast<DuiLabel*>(root->FindCtrlById(kGIdNoticeTitle)) != nullptr);
    CHECK(dynamic_cast<DuiLabel*>(root->FindCtrlById(kGIdNoticeBody)) != nullptr);
    CHECK(dynamic_cast<DuiLabel*>(root->FindCtrlById(kGIdNoticeMeta)) != nullptr);

    //空状态提示
    CHECK(dynamic_cast<DuiLabel*>(root->FindCtrlById(kGIdNoticeEmpty)) != nullptr);
}

TEST_CASE("B8 边栏三个内容容器起始只有成员面板可见", "[groupchat][notice]")
{
    auto root = BuildGroupChatWindowBody(false);
    REQUIRE(root != nullptr);

    DuiControl* members = root->FindCtrlById(kGIdMembersPanel);
    DuiControl* files   = root->FindCtrlById(kGIdFilesPanel);
    DuiControl* notice  = root->FindCtrlById(kGIdNoticePanel);
    REQUIRE(members != nullptr);
    REQUIRE(files != nullptr);
    REQUIRE(notice != nullptr);

    //成员 Tab 默认选中，另两个容器必须先藏起来 —— 三者占同一个槽位，
    //同时可见会把内容糊在一起
    CHECK(members->IsVisible());
    CHECK_FALSE(files->IsVisible());
    CHECK_FALSE(notice->IsVisible());
}

TEST_CASE("B9 公告卡起始隐藏、空状态提示起始可见", "[groupchat][notice]")
{
    auto root = BuildGroupChatWindowBody(false);
    REQUIRE(root != nullptr);

    //刚开窗时还没拉过公告，此时应当显示空状态那句话而不是一张空白卡片
    DuiControl* card  = root->FindCtrlById(kGIdNoticeCard);
    DuiControl* empty = root->FindCtrlById(kGIdNoticeEmpty);
    REQUIRE(card != nullptr);
    REQUIRE(empty != nullptr);
    CHECK_FALSE(card->IsVisible());
    CHECK(empty->IsVisible());
}

TEST_CASE("B10 头部左侧信息块存在且占满按钮左侧空间", "[groupchat][notice]")
{
    //头部左侧原先是个不可见的 spacer 占位，公告摘要行接入后换成了自绘信息块。
    //它必须真的在树里，否则群名 / 人数 / 公告摘要都无处可画。
    auto root = BuildGroupChatWindowBody(false);
    REQUIRE(root != nullptr);

    DuiControl* header = root->FindCtrlById(kGIdHeaderInfo);
    REQUIRE(header != nullptr);

    root->Layout(RECT{ 0, 0, 800, 700 });
    RECT rcHeader = header->GetRect();
    RECT rcFirstIcon = root->FindCtrlById(kGIdHdrVoice)->GetRect();

    //信息块在头部左侧、右边界不越过第一个图标按钮
    CHECK(rcHeader.left < rcFirstIcon.left);
    CHECK(rcHeader.right <= rcFirstIcon.left);
    //且确实分到了宽度（weight=1），不是被压成 0
    CHECK(rcHeader.right - rcHeader.left > 100);
}

TEST_CASE("GC2 公告面板各 id 落在 570 段且互不重复", "[groupchat][notice]")
{
    //边栏公告面板独占 570 段，与成员 / 群文件的 550 段拉开，避免哪天加控件时撞号
    const UINT ids[] =
    {
        kGIdNoticePanel, kGIdNoticeCount, kGIdNoticePrev, kGIdNoticeIndex,
        kGIdNoticeNext, kGIdNoticeCard, kGIdNoticeTitle, kGIdNoticeBody,
        kGIdNoticeMeta, kGIdNoticeEmpty,
    };
    const int count = (int)(sizeof(ids) / sizeof(ids[0]));

    for (int i = 0; i < count; ++i)
    {
        CHECK(ids[i] >= 570);
        CHECK(ids[i] <= 579);
        for (int j = i + 1; j < count; ++j)
        {
            CHECK(ids[i] != ids[j]);
        }
    }

    //头部信息块另占 580，不与公告段重叠
    CHECK(kGIdHeaderInfo == 580);
}
