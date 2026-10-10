/**
 *  右下角来电弹窗的共用版式，说明见 IncomingCallLayout.h。
 *  balloonwj@qq.com   2026-10-05
 */
#include "stdafx.h"
#include "IncomingCallLayout.h"

#include "../Call/CallButtonArt.h"            //「拒绝」「接听」的圆形图标按钮
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiAvatar.h"
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiButton.h"
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiLabel.h"
#include "../../../third-party/balloonui/balloonui/Controls/Layout/DuiLayout.h"

namespace flamingomedia {

namespace {

//内容区四周的留白（逻辑像素）
const int kBodyPadX = 20;
const int kBodyPadTop = 18;
const int kBodyPadBottom = 16;
//头像与文字之间的间距
const int kAvatarTextGap = 14;
//两行文字的高度与行间距
const int kLine1H = 26;
const int kLine2H = 20;
const int kLineGap = 4;
//两行文字的字号（磅）：第一行加粗、稍大
const int kLine1Pt = 12;
const int kLine2Pt = 10;
//文字颜色：第一行深色、第二行灰色
const COLORREF kLine1Color = RGB(0x22, 0x22, 0x22);
const COLORREF kLine2Color = RGB(0x7A, 0x7F, 0x8A);
//两个圆形按钮之间的间距（像素）：比通话窗底部栏略宽，接听与拒绝离得开一些，不易误点
const int kButtonGap = 20;

//弹性空白
std::unique_ptr<balloonwjui::DuiControl> Spacer()
{
    return std::unique_ptr<balloonwjui::DuiControl>(new balloonwjui::DuiControl());
}

//建一个圆形图标按钮：先把提示文字设成按钮文字（位图画不出来时仍能看出是什么按钮），装上位图后文字被清掉
std::unique_ptr<balloonwjui::DuiButton> MakeRoundButton(UINT ctrlId, int actionId, call::CallButtonArtCache& art)
{
    std::unique_ptr<balloonwjui::DuiButton> btn(new balloonwjui::DuiButton());
    btn->SetButtonType(balloonwjui::DuiButton::StylePushButton);
    btn->SetCtrlId(ctrlId);
    const std::wstring tip = call::TooltipForAction(actionId, false, false);
    btn->SetText(tip.c_str());
    art.Apply(btn.get(), call::LookForAction(actionId, false, false), tip);
    return btn;
}

} // namespace

IncomingCallBody::IncomingCallBody()
    : m_avatar(NULL)
    , m_line1(NULL)
    , m_line2(NULL)
{
}

IncomingCallBody BuildIncomingCallBody(std::unique_ptr<balloonwjui::DuiAvatar> avatar, const std::wstring& line1,
                                       const std::wstring& line2, UINT declineId, UINT acceptId,
                                       call::CallButtonArtCache& art)
{
    IncomingCallBody body;
    std::unique_ptr<balloonwjui::DuiVBox> root(new balloonwjui::DuiVBox());

    //上半部：头像 + 两行文字（文字在头像高度内竖直居中）
    std::unique_ptr<balloonwjui::DuiHBox> top(new balloonwjui::DuiHBox());
    if (avatar)
    {
        body.m_avatar = avatar.get();
        top->AddChild(std::move(avatar), balloonwjui::DuiLayout::Hint().Fixed(kIncomingAvatarPx, kIncomingAvatarPx));
    }
    else
    {
        top->AddChild(Spacer(), balloonwjui::DuiLayout::Hint().Fixed(kIncomingAvatarPx));
    }
    top->AddChild(Spacer(), balloonwjui::DuiLayout::Hint().Fixed(kAvatarTextGap));

    std::unique_ptr<balloonwjui::DuiVBox> texts(new balloonwjui::DuiVBox());
    texts->AddChild(Spacer(), balloonwjui::DuiLayout::Hint().Weight(1));
    std::unique_ptr<balloonwjui::DuiLabel> first(new balloonwjui::DuiLabel());
    first->SetText(line1.c_str());
    first->SetTextColor(kLine1Color);
    first->SetTextPointSize(kLine1Pt, true);
    first->SetTextAlign(DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    body.m_line1 = first.get();
    texts->AddChild(std::move(first), balloonwjui::DuiLayout::Hint().Fixed(kLine1H));
    texts->AddChild(Spacer(), balloonwjui::DuiLayout::Hint().Fixed(kLineGap));
    std::unique_ptr<balloonwjui::DuiLabel> second(new balloonwjui::DuiLabel());
    second->SetText(line2.c_str());
    second->SetTextColor(kLine2Color);
    second->SetTextPointSize(kLine2Pt, false);
    second->SetTextAlign(DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    body.m_line2 = second.get();
    texts->AddChild(std::move(second), balloonwjui::DuiLayout::Hint().Fixed(kLine2H));
    texts->AddChild(Spacer(), balloonwjui::DuiLayout::Hint().Weight(1));
    top->AddChild(std::move(texts), balloonwjui::DuiLayout::Hint().Weight(1));
    root->AddChild(std::move(top), balloonwjui::DuiLayout::Hint().Fixed(kIncomingAvatarPx)
                                       .Margin(kBodyPadX, kBodyPadTop, kBodyPadX, 0));

    root->AddChild(Spacer(), balloonwjui::DuiLayout::Hint().Weight(1));

    //底部右侧：红色的拒绝 + 绿色的接听（危险操作在左，与主流 IM 一致）
    const int btnPx = call::kCallRoundBtnPx;
    std::unique_ptr<balloonwjui::DuiHBox> buttons(new balloonwjui::DuiHBox());
    buttons->AddChild(Spacer(), balloonwjui::DuiLayout::Hint().Weight(1));
    buttons->AddChild(MakeRoundButton(declineId, call::kActionReject, art), balloonwjui::DuiLayout::Hint().Fixed(btnPx));
    buttons->AddChild(Spacer(), balloonwjui::DuiLayout::Hint().Fixed(kButtonGap));
    buttons->AddChild(MakeRoundButton(acceptId, call::kActionAccept, art), balloonwjui::DuiLayout::Hint().Fixed(btnPx));
    root->AddChild(std::move(buttons), balloonwjui::DuiLayout::Hint().Fixed(btnPx)
                                           .Margin(kBodyPadX, 0, kBodyPadX, kBodyPadBottom));

    body.m_root = std::move(root);
    return body;
}

} // namespace flamingomedia
