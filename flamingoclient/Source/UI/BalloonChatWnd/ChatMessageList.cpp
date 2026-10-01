/**
 *  ChatMessageList.cpp
 *
 *  单聊窗消息区的实现。自上而下堆叠变高的 ChatBubble 行，鼠标滚轮滚动，
 *  右侧画细滚动条。每个气泡绘制时单独设一次裁剪区，确保滚动时跨越视口
 *  边界的气泡（含彩色 emoji）不会画到头部 / 标题栏 / 输入区上。
 *
 *  进入多选模式后，底部让出一条操作条（取消 / 逐条转发 N / 合并转发 N / 删除 N）。
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
#define _RICHEDIT_VER   0x0300      // RichEdit 3.0，WTL 10 要求的下限（窗口类仍为 RichEdit20W）
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
#include <set>         // RemoveMessagesByMsgid：待移除 msgid 的查找表
#include <algorithm>   // std::rotate —— PrependMessages 把新追加的一段挪到列表头部

#include <gdiplus.h>
#pragma comment(lib, "gdiplus.lib")

#include "ChatMessageList.h"
#include "ChatScrollThumb.h"   // 滚动条的尺寸与颜色（与群聊窗成员侧栏共用）
#include "ChatBubble.h"
#include "ChatHistoryPaging.h"   // kHistoryLoadTriggerY（滚到多近算"到顶了"）
#include "ChatTimeSeparator.h"    // 时间分隔行的判定与文案（何时插、插什么字）
#include "ChatClipboardIo.h"      // Ctrl+C 时写入纯文字 + Flamingo 格式
#include "ChatMessageMenu.h"      // CanForwardMessage / ForwardMsgidOf（多选转发时判定与取 msgid）
#include <time.h>                // time（取当前时刻，用于"今天 / 昨天"的判定）
#include "../../App/CustomMsgDef.h"   // WM_CHAT_RECALL_REQUEST（撤回请求转交宿主聊天窗）
#include "../../../third-party/balloonui/balloonui/DuiHost.h"
#include "../../../third-party/balloonui/balloonui/DuiTrace.h"   // BUI_TRACE：滚轮与绘制耗时的时序跟踪
#include "../../../third-party/balloonui/balloonui/DuiAnimation.h"   // DuiAnim / DuiAnimMgr：平滑滚动的逐帧驱动
#include "../../Common/Util/PerfStopWatch.h"                      // 滚轮排版、列表绘制的计时

namespace balloonchatwnd {

namespace {

// 一个滚轮刻度滚动的像素数。
const int kWheelStep = 64;
// 滚动条的几何计算、绘制、拖动换算与命中判断都用 ChatScrollThumb.h 的公共函数，与群聊窗成员
// 侧栏、聊天输入框同一份实现，外观与手感因此完全一致；改尺寸或颜色请改那一份。

// 一个用于"滚到底部"的足够大的初值（Layout 时会夹紧到真实最大值）。
const int kScrollToBottomSentinel = 0x3FFFFFF;

// 多选操作条高度（逻辑像素）。
const int kMultiSelectBarH = 48;

// 多选操作条文字色。
const COLORREF kBarCancelColor  = RGB(0x6B, 0x6B, 0x6B);  // "取消"
const COLORREF kBarForwardColor = RGB(0xFF, 0x7A, 0x45);  // "逐条转发 N" 与 "合并转发 N"
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

/**
 *  平滑滚动的逐帧驱动：挂在 balloonui 的 DuiAnimMgr 上，由它的 60Hz 脉冲逐帧调用。
 *
 *  它不自己算位置，只是在每一帧让消息列表按平滑滚动的曲线推进一次。位置按高精度时钟
 *  （PerfNowMs）计算，与 DuiAnimMgr 传进来的进度无关 —— 后者基于系统毫秒计数，约 15.6
 *  毫秒才跳一次，拿它算位置会有帧原地不动。
 *
 *  一段驱动的时长等于一次动画的时长；期间又收到滚轮、动画被延长时，由完成回调再挂一段
 *  （见 ChatMessageList::OnScrollTickerDone）。列表先于驱动销毁时，经主人令牌得知，
 *  什么都不做。
 */
class ChatScrollTicker : public balloonwjui::DuiAnim
{
public:
    // token：与消息列表共用的主人令牌（共享所有权）。
    explicit ChatScrollTicker(const std::shared_ptr<ChatScrollAnimToken>& token)
        : balloonwjui::DuiAnim(kChatSmoothScrollDurationMs)
        , m_token(token)
    {
    }

protected:
    // DuiAnim 覆写：每一帧推进一次列表的平滑滚动。传入的进度不用，理由见类说明。
    void OnTick(double /*tEased*/) override
    {
        if (m_token->m_owner != NULL)
        {
            m_token->m_owner->AdvanceSmoothScroll(PerfNowMs());
        }
    }

private:
    std::shared_ptr<ChatScrollAnimToken> m_token;   // 主人令牌，与消息列表共享
};

/**
 *  逐帧驱动走完一段时的回调（交给 DuiAnim::SetOnComplete）。写成函数对象而不是 lambda，
 *  遵循仓库「尽量不使用 lambda」的约定。
 */
struct ChatScrollTickerDone
{
    std::shared_ptr<ChatScrollAnimToken> m_token;   // 主人令牌，与消息列表共享

    // 列表仍在时通知它这一段驱动已结束，由它决定要不要再挂一段。
    void operator()() const
    {
        if (m_token && m_token->m_owner != NULL)
        {
            m_token->m_owner->OnScrollTickerDone();
        }
    }
};

