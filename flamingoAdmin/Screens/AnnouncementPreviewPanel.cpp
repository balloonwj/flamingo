/**
 *  flamingoAdmin —— 公告中心右栏「公告详情」面板 AnnouncementPreviewPanel 实现。
 *
 *  balloonwj@qq.com   2026-05-26
 */
#include "stdafx.h"
#include "Screens/AnnouncementPreviewPanel.h"
#include "UiIds.h"
#include "Theme/AdminTheme.h"
#include "Widgets/UiHelpers.h"
#include "Widgets/CommonWidgets.h"
#include "Widgets/IconRenderer.h"

#include "Controls/Basic/DuiLabel.h"
#include "Controls/Basic/DuiButton.h"
#include "Controls/Input/DuiRichEdit.h"

using namespace balloonwjui;

namespace flamingoadmin {

namespace {

// ---- 布局常量(96 dpi 逻辑像素;与原 PaintPreview 一致) ----
const int kPanelPadH    = 30;   // 面板左右内距(原 kPvPad)
const int kPanelPadTop  = 30;   // 面板顶部内距(原 kPvPad)
const int kPanelPadBot  = 30;   // 面板底部内距
const int kHeaderRowH   = 22;   // 顶行高度(徽章 + 日期)
const int kHeaderRowGap = 8;    // 顶行内徽章 / 日期之间间距
const int kSeverityChipW = 76;  // 级别徽章预估宽度(8px+点6+6+文字+8px)
const int kPinnedChipW   = 60;  // "已置顶"徽章预估宽度
const int kScheduledChipW = 72; // "定时发送"徽章预估宽度
const int kDateW         = 240; // 日期文字最大宽度(右对齐)
const int kTitleBottomGap = 12; // 标题与发送者行之间间距
// 单行标题的高度富余量（像素）：DrawText 量出的行高与 RichEdit 实际行高
// 略有出入，多给几像素免得字形下沿被切。
const int kTitleSingleLinePad = 6;
const int kSenderRowH    = 22;  // 发送者行高度
const int kAvatarSize    = 22;  // 头像尺寸
const int kSenderRowGap  = 8;   // 头像与文字间距
const int kSenderBottomGap = 16; // 发送者行与分隔线间距
const int kDividerH      = 1;   // 分隔线高度
const int kDividerBottomGap = 16; // 分隔线与正文间距
const int kBodyBottomGap = 20;  // 正文与状态卡间距
const int kProgressCardH = 62;  // 进度卡高度
const int kScheduleCardH = 70;  // 调度卡高度
const int kStatusCardBottomGap = 16; // 状态卡与按钮行间距
const int kActionRowH    = 34;  // 按钮行高度
const int kActionBtnGap  = 8;   // 按钮间距
const int kActionBtnW    = 96;  // 按钮预估宽度
const int kProgressCardPadH = 18; // 进度卡左右内距
const int kProgressCardPadV = 14; // 进度卡上下内距
const int kProgressBarH     = 8;  // 进度条高度
const int kProgressBarGap   = 8;  // 进度条与上方文字间距
const int kScheduleIconBoxW = 40; // 调度卡图标外框尺寸

// ---- 字号 / 颜色 ----
const int kTitlePt = theme::kFontSizeHeading + 4;  // 标题(20pt)
const int kEmptyHintPt = theme::kFontSizeTitle;    // 未选中占位字号

// 公告严重级别 → 徽章文案(与 AnnouncementsScreen.cpp 内部 helper 一致)。
LPCTSTR SeverityChipLabel(Severity s)
{
    switch (s)
    {
    case Severity::Warn:
        return _T("提醒");
    case Severity::Danger:
        return _T("安全公告");
    case Severity::Info:
        return _T("通知");
    case Severity::Normal:
    default:
        return _T("通知");
    }
}

// 量算一段 word-wrap 文字在给定宽度下的高度(像素)。
//   text:待量算的文字;
//   font:字体(调用方持有所有权);
//   width:可用宽度(像素);
//   返回:换行排版后的总高度(像素)。
int MeasureWrappedH(LPCTSTR text, HFONT font, int width)
{
    HDC   dc = ::GetDC(nullptr);
    HFONT of = static_cast<HFONT>(::SelectObject(dc, font));
    RECT  rc = { 0, 0, width, 0 };
    ::DrawText(dc, text, -1, &rc,
               DT_LEFT | DT_TOP | DT_WORDBREAK | DT_NOPREFIX | DT_CALCRECT);
    ::SelectObject(dc, of);
    ::ReleaseDC(nullptr, dc);
    return rc.bottom - rc.top;
}

// 创建一个 selectable 单行 DuiLabel(已设字体 / 颜色 / 对齐)。
//   text:初始文字(可空);
//   font:字体(调用方持有);
//   color:文字色;
//   dtAlign:DT_LEFT / DT_RIGHT 等水平对齐(垂直默认 VCENTER + SINGLELINE)。
std::unique_ptr<DuiLabel> MakeSelectableLabel(LPCTSTR text, HFONT font,
                                              COLORREF color, DWORD dtAlign)
{
    std::unique_ptr<DuiLabel> lbl(new DuiLabel());
    lbl->SetText(text);
    lbl->SetFont(font);
    lbl->SetTextColor(color);
    lbl->SetTextAlign(dtAlign | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    lbl->SetSelectable(true);
    return lbl;
}

// 创建一个空 DuiControl 占位(本身不画任何东西,只为给 OnPaint 装饰
// 阶段保留一块矩形 —— PreviewPanel 之后通过裸指针拿它的 m_rcItem 在
// 该位置画徽章 / 头像 / 进度条 / 图标底等)。
std::unique_ptr<DuiControl> MakeSlot()
{
    return std::unique_ptr<DuiControl>(new DuiControl());
}

// 创建一个只读多行可选富文本控件(已设字体 / 字色 / 无边框 / 无光标)。
//   font:默认字体(调用方持有);
//   color:文字色。
std::unique_ptr<DuiRichEdit> MakeReadOnlyRichEdit(HFONT font, COLORREF color)
{
    std::unique_ptr<DuiRichEdit> re(new DuiRichEdit());
    re->SetMultiLine(true);
    re->SetWordWrap(true);
    re->SetReadOnly(true);
    re->SetShowBorder(false);       // 与父卡片底融合,不画输入框边框
    // 只读展示区不显示光标,免得看着像可以编辑。
    // 注意**不能**改用 SetFocusable(false):那样文字会变得选不中
    // (排版引擎在拖选过程中需要控件持有焦点),而这里要的正是能选中复制。
    re->SetShowCaret(false);
    // 高度由外层布局决定,不随内容长高 —— 面板各段的位置是算好的。
    re->SetAutoGrow(false);
    re->SetDefaultFontFromHFONT(font);
    re->SetTextColor(color);
    re->SetBackgroundColor(theme::kInk0);  // 与卡片白底一致
    re->SetMargins(0, 0, 0, 0);     // 与卡片 padding 衔接,不再二次缩进
    return re;
}

// 创建一个操作按钮(Default 视觉,文字 + ctrlId,默认 NotifyParent DUIN_CLICK)。
std::unique_ptr<DuiButton> MakeActionButton(LPCTSTR text, UINT ctrlId,
                                            DuiButton::Variant variant)
{
    std::unique_ptr<DuiButton> btn(new DuiButton());
    btn->SetText(text);
    btn->SetCtrlId(ctrlId);
    btn->SetVariant(variant);
    btn->SetTextPointSize(theme::kFontSizeNormal);
    return btn;
}

} // anonymous namespace

AnnouncementPreviewPanel::AnnouncementPreviewPanel()
{
    // ---- 卡片底:开 DuiVBox 卡片样式 setter,基类 OnPaint 自动画 ----
    SetBgColor      (theme::kInk0);          // 白色卡片底
    SetCornerRadius (0);                     // 预览区在 AnnouncementsScreen 内部铺满,无需圆角(与历史一致)
    SetBorderColor  (CLR_INVALID);           // 不画描边
    SetBorderWidth  (0.0f);

    // ---- 容器排布:左右内距 30、上下内距 30;子之间用各自 marginT 表达
    //      不均匀间距,SetGap=0 简化模型。 ----
    SetPadding(kPanelPadH, kPanelPadTop, kPanelPadH, kPanelPadBot);
    SetGap(0);

    // ====================================================================
    // 子控件 0:HeaderRow(顶行)
    //   级别徽章占位 / 已置顶占位 / 定时发送占位 / 弹性占位 / 日期 label
    // ====================================================================
    std::unique_ptr<DuiHBox> headerRow(new DuiHBox());
    headerRow->SetGap(kHeaderRowGap);
    {
        std::unique_ptr<DuiControl> sevSlot = MakeSlot();
        m_severityChipSlot = sevSlot.get();
        headerRow->AddChild(std::move(sevSlot), Hint().Fixed(kSeverityChipW));

        std::unique_ptr<DuiControl> pinSlot = MakeSlot();
        m_pinnedChipSlot = pinSlot.get();
        headerRow->AddChild(std::move(pinSlot), Hint().Fixed(kPinnedChipW));

        std::unique_ptr<DuiControl> schSlot = MakeSlot();
        m_scheduledChipSlot = schSlot.get();
        headerRow->AddChild(std::move(schSlot), Hint().Fixed(kScheduledChipW));

        // 弹性占位 —— 把右侧 dateLabel 推到最右
        std::unique_ptr<DuiControl> spacer = MakeSlot();
        headerRow->AddChild(std::move(spacer), Hint().Weight(1));

        std::unique_ptr<DuiLabel> dateLbl = MakeSelectableLabel(
            _T(""), ui::UiFont(theme::kFontSizeSmall, false),
            theme::kInk400, DT_RIGHT);
        m_dateLabel = dateLbl.get();
        headerRow->AddChild(std::move(dateLbl), Hint().Fixed(kDateW));
    }
    AddChild(std::move(headerRow), Hint().Fixed(kHeaderRowH).Margin(0, 0, 0, 12));

    // ====================================================================
    // 子控件 1:标题(只读多行可复制 RichEdit)
    //   高度在 Layout 时按文本 + 当前宽度量算后用 SetHint 更新。
    // ====================================================================
    std::unique_ptr<DuiRichEdit> titleEdit = MakeReadOnlyRichEdit(
        ui::UiFont(kTitlePt, true), theme::kInk700);
    //标题固定单行：换行版靠 DrawText 量算高度，而 RichEdit 的真实行高与之有出入，
    //两行标题经常差几像素、末行被切掉半截（见 snapshots/105040.png）。单行则高度
    //恒定、不依赖量算，超长部分直接被控件矩形截断，仍可选中复制。
    titleEdit->SetMultiLine(false);
    titleEdit->SetWordWrap(false);
    m_titleEdit = titleEdit.get();
    AddChild(std::move(titleEdit), Hint().Fixed(0).Margin(0, 0, 0, kTitleBottomGap));

    // ====================================================================
    // 子控件 2:SenderRow(头像占位 + "发送者  ·  受众:xxx")
    // ====================================================================
    std::unique_ptr<DuiHBox> senderRow(new DuiHBox());
    senderRow->SetGap(kSenderRowGap);
    {
        std::unique_ptr<DuiControl> avSlot = MakeSlot();
        m_avatarSlot = avSlot.get();
        senderRow->AddChild(std::move(avSlot), Hint().Fixed(kAvatarSize));

        std::unique_ptr<DuiLabel> meta = MakeSelectableLabel(
            _T(""), ui::UiFont(theme::kFontSizeNormal, false),
            theme::kInk500, DT_LEFT);
        meta->SetTextAlign(DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX
                           | DT_END_ELLIPSIS);
        m_senderMetaLabel = meta.get();
        senderRow->AddChild(std::move(meta), Hint().Weight(1));
    }
    AddChild(std::move(senderRow), Hint().Fixed(kSenderRowH).Margin(0, 0, 0, kSenderBottomGap));

    // ====================================================================
    // 子控件 3:分隔线占位(1px 灰线,装饰画在 m_rcItem 中线)
    // ====================================================================
    std::unique_ptr<DuiControl> div = MakeSlot();
    m_dividerSlot = div.get();
    AddChild(std::move(div), Hint().Fixed(kDividerH).Margin(0, 0, 0, kDividerBottomGap));

    // ====================================================================
    // 子控件 4:正文(只读多行可复制 RichEdit),高度同标题在 Layout 量算。
    // ====================================================================
    // 正文直接占一整行,不再额外套一个 HBox 挂手写滚动条。
    //
    // 原先的做法是 [ 正文 RichEdit(Weight 1) ][ 悬浮滚动条(固定窄带) ],
    // 因为旧控件寄宿真子窗口、原生滚动条既难看又会盖住东西,只能在子窗口
    // 矩形之外的右侧窄带里另画一条细条,再靠两个回调把它与正文的滚动位置
    // 双向同步。换成无窗口控件之后这一整套都不需要了:它自带的滚动条就是
    // 覆盖式 auto-hide 的,浮在内容之上、不占内容宽度,与左侧公告列表同款。
    //
    // 正文用弹性高度:撑满分隔线之下、状态卡之上的剩余竖直空间,使下方
    // 状态卡 / 按钮行被顶到预览区底部(按钮底部对齐);正文超长时框内滚动。
    {
        std::unique_ptr<DuiRichEdit> bodyEdit = MakeReadOnlyRichEdit(
            ui::UiFont(theme::kFontSizeNormal, false), theme::kInk700);
        m_bodyEdit = bodyEdit.get();
        AddChild(std::move(bodyEdit),
                 Hint().Weight(1).Margin(0, 0, 0, kBodyBottomGap));
    }

    // ====================================================================
    // 子控件 5:ProgressCard(已读进度卡:m_scheduled=false 时可见)
    //   卡片底 + 内嵌一行(左标签 + 右数字) + 进度条占位
    // ====================================================================
    std::unique_ptr<DuiVBox> progressCard(new DuiVBox());
    progressCard->SetBgColor(theme::kInk25);
    progressCard->SetCornerRadius(theme::kRadiusLg);
    progressCard->SetBorderColor(theme::kInk100);
    progressCard->SetBorderWidth(1.0f);
    progressCard->SetPadding(kProgressCardPadH, kProgressCardPadV,
                             kProgressCardPadH, kProgressCardPadV);
    progressCard->SetGap(kProgressBarGap);
    {
        // 第 1 行:左标签 + 右数字 = 一个 HBox
        std::unique_ptr<DuiHBox> topRow(new DuiHBox());
        topRow->SetGap(0);
        {
            std::unique_ptr<DuiLabel> left(new DuiLabel());
            left->SetText(_T("送达 · 已读"));
            left->SetFont(ui::UiFont(theme::kFontSizeSmall, true));
            left->SetTextColor(theme::kInk400);
            left->SetTextAlign(DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
            m_progressLeftLabel = left.get();
            topRow->AddChild(std::move(left), Hint().Weight(1));

            std::unique_ptr<DuiLabel> right = MakeSelectableLabel(
                _T(""), ui::UiFont(theme::kFontSizeNormal, false),
                theme::kInk700, DT_RIGHT);
            m_progressRightLabel = right.get();
            topRow->AddChild(std::move(right), Hint().Weight(1));
        }
        progressCard->AddChild(std::move(topRow), Hint().Fixed(18));

        // 第 2 行:进度条占位
        std::unique_ptr<DuiControl> bar = MakeSlot();
        m_progressBarSlot = bar.get();
        progressCard->AddChild(std::move(bar), Hint().Fixed(kProgressBarH));
    }
    m_progressCard = progressCard.get();
    AddChild(std::move(progressCard), Hint().Fixed(kProgressCardH).Margin(0, 0, 0, kStatusCardBottomGap));

    // ====================================================================
    // 子控件 6:ScheduleCard(调度队列卡:m_scheduled=true 时可见)
    //   卡片底 + 内嵌一个 HBox(图标占位 + 右侧两行文字 VBox)
    // ====================================================================
    std::unique_ptr<DuiVBox> scheduleCard(new DuiVBox());
    scheduleCard->SetBgColor(RGB(0xF0, 0xF5, 0xFF));
    scheduleCard->SetCornerRadius(theme::kRadiusLg);
    scheduleCard->SetBorderColor(RGB(0xC7, 0xDA, 0xF6));
    scheduleCard->SetBorderWidth(1.0f);
    scheduleCard->SetPadding(16, 15, 16, 15);
    scheduleCard->SetGap(0);
    {
        std::unique_ptr<DuiHBox> row(new DuiHBox());
        row->SetGap(14);
        {
            std::unique_ptr<DuiControl> iconSlot = MakeSlot();
            m_scheduleIconSlot = iconSlot.get();
            row->AddChild(std::move(iconSlot), Hint().Fixed(kScheduleIconBoxW));

            std::unique_ptr<DuiVBox> texts(new DuiVBox());
            texts->SetGap(0);
            {
                std::unique_ptr<DuiLabel> top(new DuiLabel());
                top->SetText(_T("尚未发送 · 已加入调度队列"));
                top->SetFont(ui::UiFont(theme::kFontSizeNormal, true));
                top->SetTextColor(theme::kInk700);
                top->SetTextAlign(DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
                m_scheduleTopLabel = top.get();
                texts->AddChild(std::move(top), Hint().Fixed(20));

                std::unique_ptr<DuiLabel> sub = MakeSelectableLabel(
                    _T(""), ui::UiFont(theme::kFontSizeSmall, false),
                    theme::kInk500, DT_LEFT);
                m_scheduleSubLabel = sub.get();
                texts->AddChild(std::move(sub), Hint().Fixed(22));
            }
            row->AddChild(std::move(texts), Hint().Weight(1));
        }
        scheduleCard->AddChild(std::move(row), Hint().Weight(1));
    }
    m_scheduleCard = scheduleCard.get();
    AddChild(std::move(scheduleCard), Hint().Fixed(kScheduleCardH).Margin(0, 0, 0, kStatusCardBottomGap));

    // ====================================================================
    // 子控件 7:ActionRow(6 个 DuiButton + 弹性占位)
    //   左对齐 5 个:编辑 / 复制 / 重新推送 / 置顶 / 删除公告(Danger,任意状态
    //   物理删除);弹性占位推 1 个右对齐:撤回(Danger,仅已发布显示,把公告
    //   改回未发布)。每个按钮独立 ctrlId,DUIN_CLICK 直接冒到 AdminFrame。
    // ====================================================================
    std::unique_ptr<DuiHBox> actionRow(new DuiHBox());
    actionRow->SetGap(kActionBtnGap);
    {
        std::unique_ptr<DuiButton> e = MakeActionButton(
            _T("编辑"), kIdAnnPreviewEdit, DuiButton::Variant::Default);
        m_btnEdit = e.get();
        actionRow->AddChild(std::move(e), Hint().Fixed(kActionBtnW));

        std::unique_ptr<DuiButton> c = MakeActionButton(
            _T("复制公告"), kIdAnnPreviewCopy, DuiButton::Variant::Default);
        m_btnCopy = c.get();
        actionRow->AddChild(std::move(c), Hint().Fixed(kActionBtnW));

        std::unique_ptr<DuiButton> r = MakeActionButton(
            _T("重新推送"), kIdAnnPreviewRepush, DuiButton::Variant::Default);
        m_btnRepush = r.get();
        actionRow->AddChild(std::move(r), Hint().Fixed(kActionBtnW));

        std::unique_ptr<DuiButton> p = MakeActionButton(
            _T("置顶"), kIdAnnPreviewTogglePin, DuiButton::Variant::Default);
        m_btnTogglePin = p.get();
        actionRow->AddChild(std::move(p), Hint().Fixed(kActionBtnW));

        std::unique_ptr<DuiButton> da = MakeActionButton(
            _T("删除公告"), kIdAnnPreviewDeleteAnn, DuiButton::Variant::Danger);
        m_btnDeleteAnn = da.get();
        actionRow->AddChild(std::move(da), Hint().Fixed(kActionBtnW));

        std::unique_ptr<DuiControl> spacer = MakeSlot();
        actionRow->AddChild(std::move(spacer), Hint().Weight(1));

        std::unique_ptr<DuiButton> d = MakeActionButton(
            _T("撤回"), kIdAnnPreviewDelete, DuiButton::Variant::Danger);
        m_btnDelete = d.get();
        actionRow->AddChild(std::move(d), Hint().Fixed(kActionBtnW));
    }
    AddChild(std::move(actionRow), Hint().Fixed(kActionRowH));

    // ====================================================================
    // 子控件 8:未选中占位 label(SetAnnouncement(nullptr) 时唯一可见的子)
    //   单行居中,与原 PaintPreview 早退分支视觉一致。
    // ====================================================================
    std::unique_ptr<DuiLabel> hint(new DuiLabel());
    hint->SetText(_T("未选择公告"));
    hint->SetFont(ui::UiFont(kEmptyHintPt, false));
    hint->SetTextColor(theme::kInk400);
    hint->SetTextAlign(DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    m_emptyHint = hint.get();
    AddChild(std::move(hint), Hint().Weight(1));

    // 初始状态:呈现"未选择公告"(无 m_current,唯独 emptyHint 可见)。
    SetAnnouncement(nullptr);
}

void AnnouncementPreviewPanel::SetAnnouncement(const Announcement* a)
{
    // 把外部公告深拷贝到 panel 自有的 m_currentData,然后让本函数后续的
    // 所有 a-> 读取重定向到自家拷贝。这样:
    //   1) DataStore::SetAnnouncements 整体替换 vector 时,本 panel 不受影响;
    //   2) OnPaint / Layout 通过 m_current 访问的也是这份本地拷贝,永远有效。
    if (a != nullptr)
    {
        m_currentData = *a;
        m_hasCurrent  = true;
        a             = &m_currentData;     // 后续读字段统一走自家拷贝
        m_current     = &m_currentData;
    }
    else
    {
        m_hasCurrent = false;
        m_current    = nullptr;
    }

    // 切换 emptyHint 与"详情子控件组"的整体可见性。
    const bool show = (a != nullptr);
    m_emptyHint->SetVisible(!show);

    // ---- 详情子控件按 show 整体显隐 ----
    // HeaderRow / titleEdit / SenderRow / divider / bodyEdit / ActionRow 始终
    // 跟随 show;ProgressCard / ScheduleCard 二选一在 show=true 时按 a->m_scheduled
    // 决定,show=false 时两者都隐藏。
    if (Children().size() >= 9)
    {
        Children()[0]->SetVisible(show);   // HeaderRow
        Children()[1]->SetVisible(show);   // titleEdit
        Children()[2]->SetVisible(show);   // SenderRow
        Children()[3]->SetVisible(show);   // dividerSlot
        Children()[4]->SetVisible(show);   // bodyEdit
        Children()[7]->SetVisible(show);   // ActionRow
    }

    if (!show)
    {
        m_progressCard->SetVisible(false);
        m_scheduleCard->SetVisible(false);
        return;
    }

    // ---- show=true:转写各字段 ----
    m_titleEdit->SetText(a->m_title);
    m_bodyEdit->SetText(a->m_body);

    // 日期文案:定时发送时显示"计划于 X 发送",已发布时直接 m_sentAt。
    CString dateText;
    if (a->m_scheduled)
    {
        dateText.Format(_T("计划于 %s 发送"), static_cast<LPCTSTR>(a->m_sentAt));
    }
    else
    {
        dateText = a->m_sentAt;
    }
    m_dateLabel->SetText(dateText);

    // "发送者    ·    受众:xxx" 单行(原 PaintPreview meta.Format 格式)。
    CString meta;
    meta.Format(_T("%s    ·    受众:%s"),
                static_cast<LPCTSTR>(a->m_sender),
                static_cast<LPCTSTR>(a->m_audience));
    m_senderMetaLabel->SetText(meta);

    // 两个可选徽章按字段切换可见性。
    m_pinnedChipSlot->SetVisible(a->m_pinned);
    m_scheduledChipSlot->SetVisible(a->m_scheduled);

    // 状态卡二选一:定时 → 调度卡,否则 → 进度卡。
    m_progressCard->SetVisible(!a->m_scheduled);
    m_scheduleCard->SetVisible(a->m_scheduled);

    // 进度数字 "X / Y 人 (Z%)"(原 PaintPreview rt.Format)。
    if (!a->m_scheduled)
    {
        const int pct = (a->m_total > 0) ? (a->m_read * 100 / a->m_total) : 0;
        CString rt;
        rt.Format(_T("%d / %d 人(%d%%)"), a->m_read, a->m_total, pct);
        m_progressRightLabel->SetText(rt);
    }

    // 调度卡副标题 "将于 X 推送至 Y 名用户"(原 t2.Format)。
    if (a->m_scheduled)
    {
        CString sub;
        sub.Format(_T("将于 %s 推送至 %d 名用户"),
                   static_cast<LPCTSTR>(a->m_sentAt), a->m_total);
        m_scheduleSubLabel->SetText(sub);
    }

    // 置顶按钮文字按 m_pinned 切换。
    m_btnTogglePin->SetText(a->m_pinned ? _T("取消置顶") : _T("置顶"));

    // 右侧「撤回」按钮专用于把已发布公告改回未发布:仅 status==1(已发布)显示,
    // 草稿 / 已撤回隐藏(物理删除统一走左侧「删除公告」按钮)。
    m_btnDelete->SetText(_T("撤回"));
    m_btnDelete->SetVisible(a->m_status == 1);

}

void AnnouncementPreviewPanel::Layout(const RECT& rcAvail)
{
    // 自递归护栏:本函数下面要在 Layout 体内调 SetHint 改 titleEdit 的 Fixed,
    // 不加守卫会触发 DuiLayout::SetHint 再次调本 Layout,导致无限递归(详见
    // DuiLayout::LayoutGuard 注释)。
    DuiLayout::LayoutGuard layoutGuard(*this);

    // 仅标题按可用宽度动态量算高度(标题字数少,通常一行,偶尔两行)。正文
    // 不再动态算高 —— 它改为弹性高度(Weight),撑满剩余竖直空间,内容超出时
    // 由控件自带的覆盖式滚动条承载框内滚动。无论正文多长框高都由布局分配,
    // 从根上避免"切换公告不重新布局导致高度过时"的老问题。
    //
    // 可用宽度 = panel 客户区宽度 - 左右内距;不能直接用 m_rcItem.right - left
    // (此时还没设),用 rcAvail 自己算。
    const int avail = (rcAvail.right - rcAvail.left)
                      - kPanelPadH - kPanelPadH;
    if (avail > 0)
    {
        //标题已固定单行,高度不随文本变化:量一行的高度即可,不必按可用宽度算换行。
        //没有选中公告时高度取 0,让整块塌陷(此时只显示"未选择公告"提示)。
        int titleH = 0;
        if (m_current != nullptr)
        {
            titleH = MeasureWrappedH(_T("A"), ui::UiFont(kTitlePt, true), avail)
                     + kTitleSingleLinePad;
        }
        SetHint(m_titleEdit, Hint().Fixed(titleH).Margin(0, 0, 0, kTitleBottomGap));
    }

    // 交给 DuiVBox 走标准纵向布局。
    DuiVBox::Layout(rcAvail);
}

void AnnouncementPreviewPanel::OnPaint(HDC hdc, const RECT& rcDirty)
{
    // 基类先画卡片底 + 转发所有子控件(label / RichEdit / button 都会画出来)。
    DuiVBox::OnPaint(hdc, rcDirty);

    // m_current == nullptr 时只有 emptyHint 可见,装饰阶段无内容可画,直接返回。
    if (m_current == nullptr)
    {
        return;
    }
    const Announcement* a = m_current;

    // ---- 装饰 1:级别徽章(SeverityChip) ----
    // 在 m_severityChipSlot 矩形里画一个圆角填充 + 级别色圆点 + 文案。
    if (m_severityChipSlot != nullptr && m_severityChipSlot->IsVisible())
    {
        ui::DrawChip(hdc, m_severityChipSlot->GetRect(),
                     SeverityChipLabel(a->m_severity),
                     theme::kInk600, theme::kInk50,
                     true, ui::SeverityColor(a->m_severity));
    }

    // ---- 装饰 2:"已置顶" 徽章 ----
    if (m_pinnedChipSlot != nullptr && m_pinnedChipSlot->IsVisible())
    {
        ui::DrawChip(hdc, m_pinnedChipSlot->GetRect(), _T("已置顶"),
                     theme::kFlamingo700, theme::kFlamingo50,
                     true, theme::kFlamingo500);
    }

    // ---- 装饰 3:"定时发送" 徽章 ----
    if (m_scheduledChipSlot != nullptr && m_scheduledChipSlot->IsVisible())
    {
        ui::DrawChip(hdc, m_scheduledChipSlot->GetRect(), _T("定时发送"),
                     theme::kColorInfo, theme::kInk50,
                     true, theme::kColorInfo);
    }

    // ---- 装饰 4:发送者头像 ----
    if (m_avatarSlot != nullptr && m_avatarSlot->IsVisible())
    {
        ui::DrawAvatar(hdc, m_avatarSlot->GetRect(),
                       ui::AvatarColor(a->m_sender),
                       ui::Initials(a->m_sender));
    }

    // ---- 装饰 5:分隔线(1px 灰线,画在 m_dividerSlot 矩形里) ----
    if (m_dividerSlot != nullptr && m_dividerSlot->IsVisible())
    {
        const RECT& d = m_dividerSlot->GetRect();
        ui::FillLine(hdc, d.left, d.top, d.right - d.left, 1, theme::kInk100);
    }

    // ---- 装饰 6:进度条(轨道 + 已读填充) ----
    if (m_progressBarSlot != nullptr && m_progressBarSlot->IsVisible()
        && !a->m_scheduled)
    {
        const RECT& bar = m_progressBarSlot->GetRect();
        ui::FillRoundRect(hdc, bar, theme::kInk100, 4);
        const int pct = (a->m_total > 0) ? (a->m_read * 100 / a->m_total) : 0;
        RECT fill = bar;
        fill.right = bar.left + (bar.right - bar.left) * pct / 100;
        if (fill.right > fill.left + 8)
        {
            ui::FillRoundRect(hdc, fill, theme::kFlamingo500, 4);
        }
    }

    // ---- 装饰 7:调度卡左侧 Bell 图标底 + 图标 ----
    if (m_scheduleIconSlot != nullptr && m_scheduleIconSlot->IsVisible()
        && a->m_scheduled)
    {
        const RECT& box = m_scheduleIconSlot->GetRect();
        ui::FillRoundRect(hdc, box, RGB(0xE6, 0xEF, 0xFF), theme::kRadiusMd);
        // Bell 图标在 box 中心,20x20 大小(与原 PaintPreview 一致)。
        const int cx = (box.left + box.right) / 2;
        const int cy = (box.top + box.bottom) / 2;
        RECT ii = { cx - 10, cy - 10, cx + 10, cy + 10 };
        ui::DrawIcon(hdc, ui::Icon::Bell, ii, theme::kColorInfo);
    }
}

} // namespace flamingoadmin
