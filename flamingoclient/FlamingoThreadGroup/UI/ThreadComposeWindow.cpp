/**
 *  发帖窗口，见 ThreadComposeWindow.h。
 *  balloonwj@qq.com   2026-10-07
 */
#include "stdafx.h"
#include "ThreadComposeWindow.h"

#include <commdlg.h>   // GetOpenFileNameW：选择本机图片

#include "../Core/ThreadRules.h"
#include "../Core/ThreadSession.h"
#include "../Core/ThreadText.h"
#include "../../Source/Common/Log/IULog.h"
#include "../../Source/Common/I18n/I18n.h"   // TR / TRF / TR_NOOP：界面文字的多语言查找
#include "../../Source/Common/Util/FileDialogFilter.h"   // filedlg::MakeFilter：文件对话框的过滤串
#include "../../../third-party/balloonui/balloonui/DuiResMgr.h"
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiButton.h"
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiLabel.h"
#include "../../../third-party/balloonui/balloonui/Controls/Input/DuiEdit.h"
#include "ThreadCommentEdit.h"
#include "ThreadToolIcons.h"
#include "ThreadUiText.h"

using balloonwjui::DuiControl;
using balloonwjui::DuiEdit;

namespace threadgroup {

namespace {

//---- 尺寸，单位：像素 ----
const int kToolIconPx = 16;         //工具按钮图标边长（与 ThreadComposeLayout.cpp 的 kToolIconPx 一致）
const int kThumbBitmapRadius = 0;   //缩略图位图本身不做圆角（圆角由格子控件裁出）
const int kNameBufferChars = 128;   //取用户显示名的缓冲区长度（字符）
const int kPrevCharBufChars = 2;    //插入「@名字」前取光标前一个字的缓冲区长度（字符，一个字加结尾 0）
const int kThumbIndexBits = 16;     //compose_defer_thumb 的 lParam 里序号占低 16 位
const LPARAM kThumbIndexMask = 0xFFFF;   //compose_defer_thumb 的 lParam 里取序号用的掩码
const DWORD kFileBufferChars = 32 * 1024;//多选文件对话框的结果缓冲区长度（字符），够放几十个长文件名

//---- 颜色 ----
const COLORREF kToolIconColor = RGB(0x55, 0x55, 0x55);    //工具按钮图标的线条颜色（样稿 #555）
const COLORREF kFaint = RGB(0x9A, 0x9A, 0x9A);            //字数
const COLORREF kOverLimit = RGB(0xD9, 0x53, 0x4F);        //字数超出上限时

//---- 界面文字（文件作用域的字符串只做标记，使用处再调 TR） ----
const wchar_t* const kTitleNewPattern = TR_NOOP(L"发帖 · {0}");
const wchar_t* const kTitleNew = TR_NOOP(L"发帖");
const wchar_t* const kTitleEditPattern = TR_NOOP(L"编辑帖子 · {0}");
const wchar_t* const kTitleEdit = TR_NOOP(L"编辑帖子");
const wchar_t* const kTitleAddendumPattern = TR_NOOP(L"追加补充 · {0}");
const wchar_t* const kTitleAddendum = TR_NOOP(L"追加补充");
const wchar_t* const kTitleCountPattern = TR_NOOP(L"{0} / {1}");
const wchar_t* const kImageCountPattern = TR_NOOP(L"图片 {0}/{1}");
const wchar_t* const kUnknownFace = TR_NOOP(L"[表情]");
const wchar_t* const kConfirmDiscard = TR_NOOP(L"内容还没有发出，确定关闭吗？");
const wchar_t* const kImageFilesText = TR_NOOP(L"图片文件");
const wchar_t* const kAllFilesText = TR_NOOP(L"所有文件");
const wchar_t* const kAllFilesPattern = L"*.*";

//去掉首尾的空白（半角空格、全角空格、制表符与换行）
std::wstring TrimSpaces(const std::wstring& text)
{
    const wchar_t* const kSpaces = L" \t\r\n\x3000";
    const size_t first = text.find_first_not_of(kSpaces);
    if (first == std::wstring::npos)
        return std::wstring();
    const size_t last = text.find_last_not_of(kSpaces);
    return text.substr(first, last - first + 1);
}

} // namespace

CThreadComposeWindow::CThreadComposeWindow(int64_t groupId, CThreadSession* session, IThreadWindowOwner* owner)
    : m_groupId(groupId)
    , m_session(session)
    , m_owner(owner)
    , m_font(NULL)
    , m_iconFace(NULL)
    , m_iconImage(NULL)
    , m_membersPending(false)
    , m_submitting(false)
    , m_submitCmd(0)
    , m_closing(false)
{
    m_facePicker.SetPickCallback(&CThreadComposeWindow::FacePickedThunk, this);
    m_memberPicker.SetPickCallback(&CThreadComposeWindow::MemberPickedThunk, this);
}

CThreadComposeWindow::~CThreadComposeWindow()
{
    Close();
    DestroyResources();
}

//======================== 打开与关闭 ========================

bool CThreadComposeWindow::Open(HWND ownerWnd, const ComposeRequest& request)
{
    if (IsOpen())
    {
        const bool same = (request.m_mode == m_request.m_mode && request.m_postId == m_request.m_postId);
        const bool dirty = (m_model && m_model->IsDirty(CurrentTitle(), CurrentWire()));
        if (same || dirty || m_submitting)
        {
            //同一份内容，或正在编辑的内容有改动：不替换，只提到前面
            if (IsIconic())
                ShowWindow(SW_RESTORE);
            ::SetForegroundWindow(m_hWnd);
            return same;
        }
        Close();
    }
    m_request = request;
    const int kind = (request.m_mode == compose_mode_addendum) ? compose_addendum : compose_post;
    m_model.reset(new CThreadComposeModel(kind));
    m_seedAt.clear();
    if (!CreateForRequest(ownerWnd))
        return false;
    //编辑帖子：带入原帖；其余模式从空内容开始
    if (request.m_mode == compose_mode_edit_post && request.m_post != NULL)
        LoadPost(*request.m_post);
    m_request.m_post = NULL;
    m_model->MarkClean(CurrentTitle(), CurrentWire());
    UpdateCounts();
    RebuildImages();
    RebuildMentions();
    ShowWindow(SW_SHOW);
    ::SetForegroundWindow(m_hWnd);
    DuiControl* focus = (m_controls.m_title != NULL) ? static_cast<DuiControl*>(m_controls.m_title)
                                                     : static_cast<DuiControl*>(m_controls.m_body);
    if (request.m_mode == compose_mode_edit_post && m_controls.m_body != NULL)
        focus = m_controls.m_body;
    if (focus != NULL)
        focus->SetFocus();
    LOG_INFO("thread compose opened, mode=%d, groupid=%lld, postid=%lld.", request.m_mode,
             static_cast<long long>(m_groupId), static_cast<long long>(request.m_postId));
    return true;
}

bool CThreadComposeWindow::CreateForRequest(HWND ownerWnd)
{
    //有所有者的弹窗，用 WS_OVERLAPPEDWINDOW 建窗。控件通知发回本窗口（开发约定文档 UI 约定第 11 条）；2026-10-14 之前
    //用 WS_POPUP 会让通知发到所有者，当时为此选用重叠窗口，现在两种样式的通知都发回本窗口
    if (Create(ownerWnd, CWindow::rcDefault, WindowTitle().c_str(), WS_OVERLAPPEDWINDOW, 0) == NULL)
    {
        LOG_ERROR("thread compose: Create failed, groupid=%lld, error=%lu.", static_cast<long long>(m_groupId),
                  ::GetLastError());
        return false;
    }
    SetTitleBarHeight(kComposeTitleBarHeight);
    //标题栏上画的文字（窗口文字只用于任务切换等系统界面）
    SetTitle(WindowTitle().c_str());
    SetButtons(false, false, true);
    SetResizable(false);
    //程序图标：标题栏左侧的位图（图标资源在 exe 里，见 TitleBarIcon.h）；有所有者，不进任务栏
    SetIcon(m_programIcons.TitleBitmap(balloonmain::kProgramTitleIconSize, balloonmain::kOpaqueTitleBarColor));
    CreateResources();
    std::unique_ptr<balloonwjui::DuiVBox> body = BuildComposeBody(m_request.m_mode, m_font, m_controls);
    SetClientContent(std::unique_ptr<DuiControl>(body.release()));
    if (m_controls.m_btnFace != NULL)
        m_controls.m_btnFace->SetLeadingIcon(m_iconFace);
    if (m_controls.m_btnImage != NULL)
        m_controls.m_btnImage->SetLeadingIcon(m_iconImage);
    if (m_controls.m_body != NULL)
        m_controls.m_body->SetSendTarget(m_hWnd, kMsgThreadComposeDeferred, compose_defer_submit);
    //整窗尺寸含标题栏（UI 约定第 7 条）
    ResizeClient(kComposeWindowWidth, ComposeWindowHeight(m_request.m_mode));
    CenterOnOwner(ownerWnd);
    return true;
}

void CThreadComposeWindow::Close()
{
    if (m_facePicker.IsWindow())
        m_facePicker.DestroyWindow();
    if (m_memberPicker.IsWindow())
        m_memberPicker.Hide();
    if (IsWindow())
    {
        m_closing = true;
        DestroyWindow();
        m_closing = false;
    }
}

bool CThreadComposeWindow::IsOpen() const
{
    return IsWindow() != FALSE;
}

int CThreadComposeWindow::Mode() const
{
    return IsOpen() ? m_request.m_mode : 0;
}

int64_t CThreadComposeWindow::PostId() const
{
    return IsOpen() ? m_request.m_postId : 0;
}

std::wstring CThreadComposeWindow::WindowTitle() const
{
    switch (m_request.m_mode)
    {
    //编辑帖子
    case compose_mode_edit_post:
        return m_request.m_groupName.empty() ? std::wstring(TR(kTitleEdit)) : TRF(kTitleEditPattern, m_request.m_groupName);

    //追加补充：标题里写帖子的标题或正文开头
    case compose_mode_addendum:
        return m_request.m_postCaption.empty() ? std::wstring(TR(kTitleAddendum))
                                               : TRF(kTitleAddendumPattern, m_request.m_postCaption);

    //发新帖
    default:
        return m_request.m_groupName.empty() ? std::wstring(TR(kTitleNew)) : TRF(kTitleNewPattern, m_request.m_groupName);
    }
}

void CThreadComposeWindow::LoadPost(const ThreadPost& post)
{
    if (m_controls.m_title != NULL)
        m_controls.m_title->SetText(post.m_title.c_str());
    if (m_controls.m_body != NULL && m_owner != NULL)
        m_controls.m_body->SetText(WireToDisplay(post.m_text, m_owner->Faces(), TR(kUnknownFace)).c_str());
    //原帖里的 @：记下当时插入的名字，提交时用它重扫（对方后来改了群名片也能保留，docs/帖子群方案.md 14.4）
    m_seedAt = CandidatesFromSpans(post.m_text, post.m_at);
    //原有图片：已在服务端，可以直接提交；缩略图下载到了再换上（请求号先登记进模型，宿主可能同步回调）
    for (size_t i = 0; i < post.m_images.size(); ++i)
    {
        const ThreadImage& image = post.m_images[i];
        const uint32_t requestId = (m_owner != NULL) ? m_owner->BeginImageRequest(m_groupId) : 0;
        if (!m_model->AddExisting(image, requestId))
            break;
        if (requestId != 0 && !m_owner->StartDownload(requestId, image.m_fileId, image.m_md5))
            m_model->OnDownloaded(requestId, false, std::wstring());
    }
    for (size_t i = 0; i < post.m_mentions.size(); ++i)
        m_model->AddMention(post.m_mentions[i]);
}

//======================== 消息处理 ========================

LRESULT CThreadComposeWindow::OnCloseMsg(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& bHandled)
{
    bHandled = TRUE;
    RequestClose();
    return 0;
}

LRESULT CThreadComposeWindow::OnDestroyMsg(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& bHandled)
{
    bHandled = FALSE;
    if (m_facePicker.IsWindow())
        m_facePicker.DestroyWindow();
    if (m_memberPicker.IsWindow())
        m_memberPicker.Hide();
    //控件随窗口销毁：指针清掉；没有应答的请求不再认领（结果到了由帖子群窗口按普通操作提示）
    m_controls = ComposeControls();
    m_submitting = false;
    m_submitCmd = 0;
    LOG_INFO("thread compose closed, mode=%d, groupid=%lld.", m_request.m_mode, static_cast<long long>(m_groupId));
    return 0;
}

LRESULT CThreadComposeWindow::OnDuiNotify(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam, BOOL& bHandled)
{
    const balloonwjui::DuiNotify* pNotify = reinterpret_cast<const balloonwjui::DuiNotify*>(lParam);
    if (pNotify == NULL)
    {
        bHandled = FALSE;
        return 0;
    }
    //标题或正文的文字变了：刷新字数（通知码与控件 id 一起判，UI 约定第 4 条）
    if (pNotify->code == balloonwjui::DUIN_VALUECHANGED
        && (pNotify->ctrlId == kIdComposeTitle || pNotify->ctrlId == kIdComposeBody))
    {
        UpdateCounts();
        return 0;
    }
    if (pNotify->code != balloonwjui::DUIN_CLICK)
    {
        bHandled = FALSE;
        return 0;
    }
    //以下单击都延后执行：有的会重建图片行、名单行或关闭窗口，不能在被点控件自己的通知里做（UI 约定第 13 条）
    const UINT id = pNotify->ctrlId;
    if (id >= kIdComposeThumbBase && id <= kIdComposeThumbLast)
    {
        const LPARAM index = static_cast<LPARAM>(id - kIdComposeThumbBase);
        PostMessage(kMsgThreadComposeDeferred, compose_defer_thumb, index | (pNotify->extra << kThumbIndexBits));
        return 0;
    }
    if (id >= kIdComposeMentionBase && id <= kIdComposeMentionLast)
    {
        PostMessage(kMsgThreadComposeDeferred, compose_defer_mention_chip, static_cast<LPARAM>(id - kIdComposeMentionBase));
        return 0;
    }
    switch (id)
    {
    //「发布 / 保存 / 补充」
    case kIdComposeSubmit:
        PostMessage(kMsgThreadComposeDeferred, compose_defer_submit, 0);
        break;

    //「取消」
    case kIdComposeCancel:
        PostMessage(kMsgThreadComposeDeferred, compose_defer_cancel, 0);
        break;

    //「表情」
    case kIdComposeFace:
        PostMessage(kMsgThreadComposeDeferred, compose_defer_open_face, 0);
        break;

    //「图片」与图片行末尾的「＋ 图片」
    case kIdComposeImage:
    case kIdComposeAddImage:
        PostMessage(kMsgThreadComposeDeferred, compose_defer_add_image, 0);
        break;

    //「@ 成员」
    case kIdComposeMention:
        PostMessage(kMsgThreadComposeDeferred, compose_defer_open_members, 0);
        break;

    //其它控件的单击：不处理
    default:
        bHandled = FALSE;
        break;
    }
    return 0;
}

LRESULT CThreadComposeWindow::OnDeferredMsg(UINT /*uMsg*/, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
{
    bHandled = TRUE;
    if (!IsWindow() || !m_model)
    {
        //窗口已在关闭：带堆对象的消息照样释放
        if (static_cast<int>(wParam) == compose_defer_member_picked)
            delete reinterpret_cast<int64_t*>(lParam);
        return 0;
    }
    switch (static_cast<int>(wParam))
    {
    //点了「发布 / 保存 / 补充」或按了 Ctrl+Enter
    case compose_defer_submit:
        Submit();
        break;

    //点了「取消」
    case compose_defer_cancel:
        RequestClose();
        break;

    //点了「表情」
    case compose_defer_open_face:
        OpenFacePicker();
        break;

    //表情浮层里选中了一个表情
    case compose_defer_face_picked:
        if (m_facePicker.IsWindow())
            m_facePicker.Hide();
        InsertFace(static_cast<int>(lParam));
        break;

    //点了「图片」或「＋ 图片」
    case compose_defer_add_image:
        PickImages();
        break;

    //点了一张缩略图：lParam 低 16 位为序号，高 16 位为点在了哪里
    case compose_defer_thumb:
        HandleThumbClick(static_cast<size_t>(lParam & kThumbIndexMask), static_cast<int>(lParam >> kThumbIndexBits));
        break;

    //点了「@ 成员」
    case compose_defer_open_members:
        OpenMemberPicker();
        break;

    //选人浮层里选中了一个人：lParam 为堆上的用户 ID
    case compose_defer_member_picked:
    {
        std::unique_ptr<int64_t> userId(reinterpret_cast<int64_t*>(lParam));
        if (m_memberPicker.IsWindow())
            m_memberPicker.Hide();
        //浮层关掉后把窗口重新激活，焦点回到正文
        ::SetForegroundWindow(m_hWnd);
        if (userId)
            InsertMention(*userId);
        break;
    }

    //点了已 @ 名单里的一个：从名单里去掉（正文里的「@名字」文字不动）
    case compose_defer_mention_chip:
    {
        const size_t index = static_cast<size_t>(lParam);
        if (index < m_model->Mentions().size())
        {
            m_model->RemoveMention(m_model->Mentions()[index]);
            RebuildMentions();
        }
        break;
    }

    //不认识的动作：忽略
    default:
        break;
    }
    return 0;
}

//======================== 内容 ========================

std::wstring CThreadComposeWindow::CurrentTitle() const
{
    if (m_controls.m_title == NULL)
        return std::wstring();
    return TrimSpaces(std::wstring(m_controls.m_title->GetText().GetString()));
}

std::wstring CThreadComposeWindow::CurrentWire() const
{
    if (m_controls.m_body == NULL || m_owner == NULL)
        return std::wstring();
    return DisplayToWire(std::wstring(m_controls.m_body->GetText().GetString()), m_owner->Faces());
}

void CThreadComposeWindow::UpdateCounts()
{
    if (m_controls.m_title != NULL)
    {
        //标题的字数按输入框里的原样计（与提交时去掉首尾空白后的口径差别很小，超出时以提交时的校验为准）
        const int count = CountChars(std::wstring(m_controls.m_title->GetText().GetString()));
        m_controls.m_title->SetIconGlyph(DuiEdit::RightIcon, m_controls.m_title->GetIconWidth(DuiEdit::RightIcon),
                                         TRF(kTitleCountPattern, count, kTitleMaxChars).c_str(),
                                         count > kTitleMaxChars ? kOverLimit : kFaint);
    }
    if (m_controls.m_count != NULL && m_model)
    {
        const int count = CountChars(CurrentWire());
        m_controls.m_count->SetText(ComposeCountText(m_request.m_mode, count, m_request.m_addendumIndex).c_str());
        m_controls.m_count->SetTextColor(count > TextLimitOf(m_model->Kind()) ? kOverLimit : kFaint);
    }
}

void CThreadComposeWindow::RebuildImages()
{
    if (m_controls.m_images == NULL || !m_model)
        return;
    const std::vector<ComposeImage>& images = m_model->Images();
    const bool showAdd = (m_model->RoomForImages() > 0);
    const int rowWidth = kComposeWindowWidth - 2 * kComposePadX;
    const int tile = CThreadComposeImageRow::TileSize(static_cast<int>(images.size()) + (showAdd ? 1 : 0), rowWidth);
    std::vector<ComposeTileView> tiles;
    for (size_t i = 0; i < images.size(); ++i)
    {
        ComposeTileView view;
        view.m_state = images[i].m_state;
        if (!images[i].m_localPath.empty())
            view.m_bitmap = m_bitmaps.Get(images[i].m_localPath, tile, kThumbBitmapRadius);
        tiles.push_back(view);
    }
    m_controls.m_images->Rebuild(tiles, showAdd, TRF(kImageCountPattern, m_model->ImageCount(), kMaxImages), m_font,
                                 rowWidth);
}

void CThreadComposeWindow::RebuildMentions()
{
    if (m_controls.m_mentions == NULL || !m_model)
        return;
    std::vector<std::wstring> names;
    const std::vector<int64_t>& ids = m_model->Mentions();
    for (size_t i = 0; i < ids.size(); ++i)
        names.push_back(UserName(ids[i]));
    const bool show = !names.empty();
    if (m_controls.m_mentions->IsVisible() != show)
    {
        //名单行显隐：SetVisible 不会重排，对它的父容器按当前矩形重排（UI 约定第 5 条）
        m_controls.m_mentions->SetVisible(show);
        DuiControl* parent = m_controls.m_mentions->GetParent();
        if (parent != NULL)
            parent->ForceLayout(parent->GetRect());
    }
    m_controls.m_mentions->Rebuild(names, m_font, kComposeWindowWidth - 2 * kComposePadX);
}

//======================== 提交与关闭 ========================

void CThreadComposeWindow::Submit()
{
    if (m_submitting || !m_model || m_session == NULL)
        return;
    const std::wstring title = CurrentTitle();
    const std::wstring wire = CurrentWire();
    const int check = m_model->CheckSubmit(title, wire);
    if (check != compose_ok)
    {
        SetError(FormatComposeCheck(check, m_model->Kind()));
        return;
    }
    const std::vector<ThreadImage> images = m_model->UploadedImages();
    const std::vector<int64_t>& mentions = m_model->Mentions();
    //@ 片段：按最终正文重扫（docs/帖子群方案.md 14.4）。候选是原帖里的 @（编辑时）加上名单里每个人现在的名字
    std::vector<ThreadAtCandidate> candidates = m_seedAt;
    for (size_t i = 0; i < mentions.size(); ++i)
    {
        ThreadAtCandidate cand;
        cand.m_userId = mentions[i];
        cand.m_name = UserName(mentions[i]);
        candidates.push_back(cand);
    }
    const std::vector<ThreadAtSpan> at = RescanAtSpans(wire, candidates);
    bool sent = false;
    switch (m_request.m_mode)
    {
    //编辑帖子
    case compose_mode_edit_post:
        m_submitCmd = thread_cmd_edit_post;
        sent = m_session->EditPost(m_request.m_postId, title, wire, images, mentions, at);
        break;

    //追加补充
    case compose_mode_addendum:
        m_submitCmd = thread_cmd_add_addendum;
        sent = m_session->AddAddendum(m_request.m_postId, wire, images, mentions, at);
        break;

    //发新帖
    default:
        m_submitCmd = thread_cmd_publish_post;
        sent = m_session->PublishPost(title, wire, images, mentions, at);
        break;
    }
    if (!sent)
    {
        m_submitCmd = 0;
        SetError(NotSentText());
        return;
    }
    SetError(std::wstring());
    SetSubmitting(true);
    LOG_INFO("thread compose submitted, mode=%d, groupid=%lld, postid=%lld, images=%u, mentions=%u.", m_request.m_mode,
             static_cast<long long>(m_groupId), static_cast<long long>(m_request.m_postId),
             static_cast<unsigned>(images.size()), static_cast<unsigned>(mentions.size()));
}

bool CThreadComposeWindow::OnActionDone(int cmd, int code, int64_t id)
{
    if (!IsOpen() || !m_submitting || cmd != m_submitCmd)
        return false;
    SetSubmitting(false);
    m_submitCmd = 0;
    if (code == thread_error_ok)
    {
        LOG_INFO("thread compose done, cmd=%d, id=%lld.", cmd, static_cast<long long>(id));
        Close();
        return true;
    }
    LOG_WARNING("thread compose failed, cmd=%d, code=%d.", cmd, code);
    SetError(FormatActionError(code));
    return true;
}

void CThreadComposeWindow::RequestClose()
{
    if (m_closing || !IsWindow())
        return;
    //内容有改动、还没有发出：先确认（正在发出时同样确认，关闭后结果由帖子群窗口提示）
    if (m_model && m_model->IsDirty(CurrentTitle(), CurrentWire()))
    {
        const std::wstring title = WindowTitle();
        if (::MessageBoxW(m_hWnd, TR(kConfirmDiscard), title.c_str(), MB_OKCANCEL | MB_ICONQUESTION | MB_DEFBUTTON2) != IDOK)
            return;
    }
    Close();
}

void CThreadComposeWindow::SetError(const std::wstring& text)
{
    if (m_controls.m_error != NULL)
        m_controls.m_error->SetText(text.c_str());
}

void CThreadComposeWindow::SetSubmitting(bool submitting)
{
    m_submitting = submitting;
    if (m_controls.m_submit != NULL)
        m_controls.m_submit->SetEnabled(!submitting);
}

//======================== 表情 ========================

void CThreadComposeWindow::OpenFacePicker()
{
    if (m_controls.m_btnFace == NULL || m_owner == NULL)
        return;
    RECT anchor = m_controls.m_btnFace->GetRect();
    ::MapWindowPoints(m_hWnd, NULL, reinterpret_cast<POINT*>(&anchor), 2);
    m_facePicker.OpenAt(anchor, m_owner->FaceEntries());
}

void CThreadComposeWindow::FacePickedThunk(void* user, int faceId)
{
    CThreadComposeWindow* self = static_cast<CThreadComposeWindow*>(user);
    if (self != NULL && self->IsWindow())
        self->PostMessage(kMsgThreadComposeDeferred, compose_defer_face_picked, static_cast<LPARAM>(faceId));
}

void CThreadComposeWindow::InsertFace(int faceId)
{
    if (m_controls.m_body == NULL || m_owner == NULL)
        return;
    std::wstring name;
    if (!m_owner->Faces().NameOf(faceId, name))
        return;
    m_controls.m_body->SetFocus();
    m_controls.m_body->ReplaceSel((L"[" + name + L"]").c_str());
    UpdateCounts();
}

//======================== 图片 ========================

void CThreadComposeWindow::PickImages()
{
    if (!m_model || m_owner == NULL)
        return;
    const int room = m_model->RoomForImages();
    if (room <= 0)
    {
        SetError(FormatComposeCheck(compose_too_many_images, m_model->Kind()));
        return;
    }
    std::vector<wchar_t> buffer(kFileBufferChars, L'\0');
    OPENFILENAMEW ofn;
    ::ZeroMemory(&ofn, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = m_hWnd;
    //文件类型过滤串：说明文字按界面语言翻译后再拼成带 \0 的过滤串，须活到对话框关闭
    const std::wstring fileFilter = filedlg::MakeFilter(TR(kImageFilesText), filedlg::kImagePatterns, TR(kAllFilesText),
                                                        kAllFilesPattern);
    ofn.lpstrFilter = fileFilter.c_str();
    ofn.lpstrFile = &buffer[0];
    ofn.nMaxFile = kFileBufferChars;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_EXPLORER | OFN_ALLOWMULTISELECT;
    if (!::GetOpenFileNameW(&ofn))
        return;
    const std::vector<std::wstring> files = SplitFileSelection(&buffer[0], buffer.size());
    size_t added = 0;
    for (size_t i = 0; i < files.size() && static_cast<int>(added) < room; ++i)
    {
        StartImageUpload(files[i]);
        ++added;
    }
    //选多了：只加到 9 张为止，说明原因
    SetError(files.size() > added ? FormatComposeCheck(compose_too_many_images, m_model->Kind()) : std::wstring());
    LOG_INFO("thread compose images picked, selected=%u, added=%u.", static_cast<unsigned>(files.size()),
             static_cast<unsigned>(added));
    RebuildImages();
}

void CThreadComposeWindow::StartImageUpload(const std::wstring& path)
{
    if (!m_model || m_owner == NULL)
        return;
    const uint32_t requestId = m_owner->BeginImageRequest(m_groupId);
    if (requestId == 0 || !m_model->AddUploading(requestId, path))
        return;
    if (!m_owner->StartUpload(requestId, path))
    {
        LOG_WARNING("thread compose upload not started, request=%u.", static_cast<unsigned>(requestId));
        m_model->OnUploaded(requestId, false, ThreadImage(), std::wstring());
    }
}

void CThreadComposeWindow::HandleThumbClick(size_t index, int where)
{
    if (!m_model || index >= m_model->Images().size())
        return;
    //点了「✕」：删除这张图（上传中的也可以删，结果到了不再认领）
    if (where == compose_thumb_remove)
    {
        m_model->RemoveImage(index);
        SetError(std::wstring());
        RebuildImages();
        return;
    }
    const ComposeImage& image = m_model->Images()[index];
    //上传失败的图：换一个请求号重新上传
    if (image.m_state == compose_image_failed && m_owner != NULL)
    {
        const std::wstring path = image.m_localPath;
        const uint32_t requestId = m_owner->BeginImageRequest(m_groupId);
        if (requestId != 0 && m_model->RetryUpload(index, requestId) && !m_owner->StartUpload(requestId, path))
            m_model->OnUploaded(requestId, false, ThreadImage(), std::wstring());
        SetError(std::wstring());
        RebuildImages();
        return;
    }
    //已上传的图：本机有文件的几张交给宿主的看图窗口，从被点的那张开始
    IThreadGroupHost* host = (m_owner != NULL) ? m_owner->Host() : NULL;
    if (image.m_state != compose_image_done || image.m_localPath.empty() || host == NULL)
        return;
    std::vector<const wchar_t*> paths;
    uint32_t start = 0;
    const std::vector<ComposeImage>& images = m_model->Images();
    for (size_t i = 0; i < images.size(); ++i)
    {
        if (images[i].m_localPath.empty())
            continue;
        if (i == index)
            start = static_cast<uint32_t>(paths.size());
        paths.push_back(images[i].m_localPath.c_str());
    }
    host->OpenImageViewer(m_hWnd, &paths[0], static_cast<uint32_t>(paths.size()), start);
}


bool CThreadComposeWindow::OnImageUploaded(uint32_t requestId, bool ok, const ThreadImage& image, const std::wstring& localPath)
{
    if (!IsOpen() || !m_model || !m_model->OnUploaded(requestId, ok, image, localPath))
        return false;
    RebuildImages();
    return true;
}

bool CThreadComposeWindow::OnImageReady(uint32_t requestId, bool ok, const std::wstring& localPath)
{
    if (!IsOpen() || !m_model || !m_model->OnDownloaded(requestId, ok, localPath))
        return false;
    RebuildImages();
    return true;
}

//======================== @ 成员 ========================

void CThreadComposeWindow::OpenMemberPicker()
{
    if (m_controls.m_btnMention == NULL || m_owner == NULL)
        return;
    IThreadGroupHost* host = m_owner->Host();
    //还没有名单：向宿主请求（结果经帖子群窗口的 OnGroupMembers 转来），浮层先显示「正在加载成员…」
    if (!m_members.IsLoaded() && !m_membersPending && host != NULL)
    {
        m_membersPending = true;
        host->RequestGroupMembers(m_groupId);
    }
    m_memberPicker.SetMembers(MemberEntries(), m_members.IsLoaded());
    RECT anchor = m_controls.m_btnMention->GetRect();
    ::MapWindowPoints(m_hWnd, NULL, reinterpret_cast<POINT*>(&anchor), 2);
    m_memberPicker.OpenAt(anchor);
}

void CThreadComposeWindow::OnGroupMembers(const std::vector<ThreadMember>& members)
{
    m_membersPending = false;
    m_members.Set(members);
    LOG_INFO("thread compose got %u member(s), groupid=%lld.", static_cast<unsigned>(m_members.Count()),
             static_cast<long long>(m_groupId));
    //浮层开着时换上名单
    if (m_memberPicker.IsWindow() && m_memberPicker.IsWindowVisible())
        m_memberPicker.SetMembers(MemberEntries(), true);
}

void CThreadComposeWindow::InvalidateMembers()
{
    //正在请求的那一份到达时照常收下：它比现在缓存的新
    m_members.Clear();
}

std::vector<MemberPickEntry> CThreadComposeWindow::MemberEntries() const
{
    //本人不在可选之列
    IThreadGroupHost* host = (m_owner != NULL) ? m_owner->Host() : NULL;
    const int64_t myId = (host != NULL) ? host->GetMyUserId() : 0;
    //有权限 @所有人 时列表最上面是「所有人」（docs/帖子群方案.md §15.1 第 1 条）：本人角色与群设置每次都向宿主取最新的
    ThreadGroupBrief brief;
    memset(&brief, 0, sizeof(brief));
    brief.m_cbSize = sizeof(brief);
    const bool known = (host != NULL) && host->GetGroupBrief(m_groupId, &brief);
    const bool canAtAll = known && CanMentionAll(brief.m_myRole, brief.m_atAllScope);
    const std::vector<ThreadMember> order = MentionPickList(m_members.PickOrder(myId), canAtAll);
    std::vector<MemberPickEntry> entries;
    for (size_t i = 0; i < order.size(); ++i)
    {
        MemberPickEntry entry;
        entry.m_userId = order[i].m_userId;
        entry.m_name = order[i].m_name.empty() ? UserName(order[i].m_userId) : order[i].m_name;
        entries.push_back(entry);
    }
    return entries;
}

void CThreadComposeWindow::MemberPickedThunk(void* user, int64_t userId)
{
    CThreadComposeWindow* self = static_cast<CThreadComposeWindow*>(user);
    if (self == NULL || !self->IsWindow())
        return;
    //用户 ID 是 64 位，32 位程序的 LPARAM 放不下：放在堆上，由消息处理函数 delete
    std::unique_ptr<int64_t> payload(new int64_t(userId));
    if (self->PostMessage(kMsgThreadComposeDeferred, compose_defer_member_picked, reinterpret_cast<LPARAM>(payload.get())))
        payload.release();
}

void CThreadComposeWindow::InsertMention(int64_t userId)
{
    if (!m_model || m_controls.m_body == NULL)
        return;
    if (!m_model->HasMention(userId) && !m_model->AddMention(userId))
    {
        SetError(FormatComposeCheck(compose_too_many_mentions, m_model->Kind()));
        return;
    }
    m_controls.m_body->SetFocus();
    //光标前一个字不是空白时先补一个空格：发送前的重扫只认位于开头或紧跟空白之后的「@名字」（NeedsSpaceBeforeAt），
    //直接接在文字后面插入的会被当成普通文字（2026-10-08 联调发现「hello@所有人」没有 @ 到任何人）
    long selStart = 0;
    long selEnd = 0;
    m_controls.m_body->GetSel(selStart, selEnd);
    wchar_t prev[kPrevCharBufChars] = { 0 };
    if (selStart > 0)
    {
        //富文本控件的 EM_GETTEXTRANGE：lParam 为 TEXTRANGEW 的指针，取 [cpMin, cpMax) 的文字，不改动选区
        TEXTRANGEW range;
        range.chrg.cpMin = selStart - 1;
        range.chrg.cpMax = selStart;
        range.lpstrText = prev;
        m_controls.m_body->SendMessageToEngine(EM_GETTEXTRANGE, 0, reinterpret_cast<LPARAM>(&range));
    }
    const std::wstring lead = NeedsSpaceBeforeAt(static_cast<int>(selStart), prev[0]) ? L" " : L"";
    m_controls.m_body->ReplaceSel((lead + L"@" + UserName(userId) + L" ").c_str());
    SetError(std::wstring());
    RebuildMentions();
    UpdateCounts();
}

std::wstring CThreadComposeWindow::UserName(int64_t userId) const
{
    //「@所有人」：插入的是固定的「所有人」三个字
    if (userId == kThreadAtAllUserId)
        return AtAllName();
    //成员名单里的名字与普通群 @ 的口径相同（群名片或昵称），优先用它
    const std::wstring fromBook = m_members.NameOf(userId);
    if (!fromBook.empty())
        return fromBook;
    wchar_t buf[kNameBufferChars] = { 0 };
    IThreadGroupHost* host = (m_owner != NULL) ? m_owner->Host() : NULL;
    if (host != NULL && host->GetUserDisplayName(userId, m_groupId, buf, kNameBufferChars) && buf[0] != L'\0')
        return buf;
    return Utf8ToWide(IdToString(userId));
}

//======================== 资源与摆放 ========================

void CThreadComposeWindow::CreateResources()
{
    DestroyResources();
    m_font = balloonwjui::DuiResMgr::Inst().GetDefaultFontForDpi(GetDpi());
    m_iconFace = MakeToolIcon(tool_icon_face, kToolIconPx, kToolIconColor);
    m_iconImage = MakeToolIcon(tool_icon_image, kToolIconPx, kToolIconColor);
}

void CThreadComposeWindow::DestroyResources()
{
    if (m_iconFace != NULL)
    {
        ::DeleteObject(m_iconFace);
        m_iconFace = NULL;
    }
    if (m_iconImage != NULL)
    {
        ::DeleteObject(m_iconImage);
        m_iconImage = NULL;
    }
    m_bitmaps.Clear();
}

void CThreadComposeWindow::CenterOnOwner(HWND ownerWnd)
{
    RECT rc;
    GetWindowRect(&rc);
    const int width = rc.right - rc.left;
    const int height = rc.bottom - rc.top;
    RECT ownerRc;
    POINT center;
    if (ownerWnd != NULL && ::IsWindowVisible(ownerWnd) && !::IsIconic(ownerWnd) && ::GetWindowRect(ownerWnd, &ownerRc))
    {
        center.x = (ownerRc.left + ownerRc.right) / 2;
        center.y = (ownerRc.top + ownerRc.bottom) / 2;
    }
    else
    {
        ::GetCursorPos(&center);
    }
    MONITORINFO info;
    info.cbSize = sizeof(info);
    if (!::GetMonitorInfoW(::MonitorFromPoint(center, MONITOR_DEFAULTTOPRIMARY), &info))
        return;
    const RECT& work = info.rcWork;
    int x = center.x - width / 2;
    int y = center.y - height / 2;
    //整窗落在工作区内：先贴右下，再贴左上（工作区比窗口小时以左上为准，标题栏不出屏）
    if (x + width > work.right)
        x = work.right - width;
    if (y + height > work.bottom)
        y = work.bottom - height;
    if (x < work.left)
        x = work.left;
    if (y < work.top)
        y = work.top;
    SetWindowPos(NULL, x, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
}

} // namespace threadgroup