ChatMessageList::ChatMessageList()
    : m_scrollY(0)
    , m_smooth()
    , m_scrollAnimToken(std::make_shared<ChatScrollAnimToken>())
    , m_scrollTickerRunning(false)
    , m_contentHeight(0)
    , m_viewHeight(0)
    , m_multiSelect(false)
    , m_readReceiptEnabled(false)
    , m_peerReadMsgid(0)
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
    , m_starredQuery(NULL)
    , m_starredQueryUd(NULL)
    , m_myNetRole(-1)
    , m_prependActive(false)
    , m_prependMarkCount(0)
    , m_prependMarkHeight(0)
    , m_lastMsgTimestampMs(0)
    , m_prependSavedLastMs(0)
{
    m_scrollAnimToken->m_owner = this;
}

void ChatMessageList::RefreshAllRows()
{
    const std::vector<std::unique_ptr<balloonwjui::DuiControl> >& kids = Children();
    for (size_t i = 0; i < kids.size(); ++i)
    {
        ChatBubble* b = dynamic_cast<ChatBubble*>(kids[i].get());
        if (b != NULL)
        {
            b->InvalidateMeasureCache();
        }
    }
    ForceLayout(GetRect());
    Invalidate();
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

void ChatMessageList::SetStarredQuery(StarredQuery fn, void* userdata)
{
    m_starredQuery   = fn;
    m_starredQueryUd = userdata;
}

bool ChatMessageList::IsStarred(long long uid) const
{
    if (m_starredQuery == NULL)
    {
        return false;   // 无从查证（单聊窗 / 调试入口），按"未关注"显示
    }
    return m_starredQuery(m_starredQueryUd, uid);
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
    //逐帧驱动可能还挂在 DuiAnimMgr 上：把令牌里的主人置空，它下次触发时就不会再访问本对象
    m_scrollAnimToken->m_owner = NULL;
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

void ChatMessageList::RequestScrollToBottom()
{
    // 直接改滚动位置：取消进行中的平滑滚动，否则它的下一帧会把位置拉回去。
    m_smooth.Cancel();
    // 置一个足够大的值；随后的 Layout 会夹紧到真实的最大滚动量。
    m_scrollY = kScrollToBottomSentinel;
}

void ChatMessageList::ScrollToBottom()
{
    RequestScrollToBottom();
    // 当场排一次版，把占位值夹紧成真实的滚动位置。原先只请求重画、等"下一次 Layout"：聊天窗
    // 渲染完首屏历史后调用本函数，之后并没有排版，滚动位置一直停在占位值上，滑块按占位值算到了
    // 可视区之外，表现为刚打开聊天窗时消息区没有滚动条，滚动一下才出现（2026-10-01 修复）
    if (m_rcItem.right > m_rcItem.left && m_rcItem.bottom > m_rcItem.top)
    {
        ForceLayout(m_rcItem);
    }
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

    // 直接改滚动位置：取消进行中的平滑滚动，理由同 ScrollToBottom。
    m_smooth.Cancel();

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
    // 需要的话，先插一行居中的时间分隔（形态与口径见 docs/聊天消息时间显示方案.md）。
    //
    // 放在这个漏斗里而不是各调用点：实时收发、向上翻页（批次期间照常调本函数）、
    // 单聊窗与群聊窗，全都从这里过，插一处即可，不会出现"某条路径忘了插"。
    //
    // 分隔行自身是 kMsgTime，不参与判定，否则会被自己的时间反复触发。
    if (msg.kind != kMsgTime)
    {
        const long long nowMs = (long long)::time(NULL) * 1000LL;
        if (ShouldInsertTimeSeparator(m_lastMsgTimestampMs, msg.timestampMs,
                                      kChatTimeSeparatorThresholdMs))
        {
            const CString timeText = FormatChatSeparatorTime(msg.timestampMs, nowMs);
            // 文案为空说明时间戳不可用，这一行就不插了 —— 插一行空白比不插更费解。
            if (!timeText.IsEmpty())
            {
                ChatMessage sep;
                sep.kind   = kMsgTime;
                sep.fromMe = false;      // kMsgTime 忽略此项，给个确定值免得读到未初始化
                sep.text   = timeText;

                std::unique_ptr<ChatBubble> sepBubble(new ChatBubble());
                sepBubble->SetMessage(sep);
                AddChild(std::unique_ptr<balloonwjui::DuiControl>(sepBubble.release()));
            }
        }
        // 时间未知（0）的消息不更新基准：否则后面一条正常消息会与 0 相比，
        // 被判成"没有上一条"而多插一行。
        if (msg.timestampMs > 0)
        {
            m_lastMsgTimestampMs = msg.timestampMs;
        }
    }

    std::unique_ptr<ChatBubble> bubble(new ChatBubble());
    bubble->SetReadReceiptEnabled(m_readReceiptEnabled);
    bubble->SetMessage(msg);
    // 对方的已读位点先于本行到达（例如先收到已读推送、后翻页加载到这条历史消息）：
    // 追加时就按位点判定，不必等下一次推送。
    if (msg.fromMe && msg.msgid > 0 && msg.msgid <= m_peerReadMsgid)
    {
        bubble->MarkPeerRead();
    }
    AddChild(std::unique_ptr<balloonwjui::DuiControl>(bubble.release()));

    // 新增行后重新堆叠，并把视图滚到底部显示这条新消息。
    // 但向上翻页的批次期间不能滚 —— 那时用户正盯着某处看，滚到底等于把他甩走；
    // 滚动位置由 EndPrependBatch 统一按新增高度补偿。
    if (!m_prependActive)
    {
        //紧接着就要排版，这里只置占位值，由下面这次排版夹紧，免得每追加一条排两遍
        RequestScrollToBottom();
    }
    ForceLayout(GetRect());
    Invalidate();
}

void ChatMessageList::SetReadReceiptEnabled(bool b)
{
    if (m_readReceiptEnabled == b)
    {
        return;
    }
    m_readReceiptEnabled = b;

    // 已有的行一并改掉：开关影响自己消息的行高，改完要重排。
    const std::vector<std::unique_ptr<balloonwjui::DuiControl> >& kids = Children();
    for (size_t i = 0; i < kids.size(); ++i)
    {
        ChatBubble* bubble = dynamic_cast<ChatBubble*>(kids[i].get());
        if (bubble != NULL)
        {
            bubble->SetReadReceiptEnabled(b);
        }
    }
    ForceLayout(GetRect());
    Invalidate();
}

int ChatMessageList::ApplyPeerReadPosition(long long readMsgid)
{
    // 位点只增不减：更小或相同的位点（乱序到达的旧推送、重复推送）不会带来任何新的已读。
    if (readMsgid <= 0 || readMsgid <= m_peerReadMsgid)
    {
        return 0;
    }
    m_peerReadMsgid = readMsgid;

    int marked = 0;
    const std::vector<std::unique_ptr<balloonwjui::DuiControl> >& kids = Children();
    for (size_t i = 0; i < kids.size(); ++i)
    {
        ChatBubble* bubble = dynamic_cast<ChatBubble*>(kids[i].get());
        if (bubble == NULL)
        {
            continue;
        }
        const ChatMessage& msg = bubble->GetMessage();
        // 只看自己发出、已拿到 msgid 的消息；尚未拿到 msgid 的等回填时再判定。
        if (!msg.fromMe || msg.msgid <= 0 || msg.msgid > readMsgid)
        {
            continue;
        }
        if (bubble->MarkPeerRead())
        {
            ++marked;
        }
    }
    return marked;
}

void ChatMessageList::BeginPrependBatch()
{
    // 先排一次，拿到批次开始时的准确内容总高（下面要用它算"上方长高了多少"）。
    ForceLayout(GetRect());
    m_prependMarkCount  = Children().size();
    m_prependMarkHeight = m_contentHeight;

    // 时间分隔行的基准要临时换一套：本批消息最终会被挪到列表<u>最前面</u>，其第一条
    // 就是整个消息流的开头，理应带一行时间；若沿用实时消息那条基准，会拿它去和末尾
    // 最新消息的时间相比，判出来的结果没有意义。故先存下原基准再清零，收尾时还原。
    m_prependSavedLastMs = m_lastMsgTimestampMs;
    m_lastMsgTimestampMs = 0;
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
        m_lastMsgTimestampMs = m_prependSavedLastMs;   // 基准原样还回去
        return;   // 本批一行都没追加（服务端返回空页）
    }

    // 补新旧交界处的时间分隔行。
    //
    // 本批消息之间的分隔行在 AppendMessage 里已经逐条判过了，唯独"本批最后一条"与
    // "翻页前原本的第一条"之间没人判过 —— 那两条在旋转后才相邻。不补的话，翻上去
    // 会看到交界处凭空缺一行。
    //
    // 此刻本批还挂在末尾，现在追加的行旋转后正好落在整批的最后、也就是原第一条之前，
    // 位置天然正确，不必额外挪动。
    if (m_prependMarkCount > 0)
    {
        ChatBubble* oldHead = dynamic_cast<ChatBubble*>(m_children[0].get());
        // 原第一条本身若已是分隔行，说明那个位置早有时间了，不必再补一行。
        if (oldHead != NULL && oldHead->GetMessage().kind != kMsgTime)
        {
            const long long oldHeadMs = oldHead->GetMessage().timestampMs;
            if (ShouldInsertTimeSeparator(m_lastMsgTimestampMs, oldHeadMs,
                                          kChatTimeSeparatorThresholdMs))
            {
                const long long nowMs = (long long)::time(NULL) * 1000LL;
                const CString timeText = FormatChatSeparatorTime(oldHeadMs, nowMs);
                if (!timeText.IsEmpty())
                {
                    ChatMessage sep;
                    sep.kind   = kMsgTime;
                    sep.fromMe = false;
                    sep.text   = timeText;

                    std::unique_ptr<ChatBubble> sepBubble(new ChatBubble());
                    sepBubble->SetMessage(sep);
                    AddChild(std::unique_ptr<balloonwjui::DuiControl>(sepBubble.release()));
                }
            }
        }
    }

    // 基准还原成翻页前那条：后续实时消息要与消息流<u>末尾</u>比，而不是与刚灌进来的
    // 这批历史消息比。
    m_lastMsgTimestampMs = m_prependSavedLastMs;

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
        // 进行中的平滑滚动也整体平移同样的量，否则它的下一帧会把视口拉回插入前的位置。
        m_smooth.Shift(grew);
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
    // 内容整个换掉了，进行中的平滑滚动失去意义，取消。
    m_smooth.Cancel();
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
            if (::IsWindow(host))
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
            if (::IsWindow(host))
                ::UpdateWindow(host);
            return;
        }
    }
}

