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
#include "../UI/BalloonChatWnd/ChatBannerLabel.h"           // G6e：屏幕共享横幅的类型与文字
#include "../UI/BalloonChatWnd/GroupChatHeaderInfo.h"       // HI1~HI3：头部信息块画群简介
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiButton.h"
#include "../UI/BalloonChatWnd/ChatIconButton.h"   // W21 / G6h：截图下拉按钮 ▾ 的控件类型
#include "../UI/BalloonChatWnd/ChatIcons.h"        // G6j：组织架构图标的画法
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiLabel.h"      // 公告面板各行文本
#include "../../../third-party/balloonui/balloonui/Controls/Input/DuiEdit.h"
#include "../../../third-party/balloonui/balloonui/Controls/Layout/DuiLayout.h"   // 读头部按钮的布局提示（W18 / G6c）
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


TEST_CASE("G5b 群聊窗头部不再有清空聊天记录按钮（2026-10-03 移到消息记录窗）", "[gp1a][groupchat][clearconv]")
{
    //516 是原先扫帚按钮的 id，枚举已删除，这里写成字面量
    const UINT kRetiredClearId = 516;
    auto root = BuildGroupChatWindowBody();
    REQUIRE(root != nullptr);
    CHECK(root->FindCtrlById(kRetiredClearId) == nullptr);
    //「…」仍在头部，「消息记录」按钮也还在
    CHECK(dynamic_cast<DuiButton*>(root->FindCtrlById(kGIdHdrMore)) != nullptr);
    CHECK(dynamic_cast<DuiButton*>(root->FindCtrlById(kGIdHdrHistory)) != nullptr);
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

TEST_CASE("G6c 群聊窗的「文件共享」按钮在头部、紧挨在「共享屏幕」右边，输入框工具栏里不再有", "[groupchat][fileshare-entry]")
{
    auto root = BuildGroupChatWindowBody();
    REQUIRE(root != nullptr);
    DuiControl* screenShare = root->FindCtrlById(kGIdHdrScreenShare);
    DuiControl* share = root->FindCtrlById(kGIdHdrShare);
    DuiControl* plus = root->FindCtrlById(kGIdTbPlus);
    REQUIRE(dynamic_cast<DuiButton*>(share) != nullptr);
    REQUIRE(screenShare != nullptr);
    REQUIRE(plus != nullptr);
    //与「共享屏幕」同在头部那一行，不在「+」所在的输入框工具栏里
    REQUIRE(share->GetParent() == screenShare->GetParent());
    CHECK(share->GetParent() != plus->GetParent());
    const auto& kids = share->GetParent()->Children();
    size_t screenAt = kids.size();
    size_t shareAt = kids.size();
    for (size_t i = 0; i < kids.size(); ++i)
    {
        if (kids[i].get() == screenShare)
            screenAt = i;
        if (kids[i].get() == share)
            shareAt = i;
    }
    CHECK(shareAt == screenAt + 1);
    //与头部其它按钮同宽
    balloonwjui::DuiLayout* row = dynamic_cast<balloonwjui::DuiLayout*>(share->GetParent());
    REQUIRE(row != nullptr);
    CHECK(row->GetHint(share).fixedMain == row->GetHint(screenShare).fixedMain);
    CHECK(kGIdHdrShare == 529);
}

TEST_CASE("G6b 群聊窗的「发起接龙」按钮在头部、紧挨在「文件共享」右边，输入框工具栏里不再有", "[groupchat][grouprelay]")
{
    //群聊窗客户区尺寸量级（像素），取值无特殊含义
    const int kBodyW = 880;
    const int kBodyH = 600;

    auto root = BuildGroupChatWindowBody();
    REQUIRE(root != nullptr);
    DuiControl* relay = root->FindCtrlById(kGIdHdrRelay);
    DuiControl* share = root->FindCtrlById(kGIdHdrShare);
    DuiControl* history = root->FindCtrlById(kGIdHdrHistory);
    DuiControl* plus = root->FindCtrlById(kGIdTbPlus);
    REQUIRE(dynamic_cast<DuiButton*>(relay) != nullptr);
    REQUIRE(share != nullptr);
    REQUIRE(history != nullptr);
    REQUIRE(plus != nullptr);

    //与「文件共享」「消息记录」同在头部那一行，不在「+」所在的输入框工具栏里
    REQUIRE(relay->GetParent() == share->GetParent());
    REQUIRE(relay->GetParent() == history->GetParent());
    CHECK(relay->GetParent() != plus->GetParent());
    const auto& kids = relay->GetParent()->Children();
    size_t shareAt = kids.size();
    size_t relayAt = kids.size();
    size_t historyAt = kids.size();
    for (size_t i = 0; i < kids.size(); ++i)
    {
        if (kids[i].get() == share)
            shareAt = i;
        if (kids[i].get() == relay)
            relayAt = i;
        if (kids[i].get() == history)
            historyAt = i;
    }
    CHECK(relayAt == shareAt + 1);
    //2026-10-05 起「消息记录」左边插入了「字幕模式」按钮（见 SubtitleTests.cpp 的 SUBH2），这里只要求排在接龙之后
    CHECK(historyAt > relayAt);

    //与头部其它按钮同宽
    balloonwjui::DuiLayout* row = dynamic_cast<balloonwjui::DuiLayout*>(relay->GetParent());
    REQUIRE(row != nullptr);
    CHECK(row->GetHint(relay).fixedMain == row->GetHint(share).fixedMain);

    //排版之后落在「文件共享」与「消息记录」之间，三者顶边对齐、宽度相同
    root->ForceLayout(RECT{ 0, 0, kBodyW, kBodyH });
    const RECT rcShare = share->GetRect();
    const RECT rcRelay = relay->GetRect();
    const RECT rcHistory = history->GetRect();
    CHECK(rcRelay.right > rcRelay.left);
    CHECK(rcShare.right <= rcRelay.left);
    CHECK(rcRelay.right <= rcHistory.left);
    CHECK(rcRelay.top == rcShare.top);
    CHECK(rcRelay.right - rcRelay.left == rcShare.right - rcShare.left);
    //编号沿用挪动之前的值
    CHECK(kGIdHdrRelay == 527);
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
        kGIdHdrVoice, kGIdHdrVideo, kGIdHdrScreenShare, kGIdHdrHistory, kGIdHdrAdmin, kGIdHdrMore,
        kGIdTbEmoji, kGIdTbImage, kGIdTbFile, kGIdTbScreenshot, kGIdTbVoice,
        kGIdTbPlus, kGIdTbFont, kGIdHdrRelay, kGIdHdrShare, kGIdTbRecord, kGIdTbScreenshotMenu,
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
    //它必须真的在树里，否则公告摘要无处可画（2026-10-03 起信息块不再画群名与人数）。
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

TEST_CASE("HI4 群聊窗缩到最小宽度时，头部群简介行仍不越过第一个图标按钮", "[groupchat][groupintro]")
{
    //B10 只在 800 宽下检查过。窗口最窄能缩到 kGroupChatWindowMinWidth，此时八个固定宽度的
    //图标按钮不变，被压缩的只有左侧信息块；它仍须止于第一个图标按钮左侧，并留有能显示文字的宽度。
    //信息块至少要有的宽度（像素），低于这个值群简介只能显示一两个字
    const int kMinUsableHeaderW = 100;

    std::unique_ptr<DuiControl> root = BuildGroupChatWindowBody(false);
    REQUIRE(root != nullptr);

    DuiControl* header = root->FindCtrlById(kGIdHeaderInfo);
    REQUIRE(header != nullptr);
    DuiControl* firstIcon = root->FindCtrlById(kGIdHdrVoice);
    REQUIRE(firstIcon != nullptr);

    root->Layout(RECT{ 0, 0, kGroupChatWindowMinWidth, kGroupChatWindowMinHeight });
    RECT rcHeader = header->GetRect();
    RECT rcFirstIcon = firstIcon->GetRect();

    CHECK(rcHeader.right <= rcFirstIcon.left);
    CHECK(rcHeader.right - rcHeader.left > kMinUsableHeaderW);
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

TEST_CASE("G6d 群聊窗头部的「共享屏幕」按钮紧挨在「视频通话」右边", "[groupchat][sscard]")
{
    auto root = BuildGroupChatWindowBody();
    REQUIRE(root != nullptr);
    DuiControl* video = root->FindCtrlById(kGIdHdrVideo);
    DuiControl* share = root->FindCtrlById(kGIdHdrScreenShare);
    REQUIRE(dynamic_cast<DuiButton*>(share) != nullptr);
    REQUIRE(video != nullptr);
    REQUIRE(share->GetParent() == video->GetParent());
    const auto& kids = share->GetParent()->Children();
    size_t videoAt = kids.size();
    size_t shareAt = kids.size();
    for (size_t i = 0; i < kids.size(); ++i)
    {
        if (kids[i].get() == video)
            videoAt = i;
        if (kids[i].get() == share)
            shareAt = i;
    }
    CHECK(shareAt == videoAt + 1);
    CHECK(kGIdHdrScreenShare == 517);
}

TEST_CASE("G6e 群聊窗的屏幕共享横幅：起始隐藏，紧挨在 Pin 提示带上方，有文字才显示", "[groupchat][sscard]")
{
    auto root = BuildGroupChatWindowBody();
    REQUIRE(root != nullptr);
    DuiControl* banner = root->FindCtrlById(kGIdScreenShareBanner);
    DuiControl* pin = root->FindCtrlById(kGIdPinBanner);
    REQUIRE(dynamic_cast<ChatBannerLabel*>(banner) != nullptr);
    REQUIRE(pin != nullptr);
    CHECK_FALSE(banner->IsVisible());
    REQUIRE(banner->GetParent() == pin->GetParent());
    const auto& kids = banner->GetParent()->Children();
    size_t bannerAt = kids.size();
    size_t pinAt = kids.size();
    for (size_t i = 0; i < kids.size(); ++i)
    {
        if (kids[i].get() == banner)
            bannerAt = i;
        if (kids[i].get() == pin)
            pinAt = i;
    }
    CHECK(bannerAt + 1 == pinAt);
    CHECK(kGIdScreenShareBanner == 518);

    //有文字：显示，返回「显隐变了」；文字相同再调一次：不变；空文字：隐藏
    CHECK(UpdateScreenShareBanner(root.get(), L"张三 正在共享屏幕，点击加入 ›"));
    CHECK(banner->IsVisible());
    CHECK(std::wstring(dynamic_cast<ChatBannerLabel*>(banner)->GetText()) == L"张三 正在共享屏幕，点击加入 ›");
    CHECK_FALSE(UpdateScreenShareBanner(root.get(), L"张三 正在共享屏幕，点击加入 ›"));
    CHECK(UpdateScreenShareBanner(root.get(), std::wstring()));
    CHECK_FALSE(banner->IsVisible());
    CHECK_FALSE(UpdateScreenShareBanner(NULL, L"x"));
}

TEST_CASE("G6f 群聊窗屏幕共享横幅的文字", "[groupchat][sscard]")
{
    CHECK(MakeScreenShareBannerText(false, L"张三") == L"张三 正在共享屏幕，点击加入 ›");
    CHECK(MakeScreenShareBannerText(false, L"") == L"有人正在共享屏幕，点击加入 ›");
    //本人正在共享：只告知，不提示加入
    CHECK(MakeScreenShareBannerText(true, L"张三") == L"你正在共享屏幕");
}

TEST_CASE("G6g 群聊窗工具栏的「屏幕录制」按钮紧挨在截图与其下拉按钮 ▾ 右边", "[groupchat][record]")
{
    // 2026-10-05 截图右边加了 ▾（docs/截图快捷键与隐藏当前窗口方案.md），录屏从「截图右边第 1 个」变为第 2 个
    auto root = BuildGroupChatWindowBody();
    REQUIRE(root != nullptr);
    DuiControl* cut = root->FindCtrlById(kGIdTbScreenshot);
    DuiControl* record = root->FindCtrlById(kGIdTbRecord);
    REQUIRE(dynamic_cast<DuiButton*>(record) != nullptr);
    REQUIRE(cut != nullptr);
    REQUIRE(record->GetParent() == cut->GetParent());
    const auto& kids = record->GetParent()->Children();
    size_t cutAt = kids.size();
    size_t recordAt = kids.size();
    for (size_t i = 0; i < kids.size(); ++i)
    {
        if (kids[i].get() == cut)
            cutAt = i;
        if (kids[i].get() == record)
            recordAt = i;
    }
    CHECK(recordAt == cutAt + 2);
    CHECK(kGIdTbRecord == 528);
}

TEST_CASE("G6h 群聊窗工具栏的截图下拉按钮 ▾ 紧挨在「截图」右边、宽 14 像素", "[groupchat][screenshot]")
{
    auto root = BuildGroupChatWindowBody();
    REQUIRE(root != nullptr);
    RECT rc = { 0, 0, 900, 640 };
    root->SetRect(rc);
    DuiControl* cut = root->FindCtrlById(kGIdTbScreenshot);
    DuiControl* caret = root->FindCtrlById(kGIdTbScreenshotMenu);
    REQUIRE(cut != nullptr);
    REQUIRE(dynamic_cast<ChatIconButton*>(caret) != nullptr);
    REQUIRE(caret->GetParent() == cut->GetParent());
    const auto& kids = caret->GetParent()->Children();
    size_t cutAt = kids.size();
    size_t caretAt = kids.size();
    for (size_t i = 0; i < kids.size(); ++i)
    {
        if (kids[i].get() == cut)
            cutAt = i;
        if (kids[i].get() == caret)
            caretAt = i;
    }
    CHECK(caretAt == cutAt + 1);
    const int kCaretWidthPx = 14;   // 布局 XML 里 ▾ 的 fixedWidth
    CHECK(caret->GetRect().right - caret->GetRect().left == kCaretWidthPx);
    CHECK(kGIdTbScreenshotMenu == 537);
}

namespace {

//信息块的尺寸，单位：像素（与群聊窗头部那一行同一量级：行高 60）
const int kHeaderW = 300;
const int kHeaderH = 60;
//群简介一行的高度（像素），与 GroupChatHeaderInfo.cpp 的 kIntroLineH 相同；这一行在信息块里垂直居中
const int kIntroLineH = 17;
const int kIntroLineTop = (kHeaderH - kIntroLineH) / 2;
//「文字一直画到右端」的判定范围：信息块最右侧这么宽（像素）的区域里要有像素。
//省略号紧跟在最后一个放得下的字后面，最多空出不到一个字宽（12px 字号约 12px），取两个字宽
const int kNearRightPx = 24;

//头部信息块画在内存位图上之后，各区域里被画成非白色的像素个数
struct HeaderPaintStats
{
    int m_painted;      // 非白色像素总数
    int m_outsideLine;  // 落在群简介那一行高度范围之外的个数
    int m_beyondRight;  // 落在信息块右边界之外（位图比信息块宽出的那一段，相当于图标按钮的位置）的个数
    int m_nearRight;    // 落在信息块最右侧 kNearRightPx 宽度之内的个数
};

//把头部信息块画到白底内存位图上并统计像素。
//  header：被测控件；ctrlWidth：控件矩形的宽度（像素）；bitmapWidth：位图宽度（像素），不小于 ctrlWidth，
//  多出的部分用来检查文字有没有画出控件右边界。控件矩形与位图左上角对齐、同高 kHeaderH。
HeaderPaintStats PaintHeaderInfo(GroupChatHeaderInfo& header, int ctrlWidth, int bitmapWidth)
{
    HDC screen = ::GetDC(NULL);
    HDC mem = ::CreateCompatibleDC(screen);
    HBITMAP bmp = ::CreateCompatibleBitmap(screen, bitmapWidth, kHeaderH);
    HGDIOBJ old = ::SelectObject(mem, bmp);
    RECT rcBitmap = { 0, 0, bitmapWidth, kHeaderH };
    HBRUSH white = ::CreateSolidBrush(RGB(0xFF, 0xFF, 0xFF));
    ::FillRect(mem, &rcBitmap, white);
    ::DeleteObject(white);
    RECT rcCtrl = { 0, 0, ctrlWidth, kHeaderH };
    header.SetRect(rcCtrl);
    header.OnPaint(mem, rcCtrl);

    HeaderPaintStats stats;
    stats.m_painted = 0;
    stats.m_outsideLine = 0;
    stats.m_beyondRight = 0;
    stats.m_nearRight = 0;
    for (int y = 0; y < kHeaderH; ++y)
    {
        for (int x = 0; x < bitmapWidth; ++x)
        {
            if (::GetPixel(mem, x, y) == RGB(0xFF, 0xFF, 0xFF))
                continue;
            ++stats.m_painted;
            if (y < kIntroLineTop || y >= kIntroLineTop + kIntroLineH)
                ++stats.m_outsideLine;
            if (x >= ctrlWidth)
                ++stats.m_beyondRight;
            else if (x >= ctrlWidth - kNearRightPx)
                ++stats.m_nearRight;
        }
    }
    ::SelectObject(mem, old);
    ::DeleteObject(bmp);
    ::DeleteDC(mem);
    ::ReleaseDC(NULL, screen);
    return stats;
}

} // namespace

TEST_CASE("HI1 头部信息块画群简介：没有简介时整块空白，有简介时只画垂直居中的一行", "[groupchat][groupintro]")
{
    //没有群简介：什么也不画（2026-10-03 起头部不再画群名与人数）
    GroupChatHeaderInfo header;
    HeaderPaintStats stats = PaintHeaderInfo(header, kHeaderW, kHeaderW);
    CHECK(stats.m_painted == 0);

    //有群简介：只画中间那一行
    header.SetGroupIntro(_T("产品研发部日常沟通群"));
    stats = PaintHeaderInfo(header, kHeaderW, kHeaderW);
    CHECK(stats.m_painted > 0);
    CHECK(stats.m_outsideLine == 0);

    //只有空白的群简介按没有群简介处理
    header.SetGroupIntro(_T(" \r\n\t "));
    stats = PaintHeaderInfo(header, kHeaderW, kHeaderW);
    CHECK(stats.m_painted == 0);
}

TEST_CASE("HI2 超长群简介只画在信息块之内、一直画到右端，不会盖住右侧的图标按钮", "[groupchat][groupintro]")
{
    //位图比信息块宽出的这一段（像素）相当于头部右侧图标按钮的位置，这里一个像素都不能画
    const int kIconAreaW = 120;
    //超长简介由这一段重复若干次拼成，总长 200 个字
    const int kRepeat = 40;

    CString longIntro;
    for (int i = 0; i < kRepeat; ++i)
    {
        longIntro += _T("群简介很长");
    }
    REQUIRE(longIntro.GetLength() == 200);

    GroupChatHeaderInfo header;
    header.SetGroupIntro(longIntro);
    HeaderPaintStats stats = PaintHeaderInfo(header, kHeaderW, kHeaderW + kIconAreaW);
    CHECK(stats.m_painted > 0);
    CHECK(stats.m_beyondRight == 0);
    //文字一直画到信息块右端（以省略号结尾），而不是在中途就断掉
    CHECK(stats.m_nearRight > 0);
    CHECK(stats.m_outsideLine == 0);
}

TEST_CASE("HI3 群简介整理成一行：换行与制表符变空格、去掉首尾空白，& 原样显示", "[groupchat][groupintro]")
{
    //每一段连续的换行 / 回车 / 制表符只换成一个空格
    CHECK(MakeHeaderIntroLine(_T("第一行\r\n第二行\n\n第三行\t末尾")) == CString(_T("第一行 第二行 第三行 末尾")));
    //首尾的半角空格、全角空格（U+3000）与换行都去掉，中间的空格保留
    CHECK(MakeHeaderIntroLine(_T("\x3000 前后 空白 \x3000\r\n")) == CString(_T("前后 空白")));
    //只有空白、空串、NULL 都得到空串
    CHECK(MakeHeaderIntroLine(_T(" \r\n\t \x3000")).IsEmpty());
    CHECK(MakeHeaderIntroLine(_T("")).IsEmpty());
    CHECK(MakeHeaderIntroLine(NULL).IsEmpty());
    //& 不做任何处理
    CHECK(MakeHeaderIntroLine(_T("研发&测试")) == CString(_T("研发&测试")));

    //绘制时 & 也要原样显示。DrawText 默认把 && 画成一个 &（Win32 文档中 DT_NOPREFIX 一节的说明），
    //所以 "&&" 与 "&" 画出的像素数相同就说明 & 被当成了快捷键前缀；原样显示时 "&&" 约是 "&" 的两倍
    GroupChatHeaderInfo header;
    header.SetGroupIntro(_T("&"));
    const int onePainted = PaintHeaderInfo(header, kHeaderW, kHeaderW).m_painted;
    header.SetGroupIntro(_T("&&"));
    const int twoPainted = PaintHeaderInfo(header, kHeaderW, kHeaderW).m_painted;
    REQUIRE(onePainted > 0);
    //以 1.5 倍为界：明显多于一个 & 的像素，说明两个 & 都画出来了
    CHECK(twoPainted * 2 > onePainted * 3);
}

TEST_CASE("G6i 成员搜索框右边是「按部门 / 平铺」图标按钮：组织架构图标，部门树视图下高亮", "[groupchat][deptview]")
{
    //2026-10-07 用户要求：原来的「按部门 / 平铺」文字链接改为图标按钮，图标用组织架构图，
    //部门树视图下图标变蓝（与「字幕模式」按钮相同），悬停提示说明点了之后切到哪种视图
    auto root = BuildGroupChatWindowBody();
    REQUIRE(root != nullptr);
    DuiControl* search = root->FindCtrlById(kGIdMemberSearch);
    ChatIconButton* toggle = dynamic_cast<ChatIconButton*>(root->FindCtrlById(kGIdMemberViewToggle));
    REQUIRE(search != nullptr);
    REQUIRE(toggle != nullptr);
    CHECK(toggle->GetIconKind() == kChatIconOrgTree);

    //与搜索框同一行，紧挨在它右边
    REQUIRE(toggle->GetParent() == search->GetParent());
    const auto& kids = toggle->GetParent()->Children();
    size_t searchAt = kids.size();
    size_t toggleAt = kids.size();
    for (size_t i = 0; i < kids.size(); ++i)
    {
        if (kids[i].get() == search)
            searchAt = i;
        if (kids[i].get() == toggle)
            toggleAt = i;
    }
    CHECK(toggleAt == searchAt + 1);

    //建好时按平铺视图设置：不高亮，提示点了会按部门显示
    CHECK_FALSE(toggle->IsActive());
    CHECK(toggle->GetToggleTip() == CString(L"按部门显示"));

    //部门树视图：高亮，提示点了会回到平铺
    CHECK(ApplyMemberViewToggle(root.get(), true));
    CHECK(toggle->IsActive());
    CHECK(toggle->GetToggleTip() == CString(L"平铺显示"));

    //切回平铺视图
    CHECK(ApplyMemberViewToggle(root.get(), false));
    CHECK_FALSE(toggle->IsActive());
    CHECK(toggle->GetToggleTip() == CString(L"按部门显示"));
}

namespace {

//G6j 的渲染边长（像素）：viewBox 24 放大 2 倍，viewBox 里的坐标乘 2 即像素坐标
const int kOrgIconPx = 48;
//描边宽度（viewBox 单位），与 ChatIconButton 的实际取值相同
const Gdiplus::REAL kOrgIconStroke = 2.0f;
//透明度超过这个值的像素算作「有墨」
const BYTE kOrgInkAlpha = 64;
//判定两个图标「明显不同」时的最少差异像素（与 RecordIconTests 的 kRicMinDiff 相同）
const int kOrgMinDiff = 60;

//在测试期间启动 GDI+，析构时关闭
class OrgIconScopedGdiplus
{
public:
    OrgIconScopedGdiplus()
        : m_token(0)
    {
        Gdiplus::GdiplusStartupInput input;
        m_ok = (Gdiplus::GdiplusStartup(&m_token, &input, NULL) == Gdiplus::Ok);
    }

    ~OrgIconScopedGdiplus()
    {
        if (m_ok)
            Gdiplus::GdiplusShutdown(m_token);
    }

    //GDI+ 是否启动成功
    bool Ok() const
    {
        return m_ok;
    }

private:
    ULONG_PTR m_token;   //GdiplusStartup 返回的令牌，关闭时交回
    bool      m_ok;      //是否启动成功
};

//用聊天窗工具栏的画法（DrawChatIcon）把一个图标画到透明位图上，逐像素返回「是否有墨」
std::vector<bool> RenderOrgTestIcon(int kind)
{
    Gdiplus::Bitmap bmp(kOrgIconPx, kOrgIconPx, PixelFormat32bppARGB);
    {
        Gdiplus::Graphics g(&bmp);
        g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        g.Clear(Gdiplus::Color(0, 0, 0, 0));
        DrawChatIcon(g, kind, 0.0f, 0.0f, (Gdiplus::REAL)kOrgIconPx, Gdiplus::Color(255, 0, 0, 0), kOrgIconStroke);
    }
    std::vector<bool> ink;
    for (int y = 0; y < kOrgIconPx; ++y)
    {
        for (int x = 0; x < kOrgIconPx; ++x)
        {
            Gdiplus::Color c;
            bmp.GetPixel(x, y, &c);
            ink.push_back(c.GetA() > kOrgInkAlpha);
        }
    }
    return ink;
}

//某个像素（位图坐标）是否有墨
bool OrgInkAt(const std::vector<bool>& ink, int x, int y)
{
    return ink[static_cast<size_t>(y * kOrgIconPx + x)];
}

//两个图标不同的像素数
int CountOrgIconDiff(const std::vector<bool>& a, const std::vector<bool>& b)
{
    int n = 0;
    for (size_t i = 0; i < a.size() && i < b.size(); ++i)
    {
        if (a[i] != b[i])
            ++n;
    }
    return n;
}

} // namespace

TEST_CASE("G6j 组织架构图标画得出来，与无序列表、入群邀请图标明显不同", "[groupchat][deptview]")
{
    OrgIconScopedGdiplus gdiplus;
    REQUIRE(gdiplus.Ok());
    const std::vector<bool> org = RenderOrgTestIcon(kChatIconOrgTree);

    //组织架构图的几处特征（viewBox 坐标乘 2）：上方框的顶边 (12, 3)、连接横线的左右两端附近 (7, 12) 与 (17, 12)、
    //左下框的底边 (6, 21)、右下框的底边 (18, 21)
    CHECK(OrgInkAt(org, 24, 6));
    CHECK(OrgInkAt(org, 14, 24));
    CHECK(OrgInkAt(org, 34, 24));
    CHECK(OrgInkAt(org, 12, 42));
    CHECK(OrgInkAt(org, 36, 42));
    //两个下方框之间（viewBox x=12、y=18）是空白，说明画的是分开的两个框而不是一整条
    CHECK_FALSE(OrgInkAt(org, 24, 36));

    CHECK(CountOrgIconDiff(org, RenderOrgTestIcon(kChatIconMdBulletList)) >= kOrgMinDiff);
    CHECK(CountOrgIconDiff(org, RenderOrgTestIcon(kChatIconGroupInvite)) >= kOrgMinDiff);
}
