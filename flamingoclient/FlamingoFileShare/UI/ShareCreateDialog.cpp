/**
 *  发起共享与调整成员对话框 CShareCreateDialog 的实现，见 ShareCreateDialog.h。
 *  balloonwj@qq.com   2026-10-03
 */
#include "stdafx.h"
#include "ShareCreateDialog.h"

#include <memory>

#include "SharePickDialog.h"
#include "../Engine/ShareWinPath.h"
#include "../../Source/net/Protocol.h"
#include "../../Source/Common/Log/IULog.h"
#include "../../../third-party/balloonui/balloonui/Controls/Layout/DuiLayout.h"
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiLabel.h"
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiButton.h"
#include "../../../third-party/balloonui/balloonui/Controls/Input/DuiEdit.h"   //「共享名称」输入框
#include "../../../third-party/balloonui/balloonui/Controls/Window/DuiScrollBar.h"
#include "../../../third-party/balloonui/balloonui/Controls/List/DuiMenu.h"
#include "../../../third-party/balloonui/balloonui/DuiResMgr.h"   //GetDefaultFontForDpi：窗口 DPI 变化时按新 DPI 的字体量权限列宽
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiAvatar.h"
#include "../../Source/UI/BalloonMain/ClickableLabel.h"   //CClickableLabel：成员行的名字，点击等于点勾选框
#include "../../Source/Common/I18n/I18n.h"   // TR / TR_NOOP：界面文字的多语言查找

using balloonwjui::DuiControl;
using balloonwjui::DuiLayout;
using balloonwjui::DuiVBox;
using balloonwjui::DuiHBox;
using balloonwjui::DuiLabel;
using balloonwjui::DuiButton;
using balloonwjui::DuiEdit;
using balloonwjui::DuiScrollView;
using balloonwjui::DuiMenu;

