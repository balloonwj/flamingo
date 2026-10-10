/**
 *  机器人管理界面的布局单测（标签 [botdll]，UI 约定第 7 条：固定尺寸的窗口按设计尺寸排得下）：
 *    - BOTL1 规则编辑窗：内容最多的表单（个人机器人的收到单聊，选了人、勾了时间段）按设计尺寸排版，底部「确定」没有被压扁、
 *            没有越过内容区，下方留白与空间充裕时相同；表单的总高超过可见区域时由滚动视图承担；重建表单之后控件齐全；
 *    - BOTL2 机器人编辑窗：带 Webhook 一组的群机器人与个人机器人，底部「保存」排得下；规则列表填入后高度随之变化；
 *    - BOTL3 列表窗口：群机器人管理窗与「我的机器人」窗口的骨架，底部按钮排得下；加一行机器人后内容区变高；
 *    - BOTL4 列表先排过版、再换一批总高度相同的内容（如「在本群关闭」之后重建列表）：新的每一行仍然排了版。
 *      2026-10-08 实机联调时管理窗在这种情况下整片空白，原因见开发约定文档 UI 约定第 12 条；
 *    - BOTL5 同上，但第二批多一行机器人、内容变高（新建群机器人之后重建列表）：新的每一行同样排了版；
 *    - BOTL6 编辑窗的 Webhook 一组（docs/机器人方案.md §11 Q19）：服务端下发了对外地址时多一个只读的「地址」框、显示该地址，
 *            没有下发时不建这个框；两种情况底部「保存」都按设计尺寸排得下；
 *    - BOTL7 规则编辑窗的「指定的部门」（docs/机器人方案.md §14.8）：发送者下拉框始终有三项；选了部门时有「选择…」，
 *            下面写出已选的部门名称与「含下级部门」，没选时提示去选；选「全部人」时没有「选择…」；读回发送者范围不变。
 *    - BOTL8 编辑一条规则后列表整体重建、总高度不变时，每张规则卡片仍然排了版（曾经那一行变成空白）。
 *  框架 Catch2 v3；排版检查用 Source/Tests/LayoutFitCheck.h。
 *  balloonwj@qq.com   2026-10-08
 */
#include "stdafx.h"

#include "catch_amalgamated.hpp"

#include <memory>
#include <string>
#include <vector>

#include "../Core/BotRuleForm.h"
#include "../Core/BotRules.h"
#include "../UI/BotEditLayout.h"
#include "../UI/BotListLayout.h"
#include "../UI/BotRuleLayout.h"
#include "../UI/BotWidgets.h"
#include "../../Source/Tests/LayoutFitCheck.h"

using namespace flamingobot;

namespace {

//标准 DPI
const int kTestDpi = 96;

//检查某个控件按给定内容区尺寸排得下
void CheckFits(balloonwjui::DuiControl* root, UINT id, int width, int height)
{
    const layoutfit::FitResult r = layoutfit::MeasureAt(root, id, width, height);
    REQUIRE(r.m_found);
    CHECK(r.m_bottom <= height);
    CHECK(r.m_height == r.m_roomyHeight);
    CHECK(r.m_bottomGap == r.m_roomyBottomGap);
}

//在控件树里找一个文字包含 needle 的 DuiLabel
bool HasLabelContaining(balloonwjui::DuiControl* root, const std::wstring& needle)
{
    if (root == NULL)
        return false;
    balloonwjui::DuiLabel* label = dynamic_cast<balloonwjui::DuiLabel*>(root);
    if (label != NULL && std::wstring(label->GetText()).find(needle) != std::wstring::npos)
        return true;
    for (size_t i = 0; i < root->Children().size(); ++i)
    {
        if (HasLabelContaining(root->Children()[i].get(), needle))
            return true;
    }
    return false;
}

} // namespace

