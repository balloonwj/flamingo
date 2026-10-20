/**
 *  帖子群窗口 CThreadWindow 的实现，见 ThreadWindow.h。
 *  balloonwj@qq.com   2026-10-07
 */
#include "stdafx.h"
#include "ThreadWindow.h"

#include <algorithm>   // std::find：留言里 @ 的人去重
#include <memory>

#include "../Core/ThreadRules.h"
#include "../Core/ThreadText.h"
#include "../Core/ThreadReactions.h"
#include "../../Source/Common/Log/IULog.h"
#include "../../Source/Common/I18n/I18n.h"   // TR / TRF / TR_NOOP：界面文字的多语言查找
#include "../../../third-party/balloonui/balloonui/Controls/Layout/DuiLayout.h"
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiAvatar.h"
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiButton.h"
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiLabel.h"
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiSeparator.h"
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiToast.h"
#include "../../../third-party/balloonui/balloonui/Controls/Feedback/DuiToolTip.h"   // DuiToolTipMgr：互动名单的提示
#include "ThreadWidgets.h"   // kNotifyPillEnter / kNotifyPillLeave
#include "ThreadCardMetrics.h"
#include "ThreadComposer.h"
#include "ThreadDetailPane.h"
#include "ThreadFeedList.h"
#include "ThreadUiText.h"

using balloonwjui::DuiAvatar;
using balloonwjui::DuiButton;
using balloonwjui::DuiControl;
using balloonwjui::DuiHBox;
using balloonwjui::DuiLabel;
using balloonwjui::DuiLayout;
using balloonwjui::DuiSeparator;
using balloonwjui::DuiVBox;