bool ChatMessageList::SetFileDownloadedByMsgid(long long msgid, const CString& localPath,
                                               bool* incoming)
{
    if (msgid == 0)
        return false;
    const std::vector<std::unique_ptr<balloonwjui::DuiControl> >& kids = Children();
    for (size_t i = 0; i < kids.size(); ++i)
    {
        ChatBubble* b = dynamic_cast<ChatBubble*>(kids[i].get());
        if (b != NULL && b->FileMsgid() == msgid)
        {
            if (incoming != NULL)
            {
                *incoming = !b->GetMessage().fromMe;
            }
            b->SetFileDownloaded(localPath);
            ForceLayout(GetRect());
            Invalidate();
            return true;
        }
    }
    return false;
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

//这一行就是"活的本地文件夹气泡"的判据，本文件三处定位共用一份：会话 id 对得上，
//且还没有被服务端的传输记录条接管（msgid 仍为 0）。同一次传输可能留下多条记录（每次
//尝试各一条），不加 msgid 这一半，第二条记录会去改第一条那个已经定格的气泡。
static bool IsLiveFolderBubble(const ChatBubble* bubble, const std::string& sessionId)
{
    return bubble != NULL
        && bubble->FolderSessionId() == sessionId
        && bubble->Msgid() == 0;
}

bool ChatMessageList::GetFolderMessage(const std::string& sessionId, ChatMessage& out) const
{
    if (sessionId.empty())
        return false;

    const std::vector<std::unique_ptr<balloonwjui::DuiControl> >& kids = Children();
    for (size_t i = 0; i < kids.size(); ++i)
    {
        ChatBubble* b = dynamic_cast<ChatBubble*>(kids[i].get());
        if (IsLiveFolderBubble(b, sessionId))
        {
            out = b->GetMessage();
            return true;
        }
    }
    return false;
}

bool ChatMessageList::SetFolderMessage(const std::string& sessionId, const ChatMessage& msg)
{
    if (sessionId.empty())
        return false;

    const std::vector<std::unique_ptr<balloonwjui::DuiControl> >& kids = Children();
    for (size_t i = 0; i < kids.size(); ++i)
    {
        ChatBubble* b = dynamic_cast<ChatBubble*>(kids[i].get());
        if (IsLiveFolderBubble(b, sessionId))
        {
            b->SetMessage(msg);
            //文件夹气泡的高度随状态变化（进度条 / 失败原因行 / 按钮行的有无），
            //只 Invalidate 的话新出现的按钮会被裁在气泡外面、点不到。
            ForceLayout(GetRect());
            Invalidate();
            return true;
        }
    }
    return false;
}

bool ChatMessageList::RemoveFolderMessage(const std::string& sessionId)
{
    if (sessionId.empty())
        return false;

    const std::vector<std::unique_ptr<balloonwjui::DuiControl> >& kids = Children();
    for (size_t i = 0; i < kids.size(); ++i)
    {
        ChatBubble* b = dynamic_cast<ChatBubble*>(kids[i].get());
        if (IsLiveFolderBubble(b, sessionId))
        {
            //RemoveChild 会立即销毁这个气泡，其后不得再访问 b；随即重排让下方各行
            //补上空出来的位置。做法与右键菜单删除消息行（RequestDeleteBubble）一致。
            RemoveChild(b);
            ForceLayout(GetRect());
            Invalidate();
            return true;
        }
    }
    return false;
}

bool ChatMessageList::ReviveFolderMessage(const std::string& sessionId)
{
    if (sessionId.empty())
        return false;

    //从后往前找：同一会话若有多条气泡（翻历史把这一笔早先几次尝试的记录也拉了进来），
    //最后那条才是刚被接管、马上要复用的那一条。
    const std::vector<std::unique_ptr<balloonwjui::DuiControl> >& kids = Children();
    for (size_t i = kids.size(); i > 0; --i)
    {
        ChatBubble* b = dynamic_cast<ChatBubble*>(kids[i - 1].get());
        if (b == NULL || b->FolderSessionId() != sessionId)
        {
            continue;
        }

        ChatMessage msg = b->GetMessage();
        //已经是活的就不必再动，直接算成功——调用方只关心"这条气泡现在能被更新了"。
        if (msg.msgid != 0)
        {
            msg.msgid = 0;
            msg.timestampMs = 0;
            b->SetMessage(msg);
            ForceLayout(GetRect());
            Invalidate();
        }
        return true;
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

bool ChatMessageList::RequestDeleteBubble(ChatBubble* bubble)
{
    if (bubble == NULL)
    {
        return false;
    }

    //服务器上已有这条消息：删除要经服务端完成，界面等删除推送回来再改，
    //这样本人其他设备与本窗口看到的结果一致
    const long long msgid = bubble->Msgid();
    if (msgid > 0)
    {
        std::vector<long long> msgids;
        msgids.push_back(msgid);
        PostDeleteRequest(msgids);
        return false;
    }

    //发送中 / 发送失败的行服务器上没有，只存在于本窗口，直接移除。
    //RemoveChild 会立即销毁 bubble；调用方其后不得再访问它。
    RemoveChild(bubble);
    ForceLayout(GetRect());
    Invalidate();
    return true;
}

std::vector<long long> ChatMessageList::DeleteSelected()
{
    std::vector<long long> serverMsgids;
    if (!m_multiSelect || SelectedCount() == 0)
    {
        return serverMsgids;
    }

    //先收集，再移除：RemoveChild 会改动子控件数组，不能在遍历 Children() 的过程中直接删
    std::vector<ChatBubble*> localOnly;
    const std::vector<std::unique_ptr<balloonwjui::DuiControl> >& kids = Children();
    for (size_t i = 0; i < kids.size(); ++i)
    {
        ChatBubble* b = dynamic_cast<ChatBubble*>(kids[i].get());
        if (b == NULL || !b->IsSelected())
        {
            continue;
        }
        if (b->Msgid() > 0)
        {
            serverMsgids.push_back(b->Msgid());
        }
        else
        {
            localOnly.push_back(b);
        }
    }
    for (size_t i = 0; i < localOnly.size(); ++i)
    {
        RemoveChild(localOnly[i]);
    }

    //退出多选并重排（SetMultiSelectMode 内含 ForceLayout），留下的行一并清掉选中态
    SetMultiSelectMode(false);

    PostDeleteRequest(serverMsgids);
    return serverMsgids;
}

std::vector<long long> ChatMessageList::GetSelectedForwardMsgids(size_t* skipped) const
{
    if (skipped != NULL)
    {
        *skipped = 0;
    }

    std::vector<long long> msgids;
    if (!m_multiSelect)
    {
        return msgids;
    }

    //去重用 set：同一条消息理论上不会出现两行，这里只是防止将来列表里出现重复行时
    //把同一个 msgid 发两遍
    std::set<long long> seen;
    const std::vector<std::unique_ptr<balloonwjui::DuiControl> >& kids = Children();
    for (size_t i = 0; i < kids.size(); ++i)
    {
        const ChatBubble* b = dynamic_cast<const ChatBubble*>(kids[i].get());
        if (b == NULL || !b->IsSelected())
        {
            continue;
        }

        const ChatMessage& msg = b->GetMessage();
        if (!CanForwardMessage(msg.kind, msg.msgid, msg.fileMsgid))
        {
            if (skipped != NULL)
            {
                ++(*skipped);
            }
            continue;
        }

        const long long id = ForwardMsgidOf(msg.msgid, msg.fileMsgid);
        if (seen.insert(id).second)
        {
            msgids.push_back(id);
        }
    }

    //按 msgid 升序：列表里的显示顺序基本就是 msgid 顺序，但本端刚发出的消息按回执回填
    //的先后排在末尾，不一定严格有序；服务端也是按 msgid 升序处理转发源
    std::sort(msgids.begin(), msgids.end());
    return msgids;
}

bool ChatMessageList::ContainsMessage(long long msgid, const std::string& clientMsgId) const
{
    const std::vector<std::unique_ptr<balloonwjui::DuiControl> >& kids = Children();
    for (size_t i = 0; i < kids.size(); ++i)
    {
        const ChatBubble* b = dynamic_cast<const ChatBubble*>(kids[i].get());
        if (b == NULL)
        {
            continue;
        }
        const ChatMessage& msg = b->GetMessage();
        if (msgid > 0 && (msg.msgid == msgid || msg.fileMsgid == msgid))
        {
            return true;
        }
        if (!clientMsgId.empty()
            && (msg.clientMsgId == clientMsgId || msg.fileClientMsgId == clientMsgId))
        {
            return true;
        }
    }
    return false;
}

void ChatMessageList::PostDeleteRequest(const std::vector<long long>& msgids)
{
    if (msgids.empty())
    {
        return;
    }

    //消息列表是纯 UI 控件、不直接碰网络：与撤回、Pin 相同，把 msgid 投给宿主聊天窗，
    //由它确认后发 cmd_delete_message。堆上分配，宿主处理后 delete。
    HWND hwnd = HostHwnd();
    if (!::IsWindow(hwnd))
    {
        return;
    }
    std::vector<long long>* pMsgids = new std::vector<long long>(msgids);
    if (!::PostMessage(hwnd, WM_CHAT_DELETE_REQUEST, 0, (LPARAM)pMsgids))
    {
        //投递失败（窗口已销毁）时自行释放，避免泄漏。
        delete pMsgids;
    }
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
    if (!::IsWindow(hwnd))
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
    if (!::IsWindow(hwnd))
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
        // 发送回执晚于对方的已读推送到达时，回填的 msgid 可能已经在位点之内。
        if (cur.fromMe && msgid > 0 && msgid <= m_peerReadMsgid)
        {
            b->MarkPeerRead();
        }
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

int ChatMessageList::RemoveMessagesByMsgid(const std::vector<long long>& msgids)
{
    std::set<long long> targets;
    for (size_t i = 0; i < msgids.size(); ++i)
    {
        //msgid 非正的行是本端刚发出、还没拿到服务器 ID 的，不可能是服务端删掉的那条
        if (msgids[i] > 0)
        {
            targets.insert(msgids[i]);
        }
    }
    if (targets.empty())
    {
        return 0;
    }

    //第一遍：先收集，再移除。RemoveChild 会立即销毁该行并改动子控件数组，
    //边遍历边移除会让下标错位
    std::vector<ChatBubble*> doomed;
    {
        const std::vector<std::unique_ptr<balloonwjui::DuiControl> >& kids = Children();
        for (size_t i = 0; i < kids.size(); ++i)
        {
            ChatBubble* b = dynamic_cast<ChatBubble*>(kids[i].get());
            if (b != NULL && b->Msgid() > 0 && targets.find(b->Msgid()) != targets.end())
            {
                doomed.push_back(b);
            }
        }
    }
    if (doomed.empty())
    {
        return 0;
    }
    for (size_t i = 0; i < doomed.size(); ++i)
    {
        RemoveChild(doomed[i]);
    }

    //第二遍：从后往前找孤立的时间分隔行 —— 后面紧跟另一个分隔行、或已到列表末尾。
    //"纯粹的分隔行"认 kind 为 kMsgTime 且没有 msgid：撤回提示行也是 kMsgTime 样式，
    //但保留着原消息的 msgid（见 ChatBubble::RecallToTip），属于内容，不算分隔行。
    //一个分隔行无论保留还是移除，它前面那一行看到的"后一行"都仍是分隔行或末尾，
    //所以一遍倒序扫描即可，不必反复扫
    std::vector<ChatBubble*> orphans;
    {
        const std::vector<std::unique_ptr<balloonwjui::DuiControl> >& kids = Children();
        bool nextIsSeparatorOrEnd = true;
        for (size_t i = kids.size(); i > 0; --i)
        {
            ChatBubble* b = dynamic_cast<ChatBubble*>(kids[i - 1].get());
            if (b == NULL)
            {
                nextIsSeparatorOrEnd = false;
                continue;
            }
            const bool isSeparator = (b->GetMessage().kind == kMsgTime && b->Msgid() <= 0);
            if (isSeparator)
            {
                if (nextIsSeparatorOrEnd)
                {
                    orphans.push_back(b);
                }
                nextIsSeparatorOrEnd = true;
            }
            else
            {
                nextIsSeparatorOrEnd = false;
            }
        }
    }
    for (size_t i = 0; i < orphans.size(); ++i)
    {
        RemoveChild(orphans[i]);
    }

    //全部移除完只重排、重绘一次，下方各行补上空出来的位置
    ForceLayout(GetRect());
    Invalidate();
    return static_cast<int>(doomed.size());
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

size_t ChatMessageList::RequestForwardSelected(bool merge)
{
    size_t skipped = 0;
    const std::vector<long long> msgids = GetSelectedForwardMsgids(&skipped);
    if (msgids.empty())
    {
        return 0;
    }

    //与删除相同：消息列表不直接碰网络，把请求投给宿主聊天窗，由它补上源会话信息后
    //交给主面板打开目标选择窗。堆上分配，宿主处理后 delete
    HWND hwnd = HostHwnd();
    if (::IsWindow(hwnd))
    {
        ChatForwardRequest* pReq = new ChatForwardRequest();
        pReq->m_merge = merge;
        pReq->m_msgids = msgids;
        pReq->m_skipped = skipped;
        if (!::PostMessage(hwnd, WM_CHAT_FORWARD_REQUEST, 0, (LPARAM)pReq))
        {
            //投递失败（窗口已销毁）时自行释放，避免泄漏
            delete pReq;
        }
    }

    //退出多选并重排（SetMultiSelectMode 内含 ForceLayout），留下的行一并清掉选中态
    SetMultiSelectMode(false);
    return msgids.size();
}

MultiSelectBarButton MultiSelectBarHitTest(const RECT& bar, POINT pt)
{
    const bool inBar = (pt.x >= bar.left && pt.x < bar.right
                        && pt.y >= bar.top && pt.y < bar.bottom);
    if (!inBar)
    {
        return kBarButtonNone;
    }

    //逐个按钮比对矩形，与绘制用的是同一份分段
    const MultiSelectBarButton buttons[] = {
        kBarButtonCancel, kBarButtonForwardEach, kBarButtonForwardMerged, kBarButtonDelete
    };
    for (size_t i = 0; i < sizeof(buttons) / sizeof(buttons[0]); ++i)
    {
        const RECT r = MultiSelectBarButtonRect(bar, buttons[i]);
        if (pt.x >= r.left && pt.x < r.right)
        {
            return buttons[i];
        }
    }
    return kBarButtonNone;
}

RECT MultiSelectBarButtonRect(const RECT& bar, MultiSelectBarButton button)
{
    RECT r = { 0, 0, 0, 0 };
    if (button < kBarButtonCancel || button > kBarButtonDelete)
    {
        return r;
    }

    //按钮个数就是枚举里最后一个按钮的取值（从 1 起连续编号）
    const int count = (int)kBarButtonDelete;
    const int width = (bar.right - bar.left) / count;
    const int index = (int)button - (int)kBarButtonCancel;
    r.left = bar.left + width * index;
    r.right = (button == kBarButtonDelete) ? bar.right : (r.left + width);
    r.top = bar.top;
    r.bottom = bar.bottom;
    return r;
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
    if (!::IsWindow(hwnd))
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

void PutTextToClipboard(HWND owner, LPCWSTR text)
{
    if (text == NULL || text[0] == L'\0')
    {
        return;
    }
    if (!::OpenClipboard(owner))
    {
        return;
    }
    ::EmptyClipboard();
    const size_t chars = ::wcslen(text) + 1;
    const size_t bytes = chars * sizeof(WCHAR);
    HGLOBAL hMem = ::GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (hMem != NULL)
    {
        void* dst = ::GlobalLock(hMem);
        if (dst != NULL)
        {
            ::CopyMemory(dst, text, bytes);
            ::GlobalUnlock(hMem);
            ::SetClipboardData(CF_UNICODETEXT, hMem);
        }
        else
        {
            //锁不住就得自己释放，否则这块内存既没交给剪贴板、也没人回收。
            ::GlobalFree(hMem);
        }
    }
    ::CloseClipboard();
}

bool ChatMessageList::CopySelectedTextToClipboard()
{
    const std::vector<std::unique_ptr<balloonwjui::DuiControl> >& kids = Children();
    for (size_t i = 0; i < kids.size(); ++i)
    {
        const ChatBubble* b = dynamic_cast<const ChatBubble*>(kids[i].get());
        if (b == NULL || !b->HasTextSelection())
        {
            continue;
        }
        // 同一时刻只会有一个气泡有选区。与气泡右键「复制」同口径：同时写纯文字与
        // Flamingo 自己的格式，粘到聊天输入框时表情能还原成表情。
        const std::vector<ChatClipSegment> segs = b->GetSelectedClipSegments();
        if (segs.empty())
        {
            return false;
        }
        WriteChatContentToClipboard(HostHwnd(), segs);
        return true;
    }
    return false;   // 没有选区：这一下按键不归消息区，交回原处理者
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
    PerfStopWatch sw;
    int paintedRows = 0;   // 本帧实际绘制的消息行数（与可视区相交的行）
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
                ++paintedRows;
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
    const RECT thumb = ThumbRect();
    if (thumb.bottom > thumb.top)
    {
        Gdiplus::Graphics g(hdc);
        g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        PaintChatScrollThumb(g, thumb);
    }

    // ---- 多选操作条 ----
    if (m_multiSelect)
    {
        const RECT bar = MultiSelectBarRect();
        HBRUSH white = ::CreateSolidBrush(RGB(255, 255, 255));
        ::FillRect(hdc, &bar, white);
        ::DeleteObject(white);

        // 四个按钮等分操作条，分段与点击共用 MultiSelectBarButtonRect。
        // 删除按选中条数计；转发按其中能转发的条数计（发送中 / 发送失败、文件夹行不算），
        // 能转发的一条都没有时两个转发按钮置灰、点击不响应。
        const int n = SelectedCount();
        const int forwardable = (int)GetSelectedForwardMsgids(NULL).size();

        // 取消。
        DrawBarText(hdc, L"取消", MultiSelectBarButtonRect(bar, kBarButtonCancel), kBarCancelColor);

        // 逐条转发 N。
        CString fwdEach;
        fwdEach.Format(_T("逐条转发 %d"), forwardable);
        DrawBarText(hdc, (LPCWSTR)fwdEach, MultiSelectBarButtonRect(bar, kBarButtonForwardEach),
                    (forwardable > 0) ? kBarForwardColor : kBarDisabledColor);

        // 合并转发 N。
        CString fwdMerged;
        fwdMerged.Format(_T("合并转发 %d"), forwardable);
        DrawBarText(hdc, (LPCWSTR)fwdMerged, MultiSelectBarButtonRect(bar, kBarButtonForwardMerged),
                    (forwardable > 0) ? kBarForwardColor : kBarDisabledColor);

        // 删除 N。
        CString del;
        del.Format(_T("删除 %d"), n);
        DrawBarText(hdc, (LPCWSTR)del, MultiSelectBarButtonRect(bar, kBarButtonDelete),
                    (n > 0) ? kBarDeleteColor : kBarDisabledColor);
    }

    BUI_TRACE("CHATLIST-PAINT rows=%u painted=%d scroll=%d content=%d view=%d rect=%d,%d,%d,%d cost=%.3fms",
              (unsigned)Children().size(), paintedRows, m_scrollY, m_contentHeight, m_viewHeight,
              m_rcItem.left, m_rcItem.top, m_rcItem.right, m_rcItem.bottom, sw.ElapsedMs());
}


bool ChatMessageList::OnMouseWheel(POINT /*pt*/, short zDelta, UINT /*mkFlags*/)
{
    const int maxScroll = (m_contentHeight > m_viewHeight)
                          ? (m_contentHeight - m_viewHeight) : 0;
    if (maxScroll <= 0)
    {
        return true;   // 无需滚动，但仍消费滚轮
    }

    //ScrollToBottom 只把位置设成哨兵值、等下一次重排才归到真实的最大值；此时画面其实
    //已经在底部。先把位置归到可滚动范围内再算目标，否则目标从哨兵值算起、夹紧后正好是
    //最大值，第一格滚轮就被吞掉了（打开聊天窗后的第一次滚动实测如此）。
    if (m_scrollY > maxScroll)
    {
        m_scrollY = maxScroll;
    }

    //只改平滑滚动的目标，滚动位置由逐帧驱动按缓出曲线推进过去（见 ChatSmoothScroll.h）。
    //delta 按比例换算成像素：触控板与高精度滚轮送来的小于一格的 delta 不再被丢弃。
    const bool animating = m_smooth.OnWheel(m_scrollY, maxScroll, zDelta, kWheelStep, PerfNowMs());
    if (animating)
    {
        EnsureScrollTicker();
    }
    BUI_TRACE("CHATLIST-WHEEL delta=%d scroll=%d target=%.1f animating=%d rows=%u",
              (int)zDelta, m_scrollY, m_smooth.Target(), animating ? 1 : 0,
              (unsigned)Children().size());
    return true;
}

void ChatMessageList::AdvanceSmoothScroll(double nowMs)
{
    if (!m_smooth.IsActive())
    {
        return;
    }
    PerfStopWatch sw;
    const int oldScroll = m_scrollY;
    int pos = m_smooth.Advance(nowMs);
    //动画开始后内容高度可能变了（窗口缩放、图片下载完成撑高了气泡），再夹紧一次
    const int maxScroll = (m_contentHeight > m_viewHeight) ? (m_contentHeight - m_viewHeight) : 0;
    if (pos > maxScroll)
    {
        pos = maxScroll;
    }
    if (pos < 0)
    {
        pos = 0;
    }
    if (pos != m_scrollY)
    {
        m_scrollY = pos;
        ForceLayout(GetRect());
        Invalidate();
        NotifyMaybeLoadOlder();
    }
    BUI_TRACE("CHATLIST-SMOOTH scroll=%d->%d active=%d layout=%.3fms",
              oldScroll, m_scrollY, m_smooth.IsActive() ? 1 : 0, sw.ElapsedMs());
}

bool ChatMessageList::IsSmoothScrolling() const
{
    return m_smooth.IsActive();
}

void ChatMessageList::EnsureScrollTicker()
{
    if (m_scrollTickerRunning)
    {
        return;
    }
    std::unique_ptr<ChatScrollTicker> ticker(new ChatScrollTicker(m_scrollAnimToken));
    ChatScrollTickerDone done;
    done.m_token = m_scrollAnimToken;
    ticker->SetOnComplete(std::function<void()>(done));
    m_scrollTickerRunning = true;
    balloonwjui::DuiAnimMgr::Inst().Add(std::unique_ptr<balloonwjui::DuiAnim>(ticker.release()));
}

void ChatMessageList::OnScrollTickerDone()
{
    m_scrollTickerRunning = false;
    if (!m_smooth.IsActive())
    {
        return;
    }
    //这一段驱动走完了，但期间又收到了滚轮、动画被延长：先推进到此刻，没走完就再挂一段。
    //在完成回调里向 DuiAnimMgr 添加新动画是它明确支持的用法（见 DuiAnimMgr::Add 的说明）。
    AdvanceSmoothScroll(PerfNowMs());
    if (m_smooth.IsActive())
    {
        EnsureScrollTicker();
    }
}

void ChatMessageList::NotifyMaybeLoadOlder()
{
    // 只在<u>接近顶部</u>时才惊动宿主，免得每一格滚轮都投一条消息。真正"要不要拉"
    // 的判定（有没有在途、还有没有更早的、锚点是什么）在宿主那边用
    // ShouldLoadOlderHistory 做 —— 那些状态本控件不持有，也不该持有。
    // "接近顶部"的口径与宿主那边共用 IsNearTopForOlderHistory（离顶一屏以内）。
    if (!IsNearTopForOlderHistory(m_scrollY, m_viewHeight))
    {
        return;
    }
    HWND hwnd = HostHwnd();
    if (::IsWindow(hwnd))
    {
        ::PostMessage(hwnd, WM_CHAT_LOAD_OLDER, 0, 0);
    }
}

RECT ChatMessageList::ThumbRect() const
{
    //ComputeChatScrollThumb 内部会先把滚动位置收进合法范围：即便某条路径留下了未经排版夹紧的
    //占位值，滑块也落在可视区内，不会画到几百万像素之外
    return ComputeChatScrollThumb(RowsViewRect(), m_viewHeight, m_contentHeight, m_scrollY);
}

bool ChatMessageList::HitScrollBarBand(POINT pt) const
{
    // 命中带：滑块所在竖直窄带（左右各放宽一点便于点中），且纵向落在消息区内；内容没溢出时
    // 没有滚动条，恒为 false。判断规则与成员侧栏、输入框共用
    return HitChatScrollBand(pt, ThumbRect(), RowsViewRect());
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

    // 多选操作条同理：进入多选时可视区底部让出操作条的高度，而消息行的矩形并不随之截短，
    // 最后几行会伸到操作条下面。不截下的话，点「取消 / 转发 / 删除」会落到下面的消息行上，
    // 变成切换那一行的选中态（2026-10-01 联调发现，见 ChatMessageDeleteTests 的 CD7）。
    if (hit != NULL && m_multiSelect)
    {
        const RECT bar = MultiSelectBarRect();
        if (::PtInRect(&bar, ptHostClient))
        {
            return this;
        }
    }
    return hit;
}

void ChatMessageList::ScrollToThumbTop(int thumbTopY)
{
    if (m_contentHeight <= m_viewHeight || m_viewHeight <= 0)
        return;

    //由滑块上沿反推滚动位置，与 ThumbRect 的几何互逆（ChatScrollThumb 的公共函数）
    const int newScroll = ChatScrollYFromThumbTop(thumbTopY, RowsViewRect(), m_viewHeight, m_contentHeight);

    // 拖动滚动条直接决定位置：取消进行中的平滑滚动，免得它与拖动争夺位置。
    m_smooth.Cancel();

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
    const MultiSelectBarButton button = MultiSelectBarHitTest(MultiSelectBarRect(), pt);
    switch (button)
    {
    // 落点不在操作条上：交给其它逻辑处理
    case kBarButtonNone:
        return false;

    // 取消：退出多选。
    case kBarButtonCancel:
        SetMultiSelectMode(false);
        return true;

    // 逐条转发：没有可转发的行时 RequestForwardSelected 什么都不做、也不退出多选
    // （按钮此时是灰的）；否则投给宿主并退出多选。
    case kBarButtonForwardEach:
        RequestForwardSelected(false);
        return true;

    // 合并转发：同上，只是模式为合并。
    case kBarButtonForwardMerged:
        RequestForwardSelected(true);
        return true;

    // 删除所选（选中数为 0 时无动作）。本地独有的行立即移除，服务器上已有的行转交
    // 宿主确认并发删除请求，随后退出多选。
    case kBarButtonDelete:
        if (SelectedCount() > 0)
        {
            DeleteSelected();
        }
        return true;

    // 枚举之外的取值不会出现，按"已处理"吞掉，避免点到操作条下面的消息行
    default:
        return true;
    }
}

} // namespace balloonchatwnd