TEST_CASE("BOTL7 规则编辑窗：指定的部门可以选择", "[botdll]")
{
    HFONT font = WindowFont(kTestDpi);
    ScrollFrameControls frame;
    std::unique_ptr<balloonwjui::DuiVBox> root = BuildRuleWindowBody(font, frame);
    REQUIRE(frame.m_content != NULL);
    const int contentHeight = kRuleWindowHeight - kBotTitleBarHeight;

    //选了两个部门：有「选择…」，名称与「含下级部门」写在下面
    BotRuleForm form = RuleToForm(MakeDefaultRule(bot_owner_personal));
    form.m_senderKind = bot_sender_depts;
    form.m_senderDepts.push_back(30);
    form.m_senderDepts.push_back(40);
    RuleFormContext ctx;
    ctx.m_ownerType = bot_owner_personal;
    ctx.m_senderNames = L"研发中心、市场部";
    RuleFormControls controls;
    BuildRuleForm(frame.m_content, form, ctx, font, kRuleContentWidth, controls);
    REQUIRE(controls.m_senderValues.size() == 3);
    CHECK(controls.m_senderValues[2] == bot_sender_depts);
    CHECK(controls.m_pickSenders != NULL);
    CHECK(HasLabelContaining(frame.m_content, L"研发中心、市场部"));
    CHECK(HasLabelContaining(frame.m_content, L"含下级部门"));
    CheckFits(root.get(), kIdRuleOk, kRuleWindowWidth, contentHeight);
    //读回：发送者范围与部门不变
    BotRuleForm harvested = form;
    HarvestRuleForm(controls, harvested);
    CHECK(harvested.m_senderKind == bot_sender_depts);
    CHECK(harvested.m_senderDepts.size() == 2);

    //选了「指定的部门」但还没选：提示去选
    BotRuleForm empty = form;
    empty.m_senderDepts.clear();
    RuleFormContext emptyCtx = ctx;
    emptyCtx.m_senderNames.clear();
    RemoveAllChildren(frame.m_content);
    BuildRuleForm(frame.m_content, empty, emptyCtx, font, kRuleContentWidth, controls);
    CHECK(controls.m_pickSenders != NULL);
    CHECK(HasLabelContaining(frame.m_content, L"组织架构"));

    //全部人：下拉框仍有三项，没有「选择…」
    BotRuleForm all = form;
    all.m_senderKind = bot_sender_all;
    RemoveAllChildren(frame.m_content);
    BuildRuleForm(frame.m_content, all, emptyCtx, font, kRuleContentWidth, controls);
    CHECK(controls.m_senderValues.size() == 3);
    CHECK(controls.m_pickSenders == NULL);
}