namespace threadgroup {

namespace {

//---- 窗口尺寸（DuiFrameWindow 的 ResizeClient 设的是整窗尺寸，标题栏也在其内，UI 约定第 7 条；样稿第 0 期） ----
const int kWindowWidth = 1040;      //默认整窗宽，单位：像素
const int kWindowHeight = 700;      //默认整窗高（含标题栏）
const int kMinWindowWidth = 860;    //最小整窗宽
const int kMinWindowHeight = 560;   //最小整窗高
const int kTitleBarHeight = 32;     //标题栏高，与客户端其余窗口一致

//---- 头部 ----
const int kHeaderHeight = 58;       //头部高
const int kHeaderPadX = 16;         //头部左右内边距
const int kGroupAvatarPx = 36;      //群头像边长
const int kGroupAvatarRadius = 8;   //群头像圆角
const int kGroupAvatarGap = 10;     //群头像与群名之间的距离
const int kHeaderNamePt = 11;       //群名字号（磅）
const int kHeaderNameH = 22;        //群名一行的高
const int kHeaderSubH = 18;         //成员数一行的高
const int kTagHeight = 18;          //「帖子群」标签高
const int kTagPadding = 7;          //「帖子群」标签左右内边距
const int kTagGap = 8;              //群名与「帖子群」标签之间的距离
const int kHeaderNameMaxWidth = 520;//群名最多占的宽度，更长时省略
const int kHeaderButtonH = 30;      //头部右侧「成员」「群资料」「群管理」按钮高
const int kCommentAtLineHeight = 20;//留言框里「@」所在那一行的高（成员浮层弹在它上方），单位：像素
const int kMemberPanelWidth = 300;  //群成员面板的宽（docs/帖子群方案.md 14.3），单位：像素
const int kHeaderButtonGap = 6;     //两个按钮之间的距离

//---- 左栏 ----
const int kLeftWidth = 380;         //左栏宽
const int kToolbarHeight = 50;      //左栏顶部那一行的高
const int kToolbarPadX = 12;        //左栏顶部那一行的左右内边距
const int kToolbarButtonH = 30;     //「发帖」与排序按钮高
const int kToolbarGap = 8;          //左栏顶部那一行里各按钮之间的距离
const int kMuteBarHeight = 32;      //「不能发言」说明行的高
const int kMuteBarMargin = 10;      //说明行左右与下方的外边距
const int kMuteBarRadius = 6;       //说明行圆角
const int kMuteBarPadX = 10;        //说明行文字左右内边距
const int kCardAvatarPx = 22;       //卡片作者头像边长（读照片时的位图边长）
const int kCardFacePx = 16;         //卡片互动标签里的表情图标边长
const int kDetailAvatarPx = 34;     //详情作者头像边长
const int kDetailImagePx = 132;     //详情图片网格里每张图的边长（与 ThreadDetailPane 一致）
const int kAddendumImagePx = 72;    //补充里每张图的边长（与 ThreadDetailPane 一致）
const int kCommentAvatarPx = 28;    //留言人头像边长（与 ThreadDetailPane 一致）
const int kToastTop = 66;           //浮层提示离窗口内容顶边的距离（头部下方一点）
const int kToastMaxWidth = 520;     //浮层提示最宽
const UINT kToastMs = 2500;         //普通提示显示多久，单位：毫秒
const UINT kErrorToastMs = 3500;    //出错提示显示多久
const COLORREF kToastInfoBg = RGB(0x3A, 0x3A, 0x3A);    //普通提示底色
const COLORREF kToastErrorBg = RGB(0xD9, 0x53, 0x4F);   //出错提示底色
const COLORREF kToastText = RGB(0xFF, 0xFF, 0xFF);      //提示文字
const int kCaptionMaxChars = 20;    //追加补充窗口标题里帖子标题或正文开头最多保留的字数
const wchar_t* const kEllipsis = L"…";  //截断后补在末尾的省略号
const int kDetailTitleScaleNum = 4; //详情标题字体比正文大的倍数：分子
const int kDetailTitleScaleDen = 3; //详情标题字体比正文大的倍数：分母

//---- 按钮宽度：文字宽度加两侧内边距，不小于最小宽度 ----
const int kButtonPadding = 14;      //按钮文字两侧内边距
const int kMinButtonWidth = 56;     //按钮最小宽度

//---- 颜色（样稿第 0 期，取自聊天窗） ----
const COLORREF kHeaderBg = RGB(0xFF, 0xFF, 0xFF);     //头部与右栏底色
const COLORREF kLeftBg = RGB(0xFB, 0xFA, 0xF7);       //左栏底色（暖白，与聊天区一致）
const COLORREF kLineColor = RGB(0xEC, 0xEB, 0xE8);    //分隔线
const COLORREF kTextColor = RGB(0x1A, 0x1A, 0x1A);    //正文
const COLORREF kFaintText = RGB(0x9A, 0x9A, 0x9A);    //弱色文字
const COLORREF kTagBg = RGB(0xFF, 0xE6, 0xD6);        //「帖子群」标签底色
const COLORREF kTagText = RGB(0xFF, 0x7A, 0x45);      //「帖子群」标签文字
const COLORREF kMuteBg = RGB(0xFF, 0xF6, 0xE5);       //「不能发言」说明行底色
const COLORREF kMuteText = RGB(0x8A, 0x5A, 0x10);     //「不能发言」说明行文字

//群头像（群名首字）的底色：按群号取一个，同一个群总是同一种颜色
const COLORREF kGroupAvatarColors[] =
{
    RGB(0x5B, 0x8D, 0xEF),   //蓝
    RGB(0xFF, 0x7A, 0x45),   //橙
    RGB(0x3B, 0xAA, 0x7E),   //绿
    RGB(0x9C, 0x6A, 0xDE),   //紫
    RGB(0xE8, 0x83, 0x3A),   //琥珀
    RGB(0x2F, 0xA4, 0xB8),   //青
};

//---- 控件 id（只在本窗口内使用；派发通知时连同 ctrlId 一起判，UI 约定第 4 条） ----
const UINT kIdMembers = 7101;       //头部「成员」
const UINT kIdGroupInfo = 7102;     //头部「群资料」（2026-10-08 之前是「群设置」，打开的同样是群资料窗口）
const UINT kIdPublish = 7103;       //左栏「发帖」
const UINT kIdSortActivity = 7104;  //左栏「最新动态」
const UINT kIdSortPublish = 7105;   //左栏「最新发布」
const UINT kIdGroupManage = 7106;   //头部「群管理」（只有群主与管理员有，docs/帖子群方案.md §15.1 第 3 条）

//---- 界面文字（文件作用域的字符串只做标记，使用处再调 TR） ----
const wchar_t* const kThreadTag = TR_NOOP(L"帖子群");
const wchar_t* const kMembersText = TR_NOOP(L"成员");
const wchar_t* const kGroupInfoText = TR_NOOP(L"群资料");
const wchar_t* const kGroupManageText = TR_NOOP(L"群管理");
const wchar_t* const kPublishText = TR_NOOP(L"＋ 发帖");
const wchar_t* const kSortActivityText = TR_NOOP(L"最新动态");
const wchar_t* const kSortPublishText = TR_NOOP(L"最新发布");
const wchar_t* const kMemberCountPattern = TR_NOOP(L"{0} 人");
const wchar_t* const kMutedText = TR_NOOP(L"本群目前只有群主和管理员可以发帖和留言");
const wchar_t* const kFeedLoadingText = TR_NOOP(L"正在加载帖子…");
const wchar_t* const kFeedEmptyText = TR_NOOP(L"还没有帖子，点「发帖」发第一条");
const wchar_t* const kDetailHintText = TR_NOOP(L"在左侧选择一条帖子查看详情");
const wchar_t* const kGroupFallbackPattern = TR_NOOP(L"帖子群({0})");
const wchar_t* const kFeedFooterLoading = TR_NOOP(L"正在加载…");
const wchar_t* const kFeedFooterMore = TR_NOOP(L"滚动到底部加载更多");
const wchar_t* const kFeedFooterEnd = TR_NOOP(L"没有更多了");
const wchar_t* const kUnknownFace = TR_NOOP(L"[表情]");
const wchar_t* const kCannotCommentText = TR_NOOP(L"你目前不能在本群留言");
const wchar_t* const kConfirmDeletePost = TR_NOOP(L"确定删除这条帖子吗？帖子下的补充、留言与回复会一起删除。");
const wchar_t* const kConfirmDeleteAddendum = TR_NOOP(L"确定删除这段补充吗？");
const wchar_t* const kConfirmDeleteComment = TR_NOOP(L"确定删除这条留言吗？");
const wchar_t* const kPinnedToast = TR_NOOP(L"已置顶");
const wchar_t* const kUnpinnedToast = TR_NOOP(L"已取消置顶");
const wchar_t* const kDeletedToast = TR_NOOP(L"已删除");
const wchar_t* const kPublishedToast = TR_NOOP(L"已发布");
const wchar_t* const kSavedToast = TR_NOOP(L"已保存修改");
const wchar_t* const kAddendumToast = TR_NOOP(L"已追加补充");
const wchar_t* const kComposeBusy = TR_NOOP(L"请先完成正在编辑的内容");
const wchar_t* const kLikeName = TR_NOOP(L"赞");
const wchar_t* const kNameSeparator = TR_NOOP(L"、");
const wchar_t* const kReactorsLine = TR_NOOP(L"{0}：{1}");
const wchar_t* const kReactorsLineMore = TR_NOOP(L"{0}：{1} 等 {2} 人");

//一段文字的宽度，单位：像素。font 为文字所用的字体，为 NULL 时按设备上下文的默认字体测量
int MeasureTextWidth(HFONT font, const std::wstring& text)
{
    HDC hdc = ::GetDC(NULL);
    HGDIOBJ oldFont = (font != NULL) ? ::SelectObject(hdc, font) : NULL;
    SIZE size = { 0, 0 };
    ::GetTextExtentPoint32W(hdc, text.c_str(), static_cast<int>(text.size()), &size);
    if (oldFont != NULL)
        ::SelectObject(hdc, oldFont);
    ::ReleaseDC(NULL, hdc);
    return size.cx;
}

//正好放下文字的按钮宽度：文字宽度加两侧内边距，不小于最小宽度
int FitButtonWidth(HFONT font, const std::wstring& text)
{
    const int width = MeasureTextWidth(font, text) + 2 * kButtonPadding;
    return (width > kMinButtonWidth) ? width : kMinButtonWidth;
}

//建一个按钮
std::unique_ptr<DuiButton> MakeButton(const wchar_t* text, UINT id, DuiButton::Variant variant)
{
    std::unique_ptr<DuiButton> button(new DuiButton());
    button->SetText(text);
    button->SetVariant(variant);
    button->SetCtrlId(id);
    button->SetTabStop(false);
    return button;
}

//建一个文字标签
std::unique_ptr<DuiLabel> MakeLabel(const std::wstring& text, COLORREF color, DWORD align)
{
    std::unique_ptr<DuiLabel> label(new DuiLabel());
    label->SetText(text.c_str());
    label->SetTextColor(color);
    label->SetTextAlign(align);
    return label;
}

//建一条分隔线
std::unique_ptr<DuiSeparator> MakeLine(DuiSeparator::Orientation orientation)
{
    std::unique_ptr<DuiSeparator> line(new DuiSeparator());
    line->SetOrientation(orientation);
    line->SetColor(kLineColor);
    return line;
}

//群名的首字（群头像上画的字）；群名为空时为「群」
std::wstring FirstGlyph(const std::wstring& name)
{
    if (name.empty())
        return std::wstring(TR(L"群"));
    //代理对（4 字节字符）整体取出，不拆开
    const bool surrogate = name.size() >= 2 && name[0] >= 0xD800 && name[0] <= 0xDBFF;
    return name.substr(0, surrogate ? 2 : 1);
}

} // namespace

CThreadWindow::CThreadWindow(int64_t groupId, CThreadSession* session, IThreadWindowOwner* owner)
    : m_groupId(groupId)
    , m_session(session)
    , m_owner(owner)
    , m_closingByHost(false)
    , m_watching(false)
    , m_canSpeak(true)
    , m_bodyFont(NULL)
    , m_titleFont(NULL)
    , m_detailTitleFont(NULL)
    , m_myRole(thread_role_member)
    , m_atAllScope(thread_at_all_owner_admin)
    , m_loadMorePosted(false)
    , m_refreshPosted(false)
    , m_headerRow(NULL)
    , m_headerNameRow(NULL)
    , m_headerTag(NULL)
    , m_headerAvatar(NULL)
    , m_headerName(NULL)
    , m_headerSub(NULL)
    , m_btnMembers(NULL)
    , m_btnGroupInfo(NULL)
    , m_btnGroupManage(NULL)
    , m_btnPublish(NULL)
    , m_btnSortActivity(NULL)
    , m_btnSortPublish(NULL)
    , m_leftToolbar(NULL)
    , m_leftColumn(NULL)
    , m_muteBar(NULL)
    , m_muteText(NULL)
    , m_feedHint(NULL)
    , m_feedList(NULL)
    , m_detailHint(NULL)
    , m_rightColumn(NULL)
    , m_detailPane(NULL)
    , m_composer(NULL)
    , m_composerLine(NULL)
    , m_toast(NULL)
    , m_commentSending(false)
    , m_pickTargetType(0)
    , m_pickTargetId(0)
    , m_hoverChip(NULL)
    , m_compose(groupId, session, owner)
    , m_membersPending(false)
    , m_commentAtPos(-1)
    , m_memberPanelShown(false)
{
    ::SetRectEmpty(&m_lastClickRect);
    m_facePicker.SetPickCallback(&CThreadWindow::FacePickedThunk, this);
    m_commentPicker.SetPickCallback(&CThreadWindow::CommentMemberPickedThunk, this);
}

CThreadWindow::~CThreadWindow()
{
    Close();
}

//======================== IThreadWindow ========================

bool CThreadWindow::Open(int64_t postId)
{
    if (!IsWindow())
    {
        //独立的顶层窗口，不设所有者。控件通知发回本窗口（不带 WS_CHILD 的宿主都是如此，开发约定文档 UI 约定第 11 条）。
        //WS_CLIPCHILDREN：右侧的群成员面板是 exe 建的子窗口，本窗口重画时不能盖住它
        if (Create(NULL, CWindow::rcDefault, L"", WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, 0) == NULL)
        {
            LOG_ERROR("thread window: Create failed, groupid=%lld, error=%lu.", static_cast<long long>(m_groupId),
                      ::GetLastError());
            return false;
        }
        SetTitleBarHeight(kTitleBarHeight);
        SetButtons(true, true, true);
        //程序图标：标题栏左侧的位图与任务栏 / Alt-Tab 图标（图标资源在 exe 里，见 TitleBarIcon.h）
        SetIcon(m_programIcons.TitleBitmap(balloonmain::kProgramTitleIconSize, balloonmain::kOpaqueTitleBarColor));
        m_programIcons.ApplyTaskIcons(m_hWnd);
        SetResizable(true);
        SetMinSize(kMinWindowWidth, kMinWindowHeight);
        BuildContent();
        ResizeClient(kWindowWidth, kWindowHeight);
        CenterOnWorkArea();
        RefreshGroupBrief();
        RefreshSortButtons();
        OnFeedChanged();
        //预先拉成员名单：点正文里的 @某人弹成员菜单时要知道对方的群角色、还在不在群里（docs/帖子群方案.md §15.1 第 4 条）
        EnsureMembersRequested();
        LOG_INFO("thread window created, groupid=%lld.", static_cast<long long>(m_groupId));
    }
    if (IsIconic())
        ShowWindow(SW_RESTORE);
    else
        ShowWindow(SW_SHOW);
    ::SetForegroundWindow(m_hWnd);
    if (postId > 0 && m_session != NULL)
        m_session->SelectPost(postId);
    NotifyWatchChanged();
    return true;
}

void CThreadWindow::Close()
{
    //互动名单的提示按控件指针登记，控件随窗口销毁之前先撤销；表情浮层与发帖窗口一并关掉
    ClearChipHover();
    m_compose.Close();
    if (m_facePicker.IsWindow())
        m_facePicker.DestroyWindow();
    if (m_commentPicker.IsWindow())
        m_commentPicker.DestroyWindow();
    if (IsWindow())
    {
        m_closingByHost = true;
        DestroyWindow();
        m_closingByHost = false;
        LOG_INFO("thread window destroyed by host, groupid=%lld.", static_cast<long long>(m_groupId));
    }
    //控件都已随窗口销毁，再释放它们借用的字体与位图
    DestroyFonts();
    m_bitmaps.Clear();
    m_loadMorePosted = false;
    m_refreshPosted = false;
    //图片的本机路径留着（下次打开不用再下载）；没完成的请求作废，结果到了也不再处理
    m_imageRequests.clear();
    m_watching = false;
    m_headerRow = NULL;
    m_headerNameRow = NULL;
    m_headerTag = NULL;
    m_headerAvatar = NULL;
    m_headerName = NULL;
    m_headerSub = NULL;
    m_btnMembers = NULL;
    m_btnGroupInfo = NULL;
    m_btnGroupManage = NULL;
    m_btnPublish = NULL;
    m_btnSortActivity = NULL;
    m_btnSortPublish = NULL;
    m_leftToolbar = NULL;
    m_leftColumn = NULL;
    m_muteBar = NULL;
    m_muteText = NULL;
    m_feedHint = NULL;
    m_feedList = NULL;
    m_detailHint = NULL;
    m_rightColumn = NULL;
    m_detailPane = NULL;
    m_composer = NULL;
    m_composerLine = NULL;
    m_toast = NULL;
    m_commentSending = false;
}

bool CThreadWindow::IsWatching() const
{
    if (!IsWindow() || IsIconic())
        return false;
    return ::GetForegroundWindow() == m_hWnd;
}

IThreadSessionView* CThreadWindow::View()
{
    return this;
}

void CThreadWindow::OnGroupChanged()
{
    RefreshGroupBrief();
    //成员可能变了：发帖窗口与留言框缓存的名单作废并重新拉取（点 @某人弹成员菜单要用最新的角色）
    m_compose.InvalidateMembers();
    m_members.Clear();
    if (IsWindow())
        EnsureMembersRequested();
}

void CThreadWindow::OnGroupMembers(const std::vector<ThreadMember>& members)
{
    //成员名单给发帖窗口的「@ 成员」与留言框的 @ 用
    LOG_INFO("thread window got %u group member(s), groupid=%lld.", static_cast<unsigned>(members.size()),
             static_cast<long long>(m_groupId));
    m_compose.OnGroupMembers(members);
    m_membersPending = false;
    m_members.Set(members);
    //留言框的成员浮层开着时换上名单
    if (m_commentPicker.IsWindow() && m_commentPicker.IsWindowVisible())
        m_commentPicker.SetMembers(CommentMemberEntries(), true);
}

void CThreadWindow::OpenCommentMemberPicker(int atPos, const POINT& caretScreen)
{
    m_commentAtPos = atPos;
    EnsureMembersRequested();
    m_commentPicker.SetMembers(CommentMemberEntries(), m_members.IsLoaded());
    //锚点是「@」所在的那一行：浮层弹在它上方（留言框在窗口底部），放不下时由浮层翻到下方
    RECT anchor;
    anchor.left = caretScreen.x;
    anchor.right = caretScreen.x + 1;
    anchor.bottom = caretScreen.y;
    anchor.top = caretScreen.y - kCommentAtLineHeight;
    m_commentPicker.OpenAt(anchor);
}

void CThreadWindow::CommentMemberPickedThunk(void* user, int64_t userId)
{
    CThreadWindow* self = static_cast<CThreadWindow*>(user);
    if (self == NULL || !self->IsWindow())
        return;
    //用户 ID 是 64 位，32 位程序的 LPARAM 放不下：放在堆上，由消息处理函数 delete
    std::unique_ptr<int64_t> payload(new int64_t(userId));
    if (self->PostMessage(kMsgThreadWindowDeferred, thread_defer_comment_mention,
                          reinterpret_cast<LPARAM>(payload.get())))
        payload.release();
}

void CThreadWindow::EnsureMembersRequested()
{
    IThreadGroupHost* host = (m_owner != NULL) ? m_owner->Host() : NULL;
    if (m_members.IsLoaded() || m_membersPending || host == NULL)
        return;
    m_membersPending = true;
    host->RequestGroupMembers(m_groupId);
}

void CThreadWindow::AtClickThunk(void* user, int64_t userId, POINT screenPt)
{
    CThreadWindow* self = static_cast<CThreadWindow*>(user);
    if (self == NULL || !self->IsWindow() || userId <= 0)
        return;
    //负载放在堆上，由消息处理函数 delete；投递失败时由这里释放
    std::unique_ptr<ThreadMemberMenuRequest> request(new ThreadMemberMenuRequest());
    request->m_userId = userId;
    request->m_screen = screenPt;
    if (self->PostMessage(kMsgThreadWindowDeferred, thread_defer_member_menu, reinterpret_cast<LPARAM>(request.get())))
        request.release();
}

std::vector<MemberPickEntry> CThreadWindow::CommentMemberEntries() const
{
    IThreadGroupHost* host = (m_owner != NULL) ? m_owner->Host() : NULL;
    const int64_t myId = (host != NULL) ? host->GetMyUserId() : 0;
    //有权限 @所有人 时列表最上面是「所有人」（docs/帖子群方案.md §15.1 第 1 条）
    const std::vector<ThreadMember> order =
        MentionPickList(m_members.PickOrder(myId), CanMentionAll(m_myRole, m_atAllScope));
    std::vector<MemberPickEntry> entries;
    for (size_t i = 0; i < order.size(); ++i)
    {
        MemberPickEntry entry;
        entry.m_userId = order[i].m_userId;
        entry.m_name = order[i].m_name;
        if (entry.m_name.empty())
            entry.m_name = Utf8ToWide(IdToString(order[i].m_userId));
        entries.push_back(entry);
    }
    return entries;
}

void CThreadWindow::OnImageUploaded(uint32_t requestId, bool ok, const ThreadImage& image, const std::wstring& localPath)
{
    //图片上传只有发帖窗口用：交给它；它已关闭时结果作废
    if (!m_compose.OnImageUploaded(requestId, ok, image, localPath))
        LOG_INFO("thread window image upload result ignored, request=%u, ok=%d.", static_cast<unsigned>(requestId), ok ? 1 : 0);
}

void CThreadWindow::OnImageReady(uint32_t requestId, bool ok, const std::wstring& localPath)
{
    //编辑帖子时原有图片的缩略图：交给发帖窗口
    if (m_compose.OnImageReady(requestId, ok, localPath))
        return;
    std::map<uint32_t, std::string>::iterator it = m_imageRequests.find(requestId);
    if (it == m_imageRequests.end())
        return;
    const std::string fileId = it->second;
    m_imageRequests.erase(it);
    //失败的记成空路径，不再重试（卡片一直画占位底色）
    m_imagePaths[fileId] = ok ? localPath : std::wstring();
    if (!ok)
        LOG_WARNING("thread image download failed, request=%u, fileid=%s.", static_cast<unsigned>(requestId), fileId.c_str());
    //可能是在 ThumbBitmap 请求下载时同步回报的（正在建卡片），重建延后到这次消息处理完
    PostFeedRefresh();
}

void CThreadWindow::OnAvatarsUpdated()
{
    //照片头像下载到本机了：重建卡片与详情，头像重新向宿主取（位图缓存按路径区分，新照片会读进来）
    RebuildFeed();
    RebuildDetail();
}

//======================== IThreadSessionView ========================

void CThreadWindow::OnFeedChanged()
{
    RebuildFeed();
}

void CThreadWindow::OnDetailChanged()
{
    //选中的帖子可能变了（单击卡片、或从卡片消息点进来由模块定位）：左栏同步选中描边，右栏重建详情
    if (m_feedList != NULL && m_session != NULL)
        m_feedList->SetSelected(m_session->Detail().PostId());
    RebuildDetail();
}

void CThreadWindow::OnActionDone(int cmd, int code, int64_t id)
{
    LOG_INFO("thread window action done, cmd=%d, code=%d, id=%lld.", cmd, code, static_cast<long long>(id));
    //发帖窗口发出的发帖、修改与补充：先交给它（失败时它在窗口里说明原因，成功时关闭）
    const bool fromCompose = m_compose.OnActionDone(cmd, code, id);
    switch (cmd)
    {
    //留言或回复：成功时清空留言框，失败时在留言框里说明原因、内容保留
    case thread_cmd_add_comment:
    {
        m_commentSending = false;
        if (m_composer == NULL)
            break;
        //被回复的那一条：新回复没有显示出来（所在留言还有没展开的回复）时改为滚动到它
        const int64_t replyTo = m_composer->ReplyTarget();
        if (code == thread_error_ok)
            m_composer->Clear();
        else
            m_composer->SetError(FormatActionError(code));
        UpdateComposer();
        //本人的留言或回复成功：详情已按新内容重建，滚动到新的那一条并使它完整可见（别人的推送不经这里，不滚动；
        //docs/帖子群方案.md §15.1 第 5 条）。留言框的高度可能刚变过，放在 UpdateComposer 之后按新的视野计算
        if (code == thread_error_ok && m_detailPane != NULL && id > 0)
        {
            const bool found = m_detailPane->RevealComment(id, replyTo);
            LOG_INFO("thread comment added, reveal commentid=%lld (fallback %lld), found=%d.", static_cast<long long>(id),
                     static_cast<long long>(replyTo), found ? 1 : 0);
        }
        break;
    }

    //置顶 / 取消置顶：成功时提示一下（按钮文字随详情重建而变）
    case thread_cmd_pin_post:
        if (code == thread_error_ok)
        {
            const bool pinned = (m_session != NULL) && m_session->Detail().Post().m_pinTime > 0;
            ShowToast(TR(pinned ? kPinnedToast : kUnpinnedToast), false);
        }
        else
        {
            ShowToast(FormatActionError(code), true);
        }
        break;

    //点赞与表情回应：界面已经立即更新过；失败时会话已恢复原状，这里说明原因
    case thread_cmd_react:
        if (code != thread_error_ok)
            ShowToast(FormatActionError(code), true);
        break;

    //删除帖子、补充、留言或回复
    case thread_cmd_delete_post:
    case thread_cmd_delete_addendum:
    case thread_cmd_delete_comment:
        ShowToast(code == thread_error_ok ? std::wstring(TR(kDeletedToast)) : FormatActionError(code), code != thread_error_ok);
        break;

    //发帖：成功时选中新帖、提示一下；发帖窗口已经关掉时失败原因由这里提示
    case thread_cmd_publish_post:
        if (code == thread_error_ok && fromCompose)
        {
            ShowToast(TR(kPublishedToast), false);
            if (m_session != NULL && id > 0)
                m_session->SelectPost(id);
        }
        else if (code != thread_error_ok && !fromCompose)
        {
            ShowToast(FormatActionError(code), true);
        }
        break;

    //修改帖子与追加补充：详情已按应答更新，这里只提示
    case thread_cmd_edit_post:
    case thread_cmd_add_addendum:
        if (code == thread_error_ok && fromCompose)
            ShowToast(TR(cmd == thread_cmd_edit_post ? kSavedToast : kAddendumToast), false);
        else if (code != thread_error_ok && !fromCompose)
            ShowToast(FormatActionError(code), true);
        break;

    //其余操作（拉列表、拉留言等）：只记日志
    default:
        break;
    }
}

void CThreadWindow::OnReactorsLoaded(int targetType, int64_t targetId, int reaction, const std::vector<int64_t>& users,
                                     int total)
{
    //鼠标已经离开、或停到了别的标签上：这份名单不用了
    if (m_hoverChip == NULL || m_hoverSlot.m_targetType != targetType || m_hoverSlot.m_targetId != targetId
        || m_hoverSlot.m_reaction != reaction || total <= 0)
        return;
    //「赞：张三、李四 等 8 人」：最多列 20 个名字，其余写「等 N 人」
    int shown = 0;
    int rest = 0;
    ReactorListSplit(static_cast<int>(users.size()), total, kReactorListLimit, shown, rest);
    std::wstring names;
    for (int i = 0; i < shown; ++i)
    {
        if (i > 0)
            names += TR(kNameSeparator);
        names += UserName(users[i]);
    }
    std::wstring label = TR(kLikeName);
    const int faceId = FaceOfReaction(reaction);
    if (faceId >= 0 && m_owner != NULL)
    {
        std::wstring faceName;
        if (m_owner->Faces().NameOf(faceId, faceName))
            label = faceName;
    }
    const std::wstring text = (rest > 0) ? TRF(kReactorsLineMore, label, names, rest) : TRF(kReactorsLine, label, names);
    POINT cursor = { 0, 0 };
    ::GetCursorPos(&cursor);
    //按名单文字登记并开始计时；同一标签的名单重复到达时，Retarget 先收起正在显示的旧文字再换成新文字
    balloonwjui::DuiToolTipMgr::Inst().Retarget(m_hoverChip, text.c_str(), cursor);
}

//======================== 测试用 ========================

std::wstring CThreadWindow::GetHeaderName() const
{
    return (m_headerName != NULL) ? std::wstring(m_headerName->GetText().GetString()) : std::wstring();
}

std::wstring CThreadWindow::GetHeaderSub() const
{
    return (m_headerSub != NULL) ? std::wstring(m_headerSub->GetText().GetString()) : std::wstring();
}

bool CThreadWindow::IsPublishEnabled() const
{
    return m_btnPublish != NULL && m_btnPublish->IsEnabled();
}

int CThreadWindow::GetSort() const
{
    return (m_session != NULL) ? m_session->Feed().Sort() : thread_sort_activity;
}

int CThreadWindow::GetCardCount() const
{
    return (m_feedList != NULL && m_feedList->IsVisible()) ? m_feedList->CardCount() : 0;
}

//======================== 消息 ========================

LRESULT CThreadWindow::OnCloseMsg(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& bHandled)
{
    bHandled = TRUE;
    DestroyWindow();
    return 0;
}

LRESULT CThreadWindow::OnDestroyMsg(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& bHandled)
{
    bHandled = FALSE;
    m_watching = false;
    //群成员面板是 exe 建在本窗口下的子窗口，会随本窗口一起销毁：先请宿主收起并登记面板已不在
    //（WM_DESTROY 先于子窗口的销毁送达；用户关闭与宿主关闭都经过这里）
    if (m_memberPanelShown)
    {
        m_memberPanelShown = false;
        IThreadGroupHost* host = (m_owner != NULL) ? m_owner->Host() : NULL;
        if (host != NULL)
            host->HideMemberPanel(m_groupId);
    }
    //用户关闭：通知模块撤销「正在查看」、稍后释放本对象；宿主关闭（Close）时模块已经知道，不再通知
    if (!m_closingByHost && m_owner != NULL)
    {
        LOG_INFO("thread window closed by user, groupid=%lld.", static_cast<long long>(m_groupId));
        m_owner->OnWindowClosed(m_groupId);
    }
    return 0;
}

LRESULT CThreadWindow::OnDuiNotify(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam, BOOL& bHandled)
{
    const balloonwjui::DuiNotify* pNotify = reinterpret_cast<const balloonwjui::DuiNotify*>(lParam);
    //留言框的文字变了：刷新字数（通知码与控件 id 一起判，UI 约定第 4 条）
    if (pNotify != NULL && pNotify->code == balloonwjui::DUIN_VALUECHANGED && pNotify->ctrlId == kIdComposerEdit)
    {
        if (m_composer != NULL && m_owner != NULL)
        {
            m_composer->UpdateCount(m_owner->Faces());
            //刚输入了一个用来 @ 人的「@」：在光标处弹出成员浮层（docs/帖子群方案.md 14.1 的 P6）
            int atPos = -1;
            POINT caret;
            if (m_composer->DetectMentionTrigger(atPos, caret))
                OpenCommentMemberPicker(atPos, caret);
        }
        return 0;
    }
    //鼠标进入 / 离开互动标签：拉名单显示提示 / 关掉提示（不重建任何控件，就地处理）
    if (pNotify != NULL && pNotify->ctrlId >= kIdCommentActionBase && pNotify->ctrlId <= kIdCommentActionLast)
    {
        if (pNotify->code == kNotifyPillEnter)
        {
            OnChipEnter(static_cast<balloonwjui::DuiControl*>(pNotify->pCtrl), pNotify->ctrlId);
            return 0;
        }
        if (pNotify->code == kNotifyPillLeave)
        {
            //只处理当前登记的那个标签离开；别的标签的离开通知（顺序上可能晚于新标签的进入）不能清掉新登记的提示
            if (pNotify->pCtrl == m_hoverChip)
                ClearChipHover();
            return 0;
        }
    }
    if (pNotify == NULL || pNotify->code != balloonwjui::DUIN_CLICK)
    {
        bHandled = FALSE;
        return 0;
    }
    //记下被点的控件在屏幕上的位置（「＋」据此定表情浮层的位置；控件指针只在这条通知期间有效）
    if (pNotify->pCtrl != NULL)
    {
        RECT rc = static_cast<balloonwjui::DuiControl*>(pNotify->pCtrl)->GetRect();
        ::MapWindowPoints(m_hWnd, NULL, reinterpret_cast<POINT*>(&rc), 2);
        m_lastClickRect = rc;
    }
    //留言框的「发送」：与按 Enter 一样延后发送
    if (pNotify->ctrlId == kIdComposerSend)
    {
        PostMessage(kMsgThreadWindowDeferred, thread_defer_send_comment, 0);
        return 0;
    }
    //单击了左栏的一张卡片：延后到这条通知处理完再选中（选中可能引起列表重建，卡片不能在自己的点击通知里被销毁，UI 约定第 13 条）
    if (pNotify->ctrlId == kIdPostCard)
    {
        PostMessage(kMsgThreadWindowDeferred, thread_defer_select_post, pNotify->extra);
        return 0;
    }
    //单击了详情里的一张图片：交给宿主的看图窗口
    if (pNotify->ctrlId >= kIdDetailImageBase && pNotify->ctrlId < kIdDetailImageBase + static_cast<UINT>(kMaxImages))
    {
        OpenDetailImage(static_cast<size_t>(pNotify->ctrlId - kIdDetailImageBase));
        return 0;
    }
    //留言区的按钮与「加载更多留言」：动作会重建详情，延后到这条通知处理完再执行（UI 约定第 13 条）
    if (pNotify->ctrlId == kIdLoadMoreComments
        || (pNotify->ctrlId >= kIdCommentActionBase && pNotify->ctrlId <= kIdCommentActionLast))
    {
        PostMessage(kMsgThreadWindowDeferred, thread_defer_detail_click, static_cast<LPARAM>(pNotify->ctrlId));
        return 0;
    }
    //详情里的「编辑」「置顶」「删除」「追加补充」、补充的「删除」与留言框的「取消」：会重建详情或改变留言框，延后执行
    if (pNotify->ctrlId == kIdDetailEdit || pNotify->ctrlId == kIdDetailPin || pNotify->ctrlId == kIdDetailDelete
        || pNotify->ctrlId == kIdDetailAddAddendum || pNotify->ctrlId == kIdComposerCancelReply
        || (pNotify->ctrlId >= kIdAddendumDeleteBase && pNotify->ctrlId < kIdAddendumDeleteBase + static_cast<UINT>(kMaxAddenda)))
    {
        PostMessage(kMsgThreadWindowDeferred, thread_defer_detail_button, static_cast<LPARAM>(pNotify->ctrlId));
        return 0;
    }
    IThreadGroupHost* host = (m_owner != NULL) ? m_owner->Host() : NULL;
    switch (pNotify->ctrlId)
    {
    //头部「成员」：在窗口右侧滑出群成员面板，再点一次收起（docs/帖子群方案.md 14.3）
    case kIdMembers:
        ToggleMemberPanel();
        return 0;

    //头部「群资料」：打开现有的群资料窗口（所有成员都有）
    case kIdGroupInfo:
        if (host != NULL)
            host->OpenGroupInfo(m_hWnd, m_groupId);
        return 0;

    //头部「群管理」：打开现有的群管理窗口（只有群主与管理员看得到这个按钮；宿主还会再按角色判定一次）
    case kIdGroupManage:
        if (host != NULL)
            host->OpenGroupManage(m_hWnd, m_groupId);
        return 0;

    //左栏「发帖」：打开发帖窗口（建窗口不会改动本窗口的控件树，不必延后）
    case kIdPublish:
        OpenCompose(compose_mode_new_post);
        return 0;

    //左栏排序「最新动态」
    case kIdSortActivity:
        if (m_session != NULL && m_session->Feed().Sort() != thread_sort_activity)
        {
            m_session->SetSort(thread_sort_activity);
            RefreshSortButtons();
        }
        return 0;

    //左栏排序「最新发布」
    case kIdSortPublish:
        if (m_session != NULL && m_session->Feed().Sort() != thread_sort_publish)
        {
            m_session->SetSort(thread_sort_publish);
            RefreshSortButtons();
        }
        return 0;

    //其余控件的点击：交给基类
    default:
        bHandled = FALSE;
        return 0;
    }
}

LRESULT CThreadWindow::OnActivateMsg(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& bHandled)
{
    bHandled = FALSE;
    NotifyWatchChanged();
    return 0;
}

LRESULT CThreadWindow::OnSizeMsg(UINT /*uMsg*/, WPARAM wParam, LPARAM /*lParam*/, BOOL& bHandled)
{
    bHandled = FALSE;
    //只有最小化与从最小化还原会改变「正在看」；普通的改变大小不用管
    if (wParam == SIZE_MINIMIZED || wParam == SIZE_RESTORED || wParam == SIZE_MAXIMIZED)
        NotifyWatchChanged();
    //右栏宽度可能变了：等基类按新尺寸排完版，再让详情按新宽度重排（富文本的高度随宽度变化）
    if (wParam != SIZE_MINIMIZED && IsWindow())
        PostMessage(kMsgThreadWindowDeferred, thread_defer_relayout, 0);
    //群成员面板贴着右边：跟着窗口的新尺寸走
    if (wParam != SIZE_MINIMIZED && m_memberPanelShown && m_owner != NULL && m_owner->Host() != NULL)
    {
        const RECT rc = MemberPanelRect();
        m_owner->Host()->MoveMemberPanel(m_groupId, &rc);
    }
    return 0;
}

void CThreadWindow::OnMemberPanelClosed()
{
    m_memberPanelShown = false;
    LOG_INFO("thread member panel closed by user, groupid=%lld.", static_cast<long long>(m_groupId));
}

void CThreadWindow::ToggleMemberPanel()
{
    IThreadGroupHost* host = (m_owner != NULL) ? m_owner->Host() : NULL;
    if (host == NULL || !IsWindow())
        return;
    if (m_memberPanelShown)
    {
        host->HideMemberPanel(m_groupId);
        m_memberPanelShown = false;
        return;
    }
    const RECT rc = MemberPanelRect();
    m_memberPanelShown = host->ShowMemberPanel(m_hWnd, m_groupId, &rc);
    LOG_INFO("thread member panel toggled, groupid=%lld, shown=%d.", static_cast<long long>(m_groupId),
             m_memberPanelShown ? 1 : 0);
}

RECT CThreadWindow::MemberPanelRect() const
{
    RECT client;
    ::SetRectEmpty(&client);
    if (IsWindow())
        GetClientRect(&client);
    //面板从头部下方开始：标题栏与头部那一行（群名、「成员」「群资料」「群管理」按钮）不被盖住
    const ThreadPanelRect panel = ComputeMemberPanelRect(client.right - client.left, client.bottom - client.top,
                                                         kTitleBarHeight + kHeaderHeight, kMemberPanelWidth);
    RECT rc;
    rc.left = panel.m_left;
    rc.top = panel.m_top;
    rc.right = panel.m_right;
    rc.bottom = panel.m_bottom;
    return rc;
}

LRESULT CThreadWindow::OnDeferredMsg(UINT /*uMsg*/, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
{
    bHandled = TRUE;
    if (m_session == NULL || !IsWindow())
        return 0;
    switch (wParam)
    {
    //单击了左栏的一张卡片：选中那条帖子，右栏显示它的详情
    case thread_defer_select_post:
    {
        const int64_t postId = (m_feedList != NULL) ? m_feedList->PostIdAt(static_cast<int>(lParam)) : 0;
        if (postId <= 0)
            break;
        m_feedList->SetSelected(postId);
        if (m_session->Detail().PostId() != postId)
            m_session->SelectPost(postId);
        break;
    }

    //左栏滚到了接近底部：还有下一页且没在加载时拉下一页
    case thread_defer_load_more:
        m_loadMorePosted = false;
        if (m_session->Feed().HasMore() && !m_session->Feed().IsLoading())
            m_session->LoadMore();
        break;

    //有缩略图下载完成：重建一次左栏与右栏
    case thread_defer_refresh_feed:
        m_refreshPosted = false;
        RebuildFeed();
        RebuildDetail();
        break;

    //窗口大小变了：右栏的详情按新宽度重新排版
    case thread_defer_relayout:
        if (m_detailPane != NULL && m_detailPane->IsVisible())
            m_detailPane->Relayout();
        break;

    //单击了右栏留言区的按钮
    case thread_defer_detail_click:
        HandleDetailClick(static_cast<UINT>(lParam));
        break;

    //留言框按了 Enter 或点了「发送」
    case thread_defer_send_comment:
        SendComment();
        break;

    //详情里的「编辑」「置顶」「删除」、补充的「删除」或留言框的「取消」
    case thread_defer_detail_button:
        HandleDetailButton(static_cast<UINT>(lParam));
        break;

    //表情浮层里选中了一个表情：关掉浮层，用这个表情回应目标（已经加过这种表情时为取消）
    case thread_defer_face_picked:
    {
        if (m_facePicker.IsWindow())
            m_facePicker.Hide();
        const int reaction = ReactionOfFace(static_cast<int>(lParam));
        if (IsValidReaction(reaction) && m_pickTargetId > 0)
            ToggleReaction(m_pickTargetType, m_pickTargetId, reaction);
        break;
    }

    //留言框的成员浮层里选中了一个人：把那个「@」换成「@名字 」（名字取成员名单里的群名片或昵称）
    case thread_defer_comment_mention:
    {
        std::unique_ptr<int64_t> userId(reinterpret_cast<int64_t*>(lParam));
        if (m_commentPicker.IsWindow())
            m_commentPicker.Hide();
        if (!userId || m_composer == NULL)
            break;
        //「@所有人」插入固定的「所有人」三个字；其余取成员名单里的名字
        std::wstring name = (*userId == kThreadAtAllUserId) ? AtAllName() : m_members.NameOf(*userId);
        if (name.empty())
            name = Utf8ToWide(IdToString(*userId));
        m_composer->InsertMention(m_commentAtPos, *userId, name);
        m_commentAtPos = -1;
        if (m_owner != NULL)
            m_composer->UpdateCount(m_owner->Faces());
        break;
    }

    //点了详情里的 @某人：经宿主弹出与群成员面板右键菜单相同的成员菜单（docs/帖子群方案.md §15.1 第 4 条）
    case thread_defer_member_menu:
    {
        std::unique_ptr<ThreadMemberMenuRequest> request(reinterpret_cast<ThreadMemberMenuRequest*>(lParam));
        IThreadGroupHost* host = (m_owner != NULL) ? m_owner->Host() : NULL;
        if (!request || host == NULL)
            break;
        //对方的群角色：名单里没有这个人说明已退群；名单还没到时按未知，同时去拉，下次就准了
        const int role = m_members.MenuRoleOf(request->m_userId);
        if (role == kMemberRoleUnknown)
            EnsureMembersRequested();
        LOG_INFO("thread mention clicked, groupid=%lld, userid=%lld, role=%d.", static_cast<long long>(m_groupId),
                 static_cast<long long>(request->m_userId), role);
        host->ShowMemberMenu(m_hWnd, m_groupId, request->m_userId, role, request->m_screen.x, request->m_screen.y);
        break;
    }

    //不认识的动作：忽略
    default:
        break;
    }
    return 0;
}

void CThreadWindow::PostFeedRefresh()
{
    if (m_refreshPosted || !IsWindow())
        return;
    m_refreshPosted = true;
    PostMessage(kMsgThreadWindowDeferred, thread_defer_refresh_feed, 0);
}

HBITMAP CThreadWindow::ThumbBitmap(const ThreadImage& image, int sizePx)
{
    if (image.m_fileId.empty() || m_owner == NULL)
        return NULL;
    std::map<std::string, std::wstring>::const_iterator done = m_imagePaths.find(image.m_fileId);
    if (done != m_imagePaths.end())
        return done->second.empty() ? NULL : m_bitmaps.Get(done->second, sizePx, 0);
    //已经在下载：等结果
    for (std::map<uint32_t, std::string>::const_iterator it = m_imageRequests.begin(); it != m_imageRequests.end(); ++it)
    {
        if (it->second == image.m_fileId)
            return NULL;
    }
    //先登记请求号再开始下载：宿主对已缓存的图片可能同步回报
    const uint32_t requestId = m_owner->BeginImageRequest(m_groupId);
    m_imageRequests[requestId] = image.m_fileId;
    if (!m_owner->StartDownload(requestId, image.m_fileId, image.m_md5))
    {
        m_imageRequests.erase(requestId);
        m_imagePaths[image.m_fileId] = std::wstring();
    }
    return NULL;
}

void CThreadWindow::NearBottomThunk(void* user)
{
    CThreadWindow* self = static_cast<CThreadWindow*>(user);
    if (self == NULL || self->m_session == NULL || self->m_loadMorePosted || !self->IsWindow())
        return;
    if (!self->m_session->Feed().HasMore() || self->m_session->Feed().IsLoading())
        return;
    self->m_loadMorePosted = true;
    self->PostMessage(kMsgThreadWindowDeferred, thread_defer_load_more, 0);
}

//======================== 控件树 ========================

void CThreadWindow::BuildContent()
{
    typedef DuiLayout::Hint Hint;
    std::unique_ptr<DuiVBox> root(new DuiVBox());
    root->SetBgColor(kHeaderBg);
    root->AddChild(std::unique_ptr<DuiControl>(BuildHeader().release()), Hint().Fixed(kHeaderHeight));
    root->AddChild(std::unique_ptr<DuiControl>(MakeLine(DuiSeparator::Horizontal).release()), Hint().Fixed(1));

    std::unique_ptr<DuiHBox> body(new DuiHBox());
    body->AddChild(std::unique_ptr<DuiControl>(BuildLeftColumn().release()), Hint().Fixed(kLeftWidth));
    body->AddChild(std::unique_ptr<DuiControl>(MakeLine(DuiSeparator::Vertical).release()), Hint().Fixed(1));
    body->AddChild(std::unique_ptr<DuiControl>(BuildRightColumn().release()), Hint().Weight(1));
    root->AddChild(std::unique_ptr<DuiControl>(body.release()), Hint().Weight(1));

    //浮层提示：挂在根容器的最后（最后一个画在最上层）；布局给 0 高度、不占位置，DuiToast 自己按根容器的矩形定位
    std::unique_ptr<balloonwjui::DuiToast> toast(new balloonwjui::DuiToast());
    toast->SetTextColor(kToastText);
    toast->SetTopOffset(kToastTop);
    toast->SetMaxWidth(kToastMaxWidth);
    m_toast = toast.get();
    root->AddChild(std::unique_ptr<DuiControl>(toast.release()), Hint().Fixed(0));

    SetClientContent(std::unique_ptr<DuiControl>(root.release()));
    CreateFonts();
    UpdateWidths();
}

std::unique_ptr<DuiHBox> CThreadWindow::BuildHeader()
{
    typedef DuiLayout::Hint Hint;
    std::unique_ptr<DuiHBox> header(new DuiHBox());
    header->SetPadding(kHeaderPadX, 0, kHeaderPadX, 0);

    std::unique_ptr<DuiAvatar> avatar(new DuiAvatar());
    avatar->SetShape(DuiAvatar::ShapeRoundRect);
    avatar->SetCornerRadius(kGroupAvatarRadius);
    const size_t colorCount = sizeof(kGroupAvatarColors) / sizeof(kGroupAvatarColors[0]);
    avatar->SetFallbackBgColor(kGroupAvatarColors[static_cast<uint64_t>(m_groupId) % colorCount]);
    m_headerAvatar = avatar.get();
    header->AddChild(std::unique_ptr<DuiControl>(avatar.release()),
                     Hint().Fixed(kGroupAvatarPx, kGroupAvatarPx).Margin(0, 0, kGroupAvatarGap, 0).AlignC(DuiLayout::AlignCenter));

    //群名一行（群名 + 「帖子群」标签）与成员数一行，上下排列、整体垂直居中
    std::unique_ptr<DuiVBox> texts(new DuiVBox());
    std::unique_ptr<DuiHBox> nameRow(new DuiHBox());
    std::unique_ptr<DuiLabel> name = MakeLabel(std::wstring(), kTextColor, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    name->SetTextPointSize(kHeaderNamePt, true);
    m_headerName = name.get();
    //群名按文字宽度定宽（UpdateWidths），「帖子群」标签紧跟其后，剩余宽度留给后面的空白
    nameRow->AddChild(std::unique_ptr<DuiControl>(name.release()), Hint().Fixed(kMinButtonWidth));
    std::unique_ptr<DuiVBox> tag(new DuiVBox());
    tag->SetBgColor(kTagBg);
    tag->SetCornerRadius(kTagHeight / 2);
    tag->AddChild(std::unique_ptr<DuiControl>(MakeLabel(TR(kThreadTag), kTagText, DT_CENTER | DT_VCENTER | DT_SINGLELINE).release()),
                  Hint().Weight(1));
    //宽度先给最小按钮宽度占位，控件挂到窗口上之后由 UpdateWidths 按实际字体定
    m_headerTag = tag.get();
    nameRow->AddChild(std::unique_ptr<DuiControl>(tag.release()),
                      Hint().Fixed(kMinButtonWidth, kTagHeight).Margin(kTagGap, 0, 0, 0).AlignC(DuiLayout::AlignCenter));
    nameRow->AddChild(std::unique_ptr<DuiControl>(new DuiLabel()), Hint().Weight(1));
    m_headerNameRow = nameRow.get();
    texts->AddChild(std::unique_ptr<DuiControl>(nameRow.release()), Hint().Fixed(kHeaderNameH));
    std::unique_ptr<DuiLabel> sub = MakeLabel(std::wstring(), kFaintText, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    m_headerSub = sub.get();
    texts->AddChild(std::unique_ptr<DuiControl>(sub.release()), Hint().Fixed(kHeaderSubH));
    header->AddChild(std::unique_ptr<DuiControl>(texts.release()),
                     Hint().Weight(1).Fixed(-1, kHeaderNameH + kHeaderSubH).AlignC(DuiLayout::AlignCenter));

    std::unique_ptr<DuiButton> members = MakeButton(TR(kMembersText), kIdMembers, DuiButton::Variant::Default);
    m_btnMembers = members.get();
    header->AddChild(std::unique_ptr<DuiControl>(members.release()),
                     Hint().Fixed(kMinButtonWidth, kHeaderButtonH).Margin(kHeaderButtonGap, 0, 0, 0).AlignC(DuiLayout::AlignCenter));
    std::unique_ptr<DuiButton> info = MakeButton(TR(kGroupInfoText), kIdGroupInfo, DuiButton::Variant::Default);
    m_btnGroupInfo = info.get();
    header->AddChild(std::unique_ptr<DuiControl>(info.release()),
                     Hint().Fixed(kMinButtonWidth, kHeaderButtonH).Margin(kHeaderButtonGap, 0, 0, 0).AlignC(DuiLayout::AlignCenter));
    //「群管理」排在最右；普通成员看不到，角色变化后由 ApplyGroupManageButton 显隐
    std::unique_ptr<DuiButton> manage = MakeButton(TR(kGroupManageText), kIdGroupManage, DuiButton::Variant::Default);
    manage->SetVisible(CanManageGroup(m_myRole));
    m_btnGroupManage = manage.get();
    header->AddChild(std::unique_ptr<DuiControl>(manage.release()),
                     Hint().Fixed(kMinButtonWidth, kHeaderButtonH).Margin(kHeaderButtonGap, 0, 0, 0).AlignC(DuiLayout::AlignCenter));
    m_headerRow = header.get();
    return header;
}

std::unique_ptr<DuiVBox> CThreadWindow::BuildLeftColumn()
{
    typedef DuiLayout::Hint Hint;
    std::unique_ptr<DuiVBox> left(new DuiVBox());
    left->SetBgColor(kLeftBg);

    std::unique_ptr<DuiHBox> toolbar(new DuiHBox());
    toolbar->SetPadding(kToolbarPadX, 0, kToolbarPadX, 0);
    toolbar->SetGap(kToolbarGap);
    std::unique_ptr<DuiButton> publish = MakeButton(TR(kPublishText), kIdPublish, DuiButton::Variant::Primary);
    m_btnPublish = publish.get();
    toolbar->AddChild(std::unique_ptr<DuiControl>(publish.release()),
                      Hint().Fixed(kMinButtonWidth, kToolbarButtonH).AlignC(DuiLayout::AlignCenter));
    toolbar->AddChild(std::unique_ptr<DuiControl>(new DuiLabel()), Hint().Weight(1));
    std::unique_ptr<DuiButton> sortActivity = MakeButton(TR(kSortActivityText), kIdSortActivity, DuiButton::Variant::Default);
    m_btnSortActivity = sortActivity.get();
    toolbar->AddChild(std::unique_ptr<DuiControl>(sortActivity.release()),
                      Hint().Fixed(kMinButtonWidth, kToolbarButtonH).AlignC(DuiLayout::AlignCenter));
    std::unique_ptr<DuiButton> sortPublish = MakeButton(TR(kSortPublishText), kIdSortPublish, DuiButton::Variant::Default);
    m_btnSortPublish = sortPublish.get();
    toolbar->AddChild(std::unique_ptr<DuiControl>(sortPublish.release()),
                      Hint().Fixed(kMinButtonWidth, kToolbarButtonH).AlignC(DuiLayout::AlignCenter));
    m_leftToolbar = toolbar.get();
    left->AddChild(std::unique_ptr<DuiControl>(toolbar.release()), Hint().Fixed(kToolbarHeight));

    //「不能发言」的说明行：平时隐藏，RefreshGroupBrief 按群概况显隐
    std::unique_ptr<DuiVBox> muteBar(new DuiVBox());
    muteBar->SetBgColor(kMuteBg);
    muteBar->SetCornerRadius(kMuteBarRadius);
    muteBar->SetPadding(kMuteBarPadX, 0, kMuteBarPadX, 0);
    std::unique_ptr<DuiLabel> muteText = MakeLabel(TR(kMutedText), kMuteText, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    m_muteText = muteText.get();
    muteBar->AddChild(std::unique_ptr<DuiControl>(muteText.release()), Hint().Weight(1));
    muteBar->SetVisible(false);
    m_muteBar = muteBar.get();
    left->AddChild(std::unique_ptr<DuiControl>(muteBar.release()),
                   Hint().Fixed(kMuteBarHeight).Margin(kMuteBarMargin, 0, kMuteBarMargin, kMuteBarMargin));

    //帖子列表区：正在加载或还没有帖子时显示提示文字，有帖子时显示卡片列表（两者只显示一个，RebuildFeed 切换）
    std::unique_ptr<DuiLabel> feedHint = MakeLabel(std::wstring(), kFaintText, DT_CENTER | DT_TOP | DT_SINGLELINE);
    m_feedHint = feedHint.get();
    left->AddChild(std::unique_ptr<DuiControl>(feedHint.release()), Hint().Weight(1).Margin(0, kToolbarGap, 0, 0));
    std::unique_ptr<CThreadFeedList> feedList(new CThreadFeedList());
    feedList->SetNearBottomCallback(&CThreadWindow::NearBottomThunk, this);
    feedList->SetVisible(false);
    m_feedList = feedList.get();
    left->AddChild(std::unique_ptr<DuiControl>(feedList.release()), Hint().Weight(1));
    m_leftColumn = left.get();
    return left;
}

std::unique_ptr<DuiVBox> CThreadWindow::BuildRightColumn()
{
    typedef DuiLayout::Hint Hint;
    std::unique_ptr<DuiVBox> right(new DuiVBox());
    right->SetBgColor(kHeaderBg);
    //没有选中帖子时显示提示文字，选中后显示详情（两者只显示一个，RebuildDetail 切换）
    std::unique_ptr<DuiLabel> hint = MakeLabel(TR(kDetailHintText), kFaintText, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    m_detailHint = hint.get();
    right->AddChild(std::unique_ptr<DuiControl>(hint.release()), Hint().Weight(1));
    std::unique_ptr<CThreadDetailPane> pane(new CThreadDetailPane());
    pane->SetVisible(false);
    m_detailPane = pane.get();
    right->AddChild(std::unique_ptr<DuiControl>(pane.release()), Hint().Weight(1));
    //底部的留言框与它上方的分隔线：选中帖子时才显示；里面的控件在取到字体之后建（CreateFonts）
    std::unique_ptr<DuiSeparator> line = MakeLine(DuiSeparator::Horizontal);
    line->SetVisible(false);
    m_composerLine = line.get();
    right->AddChild(std::unique_ptr<DuiControl>(line.release()), Hint().Fixed(1));
    std::unique_ptr<CThreadComposer> composer(new CThreadComposer());
    composer->SetVisible(false);
    m_composer = composer.get();
    right->AddChild(std::unique_ptr<DuiControl>(composer.release()), Hint().Fixed(0));
    m_rightColumn = right.get();
    return right;
}

//======================== 刷新 ========================

void CThreadWindow::RefreshGroupBrief()
{
    IThreadGroupHost* host = (m_owner != NULL) ? m_owner->Host() : NULL;
    ThreadGroupBrief brief;
    memset(&brief, 0, sizeof(brief));
    brief.m_cbSize = sizeof(brief);
    const bool known = (host != NULL) && host->GetGroupBrief(m_groupId, &brief);
    if (known)
    {
        brief.m_name[kThreadNameChars - 1] = L'\0';
        m_groupName = brief.m_name;
    }
    const std::wstring shownName = m_groupName.empty() ? TRF(kGroupFallbackPattern, static_cast<long long>(m_groupId))
                                                       : m_groupName;
    m_canSpeak = known && CanSpeak(brief.m_myRole, brief.m_mutedForMe);
    const int oldRole = m_myRole;
    m_myRole = known ? brief.m_myRole : thread_role_none;
    m_atAllScope = known ? brief.m_atAllScope : thread_at_all_owner_admin;
    if (!IsWindow())
        return;
    //角色变了（被设为管理员、被取消）：详情里「删除」「置顶」与头部「群管理」的显隐跟着变
    if (oldRole != m_myRole)
    {
        RebuildDetail();
        ApplyGroupManageButton();
    }
    SetTitle(shownName.c_str());
    if (m_headerName != NULL)
        m_headerName->SetText(shownName.c_str());
    //群名可能变了，按新文字重定群名的宽度
    UpdateWidths();
    if (m_headerSub != NULL)
        m_headerSub->SetText(known ? TRF(kMemberCountPattern, static_cast<unsigned>(brief.m_memberCount)).c_str() : L"");
    if (m_headerAvatar != NULL)
        m_headerAvatar->SetName(FirstGlyph(m_groupName).c_str());
    if (m_btnPublish != NULL)
        m_btnPublish->SetEnabled(m_canSpeak);
    if (m_composer != NULL)
        m_composer->SetCanSpeak(m_canSpeak, TR(kCannotCommentText));
    if (m_muteBar != NULL && m_leftColumn != NULL && m_muteBar->IsVisible() == m_canSpeak)
    {
        //说明行显隐变了：SetVisible 不会重排，要对它的父容器按当前矩形重排（UI 约定第 5 条）
        m_muteBar->SetVisible(!m_canSpeak);
        m_leftColumn->ForceLayout(m_leftColumn->GetRect());
    }
}

void CThreadWindow::UpdateWidths()
{
    typedef DuiLayout::Hint Hint;
    if (m_btnPublish == NULL)
        return;
    //SetHint 改完会让所在的行立即重新排版
    HFONT font = m_btnPublish->GetDefaultFont();
    if (m_leftToolbar != NULL && m_btnSortActivity != NULL && m_btnSortPublish != NULL)
    {
        DuiButton* buttons[] = { m_btnPublish, m_btnSortActivity, m_btnSortPublish };
        for (size_t i = 0; i < sizeof(buttons) / sizeof(buttons[0]); ++i)
        {
            const int width = FitButtonWidth(font, std::wstring(buttons[i]->GetText().GetString()));
            m_leftToolbar->SetHint(buttons[i], Hint().Fixed(width, kToolbarButtonH).AlignC(DuiLayout::AlignCenter));
        }
    }
    if (m_headerRow != NULL && m_btnMembers != NULL && m_btnGroupInfo != NULL && m_btnGroupManage != NULL)
    {
        DuiButton* buttons[] = { m_btnMembers, m_btnGroupInfo, m_btnGroupManage };
        for (size_t i = 0; i < sizeof(buttons) / sizeof(buttons[0]); ++i)
        {
            const int width = FitButtonWidth(font, std::wstring(buttons[i]->GetText().GetString()));
            m_headerRow->SetHint(buttons[i], Hint().Fixed(width, kHeaderButtonH).Margin(kHeaderButtonGap, 0, 0, 0)
                                                 .AlignC(DuiLayout::AlignCenter));
        }
    }
    if (m_headerNameRow != NULL && m_headerName != NULL)
    {
        int width = MeasureTextWidth(m_headerName->GetFont(), std::wstring(m_headerName->GetText().GetString()));
        if (width > kHeaderNameMaxWidth)
            width = kHeaderNameMaxWidth;
        m_headerNameRow->SetHint(m_headerName, Hint().Fixed(width));
    }
    if (m_headerNameRow != NULL && m_headerTag != NULL)
    {
        const int width = MeasureTextWidth(font, TR(kThreadTag)) + 2 * kTagPadding;
        m_headerNameRow->SetHint(m_headerTag, Hint().Fixed(width, kTagHeight).Margin(kTagGap, 0, 0, 0)
                                                  .AlignC(DuiLayout::AlignCenter));
    }
}

void CThreadWindow::ApplyGroupManageButton()
{
    if (m_btnGroupManage == NULL || m_headerRow == NULL)
        return;
    const bool show = CanManageGroup(m_myRole);
    if (m_btnGroupManage->IsVisible() == show)
        return;
    m_btnGroupManage->SetVisible(show);
    //SetVisible 只改标志位、不重新排版（UI 约定第 5 条）：对按钮所在的头部那一行强制重排，其余按钮随之移位
    m_headerRow->ForceLayout(m_headerRow->GetRect());
    m_headerRow->Invalidate();
    LOG_INFO("thread window group manage button %s, groupid=%lld, role=%d.", show ? "shown" : "hidden",
             static_cast<long long>(m_groupId), m_myRole);
}

void CThreadWindow::RefreshSortButtons()
{
    if (m_btnSortActivity == NULL || m_btnSortPublish == NULL)
        return;
    const int sort = GetSort();
    m_btnSortActivity->SetVariant(sort == thread_sort_activity ? DuiButton::Variant::Primary : DuiButton::Variant::Default);
    m_btnSortPublish->SetVariant(sort == thread_sort_publish ? DuiButton::Variant::Primary : DuiButton::Variant::Default);
}

void CThreadWindow::NotifyWatchChanged()
{
    const bool watching = IsWatching();
    if (watching == m_watching)
        return;
    m_watching = watching;
    if (m_owner != NULL)
        m_owner->OnWindowWatchChanged(m_groupId, watching);
}

void CThreadWindow::CenterOnWorkArea()
{
    POINT cursor = { 0, 0 };
    ::GetCursorPos(&cursor);
    HMONITOR monitor = ::MonitorFromPoint(cursor, MONITOR_DEFAULTTOPRIMARY);
    MONITORINFO info;
    info.cbSize = sizeof(info);
    if (!::GetMonitorInfoW(monitor, &info))
        return;
    RECT rc;
    GetWindowRect(&rc);
    const RECT& work = info.rcWork;
    const int width = rc.right - rc.left;
    const int height = rc.bottom - rc.top;
    int x = work.left + ((work.right - work.left) - width) / 2;
    int y = work.top + ((work.bottom - work.top) - height) / 2;
    //工作区比窗口小时贴住左上角，不让标题栏跑到屏幕外
    if (x < work.left)
        x = work.left;
    if (y < work.top)
        y = work.top;
    SetWindowPos(NULL, x, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
}

//======================== 左栏帖子列表 ========================

void CThreadWindow::RebuildFeed()
{
    if (m_feedHint == NULL || m_feedList == NULL || m_session == NULL || m_leftColumn == NULL)
        return;
    const CThreadFeedModel& feed = m_session->Feed();
    const bool showList = feed.IsLoaded() && !feed.Posts().empty();
    if (!showList)
        m_feedHint->SetText(feed.IsLoaded() ? TR(kFeedEmptyText) : TR(kFeedLoadingText));
    if (m_feedList->IsVisible() != showList)
    {
        //提示文字与卡片列表互换：SetVisible 不会重排，对它们的父容器按当前矩形重排（UI 约定第 5 条）
        m_feedHint->SetVisible(!showList);
        m_feedList->SetVisible(showList);
        m_leftColumn->ForceLayout(m_leftColumn->GetRect());
    }
    if (!showList)
        return;

    //卡片内容区宽度按左栏的固定宽度算（列表隐藏时矩形是空的，不能按矩形算）
    const int innerWidth = CThreadFeedList::CardInnerWidth(kLeftWidth);
    std::vector<PostCardData> cards;
    HDC hdc = ::GetDC(NULL);
    const std::vector<ThreadPost>& posts = feed.Posts();
    for (size_t i = 0; i < posts.size(); ++i)
        cards.push_back(BuildCardData(hdc, posts[i], innerWidth));
    ::ReleaseDC(NULL, hdc);

    std::wstring footer;
    if (feed.IsLoading())
        footer = TR(kFeedFooterLoading);
    else if (feed.HasMore())
        footer = TR(kFeedFooterMore);
    else
        footer = TR(kFeedFooterEnd);
    m_feedList->Rebuild(cards, m_bodyFont, m_titleFont, innerWidth, m_session->Detail().PostId(), footer);
}

PostCardData CThreadWindow::BuildCardData(HDC hdc, const ThreadPost& post, int innerWidth)
{
    PostCardData data;
    data.m_postId = post.m_postId;
    data.m_pinned = (post.m_pinTime > 0);
    data.m_title = post.m_title;
    data.m_timeText = FormatPostTime(post.m_createTime);
    data.m_commentText = FormatCommentCount(post.m_commentCount);

    AuthorInfo(post.m_author, kCardAvatarPx, data.m_authorName, data.m_avatarGlyph, data.m_avatarColor, data.m_avatarBitmap);
    data.m_chips = BuildChips(post.m_reactions, false);

    //正文：表情编码换成「[表情名]」，再按卡片宽度截到三行；没有互动也没有留言时不要底部那一行
    const std::wstring display = (m_owner != NULL) ? WireToDisplay(post.m_text, m_owner->Faces(), TR(kUnknownFace)) : post.m_text;
    const bool hasFoot = !data.m_chips.empty() || !data.m_commentText.empty();
    data.m_metrics = MeasurePostCard(hdc, m_bodyFont, m_titleFont, innerWidth, post.m_title, display, post.m_imageCount, hasFoot);
    //缩略图：已下载的读成位图，还没下载的请求下载、先画占位底色
    for (int i = 0; i < data.m_metrics.m_thumbCount; ++i)
    {
        const bool known = (i < static_cast<int>(post.m_images.size()));
        data.m_thumbs.push_back(known ? ThumbBitmap(post.m_images[i], kCardThumbPx) : static_cast<HBITMAP>(NULL));
    }
    return data;
}

void CThreadWindow::AuthorInfo(int64_t userId, int avatarPx, std::wstring& name, std::wstring& glyph, COLORREF& color,
                               HBITMAP& bitmap)
{
    name.clear();
    glyph.clear();
    color = 0;
    bitmap = NULL;
    IThreadGroupHost* host = (m_owner != NULL) ? m_owner->Host() : NULL;
    if (host == NULL)
        return;
    wchar_t buf[kThreadNameChars] = { 0 };
    if (host->GetUserDisplayName(userId, m_groupId, buf, kThreadNameChars))
        name = buf;
    ThreadAvatarInfo avatar;
    memset(&avatar, 0, sizeof(avatar));
    avatar.m_cbSize = sizeof(avatar);
    if (!host->GetUserAvatar(userId, &avatar))
        return;
    avatar.m_glyph[kThreadGlyphChars - 1] = L'\0';
    avatar.m_imagePath[kThreadPathChars - 1] = L'\0';
    glyph = avatar.m_glyph;
    color = avatar.m_color;
    //照片头像在本机时读成位图，否则画字形
    if (avatar.m_imagePath[0] != L'\0')
        bitmap = m_bitmaps.Get(avatar.m_imagePath, avatarPx, 0);
}

std::vector<PostCardChip> CThreadWindow::BuildChips(const std::vector<ThreadReaction>& reactions, bool ensureLike)
{
    std::vector<PostCardChip> chips;
    std::vector<ThreadReaction> sorted = reactions;
    SortReactions(sorted);
    //详情里「赞」标签就是点赞按钮：没人点过时也放一个（排序后「赞」总在最前）
    if (ensureLike && (sorted.empty() || sorted[0].m_reaction != kReactionLike))
    {
        PostCardChip like;
        like.m_like = true;
        like.m_reaction = kReactionLike;
        chips.push_back(like);
    }
    for (size_t i = 0; i < sorted.size(); ++i)
    {
        PostCardChip chip;
        chip.m_like = (sorted[i].m_reaction == kReactionLike);
        chip.m_reaction = sorted[i].m_reaction;
        chip.m_count = sorted[i].m_count;
        chip.m_mine = sorted[i].m_mine;
        if (!chip.m_like)
        {
            const int faceId = FaceOfReaction(sorted[i].m_reaction);
            if (faceId < 0)
                continue;
            chip.m_icon = m_bitmaps.Get(FacePath(faceId), kCardFacePx, 0);
        }
        chips.push_back(chip);
    }
    return chips;
}

const std::vector<ThreadReaction>* CThreadWindow::ReactionsOf(int targetType, int64_t targetId)
{
    if (m_session == NULL)
        return NULL;
    const CThreadDetailModel& detail = m_session->Detail();
    if (targetType == thread_target_post)
        return (detail.PostId() == targetId) ? &detail.Post().m_reactions : NULL;
    const ThreadComment* comment = detail.FindComment(targetId);
    return (comment != NULL) ? &comment->m_reactions : NULL;
}

void CThreadWindow::ToggleReaction(int targetType, int64_t targetId, int reaction)
{
    const std::vector<ThreadReaction>* current = ReactionsOf(targetType, targetId);
    if (current == NULL || m_session == NULL)
        return;
    //互动会立即更新并重建详情，鼠标下的标签随之销毁：先撤销提示
    ClearChipHover();
    const bool on = !HasMyReaction(*current, reaction);
    if (!m_session->React(targetType, targetId, reaction, on))
        ShowToast(NotSentText(), true);
}

void CThreadWindow::FacePickedThunk(void* user, int faceId)
{
    CThreadWindow* self = static_cast<CThreadWindow*>(user);
    if (self != NULL && self->IsWindow())
        self->PostMessage(kMsgThreadWindowDeferred, thread_defer_face_picked, static_cast<LPARAM>(faceId));
}

void CThreadWindow::OnChipEnter(balloonwjui::DuiControl* chip, UINT ctrlId)
{
    ClearChipHover();
    DetailActionSlot slot;
    if (chip == NULL || m_detailPane == NULL || m_session == NULL || !m_detailPane->SlotAt(ctrlId, slot)
        || slot.m_action != detail_action_react)
        return;
    //没有人加过这种互动（只有「赞」会这样出现）时不拉名单
    const std::vector<ThreadReaction>* current = ReactionsOf(slot.m_targetType, slot.m_targetId);
    if (current == NULL || ReactionCount(*current, slot.m_reaction) <= 0)
        return;
    m_hoverChip = chip;
    m_hoverSlot = slot;
    m_session->GetReactors(slot.m_targetType, slot.m_targetId, slot.m_reaction);
}

void CThreadWindow::ClearChipHover()
{
    if (m_hoverChip == NULL)
        return;
    balloonwjui::DuiToolTipMgr& tips = balloonwjui::DuiToolTipMgr::Inst();
    tips.OnLeave(m_hoverChip);
    tips.Unregister(m_hoverChip);
    m_hoverChip = NULL;
    m_hoverSlot = DetailActionSlot();
}

//======================== 右栏帖子详情 ========================

void CThreadWindow::RebuildDetail()
{
    if (m_detailHint == NULL || m_detailPane == NULL || m_rightColumn == NULL || m_session == NULL)
        return;
    //详情里的互动标签要整体重建：鼠标下那个标签的提示登记先撤销（提示管理器按控件指针登记）
    ClearChipHover();
    const bool showDetail = (m_session->Detail().PostId() > 0);
    if (m_detailPane->IsVisible() != showDetail)
    {
        //提示文字与详情互换：SetVisible 不会重排，对它们的父容器按当前矩形重排（UI 约定第 5 条）
        m_detailHint->SetVisible(!showDetail);
        m_detailPane->SetVisible(showDetail);
        m_rightColumn->ForceLayout(m_rightColumn->GetRect());
    }
    //换了帖子：留言框退出回复状态（被回复的留言属于上一条帖子）
    if (m_composer != NULL && m_composer->ReplyTarget() > 0 && m_session->Detail().FindComment(m_composer->ReplyTarget()) == NULL)
        m_composer->ClearReplyTarget();
    UpdateComposer();
    if (!showDetail)
        return;
    DetailViewData data;
    BuildDetailData(data);
    m_detailPane->Rebuild(data);
}

void CThreadWindow::BuildDetailData(DetailViewData& data)
{
    const CThreadDetailModel& detail = m_session->Detail();
    const ThreadPost& post = detail.Post();
    data.m_postId = detail.PostId();
    data.m_deleted = detail.IsDeleted();
    if (data.m_deleted)
        return;
    const int64_t myId = (m_owner != NULL && m_owner->Host() != NULL) ? m_owner->Host()->GetMyUserId() : 0;
    AuthorInfo(post.m_author, kDetailAvatarPx, data.m_authorName, data.m_avatarGlyph, data.m_avatarColor, data.m_avatarBitmap);
    data.m_timeText = FormatPostTime(post.m_createTime);
    data.m_edited = (post.m_editTime > 0);
    data.m_canEdit = CanEditPost(post.m_author, myId);
    data.m_canPin = CanPin(m_myRole);
    data.m_pinned = (post.m_pinTime > 0);
    data.m_canDelete = CanDeleteContent(post.m_author, myId, m_myRole);
    data.m_title = post.m_title;
    data.m_wireText = post.m_text;
    data.m_at = post.m_at;
    data.m_myUserId = myId;
    //完整内容到达之前是列表里的摘要：正文被截过时也照样先显示，完整内容到了再整体重建
    for (size_t i = 0; i < post.m_images.size(); ++i)
        data.m_images.push_back(ThumbBitmap(post.m_images[i], kDetailImagePx));
    data.m_chips = BuildChips(post.m_reactions, true);
    for (size_t i = 0; i < post.m_addenda.size(); ++i)
    {
        const ThreadAddendum& add = post.m_addenda[i];
        DetailAddendumView view;
        view.m_addId = add.m_addId;
        view.m_timeText = FormatPostTime(add.m_createTime);
        view.m_wireText = add.m_text;
        view.m_at = add.m_at;
        view.m_canDelete = CanDeleteContent(add.m_author, myId, m_myRole);
        for (size_t k = 0; k < add.m_images.size(); ++k)
            view.m_images.push_back(ThumbBitmap(add.m_images[k], kAddendumImagePx));
        data.m_addenda.push_back(view);
    }
    data.m_canAddAddendum = detail.IsPostLoaded() && CanAddAddendum(post.m_author, myId, post.m_addendumCount);

    //留言区：留言按时间排列，回复挂在各自的留言下
    data.m_commentCount = post.m_commentCount;
    data.m_commentsLoaded = detail.IsCommentsLoaded();
    data.m_commentsLoading = detail.IsLoadingComments();
    data.m_hasMoreComments = detail.HasMoreComments();
    const std::vector<ThreadComment>& comments = detail.Comments();
    for (size_t i = 0; i < comments.size(); ++i)
    {
        const ThreadComment& c = comments[i];
        DetailCommentView view;
        view.m_commentId = c.m_commentId;
        view.m_deleted = c.m_deleted;
        if (!c.m_deleted)
        {
            AuthorInfo(c.m_author, kCommentAvatarPx, view.m_authorName, view.m_avatarGlyph, view.m_avatarColor,
                       view.m_avatarBitmap);
            view.m_authorIsOwner = (c.m_author == post.m_author);
            view.m_timeText = FormatPostTime(c.m_createTime);
            view.m_wireText = c.m_text;
            view.m_at = c.m_at;
            view.m_canDelete = CanDeleteContent(c.m_author, myId, m_myRole);
            view.m_chips = BuildChips(c.m_reactions, true);
        }
        for (size_t k = 0; k < c.m_replies.size(); ++k)
        {
            const ThreadComment& r = c.m_replies[k];
            DetailReplyView reply;
            reply.m_commentId = r.m_commentId;
            reply.m_authorName = UserName(r.m_author);
            reply.m_authorIsOwner = (r.m_author == post.m_author);
            reply.m_replyToName = UserName(r.m_replyTo);
            reply.m_timeText = FormatPostTime(r.m_createTime);
            reply.m_wireText = r.m_text;
            reply.m_at = r.m_at;
            reply.m_canDelete = CanDeleteContent(r.m_author, myId, m_myRole);
            reply.m_chips = BuildChips(r.m_reactions, true);
            view.m_replies.push_back(reply);
        }
        view.m_moreReplies = CThreadDetailModel::UnloadedReplies(c);
        data.m_comments.push_back(view);
    }
}

std::wstring CThreadWindow::UserName(int64_t userId)
{
    IThreadGroupHost* host = (m_owner != NULL) ? m_owner->Host() : NULL;
    wchar_t buf[kThreadNameChars] = { 0 };
    if (host == NULL || userId <= 0 || !host->GetUserDisplayName(userId, m_groupId, buf, kThreadNameChars))
        return std::wstring();
    return std::wstring(buf);
}

void CThreadWindow::HandleDetailClick(UINT ctrlId)
{
    if (m_session == NULL || m_detailPane == NULL)
        return;
    //「加载更多留言」
    if (ctrlId == kIdLoadMoreComments)
    {
        m_session->LoadMoreComments();
        return;
    }
    int64_t commentId = 0;
    switch (m_detailPane->ActionAt(ctrlId, commentId))
    {
    //「查看更多回复」：拉这条留言下更多的回复
    case detail_action_more_replies:
        m_session->LoadMoreReplies(commentId);
        break;

    //「回复」：留言框切到回复这条留言或回复
    case detail_action_reply:
    {
        const ThreadComment* target = m_session->Detail().FindComment(commentId);
        if (target == NULL || m_composer == NULL)
            break;
        m_composer->SetReplyTarget(commentId, UserName(target->m_author));
        UpdateComposer();
        break;
    }

    //「删除」：确认后删除这条留言或回复
    case detail_action_delete:
        if (ConfirmDelete(TR(kConfirmDeleteComment)) && !m_session->DeleteComment(commentId))
            ShowToast(NotSentText(), true);
        break;

    //互动标签：切换本人的这种互动
    case detail_action_react:
    {
        DetailActionSlot slot;
        if (m_detailPane->SlotAt(ctrlId, slot))
            ToggleReaction(slot.m_targetType, slot.m_targetId, slot.m_reaction);
        break;
    }

    //「＋」：在按钮旁打开表情浮层，选中后回应这个目标
    case detail_action_add_face:
    {
        DetailActionSlot slot;
        if (!m_detailPane->SlotAt(ctrlId, slot) || m_owner == NULL)
            break;
        m_pickTargetType = slot.m_targetType;
        m_pickTargetId = slot.m_targetId;
        m_facePicker.OpenAt(m_lastClickRect, m_owner->FaceEntries());
        break;
    }

    //不是留言区的按钮，或按钮已随详情重建失效：忽略
    default:
        break;
    }
}

void CThreadWindow::HandleDetailButton(UINT ctrlId)
{
    if (m_session == NULL)
        return;
    const CThreadDetailModel& detail = m_session->Detail();
    const int64_t postId = detail.PostId();
    //留言框「取消」：退出回复状态
    if (ctrlId == kIdComposerCancelReply)
    {
        if (m_composer != NULL)
            m_composer->ClearReplyTarget();
        UpdateComposer();
        return;
    }
    if (postId <= 0 || detail.IsDeleted())
        return;
    //补充的「删除」：按段序号找到补充
    if (ctrlId >= kIdAddendumDeleteBase && ctrlId < kIdAddendumDeleteBase + static_cast<UINT>(kMaxAddenda))
    {
        const size_t index = static_cast<size_t>(ctrlId - kIdAddendumDeleteBase);
        const std::vector<ThreadAddendum>& addenda = detail.Post().m_addenda;
        if (index < addenda.size() && ConfirmDelete(TR(kConfirmDeleteAddendum))
            && !m_session->DeleteAddendum(addenda[index].m_addId))
            ShowToast(NotSentText(), true);
        return;
    }
    switch (ctrlId)
    {
    //「置顶」或「取消置顶」
    case kIdDetailPin:
        if (!m_session->PinPost(postId, detail.Post().m_pinTime <= 0))
            ShowToast(NotSentText(), true);
        break;

    //「删除」帖子：确认后删除
    case kIdDetailDelete:
        if (ConfirmDelete(TR(kConfirmDeletePost)) && !m_session->DeletePost(postId))
            ShowToast(NotSentText(), true);
        break;

    //「编辑」：在发帖窗口里带入原帖
    case kIdDetailEdit:
        OpenCompose(compose_mode_edit_post);
        break;

    //「追加补充」
    case kIdDetailAddAddendum:
        OpenCompose(compose_mode_addendum);
        break;

    //其余按钮：忽略
    default:
        break;
    }
}

void CThreadWindow::SendComment()
{
    if (m_session == NULL || m_composer == NULL || m_owner == NULL || m_commentSending || !m_canSpeak)
        return;
    const int64_t postId = m_session->Detail().PostId();
    if (postId <= 0 || m_session->Detail().IsDeleted())
        return;
    //先在本地校验（与服务端口径一致），不合法时在留言框里说明原因，不发请求
    const std::wstring wire = m_composer->WireText(m_owner->Faces());
    //@ 的人与片段：按最终正文重扫插入过的候选，删改过的不算（docs/帖子群方案.md 14.4）
    const std::vector<ThreadAtSpan> at = RescanAtSpans(wire, m_composer->MentionCandidates());
    std::vector<int64_t> mentions;
    for (size_t i = 0; i < at.size(); ++i)
    {
        if (std::find(mentions.begin(), mentions.end(), at[i].m_userId) == mentions.end())
            mentions.push_back(at[i].m_userId);
    }
    const int check = CheckCompose(compose_comment, std::wstring(), wire, 0, static_cast<int>(mentions.size()));
    if (check != compose_ok)
    {
        m_composer->SetError(FormatComposeCheck(check, compose_comment));
        return;
    }
    m_composer->SetError(std::wstring());
    if (!m_session->AddComment(postId, m_composer->ReplyTarget(), wire, mentions, at))
    {
        m_composer->SetError(NotSentText());
        return;
    }
    m_commentSending = true;
}

bool CThreadWindow::ConfirmDelete(const std::wstring& text)
{
    const std::wstring title = m_groupName.empty() ? std::wstring(TR(kThreadTag)) : m_groupName;
    return ::MessageBoxW(m_hWnd, text.c_str(), title.c_str(), MB_OKCANCEL | MB_ICONQUESTION | MB_DEFBUTTON2) == IDOK;
}

void CThreadWindow::ShowToast(const std::wstring& text, bool isError)
{
    if (m_toast == NULL || text.empty())
        return;
    m_toast->SetBgColor(isError ? kToastErrorBg : kToastInfoBg);
    m_toast->SetDurationMs(isError ? kErrorToastMs : kToastMs);
    m_toast->Show(text.c_str());
}

void CThreadWindow::OpenCompose(int mode)
{
    if (m_session == NULL || !IsWindow())
        return;
    ComposeRequest request;
    request.m_mode = mode;
    request.m_groupName = m_groupName;
    if (mode != compose_mode_new_post)
    {
        //编辑与追加补充针对右栏当前的帖子；帖子已被删除时不打开
        const CThreadDetailModel& detail = m_session->Detail();
        if (detail.PostId() <= 0 || detail.IsDeleted())
            return;
        const ThreadPost& post = detail.Post();
        request.m_postId = post.m_postId;
        if (mode == compose_mode_edit_post)
        {
            request.m_post = &post;
        }
        else
        {
            request.m_addendumIndex = post.m_addendumCount + 1;
            //窗口标题里的帖子：有标题用标题，没有时用正文开头
            bool cut = false;
            const std::wstring source = post.m_title.empty()
                ? WireToDisplay(post.m_text, (m_owner != NULL) ? m_owner->Faces() : FaceTable(), TR(kUnknownFace))
                : post.m_title;
            request.m_postCaption = MakeDigest(source, kCaptionMaxChars, cut);
            if (cut)
                request.m_postCaption += kEllipsis;
        }
    }
    if (!m_compose.Open(m_hWnd, request))
        ShowToast(TR(kComposeBusy), false);
}

void CThreadWindow::UpdateComposer()
{
    typedef DuiLayout::Hint Hint;
    if (m_composer == NULL || m_rightColumn == NULL || m_session == NULL)
        return;
    const bool show = m_session->Detail().PostId() > 0 && !m_session->Detail().IsDeleted();
    //高度随「回复」提示行变化；显隐与高度变了都要对右栏重排（UI 约定第 5 条）
    m_rightColumn->SetHint(m_composer, Hint().Fixed(m_composer->PreferredHeight()));
    if (m_composer->IsVisible() != show)
        m_composer->SetVisible(show);
    if (m_composerLine != NULL && m_composerLine->IsVisible() != show)
        m_composerLine->SetVisible(show);
    m_rightColumn->ForceLayout(m_rightColumn->GetRect());
}

void CThreadWindow::OpenDetailImage(size_t index)
{
    IThreadGroupHost* host = (m_owner != NULL) ? m_owner->Host() : NULL;
    if (host == NULL || m_session == NULL)
        return;
    //只交已经下载到本机的几张；被点的那张还没下载好时什么也不做
    const std::vector<ThreadImage>& images = m_session->Detail().Post().m_images;
    std::vector<std::wstring> paths;
    uint32_t start = 0;
    bool clickedReady = false;
    for (size_t i = 0; i < images.size(); ++i)
    {
        std::map<std::string, std::wstring>::const_iterator it = m_imagePaths.find(images[i].m_fileId);
        if (it == m_imagePaths.end() || it->second.empty())
            continue;
        //被点的那张在已下载的这几张里排第几
        if (i == index)
        {
            start = static_cast<uint32_t>(paths.size());
            clickedReady = true;
        }
        paths.push_back(it->second);
    }
    if (!clickedReady)
        return;
    std::vector<const wchar_t*> raw;
    for (size_t i = 0; i < paths.size(); ++i)
        raw.push_back(paths[i].c_str());
    host->OpenImageViewer(m_hWnd, &raw[0], static_cast<uint32_t>(raw.size()), start);
}

std::wstring CThreadWindow::FacePath(int faceId)
{
    if (m_owner == NULL)
        return std::wstring();
    const std::vector<ThreadFaceEntry>& faces = m_owner->FaceEntries();
    for (size_t i = 0; i < faces.size(); ++i)
    {
        if (faces[i].m_faceId == faceId)
            return faces[i].m_path;
    }
    return std::wstring();
}

void CThreadWindow::CreateFonts()
{
    DestroyFonts();
    //正文用 balloonui 按本窗口 DPI 给的默认字体（归 balloonui 所有），标题用同样字号的加粗字体（本对象创建）
    m_bodyFont = (m_btnPublish != NULL) ? m_btnPublish->GetDefaultFont() : NULL;
    if (m_bodyFont == NULL)
        return;
    LOGFONTW lf;
    if (::GetObjectW(m_bodyFont, sizeof(lf), &lf) == sizeof(lf))
    {
        lf.lfWeight = FW_BOLD;
        m_titleFont = ::CreateFontIndirectW(&lf);
        //详情的标题：再大一些
        lf.lfHeight = lf.lfHeight * kDetailTitleScaleNum / kDetailTitleScaleDen;
        m_detailTitleFont = ::CreateFontIndirectW(&lf);
    }
    if (m_detailPane != NULL)
    {
        m_detailPane->Setup(m_bodyFont, m_titleFont, m_detailTitleFont, (m_owner != NULL) ? &m_owner->FaceEntries() : NULL);
        //正文、补充、留言与回复里点 @某人：弹成员菜单
        m_detailPane->SetAtClickHandler(&CThreadWindow::AtClickThunk, this);
    }
    //留言框里的控件只建一次（窗口关闭再打开时控件树整个重建，m_composer 是新的对象）
    if (m_composer != NULL && m_composer->Children().empty())
        m_composer->Build(m_bodyFont, m_hWnd, kMsgThreadWindowDeferred, thread_defer_send_comment);
}

void CThreadWindow::DestroyFonts()
{
    if (m_titleFont != NULL)
    {
        ::DeleteObject(m_titleFont);
        m_titleFont = NULL;
    }
    if (m_detailTitleFont != NULL)
    {
        ::DeleteObject(m_detailTitleFont);
        m_detailTitleFont = NULL;
    }
    m_bodyFont = NULL;
}

} // namespace threadgroup
