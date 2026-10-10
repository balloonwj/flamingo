/**
 *  录制完成的提示，说明见 RecordDoneToast.h。
 *  balloonwj@qq.com   2026-10-03
 */
#include "stdafx.h"

#include "RecordDoneToast.h"

#include <memory>
#include "RecordMessages.h"
#include "../../Source/Common/Log/IULog.h"
#include "../../Source/UI/BalloonMain/CornerPopupPlacement.h"
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiButton.h"
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiLabel.h"
#include "../../../third-party/balloonui/balloonui/Controls/Layout/DuiLayout.h"
#include "../../Source/Common/I18n/I18n.h"   // TR：界面文字的多语言查找

namespace screenrecord {

namespace {

//整窗尺寸与标题栏高（逻辑像素）。第 14 期加了文件名一行，高度由 150 加到 178；2026-10-06 加了「打开我的录制」后
//五个按钮排成一行，窗口一度加宽到 508（有「编辑字幕」时 620）。同日用户嫌它太长、太矮，改为与屏幕共享的右下角提示
//同宽的 400，按钮分两行，高度加到 228：内容区高 228 - 28 = 200，上下内边距 2 * 14 + 三行文字 3 * 20 + 两行按钮
//2 * 30 + 五个间距 5 * 8 = 188，余下 12 给文字与按钮之间的弹性占位。有没有「编辑字幕」窗口大小都不变
const int kToastW = 400;
const int kToastH = 228;
const int kTitleBarH = 28;
//内容区内边距与间距、文字行高、按钮宽与按钮行高
const int kPadding = 14;
const int kGap = 8;
const int kLineH = 20;
//第一行「打开」「打开所在文件夹」「打开我的录制」的宽：内容区宽 400 - 2 * 14 = 372，三个按钮加两个间距
//64 + 124 + 120 + 16 = 324，右侧余下的空间由弹性占位填满。「打开我的录制」取 120 而不是按中文六个字算的 112，
//是给英文译文「My recordings」留出余量
const int kPlayW = 64;
const int kFolderW = 124;
const int kManagerW = 120;
//第二行右侧「发送」（或「发送到会话…」）的宽，英文译文「Send to conversation…」也放得下
const int kSendW = 140;
//「编辑字幕」的宽（语音字幕，2026-10-06）：中文四个字约 64，取 104 给英文译文「Edit captions」留出余量。
//它放在第二行左侧，与右侧的发送按钮之间是弹性占位：104 + 140 + 两个间距 16 = 260，不超过 372
const int kCaptionW = 104;
const int kButtonRowH = 30;
//次要文字的颜色
const COLORREF kDetailColor = RGB(110, 114, 122);
//标题栏文字。第 15 期起标题后显示两分钟的关闭倒计时，2026-10-05 用户要求去掉：提示一直保留到用户关闭
const wchar_t* const kToastTitle = TR_NOOP(L"屏幕录制");

} // namespace

CRecordDoneToast::CRecordDoneToast()
    : m_notifyWnd(NULL)
{
}

CRecordDoneToast::~CRecordDoneToast()
{
    Hide();
}

void CRecordDoneToast::Show(const std::wstring& title, const std::wstring& fileName, const std::wstring& detail,
                            const std::wstring& sendText, HWND notifyWnd, bool showCaption)
{
    m_notifyWnd = notifyWnd;
    if (!IsWindow())
    {
        const DWORD exStyle = WS_EX_TOPMOST | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW;
        if (Create(NULL, CWindow::rcDefault, TR(kToastTitle), WS_POPUP, exStyle) == NULL)
        {
            LOG_ERROR("record: create done toast failed.");
            return;
        }
        SetTitleBarHeight(kTitleBarH);
        //右下角提示不加最小化，只留关闭按钮（第 15 期）
        SetButtons(false, false, true);
        SetResizable(false);
        SetIcon(m_icons.TitleBitmap(balloonmain::kProgramTitleIconSize, balloonmain::kOpaqueTitleBarColor));
    }
    //整窗尺寸固定（有没有「编辑字幕」都一样），已弹出时替换内容也照样设一次
    ResizeClient(kToastW, kToastH);
    SetTitle(TR(kToastTitle));
    std::unique_ptr<balloonwjui::DuiVBox> root(new balloonwjui::DuiVBox());
    root->SetPadding(kPadding);
    root->SetGap(kGap);
    std::unique_ptr<balloonwjui::DuiLabel> titleLabel(new balloonwjui::DuiLabel());
    titleLabel->SetText(title.c_str());
    titleLabel->SetTextAlign(DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    root->AddChild(std::move(titleLabel), balloonwjui::DuiLayout::Hint().Fixed(kLineH));
    //文件名（第 14 期）：太长时省略末尾；文件名以年月日时分秒开头，省略后仍能分辨是哪一次录制
    std::unique_ptr<balloonwjui::DuiLabel> nameLabel(new balloonwjui::DuiLabel());
    nameLabel->SetText(fileName.c_str());
    nameLabel->SetTextAlign(DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
    root->AddChild(std::move(nameLabel), balloonwjui::DuiLayout::Hint().Fixed(kLineH));
    std::unique_ptr<balloonwjui::DuiLabel> detailLabel(new balloonwjui::DuiLabel());
    detailLabel->SetText(detail.c_str());
    detailLabel->SetTextColor(kDetailColor);
    detailLabel->SetTextAlign(DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    root->AddChild(std::move(detailLabel), balloonwjui::DuiLayout::Hint().Fixed(kLineH));
    root->AddChild(std::unique_ptr<balloonwjui::DuiControl>(new balloonwjui::DuiControl()),
                   balloonwjui::DuiLayout::Hint().Weight(1));
    //第一行：打开、打开所在文件夹、打开我的录制，靠左排列，右侧余下的空间由弹性占位填满
    std::unique_ptr<balloonwjui::DuiHBox> firstRow(new balloonwjui::DuiHBox());
    firstRow->SetGap(kGap);
    std::unique_ptr<balloonwjui::DuiButton> play(new balloonwjui::DuiButton());
    play->SetText(TR(L"打开"));
    play->SetVariant(balloonwjui::DuiButton::Variant::Default);
    play->SetCtrlId(kIdPlay);
    firstRow->AddChild(std::move(play), balloonwjui::DuiLayout::Hint().Fixed(kPlayW));
    std::unique_ptr<balloonwjui::DuiButton> folder(new balloonwjui::DuiButton());
    folder->SetText(TR(L"打开所在文件夹"));
    folder->SetVariant(balloonwjui::DuiButton::Variant::Default);
    folder->SetCtrlId(kIdFolder);
    firstRow->AddChild(std::move(folder), balloonwjui::DuiLayout::Hint().Fixed(kFolderW));
    std::unique_ptr<balloonwjui::DuiButton> manager(new balloonwjui::DuiButton());
    manager->SetText(TR(L"打开我的录制"));
    manager->SetVariant(balloonwjui::DuiButton::Variant::Default);
    manager->SetCtrlId(kIdManager);
    firstRow->AddChild(std::move(manager), balloonwjui::DuiLayout::Hint().Fixed(kManagerW));
    firstRow->AddChild(std::unique_ptr<balloonwjui::DuiControl>(new balloonwjui::DuiControl()),
                       balloonwjui::DuiLayout::Hint().Weight(1));
    root->AddChild(std::move(firstRow), balloonwjui::DuiLayout::Hint().Fixed(kButtonRowH));

    //第二行：左侧是「编辑字幕」（有字幕草稿时才有），右侧是主按钮「发送」
    std::unique_ptr<balloonwjui::DuiHBox> secondRow(new balloonwjui::DuiHBox());
    secondRow->SetGap(kGap);
    if (showCaption)
    {
        std::unique_ptr<balloonwjui::DuiButton> caption(new balloonwjui::DuiButton());
        caption->SetText(TR(L"编辑字幕"));
        caption->SetVariant(balloonwjui::DuiButton::Variant::Default);
        caption->SetCtrlId(kIdCaption);
        secondRow->AddChild(std::move(caption), balloonwjui::DuiLayout::Hint().Fixed(kCaptionW));
    }
    secondRow->AddChild(std::unique_ptr<balloonwjui::DuiControl>(new balloonwjui::DuiControl()),
                        balloonwjui::DuiLayout::Hint().Weight(1));
    std::unique_ptr<balloonwjui::DuiButton> sendBtn(new balloonwjui::DuiButton());
    sendBtn->SetText(sendText.c_str());
    sendBtn->SetCtrlId(kIdSend);
    secondRow->AddChild(std::move(sendBtn), balloonwjui::DuiLayout::Hint().Fixed(kSendW));
    root->AddChild(std::move(secondRow), balloonwjui::DuiLayout::Hint().Fixed(kButtonRowH));
    SetClientContent(std::move(root));
    balloonmain::MoveWindowToWorkAreaCorner(m_hWnd, balloonmain::kCornerPopupMarginX,
                                            balloonmain::kCornerPopupMarginY);
    ShowWindow(SW_SHOWNA);
}

void CRecordDoneToast::Hide()
{
    m_notifyWnd = NULL;
    if (IsWindow())
        DestroyWindow();
}

void CRecordDoneToast::send(int command)
{
    HWND notify = m_notifyWnd;
    Hide();
    if (notify != NULL && ::IsWindow(notify))
        ::PostMessage(notify, kMsgRecordDoneCommand, static_cast<WPARAM>(command), 0);
}

LRESULT CRecordDoneToast::OnDuiNotify(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
{
    (void)uMsg;
    (void)wParam;
    const balloonwjui::DuiNotify* n = reinterpret_cast<const balloonwjui::DuiNotify*>(lParam);
    if (n != NULL && n->code == static_cast<UINT>(balloonwjui::DUIN_CLICK))
    {
        //打开：提示保留，便于接着打开所在文件夹或发送
        if (n->ctrlId == static_cast<UINT>(kIdPlay))
        {
            if (m_notifyWnd != NULL && ::IsWindow(m_notifyWnd))
                ::PostMessage(m_notifyWnd, kMsgRecordDoneCommand, kCmdPlay, 0);
            return 0;
        }
        //打开所在文件夹：同上，提示保留
        if (n->ctrlId == static_cast<UINT>(kIdFolder))
        {
            if (m_notifyWnd != NULL && ::IsWindow(m_notifyWnd))
                ::PostMessage(m_notifyWnd, kMsgRecordDoneCommand, kCmdOpenFolder, 0);
            return 0;
        }
        //打开我的录制：打开录制管理窗口，提示保留，便于接着发送
        if (n->ctrlId == static_cast<UINT>(kIdManager))
        {
            if (m_notifyWnd != NULL && ::IsWindow(m_notifyWnd))
                ::PostMessage(m_notifyWnd, kMsgRecordDoneCommand, kCmdOpenManager, 0);
            return 0;
        }
        //编辑字幕：打开这次录像的字幕编辑器，提示保留，便于改完字幕后接着发送
        if (n->ctrlId == static_cast<UINT>(kIdCaption))
        {
            if (m_notifyWnd != NULL && ::IsWindow(m_notifyWnd))
                ::PostMessage(m_notifyWnd, kMsgRecordDoneCommand, kCmdEditCaption, 0);
            return 0;
        }
        //发送：发出后关闭提示
        if (n->ctrlId == static_cast<UINT>(kIdSend))
        {
            send(kCmdSend);
            return 0;
        }
    }
    bHandled = FALSE;
    return 0;
}

LRESULT CRecordDoneToast::OnCloseMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
{
    (void)uMsg;
    (void)wParam;
    (void)lParam;
    (void)bHandled;
    Hide();
    return 0;
}

} // namespace screenrecord