TEST_CASE("BOTL1 规则编辑窗按设计尺寸排得下", "[botdll]")
{
    HFONT font = WindowFont(kTestDpi);
    ScrollFrameControls frame;
    std::unique_ptr<balloonwjui::DuiVBox> root = BuildRuleWindowBody(font, frame);
    REQUIRE(frame.m_content != NULL);
    REQUIRE(frame.m_buttons.size() == 2);

    BotRuleForm form = RuleToForm(MakeDefaultRule(bot_owner_personal));
    form.m_senderKind = bot_sender_users;
    form.m_senderUsers.push_back(5302);
    form.m_windowOn = true;
    form.m_keywords = L"请假";
    RuleFormContext ctx;
    ctx.m_ownerType = bot_owner_personal;
    ctx.m_senderNames = L"李四";
    RuleFormControls controls;
    BuildRuleForm(frame.m_content, form, ctx, font, kRuleContentWidth, controls);
    CHECK(controls.m_keywords != NULL);
    CHECK(controls.m_presence[bot_presence_busy] != NULL);
    CHECK(controls.m_pickSenders != NULL);
    CHECK(controls.m_windowFrom != NULL);
    CHECK(controls.m_text != NULL);
    CHECK(controls.m_needAt == NULL);
    CHECK(controls.m_url == NULL);
    REQUIRE(controls.m_triggerValues.size() == 1);
    CHECK(frame.m_content->GetDesiredSize().cy > 0);

    const int contentHeight = kRuleWindowHeight - kBotTitleBarHeight;
    CheckFits(root.get(), kIdRuleOk, kRuleWindowWidth, contentHeight);
    CheckFits(root.get(), kIdRuleCancel, kRuleWindowWidth, contentHeight);

    //重建成群机器人的定时规则（每周）：关键词、在线状态不再有，星期复选框齐全；读回之后字段不变
    BotRuleForm group = RuleToForm(MakeDefaultRule(bot_owner_group));
    group.m_trigger = bot_trigger_schedule;
    group.m_scheduleKind = bot_schedule_weekly;
    group.m_weekdays[2] = true;
    RuleFormContext gctx;
    gctx.m_ownerType = bot_owner_group;
    gctx.m_allowExternal = true;
    RemoveAllChildren(frame.m_content);
    BuildRuleForm(frame.m_content, group, gctx, font, kRuleContentWidth, controls);
    CHECK(controls.m_keywords == NULL);
    CHECK(controls.m_presence[0] == NULL);
    CHECK(controls.m_weekdays[6] != NULL);
    CHECK(controls.m_windowOn == NULL);
    CHECK(controls.m_triggerValues.size() == 5);
    BotRuleForm harvested = group;
    harvested.m_weekdays[2] = false;
    HarvestRuleForm(controls, harvested);
    CHECK(harvested.m_weekdays[2]);
    CHECK(harvested.m_trigger == bot_trigger_schedule);
    CHECK(harvested.m_scheduleKind == bot_schedule_weekly);
    CheckFits(root.get(), kIdRuleOk, kRuleWindowWidth, contentHeight);
}

TEST_CASE("BOTL2 机器人编辑窗按设计尺寸排得下", "[botdll]")
{
    HFONT font = WindowFont(kTestDpi);
    const int contentHeight = kEditWindowHeight - kBotTitleBarHeight;
    for (int pass = 0; pass < 2; ++pass)
    {
        EditView view;
        view.m_ownerType = (pass == 0) ? bot_owner_group : bot_owner_personal;
        view.m_showWebhook = (pass == 0);
        view.m_name = L"值班提醒";
        view.m_webhookToken = L"0123456789abcdef0123456789abcdef";
        view.m_secret = L"fedcba9876543210fedcba9876543210";
        EditControls controls;
        std::unique_ptr<balloonwjui::DuiVBox> root = BuildEditBody(view, font, controls);
        REQUIRE(controls.m_save != NULL);
        REQUIRE(controls.m_rules != NULL);
        CHECK((controls.m_tryAt != NULL) == (pass == 0));
        CHECK((controls.m_tryPresence != NULL) == (pass == 1));
        const int before = controls.m_frame.m_content->GetDesiredSize().cy;
        std::vector<BotRule> rules;
        rules.push_back(MakeDefaultRule(view.m_ownerType));
        rules.push_back(MakeDefaultRule(view.m_ownerType));
        FillRuleList(controls, rules, view.m_ownerType, font);
        CHECK(controls.m_frame.m_content->GetDesiredSize().cy > before);
        CHECK(controls.m_rules->Children().size() == 2);
        CheckFits(root.get(), kIdEditSave, kEditWindowWidth, contentHeight);
        //判定结果显示后高度随之变化，清空后恢复
        const int withRules = controls.m_frame.m_content->GetDesiredSize().cy;
        SetTryResult(controls, L"命中规则「VPN」，会回复：\n重启客户端", false, font);
        CHECK(controls.m_frame.m_content->GetDesiredSize().cy > withRules);
        SetTryResult(controls, std::wstring(), false, font);
        CHECK(controls.m_frame.m_content->GetDesiredSize().cy == withRules);
    }

    //规则按钮 id 的编码与解码
    size_t index = 0;
    int button = 0;
    REQUIRE(ParseEditRuleButtonId(EditRuleButtonId(49, edit_rule_delete), index, button));
    CHECK(index == 49);
    CHECK(button == edit_rule_delete);
    CHECK(EditRuleButtonId(49, edit_rule_delete) <= kIdEditRuleLast);
    CHECK_FALSE(ParseEditRuleButtonId(kIdEditSave, index, button));
}

