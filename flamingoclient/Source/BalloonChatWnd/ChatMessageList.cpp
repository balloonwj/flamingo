/**
 *  ChatMessageList.cpp
 *
 *  单聊窗消息区的实现。自上而下堆叠变高的 ChatBubble 行，鼠标滚轮滚动，
 *  右侧画细滚动条。每个气泡绘制时单独设一次裁剪区，确保滚动时跨越视口
 *  边界的气泡（含彩色 emoji）不会画到头部 / 标题栏 / 输入区上。
 *
 *  进入多选模式后，底部让出一条操作条（取消 / 转发 N / 删除 N）。
 *
 *  本 .cpp 不 include Flamingo 的 stdafx.h —— 它要被 Flamingo.vcxproj 与
 *  FlamingoUnit.vcxproj 共用。在 Flamingo.vcxproj 里需标记
 *  PrecompiledHeader=NotUsing。
 *
 *  balloonwj@qq.com   2026-05-21
 */

// ---- balloonui 头文件要求的 ATL/WTL 前置依赖（等价 NewChatDemo/stdafx.h）----
#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif
#define WINVER          0x0601      // 目标系统下限 = Windows 7
#define _WIN32_WINNT    0x0601
#define _WIN32_IE       0x0601
#define _RICHEDIT_VER   0x0200
#define _WTL_NO_CSTRING
#define _WTL_NO_WTYPES

#include <atlbase.h>
#include <atlstr.h>
#include <atltypes.h>
#include <atlapp.h>

extern CAppModule _Module;

#include <atlwin.h>
#include <atlcrack.h>
#include <atlmisc.h>
#include <atlgdi.h>

#include <vector>
#include <algorithm>   // std::rotate —— PrependMessages 把新追加的一段挪到列表头部

#include <gdiplus.h>
#pragma comment(lib, "gdiplus.lib")

#include "ChatMessageList.h"
#include "ChatBubble.h"
#include "ChatHistoryPaging.h"   // kHistoryLoadTriggerY（滚到多近算"到顶了"）
#include "../CustomMsgDef.h"   // WM_CHAT_RECALL_REQUEST（撤回请求转交宿主聊天窗）
#include "../../balloonui/balloonui/DuiHost.h"

