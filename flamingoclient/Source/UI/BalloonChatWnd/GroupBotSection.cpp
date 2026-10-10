/**
 *  GroupBotSection.cpp
 *
 *  群聊窗成员页签「机器人 N」区域的重建与重排，说明见 GroupBotSection.h。
 *  每个机器人一行，2026-10-08 起与成员行同一样式（用户确认）：左边头像（balloonui DuiAvatar），中间名字（DuiLabel），
 *  右边小标签（带底色的 DuiVBox 加 DuiLabel，字号与成员行的「群主」徽标一致）；整行由 GroupBotRow 接收点击，点了发 DUIN_CLICK（控件 id 为 kGIdBotRowFirst 起），群聊窗据此弹资料卡。
 *
 *  balloonwj@qq.com   2026-10-08
 */
#include "stdafx.h"
#include "GroupBotSection.h"

#include <memory>

#include "GroupChatWindowLayout.h"   // kGIdBotSection / kGIdBotTitle / kGIdBotRows
#include "GroupMemberItem.h"         // kGroupMemberAvatarPx / kGroupMemberAvatarGapPx：与成员行同一尺寸
#include "GroupMemberList.h"         // kGroupMemberRowHeight
#include "../../../third-party/balloonui/balloonui/DuiControl.h"
#include "../../../third-party/balloonui/balloonui/DuiPaintAA.h"   // DuiAA::FillRoundRect：悬停底
#include "../../../third-party/balloonui/balloonui/DuiResMgr.h"    // 默认字体：量标签文字的宽度
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiAvatar.h"
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiLabel.h"
#include "../../../third-party/balloonui/balloonui/Controls/Layout/DuiLayout.h"

namespace balloonchatwnd {

static_assert(kBotSectionRowHeight == kGroupMemberRowHeight, "bot rows must be as tall as member rows");

namespace {

//---- 与成员行（GroupMemberItem.cpp）一致的外观 ----
//成员行用固定像素的字体（名字 13 像素、徽标 10 像素加粗），不随 DPI 放大。DuiResMgr 只按磅值缓存字体，
//字高为 MulDiv(磅值, DPI, 72)；DPI 取 72 时字高恰好等于磅值，借此取到指定像素高度的字体
const int      kPixelFontDpi = 72;                        //按像素取字体时传给 DuiResMgr 的 DPI
const int      kRowPadRight = 10;                         //行右侧内边距（像素），与成员行的 kEdgePad 一致；左侧由区域容器留出
const int      kAvatarRadius = 5;                         //头像圆角（像素）
const int      kNamePx = 13;                              //名字字高（像素），与成员行的 kNamePx 一致
const COLORREF kNameColor = RGB(0x3C, 0x3C, 0x3C);        //名字颜色
const COLORREF kAvatarTextColor = RGB(0xFF, 0xFF, 0xFF);  //头像上的字
const COLORREF kHoverFill = RGB(0xF2, 0xF1, 0xEE);        //悬停底色
const int      kHoverRadius = 5;                          //悬停底圆角（像素）
//---- 小标签：尺寸与成员行的「群主」徽标一致；浅灰蓝底、灰蓝字，与成员行「未确认必读」标记同一配色，和橙色的「群主」区分 ----
const COLORREF kTagFill = RGB(0xEE, 0xF0, 0xF3);          //标签底色
const COLORREF kTagColor = RGB(0x5B, 0x6B, 0x80);         //标签文字
const int      kTagPx = 10;                               //标签字高（像素，加粗）
const int      kTagH = 16;                                //标签高度（像素）
const int      kTagRadius = 3;                            //标签圆角（像素）
const int      kTagPadW = 10;                             //标签宽度在文字宽度之外多留的空白（像素，左右各一半）
//---- 资料卡里的 DuiBadge（按 balloonui 默认字体绘制）----
const int      kBadgeChrome = 10;                         //DuiBadge 在文字两侧各留的空白之和（见 DuiBadge::OnPaint）
const int      kBadgeSlack = 4;                           //再多留的余量，避免字体换算误差把胶囊挤窄

/**
 *  GroupBotRow —— 群聊窗成员页签里机器人的一行：头像、名字、小标签的容器。
 *  在 DuiHBox 的基础上补三件事：整行命中都落到本控件（子控件不单独接收鼠标，基类的 OnLButtonUp 据此发出带本行
 *  id 的 DUIN_CLICK）；悬停时画浅灰圆角底并请求重画（UI 约定第 9 条）；光标显示为手形，提示可以点开资料卡。
 */
class GroupBotRow : public balloonwjui::DuiHBox
{
public:
    /**
     *  整行命中：点在本行矩形内时一律返回本控件，头像、名字、标签都不单独接收鼠标。
     *    ptHostClient：宿主客户区坐标。
     *  @return 在本行内返回 this，否则返回 NULL
     */
    balloonwjui::DuiControl* HitTest(POINT ptHostClient) override
    {
        if (!IsVisible() || !::PtInRect(&m_rcItem, ptHostClient))
            return NULL;
        return this;
    }

