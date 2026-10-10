/**
 *  本会话共享列表浮层 CShareConversationPopup 的实现，见 ShareConversationPopup.h。
 *  balloonwj@qq.com   2026-10-03
 */
#include "stdafx.h"
#include "ShareConversationPopup.h"

#include <memory>

#include "../Engine/ShareConversationModel.h"
#include "../Engine/ShareCreateModel.h"
#include "../Engine/ShareWinPath.h"
#include "../../Source/net/Protocol.h"
#include "../../Source/Common/Log/IULog.h"
#include "../../Source/Common/Graphics/ShellTypeIcon.h"
#include "../../../third-party/balloonui/balloonui/Controls/Layout/DuiLayout.h"
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiLabel.h"
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiButton.h"
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiAvatar.h"
#include "../../../third-party/balloonui/balloonui/Controls/Window/DuiScrollBar.h"
#include "../../../third-party/balloonui/balloonui/Controls/List/DuiMenuPlacement.h"
#include "../../Source/Common/I18n/I18n.h"   // TR / TR_NOOP：界面文字的多语言查找

using balloonwjui::DuiControl;
using balloonwjui::DuiLayout;
using balloonwjui::DuiVBox;
using balloonwjui::DuiHBox;
using balloonwjui::DuiLabel;
using balloonwjui::DuiButton;
using balloonwjui::DuiScrollView;
using balloonwjui::DuiAvatar;