TEST_CASE("BOTL6 编辑窗：有对外地址时显示地址框，没有时不建", "[botdll]")
{
    HFONT font = WindowFont(kTestDpi);
    const int contentHeight = kEditWindowHeight - kBotTitleBarHeight;
    const std::wstring url = L"https://im.example.com:8443/bot/webhook.do";
    for (int pass = 0; pass < 2; ++pass)
    {
        EditView view;
        view.m_ownerType = bot_owner_group;
        view.m_showWebhook = true;
        view.m_name = L"值班提醒";
        view.m_webhookToken = L"0123456789abcdef0123456789abcdef";
        view.m_secret = L"fedcba9876543210fedcba9876543210";
        if (pass == 0)
            view.m_webhookUrl = url;
        EditControls controls;
        std::unique_ptr<balloonwjui::DuiVBox> root = BuildEditBody(view, font, controls);
        balloonwjui::DuiEdit* field = dynamic_cast<balloonwjui::DuiEdit*>(root->FindCtrlById(kIdEditWebhookUrl));
        if (pass == 0)
        {
            REQUIRE(field != NULL);
            CHECK(std::wstring(field->GetText()) == url);
        }
        else
        {
            CHECK(field == NULL);
        }
        CHECK(root->FindCtrlById(kIdEditToken) != NULL);
        CheckFits(root.get(), kIdEditSave, kEditWindowWidth, contentHeight);
    }
}

TEST_CASE("BOTL3 列表窗口按设计尺寸排得下", "[botdll]")
{
    HFONT font = WindowFont(kTestDpi);
    std::vector<FooterButtonSpec> buttons;
    const UINT kIds[] = { 8101, 8102, 8103 };
    for (size_t i = 0; i < sizeof(kIds) / sizeof(kIds[0]); ++i)
    {
        FooterButtonSpec spec;
        spec.m_text = L"新建群机器人";
        spec.m_id = kIds[i];
        spec.m_variant = balloonwjui::DuiButton::Variant::Default;
        buttons.push_back(spec);
    }
    ScrollFrameControls frame;
    std::unique_ptr<balloonwjui::DuiVBox> root = BuildScrollFrame(font, buttons, frame);
    REQUIRE(frame.m_buttons.size() == 3);
    const int empty = frame.m_content->GetDesiredSize().cy;
    BotRowView row;
    row.m_name = L"值班提醒";
    row.m_state = L"生效中";
    row.m_desc = L"每个工作日 09:00 提醒值班";
    row.m_detail = L"共 1 条规则\n每个工作日 09:00 发送消息";
    row.m_buttons.push_back(BotRowButton(L"编辑", 9200, true));
    row.m_buttons.push_back(BotRowButton(L"停用", 9201, true));
    row.m_buttons.push_back(BotRowButton(L"删除", 9202, false));
    AddListSection(frame.m_content, L"本群的群机器人（1 / 5）", font);
    AddBotRow(frame.m_content, row, font, kListContentWidth);
    AddListNote(frame.m_content, L"说明", kBotSubText, font, kListContentWidth);
    CHECK(frame.m_content->GetDesiredSize().cy > empty + kBotAvatarSize);
    CHECK(frame.m_content->FindCtrlById(9202) != NULL);
    CHECK_FALSE(frame.m_content->FindCtrlById(9202)->IsEnabled());
    CheckFits(root.get(), kIds[2], kListWindowWidth, kListWindowHeight - kBotTitleBarHeight);
}

