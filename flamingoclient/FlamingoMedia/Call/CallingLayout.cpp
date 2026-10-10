/**
 *  一对一通话窗「呼叫中」态的控件树，说明见 CallingLayout.h。
 *  balloonwj@qq.com   2026-10-05
 */
#include "stdafx.h"
#include "CallingLayout.h"

#include "CallVideoCanvas.h"
#include "CallButtonArt.h"                    //「取消」的红色圆形图标按钮
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiButton.h"
#include "../../../third-party/balloonui/balloonui/Controls/Layout/DuiLayout.h"

namespace call {

CallingBody::CallingBody()
    : m_canvas(NULL)
{
}

CallingBody BuildCallingBody(UINT canvasId, UINT cancelId, CallButtonArtCache& art)
{
    CallingBody body;
    std::unique_ptr<balloonwjui::DuiVBox> root(new balloonwjui::DuiVBox());

    //画面区：语音态画法（浅灰底、圆形头像、下面一行文字），不响应点击；四周留白与接通后的语音态一致，
    //对方接听后换成接通态的控件树时画面区不跳动
    std::unique_ptr<CallVideoCanvas> canvas(new CallVideoCanvas());
    canvas->SetCtrlId(canvasId);
    canvas->SetVoiceArt(true);
    body.m_canvas = canvas.get();
    root->AddChild(std::move(canvas), balloonwjui::DuiLayout::Hint().Weight(1)
                                          .Margin(kCallArtMargin, kCallArtMargin, kCallArtMargin, 0));

    //底部栏：左侧弹性空白，右侧「取消」（红色圆形图标按钮，与接通态的挂断同一位置、同一外观），竖直居中
    std::unique_ptr<balloonwjui::DuiHBox> bottom(new balloonwjui::DuiHBox());
    bottom->AddChild(std::unique_ptr<balloonwjui::DuiControl>(new balloonwjui::DuiControl()),
                     balloonwjui::DuiLayout::Hint().Weight(1));
    std::unique_ptr<balloonwjui::DuiButton> cancel(new balloonwjui::DuiButton());
    cancel->SetButtonType(balloonwjui::DuiButton::StylePushButton);
    cancel->SetCtrlId(cancelId);
    const CallButtonLook look = LookForAction(kActionCancel, false, false);
    const std::wstring tip = TooltipForAction(kActionCancel, false, false);
    //先设文字：位图画不出来时按钮仍显示「取消」，不至于变成空白；装上位图后文字会被清掉
    cancel->SetText(tip.c_str());
    art.Apply(cancel.get(), look, tip);
    bottom->AddChild(std::move(cancel), balloonwjui::DuiLayout::Hint().Fixed(look.m_diameter, look.m_diameter)
                                            .Margin(0, 0, kCallSideMargin, 0)
                                            .AlignC(balloonwjui::DuiLayout::AlignCenter));
    root->AddChild(std::move(bottom), balloonwjui::DuiLayout::Hint().Fixed(kCallBottomBarH));

    body.m_root = std::move(root);
    return body;
}

} // namespace call