    /**
     *  鼠标进入：记下悬停并请求重画悬停底。
     */
    bool OnMouseEnter() override
    {
        const bool handled = balloonwjui::DuiHBox::OnMouseEnter();
        Invalidate();
        return handled;
    }

    /**
     *  鼠标离开：清掉悬停并请求重画，去掉悬停底。
     */
    bool OnMouseLeave() override
    {
        const bool handled = balloonwjui::DuiHBox::OnMouseLeave();
        Invalidate();
        return handled;
    }

    /**
     *  光标显示为手形。
     *  @return 已设置光标返回 true
     */
    bool OnSetCursor(POINT /*pt*/) override
    {
        ::SetCursor(::LoadCursor(NULL, IDC_HAND));
        return true;
    }

    /**
     *  悬停时先画浅灰圆角底，再画头像、名字与标签。
     */
    void OnPaint(HDC hdc, const RECT& rcDirty) override
    {
        if (IsHover())
            balloonwjui::DuiAA::FillRoundRect(hdc, m_rcItem, kHoverFill, kHoverRadius);
        balloonwjui::DuiHBox::OnPaint(hdc, rcDirty);
    }
};

//行容器的高度：各行加上行与行之间的间距
int RowsHeight(int rowCount)
{
    if (rowCount <= 0)
    {
        return 0;
    }
    return rowCount * kBotSectionRowHeight + (rowCount - 1) * kBotSectionGap;
}

//指定像素字高的界面字体（DuiResMgr 缓存，调用方不得释放）
HFONT PixelFont(int px, bool bold)
{
    return balloonwjui::DuiResMgr::Inst().GetFontByPointSizeForDpi(px, bold, kPixelFontDpi);
}

//按给定字体量一段文字的宽度（像素）；font 为 NULL 时按系统默认字体
int TextWidth(const std::wstring& text, HFONT font)
{
    if (text.empty())
    {
        return 0;
    }
    HDC hdc = ::GetDC(NULL);
    if (hdc == NULL)
    {
        return 0;
    }
    HFONT old = (font != NULL) ? (HFONT)::SelectObject(hdc, font) : NULL;
    SIZE sz = { 0, 0 };
    ::GetTextExtentPoint32W(hdc, text.c_str(), (int)text.size(), &sz);
    if (old != NULL)
    {
        ::SelectObject(hdc, old);
    }
    ::ReleaseDC(NULL, hdc);
    return sz.cx;
}

} // anonymous namespace

std::unique_ptr<balloonwjui::DuiControl> BuildGroupBotRow(const GroupBotSectionRow& r)
{
    std::unique_ptr<GroupBotRow> row(new GroupBotRow());
    row->SetCtrlId(r.m_ctrlId);
    row->SetGap(kGroupMemberAvatarGapPx);
    row->SetPadding(0, 0, kRowPadRight, 0);

    std::unique_ptr<balloonwjui::DuiAvatar> avatar(new balloonwjui::DuiAvatar());
    avatar->SetShape(balloonwjui::DuiAvatar::ShapeRoundRect);
    avatar->SetCornerRadius(kAvatarRadius);
    avatar->SetName((r.m_avatarText.empty() ? r.m_text : r.m_avatarText).c_str());
    avatar->SetFallbackBgColor(r.m_avatarColor);
    avatar->SetInitialsColor(kAvatarTextColor);
    row->AddChild(std::move(avatar), balloonwjui::DuiLayout::Hint()
                                         .Fixed(kGroupMemberAvatarPx, kGroupMemberAvatarPx)
                                         .AlignC(balloonwjui::DuiLayout::AlignCenter));

    std::unique_ptr<balloonwjui::DuiLabel> name(new balloonwjui::DuiLabel());
    name->SetText(r.m_text.c_str());
    name->SetFont(PixelFont(kNamePx, false));
    name->SetTextColor(kNameColor);
    name->SetTextAlign(DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    row->AddChild(std::move(name), balloonwjui::DuiLayout::Hint().Weight(1));

    //标签：带底色与圆角的容器里放一段居中的文字。不用 DuiBadge，是因为它按 balloonui 默认字体绘制、不能指定字体，
    //在高 DPI 下比成员行的「群主」徽标大一号
    if (!r.m_tag.empty())
    {
        HFONT tagFont = PixelFont(kTagPx, true);
        std::unique_ptr<balloonwjui::DuiVBox> tag(new balloonwjui::DuiVBox());
        tag->SetBgColor(kTagFill);
        tag->SetCornerRadius(kTagRadius);
        std::unique_ptr<balloonwjui::DuiLabel> tagText(new balloonwjui::DuiLabel());
        tagText->SetText(r.m_tag.c_str());
        tagText->SetFont(tagFont);
        tagText->SetTextColor(kTagColor);
        tagText->SetTextAlign(DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        tag->AddChild(std::move(tagText), balloonwjui::DuiLayout::Hint().Weight(1));
        const int tagWidth = TextWidth(r.m_tag, tagFont) + kTagPadW;
        row->AddChild(std::move(tag), balloonwjui::DuiLayout::Hint()
                                          .Fixed(tagWidth, kTagH)
                                          .AlignC(balloonwjui::DuiLayout::AlignCenter));
    }
    return std::unique_ptr<balloonwjui::DuiControl>(row.release());
}

namespace {

//按控件当前的矩形重新排版并请求重画；矩形没变时 SetRect 不会往下排，所以要用 ForceLayout
void Relayout(balloonwjui::DuiControl* ctrl)
{
    if (ctrl == NULL)
    {
        return;
    }
    ctrl->ForceLayout(ctrl->GetRect());
    ctrl->Invalidate();
}

} // anonymous namespace

int BotBadgeWidth(const std::wstring& text)
{
    if (text.empty())
    {
        return 0;
    }
    return TextWidth(text, balloonwjui::DuiResMgr::Inst().GetDefaultFont()) + kBadgeChrome + kBadgeSlack;
}

int GroupBotSectionHeight(int rowCount)
{
    return 2 * kBotSectionPadV + kBotSectionTitleHeight + kBotSectionGap + RowsHeight(rowCount);
}

bool ApplyGroupBotSection(balloonwjui::DuiControl* root, const GroupBotSectionView& view)
{
    if (root == NULL)
    {
        return false;
    }
    balloonwjui::DuiControl* section = root->FindCtrlById(kGIdBotSection);
    balloonwjui::DuiControl* rows = root->FindCtrlById(kGIdBotRows);
    balloonwjui::DuiLabel* title = dynamic_cast<balloonwjui::DuiLabel*>(root->FindCtrlById(kGIdBotTitle));
    balloonwjui::DuiLayout* rowsBox = dynamic_cast<balloonwjui::DuiLayout*>(rows);
    balloonwjui::DuiLayout* sectionBox = dynamic_cast<balloonwjui::DuiLayout*>(section);
    if (section == NULL || rowsBox == NULL || sectionBox == NULL || title == NULL)
    {
        return false;
    }

    //摘掉旧行，按 view 重建
    while (!rows->Children().empty())
    {
        rows->RemoveChild(rows->Children().back().get());
    }
    if (view.m_show)
    {
        title->SetText(view.m_title.c_str());
        rowsBox->SetGap(kBotSectionGap);
        for (size_t i = 0; i < view.m_rows.size(); ++i)
        {
            rowsBox->AddChild(BuildGroupBotRow(view.m_rows[i]), balloonwjui::DuiLayout::Hint().Fixed(kBotSectionRowHeight));
        }
    }

    //区域与行容器的高度
    const int rowCount = (int)rows->Children().size();
    sectionBox->SetHint(rows, balloonwjui::DuiLayout::Hint().Fixed(RowsHeight(rowCount)));
    balloonwjui::DuiLayout* parentBox = dynamic_cast<balloonwjui::DuiLayout*>(section->GetParent());
    if (parentBox != NULL)
    {
        parentBox->SetHint(section, balloonwjui::DuiLayout::Hint().Fixed(view.m_show ? GroupBotSectionHeight(rowCount) : 0));
    }
    section->SetVisible(view.m_show);

    //逐层重排：成员容器决定区域的矩形，区域决定行容器的矩形，行容器给各行排版。区域或行容器的矩形没变时，
    //上一层的排版不会往下排（开发约定文档 UI 约定第 5 条），所以三层都要 ForceLayout
    Relayout(section->GetParent());
    if (view.m_show)
    {
        Relayout(section);
        Relayout(rows);
    }
    return true;
}

} // namespace balloonchatwnd
