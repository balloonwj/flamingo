/**
 *  退出、登出、电脑关机或注销前的「云端录像」对话框，说明见 CloudExitDialog.h。
 *  balloonwj@qq.com   2026-10-07
 */
#include "stdafx.h"

#include "CloudExitDialog.h"

#include <memory>
#include "../Api/FlamingoMediaApi.h"   // MediaCloudExitKind、MediaCloudExitChoice
#include "../../Source/Common/Log/IULog.h"
#include "../../Source/Common/I18n/I18n.h"         // TR / TRF：界面文字的多语言查找
#include "../../Source/Common/I18n/I18nLayout.h"   // MeasureUiTextWidth：按文字宽度加宽按钮
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiButton.h"
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiLabel.h"
#include "../../../third-party/balloonui/balloonui/Controls/Layout/DuiLayout.h"

namespace screenrecord {

namespace {

//整窗宽、标题栏高（逻辑像素）
const int kDlgW = 480;
const int kTitleBarH = 32;
//内容区内边距与控件间距
const int kPad = 16;
const int kGap = 10;
//说明文字一行的高度；清单上方的说明占一行，下方的说明占两行
const int kLineH = 20;
const int kIntroLines = 1;
const int kNoteLines = 3;
//勾选项行高
const int kCheckH = 26;
//按钮行高与按钮最小宽度
const int kButtonRowH = 32;
const int kBtnMinW = 96;
//说明文字与清单的颜色（深灰）
const COLORREF kTextColor = RGB(40, 44, 52);
const COLORREF kListColor = RGB(70, 76, 88);

//按钮下标对应的控件 id
UINT buttonIdOf(int index)
{
    return static_cast<UINT>(CCloudExitDialog::kIdButtonFirst + index);
}

} // namespace

CloudExitDialogSpec::CloudExitDialogSpec()
    : m_hasAbandonCheck(false)
{
}

CloudExitDialogSpec MakeCloudExitDialogSpec(int kind)
{
    CloudExitDialogSpec s;
    s.m_title = TR(L"云端录像");
    switch (kind)
    {
    //电脑关机或重启：分不清是哪一种，让用户选；放弃与否用勾选项
    case flamingomedia::media_cloud_exit_shutdown:
        s.m_intro = TR(L"电脑正要关机或重启，以下云端录像还没有生成或上传完成：");
        s.m_note = TR(L"不勾选时，下次登录后会接着合成或上传；勾选后这些录像将从云端删除，本机的录像文件不受影响。");
        s.m_buttons[kCloudExitButtonFirst] = TR(L"关机");
        s.m_buttons[kCloudExitButtonSecond] = TR(L"重启");
        s.m_buttons[kCloudExitButtonThird] = TR(L"取消关机");
        s.m_hasAbandonCheck = true;
        s.m_checkText = TR(L"同时放弃这些录像（从云端删除）");
        return s;

    //登出账号
    case flamingomedia::media_cloud_exit_logout:
        s.m_buttons[kCloudExitButtonFirst] = TR(L"登出，下次继续");
        s.m_buttons[kCloudExitButtonSecond] = TR(L"登出并放弃这些录像");
        s.m_buttons[kCloudExitButtonThird] = TR(L"不登出");
        break;

    //Windows 注销
    case flamingomedia::media_cloud_exit_logoff:
        s.m_buttons[kCloudExitButtonFirst] = TR(L"注销，下次继续");
        s.m_buttons[kCloudExitButtonSecond] = TR(L"注销并放弃这些录像");
        s.m_buttons[kCloudExitButtonThird] = TR(L"不注销");
        break;

    //退出程序（以及不认识的场合）
    default:
        s.m_buttons[kCloudExitButtonFirst] = TR(L"退出，下次继续");
        s.m_buttons[kCloudExitButtonSecond] = TR(L"退出并放弃这些录像");
        s.m_buttons[kCloudExitButtonThird] = TR(L"不退出");
        break;
    }
    s.m_intro = TR(L"以下云端录像还没有生成或上传完成：");
    s.m_note = TR(L"选择「下次继续」，下次登录后会接着合成或上传；选择「放弃」，这些录像将从云端删除，本机的录像文件不受影响。");
    return s;
}

int CloudExitChoiceFor(int kind, int button, bool abandonChecked)
{
    if (kind == flamingomedia::media_cloud_exit_shutdown)
    {
        //关机场合：第一个按钮关机、第二个重启，放弃与否看勾选项；第三个按钮与关窗为取消关机
        if (button == kCloudExitButtonFirst)
            return abandonChecked ? flamingomedia::media_cloud_exit_shutdown_abandon
                                  : flamingomedia::media_cloud_exit_shutdown_keep;
        if (button == kCloudExitButtonSecond)
            return abandonChecked ? flamingomedia::media_cloud_exit_restart_abandon
                                  : flamingomedia::media_cloud_exit_restart_keep;
        return flamingomedia::media_cloud_exit_cancel;
    }
    //退出、登出、注销：第一个按钮下次继续、第二个放弃，第三个按钮与关窗为不退出
    if (button == kCloudExitButtonFirst)
        return flamingomedia::media_cloud_exit_keep;
    if (button == kCloudExitButtonSecond)
        return flamingomedia::media_cloud_exit_abandon;
    return flamingomedia::media_cloud_exit_cancel;
}

bool CloudExitChoiceAbandons(int choice)
{
    return choice == flamingomedia::media_cloud_exit_abandon || choice == flamingomedia::media_cloud_exit_shutdown_abandon ||
           choice == flamingomedia::media_cloud_exit_restart_abandon;
}

std::wstring MakeCloudExitListText(const std::vector<std::wstring>& titles)
{
    std::wstring text;
    const size_t listed = titles.size() > static_cast<size_t>(kCloudExitMaxListed) ? static_cast<size_t>(kCloudExitMaxListed)
                                                                                    : titles.size();
    for (size_t i = 0; i < listed; ++i)
    {
        if (!text.empty())
            text += L"\n";
        //「· 」为清单的项目符号，与语言无关
        text += L"· ";
        text += titles[i].empty() ? std::wstring(TR(L"（未命名的录像）")) : titles[i];
    }
    if (titles.size() > listed)
    {
        //{0} 为全部未完成的条数
        text += L"\n";
        text += TRF(L"等 {0} 条", static_cast<int>(titles.size()));
    }
    return text;
}

CCloudExitDialog::CCloudExitDialog()
    : m_kind(flamingomedia::media_cloud_exit_app)
    , m_result(flamingomedia::media_cloud_exit_cancel)
    , m_decided(false)
    , m_check(NULL)
{
}

CCloudExitDialog::~CCloudExitDialog()
{
    Close();
}

bool CCloudExitDialog::Open(int kind, const std::vector<std::wstring>& titles)
{
    Close();
    m_kind = kind;
    m_result = flamingomedia::media_cloud_exit_cancel;
    m_decided = false;
    m_check = NULL;
    const CloudExitDialogSpec spec = MakeCloudExitDialogSpec(kind);
    //不设所有者窗口（原为让按钮通知发回本窗口；2026-10-14 起有无所有者，控件通知都发回本窗口，见头文件说明）；
    //出现在任务栏，关机时用户回到桌面能找到它
    if (Create(NULL, CWindow::rcDefault, spec.m_title.c_str(), WS_POPUP, WS_EX_TOPMOST | WS_EX_APPWINDOW) == NULL)
    {
        LOG_ERROR("cloud exit dialog: create failed, error %lu.", ::GetLastError());
        return false;
    }
    SetTitle(spec.m_title.c_str());
    //标题栏左侧与任务栏的程序图标（开发约定文档 UI 约定第 10 条）：标题栏不透明，底色与它一致
    SetIcon(m_programIcons.TitleBitmap(balloonmain::kProgramTitleIconSize, balloonmain::kOpaqueTitleBarColor));
    m_programIcons.ApplyTaskIcons(m_hWnd);
    SetTitleBarHeight(kTitleBarH);
    SetButtons(false, false, true);
    SetResizable(false);

    const std::wstring listText = MakeCloudExitListText(titles);
    int listLines = 1;
    for (size_t i = 0; i < listText.size(); ++i)
    {
        if (listText[i] == L'\n')
            ++listLines;
    }

    std::unique_ptr<balloonwjui::DuiVBox> root(new balloonwjui::DuiVBox());
    root->SetPadding(kPad, kPad, kPad, kPad);
    root->SetGap(kGap);
    std::unique_ptr<balloonwjui::DuiLabel> intro(new balloonwjui::DuiLabel());
    intro->SetText(spec.m_intro.c_str());
    intro->SetTextColor(kTextColor);
    intro->SetTextAlign(DT_LEFT | DT_TOP | DT_WORDBREAK | DT_END_ELLIPSIS);
    root->AddChild(std::move(intro), balloonwjui::DuiLayout::Hint().Fixed(kIntroLines * kLineH));
    std::unique_ptr<balloonwjui::DuiLabel> list(new balloonwjui::DuiLabel());
    list->SetText(listText.c_str());
    list->SetTextColor(kListColor);
    list->SetCtrlId(kIdList);
    list->SetTextAlign(DT_LEFT | DT_TOP | DT_WORDBREAK | DT_END_ELLIPSIS);
    root->AddChild(std::move(list), balloonwjui::DuiLayout::Hint().Fixed(listLines * kLineH));
    std::unique_ptr<balloonwjui::DuiLabel> note(new balloonwjui::DuiLabel());
    note->SetText(spec.m_note.c_str());
    note->SetTextColor(kTextColor);
    note->SetTextAlign(DT_LEFT | DT_TOP | DT_WORDBREAK | DT_END_ELLIPSIS);
    root->AddChild(std::move(note), balloonwjui::DuiLayout::Hint().Fixed(kNoteLines * kLineH));
    if (spec.m_hasAbandonCheck)
    {
        std::unique_ptr<balloonwjui::DuiButton> check(new balloonwjui::DuiButton());
        check->SetButtonType(balloonwjui::DuiButton::StyleCheckbox);
        check->SetText(spec.m_checkText.c_str());
        check->SetCtrlId(kIdAbandonCheck);
        check->SetCheck(false, false);
        m_check = check.get();
        root->AddChild(std::move(check), balloonwjui::DuiLayout::Hint().Fixed(kCheckH));
    }
    //按钮行：靠右，从左到右为继续退出（主色）、放弃（危险色；关机场合的「重启」为默认色）、不退出
    std::unique_ptr<balloonwjui::DuiHBox> row(new balloonwjui::DuiHBox());
    row->SetGap(kGap);
    row->AddChild(std::unique_ptr<balloonwjui::DuiControl>(new balloonwjui::DuiControl()),
                  balloonwjui::DuiLayout::Hint().Weight(1));
    int rowW = 0;
    //按钮文字用的默认字体：按本机最高的显示器 DPI 取字高，窗口放在哪块显示器上都放得下
    const int buttonFontPx = i18n::DefaultFontPixelHeightForDpi(i18n::MaxMonitorDpi());
    for (int i = 0; i < kCloudExitButtonCount; ++i)
    {
        std::unique_ptr<balloonwjui::DuiButton> b(new balloonwjui::DuiButton());
        b->SetButtonType(balloonwjui::DuiButton::StylePushButton);
        balloonwjui::DuiButton::Variant variant = balloonwjui::DuiButton::Variant::Default;
        if (i == kCloudExitButtonFirst)
            variant = balloonwjui::DuiButton::Variant::Primary;
        else if (i == kCloudExitButtonSecond && !spec.m_hasAbandonCheck)
            variant = balloonwjui::DuiButton::Variant::Danger;
        b->SetVariant(variant);
        b->SetText(spec.m_buttons[i].c_str());
        b->SetCtrlId(buttonIdOf(i));
        //按钮宽度按文字实际宽度取：DefaultButtonWidthForText 在简体中文界面下恒为设计宽度，
        //而「登出并放弃这些录像」这类原文本身就放不进 96 像素，所以不论界面语言都量一次
        const int need = i18n::MeasureUiTextWidth(spec.m_buttons[i].c_str(), buttonFontPx, false) +
                         i18n::kDefaultButtonTextPadPx;
        const int w = (need > kBtnMinW) ? need : kBtnMinW;
        rowW += w + kGap;
        row->AddChild(std::move(b), balloonwjui::DuiLayout::Hint().Fixed(w));
    }
    root->AddChild(std::move(row), balloonwjui::DuiLayout::Hint().Fixed(kButtonRowH));
    SetClientContent(std::move(root));

    //按钮按文字加宽后整窗也跟着加宽，不小于设计宽度
    int w = rowW + 2 * kPad;
    if (w < kDlgW)
        w = kDlgW;
    int h = kTitleBarH + 2 * kPad + (kIntroLines + listLines + kNoteLines) * kLineH + kButtonRowH + 3 * kGap;
    if (spec.m_hasAbandonCheck)
        h += kCheckH + kGap;
    RECT work;
    ::SystemParametersInfo(SPI_GETWORKAREA, 0, &work, 0);
    const int x = work.left + ((work.right - work.left) - w) / 2;
    const int y = work.top + ((work.bottom - work.top) - h) / 2;
    ::SetWindowPos(m_hWnd, HWND_TOPMOST, x, y, w, h, SWP_SHOWWINDOW);
    LOG_INFO("cloud exit dialog: opened, kind %d, %u records.", kind, static_cast<unsigned>(titles.size()));
    return true;
}

int CCloudExitDialog::RunModal(HWND owner, int kind, const std::vector<std::wstring>& titles)
{
    if (!Open(kind, titles))
    {
        //建不出窗口时不挡住退出：录像下次登录后接着做
        return (kind == flamingomedia::media_cloud_exit_shutdown) ? flamingomedia::media_cloud_exit_shutdown_keep
                                                                 : flamingomedia::media_cloud_exit_keep;
    }
    ::SetForegroundWindow(m_hWnd);
    if (owner != NULL && ::IsWindow(owner))
        ::EnableWindow(owner, FALSE);
    MSG msg;
    while (!m_decided && ::GetMessage(&msg, NULL, 0, 0) > 0)
    {
        ::TranslateMessage(&msg);
        ::DispatchMessage(&msg);
    }
    //先恢复 owner 再销毁本窗口：顺序反了的话，本窗口销毁时焦点会落到别的进程的窗口上
    if (owner != NULL && ::IsWindow(owner))
        ::EnableWindow(owner, TRUE);
    Close();
    LOG_INFO("cloud exit dialog: kind %d, choice %d.", kind, m_result);
    return m_result;
}

void CCloudExitDialog::Close()
{
    m_check = NULL;
    if (IsWindow())
        DestroyWindow();
}

bool CCloudExitDialog::IsDecided() const
{
    return m_decided;
}

int CCloudExitDialog::Result() const
{
    return m_result;
}

void CCloudExitDialog::decide(int button)
{
    if (m_decided)
        return;
    const bool checked = (m_check != NULL && m_check->IsChecked());
    m_result = CloudExitChoiceFor(m_kind, button, checked);
    m_decided = true;
}

LRESULT CCloudExitDialog::OnCloseMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
{
    (void)uMsg;
    (void)wParam;
    (void)lParam;
    (void)bHandled;
    //只记下结果，窗口由 RunModal 在模态循环结束后销毁
    decide(kCloudExitClosed);
    return 0;
}

LRESULT CCloudExitDialog::OnDuiNotify(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
{
    (void)uMsg;
    (void)wParam;
    const balloonwjui::DuiNotify* n = reinterpret_cast<const balloonwjui::DuiNotify*>(lParam);
    if (n == NULL || n->code != static_cast<UINT>(balloonwjui::DUIN_CLICK))
    {
        bHandled = FALSE;
        return 0;
    }
    for (int i = 0; i < kCloudExitButtonCount; ++i)
    {
        //三个按钮之一：记下选择，结束模态循环
        if (n->ctrlId == buttonIdOf(i))
        {
            decide(i);
            return 0;
        }
    }
    bHandled = FALSE;
    return 0;
}

} // namespace screenrecord
