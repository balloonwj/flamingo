/**
 *  flamingoAdmin —— AnnouncementPreviewPanel 单元测试。
 *
 *  覆盖 8 条用例,验证"自绘 PaintPreview → DuiVBox + DuiLabel(selectable) +
 *  DuiRichEdit"迁移后,SetAnnouncement 仍能把 Announcement 的 7 个★强项
 *  文字字段正确转写到对应子控件,并按 m_scheduled / m_pinned / m_status 切换
 *  状态卡和按钮文字。
 *
 *  所有用例都在【无 host】下裸构造 PreviewPanel —— DuiControl::Invalidate /
 *  SetVisible / DuiLabel::SetText / DuiRichEdit::SetText 在 m_pHost ==
 *  nullptr 时都能正常工作 —— 无窗口富文本控件构造完即可读写文本,
 *  不依赖任何窗口,也没有"创建"这一步。本文件【绝不调用
 *  OnPaint】,因此不必拿 HDC、不必 init gdiplus。
 *
 *  断言走 panel.Children()[i] + 裸指针(通过 helper 暴露的 5 个 m_btn / 8 个
 *  m_*Label / m_*Slot 等)读 GetText / IsVisible。由于 PreviewPanel 的字段都
 *  是 private,这里采用"间接路径":通过已 layout 的子控件树按下标路由,
 *  跟 StatTile 测试同 pattern。
 *
 *  balloonwj@qq.com   2026-05-26
 */
#include "stdafx.h"
#include "Tests/TestFramework.h"
#include "Screens/AnnouncementPreviewPanel.h"
#include "Mock/MockData.h"

#include "Controls/Basic/DuiLabel.h"
#include "Controls/Basic/DuiButton.h"
#include "Controls/Input/DuiRichEdit.h"
#include "Controls/Layout/DuiLayout.h"