namespace fileshare {

namespace {

// ---- 尺寸（像素）----
const int kContentWidth       = 680;   //标题栏以下内容区的默认宽度（整窗宽度与之相同）
const int kContentHeight      = 572;   //标题栏以下内容区的默认高度（2026-10-04 加了「共享名称」一行，比原来高一行）
const int kMinContentHeight   = 432;   //标题栏以下内容区的最小高度（同上，加一行）
const int kTitleBarHeight     = 32;    //标题栏高，与客户端其余窗口一致
const int kSidePadding        = 16;    //左右内边距
const int kTopPadding         = 12;    //上内边距
const int kBottomPadding      = 12;    //下内边距
const int kRowHeight          = 32;    //共享对象、共享名称、隐藏文件、分组标题各行的高度
const int kCaptionWidth       = 72;    //「共享内容」标题宽
const int kPickButtonWidth    = 72;    //「磁盘」「文件夹」「文件」按钮宽
const int kRowGap             = 8;     //同一行内控件之间的间距
const int kHeaderHeight       = 30;    //成员表表头高
const int kMemberRowHeight    = 40;    //成员行高：放得下头像，上下各留 6
const int kMemberAvatarPx     = 28;    //成员行头像的边长（圆形）
const int kMemberCheckWidth   = 28;    //成员行勾选框一列的宽度：只画勾选框，左内边距 6 + 勾选框 16 + 右侧 6
const int kMemberAvatarGap    = 8;     //头像与名字之间的间距
const int kPermColumnWidth    = 92;    //每个权限列的最小宽度（96 DPI 下「新建文件夹」加勾选框正好 92）；实际列宽按字体量出，见 MeasurePermColumnWidth
const int kCheckGlyphWidth    = 28;    //Checkbox 风格的 DuiButton 在文字左侧留出的宽度：左内边距 6 + 勾选框 16 + 与文字的间距 6
                                       //（balloonui DuiButton.cpp 的 kGlyphPadPx、kGlyphSizePx，不随 DPI 缩放）
const int kPermTextTrailing   = 4;     //权限列文字右侧的留白，文字不紧贴下一列的勾选框
const int kStatusHeight       = 26;    //底部提示行高
const int kButtonRowHeight    = 36;    //底部按钮行高
const int kButtonWidth        = 96;    //「取消」「发起共享」按钮宽

// ---- 控件 id（只在本窗口内唯一）----
const UINT kIdPickDisk    = 201;   //「磁盘」
const UINT kIdPickFolder  = 202;   //「文件夹」
const UINT kIdPickFile    = 203;   //「文件」
const UINT kIdHidden      = 204;   //「包含隐藏文件和系统文件」
const UINT kIdAll         = 205;   //表头「成员」：全选或全不选
const UINT kIdCancel      = 206;   //「取消」
const UINT kIdOk          = 207;   //「发起共享」或「保存」
const UINT kIdName        = 208;   //「共享名称」输入框
const UINT kIdColumnBase  = 211;   //表头各权限列的「全选此列」：kIdColumnBase + 列序号（0~4）
const UINT kIdMemberBase  = 1000;  //成员行的控件：kIdMemberBase + 行号 × kIdsPerRow + 列（0 为成员勾选框，1~5 为权限，6、7 见下）
const UINT kIdsPerRow     = 8;     //每个成员行占用的控件 id 个数（8 个全部用上）
const UINT kMemberNameColumn   = 6;   //成员行里名字的列号：点名字等于点勾选框
const UINT kMemberAvatarColumn = 7;   //成员行里头像的列号（头像不响应点击，id 只用来定位控件）

//磁盘菜单项的 id：kMenuDriveBase + 驱动器序号（TrackPopup 返回 0 表示没有选）
const UINT kMenuDriveBase = 1;

// ---- 配色 ----
const COLORREF kBgColor        = RGB(0xFF, 0xFF, 0xFF);   //客户区白底
const COLORREF kTextColor      = RGB(0x1F, 0x1F, 0x1F);   //正文
const COLORREF kWeakTextColor  = RGB(0x9A, 0x98, 0x93);   //提示文字
const COLORREF kErrorColor     = RGB(0xD9, 0x3B, 0x3B);   //错误提示与被拒成员

// ---- 文字 ----
const wchar_t* const kCreateTitle    = TR_NOOP(L"发起共享");
const wchar_t* const kEditTitle      = TR_NOOP(L"调整成员");
const wchar_t* const kObjectCaption  = TR_NOOP(L"共享内容");
const wchar_t* const kNameCaption    = TR_NOOP(L"共享名称");
const wchar_t* const kPickDiskText   = TR_NOOP(L"磁盘");
const wchar_t* const kPickFolderText = TR_NOOP(L"文件夹");
const wchar_t* const kPickFileText   = TR_NOOP(L"文件");
const wchar_t* const kNoObjectText   = TR_NOOP(L"尚未选择，请点右侧按钮选择要共享的磁盘、文件夹或文件");
const wchar_t* const kKindDisk       = TR_NOOP(L"[磁盘] ");
const wchar_t* const kKindFolder     = TR_NOOP(L"[文件夹] ");
const wchar_t* const kKindFile       = TR_NOOP(L"[文件] ");
const wchar_t* const kHiddenText     = TR_NOOP(L"包含隐藏文件和系统文件");
const wchar_t* const kSectionText    = TR_NOOP(L"成员与权限：成员都可以浏览和下载，勾选的写权限只对该成员生效");
const wchar_t* const kSectionFileText= TR_NOOP(L"成员：单个文件只能下载，没有写权限");
const wchar_t* const kAllText        = TR_NOOP(L"成员");
const wchar_t* const kPermTexts[kShareMemberPermColumnCount] = { TR_NOOP(L"上传"), TR_NOOP(L"新建文件夹"), TR_NOOP(L"重命名"), TR_NOOP(L"删除"), TR_NOOP(L"移动") };
const wchar_t* const kLoadingMembers = TR_NOOP(L"正在获取成员…");
const wchar_t* const kNoMembers      = TR_NOOP(L"这个会话里没有可以共享的成员");
const wchar_t* const kInvalidSuffix  = TR_NOOP(L"（不满足条件）");
const wchar_t* const kCancelText     = TR_NOOP(L"取消");
const wchar_t* const kCreateOkText   = TR_NOOP(L"发起共享");
const wchar_t* const kEditOkText     = TR_NOOP(L"保存");
const wchar_t* const kRemovableSuffix= TR_NOOP(L"（可移动磁盘）");
const wchar_t* const kPickFolderTitle= TR_NOOP(L"选择要共享的文件夹");
const wchar_t* const kPickFileTitle  = TR_NOOP(L"选择要共享的文件");
const wchar_t* const kNeedObjectText = TR_NOOP(L"请先选择要共享的磁盘、文件夹或文件");
const wchar_t* const kNeedMemberText = TR_NOOP(L"请至少勾选一名成员");
const wchar_t* const kKeepMemberText = TR_NOOP(L"请至少保留一名成员；不再共享请取消共享");
const wchar_t* const kNoChangeText   = TR_NOOP(L"成员与权限没有变化");
const wchar_t* const kSendFailText   = TR_NOOP(L"发送失败，请检查是否已登录");
const wchar_t* const kCreatingText   = TR_NOOP(L"正在发起…");
const wchar_t* const kCreatingGroupText = TR_NOOP(L"正在建群…");
const wchar_t* const kGroupSendFailText = TR_NOOP(L"没有发出建群请求，请检查网络后重试");
const wchar_t* const kGroupFailFormat   = TR_NOOP(L"建群失败（错误码 %d），请稍后重试");
const wchar_t* const kGroupKeptPrefix   = TR_NOOP(L"群已建好，但发起共享失败：");

//建群失败提示的缓冲区字符数
const size_t kGroupFailBufChars = 128;
const wchar_t* const kSavingText     = TR_NOOP(L"正在保存…");
const wchar_t* const kNoResponseText = TR_NOOP(L"服务器没有响应，请检查网络后重试");
const wchar_t* const kMembersFailText= TR_NOOP(L"获取成员失败，请关闭后重新打开");
const wchar_t* const kRiskTitle      = TR_NOOP(L"发起共享");
const wchar_t* const kRiskText       = TR_NOOP(
    L"要共享的位置是系统盘、Windows 目录、Program Files 或用户目录，其中可能有系统文件和个人资料。\n\n"
    L"成员可以浏览并下载其中的全部文件。若授予了删除、重命名、移动等写权限，而本客户端又是以管理员身份运行的，"
    L"成员还可能改动系统文件。\n\n确定要共享吗？");

//UTF-8 转成界面用的宽字符串
std::wstring Wide(const std::string& utf8)
{
    std::wstring w;
    ShareUtf8ToWide(utf8, w);
    return w;
}

//权限列的宽度，单位：像素：勾选框区域加五个表头文字中最宽的一个，再加右侧留白，不小于 kPermColumnWidth。
//列宽是固定像素，字体却随 DPI 放大，DPI 高于 96 时按固定宽度会截断「新建文件夹」，所以按字体量。
//font 为表头勾选框所用的字体，为 NULL 时按设备上下文的默认字体测量
int MeasurePermColumnWidth(HFONT font)
{
    HDC hdc = ::GetDC(NULL);
    HGDIOBJ oldFont = (font != NULL) ? ::SelectObject(hdc, font) : NULL;
    int widest = 0;
    for (size_t i = 0; i < kShareMemberPermColumnCount; ++i)
    {
        SIZE size = { 0, 0 };
        ::GetTextExtentPoint32W(hdc, TR(kPermTexts[i]), static_cast<int>(wcslen(TR(kPermTexts[i]))), &size);
        if (size.cx > widest)
            widest = size.cx;
    }
    if (oldFont != NULL)
        ::SelectObject(hdc, oldFont);
    ::ReleaseDC(NULL, hdc);
    const int width = kCheckGlyphWidth + widest + kPermTextTrailing;
    return (width > kPermColumnWidth) ? width : kPermColumnWidth;
}

//整窗宽度，单位：像素：权限列比最小宽度宽出多少，窗口就加宽多少，成员名一列保持 96 DPI 下的设计宽度
int ContentWidthFor(int permColumnWidth)
{
    return kContentWidth + static_cast<int>(kShareMemberPermColumnCount) * (permColumnWidth - kPermColumnWidth);
}

//建一个 Checkbox 风格的按钮。用 Ghost 变体：只画勾选框本身，不给整格画底色与边框，成员表才不显得拥挤
std::unique_ptr<DuiButton> MakeCheck(const wchar_t* text, UINT id)
{
    std::unique_ptr<DuiButton> check(new DuiButton());
    check->SetButtonType(DuiButton::StyleCheckbox);
    check->SetVariant(DuiButton::Variant::Ghost);
    check->SetText(text);
    check->SetTextColor(kTextColor);
    check->SetCtrlId(id);
    return check;
}

//建一个 Ghost 样式的按钮
std::unique_ptr<DuiButton> MakeGhost(const wchar_t* text, UINT id)
{
    std::unique_ptr<DuiButton> button(new DuiButton());
    button->SetText(text);
    button->SetVariant(DuiButton::Variant::Ghost);
    button->SetCtrlId(id);
    return button;
}

} // namespace

CShareCreateDialog::CShareCreateDialog()
    : m_service(NULL)
    , m_host(NULL)
    , m_editMode(false)
    , m_targetMode(false)
    , m_waitingGroup(false)
    , m_groupCreated(false)
    , m_openConversationOnSuccess(false)
    , m_ctype(0)
    , m_cid(0)
    , m_shareId(0)
    , m_kind(0)
    , m_membersLoaded(false)
    , m_requestId(0)
    , m_permColumnWidth(kPermColumnWidth)
    , m_root(NULL)
    , m_objectLabel(NULL)
    , m_pickRow(NULL)
    , m_pickDisk(NULL)
    , m_pickFolder(NULL)
    , m_pickFile(NULL)
    , m_nameEdit(NULL)
    , m_hiddenCheck(NULL)
    , m_sectionLabel(NULL)
    , m_header(NULL)
    , m_allCheck(NULL)
    , m_scroll(NULL)
    , m_status(NULL)
    , m_okButton(NULL)
{
    for (size_t i = 0; i < kShareMemberPermColumnCount; ++i)
        m_columnChecks[i] = NULL;
}

CShareCreateDialog::~CShareCreateDialog()
{
    Close();
}

bool CShareCreateDialog::OpenForCreate(HWND owner, CShareService* service, IFileShareHost* host, int ctype, int64_t cid)
{
    if (service == NULL || host == NULL || cid <= 0 || (ctype != conversation_single && ctype != conversation_group))
        return false;
    if (IsWindow())
    {
        ::SetForegroundWindow(m_hWnd);
        return true;
    }

    m_service = service;
    m_host = host;
    m_editMode = false;
    m_targetMode = false;
    m_waitingGroup = false;
    m_groupCreated = false;
    m_ctype = ctype;
    m_cid = cid;
    m_shareId = 0;
    m_path.clear();
    m_kind = 0;
    m_label.clear();
    m_before.clear();
    m_table.Reset(std::vector<ShareMemberRow>(), false);
    m_membersLoaded = false;
    m_requestId = 0;
    if (!CreateCommon(owner, TR(kCreateTitle)))
        return false;
    LOG_INFO("share create dialog opened, ctype: %d, cid: %lld.", ctype, static_cast<long long>(cid));

    //单聊可能在本次调用返回之前就回送成员，此时窗口与会话都已就绪
    if (!m_host->RequestConversationMembers(ctype, cid))
        SetStatus(TR(kMembersFailText), true);
    return true;
}

bool CShareCreateDialog::OpenForEdit(HWND owner, CShareService* service, IFileShareHost* host, const ShareListEntry& share)
{
    if (service == NULL || host == NULL || share.m_shareId <= 0)
        return false;
    if (IsWindow())
    {
        ::SetForegroundWindow(m_hWnd);
        return true;
    }

    m_service = service;
    m_host = host;
    m_editMode = true;
    m_targetMode = false;
    m_waitingGroup = false;
    m_groupCreated = false;
    m_ctype = share.m_ctype;
    m_cid = share.m_cid;
    m_shareId = share.m_shareId;
    m_path = Wide(share.m_displayPath);
    m_kind = share.m_kind;
    m_label.clear();
    m_editName = share.m_name;
    m_editDisplayPath = share.m_displayPath;
    m_before = share.m_members;
    m_table.Reset(std::vector<ShareMemberRow>(), share.m_kind == share_kind_file);
    m_membersLoaded = false;
    m_requestId = 0;
    if (!CreateCommon(owner, TR(kEditTitle)))
        return false;
    LOG_INFO("share edit members dialog opened, shareid: %lld, members: %u.", static_cast<long long>(m_shareId),
             static_cast<unsigned>(m_before.size()));

    if (!m_host->RequestConversationMembers(m_ctype, m_cid))
        SetStatus(TR(kMembersFailText), true);
    return true;
}

bool CShareCreateDialog::OpenForTargets(HWND owner, CShareService* service, IFileShareHost* host,
                                        const std::vector<ShareMemberRow>& targets)
{
    if (service == NULL || host == NULL || targets.empty())
        return false;
    if (IsWindow())
    {
        ::SetForegroundWindow(m_hWnd);
        return true;
    }

    m_service = service;
    m_host = host;
    m_editMode = false;
    m_targetMode = true;
    m_waitingGroup = false;
    m_groupCreated = false;
    //只选了 1 人：会话就是与他的单聊；多人：会话待定（提交时按勾选人数决定，见文件头）
    const bool single = (targets.size() == 1);
    m_ctype = single ? conversation_single : conversation_group;
    m_cid = single ? targets[0].m_userId : 0;
    m_shareId = 0;
    m_path.clear();
    m_kind = 0;
    m_label.clear();
    m_before.clear();
    m_membersLoaded = true;
    m_requestId = 0;
    //选好的人全部勾选；单聊时他固定勾选（ApplyCandidates 里处理）
    ApplyCandidates(targets);
    for (size_t i = 0; i < targets.size(); ++i)
        m_table.SetChecked(targets[i].m_userId, true);
    if (!CreateCommon(owner, TR(kCreateTitle)))
        return false;
    LOG_INFO("share create dialog opened for picked targets, count: %u.", static_cast<unsigned>(targets.size()));
    return true;
}

void CShareCreateDialog::OnShareGroupCreated(int code, int64_t groupId)
{
    if (!m_waitingGroup)
        return;
    m_waitingGroup = false;
    if (m_okButton != NULL)
        m_okButton->SetEnabled(true);
    if (code != error_code_ok || groupId <= 0)
    {
        LOG_WARNING("share create dialog: group for picked targets not created, code: %d.", code);
        wchar_t text[kGroupFailBufChars] = { 0 };
        _snwprintf_s(text, kGroupFailBufChars, _TRUNCATE, TR(kGroupFailFormat), code);
        SetStatus(text, true);
        return;
    }
    //群已建好：之后（含共享被拒后的重试）都在这个群里发起
    m_groupCreated = true;
    m_ctype = conversation_group;
    m_cid = groupId;
    LOG_INFO("share create dialog: group created for picked targets, groupid: %lld.", static_cast<long long>(groupId));
    //风险提示在建群之前已经确认过
    Submit(true);
}

bool CShareCreateDialog::RequestGroup(const std::vector<ShareMemberPerm>& members)
{
    std::vector<int64_t> ids;
    for (size_t i = 0; i < members.size(); ++i)
        ids.push_back(members[i].m_userId);
    //群名「共享 <共享名> 给 <成员名>」：成员名按成员表的顺序取勾选的人（2026-10-03 用户要求）
    std::vector<std::wstring> names;
    const std::vector<ShareMemberRow>& rows = m_table.Rows();
    for (size_t i = 0; i < ids.size(); ++i)
    {
        std::wstring name;
        for (size_t r = 0; r < rows.size(); ++r)
        {
            if (rows[r].m_userId == ids[i])
            {
                name = Wide(rows[r].m_name);
                break;
            }
        }
        names.push_back(name);
    }
    const std::string groupName = ShareWideToUtf8(ShareCreateModel::DirectShareGroupName(
        CurrentShareName(), names, kShareGroupNameMaxChars));
    if (m_host == NULL || ids.empty()
        || !m_host->CreateShareGroup(groupName.c_str(), &ids[0], static_cast<uint32_t>(ids.size())))
    {
        SetStatus(TR(kGroupSendFailText), true);
        return false;
    }
    m_waitingGroup = true;
    SetStatus(TR(kCreatingGroupText), false);
    if (m_okButton != NULL)
        m_okButton->SetEnabled(false);
    LOG_INFO("share create dialog: group requested for picked targets, members: %u.", static_cast<unsigned>(ids.size()));
    return true;
}

bool CShareCreateDialog::CreateCommon(HWND owner, const wchar_t* title)
{
    //带所有者创建：对话框始终在所有者之上，所有者销毁时一并销毁
    HWND parent = (owner != NULL && ::IsWindow(owner)) ? owner : NULL;
    if (Create(parent, CWindow::rcDefault, title, WS_OVERLAPPEDWINDOW, 0) == NULL)
    {
        LOG_ERROR("share create dialog: Create failed, error: %lu.", ::GetLastError());
        return false;
    }
    SetTitle(title);
    SetTitleBarHeight(kTitleBarHeight);
    SetButtons(false, false, true);
    //程序图标：标题栏左侧的位图与任务栏 / Alt-Tab 图标（图标资源在 exe 里，见 TitleBarIcon.h）
    SetIcon(m_programIcons.TitleBitmap(balloonmain::kProgramTitleIconSize, balloonmain::kOpaqueTitleBarColor));
    m_programIcons.ApplyTaskIcons(m_hWnd);
    SetResizable(true);
    //分享者本人的名字：没起共享名称时的默认名「XXX的文件共享」用它。优先昵称，昵称为空时用账号
    m_selfName.clear();
    FileShareSessionInfo session;
    memset(&session, 0, sizeof(session));
    session.m_cbSize = sizeof(session);
    if (m_host != NULL && m_host->GetSessionInfo(&session))
        m_selfName = Wide(session.m_nickName[0] != '\0' ? session.m_nickName : session.m_account);
    BuildContent();
    //BuildContent 已按窗口 DPI 的字体量出权限列宽，窗口随之加宽。
    //DuiFrameWindow 的 SetMinSize、ResizeClient 设的都是整窗尺寸，标题栏也在其内（UI 约定第 7 条）
    SetMinSize(ContentWidthFor(m_permColumnWidth), kMinContentHeight + kTitleBarHeight);
    ResizeClient(ContentWidthFor(m_permColumnWidth), kContentHeight + kTitleBarHeight);
    CenterWindow(parent);
    UpdateObjectRow();
    RefreshMemberRows();
    ShowWindow(SW_SHOW);
    ::SetForegroundWindow(m_hWnd);
    //初始焦点放在表头的「成员」勾选框上：否则落在第一个按钮「磁盘」上，按钮带着焦点虚线框；勾选框不画焦点框
    if (m_allCheck != NULL)
        m_allCheck->SetFocus();
    return true;
}

void CShareCreateDialog::Close()
{
    if (m_service != NULL)
        m_service->DetachManageObserver(this);
    m_requestId = 0;
    m_waitingGroup = false;
    if (IsWindow())
        DestroyWindow();
    //头像控件随窗口一起销毁之后再释放它们借用的位图（DuiAvatar 不拥有位图，也不会再绘制）
    m_avatars.Clear();
    m_rowCtrls.clear();
    m_root = NULL;
    m_objectLabel = NULL;
    m_pickRow = NULL;
    m_pickDisk = NULL;
    m_pickFolder = NULL;
    m_pickFile = NULL;
    m_nameEdit = NULL;
    m_hiddenCheck = NULL;
    m_sectionLabel = NULL;
    m_header = NULL;
    m_allCheck = NULL;
    for (size_t i = 0; i < kShareMemberPermColumnCount; ++i)
        m_columnChecks[i] = NULL;
    m_scroll = NULL;
    m_status = NULL;
    m_okButton = NULL;
}

void CShareCreateDialog::OnConversationMembers(int ctype, int64_t cid, int code, const FileShareMemberInfo* members,
                                               uint32_t count)
{
    if (!IsWindow() || ctype != m_ctype || cid != m_cid || m_membersLoaded)
        return;
    if (code != 0)
    {
        LOG_WARNING("share create dialog: members not available, ctype: %d, cid: %lld, code: %d.", ctype,
                    static_cast<long long>(cid), code);
        SetStatus(TR(kMembersFailText), true);
        return;
    }

    //候选不含本人：分享者不是自己共享的成员
    const int64_t self = (m_service != NULL) ? m_service->GetUserId() : 0;
    std::vector<ShareMemberRow> candidates;
    for (uint32_t i = 0; i < count && members != NULL; ++i)
    {
        if (members[i].m_userId <= 0 || members[i].m_userId == self)
            continue;
        ShareMemberRow row;
        row.m_userId = members[i].m_userId;
        row.m_name = members[i].m_name;
        candidates.push_back(row);
    }
    m_membersLoaded = true;
    ApplyCandidates(candidates);
    RefreshMemberRows();
    LOG_INFO("share create dialog: members arrived, ctype: %d, cid: %lld, candidates: %u.", ctype,
             static_cast<long long>(cid), static_cast<unsigned>(candidates.size()));
}

void CShareCreateDialog::ApplyCandidates(const std::vector<ShareMemberRow>& candidates)
{
    const bool fileShare = (m_kind == share_kind_file);
    m_table.Reset(candidates, fileShare);
    if (m_editMode)
    {
        //已有成员里不在候选中的（例如已退群的人）追加到表末尾，名字经宿主取
        std::vector<ShareMemberRow> names;
        for (size_t i = 0; i < m_before.size(); ++i)
        {
            char buf[kFileShareTextBytes] = { 0 };
            ShareMemberRow row;
            row.m_userId = m_before[i].m_userId;
            if (m_host != NULL && m_host->GetUserDisplayName(m_before[i].m_userId, buf, kFileShareTextBytes))
                row.m_name = buf;
            names.push_back(row);
        }
        m_table.ApplyExisting(m_before, names);
        return;
    }
    //单聊：只能共享给对方一人，固定勾选
    if (m_ctype == conversation_single)
        m_table.SetFixed(m_cid);
}

void CShareCreateDialog::OnShareManageResult(const ShareManageResult& result)
{
    if (result.m_requestId != m_requestId || m_requestId == 0)
        return;
    m_requestId = 0;
    if (m_okButton != NULL)
        m_okButton->SetEnabled(true);

    if (result.m_code == error_code_ok)
    {
        LOG_INFO("share %s succeeded, shareid: %lld.", m_editMode ? "edit members" : "create",
                 static_cast<long long>(result.m_shareId));
        //按选好的对象发起、或从「文件共享」窗口在某个会话里发起：打开共享所在的单聊或群聊，卡片就在那里
        if ((m_targetMode || (m_openConversationOnSuccess && !m_editMode)) && m_host != NULL)
        {
            const bool opened = m_host->OpenConversation(m_ctype, m_cid);
            LOG_INFO("share create dialog: conversation opened after share, ctype: %d, cid: %lld, ok: %d.", m_ctype,
                     static_cast<long long>(m_cid), opened ? 1 : 0);
        }
        Close();
        return;
    }
    if (!result.m_invalidMembers.empty())
    {
        m_table.MarkInvalid(result.m_invalidMembers);
        RefreshMemberRows();
    }
    //群已建好而共享被拒：说明群留着，重试时仍在这个群里发起
    if (m_targetMode && m_groupCreated)
        SetStatus(std::wstring(TR(kGroupKeptPrefix)) + ResultText(result.m_code), true);
    else
        SetStatus(ResultText(result.m_code), true);
}

void CShareCreateDialog::SetShareObject(const std::wstring& path, int kind, const std::wstring& label)
{
    if (m_editMode || path.empty())
        return;
    const bool wasFile = m_table.IsFileShare();
    m_path = path;
    m_kind = kind;
    m_label = label;
    m_table.SetFileShare(kind == share_kind_file);
    UpdateObjectRow();
    if (wasFile != m_table.IsFileShare())
        RefreshMemberRows();
    SetStatus(std::wstring(), false);
}

bool CShareCreateDialog::Submit(bool riskConfirmed)
{
    if (m_service == NULL || m_requestId != 0 || m_waitingGroup)
        return false;

    const std::vector<ShareMemberPerm> members = m_table.CheckedMembers();
    if (m_editMode)
    {
        if (members.empty())
        {
            SetStatus(TR(kKeepMemberText), true);
            return false;
        }
        std::vector<ShareMemberPerm> add;
        std::vector<int64_t> remove;
        std::vector<ShareMemberPerm> update;
        if (!m_table.Diff(m_before, add, remove, update))
        {
            SetStatus(TR(kNoChangeText), true);
            return false;
        }
        m_requestId = m_service->UpdateMembers(m_shareId, add, remove, update, this);
        if (m_requestId == 0)
        {
            SetStatus(TR(kSendFailText), true);
            return false;
        }
        SetStatus(TR(kSavingText), false);
    }
    else
    {
        if (m_path.empty() || m_kind == 0)
        {
            SetStatus(TR(kNeedObjectText), true);
            return false;
        }
        if (members.empty())
        {
            SetStatus(TR(kNeedMemberText), true);
            return false;
        }
        //敏感位置：确认前弹一次风险提示，只提示、不限制权限（方案 9.3）
        if (!riskConfirmed && ShareCreateModel::IsSensitivePath(m_path, ShareCreateModel::CollectSensitivePaths()))
        {
            if (::MessageBoxW(m_hWnd, TR(kRiskText), TR(kRiskTitle), MB_OKCANCEL | MB_ICONWARNING | MB_DEFBUTTON2) != IDOK)
                return false;
        }

        //按选好的对象发起、群还没建：勾选 1 人在与他的单聊里发起；多人先请宿主建群，建好后由 OnShareGroupCreated 接着发起
        if (m_targetMode && !m_groupCreated)
        {
            if (members.size() > 1)
                return RequestGroup(members);
            m_ctype = conversation_single;
            m_cid = members[0].m_userId;
        }

        ShareCreateParams params;
        params.m_ctype = m_ctype;
        params.m_cid = m_cid;
        params.m_kind = m_kind;
        params.m_name = ShareWideToUtf8(CurrentShareName());
        params.m_displayPath = ShareWideToUtf8(m_path);
        params.m_includeHidden = (m_kind != share_kind_file && m_hiddenCheck != NULL && m_hiddenCheck->IsChecked());
        params.m_members = members;
        m_requestId = m_service->CreateShare(params, m_path, this);
        if (m_requestId == 0)
        {
            SetStatus(TR(kSendFailText), true);
            return false;
        }
        SetStatus(TR(kCreatingText), false);
    }
    if (m_okButton != NULL)
        m_okButton->SetEnabled(false);
    return true;
}

CShareMemberTable& CShareCreateDialog::GetMemberTable()
{
    return m_table;
}

std::wstring CShareCreateDialog::GetStatusText() const
{
    return (m_status != NULL) ? std::wstring(m_status->GetText().GetString()) : std::wstring();
}

void CShareCreateDialog::SetShareNameText(const std::wstring& name)
{
    if (m_nameEdit != NULL)
        m_nameEdit->SetText(name.c_str());
}

bool CShareCreateDialog::GetShareNameState(std::wstring& text, std::wstring& placeholder, bool& enabled) const
{
    if (m_nameEdit == NULL)
        return false;
    text = m_nameEdit->GetText().GetString();
    placeholder = m_nameEdit->GetPlaceholder().GetString();
    enabled = m_nameEdit->IsEnabled();
    return true;
}

std::wstring CShareCreateDialog::CurrentShareName() const
{
    const std::wstring input = (m_nameEdit != NULL) ? std::wstring(m_nameEdit->GetText().GetString()) : std::wstring();
    return ShareCreateModel::ResolveShareName(input, m_selfName);
}

bool CShareCreateDialog::IsBusy() const
{
    return m_requestId != 0 || m_waitingGroup;
}

int CShareCreateDialog::GetCtype() const
{
    return m_ctype;
}

int64_t CShareCreateDialog::GetCid() const
{
    return m_cid;
}

LRESULT CShareCreateDialog::OnCloseMsg(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& bHandled)
{
    bHandled = TRUE;
    Close();
    return 0;
}

LRESULT CShareCreateDialog::OnDuiNotify(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam, BOOL& bHandled)
{
    const balloonwjui::DuiNotify* pNotify = reinterpret_cast<const balloonwjui::DuiNotify*>(lParam);
    if (pNotify == NULL)
    {
        bHandled = FALSE;
        return 0;
    }

    //勾选框：表头「成员」、各列的「全选此列」、成员行（通知码与控件 id 一起判，UI 约定第 4 条）
    const UINT id = pNotify->ctrlId;
    const bool memberCheck = (id == kIdAll)
        || (id >= kIdColumnBase && id < kIdColumnBase + kShareMemberPermColumnCount)
        || (id >= kIdMemberBase && id < kIdMemberBase + kIdsPerRow * static_cast<UINT>(m_rowCtrls.size()));
    if (pNotify->code == balloonwjui::DUIN_VALUECHANGED && memberCheck)
    {
        OnMemberCheckChanged(id, pNotify->extra != 0);
        return 0;
    }
    //成员行的名字：点击等于点该成员的勾选框（通知码、控件 id 与列号一起判）
    const bool memberName = (id >= kIdMemberBase && id < kIdMemberBase + kIdsPerRow * static_cast<UINT>(m_rowCtrls.size())
                             && (id - kIdMemberBase) % kIdsPerRow == kMemberNameColumn);
    if (pNotify->code == balloonwjui::DUIN_CLICK && memberName)
    {
        OnMemberNameClicked((id - kIdMemberBase) / kIdsPerRow);
        return 0;
    }
    if (pNotify->code != balloonwjui::DUIN_CLICK)
    {
        bHandled = FALSE;
        return 0;
    }

    switch (id)
    {
    //「磁盘」：弹出本机驱动器菜单
    case kIdPickDisk:
        OnPickDisk();
        return 0;

    //「文件夹」「文件」：弹出系统的选择框
    case kIdPickFolder:
        OnPickFolderOrFile(true);
        return 0;
    case kIdPickFile:
        OnPickFolderOrFile(false);
        return 0;

    //「取消」：关闭，不发请求。在通知处理过程中不直接销毁窗口（按钮的鼠标处理还在调用栈上），投递 WM_CLOSE
    case kIdCancel:
        PostMessage(WM_CLOSE);
        return 0;

    //「发起共享」或「保存」
    case kIdOk:
        Submit(false);
        return 0;

    //其余控件的点击（含勾选框随状态变化一起发出的点击）：交给基类
    default:
        bHandled = FALSE;
        return 0;
    }
}

void CShareCreateDialog::BuildContent()
{
    typedef DuiLayout::Hint Hint;

    std::unique_ptr<DuiVBox> root(new DuiVBox());
    root->SetBgColor(kBgColor);
    root->SetPadding(kSidePadding, kTopPadding, kSidePadding, kBottomPadding);
    m_root = root.get();

    //共享对象：标题 | 路径 | 磁盘 | 文件夹 | 文件（调整成员时不显示三个按钮）
    std::unique_ptr<DuiHBox> pickRow(new DuiHBox());
    pickRow->SetGap(kRowGap);
    std::unique_ptr<DuiLabel> caption(new DuiLabel());
    caption->SetText(TR(kObjectCaption));
    caption->SetTextColor(kTextColor);
    caption->SetTextAlign(DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    pickRow->AddChild(std::unique_ptr<DuiControl>(caption.release()), Hint().Fixed(kCaptionWidth));
    std::unique_ptr<DuiLabel> object(new DuiLabel());
    object->SetTextAlign(DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_PATH_ELLIPSIS);
    m_objectLabel = object.get();
    pickRow->AddChild(std::unique_ptr<DuiControl>(object.release()), Hint().Weight(1));
    std::unique_ptr<DuiButton> disk = MakeGhost(TR(kPickDiskText), kIdPickDisk);
    m_pickDisk = disk.get();
    pickRow->AddChild(std::unique_ptr<DuiControl>(disk.release()), Hint().Fixed(kPickButtonWidth));
    std::unique_ptr<DuiButton> folder = MakeGhost(TR(kPickFolderText), kIdPickFolder);
    m_pickFolder = folder.get();
    pickRow->AddChild(std::unique_ptr<DuiControl>(folder.release()), Hint().Fixed(kPickButtonWidth));
    std::unique_ptr<DuiButton> file = MakeGhost(TR(kPickFileText), kIdPickFile);
    m_pickFile = file.get();
    pickRow->AddChild(std::unique_ptr<DuiControl>(file.release()), Hint().Fixed(kPickButtonWidth));
    if (m_editMode)
    {
        m_pickDisk->SetVisible(false);
        m_pickFolder->SetVisible(false);
        m_pickFile->SetVisible(false);
    }
    m_pickRow = pickRow.get();
    root->AddChild(std::unique_ptr<DuiControl>(pickRow.release()), Hint().Fixed(kRowHeight));

    //共享名称：分享者以外的人只看到这个名字，看不到真实路径（2026-10-04 起）。提示文字就是不填时使用的默认名称；
    //调整成员时显示当前名称、不可改（服务端没有改名的命令）
    std::unique_ptr<DuiHBox> nameRow(new DuiHBox());
    nameRow->SetGap(kRowGap);
    std::unique_ptr<DuiLabel> nameCaption(new DuiLabel());
    nameCaption->SetText(TR(kNameCaption));
    nameCaption->SetTextColor(kTextColor);
    nameCaption->SetTextAlign(DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    nameRow->AddChild(std::unique_ptr<DuiControl>(nameCaption.release()), Hint().Fixed(kCaptionWidth));
    std::unique_ptr<DuiEdit> nameEdit(new DuiEdit());
    nameEdit->SetCtrlId(kIdName);
    nameEdit->SetMaxLength(static_cast<int>(kShareTitleNameMaxChars));
    nameEdit->SetVerticalCenter(true);
    nameEdit->SetShowBorder(true);
    nameEdit->SetPlaceholder(ShareCreateModel::DefaultShareName(m_selfName).c_str());
    if (m_editMode)
    {
        nameEdit->SetTextNoNotify(Wide(m_editName).c_str());
        nameEdit->SetEnabled(false);
    }
    m_nameEdit = nameEdit.get();
    nameRow->AddChild(std::unique_ptr<DuiControl>(nameEdit.release()), Hint().Weight(1));
    root->AddChild(std::unique_ptr<DuiControl>(nameRow.release()), Hint().Fixed(kRowHeight));

    //包含隐藏文件（调整成员时不可改，不显示；单文件共享时没有意义，也不显示）
    std::unique_ptr<DuiButton> hidden = MakeCheck(TR(kHiddenText), kIdHidden);
    m_hiddenCheck = hidden.get();
    root->AddChild(std::unique_ptr<DuiControl>(hidden.release()), Hint().Fixed(kRowHeight));

    //分组标题
    std::unique_ptr<DuiLabel> section(new DuiLabel());
    section->SetTextColor(kWeakTextColor);
    section->SetTextAlign(DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    m_sectionLabel = section.get();
    root->AddChild(std::unique_ptr<DuiControl>(section.release()), Hint().Fixed(kRowHeight));

    //表头：成员（全选）| 五个权限列（全选此列）| 滚动条占位
    std::unique_ptr<DuiHBox> header(new DuiHBox());
    std::unique_ptr<DuiButton> all = MakeCheck(TR(kAllText), kIdAll);
    m_allCheck = all.get();
    header->AddChild(std::unique_ptr<DuiControl>(all.release()), Hint().Weight(1));
    for (size_t i = 0; i < kShareMemberPermColumnCount; ++i)
    {
        std::unique_ptr<DuiButton> column = MakeCheck(TR(kPermTexts[i]), kIdColumnBase + static_cast<UINT>(i));
        m_columnChecks[i] = column.get();
        header->AddChild(std::unique_ptr<DuiControl>(column.release()), Hint().Fixed(m_permColumnWidth));
    }
    //2026-10-04 起成员表的滚动条为悬浮式、不占列宽，表头不再需要在末尾留出与滚动条同宽的占位
    m_header = header.get();
    root->AddChild(std::unique_ptr<DuiControl>(header.release()), Hint().Fixed(kHeaderHeight));

    //成员表
    std::unique_ptr<DuiScrollView> scroll(new DuiScrollView());
    m_scroll = scroll.get();
    root->AddChild(std::unique_ptr<DuiControl>(scroll.release()), Hint().Weight(1));

    //提示行
    std::unique_ptr<DuiLabel> status(new DuiLabel());
    status->SetTextColor(kWeakTextColor);
    status->SetTextAlign(DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    m_status = status.get();
    root->AddChild(std::unique_ptr<DuiControl>(status.release()), Hint().Fixed(kStatusHeight));

    //按钮行：取消 | 发起共享
    std::unique_ptr<DuiHBox> buttons(new DuiHBox());
    buttons->SetGap(kRowGap);
    buttons->AddChild(std::unique_ptr<DuiControl>(new DuiLabel()), Hint().Weight(1));
    std::unique_ptr<DuiButton> cancel(new DuiButton());
    cancel->SetText(TR(kCancelText));
    cancel->SetVariant(DuiButton::Variant::Outlined);
    cancel->SetCtrlId(kIdCancel);
    buttons->AddChild(std::unique_ptr<DuiControl>(cancel.release()), Hint().Fixed(kButtonWidth));
    std::unique_ptr<DuiButton> ok(new DuiButton());
    ok->SetText(m_editMode ? TR(kEditOkText) : TR(kCreateOkText));
    ok->SetCtrlId(kIdOk);
    m_okButton = ok.get();
    buttons->AddChild(std::unique_ptr<DuiControl>(ok.release()), Hint().Fixed(kButtonWidth));
    root->AddChild(std::unique_ptr<DuiControl>(buttons.release()), Hint().Fixed(kButtonRowHeight));

    SetClientContent(std::unique_ptr<DuiControl>(root.release()));
    //控件挂到窗口上之后才拿得到窗口 DPI 下的字体，这时再量权限列宽
    UpdatePermColumnWidth(m_allCheck->GetDefaultFont());
}

void CShareCreateDialog::UpdatePermColumnWidth(HFONT font)
{
    m_permColumnWidth = MeasurePermColumnWidth(font);
    if (m_header == NULL)
        return;
    //SetHint 改完会让表头立即重新排版
    for (size_t i = 0; i < kShareMemberPermColumnCount; ++i)
    {
        if (m_columnChecks[i] != NULL)
            m_header->SetHint(m_columnChecks[i], DuiLayout::Hint().Fixed(m_permColumnWidth));
    }
}

LRESULT CShareCreateDialog::OnDpiChangedMsg(UINT /*uMsg*/, WPARAM wParam, LPARAM /*lParam*/, BOOL& bHandled)
{
    //基类（DuiHost）随后把新 DPI 通知给各控件并整体重新布局，这里只更新权限列宽、窗口最小宽度，并按新列宽重建成员行，
    //不拦截消息。系统给出的横纵两个 DPI 相同，取横向的那个
    bHandled = FALSE;
    const int dpi = LOWORD(wParam);
    if (dpi > 0)
    {
        UpdatePermColumnWidth(balloonwjui::DuiResMgr::Inst().GetDefaultFontForDpi(dpi));
        SetMinSize(ContentWidthFor(m_permColumnWidth), kMinContentHeight + kTitleBarHeight);
        RefreshMemberRows();
    }
    return 0;
}

void CShareCreateDialog::UpdateObjectRow()
{
    if (m_objectLabel == NULL || m_hiddenCheck == NULL || m_root == NULL)
        return;
    if (m_path.empty())
    {
        m_objectLabel->SetText(TR(kNoObjectText));
        m_objectLabel->SetTextColor(kWeakTextColor);
    }
    else
    {
        const wchar_t* prefix = (m_kind == share_kind_disk) ? TR(kKindDisk) : ((m_kind == share_kind_file) ? TR(kKindFile) : TR(kKindFolder));
        std::wstring text = prefix;
        text += (m_kind == share_kind_disk) ? ShareCreateModel::DisplayName(m_path, m_kind, m_label) : m_path;
        m_objectLabel->SetText(text.c_str());
        m_objectLabel->SetTextColor(kTextColor);
    }

    //「包含隐藏文件」：调整成员时不可改、单文件共享时没有意义，都不显示；显隐变化后对父容器重排（UI 约定第 5 条）
    const bool showHidden = !m_editMode && m_kind != share_kind_file;
    if (m_hiddenCheck->IsVisible() != showHidden)
    {
        m_hiddenCheck->SetVisible(showHidden);
        m_root->ForceLayout(m_root->GetRect());
    }
}

void CShareCreateDialog::RefreshMemberRows()
{
    if (m_scroll == NULL || m_header == NULL)
        return;
    typedef DuiLayout::Hint Hint;
    const bool fileShare = m_table.IsFileShare();

    //分组标题与表头的权限列按共享类型显隐；显隐变化后对表头重排（UI 约定第 5 条）
    if (m_sectionLabel != NULL)
        m_sectionLabel->SetText(fileShare ? TR(kSectionFileText) : TR(kSectionText));
    bool headerChanged = false;
    for (size_t i = 0; i < kShareMemberPermColumnCount; ++i)
    {
        if (m_columnChecks[i] != NULL && m_columnChecks[i]->IsVisible() == fileShare)
        {
            m_columnChecks[i]->SetVisible(!fileShare);
            headerChanged = true;
        }
    }
    if (headerChanged)
        m_header->ForceLayout(m_header->GetRect());

    m_rowCtrls.clear();
    m_avatars.SetHost(m_host);
    std::unique_ptr<DuiVBox> list(new DuiVBox());
    const std::vector<ShareMemberRow>& rows = m_table.Rows();
    if (!m_membersLoaded || rows.empty())
    {
        std::unique_ptr<DuiLabel> hint(new DuiLabel());
        hint->SetText(m_membersLoaded ? TR(kNoMembers) : TR(kLoadingMembers));
        hint->SetTextColor(kWeakTextColor);
        hint->SetTextAlign(DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        list->AddChild(std::unique_ptr<DuiControl>(hint.release()), Hint().Fixed(kMemberRowHeight));
        m_scroll->SetContentHeight(kMemberRowHeight);
    }
    else
    {
        for (size_t r = 0; r < rows.size(); ++r)
        {
            const UINT base = kIdMemberBase + static_cast<UINT>(r) * kIdsPerRow;
            MemberRowCtrls ctrls;
            std::unique_ptr<DuiHBox> line(new DuiHBox());

            //勾选框：只画勾选框，名字另起一个控件放在头像后面
            std::unique_ptr<DuiButton> check = MakeCheck(L"", base);
            ctrls.m_check = check.get();
            line->AddChild(std::unique_ptr<DuiControl>(check.release()), Hint().Fixed(kMemberCheckWidth));

            //头像：正方形、在行内垂直居中（只给宽时高会被拉满整行，画成长条）
            std::unique_ptr<balloonwjui::DuiAvatar> avatar = m_avatars.MakeAvatar(rows[r].m_userId, kMemberAvatarPx);
            avatar->SetCtrlId(base + kMemberAvatarColumn);
            line->AddChild(std::unique_ptr<DuiControl>(avatar.release()),
                           Hint().Fixed(kMemberAvatarPx, kMemberAvatarPx).AlignC(DuiLayout::AlignCenter));
            line->AddChild(std::unique_ptr<DuiControl>(new DuiLabel()), Hint().Fixed(kMemberAvatarGap));

            //名字：点击等于点勾选框；不满足条件的成员红字并带后缀
            std::wstring name = Wide(rows[r].m_name);
            if (rows[r].m_invalid)
                name += TR(kInvalidSuffix);
            std::unique_ptr<balloonmain::CClickableLabel> label(new balloonmain::CClickableLabel());
            label->SetText(name.c_str());
            label->SetTextColor(rows[r].m_invalid ? kErrorColor : kTextColor);
            label->SetTextAlign(DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
            label->SetCtrlId(base + kMemberNameColumn);
            line->AddChild(std::unique_ptr<DuiControl>(label.release()), Hint().Weight(1));
            for (size_t c = 0; c < kShareMemberPermColumnCount; ++c)
            {
                ctrls.m_perms[c] = NULL;
                if (fileShare)
                    continue;
                std::unique_ptr<DuiButton> perm = MakeCheck(L"", base + 1 + static_cast<UINT>(c));
                ctrls.m_perms[c] = perm.get();
                line->AddChild(std::unique_ptr<DuiControl>(perm.release()), Hint().Fixed(m_permColumnWidth));
            }
            m_rowCtrls.push_back(ctrls);
            list->AddChild(std::unique_ptr<DuiControl>(line.release()), Hint().Fixed(kMemberRowHeight));
        }
        m_scroll->SetContentHeight(kMemberRowHeight * static_cast<int>(rows.size()));
    }
    m_scroll->SetContent(std::unique_ptr<DuiControl>(list.release()));
    m_scroll->ForceLayout(m_scroll->GetRect());
    SyncCheckStates();
    m_scroll->Invalidate();
}

void CShareCreateDialog::SyncCheckStates()
{
    const std::vector<ShareMemberRow>& rows = m_table.Rows();
    bool allChecked = !rows.empty();
    bool anyEditable = false;
    for (size_t r = 0; r < rows.size() && r < m_rowCtrls.size(); ++r)
    {
        const MemberRowCtrls& ctrls = m_rowCtrls[r];
        ctrls.m_check->SetCheck(rows[r].m_checked, false);
        //固定成员（单聊的对方）不可取消
        ctrls.m_check->SetEnabled(!rows[r].m_fixed);
        anyEditable = anyEditable || !rows[r].m_fixed;
        allChecked = allChecked && rows[r].m_checked;
        for (size_t c = 0; c < kShareMemberPermColumnCount; ++c)
        {
            if (ctrls.m_perms[c] == NULL)
                continue;
            ctrls.m_perms[c]->SetCheck((rows[r].m_perms & kShareMemberPermColumns[c]) != 0, false);
            //未勾选为成员的人不能设权限
            ctrls.m_perms[c]->SetEnabled(rows[r].m_checked);
        }
    }
    if (m_allCheck != NULL)
    {
        m_allCheck->SetCheck(allChecked, false);
        //全是固定成员（单聊）时全选没有意义
        m_allCheck->SetEnabled(anyEditable);
    }
    for (size_t c = 0; c < kShareMemberPermColumnCount; ++c)
    {
        if (m_columnChecks[c] != NULL)
            m_columnChecks[c]->SetCheck(m_table.ColumnState(kShareMemberPermColumns[c]) == share_column_all, false);
    }
}

void CShareCreateDialog::OnMemberCheckChanged(UINT ctrlId, bool checked)
{
    const std::vector<ShareMemberRow>& rows = m_table.Rows();
    if (ctrlId == kIdAll)
    {
        //全选或全不选；固定成员不受影响（SetChecked 里不允许取消固定成员）
        for (size_t r = 0; r < rows.size(); ++r)
            m_table.SetChecked(rows[r].m_userId, checked);
    }
    else if (ctrlId >= kIdColumnBase && ctrlId < kIdColumnBase + kShareMemberPermColumnCount)
    {
        m_table.SetColumn(kShareMemberPermColumns[ctrlId - kIdColumnBase], checked);
    }
    else
    {
        const size_t r = (ctrlId - kIdMemberBase) / kIdsPerRow;
        const size_t c = (ctrlId - kIdMemberBase) % kIdsPerRow;
        if (r >= rows.size())
            return;
        const int64_t userId = rows[r].m_userId;
        if (c == 0)
            m_table.SetChecked(userId, checked);
        else if (c <= kShareMemberPermColumnCount)
            m_table.SetPerm(userId, kShareMemberPermColumns[c - 1], checked);
    }
    SyncCheckStates();
}

void CShareCreateDialog::OnMemberNameClicked(size_t row)
{
    const std::vector<ShareMemberRow>& rows = m_table.Rows();
    if (row >= rows.size())
        return;
    //固定成员（单聊的对方）不可取消，SetChecked 里会保持勾选
    m_table.SetChecked(rows[row].m_userId, !rows[row].m_checked);
    SyncCheckStates();
}

void CShareCreateDialog::SetOpenConversationOnSuccess(bool open)
{
    m_openConversationOnSuccess = open;
}

void CShareCreateDialog::RefreshAvatars()
{
    if (!IsWindow())
        return;
    RefreshMemberRows();
}

void CShareCreateDialog::OnPickDisk()
{
    const std::vector<ShareDriveInfo> drives = ShareCreateModel::ListLocalDrives();
    if (drives.empty() || m_pickDisk == NULL)
        return;
    DuiMenu menu;
    for (size_t i = 0; i < drives.size(); ++i)
    {
        std::wstring text = ShareCreateModel::DisplayName(drives[i].m_root, share_kind_disk, drives[i].m_label);
        if (drives[i].m_removable)
            text += TR(kRemovableSuffix);
        menu.AppendItem(kMenuDriveBase + static_cast<UINT>(i), text.c_str());
    }
    //菜单从按钮左下角弹出；靠近屏幕边缘时 DuiMenu 自己翻转（UI 约定第 3 条）
    const RECT& rc = m_pickDisk->GetRect();
    POINT pt = { rc.left, rc.bottom };
    ClientToScreen(&pt);
    const UINT chosen = menu.TrackPopup(pt.x, pt.y, m_hWnd);
    if (chosen < kMenuDriveBase || chosen - kMenuDriveBase >= drives.size())
        return;
    const ShareDriveInfo& drive = drives[chosen - kMenuDriveBase];
    SetShareObject(drive.m_root, share_kind_disk, drive.m_label);
}

void CShareCreateDialog::OnPickFolderOrFile(bool folder)
{
    std::vector<std::wstring> picked;
    if (!SharePickItems(m_hWnd, folder, false, false, folder ? TR(kPickFolderTitle) : TR(kPickFileTitle), picked))
        return;
    SetShareObject(picked[0], folder ? share_kind_folder : share_kind_file, std::wstring());
}

void CShareCreateDialog::SetStatus(const std::wstring& text, bool isError)
{
    if (m_status == NULL)
        return;
    m_status->SetText(text.c_str());
    m_status->SetTextColor(isError ? kErrorColor : kWeakTextColor);
}

std::wstring CShareCreateDialog::ResultText(int code)
{
    if (code == kShareManageNoResponse)
        return TR(kNoResponseText);
    return ShareCreateModel::CreateErrorText(code);
}

} // namespace fileshare
