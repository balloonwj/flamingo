/**
 *  在共享内选目标文件夹的小窗口 CShareFolderPickDialog 的实现，见 ShareFolderPickDialog.h。
 *  balloonwj@qq.com   2026-10-03
 */
#include "stdafx.h"
#include "ShareFolderPickDialog.h"

#include <memory>

#include "../Engine/ShareBrowseModel.h"
#include "../Engine/ShareWinPath.h"
#include "../../Source/Common/Log/IULog.h"
#include "../../Source/Common/Graphics/ShellTypeIcon.h"
#include "../../../third-party/balloonui/balloonui/Controls/Layout/DuiLayout.h"
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiLabel.h"
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiButton.h"
#include "../../../third-party/balloonui/balloonui/Controls/List/DuiListBox.h"
#include "../../Source/Common/I18n/I18n.h"   // TR / TR_NOOP：界面文字的多语言查找

using balloonwjui::DuiControl;
using balloonwjui::DuiLayout;
using balloonwjui::DuiVBox;
using balloonwjui::DuiHBox;
using balloonwjui::DuiLabel;
using balloonwjui::DuiButton;
using balloonwjui::DuiVirtualList;

namespace fileshare {

namespace {

// ---- 尺寸（像素）----
const int kContentWidth    = 420;   //整窗宽度
const int kContentHeight   = 380;   //标题栏以下内容区的高度
const int kTitleBarHeight  = 32;    //标题栏高，与客户端其余窗口一致
const int kPadding         = 12;    //四周内边距
const int kBarHeight       = 32;    //「上一级」与路径那一行的高
const int kUpButtonWidth   = 72;    //「上一级」按钮宽
const int kRowHeight       = 28;    //列表行高
const int kStatusHeight    = 22;    //状态文字高
const int kButtonHeight    = 30;    //底部按钮高
const int kConfirmWidth    = 104;   //「移动到这里」按钮宽
const int kCancelWidth     = 80;    //「取消」按钮宽
const int kGap             = 8;     //各行、各按钮之间的间距
const int kCellPadding     = 8;     //单元格左右内边距
const int kIconSize        = 16;    //目录图标边长（系统小图标的尺寸）
const int kIconTextGap     = 8;     //图标与名称之间的间距

// ---- 控件 id（只在本窗口内唯一）----
const UINT kIdUp      = 301;   //「上一级」
const UINT kIdConfirm = 302;   //「移动到这里」
const UINT kIdCancel  = 303;   //「取消」

// ---- 配色 ----
const COLORREF kBgColor       = RGB(0xFF, 0xFF, 0xFF);   //白底
const COLORREF kTextColor     = RGB(0x1F, 0x1F, 0x1F);   //名称与路径
const COLORREF kWeakTextColor = RGB(0x9A, 0x98, 0x93);   //状态文字
const COLORREF kSelectedBg    = RGB(0xEA, 0xF1, 0xFE);   //选中行的底色，与「文件共享」窗口一致
const COLORREF kHoverBg       = RGB(0xF5, 0xF7, 0xFA);   //鼠标所在行的底色

// ---- 文字 ----
const wchar_t* const kWindowTitle   = TR_NOOP(L"移动到");
const wchar_t* const kUpText        = TR_NOOP(L"上一级");
const wchar_t* const kConfirmText   = TR_NOOP(L"移动到这里");
const wchar_t* const kCancelText    = TR_NOOP(L"取消");
const wchar_t* const kLoadingText   = TR_NOOP(L"正在加载…");
const wchar_t* const kNoFolderText  = TR_NOOP(L"这里没有子文件夹，可以直接移动到这里");
const wchar_t* const kHintText      = TR_NOOP(L"双击文件夹进入，选好位置后点「移动到这里」");
const wchar_t* const kPathSeparator = L" / ";

std::wstring Wide(const std::string& utf8)
{
    std::wstring w;
    ShareUtf8ToWide(utf8, w);
    return w;
}

} // namespace

CShareFolderPickDialog::CShareFolderPickDialog()
    : m_owner(NULL)
    , m_source(NULL)
    , m_listener(NULL)
    , m_loading(false)
    , m_upButton(NULL)
    , m_pathLabel(NULL)
    , m_list(NULL)
    , m_status(NULL)
{
}

CShareFolderPickDialog::~CShareFolderPickDialog()
{
    //析构时不回调：监听者可能已先于本对象销毁
    m_listener = NULL;
    Close();
}

bool CShareFolderPickDialog::Open(HWND owner, IShareFolderSource* source, IShareFolderPickListener* listener,
                                  const std::string& startPath, const std::wstring& rootName)
{
    if (source == NULL || listener == NULL)
        return false;
    Close();

    HWND parent = (owner != NULL && ::IsWindow(owner)) ? owner : NULL;
    if (Create(parent, CWindow::rcDefault, TR(kWindowTitle), WS_OVERLAPPEDWINDOW, 0) == NULL)
    {
        LOG_ERROR("share folder pick dialog: Create failed, error: %lu.", ::GetLastError());
        return false;
    }
    m_owner = parent;
    m_source = source;
    m_listener = listener;
    m_rootName = rootName;
    m_folders.clear();
    SetTitle(TR(kWindowTitle));
    SetTitleBarHeight(kTitleBarHeight);
    SetButtons(false, false, true);
    //程序图标：标题栏左侧的位图与任务栏 / Alt-Tab 图标（图标资源在 exe 里，见 TitleBarIcon.h）
    SetIcon(m_programIcons.TitleBitmap(balloonmain::kProgramTitleIconSize, balloonmain::kOpaqueTitleBarColor));
    m_programIcons.ApplyTaskIcons(m_hWnd);
    SetResizable(false);
    BuildContent();
    //DuiFrameWindow 的 ResizeClient 设的是整窗尺寸，标题栏也在其内（UI 约定第 7 条）
    ResizeClient(kContentWidth, kContentHeight + kTitleBarHeight);
    CenterWindow(parent);
    if (m_owner != NULL)
        ::EnableWindow(m_owner, FALSE);
    ShowWindow(SW_SHOW);
    ::SetForegroundWindow(m_hWnd);
    if (m_list != NULL)
        m_list->SetFocus();
    LOG_INFO("share folder pick dialog opened.");
    Request(startPath);
    return true;
}

void CShareFolderPickDialog::Close()
{
    const bool wasOpen = IsWindow() != FALSE;
    RestoreOwner();
    if (wasOpen)
        DestroyWindow();
    m_upButton = NULL;
    m_pathLabel = NULL;
    m_list = NULL;
    m_status = NULL;
    m_loading = false;
    IShareFolderPickListener* listener = m_listener;
    m_listener = NULL;
    m_source = NULL;
    if (wasOpen && listener != NULL)
        listener->OnShareFolderPickClosed();
}

bool CShareFolderPickDialog::IsOpen() const
{
    return IsWindow() != FALSE;
}

void CShareFolderPickDialog::OnListed(const std::string& path, int code, const std::vector<ShareEntry>& entries)
{
    if (!IsOpen() || !m_loading || path != m_requestedPath)
        return;
    m_loading = false;
    if (code != 0)
    {
        SetStatus(ShareBrowseModel::ErrorText(code));
        return;
    }
    m_currentPath = path;
    m_folders.clear();
    for (size_t i = 0; i < entries.size(); ++i)
    {
        if (entries[i].m_isDir)
            m_folders.push_back(entries[i]);
    }
    ShareBrowseModel::SortEntries(m_folders, share_sort_name, true);
    if (m_list != NULL)
    {
        m_list->SetRowCount(static_cast<int>(m_folders.size()));
        //设了行数之后按当前矩形重新排版，滚动条才会按新的行数摆放（balloonui 2026-10-03 已在设行数时自行处理，这里保留无害）
        m_list->ForceLayout(m_list->GetRect());
        m_list->SetCurSel(-1, false);
        m_list->SetScrollPos(0);
        m_list->Invalidate();
    }
    UpdatePathBar();
    SetStatus(m_folders.empty() ? TR(kNoFolderText) : TR(kHintText));
}

const std::string& CShareFolderPickDialog::GetCurrentPath() const
{
    return m_currentPath;
}

size_t CShareFolderPickDialog::GetFolderCount() const
{
    return m_folders.size();
}

void CShareFolderPickDialog::EnterFolder(size_t index)
{
    if (index >= m_folders.size() || m_loading)
        return;
    Request(ShareBrowseModel::JoinPath(m_currentPath, m_folders[index].m_name));
}

void CShareFolderPickDialog::GoUp()
{
    if (m_currentPath.empty() || m_loading)
        return;
    Request(ShareBrowseModel::ParentPath(m_currentPath));
}

bool CShareFolderPickDialog::ConfirmHere()
{
    if (!IsOpen() || m_loading || m_listener == NULL)
        return false;
    IShareFolderPickListener* listener = m_listener;
    const std::string dir = m_currentPath;
    RestoreOwner();
    ShowWindow(SW_HIDE);
    listener->OnShareFolderPicked(dir);
    //销毁放到消息处理完之后：本函数可能正处在本窗口的通知处理里
    if (IsWindow())
        PostMessage(WM_CLOSE);
    return true;
}

LRESULT CShareFolderPickDialog::OnCloseMsg(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& bHandled)
{
    bHandled = TRUE;
    Close();
    return 0;
}

LRESULT CShareFolderPickDialog::OnDuiNotify(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam, BOOL& bHandled)
{
    const balloonwjui::DuiNotify* pNotify = reinterpret_cast<const balloonwjui::DuiNotify*>(lParam);
    if (pNotify == NULL || pNotify->code != balloonwjui::DUIN_CLICK)
    {
        bHandled = FALSE;
        return 0;
    }
    switch (pNotify->ctrlId)
    {
    //上一级
    case kIdUp:
        GoUp();
        return 0;

    //移动到当前目录
    case kIdConfirm:
        ConfirmHere();
        return 0;

    //取消：通知处理里不能销毁窗口，经消息投递
    case kIdCancel:
        PostMessage(WM_CLOSE);
        return 0;

    //其余控件的点击：交给基类
    default:
        bHandled = FALSE;
        return 0;
    }
}

void CShareFolderPickDialog::BuildContent()
{
    typedef DuiLayout::Hint Hint;

    std::unique_ptr<DuiVBox> root(new DuiVBox());
    root->SetBgColor(kBgColor);
    root->SetPadding(kPadding, kPadding, kPadding, kPadding);
    root->SetGap(kGap);

    //上一级 | 路径
    std::unique_ptr<DuiHBox> bar(new DuiHBox());
    bar->SetGap(kGap);
    std::unique_ptr<DuiButton> up(new DuiButton());
    up->SetText(TR(kUpText));
    up->SetVariant(DuiButton::Variant::Ghost);
    up->SetCtrlId(kIdUp);
    m_upButton = up.get();
    bar->AddChild(std::unique_ptr<DuiControl>(up.release()), Hint().Fixed(kUpButtonWidth));
    std::unique_ptr<DuiLabel> path(new DuiLabel());
    path->SetTextColor(kTextColor);
    path->SetTextAlign(DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_PATH_ELLIPSIS | DT_NOPREFIX);
    m_pathLabel = path.get();
    bar->AddChild(std::unique_ptr<DuiControl>(path.release()), Hint().Weight(1));
    root->AddChild(std::unique_ptr<DuiControl>(bar.release()), Hint().Fixed(kBarHeight));

    //子文件夹列表
    std::unique_ptr<DuiVirtualList> list(new DuiVirtualList());
    list->SetRowHeight(kRowHeight);
    list->SetPaintRowCallback(&CShareFolderPickDialog::PaintRowThunk, this);
    list->SetRowClickCallback(&CShareFolderPickDialog::RowClickThunk, this);
    m_list = list.get();
    root->AddChild(std::unique_ptr<DuiControl>(list.release()), Hint().Weight(1));

    std::unique_ptr<DuiLabel> status(new DuiLabel());
    status->SetTextColor(kWeakTextColor);
    status->SetTextAlign(DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
    m_status = status.get();
    root->AddChild(std::unique_ptr<DuiControl>(status.release()), Hint().Fixed(kStatusHeight));

    //按钮行：右对齐的「移动到这里」「取消」
    std::unique_ptr<DuiHBox> buttons(new DuiHBox());
    buttons->SetGap(kGap);
    buttons->AddChild(std::unique_ptr<DuiControl>(new DuiLabel()), Hint().Weight(1));
    std::unique_ptr<DuiButton> confirm(new DuiButton());
    confirm->SetText(TR(kConfirmText));
    confirm->SetCtrlId(kIdConfirm);
    buttons->AddChild(std::unique_ptr<DuiControl>(confirm.release()), Hint().Fixed(kConfirmWidth));
    std::unique_ptr<DuiButton> cancel(new DuiButton());
    cancel->SetText(TR(kCancelText));
    cancel->SetVariant(DuiButton::Variant::Ghost);
    cancel->SetCtrlId(kIdCancel);
    buttons->AddChild(std::unique_ptr<DuiControl>(cancel.release()), Hint().Fixed(kCancelWidth));
    root->AddChild(std::unique_ptr<DuiControl>(buttons.release()), Hint().Fixed(kButtonHeight));

    SetClientContent(std::unique_ptr<DuiControl>(root.release()));
}

void CShareFolderPickDialog::Request(const std::string& path)
{
    if (m_source == NULL)
        return;
    m_requestedPath = path;
    m_loading = true;
    SetStatus(TR(kLoadingText));
    m_source->RequestFolderList(path);
}

void CShareFolderPickDialog::UpdatePathBar()
{
    if (m_pathLabel == NULL)
        return;
    std::wstring text = m_rootName;
    const std::vector<std::string> parts = ShareBrowseModel::SplitPath(m_currentPath);
    for (size_t i = 0; i < parts.size(); ++i)
        text += kPathSeparator + Wide(parts[i]);
    m_pathLabel->SetText(text.c_str());
    if (m_upButton != NULL)
        m_upButton->SetEnabled(!m_currentPath.empty());
}

void CShareFolderPickDialog::SetStatus(const std::wstring& text)
{
    if (m_status != NULL)
    {
        m_status->SetText(text.c_str());
        m_status->Invalidate();
    }
}

void CShareFolderPickDialog::RestoreOwner()
{
    if (m_owner == NULL)
        return;
    const HWND owner = m_owner;
    m_owner = NULL;
    if (::IsWindow(owner))
    {
        ::EnableWindow(owner, TRUE);
        ::SetForegroundWindow(owner);
    }
}

void CShareFolderPickDialog::PaintRowThunk(void* user, HDC hdc, int rowIndex, const RECT& rowRect, bool selected,
                                           bool hover)
{
    CShareFolderPickDialog* self = static_cast<CShareFolderPickDialog*>(user);
    if (self != NULL)
        self->PaintRow(hdc, rowIndex, rowRect, selected, hover);
}

void CShareFolderPickDialog::RowClickThunk(void* user, int rowIndex, bool isDoubleClick)
{
    CShareFolderPickDialog* self = static_cast<CShareFolderPickDialog*>(user);
    if (self != NULL && isDoubleClick && rowIndex >= 0)
        self->EnterFolder(static_cast<size_t>(rowIndex));
}

void CShareFolderPickDialog::PaintRow(HDC hdc, int rowIndex, const RECT& rowRect, bool selected, bool hover)
{
    if (rowIndex < 0 || static_cast<size_t>(rowIndex) >= m_folders.size() || m_list == NULL)
        return;
    //选中与悬停的底色由画行回调自己画（DuiVirtualList 只告诉状态，不画底色）
    if (selected || hover)
    {
        HBRUSH bg = ::CreateSolidBrush(selected ? kSelectedBg : kHoverBg);
        ::FillRect(hdc, &rowRect, bg);
        ::DeleteObject(bg);
    }
    const int iconTop = rowRect.top + (rowRect.bottom - rowRect.top - kIconSize) / 2;
    RECT icon = { rowRect.left + kCellPadding, iconTop, rowRect.left + kCellPadding + kIconSize, iconTop + kIconSize };
    //本机的系统文件夹图标，与浏览窗口一致
    shellicon::DrawItemIcon(hdc, shellicon::kItemFolder, std::wstring(), icon);

    HFONT oldFont = static_cast<HFONT>(::SelectObject(hdc, m_list->GetDefaultFont()));
    const int oldMode = ::SetBkMode(hdc, TRANSPARENT);
    ::SetTextColor(hdc, kTextColor);
    RECT nameRect = { icon.right + kIconTextGap, rowRect.top, rowRect.right - kCellPadding, rowRect.bottom };
    const std::wstring name = Wide(m_folders[static_cast<size_t>(rowIndex)].m_name);
    ::DrawTextW(hdc, name.c_str(), -1, &nameRect, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
    ::SetBkMode(hdc, oldMode);
    ::SelectObject(hdc, oldFont);
}

} // namespace fileshare