namespace flamingoadmin { namespace test {

namespace {

// PreviewPanel 子控件树的固定下标(与 AnnouncementPreviewPanel 构造里 AddChild
// 的顺序一一对应,详见 AnnouncementPreviewPanel.cpp)。
//   0: HeaderRow (DuiHBox) —— 5 个子:severityChipSlot / pinnedChipSlot /
//        scheduledChipSlot / spacer / dateLabel
//   1: titleEdit (DuiRichEdit)
//   2: SenderRow (DuiHBox) —— 2 个子:avatarSlot / senderMetaLabel
//   3: dividerSlot
//   4: bodyEdit (DuiRichEdit) —— 正文直接占一行,不再套 HBox 挂手写滚动条:
//        改用无窗口控件之后,滚动条是它自带的覆盖式细条,浮在内容之上。
//   5: progressCard (DuiVBox) —— 内含 topRow(2 label) + progressBarSlot
//   6: scheduleCard (DuiVBox) —— 内含 row(iconSlot + texts vbox(top/sub labels))
//   7: ActionRow (DuiHBox) —— 6 button + 1 spacer
//        (编辑0 / 复制1 / 重新推送2 / 置顶3 / 删除公告4 / spacer5 / 撤回6)
//   8: emptyHint (DuiLabel)
const size_t kIdxHeaderRow    = 0;
const size_t kIdxTitleEdit    = 1;
const size_t kIdxSenderRow    = 2;
const size_t kIdxDividerSlot  = 3;
const size_t kIdxBodyEdit     = 4;
const size_t kIdxProgressCard = 5;
const size_t kIdxScheduleCard = 6;
const size_t kIdxActionRow    = 7;
const size_t kIdxEmptyHint    = 8;

// HeaderRow 内子下标。
const size_t kHdrIdxSeverity  = 0;
const size_t kHdrIdxPinned    = 1;
const size_t kHdrIdxScheduled = 2;
const size_t kHdrIdxDate      = 4;  // index 3 是弹性 spacer

// ActionRow 内子下标(前 5 个左对齐按钮 + spacer + 右对齐撤回)。
const size_t kActIdxEdit      = 0;
const size_t kActIdxCopy      = 1;
const size_t kActIdxRepush    = 2;
const size_t kActIdxTogglePin = 3;
const size_t kActIdxDeleteAnn = 4;  // 「删除公告」(任意状态物理删除,Danger)
const size_t kActIdxRecall    = 6;  // 「撤回」(仅已发布显示);index 5 是弹性 spacer

// 取 PreviewPanel 第 idx 个直接子。
balloonwjui::DuiControl* GetChild(const AnnouncementPreviewPanel& panel, size_t idx)
{
    const auto& children = panel.Children();
    if (idx >= children.size())
    {
        return nullptr;
    }
    return children[idx].get();
}

// 取 PreviewPanel 第 idx 个直接子的第 subIdx 个孙子。
balloonwjui::DuiControl* GetGrandChild(const AnnouncementPreviewPanel& panel,
                                       size_t idx, size_t subIdx)
{
    balloonwjui::DuiControl* row = GetChild(panel, idx);
    if (row == nullptr)
    {
        return nullptr;
    }
    if (subIdx >= row->Children().size())
    {
        return nullptr;
    }
    return row->Children()[subIdx].get();
}

// 构造一个已发布(m_scheduled=false)样本公告供测试用。
Announcement MakePublishedSample()
{
    Announcement a;
    a.m_id        = _T("ann-001");
    a.m_title     = _T("平台维护通知");
    a.m_body      = _T("各位用户,系统将于本周六凌晨进行维护升级,期间短信会延迟送达。");
    a.m_audience  = _T("全平台");
    a.m_severity  = Severity::Warn;
    a.m_sender    = _T("张三");
    a.m_pinned    = true;
    a.m_scheduled = false;
    a.m_sentAt    = _T("2026-05-21 10:00");
    a.m_read      = 320;
    a.m_total     = 1024;
    a.m_status    = 1;
    return a;
}

// 构造一个定时(m_scheduled=true)样本公告供测试用。
Announcement MakeScheduledSample()
{
    Announcement a;
    a.m_id        = _T("ann-002");
    a.m_title     = _T("会员限时活动");
    a.m_body      = _T("活动详情见正文……");
    a.m_audience  = _T("付费会员");
    a.m_severity  = Severity::Info;
    a.m_sender    = _T("市场部");
    a.m_pinned    = false;
    a.m_scheduled = true;
    a.m_sentAt    = _T("2026-06-01 09:00");
    a.m_read      = 0;
    a.m_total     = 2048;
    a.m_status    = 0;
    return a;
}

} // anonymous namespace

void RunAllAnnouncementPreviewPanelTests()
{
    // ---- 用例 1:SetAnnouncement(nullptr) 时只有 emptyHint 可见,其它隐藏 ----
    BeginCase("AnnPreview.SetAnnouncement_Null_HidesAll:nullptr → emptyHint 唯一可见");
    {
        AnnouncementPreviewPanel panel;
        panel.SetAnnouncement(nullptr);

        balloonwjui::DuiControl* hint = GetChild(panel, kIdxEmptyHint);
        FA_EXPECT_NOTNULL(hint);
        FA_EXPECT_TRUE(hint->IsVisible());

        // 其它内容子都应隐藏(HeaderRow / titleEdit / SenderRow / divider /
        // bodyEdit / progressCard / scheduleCard / ActionRow)。
        FA_EXPECT_FALSE(GetChild(panel, kIdxHeaderRow)->IsVisible());
        FA_EXPECT_FALSE(GetChild(panel, kIdxTitleEdit)->IsVisible());
        FA_EXPECT_FALSE(GetChild(panel, kIdxSenderRow)->IsVisible());
        FA_EXPECT_FALSE(GetChild(panel, kIdxDividerSlot)->IsVisible());
        FA_EXPECT_FALSE(GetChild(panel, kIdxBodyEdit)->IsVisible());
        FA_EXPECT_FALSE(GetChild(panel, kIdxProgressCard)->IsVisible());
        FA_EXPECT_FALSE(GetChild(panel, kIdxScheduleCard)->IsVisible());
        FA_EXPECT_FALSE(GetChild(panel, kIdxActionRow)->IsVisible());
    }

    // ---- 用例 2:已发布公告 → 7 个★强项字段 GetText 正确,progressCard 可见 scheduleCard 隐藏 ----
    BeginCase("AnnPreview.SetAnnouncement_Published_StoresTextFields:7 字段 + 进度卡可见");
    {
        AnnouncementPreviewPanel panel;
        Announcement a = MakePublishedSample();
        panel.SetAnnouncement(&a);

        // 标题 / 正文 = RichEdit GetText。正文现在包在 HBox 里(下标 4 是
        // [正文 | 悬浮滚动条] 行),正文 RichEdit 是该行第 0 个孙子。
        balloonwjui::DuiRichEdit* titleEdit =
            dynamic_cast<balloonwjui::DuiRichEdit*>(GetChild(panel, kIdxTitleEdit));
        balloonwjui::DuiRichEdit* bodyEdit =
            dynamic_cast<balloonwjui::DuiRichEdit*>(GetChild(panel, kIdxBodyEdit));
        FA_EXPECT_NOTNULL(titleEdit);
        FA_EXPECT_NOTNULL(bodyEdit);
        FA_EXPECT_STREQ(titleEdit->GetText(), a.m_title);
        FA_EXPECT_STREQ(bodyEdit->GetText(),  a.m_body);

        // 日期 = HeaderRow[4] label (已发布:直接 m_sentAt)
        balloonwjui::DuiLabel* dateLbl = dynamic_cast<balloonwjui::DuiLabel*>(
            GetGrandChild(panel, kIdxHeaderRow, kHdrIdxDate));
        FA_EXPECT_NOTNULL(dateLbl);
        FA_EXPECT_STREQ(dateLbl->GetText(), a.m_sentAt);

        // 发送者 + 受众 = SenderRow[1] label,格式 "sender    ·    受众:audience"
        balloonwjui::DuiLabel* metaLbl = dynamic_cast<balloonwjui::DuiLabel*>(
            GetGrandChild(panel, kIdxSenderRow, 1));
        FA_EXPECT_NOTNULL(metaLbl);
        CString expectMeta;
        expectMeta.Format(_T("%s    ·    受众:%s"),
                          static_cast<LPCTSTR>(a.m_sender),
                          static_cast<LPCTSTR>(a.m_audience));
        FA_EXPECT_STREQ(metaLbl->GetText(), expectMeta);

        // 进度数字 = ProgressCard.topRow[1] label,格式 "320 / 1024 人(31%)"
        balloonwjui::DuiControl* progressCard = GetChild(panel, kIdxProgressCard);
        FA_EXPECT_NOTNULL(progressCard);
        FA_EXPECT_TRUE(progressCard->IsVisible());
        balloonwjui::DuiControl* topRow = progressCard->Children()[0].get();
        balloonwjui::DuiLabel* rightLbl = dynamic_cast<balloonwjui::DuiLabel*>(
            topRow->Children()[1].get());
        FA_EXPECT_NOTNULL(rightLbl);
        const int pct = a.m_total > 0 ? a.m_read * 100 / a.m_total : 0;
        CString expectRt;
        expectRt.Format(_T("%d / %d 人(%d%%)"), a.m_read, a.m_total, pct);
        FA_EXPECT_STREQ(rightLbl->GetText(), expectRt);

        // 调度卡隐藏
        FA_EXPECT_FALSE(GetChild(panel, kIdxScheduleCard)->IsVisible());
    }

    // ---- 用例 3:定时公告 → dateLabel 是 "计划于 X 发送",调度卡可见 ----
    BeginCase("AnnPreview.SetAnnouncement_Scheduled_StoresTextFields:定时格式 + 调度卡");
    {
        AnnouncementPreviewPanel panel;
        Announcement a = MakeScheduledSample();
        panel.SetAnnouncement(&a);

        // 日期 = "计划于 m_sentAt 发送"
        balloonwjui::DuiLabel* dateLbl = dynamic_cast<balloonwjui::DuiLabel*>(
            GetGrandChild(panel, kIdxHeaderRow, kHdrIdxDate));
        FA_EXPECT_NOTNULL(dateLbl);
        CString expectDate;
        expectDate.Format(_T("计划于 %s 发送"), static_cast<LPCTSTR>(a.m_sentAt));
        FA_EXPECT_STREQ(dateLbl->GetText(), expectDate);

        // 调度卡可见,进度卡隐藏
        FA_EXPECT_TRUE(GetChild(panel, kIdxScheduleCard)->IsVisible());
        FA_EXPECT_FALSE(GetChild(panel, kIdxProgressCard)->IsVisible());

        // 调度副标题 = "将于 m_sentAt 推送至 m_total 名用户"
        // ScheduleCard.row[1=texts vbox].[1=sub label]
        balloonwjui::DuiControl* scheduleCard = GetChild(panel, kIdxScheduleCard);
        balloonwjui::DuiControl* row     = scheduleCard->Children()[0].get();
        balloonwjui::DuiControl* texts   = row->Children()[1].get();
        balloonwjui::DuiLabel*   subLbl  = dynamic_cast<balloonwjui::DuiLabel*>(
            texts->Children()[1].get());
        FA_EXPECT_NOTNULL(subLbl);
        CString expectSub;
        expectSub.Format(_T("将于 %s 推送至 %d 名用户"),
                         static_cast<LPCTSTR>(a.m_sentAt), a.m_total);
        FA_EXPECT_STREQ(subLbl->GetText(), expectSub);
    }

    // ---- 用例 4:置顶按钮文字按 m_pinned 切换 ----
    BeginCase("AnnPreview.TogglePin_ButtonLabelSwitches:m_pinned → 置顶 / 取消置顶");
    {
        AnnouncementPreviewPanel panel;
        Announcement a = MakePublishedSample();

        // 默认 m_pinned=true → "取消置顶"
        a.m_pinned = true;
        panel.SetAnnouncement(&a);
        balloonwjui::DuiButton* btnPin = dynamic_cast<balloonwjui::DuiButton*>(
            GetGrandChild(panel, kIdxActionRow, kActIdxTogglePin));
        FA_EXPECT_NOTNULL(btnPin);
        FA_EXPECT_STREQ(btnPin->GetText(), _T("取消置顶"));

        a.m_pinned = false;
        panel.SetAnnouncement(&a);
        FA_EXPECT_STREQ(btnPin->GetText(), _T("置顶"));
    }

    // ---- 用例 5:撤回按钮按 m_status 显隐;删除公告按钮始终在 ----
    BeginCase("AnnPreview.Status_RecallVisibilityAndDeleteAlways:撤回仅已发布显示、删除公告恒在");
    {
        AnnouncementPreviewPanel panel;
        Announcement a = MakePublishedSample();

        balloonwjui::DuiButton* btnRecall = dynamic_cast<balloonwjui::DuiButton*>(
            GetGrandChild(panel, kIdxActionRow, kActIdxRecall));
        balloonwjui::DuiButton* btnDelAnn = dynamic_cast<balloonwjui::DuiButton*>(
            GetGrandChild(panel, kIdxActionRow, kActIdxDeleteAnn));
        FA_EXPECT_NOTNULL(btnRecall);
        FA_EXPECT_NOTNULL(btnDelAnn);

        // 「删除公告」按钮:文案固定,任意状态都在。
        FA_EXPECT_STREQ(btnDelAnn->GetText(), _T("删除公告"));

        // status=1(已发布) → 撤回按钮显示、文案"撤回"。
        a.m_status = 1;
        panel.SetAnnouncement(&a);
        FA_EXPECT_STREQ(btnRecall->GetText(), _T("撤回"));
        FA_EXPECT_TRUE(btnRecall->IsVisible());

        // status=0(草稿) → 撤回按钮隐藏。
        a.m_status = 0;
        panel.SetAnnouncement(&a);
        FA_EXPECT_FALSE(btnRecall->IsVisible());

        // status=2(已撤回) → 撤回按钮隐藏。
        a.m_status = 2;
        panel.SetAnnouncement(&a);
        FA_EXPECT_FALSE(btnRecall->IsVisible());
    }

    // ---- 用例 6:已置顶 / 定时徽章可见性独立 ----
    BeginCase("AnnPreview.PinnedAndScheduled_ChipsVisibility:4 组排列徽章独立显隐");
    {
        AnnouncementPreviewPanel panel;
        Announcement a = MakePublishedSample();

        balloonwjui::DuiControl* pinSlot = GetGrandChild(panel, kIdxHeaderRow, kHdrIdxPinned);
        balloonwjui::DuiControl* schSlot = GetGrandChild(panel, kIdxHeaderRow, kHdrIdxScheduled);
        FA_EXPECT_NOTNULL(pinSlot);
        FA_EXPECT_NOTNULL(schSlot);

        // (false, false)
        a.m_pinned = false; a.m_scheduled = false;
        panel.SetAnnouncement(&a);
        FA_EXPECT_FALSE(pinSlot->IsVisible());
        FA_EXPECT_FALSE(schSlot->IsVisible());

        // (true, false)
        a.m_pinned = true; a.m_scheduled = false;
        panel.SetAnnouncement(&a);
        FA_EXPECT_TRUE(pinSlot->IsVisible());
        FA_EXPECT_FALSE(schSlot->IsVisible());

        // (false, true)
        a.m_pinned = false; a.m_scheduled = true;
        panel.SetAnnouncement(&a);
        FA_EXPECT_FALSE(pinSlot->IsVisible());
        FA_EXPECT_TRUE(schSlot->IsVisible());

        // (true, true)
        a.m_pinned = true; a.m_scheduled = true;
        panel.SetAnnouncement(&a);
        FA_EXPECT_TRUE(pinSlot->IsVisible());
        FA_EXPECT_TRUE(schSlot->IsVisible());
    }

    // ---- 用例 7:重复 SetAnnouncement 不丢子,文字按第二次值刷新 ----
    BeginCase("AnnPreview.Repeated_RefreshesAllFields:连调 2 次取第二次值");
    {
        AnnouncementPreviewPanel panel;
        Announcement a1 = MakePublishedSample();
        Announcement a2 = MakePublishedSample();
        a2.m_title = _T("新标题");
        a2.m_body  = _T("新正文");
        a2.m_sentAt = _T("2026-05-22 12:00");
        a2.m_sender = _T("李四");

        panel.SetAnnouncement(&a1);
        const size_t childrenCountAfter1 = panel.Children().size();
        panel.SetAnnouncement(&a2);
        const size_t childrenCountAfter2 = panel.Children().size();
        FA_EXPECT_EQ(childrenCountAfter1, childrenCountAfter2);

        balloonwjui::DuiRichEdit* titleEdit =
            dynamic_cast<balloonwjui::DuiRichEdit*>(GetChild(panel, kIdxTitleEdit));
        balloonwjui::DuiRichEdit* bodyEdit =
            dynamic_cast<balloonwjui::DuiRichEdit*>(GetChild(panel, kIdxBodyEdit));
        FA_EXPECT_STREQ(titleEdit->GetText(), a2.m_title);
        FA_EXPECT_STREQ(bodyEdit->GetText(),  a2.m_body);
    }

    // ---- 用例 8:有效公告 → nullptr 应回到 EmptyHint 状态(双向可逆) ----
    BeginCase("AnnPreview.SetAnnouncement_BackToNull_RestoresEmptyHint:有效 → nullptr 回到占位");
    {
        AnnouncementPreviewPanel panel;
        Announcement a = MakePublishedSample();

        panel.SetAnnouncement(&a);
        balloonwjui::DuiControl* hint = GetChild(panel, kIdxEmptyHint);
        FA_EXPECT_NOTNULL(hint);
        FA_EXPECT_FALSE(hint->IsVisible());   // 有效公告时占位隐藏

        panel.SetAnnouncement(nullptr);
        FA_EXPECT_TRUE(hint->IsVisible());    // nullptr 后占位再次可见
        FA_EXPECT_FALSE(GetChild(panel, kIdxTitleEdit)->IsVisible());
    }
}

}} // namespace flamingoadmin::test
