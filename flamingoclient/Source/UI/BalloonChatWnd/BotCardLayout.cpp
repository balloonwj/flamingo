/**
 *  BotCardLayout.cpp
 *
 *  机器人资料卡小窗的客户区控件树、按内容定高与逐层重排，说明见 BotCardLayout.h。
 *
 *  balloonwj@qq.com   2026-10-08
 */
#include "stdafx.h"
#include "BotCardLayout.h"

#include "GroupBotSection.h"   // BotBadgeWidth：小标签的宽度，与成员页签的机器人行同一算法
#include "../../Common/I18n/I18n.h"   // TR：界面文字的多语言查找
#include "../../../third-party/balloonui/balloonui/DuiControl.h"
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiAvatar.h"
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiBadge.h"
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiButton.h"
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiLabel.h"
#include "../../../third-party/balloonui/balloonui/Controls/Layout/DuiLayout.h"
#include "../../../third-party/balloonui/balloonui/Controls/Window/DuiScrollBar.h"   // DuiScrollView

using namespace balloonwjui;

namespace balloonchatwnd {

namespace {

// ---- 颜色 ----
const COLORREF kColorWhite = RGB(0xFF, 0xFF, 0xFF);
const COLORREF kColorName = RGB(0x1F, 0x1F, 0x1F);        // 名字
const COLORREF kColorSection = RGB(0x8A, 0x8A, 0x8A);     // 「简介」「用法」「来源」小标题，以及「暂无可用的触发方式」
const COLORREF kColorBody = RGB(0x3C, 0x3C, 0x3C);        // 各段正文
const COLORREF kColorDivider = RGB(0xED, 0xED, 0xED);     // 头部下方的分隔线
const COLORREF kColorTagFill = RGB(0xEE, 0xF0, 0xF3);     // 「企业」等标签的底色，与成员页签机器人行的标签一致
const COLORREF kColorTagText = RGB(0x5B, 0x6B, 0x80);     // 「企业」等标签的文字
const COLORREF kColorAvatarText = RGB(0xFF, 0xFF, 0xFF);  // 头像上的字
// 「机器人」徽标的底色与文字色，与聊天气泡里的徽章（ChatPaintUtil.h 的 chatpaint::kBotBadgeBgColor / kBotBadgeTextColor）一致。
// 不直接包含 ChatPaintUtil.h：它要求调用方先引入 GDI+，本文件还要编进 FlamingoUnit
const COLORREF kColorBotBadgeFill = RGB(0x1E, 0x88, 0xE5);
const COLORREF kColorBotBadgeText = RGB(0xFF, 0xFF, 0xFF);

// ---- 版式（像素）----
const int kPadH = 20;              // 客户区左右内边距
const int kPadTop = 16;            // 客户区上内边距
const int kPadBottom = 16;         // 客户区下内边距
const int kRootGap = 12;           // 头部、分隔线、详情区、按钮行之间的间距
const int kAvatarPx = 52;          // 头像边长
const int kAvatarRadius = 8;       // 头像圆角
const int kHeaderGap = 12;         // 头像与右侧名字之间的间距
const int kHeaderInfoPadTop = 4;   // 名字一栏的上内边距，让名字与徽标整体在头像高度内居中
const int kHeaderInfoGap = 4;      // 名字与徽标行之间的间距
const int kNamePt = 12;            // 名字字号（磅，加粗）
const int kNameH = 24;             // 名字一行的高
const int kBadgeRowH = 20;         // 徽标一行的高
const int kBadgeGap = 6;           // 「机器人」徽标与标签之间的间距
const int kDividerH = 1;           // 分隔线的高
const int kBodyPt = 10;            // 各段正文字号（磅）
const int kSectionTitleH = 20;     // 「简介」等小标题的高
const int kLineMinH = 20;          // 正文一行的最小高度，量出来比它矮时按它占位
const int kDetailGap = 4;          // 详情区里各行之间的间距
const int kSectionGap = 10;        // 详情区里两段之间额外空出的高度
const int kButtonH = 32;           // 按钮行的高
const int kButtonW = 80;           // 「关闭」按钮的宽

// 量自动换行文字时的最窄宽度（像素）：窗口宽度给得过小时避免按 0 宽度量出单行高度
const int kMinTextWidth = 40;

std::unique_ptr<DuiControl> MakeLabel(UINT id, const std::wstring& text, COLORREF color, int pt, bool wrap)
{
    std::unique_ptr<DuiLabel> label(new DuiLabel());
    label->SetCtrlId(id);
    label->SetText(text.c_str());
    label->SetTextColor(color);
    label->SetTextPointSize(pt);
    if (wrap)
    {
        label->SetWordWrap(true);
    }
    else
    {
        label->SetTextAlign(DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    }
    return std::unique_ptr<DuiControl>(label.release());
}

std::unique_ptr<DuiControl> MakeBadge(UINT id, const std::wstring& text, COLORREF fill, COLORREF color)
{
    std::unique_ptr<DuiBadge> badge(new DuiBadge());
    badge->SetCtrlId(id);
    badge->SetText(text.c_str());
    badge->SetBgColor(fill);
    badge->SetTextColor(color);
    return std::unique_ptr<DuiControl>(badge.release());
}

// 量一段自动换行文字在给定宽度下的高度，不低于一行的最小高度
int WrappedHeight(const DuiLabel* label, int width)
{
    const int h = label->MeasureHeight((width > kMinTextWidth) ? width : kMinTextWidth);
    return (h > kLineMinH) ? h : kLineMinH;
}

// 往详情区加一段自动换行的正文，先按 width 量一次高度
void AddWrapped(DuiVBox* detail, UINT id, const std::wstring& text, COLORREF color, int width)
{
    std::unique_ptr<DuiControl> label = MakeLabel(id, text, color, kBodyPt, true);
    const int h = WrappedHeight(static_cast<DuiLabel*>(label.get()), width);
    detail->AddChild(std::move(label), DuiLayout::Hint().Fixed(h));
}

// 往详情区加一个小标题；不是第一段时先空出段间距
void AddSectionTitle(DuiVBox* detail, const std::wstring& text)
{
    if (!detail->Children().empty())
    {
        detail->AddChild(std::unique_ptr<DuiControl>(new DuiLabel()), DuiLayout::Hint().Fixed(kSectionGap));
    }
    detail->AddChild(MakeLabel(0, text, kColorSection, kBodyPt, false), DuiLayout::Hint().Fixed(kSectionTitleH));
}

// 头部：头像、名字，名字下方是「机器人」徽标与标签
std::unique_ptr<DuiControl> BuildHeader(const BotCardView& view)
{
    std::unique_ptr<DuiHBox> header(new DuiHBox());
    header->SetCtrlId(kBotCardIdHeader);
    header->SetGap(kHeaderGap);

    std::unique_ptr<DuiAvatar> avatar(new DuiAvatar());
    avatar->SetShape(DuiAvatar::ShapeRoundRect);
    avatar->SetCornerRadius(kAvatarRadius);
    avatar->SetName((view.m_avatarText.empty() ? view.m_name : view.m_avatarText).c_str());
    avatar->SetFallbackBgColor(view.m_avatarColor);
    avatar->SetInitialsColor(kColorAvatarText);
    header->AddChild(std::unique_ptr<DuiControl>(avatar.release()), DuiLayout::Hint().Fixed(kAvatarPx, kAvatarPx));

    std::unique_ptr<DuiVBox> info(new DuiVBox());
    info->SetPadding(0, kHeaderInfoPadTop, 0, 0);
    info->SetGap(kHeaderInfoGap);
    std::unique_ptr<DuiLabel> name(new DuiLabel());
    name->SetCtrlId(kBotCardIdName);
    name->SetText(view.m_name.c_str());
    name->SetTextColor(kColorName);
    name->SetTextPointSize(kNamePt, true);
    name->SetTextAlign(DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    info->AddChild(std::unique_ptr<DuiControl>(name.release()), DuiLayout::Hint().Fixed(kNameH));

    std::unique_ptr<DuiHBox> badges(new DuiHBox());
    badges->SetGap(kBadgeGap);
    const std::wstring botText = TR(L"机器人");
    badges->AddChild(MakeBadge(kBotCardIdBotBadge, botText, kColorBotBadgeFill, kColorBotBadgeText),
                     DuiLayout::Hint().Fixed(BotBadgeWidth(botText)));
    if (!view.m_tag.empty())
    {
        badges->AddChild(MakeBadge(kBotCardIdTag, view.m_tag, kColorTagFill, kColorTagText),
                         DuiLayout::Hint().Fixed(BotBadgeWidth(view.m_tag)));
    }
    badges->AddChild(std::unique_ptr<DuiControl>(new DuiLabel()), DuiLayout::Hint().Weight(1));
    info->AddChild(std::unique_ptr<DuiControl>(badges.release()), DuiLayout::Hint().Fixed(kBadgeRowH));

    header->AddChild(std::unique_ptr<DuiControl>(info.release()), DuiLayout::Hint().Weight(1));
    return std::unique_ptr<DuiControl>(header.release());
}

// 详情区的内容：简介、用法、来源
std::unique_ptr<DuiControl> BuildDetail(const BotCardView& view, int textWidth)
{
    std::unique_ptr<DuiVBox> detail(new DuiVBox());
    detail->SetCtrlId(kBotCardIdDetail);
    detail->SetBgColor(kColorWhite);
    detail->SetGap(kDetailGap);

    if (!view.m_desc.empty())
    {
        AddSectionTitle(detail.get(), TR(L"简介"));
        AddWrapped(detail.get(), kBotCardIdDesc, view.m_desc, kColorBody, textWidth);
    }

    AddSectionTitle(detail.get(), TR(L"用法"));
    if (view.m_usageLines.empty())
    {
        detail->AddChild(MakeLabel(kBotCardIdNoUsage, TR(L"暂无可用的触发方式"), kColorSection, kBodyPt, false),
                         DuiLayout::Hint().Fixed(kLineMinH));
    }
    for (size_t i = 0; i < view.m_usageLines.size(); ++i)
    {
        //行首圆点是排版符号，不是文字，不需要翻译
        const std::wstring line = std::wstring(L"· ") + view.m_usageLines[i];
        AddWrapped(detail.get(), kBotCardIdUsageFirst + (UINT)i, line, kColorBody, textWidth);
    }

    AddSectionTitle(detail.get(), TR(L"来源"));
    AddWrapped(detail.get(), kBotCardIdSource, view.m_source, kColorBody, textWidth);
    return std::unique_ptr<DuiControl>(detail.release());
}

// 按控件当前的矩形重新排版；矩形没变时 SetRect 不会往下排，所以要用 ForceLayout
void ForceRelayout(DuiControl* ctrl)
{
    if (ctrl == NULL)
    {
        return;
    }
    ctrl->ForceLayout(ctrl->GetRect());
    ctrl->Invalidate();
}

} // anonymous namespace

std::unique_ptr<DuiControl> BuildBotCardBody(const BotCardView& view)
{
    typedef DuiLayout::Hint Hint;
    const int textWidth = kBotCardWindowWidth - 2 * kPadH;

    std::unique_ptr<DuiVBox> root(new DuiVBox());
    root->SetBgColor(kColorWhite);
    root->SetPadding(kPadH, kPadTop, kPadH, kPadBottom);
    root->SetGap(kRootGap);

    root->AddChild(BuildHeader(view), Hint().Fixed(kAvatarPx));

    //分隔线：用 DuiVBox 是因为布局容器里只有它带底色（卡片样式）
    std::unique_ptr<DuiVBox> divider(new DuiVBox());
    divider->SetBgColor(kColorDivider);
    root->AddChild(std::unique_ptr<DuiControl>(divider.release()), Hint().Fixed(kDividerH));

    std::unique_ptr<DuiScrollView> scroll(new DuiScrollView());
    scroll->SetCtrlId(kBotCardIdScroll);
    scroll->SetContent(BuildDetail(view, textWidth));
    scroll->SetAutoContentHeight(true);
    root->AddChild(std::unique_ptr<DuiControl>(scroll.release()), Hint().Weight(1));

    std::unique_ptr<DuiHBox> buttons(new DuiHBox());
    buttons->SetCtrlId(kBotCardIdButtonRow);
    buttons->AddChild(std::unique_ptr<DuiControl>(new DuiLabel()), Hint().Weight(1));
    std::unique_ptr<DuiButton> close(new DuiButton());
    close->SetCtrlId(kBotCardIdClose);
    close->SetText(TR(L"关闭"));
    close->SetButtonType(DuiButton::StylePushButton);
    close->SetVariant(DuiButton::Variant::Default);
    buttons->AddChild(std::unique_ptr<DuiControl>(close.release()), Hint().Fixed(kButtonW));
    root->AddChild(std::unique_ptr<DuiControl>(buttons.release()), Hint().Fixed(kButtonH));
    return std::unique_ptr<DuiControl>(root.release());
}

int FitBotCardBody(DuiControl* root, int windowWidth)
{
    if (root == NULL)
    {
        return 0;
    }
    DuiLayout* rootBox = dynamic_cast<DuiLayout*>(root);
    DuiLayout* detail = dynamic_cast<DuiLayout*>(root->FindCtrlById(kBotCardIdDetail));
    if (rootBox == NULL || detail == NULL)
    {
        return 0;
    }

    //详情区与客户区同宽减去左右内边距；滚动条浮在内容之上，不占宽度
    const int textWidth = windowWidth - 2 * kPadH;
    for (size_t i = 0; i < detail->Children().size(); ++i)
    {
        DuiLabel* label = dynamic_cast<DuiLabel*>(detail->Children()[i].get());
        if (label == NULL || !label->IsWordWrap())
        {
            continue;
        }
        detail->SetHint(label, DuiLayout::Hint().Fixed(WrappedHeight(label, textWidth)));
    }

    //根节点的期望高度里详情区（按权重占位）算 0，再加上详情内容完整显示需要的高度
    return rootBox->GetDesiredSize().cy + detail->GetDesiredSize().cy;
}

int BotCardWindowHeight(int bodyHeight)
{
    const int h = kBotCardTitleBarH + bodyHeight;
    if (h < kBotCardMinWindowHeight)
    {
        return kBotCardMinWindowHeight;
    }
    if (h > kBotCardMaxWindowHeight)
    {
        return kBotCardMaxWindowHeight;
    }
    return h;
}

void RelayoutBotCardBody(DuiControl* root)
{
    if (root == NULL)
    {
        return;
    }
    ForceRelayout(root);
    ForceRelayout(root->FindCtrlById(kBotCardIdScroll));
    ForceRelayout(root->FindCtrlById(kBotCardIdDetail));
}

} // namespace balloonchatwnd
