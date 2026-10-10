/**
 *  远程桌面会话提示对话框的提示文字与控件树，说明见 RdDialogLayout.h。
 *  FlamingoMedia.dll 与 FlamingoUnit 测试工程编译同一份源文件。
 *  balloonwj@qq.com   2026-10-10
 */
#include "stdafx.h"
#include "RdDialogLayout.h"

#include "../../../third-party/balloonui/balloonui/Controls/Layout/DuiLayout.h"   // DuiVBox / DuiHBox
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiLabel.h"
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiButton.h"
#include "../../Source/Common/I18n/I18n.h"   // TR：界面文字的多语言查找

using namespace balloonwjui;

namespace rd {

namespace {

const COLORREF kMsgColor = RGB(0x33, 0x33, 0x33);   // 提示文字色

// 截断名字时末尾加的省略号「…」
const wchar_t kEllipsis = L'\x2026';

// 建一个按钮
std::unique_ptr<DuiControl> MakeButton(UINT id, LPCTSTR text, DuiButton::Variant variant)
{
    std::unique_ptr<DuiButton> button(new DuiButton());
    button->SetButtonType(DuiButton::StylePushButton);
    button->SetVariant(variant);
    button->SetCtrlId(id);
    button->SetText(text);
    return std::unique_ptr<DuiControl>(button.release());
}

} // anonymous namespace

CString ShortenRdPeerName(const CString& name)
{
    if (name.GetLength() <= kRdMaxPeerNameChars)
    {
        return name;
    }
    //留一个字符给省略号，总长仍为 kRdMaxPeerNameChars
    CString shortened = name.Left(kRdMaxPeerNameChars - 1);
    shortened += kEllipsis;
    return shortened;
}

CString MakeRdDialogText(int mode, const CString& peerName)
{
    const CString name = peerName.IsEmpty() ? CString(TR(L"对方")) : ShortenRdPeerName(peerName);
    if (mode == kRdModeAuth)
    {
        return CString(TRF(L"{0} 请求远程控制你的电脑", name).c_str());
    }
    return CString(TRF(L"正在呼叫 {0}，等待对方接受…", name).c_str());
}

std::unique_ptr<DuiControl> BuildRdDialogBody(int mode)
{
    typedef DuiLayout::Hint Hint;
    std::unique_ptr<DuiVBox> root(new DuiVBox());
    root->SetPadding(0, 0, 0, kRdBottomPad);

    //提示区：文字上下各一块等权重的空白，ApplyRdDialogText 按折行后的高度给文字定高，文字就落在垂直正中
    std::unique_ptr<DuiVBox> msgBox(new DuiVBox());
    msgBox->SetCtrlId(kRdIdMsgBox);
    msgBox->SetPadding(kRdMsgPadH, 0, kRdMsgPadH, 0);
    msgBox->AddChild(std::unique_ptr<DuiControl>(new DuiControl()), Hint().Weight(1));
    std::unique_ptr<DuiLabel> msg(new DuiLabel());
    msg->SetCtrlId(kRdIdMsg);
    msg->SetTextColor(kMsgColor);
    msg->SetWordWrap(true);
    msg->SetTextAlign(DT_CENTER | DT_TOP | DT_WORDBREAK | DT_NOPREFIX);
    msgBox->AddChild(std::unique_ptr<DuiControl>(msg.release()), Hint().Fixed(0));
    msgBox->AddChild(std::unique_ptr<DuiControl>(new DuiControl()), Hint().Weight(1));
    root->AddChild(std::unique_ptr<DuiControl>(msgBox.release()), Hint().Weight(1));

    //按钮栏：按钮组左右各一块等权重的空白，整体水平居中（与通话邀请对话框 CBalloonCallDlg 同一套观感）
    std::unique_ptr<DuiHBox> bottom(new DuiHBox());
    bottom->AddChild(std::unique_ptr<DuiControl>(new DuiControl()), Hint().Weight(1));
    if (mode == kRdModeAuth)
    {
        bottom->AddChild(MakeButton(kRdIdBtnLeft, TR(L"拒绝"), DuiButton::Variant::Default), Hint().Fixed(kRdBtnWidth));
        bottom->AddChild(std::unique_ptr<DuiControl>(new DuiControl()), Hint().Fixed(kRdBtnGap));
        bottom->AddChild(MakeButton(kRdIdBtnRight, TR(L"接受"), DuiButton::Variant::Primary), Hint().Fixed(kRdBtnWidth));
    }
    else
    {
        bottom->AddChild(MakeButton(kRdIdBtnRight, TR(L"取消"), DuiButton::Variant::Default), Hint().Fixed(kRdBtnWidth));
    }
    bottom->AddChild(std::unique_ptr<DuiControl>(new DuiControl()), Hint().Weight(1));
    root->AddChild(std::unique_ptr<DuiControl>(bottom.release()), Hint().Fixed(kRdBottomBarH));
    return std::unique_ptr<DuiControl>(root.release());
}

void ApplyRdDialogText(DuiControl* root, const CString& text)
{
    if (root == NULL)
    {
        return;
    }
    DuiVBox* msgBox = dynamic_cast<DuiVBox*>(root->FindCtrlById(kRdIdMsgBox));
    DuiLabel* msg = dynamic_cast<DuiLabel*>(root->FindCtrlById(kRdIdMsg));
    if (msgBox == NULL || msg == NULL)
    {
        return;
    }
    msg->SetText(text);
    const RECT rc = msgBox->GetRect();
    const int textWidth = (rc.right - rc.left) - kRdMsgPadH * 2;
    const int boxHeight = rc.bottom - rc.top;
    if (textWidth <= 0 || boxHeight <= 0)
    {
        return;
    }
    int height = msg->MeasureHeight(textWidth);
    if (height > boxHeight)
    {
        height = boxHeight;
    }
    //SetHint 会立即按提示区当前矩形重排并重画
    msgBox->SetHint(msg, DuiLayout::Hint().Fixed(height));
}

} // namespace rd