namespace fileshare {

namespace {

//浮层的窗口类名（按 DLL 的模块实例注册）
const wchar_t* const kPopupClass = L"FlamingoFileShareConversationPopup";

// ---- 尺寸（像素）----
const int kPopupWidth      = 440;   //浮层宽
const int kPopupHeight     = 380;   //浮层高
const int kPadding         = 12;    //四周内边距
const int kTitleHeight     = 28;    //标题行高
const int kRowHeight       = 52;    //每个共享一行的高度（两行文字）
const int kNameHeight      = 24;    //行内第一行（名称）的高度
const int kSubHeight       = 20;    //行内第二行（谁的共享与状态）的高度
const int kActionWidth     = 72;    //行内操作按钮宽
const int kRowGap          = 4;     //行内控件之间的间距
const int kStatusHeight    = 24;    //提示行高
const int kButtonRowHeight = 34;    //底部按钮行高
const int kCreateWidth     = 104;   //「发起共享」按钮宽
const int kManageAllWidth  = 112;   //「管理全部共享」链接式按钮宽
const int kIconSize        = 32;    //行首类型图标的边长
const int kIconGap         = 10;    //类型图标与文字之间的间距（行内控件间距之外再加的部分）
const int kIconCornerRadius = 1;    //类型图标所在头像控件的圆角半径（见 RebuildRows 里的说明）

// ---- 控件 id（只在本窗口内唯一）----
const UINT kIdCreate   = 301;   //「发起共享」
const UINT kIdManageAll = 302;  //「管理全部共享」：打开「文件共享」窗口并记住本会话（2026-10-03 起）
const UINT kIdRowBase  = 1000;  //行内按钮：kIdRowBase + 行号 × kIdsPerRow + 操作序号（0 打开、1 调整成员、2 取消共享）
const UINT kIdsPerRow  = 4;     //每行占用的控件 id 个数

//行内按钮的操作序号与 share_row_action 的对应
const int kRowActionOrder[] = { share_row_action_open, share_row_action_edit, share_row_action_cancel };
const size_t kRowActionCount = sizeof(kRowActionOrder) / sizeof(kRowActionOrder[0]);

// ---- 配色 ----
const COLORREF kBgColor       = RGB(0xFF, 0xFF, 0xFF);   //白底
const COLORREF kBorderColor   = RGB(0xD9, 0xD9, 0xD9);   //1px 浅灰描边
const COLORREF kTextColor     = RGB(0x1F, 0x1F, 0x1F);   //名称
const COLORREF kWeakTextColor = RGB(0x9A, 0x98, 0x93);   //第二行与提示
const COLORREF kErrorColor    = RGB(0xD9, 0x3B, 0x3B);   //错误提示
const COLORREF kInvalidColor  = RGB(0xB0, 0xB0, 0xB0);   //已失效的名称

// ---- 文字 ----
const wchar_t* const kTitleText      = TR_NOOP(L"本会话的共享");
const wchar_t* const kLoadingText    = TR_NOOP(L"正在加载…");
const wchar_t* const kEmptyText      = TR_NOOP(L"本会话还没有共享");
const wchar_t* const kLoadFailText   = TR_NOOP(L"加载失败，请稍后再试");
const wchar_t* const kCreateText     = TR_NOOP(L"发起共享");
const wchar_t* const kManageAllText  = TR_NOOP(L"管理全部共享");
const wchar_t* const kActionTexts[]  = { TR_NOOP(L"打开"), TR_NOOP(L"调整成员"), TR_NOOP(L"取消共享") };
const wchar_t* const kMineText       = TR_NOOP(L"我的共享");
const wchar_t* const kSeparator      = L" · ";
const wchar_t* const kCancelingText  = TR_NOOP(L"正在取消…");
const wchar_t* const kNoResponseText = TR_NOOP(L"服务器没有响应，请检查网络后重试");
const wchar_t* const kConfirmTitle   = TR_NOOP(L"取消共享");
const wchar_t* const kConfirmFormat  = TR_NOOP(L"确定取消共享「%s」吗？\n\n取消后所有成员都无法再访问，正在进行的浏览与传输会立即结束。");

//确认框文字缓冲区的字符数
const size_t kConfirmBufChars = 512;

//UTF-8 转成界面用的宽字符串
std::wstring Wide(const std::string& utf8)
{
    std::wstring w;
    ShareUtf8ToWide(utf8, w);
    return w;
}

//取本 DLL 的模块实例句柄
HINSTANCE DllInstance()
{
    return reinterpret_cast<HINSTANCE>(&__ImageBase);
}

} // namespace

CShareConversationPopup::CShareConversationPopup()
    : m_service(NULL)
    , m_host(NULL)
    , m_ui(NULL)
    , m_owner(NULL)
    , m_ctype(0)
    , m_cid(0)
    , m_myRole(kShareRoleUnknown)
    , m_mineSeq(0)
    , m_sharedSeq(0)
    , m_listError(0)
    , m_cancelId(0)
    , m_modal(false)
    , m_closePosted(false)
    , m_title(NULL)
    , m_scroll(NULL)
    , m_status(NULL)
{
}

CShareConversationPopup::~CShareConversationPopup()
{
    Close();
}

bool CShareConversationPopup::Open(HWND owner, CShareService* service, IFileShareHost* host, IShareUiHost* ui, int ctype,
                                   int64_t cid, const RECT* anchor)
{
    if (service == NULL || host == NULL || ui == NULL || cid <= 0)
        return false;
    Close();

    m_service = service;
    m_host = host;
    m_ui = ui;
    m_owner = owner;
    m_ctype = ctype;
    m_cid = cid;
    m_myRole = kShareRoleUnknown;
    m_mine.clear();
    m_sharedMe.clear();
    m_rows.clear();
    m_listError = 0;
    m_cancelId = 0;
    m_modal = false;
    m_closePosted = false;
    if (!CreatePopupWindow())
        return false;
    BuildContent();
    PlaceNear(anchor);
    ShowWindow(SW_SHOW);
    ::SetForegroundWindow(m_hWnd);
    LOG_INFO("conversation shares popup opened, ctype: %d, cid: %lld.", ctype, static_cast<long long>(cid));

    RequestLists();
    //群聊：要会话成员只为取本人的群角色（群主、管理员可以取消别人的共享）
    if (ctype == conversation_group)
        m_host->RequestConversationMembers(ctype, cid);
    return true;
}

void CShareConversationPopup::Close()
{
    if (m_service != NULL)
    {
        m_service->DetachListObserver(this);
        m_service->DetachManageObserver(this);
    }
    m_mineSeq = 0;
    m_sharedSeq = 0;
    m_cancelId = 0;
    if (IsWindow())
        DestroyWindow();
    m_title = NULL;
    m_scroll = NULL;
    m_status = NULL;
    m_rowLines.clear();
    //窗口与控件树已经销毁，引用位图的头像控件不在了
    ReleaseIconBitmaps(m_iconBitmaps);
    m_closePosted = false;
}

void CShareConversationPopup::ReleaseIconBitmaps(std::vector<HBITMAP>& bitmaps)
{
    for (size_t i = 0; i < bitmaps.size(); ++i)
    {
        if (bitmaps[i] != NULL)
            ::DeleteObject(bitmaps[i]);
    }
    bitmaps.clear();
}

bool CShareConversationPopup::CreatePopupWindow()
{
    HINSTANCE instance = DllInstance();
    WNDCLASSEXW existing = { 0 };
    existing.cbSize = sizeof(existing);
    if (!::GetClassInfoExW(instance, kPopupClass, &existing))
    {
        WNDCLASSEXW wc = { 0 };
        wc.cbSize        = sizeof(wc);
        //CS_DBLCLKS：要收到 WM_LBUTTONDBLCLK（双击一行打开）
        wc.style         = CS_DROPSHADOW | CS_DBLCLKS;
        wc.lpfnWndProc   = ::DefWindowProcW;
        wc.hInstance     = instance;
        wc.hCursor       = ::LoadCursor(NULL, IDC_ARROW);
        wc.lpszClassName = kPopupClass;
        if (::RegisterClassExW(&wc) == 0)
            return false;
    }

    //不设所有者：带所有者的 WS_POPUP 经 ::GetParent 取到的是所有者，控件通知会被投给聊天窗。WS_EX_TOOLWINDOW 不进任务栏
    HWND hWnd = ::CreateWindowExW(WS_EX_TOOLWINDOW, kPopupClass, TR(kTitleText), WS_POPUP, 0, 0, kPopupWidth, kPopupHeight,
                                  NULL, NULL, instance, NULL);
    if (hWnd == NULL)
    {
        LOG_ERROR("conversation shares popup: create window failed, error: %lu.", ::GetLastError());
        return false;
    }
    if (!SubclassWindow(hWnd))
    {
        ::DestroyWindow(hWnd);
        LOG_ERROR("conversation shares popup: subclass failed.");
        return false;
    }
    return true;
}

void CShareConversationPopup::BuildContent()
{
    typedef DuiLayout::Hint Hint;

    std::unique_ptr<DuiVBox> root(new DuiVBox());
    root->SetBgColor(kBgColor);
    root->SetBorderColor(kBorderColor);
    root->SetPadding(kPadding, kPadding, kPadding, kPadding);

    std::unique_ptr<DuiLabel> title(new DuiLabel());
    title->SetText(TR(kTitleText));
    title->SetTextColor(kTextColor);
    title->SetTextAlign(DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    m_title = title.get();
    root->AddChild(std::unique_ptr<DuiControl>(title.release()), Hint().Fixed(kTitleHeight));

    std::unique_ptr<DuiScrollView> scroll(new DuiScrollView());
    //滚动条用 balloonui 统一的悬浮式细滑块与默认命中带宽（2026-10-04 起不再单独设宽）
    m_scroll = scroll.get();
    root->AddChild(std::unique_ptr<DuiControl>(scroll.release()), Hint().Weight(1));

    std::unique_ptr<DuiLabel> status(new DuiLabel());
    status->SetTextColor(kWeakTextColor);
    status->SetTextAlign(DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    m_status = status.get();
    root->AddChild(std::unique_ptr<DuiControl>(status.release()), Hint().Fixed(kStatusHeight));

    std::unique_ptr<DuiHBox> buttons(new DuiHBox());
    //左边「管理全部共享」：链接式按钮（纯文字、无底无边），打开「文件共享」窗口并记住本会话
    std::unique_ptr<DuiButton> manageAll(new DuiButton());
    manageAll->SetText(TR(kManageAllText));
    manageAll->SetVariant(DuiButton::Variant::Text);
    manageAll->SetCtrlId(kIdManageAll);
    manageAll->SetTabStop(false);
    buttons->AddChild(std::unique_ptr<DuiControl>(manageAll.release()), Hint().Fixed(kManageAllWidth));
    buttons->AddChild(std::unique_ptr<DuiControl>(new DuiLabel()), Hint().Weight(1));
    std::unique_ptr<DuiButton> create(new DuiButton());
    create->SetText(TR(kCreateText));
    create->SetCtrlId(kIdCreate);
    //浮层只靠鼠标操作，按钮不参与 Tab 切换：否则初始焦点落在按钮上，按钮带着焦点虚线框
    create->SetTabStop(false);
    buttons->AddChild(std::unique_ptr<DuiControl>(create.release()), Hint().Fixed(kCreateWidth));
    root->AddChild(std::unique_ptr<DuiControl>(buttons.release()), Hint().Fixed(kButtonRowHeight));

    SetRoot(std::unique_ptr<DuiControl>(root.release()));
    SetStatus(TR(kLoadingText), false);
}

void CShareConversationPopup::PlaceNear(const RECT* anchor)
{
    SIZE size = { kPopupWidth, kPopupHeight };
    POINT below;
    RECT anchorRect = { 0, 0, 0, 0 };
    if (anchor != NULL)
    {
        anchorRect = *anchor;
        below.x = anchorRect.left;
        below.y = anchorRect.bottom;
    }
    else
    {
        ::GetCursorPos(&below);
        anchorRect.left = below.x;
        anchorRect.right = below.x;
        anchorRect.top = below.y;
        anchorRect.bottom = below.y;
    }
    //先放在按钮下方；下方放不下时 ClampMenuOriginToWorkArea 会向上翻，此时改为贴着按钮上沿，免得盖住按钮
    POINT origin = balloonwjui::ClampMenuOriginToWorkArea(below, size);
    if (origin.y < below.y)
    {
        POINT above = { origin.x, anchorRect.top - size.cy };
        origin = balloonwjui::ClampMenuOriginToRect(above, size, balloonwjui::GetMenuPlacementArea(below));
    }
    SetWindowPos(HWND_TOP, origin.x, origin.y, size.cx, size.cy, SWP_NOACTIVATE);
}

void CShareConversationPopup::RequestLists()
{
    m_listError = 0;
    m_mineSeq = m_service->RequestList(kShareListScopeMine, m_ctype, m_cid, this);
    m_sharedSeq = m_service->RequestList(kShareListScopeSharedMe, m_ctype, m_cid, this);
    if (m_mineSeq == 0 || m_sharedSeq == 0)
        SetStatus(TR(kLoadFailText), true);
    else
        SetStatus(TR(kLoadingText), false);
}

void CShareConversationPopup::OnShareListResult(uint32_t requestId, int scope, int code,
                                                const std::vector<ShareListEntry>& shares)
{
    if (requestId != m_mineSeq && requestId != m_sharedSeq)
        return;
    if (requestId == m_mineSeq)
    {
        m_mineSeq = 0;
        m_mine = shares;
    }
    else
    {
        m_sharedSeq = 0;
        m_sharedMe = shares;
    }
    (void)scope;
    if (code != error_code_ok)
        m_listError = code;
    //两份都到了才显示
    if (m_mineSeq != 0 || m_sharedSeq != 0)
        return;
    m_rows = ShareConversationModel::Merge(m_mine, m_sharedMe);
    RebuildRows();
    if (m_listError != 0)
        SetStatus(TR(kLoadFailText), true);
    else if (m_rows.empty())
        SetStatus(TR(kEmptyText), false);
    else
        SetStatus(std::wstring(), false);
    LOG_INFO("conversation shares popup listed, ctype: %d, cid: %lld, rows: %u, error: %d.", m_ctype,
             static_cast<long long>(m_cid), static_cast<unsigned>(m_rows.size()), m_listError);
}

void CShareConversationPopup::OnConversationMembers(int ctype, int64_t cid, int code, const FileShareMemberInfo* members,
                                                    uint32_t count)
{
    if (!IsWindow() || ctype != m_ctype || cid != m_cid || code != 0 || m_service == NULL)
        return;
    const int64_t self = m_service->GetUserId();
    for (uint32_t i = 0; i < count && members != NULL; ++i)
    {
        if (members[i].m_userId == self)
        {
            m_myRole = members[i].m_role;
            break;
        }
    }
    //角色到达后重建：群主与管理员的行上会多出「取消共享」
    if (m_mineSeq == 0 && m_sharedSeq == 0)
        RebuildRows();
}

void CShareConversationPopup::RebuildRows()
{
    if (m_scroll == NULL)
        return;
    typedef DuiLayout::Hint Hint;
    const int64_t self = (m_service != NULL) ? m_service->GetUserId() : 0;

    std::unique_ptr<DuiVBox> list(new DuiVBox());
    std::vector<HBITMAP> bitmaps;
    std::vector<DuiControl*> lines;
    for (size_t r = 0; r < m_rows.size(); ++r)
    {
        const ShareListEntry& e = m_rows[r];
        std::unique_ptr<DuiHBox> line(new DuiHBox());
        line->SetGap(kRowGap);
        lines.push_back(line.get());

        //行首的系统类型图标：单个文件按显示路径（没有时按名称）的扩展名取，磁盘与文件夹与名字无关
        const std::wstring iconName = Wide(e.m_displayPath.empty() ? e.m_name : e.m_displayPath);
        HBITMAP icon = shellicon::CreateItemIconBitmap(e.m_kind, iconName, kIconSize);
        bitmaps.push_back(icon);
        std::unique_ptr<DuiAvatar> iconCtrl(new DuiAvatar());
        //头像控件按透明度绘制位图，用圆角矩形当作方形：半径取 1 而不是 0。半径为 0 时图标完全画不出来（据实证：SCP3
        //截图里图标处是空白，改为 1 后正常），据推测是裁剪路径里的四段圆弧宽高为 0、裁剪区为空，见 bugs.md BUG-70。
        //系统图标四角本来透明，1 像素的圆角看不出来
        iconCtrl->SetShape(DuiAvatar::ShapeRoundRect);
        iconCtrl->SetCornerRadius(kIconCornerRadius);
        iconCtrl->SetBitmap(icon);
        line->AddChild(std::unique_ptr<DuiControl>(iconCtrl.release()),
                       Hint().Fixed(kIconSize, kIconSize).Margin(0, 0, kIconGap, 0).AlignC(DuiLayout::AlignCenter));

        //左侧两行：名称；「某某的共享 · 状态」
        std::unique_ptr<DuiVBox> texts(new DuiVBox());
        std::unique_ptr<DuiLabel> name(new DuiLabel());
        name->SetText(Wide(e.m_name).c_str());
        name->SetTextColor(e.m_state == share_state_invalid ? kInvalidColor : kTextColor);
        name->SetTextAlign(DT_LEFT | DT_BOTTOM | DT_SINGLELINE | DT_END_ELLIPSIS);
        texts->AddChild(std::unique_ptr<DuiControl>(name.release()), Hint().Fixed(kNameHeight));
        std::wstring sub;
        if (e.m_ownerId == self)
        {
            sub = TR(kMineText);
        }
        else
        {
            char buf[kFileShareTextBytes] = { 0 };
            std::wstring owner;
            if (m_host != NULL && m_host->GetUserDisplayName(e.m_ownerId, buf, kFileShareTextBytes))
                owner = Wide(buf);
            sub = TRF(L"{0} 的共享", owner);   //{0} 为分享者名
        }
        sub += kSeparator;
        sub += ShareConversationModel::StatusText(e.m_state, e.m_reason);
        std::unique_ptr<DuiLabel> subLabel(new DuiLabel());
        subLabel->SetText(sub.c_str());
        subLabel->SetTextColor(kWeakTextColor);
        subLabel->SetTextAlign(DT_LEFT | DT_TOP | DT_SINGLELINE | DT_END_ELLIPSIS);
        texts->AddChild(std::unique_ptr<DuiControl>(subLabel.release()), Hint().Fixed(kSubHeight));
        line->AddChild(std::unique_ptr<DuiControl>(texts.release()), Hint().Weight(1));

        //右侧按本人身份给出的操作
        const int actions = ShareConversationModel::RowActions(e, self, m_myRole);
        for (size_t a = 0; a < kRowActionCount; ++a)
        {
            if ((actions & kRowActionOrder[a]) == 0)
                continue;
            std::unique_ptr<DuiButton> button(new DuiButton());
            button->SetText(TR(kActionTexts[a]));
            button->SetVariant(DuiButton::Variant::Ghost);
            button->SetCtrlId(kIdRowBase + static_cast<UINT>(r) * kIdsPerRow + static_cast<UINT>(a));
            button->SetTabStop(false);
            line->AddChild(std::unique_ptr<DuiControl>(button.release()), Hint().Fixed(kActionWidth));
        }
        list->AddChild(std::unique_ptr<DuiControl>(line.release()), Hint().Fixed(kRowHeight));
    }
    m_scroll->SetContentHeight(kRowHeight * static_cast<int>(m_rows.size()));
    m_scroll->SetContent(std::unique_ptr<DuiControl>(list.release()));
    //旧的行已随旧内容销毁，它们引用的位图可以释放了
    ReleaseIconBitmaps(m_iconBitmaps);
    m_iconBitmaps.swap(bitmaps);
    m_rowLines.swap(lines);
    m_scroll->ForceLayout(m_scroll->GetRect());
    m_scroll->Invalidate();
}

bool CShareConversationPopup::OnRowDoubleClick(size_t row)
{
    if (row >= m_rows.size() || m_service == NULL)
        return false;
    if ((ShareConversationModel::RowActions(m_rows[row], m_service->GetUserId(), m_myRole) & share_row_action_open) == 0)
        return false;
    LOG_INFO("conversation shares popup: row %u double clicked, open share %lld.", static_cast<unsigned>(row),
             static_cast<long long>(m_rows[row].m_shareId));
    return DoRowAction(row, share_row_action_open, false);
}

int CShareConversationPopup::RowAtPoint(POINT pt) const
{
    if (m_scroll == NULL || !::PtInRect(&m_scroll->GetRect(), pt))
        return -1;
    for (size_t r = 0; r < m_rowLines.size(); ++r)
    {
        if (m_rowLines[r] != NULL && ::PtInRect(&m_rowLines[r]->GetRect(), pt))
            return static_cast<int>(r);
    }
    return -1;
}

RECT CShareConversationPopup::GetRowRect(size_t row) const
{
    RECT empty = { 0, 0, 0, 0 };
    if (row >= m_rowLines.size() || m_rowLines[row] == NULL)
        return empty;
    return m_rowLines[row]->GetRect();
}

bool CShareConversationPopup::RowHasIcon(size_t row) const
{
    return row < m_iconBitmaps.size() && m_iconBitmaps[row] != NULL;
}

LRESULT CShareConversationPopup::OnDoubleClickMsg(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam, BOOL& bHandled)
{
    //客户区坐标：低 16 位为 x、高 16 位为 y，按有符号数取（多显示器时可能为负）
    POINT pt = { static_cast<short>(LOWORD(lParam)), static_cast<short>(HIWORD(lParam)) };
    //双击落在行内按钮上：交给 DuiHost 照常处理（按钮自己决定双击的含义）
    DuiControl* root = GetRoot();
    DuiControl* hit = (root != NULL) ? root->HitTest(pt) : NULL;
    if (dynamic_cast<DuiButton*>(hit) != NULL)
    {
        bHandled = FALSE;
        return 0;
    }
    const int row = RowAtPoint(pt);
    if (row < 0)
    {
        bHandled = FALSE;
        return 0;
    }
    OnRowDoubleClick(static_cast<size_t>(row));
    return 0;
}

bool CShareConversationPopup::DoRowAction(size_t row, int action, bool confirmed)
{
    if (row >= m_rows.size() || m_service == NULL || m_ui == NULL)
        return false;
    const int64_t self = m_service->GetUserId();
    if ((ShareConversationModel::RowActions(m_rows[row], self, m_myRole) & action) == 0)
        return false;
    const ShareListEntry entry = m_rows[row];

    switch (action)
    {
    //打开：显示浏览窗口，浮层关闭
    case share_row_action_open:
        m_ui->UiOpenShare(m_owner, entry.m_shareId);
        PostClose();
        return true;

    //调整成员：打开调整成员窗口，浮层关闭
    case share_row_action_edit:
        m_ui->UiEditMembers(m_owner, entry);
        PostClose();
        return true;

    //取消共享：确认后发 2202，结果到达后重新拉取列表
    case share_row_action_cancel:
    {
        if (m_cancelId != 0)
            return false;
        if (!confirmed)
        {
            wchar_t text[kConfirmBufChars] = { 0 };
            _snwprintf_s(text, kConfirmBufChars, _TRUNCATE, TR(kConfirmFormat), Wide(entry.m_name).c_str());
            //确认框期间浮层失去激活，不能因此关闭
            m_modal = true;
            const int answer = ::MessageBoxW(m_hWnd, text, TR(kConfirmTitle), MB_OKCANCEL | MB_ICONQUESTION | MB_DEFBUTTON2);
            m_modal = false;
            if (answer != IDOK)
                return false;
        }
        m_cancelId = m_service->CancelShare(entry.m_shareId, this);
        if (m_cancelId == 0)
        {
            SetStatus(TR(kLoadFailText), true);
            return false;
        }
        SetStatus(TR(kCancelingText), false);
        return true;
    }

    //其余取值不是合法的操作
    default:
        return false;
    }
}

void CShareConversationPopup::OnShareManageResult(const ShareManageResult& result)
{
    if (result.m_requestId != m_cancelId || m_cancelId == 0)
        return;
    m_cancelId = 0;
    if (result.m_code != error_code_ok)
    {
        SetStatus(result.m_code == kShareManageNoResponse ? std::wstring(TR(kNoResponseText))
                                                          : ShareCreateModel::CreateErrorText(result.m_code), true);
        return;
    }
    LOG_INFO("conversation shares popup: share canceled, shareid: %lld.", static_cast<long long>(result.m_shareId));
    RequestLists();
}

void CShareConversationPopup::OnCreateClicked()
{
    if (m_ui == NULL)
        return;
    //聊天窗就在眼前，发起成功后不必再打开它
    m_ui->UiOpenCreateShare(m_owner, m_ctype, m_cid, false);
    PostClose();
}

bool CShareConversationPopup::OnManageAllClicked()
{
    if (m_ui == NULL)
        return false;
    const bool ok = m_ui->UiOpenShareCenter(m_owner, m_ctype, m_cid);
    LOG_INFO("conversation shares popup: manage all shares, ctype: %d, cid: %lld, ok: %d.", m_ctype,
             static_cast<long long>(m_cid), ok ? 1 : 0);
    PostClose();
    return ok;
}

size_t CShareConversationPopup::GetRowCount() const
{
    return m_rows.size();
}

const ShareListEntry& CShareConversationPopup::GetRow(size_t row) const
{
    return m_rows[row];
}

int CShareConversationPopup::GetRowActions(size_t row) const
{
    if (row >= m_rows.size() || m_service == NULL)
        return 0;
    return ShareConversationModel::RowActions(m_rows[row], m_service->GetUserId(), m_myRole);
}

std::wstring CShareConversationPopup::GetStatusText() const
{
    return (m_status != NULL) ? std::wstring(m_status->GetText().GetString()) : std::wstring();
}

int CShareConversationPopup::GetMyRole() const
{
    return m_myRole;
}

LRESULT CShareConversationPopup::OnDuiNotify(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam, BOOL& bHandled)
{
    const balloonwjui::DuiNotify* pNotify = reinterpret_cast<const balloonwjui::DuiNotify*>(lParam);
    if (pNotify == NULL || pNotify->code != balloonwjui::DUIN_CLICK)
    {
        bHandled = FALSE;
        return 0;
    }
    const UINT id = pNotify->ctrlId;
    //「发起共享」
    if (id == kIdCreate)
    {
        OnCreateClicked();
        return 0;
    }
    //「管理全部共享」
    if (id == kIdManageAll)
    {
        OnManageAllClicked();
        return 0;
    }
    //行内按钮（通知码与控件 id 一起判，UI 约定第 4 条）
    if (id >= kIdRowBase && id < kIdRowBase + kIdsPerRow * static_cast<UINT>(m_rows.size()))
    {
        const size_t row = (id - kIdRowBase) / kIdsPerRow;
        const size_t order = (id - kIdRowBase) % kIdsPerRow;
        if (order < kRowActionCount)
            DoRowAction(row, kRowActionOrder[order], false);
        return 0;
    }
    bHandled = FALSE;
    return 0;
}

LRESULT CShareConversationPopup::OnActivateMsg(UINT /*uMsg*/, WPARAM wParam, LPARAM /*lParam*/, BOOL& bHandled)
{
    bHandled = FALSE;
    //失去激活即关闭；确认框期间除外。在消息处理过程中不直接销毁窗口，投递 WM_CLOSE
    if (LOWORD(wParam) == WA_INACTIVE && !m_modal)
        PostClose();
    return 0;
}

LRESULT CShareConversationPopup::OnCloseMsg(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& bHandled)
{
    bHandled = TRUE;
    Close();
    return 0;
}

LRESULT CShareConversationPopup::OnKeyDownMsg(UINT /*uMsg*/, WPARAM wParam, LPARAM /*lParam*/, BOOL& bHandled)
{
    if (wParam == VK_ESCAPE)
    {
        PostClose();
        return 0;
    }
    bHandled = FALSE;
    return 0;
}

void CShareConversationPopup::SetStatus(const std::wstring& text, bool isError)
{
    if (m_status == NULL)
        return;
    m_status->SetText(text.c_str());
    m_status->SetTextColor(isError ? kErrorColor : kWeakTextColor);
}

void CShareConversationPopup::PostClose()
{
    if (m_closePosted || !IsWindow())
        return;
    m_closePosted = true;
    PostMessage(WM_CLOSE);
}

} // namespace fileshare
