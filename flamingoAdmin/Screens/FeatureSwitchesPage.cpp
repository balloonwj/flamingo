/**
 *  flamingoAdmin —— 企业详情屏「功能开关」子 tab 的内容页实现，说明见 FeatureSwitchesPage.h。
 *
 *  balloonwj@qq.com   2026-10-08
 */
#include "stdafx.h"
#include "Screens/FeatureSwitchesPage.h"
#include "Screens/FeatureSwitchPanel.h"
#include "Screens/CloudRecordQuotaPanel.h"
#include "UiIds.h"
#include "Theme/AdminTheme.h"
#include "Widgets/UiHelpers.h"
#include "Widgets/CommonWidgets.h"     // ui::DrawCard
#include "Widgets/OverlayScrollbar.h"
#include "Util/Logger.h"

#include <memory>

using namespace balloonwjui;

namespace flamingoadmin {

namespace {

// ---- 布局常量（96 dpi 逻辑像素）----
const int kPagePad      = 24;    // 页面四周的外边距（与列表页的外边距一致）
const int kNoteH        = 20;    // 顶部说明行的高度
const int kNoteGap      = 12;    // 说明行与第一行卡片之间的间距
const int kColumnGap    = 16;    // 两栏之间的间距
const int kCardGap      = 16;    // 同一栏里上下两张卡片之间的间距
const int kCardPad      = 24;    // 卡片左右内边距（与平台设置页的卡片一致）
const int kCardTitleTop = 20;    // 卡片标题距卡片顶边的距离
const int kCardTitleH   = 22;    // 卡片标题的高度
const int kCardBodyTop  = 58;    // 卡片里第一个面板的顶边距卡片顶边的距离（标题区高度）
const int kPanelGap     = 12;    // 卡片里相邻两个面板之间的间距
const int kCardBottom   = 20;    // 卡片里最后一个面板之下留出的空白
const int kCardMaxW     = 600;   // 卡片宽度上限：再宽时说明文字与开关隔得太远，不便对照
const int kColumnMinW   = 360;   // 排成两栏时每栏所需的最小宽度；不够时排成一栏
const int kWheelStep    = 48;    // 滚轮每格（WHEEL_DELTA）滚动的像素数
const int kCardTitlePt  = 12;    // 卡片标题字号（磅），与平台设置页的卡片标题一致

// 一个开关在本页的配置：管哪个开关、读写所用的结果标签、开关的控件 ID。
struct ItemDef
{
    const FeatureSwitchSpec& (*m_spec)();   // 取开关描述的函数（描述是静态存储）
    ApiTag                   m_getTag;      // 读设置的结果标签
    ApiTag                   m_setTag;      // 改设置的结果标签
    int                      m_ctrlId;      // 开关的控件 ID，宿主据此把 DUIN_VALUECHANGED 转给 OnSwitchToggled
};

// 各开关的配置，下标与 FeatureSwitchesPage::Item 一致。结果标签沿用原企业设置弹窗的那一组（平台设置页的那一组已作废）。
const ItemDef kItemDefs[FeatureSwitchesPage::kItemCount] =
{
    { &RegisterSwitchSpec,         kApiTagRegisterSwitchGet,    kApiTagRegisterSwitchSet,    kIdFeatSwitchRegister },
    { &InviteConfirmSwitchSpec,    kApiTagInviteConfirmGet,     kApiTagInviteConfirmSet,     kIdFeatSwitchInviteConfirm },
    { &SecretChatSwitchSpec,       kApiTagSecretChatSwitchGet,  kApiTagSecretChatSwitchSet,  kIdFeatSwitchSecretChat },
    { &ThreadGroupSwitchSpec,      kApiTagThreadGroupSwitchGet, kApiTagThreadGroupSwitchSet, kIdFeatSwitchThreadGroup },
    { &FileShareSwitchSpec,        kApiTagFeatureGet,           kApiTagFeatureSet,           kIdFeatSwitchFileShare },
    { &P2PSwitchSpec,              kApiTagP2PSwitchGet,         kApiTagP2PSwitchSet,         kIdFeatSwitchP2P },
    { &GroupCallSwitchSpec,        kApiTagGroupCallSwitchGet,   kApiTagGroupCallSwitchSet,   kIdFeatSwitchGroupCall },
    { &CaptionSwitchSpec,          kApiTagCaptionSwitchGet,     kApiTagCaptionSwitchSet,     kIdFeatSwitchCaption },
    { &CloudRecordSwitchSpec,      kApiTagCloudRecordSwitchGet, kApiTagCloudRecordSwitchSet, kIdFeatSwitchCloudRecord },
    { &BotSwitchSpec,              kApiTagBotSwitchGet,         kApiTagBotSwitchSet,         kIdFeatSwitchBot },
    { &BotGroupSwitchSpec,         kApiTagBotGroupSwitchGet,    kApiTagBotGroupSwitchSet,    kIdFeatSwitchBotGroup },
    { &BotPersonalSwitchSpec,      kApiTagBotPersonalSwitchGet, kApiTagBotPersonalSwitchSet, kIdFeatSwitchBotPersonal },
    { &BotGroupExternalSwitchSpec, kApiTagBotExternalSwitchGet, kApiTagBotExternalSwitchSet, kIdFeatSwitchBotGroupExternal },
};

// 每张卡片第一个开关的下标；最后一项为开关总数，第 g 张卡片的开关是 [kGroupFirst[g], kGroupFirst[g + 1])
const int kGroupFirst[FeatureSwitchesPage::kGroupCount + 1] =
{
    FeatureSwitchesPage::kItemRegister,
    FeatureSwitchesPage::kItemFileShare,
    FeatureSwitchesPage::kItemGroupCall,
    FeatureSwitchesPage::kItemBot,
    FeatureSwitchesPage::kItemCount,
};

// 卡片标题，下标与 FeatureSwitchesPage::Group 一致
const TCHAR* const kGroupTitles[FeatureSwitchesPage::kGroupCount] =
{
    _T("账号与会话"),
    _T("文件与传输"),
    _T("音视频"),
    _T("机器人"),
};

// 顶部说明行的文字
const TCHAR kNoteText[] = _T("拨动开关后先弹出确认框，确认后立即对该企业的全部成员生效；云端录像空间修改后点「保存」生效。");

} // namespace

FeatureSwitchesPage::FeatureSwitchesPage()
    : m_cloudQuotaPanel(nullptr)
    , m_scrollbar(nullptr)
    , m_twoColumns(false)
    , m_enterpriseId(0)
    , m_scroll(0)
    , m_scrollMax(0)
{
    for (int i = 0; i < kGroupCount; ++i)
    {
        m_groupRects[i] = RECT{ 0, 0, 0, 0 };
    }

    // ---- 各开关的面板（完整样式）----
    for (int i = 0; i < kItemCount; ++i)
    {
        const ItemDef& def = kItemDefs[i];
        std::unique_ptr<FeatureSwitchPanel> panel(
            new FeatureSwitchPanel(def.m_spec(), def.m_getTag, def.m_setTag, def.m_ctrlId));
        m_panels[i] = panel.get();
        AddChild(std::move(panel));

        // 云端录像空间面板紧跟在云端录制开关之后加入（与页面上的排列顺序一致）
        if (i == kItemCloudRecord)
        {
            std::unique_ptr<CloudRecordQuotaPanel> quota(
                new CloudRecordQuotaPanel(kApiTagCloudQuotaGet, kApiTagCloudQuotaSet, kIdCloudQuotaSave,
                                          kIdCloudQuotaQuota, kIdCloudQuotaDays));
            m_cloudQuotaPanel = quota.get();
            AddChild(std::move(quota));
        }
    }

    // ---- 悬浮滚动条：最后加入，命中测试时先于其它子控件 ----
    std::unique_ptr<OverlayScrollbar> sb(new OverlayScrollbar());
    sb->SetOnScroll(&FeatureSwitchesPage::OnScrollbarScroll, this);
    m_scrollbar = sb.get();
    AddChild(std::move(sb));
}

void FeatureSwitchesPage::SetRequestSink(fsui::RequestSinkFn fn, void* user)
{
    for (int i = 0; i < kItemCount; ++i)
    {
        m_panels[i]->SetRequestSink(fn, user);
    }
    m_cloudQuotaPanel->SetRequestSink(fn, user);
}

void FeatureSwitchesPage::SetUiRequestHandler(fsui::UiRequestFn fn, void* user)
{
    for (int i = 0; i < kItemCount; ++i)
    {
        m_panels[i]->SetUiRequestHandler(fn, user);
    }
    m_cloudQuotaPanel->SetUiRequestHandler(fn, user);
}

void FeatureSwitchesPage::Load(long long enterpriseId)
{
    m_enterpriseId = (enterpriseId > 0) ? enterpriseId : 0;
    FA_LOGI(_T("feature switches page: load settings, enterprise: %lld"), m_enterpriseId);
    for (int i = 0; i < kItemCount; ++i)
    {
        m_panels[i]->Load(m_enterpriseId);
    }
    m_cloudQuotaPanel->Load(m_enterpriseId);

    //换了企业（或重新进入本页）时从顶部看起
    m_scroll = 0;
    if (m_rcItem.right > m_rcItem.left)
    {
        ForceLayout(m_rcItem);
    }
    Invalidate();
}

bool FeatureSwitchesPage::OnApiResult(ApiTag tag, const HttpResult& http)
{
    for (int i = 0; i < kItemCount; ++i)
    {
        if (tag == m_panels[i]->GetTag() || tag == m_panels[i]->SetTag())
        {
            m_panels[i]->OnApiResult(tag, http);
            return true;
        }
    }
    if (tag == m_cloudQuotaPanel->GetTag() || tag == m_cloudQuotaPanel->SetTag())
    {
        m_cloudQuotaPanel->OnApiResult(tag, http);
        return true;
    }
    return false;
}

bool FeatureSwitchesPage::OnSwitchToggled(UINT ctrlId, bool on)
{
    for (int i = 0; i < kItemCount; ++i)
    {
        if (static_cast<UINT>(kItemDefs[i].m_ctrlId) == ctrlId)
        {
            m_panels[i]->OnSwitchToggled(on);
            return true;
        }
    }
    return false;
}

bool FeatureSwitchesPage::OnQuotaSaveClicked(UINT ctrlId)
{
    if (ctrlId != static_cast<UINT>(kIdCloudQuotaSave))
    {
        return false;
    }
    m_cloudQuotaPanel->OnSaveClicked();
    return true;
}

FeatureSwitchPanel* FeatureSwitchesPage::Panel(int item) const
{
    if (item < 0 || item >= kItemCount)
    {
        return nullptr;
    }
    return m_panels[item];
}

int FeatureSwitchesPage::GroupOf(int item)
{
    if (item < 0 || item >= kItemCount)
    {
        return -1;
    }
    for (int g = 0; g < kGroupCount; ++g)
    {
        if (item >= kGroupFirst[g] && item < kGroupFirst[g + 1])
        {
            return g;
        }
    }
    return -1;
}

long long FeatureSwitchesPage::EnterpriseIdForLoad(bool platformAdmin, const CString& detailEnterpriseId)
{
    if (!platformAdmin)
    {
        return 0;
    }
    const long long id = _ttoi64(detailEnterpriseId);
    return (id > 0) ? id : 0;
}

LPCTSTR FeatureSwitchesPage::GroupTitle(int group)
{
    if (group < 0 || group >= kGroupCount)
    {
        return _T("");
    }
    return kGroupTitles[group];
}

RECT FeatureSwitchesPage::GroupRect(int group) const
{
    if (group < 0 || group >= kGroupCount)
    {
        return RECT{ 0, 0, 0, 0 };
    }
    return m_groupRects[group];
}

int FeatureSwitchesPage::GroupContentHeight(int group)
{
    const int count = kGroupFirst[group + 1] - kGroupFirst[group];
    int h = count * FeatureSwitchPanel::kPanelHeight + (count - 1) * kPanelGap;
    //「音视频」卡片最下面还有云端录像空间面板
    if (group == kGroupMedia)
    {
        h += kPanelGap + CloudRecordQuotaPanel::kPanelHeight;
    }
    return h;
}

int FeatureSwitchesPage::GroupHeight(int group)
{
    return kCardBodyTop + GroupContentHeight(group) + kCardBottom;
}

void FeatureSwitchesPage::LayoutGroup(int group, const RECT& cardRect)
{
    const int left = cardRect.left + kCardPad;
    const int right = cardRect.right - kCardPad;
    int top = cardRect.top + kCardBodyTop;
    for (int i = kGroupFirst[group]; i < kGroupFirst[group + 1]; ++i)
    {
        m_panels[i]->ForceLayout(RECT{ left, top, right, top + FeatureSwitchPanel::kPanelHeight });
        top += FeatureSwitchPanel::kPanelHeight + kPanelGap;
        //云端录像空间面板紧跟在云端录制开关之下
        if (i == kItemCloudRecord)
        {
            m_cloudQuotaPanel->ForceLayout(RECT{ left, top, right, top + CloudRecordQuotaPanel::kPanelHeight });
            top += CloudRecordQuotaPanel::kPanelHeight + kPanelGap;
        }
    }
}

void FeatureSwitchesPage::Layout(const RECT& rcAvail)
{
    m_rcItem = rcAvail;

    // 两栏还是一栏：每栏能分到 kColumnMinW 以上时排两栏，卡片宽度都不超过 kCardMaxW
    const int availW = (rcAvail.right - rcAvail.left) - 2 * kPagePad;
    const int halfW = (availW - kColumnGap) / 2;
    m_twoColumns = (halfW >= kColumnMinW);
    int cardW = m_twoColumns ? halfW : availW;
    if (cardW > kCardMaxW)
    {
        cardW = kCardMaxW;
    }
    if (cardW < 0)
    {
        cardW = 0;
    }

    // 内容总高（与滚动无关）：上边距 + 说明行 + 卡片区 + 下边距。两栏时取较高的一栏
    int cardsH = 0;
    if (m_twoColumns)
    {
        const int leftH = GroupHeight(kGroupAccount) + kCardGap + GroupHeight(kGroupTransfer);
        const int rightH = GroupHeight(kGroupMedia) + kCardGap + GroupHeight(kGroupBot);
        cardsH = (leftH > rightH) ? leftH : rightH;
    }
    else
    {
        for (int g = 0; g < kGroupCount; ++g)
        {
            cardsH += GroupHeight(g);
        }
        cardsH += (kGroupCount - 1) * kCardGap;
    }
    const int contentH = kPagePad + kNoteH + kNoteGap + cardsH + kPagePad;

    // 最大滚动偏移，并把当前偏移夹取到合法范围
    const int viewH = rcAvail.bottom - rcAvail.top;
    m_scrollMax = (contentH > viewH) ? (contentH - viewH) : 0;
    if (m_scroll > m_scrollMax)
    {
        m_scroll = m_scrollMax;
    }
    if (m_scroll < 0)
    {
        m_scroll = 0;
    }

    // 卡片矩形：内容顶边随滚动上移
    const int cardsTop = rcAvail.top - m_scroll + kPagePad + kNoteH + kNoteGap;
    const int col0L = rcAvail.left + kPagePad;
    const int col1L = col0L + cardW + kColumnGap;
    if (m_twoColumns)
    {
        // 左栏：账号与会话、文件与传输；右栏：音视频、机器人
        m_groupRects[kGroupAccount] = RECT{ col0L, cardsTop, col0L + cardW, cardsTop + GroupHeight(kGroupAccount) };
        const int transferTop = m_groupRects[kGroupAccount].bottom + kCardGap;
        m_groupRects[kGroupTransfer] = RECT{ col0L, transferTop, col0L + cardW, transferTop + GroupHeight(kGroupTransfer) };
        m_groupRects[kGroupMedia] = RECT{ col1L, cardsTop, col1L + cardW, cardsTop + GroupHeight(kGroupMedia) };
        const int botTop = m_groupRects[kGroupMedia].bottom + kCardGap;
        m_groupRects[kGroupBot] = RECT{ col1L, botTop, col1L + cardW, botTop + GroupHeight(kGroupBot) };
    }
    else
    {
        int top = cardsTop;
        for (int g = 0; g < kGroupCount; ++g)
        {
            m_groupRects[g] = RECT{ col0L, top, col0L + cardW, top + GroupHeight(g) };
            top = m_groupRects[g].bottom + kCardGap;
        }
    }
    for (int g = 0; g < kGroupCount; ++g)
    {
        LayoutGroup(g, m_groupRects[g]);
    }

    // 悬浮滚动条：贴本页右缘、与本页等高；内容放得下时它不显示也不命中
    m_scrollbar->ForceLayout(RECT{ rcAvail.right - OverlayScrollbar::kSuggestedWidth, rcAvail.top,
                                   rcAvail.right, rcAvail.bottom });
    m_scrollbar->SetMetrics(contentH, m_scroll);
}

void FeatureSwitchesPage::ScrollTo(int scroll)
{
    if (scroll > m_scrollMax)
    {
        scroll = m_scrollMax;
    }
    if (scroll < 0)
    {
        scroll = 0;
    }
    if (scroll == m_scroll)
    {
        return;
    }
    m_scroll = scroll;
    // 重新摆放子控件，并同步滚动条
    ForceLayout(m_rcItem);
    Invalidate();
}

void FeatureSwitchesPage::OnScrollbarScroll(void* ctx, int newScroll)
{
    FeatureSwitchesPage* self = static_cast<FeatureSwitchesPage*>(ctx);
    if (self != nullptr)
    {
        self->ScrollTo(newScroll);
    }
}

bool FeatureSwitchesPage::OnMouseWheel(POINT /*pt*/, short zDelta, UINT /*mkFlags*/)
{
    if (!m_bVisible || m_scrollMax <= 0)
    {
        return false;
    }
    ScrollTo(m_scroll - (static_cast<int>(zDelta) * kWheelStep) / WHEEL_DELTA);
    m_scrollbar->Flash();
    return true;
}

void FeatureSwitchesPage::OnPaint(HDC hdc, const RECT& rcDirty)
{
    if (!m_bVisible)
    {
        return;
    }
    // 浅灰底色，与列表页、「在线用户」页的主体区一致
    ui::FillSolidRect(hdc, m_rcItem, theme::kInk25);

    // 内容随滚动上移，裁剪到本页矩形：滚出本页的部分不画到企业详情屏的头部等相邻区域上
    const int savedDc = ::SaveDC(hdc);
    ::IntersectClipRect(hdc, m_rcItem.left, m_rcItem.top, m_rcItem.right, m_rcItem.bottom);

    // 顶部说明行
    const int noteTop = m_rcItem.top - m_scroll + kPagePad;
    ui::DrawTextLine(hdc, kNoteText,
                     RECT{ m_rcItem.left + kPagePad, noteTop, m_rcItem.right - kPagePad, noteTop + kNoteH },
                     theme::kInk400, ui::UiFont(theme::kFontSizeNormal, false),
                     DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);

    // 各张卡片：白底卡片 + 标题（面板本身是子控件，由下面的基类 OnPaint 绘制）
    HFONT titleFont = ui::UiFont(kCardTitlePt, true);
    for (int g = 0; g < kGroupCount; ++g)
    {
        const RECT& card = m_groupRects[g];
        ui::DrawCard(hdc, card);
        ui::DrawTextLine(hdc, kGroupTitles[g],
                         RECT{ card.left + kCardPad, card.top + kCardTitleTop,
                               card.right - kCardPad, card.top + kCardTitleTop + kCardTitleH },
                         theme::kInk700, titleFont, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    }

    // 子控件（各面板与滚动条），同样裁剪到本页矩形
    balloonwjui::DuiControl::OnPaint(hdc, rcDirty);
    ::RestoreDC(hdc, savedDc);
}

} // namespace flamingoadmin