TEST_CASE("BOTL4 列表换一批总高度相同的内容后，新的每一行仍然排了版", "[botdll]")
{
    HFONT font = WindowFont(kTestDpi);
    std::vector<FooterButtonSpec> buttons;
    FooterButtonSpec spec;
    spec.m_text = L"关闭";
    spec.m_id = 8111;
    spec.m_variant = balloonwjui::DuiButton::Variant::Default;
    buttons.push_back(spec);
    ScrollFrameControls frame;
    std::unique_ptr<balloonwjui::DuiVBox> root = BuildScrollFrame(font, buttons, frame);
    REQUIRE(frame.m_content != NULL);
    REQUIRE(frame.m_scroll != NULL);

    //第一批：一段说明、一个分组标题、一行机器人，按设计尺寸排一次版
    BotRowView row;
    row.m_name = L"IT助手";
    row.m_state = L"生效中";
    row.m_desc = L"回答 VPN 问题";
    row.m_detail = L"来自企业，管理员允许在本群关闭。";
    AddListNote(frame.m_content, L"每个群最多 5 个群机器人。", kBotSubText, font, kListContentWidth);
    AddListSection(frame.m_content, L"企业机器人（1）", font);
    AddBotRow(frame.m_content, row, font, kListContentWidth);
    const RECT rcWindow = { 0, 0, kListWindowWidth, kListWindowHeight - kBotTitleBarHeight };
    root->ForceLayout(rcWindow);
    RelayoutScroll(frame.m_scroll);
    const int firstHeight = frame.m_content->GetDesiredSize().cy;

    //第二批：结构与高度都不变，只换状态文字（与「在本群关闭」之后重建列表相同）
    RemoveAllChildren(frame.m_content);
    row.m_state = L"已在本群关闭";
    AddListNote(frame.m_content, L"每个群最多 5 个群机器人。", kBotSubText, font, kListContentWidth);
    AddListSection(frame.m_content, L"企业机器人（1）", font);
    AddBotRow(frame.m_content, row, font, kListContentWidth);
    RelayoutScroll(frame.m_scroll);
    REQUIRE(frame.m_content->GetDesiredSize().cy == firstHeight);

    //新建的每个子控件都要有矩形；没排版时矩形为空，窗口里什么都画不出来
    const std::vector<std::unique_ptr<balloonwjui::DuiControl>>& children = frame.m_content->Children();
    REQUIRE(children.size() == 3);
    for (size_t i = 0; i < children.size(); ++i)
    {
        const RECT rc = children[i]->GetRect();
        CHECK(rc.right - rc.left > 0);
        CHECK(rc.bottom - rc.top > 0);
    }
}

TEST_CASE("BOTL5 列表换一批更高的内容后，新的每一行仍然排了版", "[botdll]")
{
    HFONT font = WindowFont(kTestDpi);
    std::vector<FooterButtonSpec> buttons;
    FooterButtonSpec spec;
    spec.m_text = L"关闭";
    spec.m_id = 8121;
    spec.m_variant = balloonwjui::DuiButton::Variant::Default;
    buttons.push_back(spec);
    ScrollFrameControls frame;
    std::unique_ptr<balloonwjui::DuiVBox> root = BuildScrollFrame(font, buttons, frame);
    REQUIRE(frame.m_content != NULL);

    //第一批：企业机器人一行，本群的群机器人为空
    BotRowView row;
    row.m_name = L"IT助手";
    row.m_state = L"已在本群关闭";
    row.m_desc = L"回答 VPN 问题";
    AddListSection(frame.m_content, L"企业机器人（1）", font);
    AddBotRow(frame.m_content, row, font, kListContentWidth);
    AddListSection(frame.m_content, L"本群的群机器人（0 / 5）", font);
    AddListNote(frame.m_content, L"还没有群机器人。", kBotFaint, font, kListContentWidth);
    const RECT rcWindow = { 0, 0, kListWindowWidth, kListWindowHeight - kBotTitleBarHeight };
    root->ForceLayout(rcWindow);
    RelayoutScroll(frame.m_scroll);
    const int firstHeight = frame.m_content->GetDesiredSize().cy;

    //第二批：本群多了一个群机器人
    RemoveAllChildren(frame.m_content);
    AddListSection(frame.m_content, L"企业机器人（1）", font);
    AddBotRow(frame.m_content, row, font, kListContentWidth);
    AddListSection(frame.m_content, L"本群的群机器人（1 / 5）", font);
    BotRowView groupRow;
    groupRow.m_name = L"值班提醒";
    groupRow.m_state = L"生效中";
    groupRow.m_detail = L"共 1 条规则";
    AddBotRow(frame.m_content, groupRow, font, kListContentWidth);
    RelayoutScroll(frame.m_scroll);
    REQUIRE(frame.m_content->GetDesiredSize().cy > firstHeight);

    const std::vector<std::unique_ptr<balloonwjui::DuiControl>>& children = frame.m_content->Children();
    REQUIRE(children.size() == 4);
    for (size_t i = 0; i < children.size(); ++i)
    {
        const RECT rc = children[i]->GetRect();
        CHECK(rc.right - rc.left > 0);
        CHECK(rc.bottom - rc.top > 0);
    }
}