namespace balloonchatwnd {

namespace {

// 一个滚轮刻度滚动的像素数。
const int kWheelStep = 64;
// 滚动条几何。
const int kBarWidth   = 5;
const int kBarMargin  = 2;
const int kBarMinThumb = 36;
// 滚动条滑块色（半透明灰）。
const Gdiplus::Color kThumbColor(120, 0x5A, 0x5A, 0x5A);

// 一个用于"滚到底部"的足够大的初值（Layout 时会夹紧到真实最大值）。
const int kScrollToBottomSentinel = 0x3FFFFFF;

// 多选操作条高度（逻辑像素）。
const int kMultiSelectBarH = 48;

// 多选操作条文字色。
const COLORREF kBarCancelColor  = RGB(0x6B, 0x6B, 0x6B);  // "取消"
const COLORREF kBarForwardColor = RGB(0xFF, 0x7A, 0x45);  // "转发 N"
const COLORREF kBarDeleteColor  = RGB(0xE5, 0x39, 0x35);  // "删除 N"
const COLORREF kBarDisabledColor = RGB(0xBB, 0xBB, 0xBB); // 选中数为 0 时

// 操作条文字字号。
const int kBarTextPx = 13;

// 用 GDI 绘制文字（操作条用）。
void DrawBarText(HDC hdc, LPCWSTR text, const RECT& rc, COLORREF color)
{
    if (text == NULL || text[0] == L'\0')
    {
        return;
    }
    HFONT hFont = ::CreateFontW(
        -kBarTextPx, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Microsoft YaHei");
    if (hFont == NULL)
    {
        return;
    }
    HFONT    hOldFont  = (HFONT)::SelectObject(hdc, hFont);
    int      oldBkMode = ::SetBkMode(hdc, TRANSPARENT);
    COLORREF oldColor  = ::SetTextColor(hdc, color);
    RECT r = rc;
    ::DrawTextW(hdc, text, -1, &r,
                DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    ::SetTextColor(hdc, oldColor);
    ::SetBkMode(hdc, oldBkMode);
    ::SelectObject(hdc, hOldFont);
    ::DeleteObject(hFont);
}

} // anonymous namespace

ChatMessageList::ChatMessageList()
    : m_scrollY(0)
    , m_contentHeight(0)
    , m_viewHeight(0)
    , m_multiSelect(false)
    , m_thumbDragging(false)
    , m_thumbDragOffsetY(0)
    , m_memDC(NULL)
    , m_memBmp(NULL)
    , m_memOldBmp(NULL)
    , m_memW(0)
    , m_memH(0)
    , m_memberQuery(NULL)
    , m_memberQueryUd(NULL)
    , m_roleQuery(NULL)
    , m_roleQueryUd(NULL)
    , m_myNetRole(-1)
    , m_prependActive(false)
    , m_prependMarkCount(0)
    , m_prependMarkHeight(0)
{
}

void ChatMessageList::SetMemberInGroupQuery(MemberInGroupQuery fn, void* userdata)
{
    m_memberQuery   = fn;
    m_memberQueryUd = userdata;
}

bool ChatMessageList::IsMemberInGroup(long long uid) const
{
    if (m_memberQuery == NULL)
    {
        return true;   // 无从查证（单聊窗 / 调试入口），不平白置灰菜单项
    }
    return m_memberQuery(m_memberQueryUd, uid);
}

void ChatMessageList::SetMemberRoleQuery(MemberRoleQuery fn, void* userdata)
{
    m_roleQuery   = fn;
    m_roleQueryUd = userdata;
}

int ChatMessageList::MemberNetRole(long long uid) const
{
    if (m_roleQuery == NULL)
    {
        return -1;   // 无从查证（单聊窗 / 调试入口），管理项一律不出现
    }
    return m_roleQuery(m_roleQueryUd, uid);
}

void ChatMessageList::SetMyNetRole(int role)
{
    m_myNetRole = role;
}

int ChatMessageList::MyNetRole() const
{
    return m_myNetRole;
}

ChatMessageList::~ChatMessageList()
{
    ReleaseOffscreen();
}

void ChatMessageList::ReleaseOffscreen()
{
    if (m_memDC != NULL)
    {
        if (m_memOldBmp != NULL)
        {
            ::SelectObject(m_memDC, m_memOldBmp);
            m_memOldBmp = NULL;
        }
        ::DeleteDC(m_memDC);
        m_memDC = NULL;
    }
    if (m_memBmp != NULL)
    {
        ::DeleteObject(m_memBmp);
        m_memBmp = NULL;
    }
    m_memW = 0;
    m_memH = 0;
}

void ChatMessageList::EnsureOffscreen(HDC ref, int w, int h)
{
    // 现有缓冲已够大则复用（只增不减，避免缩放时频繁重建）。
    if (m_memDC != NULL && m_memW >= w && m_memH >= h)
    {
        return;
    }

    // 把申请尺寸按整块向上取整再分配 —— 拖动 / 缩放时 w、h 会逐帧增大若干像素，
    // 若每帧都按精确尺寸重建一张接近整窗大小的位图，CreateCompatibleBitmap
    // 会成为每帧的重开销。按 kAllocChunk 取整后，一次分配能覆盖随后多帧的增长，
    // 把重建频率从"每帧"降到"每涨过一个块才一次"。
    const int kAllocChunk = 256;
    int allocW = ((w + kAllocChunk - 1) / kAllocChunk) * kAllocChunk;
    int allocH = ((h + kAllocChunk - 1) / kAllocChunk) * kAllocChunk;
    if (allocW < m_memW) { allocW = m_memW; }   // 仍是只增不减
    if (allocH < m_memH) { allocH = m_memH; }

    ReleaseOffscreen();

    m_memDC = ::CreateCompatibleDC(ref);
    if (m_memDC == NULL)
    {
        return;
    }
    m_memBmp = ::CreateCompatibleBitmap(ref, allocW, allocH);
    if (m_memBmp == NULL)
    {
        ::DeleteDC(m_memDC);
        m_memDC = NULL;
        return;
    }
    m_memOldBmp = (HBITMAP)::SelectObject(m_memDC, m_memBmp);
    m_memW = allocW;
    m_memH = allocH;
}

RECT ChatMessageList::RowsViewRect() const
{
    // 消息行可视区 = 本控件矩形扣掉底部多选操作条（非多选时不扣）。
    RECT r = m_rcItem;
    if (m_multiSelect)
    {
        r.bottom -= kMultiSelectBarH;
    }
    return r;
}

RECT ChatMessageList::MultiSelectBarRect() const
{
    RECT r = { 0, 0, 0, 0 };
    if (m_multiSelect)
    {
        r.left   = m_rcItem.left;
        r.right  = m_rcItem.right;
        r.top    = m_rcItem.bottom - kMultiSelectBarH;
        r.bottom = m_rcItem.bottom;
    }
    return r;
}

HWND ChatMessageList::HostHwnd() const
{
    balloonwjui::DuiHost* host = GetHost();
    return (host != NULL) ? host->m_hWnd : NULL;
}

int ChatMessageList::SelectedCount() const
{
    int count = 0;
    const std::vector<std::unique_ptr<balloonwjui::DuiControl> >& kids = Children();
    for (size_t i = 0; i < kids.size(); ++i)
    {
        ChatBubble* b = dynamic_cast<ChatBubble*>(kids[i].get());
        if (b != NULL && b->IsSelected())
        {
            ++count;
        }
    }
    return count;
}

void ChatMessageList::ScrollToBottom()
{
    // 置一个足够大的值；下一次 Layout 会夹紧到真实的最大滚动量。
    m_scrollY = kScrollToBottomSentinel;
    Invalidate();
}

bool ChatMessageList::ScrollToMsgid(long long msgid)
{
    if (msgid == 0)
    {
        return false;
    }
    // 先按当前滚动布局一次，确保各气泡 rect 是最新的。
    ForceLayout(GetRect());

    const std::vector<std::unique_ptr<balloonwjui::DuiControl> >& kids = Children();
    ChatBubble* target = NULL;
    for (size_t i = 0; i < kids.size(); ++i)
    {
        ChatBubble* b = dynamic_cast<ChatBubble*>(kids[i].get());
        if (b != NULL && b->Msgid() == msgid)
        {
            target = b;
            break;
        }
    }
    if (target == NULL)
    {
        return false;   // 该 msgid 不在已加载的行里（如较早消息未加载）
    }

    // 由目标气泡当前 rect 反推其在内容坐标里的顶部偏移，把滚动偏移设到那里（留点上边距）。
    // 下一次 Layout 会把 m_scrollY 夹紧到合法范围（目标靠近底部时会被夹住，但仍在视口内）。
    const int kTopMargin = 10;
    RECT view = RowsViewRect();
    RECT br = target->GetRect();
    int contentTop = (br.top - view.top) + m_scrollY;
    m_scrollY = contentTop - kTopMargin;
    if (m_scrollY < 0)
    {
        m_scrollY = 0;
    }
    ForceLayout(GetRect());
    Invalidate();
    return true;
}

void ChatMessageList::AppendMessage(const ChatMessage& msg)
{
    std::unique_ptr<ChatBubble> bubble(new ChatBubble());
    bubble->SetMessage(msg);
    AddChild(std::unique_ptr<balloonwjui::DuiControl>(bubble.release()));

    // 新增行后重新堆叠，并把视图滚到底部显示这条新消息。
    // 但向上翻页的批次期间不能滚 —— 那时用户正盯着某处看，滚到底等于把他甩走；
    // 滚动位置由 EndPrependBatch 统一按新增高度补偿。
    if (!m_prependActive)
    {
        ScrollToBottom();
    }
    ForceLayout(GetRect());
    Invalidate();
}

void ChatMessageList::BeginPrependBatch()
{
    // 先排一次，拿到批次开始时的准确内容总高（下面要用它算"上方长高了多少"）。
    ForceLayout(GetRect());
    m_prependMarkCount  = Children().size();
    m_prependMarkHeight = m_contentHeight;
    m_prependActive     = true;
}

void ChatMessageList::EndPrependBatch()
{
    if (!m_prependActive)
    {
        return;
    }
    m_prependActive = false;

    const size_t total = Children().size();
    if (total <= m_prependMarkCount)
    {
        return;   // 本批一行都没追加（服务端返回空页）
    }

    // balloonui 的 DuiControl 只有 AddChild（追加），没有 InsertChild，而
    // SetParent_ 是 protected、没法从子类对着别的对象调 —— 所以本批是先追加到
    // 末尾（父指针、host 挂载都由 AddChild 正确完成），再在 m_children
    // （protected，子类可动）里把这一段整体旋转到最前面。两步都合法，
    // 一行 balloonui 都不必碰。
    std::rotate(m_children.begin(),
                m_children.begin() + (std::ptrdiff_t)m_prependMarkCount,
                m_children.end());

    // 重排后取新的内容总高，差值即上方新增的高度。
    ForceLayout(GetRect());
    const int grew = m_contentHeight - m_prependMarkHeight;
    if (grew > 0)
    {
        // 上方长高了多少，滚动偏移就加多少 —— 视口里看到的内容位置不变。
        m_scrollY += grew;
        ForceLayout(GetRect());
    }
    Invalidate();
}

bool ChatMessageList::GetMessageByMsgid(long long msgid, ChatMessage& out) const
{
    if (msgid == 0)
    {
        return false;
    }
    const std::vector<std::unique_ptr<balloonwjui::DuiControl> >& kids = Children();
    for (size_t i = 0; i < kids.size(); ++i)
    {
        const ChatBubble* b = dynamic_cast<const ChatBubble*>(kids[i].get());
        if (b != NULL && b->Msgid() == msgid)
        {
            out = b->GetMessage();
            return true;
        }
    }
    return false;
}

void ChatMessageList::SetImageIdsByClientMsgId(const std::string& clientMsgId,
                                               const CString& fileid, const CString& md5)
{
    if (clientMsgId.empty())
    {
        return;
    }
    const std::vector<std::unique_ptr<balloonwjui::DuiControl> >& kids = Children();
    for (size_t i = 0; i < kids.size(); ++i)
    {
        ChatBubble* b = dynamic_cast<ChatBubble*>(kids[i].get());
        if (b != NULL && b->ClientMsgId() == clientMsgId)
        {
            //只补两个不参与绘制的字段：不置脏、不重排、不重绘（与 BackfillServerIds 同理）。
            b->SetImageIds(fileid, md5);
            return;
        }
    }
}

bool ChatMessageList::HasImageMsgid(long long msgid) const
{
    if (msgid == 0)
        return false;
    const std::vector<std::unique_ptr<balloonwjui::DuiControl> >& kids = Children();
    for (size_t i = 0; i < kids.size(); ++i)
    {
        ChatBubble* b = dynamic_cast<ChatBubble*>(kids[i].get());
        if (b != NULL && b->Msgid() == msgid)
        {
            return true;
        }
    }
    return false;
}

void ChatMessageList::ClearAllMessages()
{
    // 逐个移除并销毁子气泡（RemoveChild 立即销毁，ChatBubble 析构释放其图片位图）。
    while (!Children().empty())
    {
        RemoveChild(Children().back().get());
    }
    m_scrollY = 0;
    ForceLayout(GetRect());
    Invalidate();
}

void ChatMessageList::UpdateImageUploadProgress(const std::string& clientMsgId, int percent)
{
    if (clientMsgId.empty())
        return;
    const std::vector<std::unique_ptr<balloonwjui::DuiControl> >& kids = Children();
    for (size_t i = 0; i < kids.size(); ++i)
    {
        ChatBubble* b = dynamic_cast<ChatBubble*>(kids[i].get());
        if (b != NULL && b->ClientMsgId() == clientMsgId)
        {
            b->SetImageUploadProgress(percent);
            //气泡自身 Invalidate 不足以触发本列表离屏重新合成，须再让列表整体重绘
            //（与 AppendMessage / RequestRecallBubble 等改气泡后的处理一致）。
            Invalidate();
            return;
        }
    }
}

void ChatMessageList::MarkImageUploadFailed(const std::string& clientMsgId)
{
    if (clientMsgId.empty())
        return;
    const std::vector<std::unique_ptr<balloonwjui::DuiControl> >& kids = Children();
    for (size_t i = 0; i < kids.size(); ++i)
    {
        ChatBubble* b = dynamic_cast<ChatBubble*>(kids[i].get());
        if (b != NULL && b->ClientMsgId() == clientMsgId)
        {
            b->SetImageUploadFailed();
            Invalidate();
            return;
        }
    }
}

void ChatMessageList::MarkImageUploadDone(const std::string& clientMsgId)
{
    if (clientMsgId.empty())
        return;
    const std::vector<std::unique_ptr<balloonwjui::DuiControl> >& kids = Children();
    for (size_t i = 0; i < kids.size(); ++i)
    {
        ChatBubble* b = dynamic_cast<ChatBubble*>(kids[i].get());
        if (b != NULL && b->ClientMsgId() == clientMsgId)
        {
            b->SetImageUploadDone();
            Invalidate();
            return;
        }
    }
}

void ChatMessageList::SetImageDownloadedByMsgid(long long msgid, const CString& localPath)
{
    if (msgid == 0)
        return;
    const std::vector<std::unique_ptr<balloonwjui::DuiControl> >& kids = Children();
    for (size_t i = 0; i < kids.size(); ++i)
    {
        ChatBubble* b = dynamic_cast<ChatBubble*>(kids[i].get());
        if (b != NULL && b->Msgid() == msgid)
        {
            b->SetImageDownloaded(localPath);
            Invalidate();
            return;
        }
    }
}

void ChatMessageList::RefreshSenderAvatar(int64_t userId, const CString& imagePath)
{
    if (userId == 0)
        return;
    const std::vector<std::unique_ptr<balloonwjui::DuiControl> >& kids = Children();
    bool any = false;
    for (size_t i = 0; i < kids.size(); ++i)
    {
        ChatBubble* b = dynamic_cast<ChatBubble*>(kids[i].get());
        if (b != NULL && b->SenderUserId() == userId)
        {
            b->SetSenderAvatarImage(imagePath);   // 内部仅在路径变化时重解码 / 重绘
            any = true;
        }
    }
    if (any)
    {
        Invalidate();
    }
}

void ChatMessageList::MarkImageDownloadFailedByMsgid(long long msgid)
{
    if (msgid == 0)
        return;
    const std::vector<std::unique_ptr<balloonwjui::DuiControl> >& kids = Children();
    for (size_t i = 0; i < kids.size(); ++i)
    {
        ChatBubble* b = dynamic_cast<ChatBubble*>(kids[i].get());
        if (b != NULL && b->Msgid() == msgid)
        {
            b->SetImageDownloadFailed();
            Invalidate();
            return;
        }
    }
}

bool ChatMessageList::HasFileMsgid(long long msgid) const
{
    if (msgid == 0)
        return false;
    const std::vector<std::unique_ptr<balloonwjui::DuiControl> >& kids = Children();
    for (size_t i = 0; i < kids.size(); ++i)
    {
        ChatBubble* b = dynamic_cast<ChatBubble*>(kids[i].get());
        if (b != NULL && b->FileMsgid() == msgid)
        {
            return true;
        }
    }
    return false;
}

void ChatMessageList::UpdateFileUploadProgress(const std::string& clientMsgId, int percent)
{
    if (clientMsgId.empty())
        return;
    const std::vector<std::unique_ptr<balloonwjui::DuiControl> >& kids = Children();
    for (size_t i = 0; i < kids.size(); ++i)
    {
        ChatBubble* b = dynamic_cast<ChatBubble*>(kids[i].get());
        if (b != NULL && b->FileClientMsgId() == clientMsgId)
        {
            //整数百分比未变则不重绘（一秒内几十次分片回投多数落在同一百分比）。
            if (b->FileProgress() == percent)
                return;
            //同态进度更新不改气泡高度。Invalidate 只标脏、靠 WM_PAINT 异步重绘，而密集
            //进度更新会把低优先级的 WM_PAINT 一直挤掉、进度条冻住；故 UpdateWindow 强制
            //立刻同步泵出一次重绘（标准 Win32 即时重绘惯用法）。
            b->SetFileUploadProgress(percent);
            Invalidate();
            HWND host = HostHwnd();
            if (host != NULL)
                ::UpdateWindow(host);
            return;
        }
    }
}

void ChatMessageList::MarkFileUploadFailed(const std::string& clientMsgId)
{
    if (clientMsgId.empty())
        return;
    const std::vector<std::unique_ptr<balloonwjui::DuiControl> >& kids = Children();
    for (size_t i = 0; i < kids.size(); ++i)
    {
        ChatBubble* b = dynamic_cast<ChatBubble*>(kids[i].get());
        if (b != NULL && b->FileClientMsgId() == clientMsgId)
        {
            //状态切换可能改变气泡高度（按钮 / 进度条增减），须重排。
            b->SetFileUploadFailed();
            ForceLayout(GetRect());
            Invalidate();
            return;
        }
    }
}

void ChatMessageList::MarkFileUploadDone(const std::string& clientMsgId)
{
    if (clientMsgId.empty())
        return;
    const std::vector<std::unique_ptr<balloonwjui::DuiControl> >& kids = Children();
    for (size_t i = 0; i < kids.size(); ++i)
    {
        ChatBubble* b = dynamic_cast<ChatBubble*>(kids[i].get());
        if (b != NULL && b->FileClientMsgId() == clientMsgId)
        {
            b->SetFileUploadDone();
            ForceLayout(GetRect());
            Invalidate();
            return;
        }
    }
}

void ChatMessageList::MarkFileCancelledByClientMsgId(const std::string& clientMsgId)
{
    if (clientMsgId.empty())
        return;
    const std::vector<std::unique_ptr<balloonwjui::DuiControl> >& kids = Children();
    for (size_t i = 0; i < kids.size(); ++i)
    {
        ChatBubble* b = dynamic_cast<ChatBubble*>(kids[i].get());
        if (b != NULL && b->FileClientMsgId() == clientMsgId)
        {
            b->SetFileCancelled();
            ForceLayout(GetRect());
            Invalidate();
            return;
        }
    }
}

void ChatMessageList::UpdateFileDownloadProgress(long long msgid, int percent)
{
    if (msgid == 0)
        return;
    const std::vector<std::unique_ptr<balloonwjui::DuiControl> >& kids = Children();
    for (size_t i = 0; i < kids.size(); ++i)
    {
        ChatBubble* b = dynamic_cast<ChatBubble*>(kids[i].get());
        if (b != NULL && b->FileMsgid() == msgid)
        {
            //整数百分比未变则不重绘（见 UpdateFileUploadProgress 同处说明）。
            if (b->FileProgress() == percent)
                return;
            //强制同步重绘，避免密集进度更新下 WM_PAINT 被挤掉、进度条冻住。
            b->SetFileDownloadProgress(percent);
            Invalidate();
            HWND host = HostHwnd();
            if (host != NULL)
                ::UpdateWindow(host);
            return;
        }
    }
}

void ChatMessageList::SetFileDownloadedByMsgid(long long msgid, const CString& localPath)
{
    if (msgid == 0)
        return;
    const std::vector<std::unique_ptr<balloonwjui::DuiControl> >& kids = Children();
    for (size_t i = 0; i < kids.size(); ++i)
    {
        ChatBubble* b = dynamic_cast<ChatBubble*>(kids[i].get());
        if (b != NULL && b->FileMsgid() == msgid)
        {
            b->SetFileDownloaded(localPath);
            ForceLayout(GetRect());
            Invalidate();
            return;
        }
    }
}

void ChatMessageList::MarkFileDownloadFailedByMsgid(long long msgid)
{
    if (msgid == 0)
        return;
    const std::vector<std::unique_ptr<balloonwjui::DuiControl> >& kids = Children();
    for (size_t i = 0; i < kids.size(); ++i)
    {
        ChatBubble* b = dynamic_cast<ChatBubble*>(kids[i].get());
        if (b != NULL && b->FileMsgid() == msgid)
        {
            b->SetFileDownloadFailed();
            ForceLayout(GetRect());
            Invalidate();
            return;
        }
    }
}

void ChatMessageList::MarkFileCancelledByMsgid(long long msgid)
{
    if (msgid == 0)
        return;
    const std::vector<std::unique_ptr<balloonwjui::DuiControl> >& kids = Children();
    for (size_t i = 0; i < kids.size(); ++i)
    {
        ChatBubble* b = dynamic_cast<ChatBubble*>(kids[i].get());
        if (b != NULL && b->FileMsgid() == msgid)
        {
            b->SetFileCancelled();
            ForceLayout(GetRect());
            Invalidate();
            return;
        }
    }
}

void ChatMessageList::MarkFileUploadStartedByClientMsgId(const std::string& clientMsgId)
{
    if (clientMsgId.empty())
        return;
    const std::vector<std::unique_ptr<balloonwjui::DuiControl> >& kids = Children();
    for (size_t i = 0; i < kids.size(); ++i)
    {
        ChatBubble* b = dynamic_cast<ChatBubble*>(kids[i].get());
        if (b != NULL && b->FileClientMsgId() == clientMsgId)
        {
            b->SetFileUploadStarted();
            ForceLayout(GetRect());
            Invalidate();
            return;
        }
    }
}

void ChatMessageList::MarkFileRejectedByMsgid(long long msgid)
{
    if (msgid == 0)
        return;
    const std::vector<std::unique_ptr<balloonwjui::DuiControl> >& kids = Children();
    for (size_t i = 0; i < kids.size(); ++i)
    {
        ChatBubble* b = dynamic_cast<ChatBubble*>(kids[i].get());
        if (b != NULL && b->FileMsgid() == msgid)
        {
            b->SetFileRejected();
            ForceLayout(GetRect());
            Invalidate();
            return;
        }
    }
}

void ChatMessageList::MarkFileDownloadStartedByMsgid(long long msgid)
{
    if (msgid == 0)
        return;
    const std::vector<std::unique_ptr<balloonwjui::DuiControl> >& kids = Children();
    for (size_t i = 0; i < kids.size(); ++i)
    {
        ChatBubble* b = dynamic_cast<ChatBubble*>(kids[i].get());
        if (b != NULL && b->FileMsgid() == msgid)
        {
            b->SetFileDownloadStarted();
            ForceLayout(GetRect());
            Invalidate();
            return;
        }
    }
}

bool ChatMessageList::SetFilePeerStateByMsgid(long long msgid, int peerState)
{
    if (msgid == 0)
        return false;
    const std::vector<std::unique_ptr<balloonwjui::DuiControl> >& kids = Children();
    for (size_t i = 0; i < kids.size(); ++i)
    {
        ChatBubble* b = dynamic_cast<ChatBubble*>(kids[i].get());
        if (b != NULL && b->FileMsgid() == msgid)
        {
            //只改状态文字、不改气泡高度，故无需 ForceLayout，重绘即可
            b->SetFilePeerState(peerState);
            Invalidate();
            return true;
        }
    }
    return false;
}

void ChatMessageList::MarkFileDeletedByMsgid(long long msgid)
{
    if (msgid == 0)
        return;
    const std::vector<std::unique_ptr<balloonwjui::DuiControl> >& kids = Children();
    for (size_t i = 0; i < kids.size(); ++i)
    {
        ChatBubble* b = dynamic_cast<ChatBubble*>(kids[i].get());
        if (b != NULL && b->FileMsgid() == msgid)
        {
            b->SetFileDeleted();
            ForceLayout(GetRect());
            Invalidate();
            return;
        }
    }
}

void ChatMessageList::SetMultiSelectMode(bool on)
{
    if (m_multiSelect == on)
    {
        return;
    }
    m_multiSelect = on;

    // 退出多选：清空所有行的选中态。
    if (!m_multiSelect)
    {
        const std::vector<std::unique_ptr<balloonwjui::DuiControl> >& kids =
            Children();
        for (size_t i = 0; i < kids.size(); ++i)
        {
            ChatBubble* b = dynamic_cast<ChatBubble*>(kids[i].get());
            if (b != NULL)
            {
                b->SetSelected(false);
            }
        }
    }

    // 多选条出现 / 消失改变了消息行可视区，须整体重排。
    ForceLayout(GetRect());
    Invalidate();
}

void ChatMessageList::RequestDeleteBubble(ChatBubble* bubble)
{
    if (bubble == NULL)
    {
        return;
    }
    // RemoveChild 会立即销毁 bubble；调用方其后不得再访问它。
    RemoveChild(bubble);
    ForceLayout(GetRect());
    Invalidate();
}

void ChatMessageList::RequestRecallBubble(ChatBubble* bubble)
{
    if (bubble == NULL)
    {
        return;
    }

    //msgid 为 0 说明服务器还没给这条消息分配 ID，撤回请求必被拒；右键菜单本就不会
    //对这类行显示"撤回"，这里再挡一道。
    long long msgid = bubble->Msgid();
    if (msgid <= 0)
    {
        return;
    }

    //消息列表是纯 UI 控件、不直接碰网络：把 msgid 投给宿主聊天窗，由它连同自己的
    //ctype/cid 发 cmd_recall_message。堆上分配，宿主处理后 delete。
    balloonwjui::DuiHost* host = GetHost();
    HWND hwnd = (host != NULL) ? host->m_hWnd : NULL;
    if (hwnd == NULL)
    {
        return;
    }
    long long* pMsgid = new long long(msgid);
    if (!::PostMessage(hwnd, WM_CHAT_RECALL_REQUEST, 0, (LPARAM)pMsgid))
    {
        //投递失败（窗口已销毁）时自行释放，避免泄漏。
        delete pMsgid;
    }
}

void ChatMessageList::RequestPinBubble(ChatBubble* bubble, bool pin)
{
    if (bubble == NULL)
    {
        return;
    }

    //msgid 为 0 说明服务器还没给这条消息分配 ID，Pin 请求必被判 message_not_exist；
    //右键菜单本就不会对这类行显示该项，这里再挡一道。
    long long msgid = bubble->Msgid();
    if (msgid <= 0)
    {
        return;
    }

    //与撤回同一套路：消息列表是纯 UI 控件、不直接碰网络，把 msgid 与目标状态投给
    //宿主聊天窗，由它连同自己的 ctype/cid 发 cmd_pin_message。界面改动一律等服务端
    //推 cmd_message_pinned_notify 回来再做，本方法<u>不</u>就地改标记。
    balloonwjui::DuiHost* host = GetHost();
    HWND hwnd = (host != NULL) ? host->m_hWnd : NULL;
    if (hwnd == NULL)
    {
        return;
    }
    long long* pMsgid = new long long(msgid);
    if (!::PostMessage(hwnd, WM_CHAT_PIN_REQUEST, pin ? 1 : 0, (LPARAM)pMsgid))
    {
        //投递失败（窗口已销毁）时自行释放，避免泄漏。
        delete pMsgid;
    }
}

bool ChatMessageList::SetPinnedByMsgid(long long msgid, bool pinned)
{
    if (msgid <= 0)
    {
        return false;
    }

    const std::vector<std::unique_ptr<balloonwjui::DuiControl> >& kids = Children();
    for (size_t i = 0; i < kids.size(); ++i)
    {
        ChatBubble* b = dynamic_cast<ChatBubble*>(kids[i].get());
        if (b == NULL)
        {
            continue;
        }
        if (b->Msgid() != msgid)
        {
            continue;
        }

        //标记画在气泡外侧、不占宽度也不改行高，故只重绘、不重排。
        b->SetPinned(pinned);
        Invalidate();
        return true;
    }
    return false;
}

int ChatMessageList::CountPinned() const
{
    int count = 0;
    const std::vector<std::unique_ptr<balloonwjui::DuiControl> >& kids = Children();
    for (size_t i = 0; i < kids.size(); ++i)
    {
        const ChatBubble* b = dynamic_cast<const ChatBubble*>(kids[i].get());
        if (b != NULL && b->IsPinned())
        {
            ++count;
        }
    }
    return count;
}

bool ChatMessageList::BackfillSentMessage(const std::string& clientMsgId, long long msgid,
                                          long long timestampMs)
{
    if (clientMsgId.empty())
    {
        return false;
    }

    const std::vector<std::unique_ptr<balloonwjui::DuiControl> >& kids = Children();
    for (size_t i = 0; i < kids.size(); ++i)
    {
        ChatBubble* b = dynamic_cast<ChatBubble*>(kids[i].get());
        if (b == NULL)
        {
            continue;
        }

        //文本 / 图片气泡把客户端消息ID放在 clientMsgId，文件气泡放在 fileClientMsgId
        //——两者是各自独立的字段，只比前者会让文件气泡永远回填不上服务器 msgid，
        //之后一切按 msgid 定位本行的功能（对方收取状态、撤回）都会失灵。
        const ChatMessage& cur = b->GetMessage();
        const bool hit = (!cur.clientMsgId.empty() && cur.clientMsgId == clientMsgId)
                      || (!cur.fileClientMsgId.empty() && cur.fileClientMsgId == clientMsgId);
        if (!hit)
        {
            continue;
        }

        //只回填这两个字段，其余原样保留（文本 / 字体 / 头像等已在发送时填好）。
        //回填不改行高、也不改外观，故无需重排重绘 —— 正因如此这里<u>不能</u>走
        //SetMessage：它会清空选区模型并令折行测量缓存失效，而本处又不触发重排，
        //于是这一行在下次重排前都选不中文字。改用只动 msgid / 时间的轻量回填。
        b->BackfillServerIds(msgid, timestampMs);
        return true;
    }

    return false;
}

bool ChatMessageList::RecallMessageByMsgid(long long msgid, const CString& tipText)
{
    if (msgid <= 0)
    {
        return false;
    }

    const std::vector<std::unique_ptr<balloonwjui::DuiControl> >& kids = Children();
    for (size_t i = 0; i < kids.size(); ++i)
    {
        ChatBubble* b = dynamic_cast<ChatBubble*>(kids[i].get());
        if (b == NULL)
        {
            continue;
        }
        //重连补推同一条撤回通知时会再次走到这里：RecallToTip 是幂等的（同一条 msgid
        //只会被写成同一句提示文案），故无需额外判断本行是否已经是提示行。
        if (b->Msgid() != msgid)
        {
            continue;
        }

        b->RecallToTip(tipText);
        //撤回后该行高度变化（变为时间提示行高），须重排。
        ForceLayout(GetRect());
        Invalidate();
        return true;
    }

    return false;
}

void ChatMessageList::EnterMultiSelectWith(ChatBubble* bubble)
{
    m_multiSelect = true;

    // 仅预选中作为起点的那一条。
    const std::vector<std::unique_ptr<balloonwjui::DuiControl> >& kids = Children();
    for (size_t i = 0; i < kids.size(); ++i)
    {
        ChatBubble* b = dynamic_cast<ChatBubble*>(kids[i].get());
        if (b != NULL)
        {
            b->SetSelected(b == bubble);
        }
    }

    ForceLayout(GetRect());
    Invalidate();
}

void ChatMessageList::RequestForwardBubble(ChatBubble* /*bubble*/)
{
    // mock 阶段：真实转发（选联系人 / 发送）待接入，先给占位提示。
    ::MessageBoxW(HostHwnd(),
                  L"转发功能正在开发中（当前为 mock 演示）。",
                  L"FlamingoIM", MB_OK | MB_ICONINFORMATION);
}

void ChatMessageList::RequestReplyBubble(ChatBubble* bubble)
{
    if (bubble == NULL)
    {
        return;
    }

    // 没有服务器 msgid 说明这条还没落库（本端刚发出、回执未到）。引用它，对端拿到的
    // msgid 会是 0，永远定位不到原消息 —— 与其发出一条点不动的引用，不如此刻不让引用。
    const long long msgid = bubble->Msgid();
    if (msgid == 0)
    {
        return;
    }

    HWND hwnd = HostHwnd();
    if (hwnd == NULL)
    {
        return;
    }

    // 本控件不认识"会话是谁""发送人叫什么"这些业务信息，组装引用快照要用到它们，
    // 故只把 msgid 投给宿主聊天窗，由它按自己的会话上下文组装并在输入区挂出引用条。
    // 与撤回请求（WM_CHAT_RECALL_REQUEST）同一套路子。
    long long* pMsgid = new long long(msgid);
    if (!::PostMessage(hwnd, WM_CHAT_REPLY_REQUEST, 0, (LPARAM)pMsgid))
    {
        delete pMsgid;
    }
}

void ChatMessageList::NotifySelectionChanged()
{
    // 选中数变化 —— 重绘以刷新底部操作条上的计数。
    Invalidate();
}

void ChatMessageList::ClearTextSelectionExcept(ChatBubble* keep)
{
    const std::vector<std::unique_ptr<balloonwjui::DuiControl> >& kids = Children();
    for (size_t i = 0; i < kids.size(); ++i)
    {
        ChatBubble* b = dynamic_cast<ChatBubble*>(kids[i].get());
        if (b != NULL && b != keep)
        {
            b->ClearTextSelection();   // 内部自判"本就没有选区"时不重绘
        }
    }
}

CString ChatMessageList::GetSelectedText() const
{
    const std::vector<std::unique_ptr<balloonwjui::DuiControl> >& kids = Children();
    for (size_t i = 0; i < kids.size(); ++i)
    {
        const ChatBubble* b = dynamic_cast<const ChatBubble*>(kids[i].get());
        if (b != NULL && b->HasTextSelection())
        {
            return b->GetSelectedText();   // 同一时刻只会有一个气泡有选区
        }
    }
    return CString();
}

void ChatMessageList::Layout(const RECT& rcAvail)
{
    m_rcItem = rcAvail;

    // 消息行可视区（扣掉多选操作条）。
    RECT rowsRect = RowsViewRect();
    m_viewHeight = rowsRect.bottom - rowsRect.top;
    if (m_viewHeight < 0)
    {
        m_viewHeight = 0;
    }

    const std::vector<std::unique_ptr<balloonwjui::DuiControl> >& kids = Children();
    const int availWidth = rcAvail.right - rcAvail.left;

    // 先对每行按可用宽度测量折行布局，再累加行高得到内容总高。
    int total = 0;
    for (size_t i = 0; i < kids.size(); ++i)
    {
        ChatBubble* b = dynamic_cast<ChatBubble*>(kids[i].get());
        if (b != NULL)
        {
            b->Measure(availWidth);
            total += b->GetRowHeight();
        }
    }
    m_contentHeight = total;

    // 夹紧滚动偏移。
    int maxScroll = m_contentHeight - m_viewHeight;
    if (maxScroll < 0) { maxScroll = 0; }
    if (m_scrollY > maxScroll) { m_scrollY = maxScroll; }
    if (m_scrollY < 0)         { m_scrollY = 0; }

    // 自上而下定位每行（含滚动偏移）。
    int y = rowsRect.top - m_scrollY;
    for (size_t i = 0; i < kids.size(); ++i)
    {
        ChatBubble* b = dynamic_cast<ChatBubble*>(kids[i].get());
        const int hgt = (b != NULL) ? b->GetRowHeight() : 0;
        RECT r = { rowsRect.left, y, rowsRect.right, y + hgt };
        kids[i]->SetRect(r);
        y += hgt;
    }

}

void ChatMessageList::OnPaint(HDC hdc, const RECT& /*rcDirty*/)
{
    const RECT rowsRect = RowsViewRect();
    const int  rowsW = rowsRect.right - rowsRect.left;
    const int  rowsH = rowsRect.bottom - rowsRect.top;

    // ---- 用离屏位图合成消息行 ----
    // 气泡用 GDI+ 自绘，而 GDI 的 IntersectClipRect 约束不住 GDI+ 绘制；
    // 滚动时跨越视口上 / 下边界的气泡会溢出到头部 / 输入区（见 32.png）。
    // 解法：把气泡按绝对坐标画进一张覆盖 [客户区(0,0)..列表右下角] 的
    // 离屏位图 —— 位图的物理边界天然兜住所有绘制 —— 再只把"消息行
    // 可视区"那一条 BitBlt 回 hdc，行区以外的像素永不被搬运。
    const int memW = m_rcItem.right;
    const int memH = m_rcItem.bottom;
    bool composed = false;
    if (memW > 0 && memH > 0 && rowsW > 0 && rowsH > 0)
    {
        EnsureOffscreen(hdc, memW, memH);
        if (m_memDC != NULL)
        {
            // 行区先铺暖白底（气泡抗锯齿边缘据此混色）。
            HBRUSH memBg = ::CreateSolidBrush(RGB(0xFA, 0xF9, 0xF6));
            ::FillRect(m_memDC, &rowsRect, memBg);
            ::DeleteObject(memBg);

            // 把与行区相交的气泡按绝对坐标画进离屏位图。
            const std::vector<std::unique_ptr<balloonwjui::DuiControl> >& kids =
                Children();
            for (size_t i = 0; i < kids.size(); ++i)
            {
                balloonwjui::DuiControl* child = kids[i].get();
                if (child == NULL || !child->IsVisible())
                {
                    continue;
                }
                RECT rcChild = child->GetRect();
                RECT rcInter;
                if (!::IntersectRect(&rcInter, &rcChild, &rowsRect))
                {
                    continue;   // 整行滚出可视区
                }
                child->OnPaint(m_memDC, rcInter);
            }

            // 只把消息行可视区那一条搬回 hdc：行区以外不搬运 → 气泡
            // 无从溢出。BitBlt 同时也遵守 hdc 自身的裁剪区。
            ::BitBlt(hdc, rowsRect.left, rowsRect.top, rowsW, rowsH,
                     m_memDC, rowsRect.left, rowsRect.top, SRCCOPY);
            composed = true;
        }
    }

    if (!composed)
    {
        // 退化路径（离屏缓冲创建失败的极端情形）：至少把行区底色铺上。
        HBRUSH bg = ::CreateSolidBrush(RGB(0xFA, 0xF9, 0xF6));
        ::FillRect(hdc, &rowsRect, bg);
        ::DeleteObject(bg);
    }

    // 顶部 1px 分隔线（头部下边框）+ 消息行区底部 1px 分隔线。
    HBRUSH sep = ::CreateSolidBrush(RGB(0xEC, 0xEA, 0xE6));
    RECT topLine = { m_rcItem.left, m_rcItem.top,
                     m_rcItem.right, m_rcItem.top + 1 };
    RECT botLine = { m_rcItem.left, rowsRect.bottom - 1,
                     m_rcItem.right, rowsRect.bottom };
    ::FillRect(hdc, &topLine, sep);
    ::FillRect(hdc, &botLine, sep);
    ::DeleteObject(sep);

    // 内容溢出时，右侧画一条细滚动条（限定在消息行可视区内）。
    if (m_contentHeight > m_viewHeight && m_viewHeight > 0)
    {
        int thumbH = (int)((LONGLONG)m_viewHeight * m_viewHeight / m_contentHeight);
        if (thumbH < kBarMinThumb) { thumbH = kBarMinThumb; }
        if (thumbH > m_viewHeight) { thumbH = m_viewHeight; }

        const int maxScroll = m_contentHeight - m_viewHeight;
        int thumbY = rowsRect.top;
        if (maxScroll > 0)
        {
            thumbY += (int)((LONGLONG)m_scrollY * (m_viewHeight - thumbH) / maxScroll);
        }
        const int thumbX = m_rcItem.right - kBarWidth - kBarMargin;

        Gdiplus::Graphics g(hdc);
        g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        Gdiplus::GraphicsPath path;
        const Gdiplus::REAL r = (Gdiplus::REAL)kBarWidth / 2.0f;
        const Gdiplus::REAL d = r * 2.0f;
        path.AddArc((Gdiplus::REAL)thumbX, (Gdiplus::REAL)thumbY, d, d,
                    180.0f, 180.0f);
        path.AddArc((Gdiplus::REAL)thumbX, (Gdiplus::REAL)(thumbY + thumbH) - d,
                    d, d, 0.0f, 180.0f);
        path.CloseFigure();
        Gdiplus::SolidBrush brush(kThumbColor);
        g.FillPath(&brush, &path);
    }

    // ---- 多选操作条 ----
    if (m_multiSelect)
    {
        const RECT bar = MultiSelectBarRect();
        HBRUSH white = ::CreateSolidBrush(RGB(255, 255, 255));
        ::FillRect(hdc, &bar, white);
        ::DeleteObject(white);

        const int n = SelectedCount();
        const int third = (bar.right - bar.left) / 3;

        // 取消（左）。
        RECT cCancel = { bar.left, bar.top, bar.left + third, bar.bottom };
        DrawBarText(hdc, L"取消", cCancel, kBarCancelColor);

        // 转发 N（中）。
        CString fwd;
        fwd.Format(_T("转发 %d"), n);
        RECT cForward = { bar.left + third, bar.top,
                          bar.left + third * 2, bar.bottom };
        DrawBarText(hdc, (LPCWSTR)fwd, cForward,
                    (n > 0) ? kBarForwardColor : kBarDisabledColor);

        // 删除 N（右）。
        CString del;
        del.Format(_T("删除 %d"), n);
        RECT cDelete = { bar.left + third * 2, bar.top, bar.right, bar.bottom };
        DrawBarText(hdc, (LPCWSTR)del, cDelete,
                    (n > 0) ? kBarDeleteColor : kBarDisabledColor);
    }
}

bool ChatMessageList::OnMouseWheel(POINT /*pt*/, short zDelta, UINT /*mkFlags*/)
{
    const int maxScroll = (m_contentHeight > m_viewHeight)
                          ? (m_contentHeight - m_viewHeight) : 0;
    if (maxScroll <= 0)
    {
        return true;   // 无需滚动，但仍消费滚轮
    }

    int newScroll = m_scrollY - (zDelta / WHEEL_DELTA) * kWheelStep;
    if (newScroll > maxScroll) { newScroll = maxScroll; }
    if (newScroll < 0)         { newScroll = 0; }

    if (newScroll != m_scrollY)
    {
        m_scrollY = newScroll;
        ForceLayout(GetRect());
        Invalidate();
        NotifyMaybeLoadOlder();
    }
    return true;
}

void ChatMessageList::NotifyMaybeLoadOlder()
{
    // 只在<u>接近顶部</u>时才惊动宿主，免得每一格滚轮都投一条消息。真正"要不要拉"
    // 的判定（有没有在途、还有没有更早的、锚点是什么）在宿主那边用
    // ShouldLoadOlderHistory 做 —— 那些状态本控件不持有，也不该持有。
    if (m_scrollY > kHistoryLoadTriggerY)
    {
        return;
    }
    HWND hwnd = HostHwnd();
    if (hwnd != NULL)
    {
        ::PostMessage(hwnd, WM_CHAT_LOAD_OLDER, 0, 0);
    }
}

RECT ChatMessageList::ThumbRect() const
{
    RECT empty = { 0, 0, 0, 0 };
    if (m_contentHeight <= m_viewHeight || m_viewHeight <= 0)
        return empty;

    RECT rows = RowsViewRect();
    int thumbH = (int)((LONGLONG)m_viewHeight * m_viewHeight / m_contentHeight);
    if (thumbH < kBarMinThumb)
        thumbH = kBarMinThumb;
    if (thumbH > m_viewHeight)
        thumbH = m_viewHeight;

    int maxScroll = m_contentHeight - m_viewHeight;
    int thumbY = rows.top;
    if (maxScroll > 0)
        thumbY += (int)((LONGLONG)m_scrollY * (m_viewHeight - thumbH) / maxScroll);

    int thumbX = m_rcItem.right - kBarWidth - kBarMargin;
    RECT r = { thumbX, thumbY, thumbX + kBarWidth, thumbY + thumbH };
    return r;
}

bool ChatMessageList::HitScrollBarBand(POINT pt) const
{
    const RECT thumb = ThumbRect();
    if (thumb.bottom <= thumb.top)
    {
        return false;   // 内容没溢出，压根没有滚动条
    }
    const RECT rows = RowsViewRect();
    // 命中带：滑块所在竖直窄带（左右各放宽 kBarMargin 便于点中），且纵向落在消息区内。
    const bool inBand  = (pt.x >= thumb.left - kBarMargin && pt.x <= thumb.right + kBarMargin);
    const bool inRowsY = (pt.y >= rows.top && pt.y < rows.bottom);
    return inBand && inRowsY;
}

balloonwjui::DuiControl* ChatMessageList::HitTest(POINT ptHostClient)
{
    // 先按基类规则确认落点确实落在本控件上（不可见 / 在矩形外时不参与命中），
    // 再判断是不是落在滚动条命中带里 —— 是则截下，不再下探到消息行。
    balloonwjui::DuiControl* hit = balloonwjui::DuiControl::HitTest(ptHostClient);
    if (hit != NULL && HitScrollBarBand(ptHostClient))
    {
        return this;
    }
    return hit;
}

void ChatMessageList::ScrollToThumbTop(int thumbTopY)
{
    if (m_contentHeight <= m_viewHeight || m_viewHeight <= 0)
        return;

    RECT rows = RowsViewRect();
    int thumbH = (int)((LONGLONG)m_viewHeight * m_viewHeight / m_contentHeight);
    if (thumbH < kBarMinThumb)
        thumbH = kBarMinThumb;
    if (thumbH > m_viewHeight)
        thumbH = m_viewHeight;

    int track     = m_viewHeight - thumbH;        // 滑块可移动的像素范围
    int maxScroll = m_contentHeight - m_viewHeight;
    int rel       = thumbTopY - rows.top;
    if (rel < 0)
        rel = 0;
    if (rel > track)
        rel = track;

    int newScroll = (track > 0) ? (int)((LONGLONG)rel * maxScroll / track) : 0;
    if (newScroll < 0)
        newScroll = 0;
    if (newScroll > maxScroll)
        newScroll = maxScroll;

    if (newScroll != m_scrollY)
    {
        m_scrollY = newScroll;
        ForceLayout(GetRect());
        Invalidate();
    }
}

bool ChatMessageList::OnMouseMove(POINT pt, UINT /*mkFlags*/)
{
    if (m_thumbDragging)
    {
        // 拖动中：把滑块顶部移到“鼠标 y - 抓点偏移”，反推滚动位置。
        ScrollToThumbTop(pt.y - m_thumbDragOffsetY);
        return true;
    }
    return false;
}

bool ChatMessageList::OnLButtonUp(POINT /*pt*/, UINT /*mkFlags*/)
{
    if (m_thumbDragging)
    {
        m_thumbDragging = false;
        ReleaseCapture();
        return true;
    }
    return false;
}

bool ChatMessageList::OnLButtonDown(POINT pt, UINT /*mkFlags*/)
{
    // 先看是否点在滚动条滑块/轨道上 —— 命中则开始拖动；消费鼠标避免误触发窗口拖拽。
    // 命中带的判断与 HitTest 共用 HitScrollBarBand，两处口径必须一致：HitTest 已把
    // 该带内的点击截给本控件，这里若判得更严就会出现"截下来却没人处理"的空档。
    if (HitScrollBarBand(pt))
    {
        const RECT thumb = ThumbRect();
        if (pt.y >= thumb.top && pt.y < thumb.bottom)
        {
            // 抓在滑块上：记住抓点相对滑块顶部的偏移，拖动时保持不跳。
            m_thumbDragOffsetY = pt.y - thumb.top;
        }
        else
        {
            // 点在轨道空白处：先让滑块中心跳到点击处，再按住继续拖。
            m_thumbDragOffsetY = (thumb.bottom - thumb.top) / 2;
            ScrollToThumbTop(pt.y - m_thumbDragOffsetY);
        }
        m_thumbDragging = true;
        Capture();
        return true;
    }

    // 仅多选模式下处理 —— 命中底部操作条时执行对应动作。
    if (!m_multiSelect)
    {
        return false;
    }
    const RECT bar = MultiSelectBarRect();
    const bool inBar = (pt.x >= bar.left && pt.x < bar.right
                        && pt.y >= bar.top && pt.y < bar.bottom);
    if (!inBar)
    {
        return false;
    }

    const int third = (bar.right - bar.left) / 3;
    const int rel = pt.x - bar.left;

    // 左 1/3：取消多选。
    if (rel < third)
    {
        SetMultiSelectMode(false);
        return true;
    }

    const int n = SelectedCount();

    // 中 1/3：转发所选（选中数为 0 时无动作）。
    if (rel < third * 2)
    {
        if (n > 0)
        {
            RequestForwardBubble(NULL);
            SetMultiSelectMode(false);
        }
        return true;
    }

    // 右 1/3：删除所选（选中数为 0 时无动作）。
    if (n > 0)
    {
        // 先收集选中行指针，再逐个移除（RemoveChild 会改动子控件容器，
        // 不能在遍历 Children() 的过程中直接删）。
        std::vector<ChatBubble*> selected;
        const std::vector<std::unique_ptr<balloonwjui::DuiControl> >& kids =
            Children();
        for (size_t i = 0; i < kids.size(); ++i)
        {
            ChatBubble* b = dynamic_cast<ChatBubble*>(kids[i].get());
            if (b != NULL && b->IsSelected())
            {
                selected.push_back(b);
            }
        }
        for (size_t i = 0; i < selected.size(); ++i)
        {
            RemoveChild(selected[i]);
        }
        // 退出多选并重排（SetMultiSelectMode 内含 ForceLayout）。
        SetMultiSelectMode(false);
    }
    return true;
}

} // namespace balloonchatwnd