namespace {

//控件及其全部子控件都有非空矩形时返回 true；没排过版的控件矩形为空，窗口里画不出来
bool AllLaidOut(balloonwjui::DuiControl* ctrl)
{
    const RECT rc = ctrl->GetRect();
    if (rc.right - rc.left <= 0 || rc.bottom - rc.top <= 0)
        return false;
    const std::vector<std::unique_ptr<balloonwjui::DuiControl>>& children = ctrl->Children();
    for (size_t i = 0; i < children.size(); ++i)
    {
        if (!AllLaidOut(children[i].get()))
            return false;
    }
    return true;
}

} // namespace

//编辑一条规则后列表整体重建，摘要行数不变时规则列表的总高度也不变；规则列表嵌在滚动内容里面一层，
//RelayoutScroll 对内容 ForceLayout 时给规则列表调的是 SetRect，矩形没变就直接返回，新建的卡片没有排版，那一行变成空白
TEST_CASE("BOTL8 编辑规则后重建的规则列表（总高度不变）每张卡片都排了版", "[botdll]")
{
    HFONT font = WindowFont(kTestDpi);
    EditView view;
    view.m_ownerType = bot_owner_personal;
    view.m_name = L"请假自动回复";
    EditControls controls;
    std::unique_ptr<balloonwjui::DuiVBox> root = BuildEditBody(view, font, controls);
    REQUIRE(controls.m_rules != NULL);

    //第一批：两条规则，按设计尺寸排一次版
    std::vector<BotRule> rules;
    rules.push_back(MakeDefaultRule(bot_owner_personal));
    rules.push_back(MakeDefaultRule(bot_owner_personal));
    FillRuleList(controls, rules, bot_owner_personal, font);
    const RECT rcWindow = { 0, 0, kEditWindowWidth, kEditWindowHeight - kBotTitleBarHeight };
    root->ForceLayout(rcWindow);
    RelayoutScroll(controls.m_frame.m_scroll);
    const RECT rcBefore = controls.m_rules->GetRect();
    REQUIRE(controls.m_rules->Children().size() == 2);
    for (size_t i = 0; i < controls.m_rules->Children().size(); ++i)
    {
        CHECK(AllLaidOut(controls.m_rules->Children()[i].get()));
    }

    //第二批：打开第一条规则编辑、点确定（内容未改，摘要行数与总高度都不变），与 CBotEditWindow::OnRuleEdited 相同的调用
    FillRuleList(controls, rules, bot_owner_personal, font);
    const RECT rcAfter = controls.m_rules->GetRect();
    REQUIRE(rcAfter.bottom - rcAfter.top == rcBefore.bottom - rcBefore.top);
    REQUIRE(controls.m_rules->Children().size() == 2);
    for (size_t i = 0; i < controls.m_rules->Children().size(); ++i)
    {
        INFO("rule card " << i);
        CHECK(AllLaidOut(controls.m_rules->Children()[i].get()));
    }
}
