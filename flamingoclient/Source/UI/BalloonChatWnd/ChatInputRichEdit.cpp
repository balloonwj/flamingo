/**
 *  ChatInputRichEdit 的实现。设计背景与消息协议见 ChatInputRichEdit.h。
 *  balloonwj@qq.com   2026-08-16
 */
#include "stdafx.h"
#include "ChatInputRichEdit.h"

#include "../../App/CustomMsgDef.h"   // WM_CHAT_FOCUS_AREA / WM_CHAT_COPY_SELECTION
#include "../../Common/Log/IULog.h"   // LOG_INFO
#include "ChatClipboardIo.h"          // 读剪贴板里的 Flamingo 聊天内容
#include "ChatFaceTag.h"              // FaceTagFromId / kFaceDisplayPx（插入表情）
#include "ChatInlineText.h"           // FaceImagePathFromId（表情编号 → 图片文件）
#include "ChatScrollThumb.h"          // 与消息区同一种细滚动条：几何计算与滑块绘制
#include "../../../third-party/balloonui/balloonui/DuiHost.h"   // 取宿主窗口句柄作剪贴板归属

#include <gdiplus.h>   // 画滑块（半透明圆角）

namespace balloonchatwnd {

namespace {

// 输入框文字区的内边距（像素）。左、上、下沿用库内文本控件的默认值；右侧留出细滚动条的位置：
// 滑块宽 kChatScrollBarWidth，左右各空 kChatScrollBarMargin，文字不会被滑块盖住
const int kInputMarginLeft   = 4;
const int kInputMarginTop    = 2;
const int kInputMarginRight  = kChatScrollBarWidth + kChatScrollBarMargin * 2;
const int kInputMarginBottom = 2;

// 外边框宽度（像素）：显示边框时基类在控件矩形内侧画 1 像素边框，文字区相应内缩（DuiRichEdit::UpdateTextRect）
const int kInputBorderPx = 1;

// 按键按下消息里表示「自动重复」的那一位：为 1 说明这个键在本次消息之前
// 就已经按着了。按住回车不该连发多条消息，发送路径据此忽略重复。
const UINT kPrevKeyDownFlag = 0x4000;

// 判断某个虚拟键当前是否处于按下状态。
const SHORT kKeyDownMask = (SHORT)0x8000;

// Ctrl+回车在光标处插入的换行串。用回车加换行两个字符，与排版引擎内部的
// 换行表示一致。
const TCHAR* const kNewLine = _T("\r\n");

// 「@提醒」浮层要接管的那几个导航键。
bool IsMentionNavKey(UINT vk)
{
    return vk == VK_UP || vk == VK_DOWN
        || vk == VK_RETURN || vk == VK_TAB || vk == VK_ESCAPE;
}

// 判断当前按键组合是不是「粘贴」：Ctrl+V，或 Shift+Insert。按着 Alt 的不算（留给菜单
// 加速键等其它用途）。
bool IsPasteShortcut(UINT vk)
{
    const bool ctrl  = (::GetKeyState(VK_CONTROL) & kKeyDownMask) != 0;
    const bool shift = (::GetKeyState(VK_SHIFT) & kKeyDownMask) != 0;
    const bool alt   = (::GetKeyState(VK_MENU) & kKeyDownMask) != 0;
    if (alt)
    {
        return false;
    }
    return (vk == 'V' && ctrl) || (vk == VK_INSERT && shift && !ctrl);
}

} // 匿名命名空间

ChatInputRichEdit::ChatInputRichEdit()
    : m_hNotifyTarget(NULL)
    , m_nSendOnEnterMsg(0)
    , m_nTypingChangedMsg(0)
    , m_nMentionChangedMsg(0)
    , m_nMentionNavMsg(0)
    , m_nMentionWheelMsg(0)
    , m_bImeComposing(false)
    , m_pfnSendQuery(NULL)
    , m_pSendQueryCtx(NULL)
    , m_pfnStageImage(NULL)
    , m_pStageImageCtx(NULL)
    , m_marginTop(kInputMarginTop)
    , m_thumbDragging(false)
    , m_thumbDragOffsetY(0)
    , m_thumbCacheDirty(true)
    , m_thumbQuerying(false)
    , m_thumbQueryCount(0)
{
    ::SetRectEmpty(&m_thumbCacheRect);
    ::SetRectEmpty(&m_thumbCacheItem);

    // 关掉基类自带的拖放，且<u>必须在构造时就关</u>。
    //
    // 操作系统的拖放目标是按窗口注册的，一个窗口只能注册一个。基类在第一次
    // 布局时会替自己向宿主申请开启拖放分发（DuiRichEdit::Layout →
    // DuiHost::EnableDropDispatch），那一步就把聊天窗的窗口拖放目标占走了；
    // 随后聊天窗自己的 ChatDropController::Attach 再去注册必然失败，症状是
    // 拖任何文件进聊天窗都没有反应，界面上没有任何提示。
    //
    // 聊天窗的投放区是整个窗口（拖文件进来就发送），不需要输入框单独收一份，
    // 所以这里直接关掉。关的时机是关键：等控件挂进控件树之后再关就晚了 ——
    // 挂进去那一下就会走布局，申请已经发出去了，而这个开关只翻排版引擎的一个
    // 属性位，并不会把宿主已经做的注册撤销。
    //
    // 守卫用例：Source/Tests/ChatDropRegistrationTests.cpp（Suite CDR）。
    SetDragDropEnabled(false);

    // 滚动条与消息区统一：关掉基类自带的覆盖式滚动条，改由 OnPaint 画细滚动条，内容超出时
    // 一直显示。滚轮仍由基类转交排版引擎处理，不受影响。右侧内边距放宽到能容下滑块
    SetVScrollPolicy(kScrollBarNever);
    SetMargins(kInputMarginLeft, kInputMarginTop, kInputMarginRight, kInputMarginBottom);
}

void ChatInputRichEdit::SetMargins(int left, int top, int right, int bottom)
{
    m_marginTop = (top > 0) ? top : 0;
    DuiRichEdit::SetMargins(left, top, right, bottom);
}

bool ChatInputRichEdit::ThumbGeometry(RECT& view, int& viewH, int& contentH, int& scrollY) const
{
    if (!GetVScrollMetrics(contentH, scrollY, viewH) || viewH <= 0)
    {
        return false;
    }
    //横向取整个控件（去掉边框），滑块贴着右缘；纵向取文字区，与引擎报告的可视高度一致
    const int border = IsShowBorder() ? kInputBorderPx : 0;
    view.left   = m_rcItem.left + border;
    view.top    = m_rcItem.top + border + m_marginTop;
    view.right  = m_rcItem.right - border;
    view.bottom = view.top + viewH;
    return true;
}

RECT ChatInputRichEdit::ScrollThumbRect() const
{
    RECT none = { 0, 0, 0, 0 };
    RECT view = { 0, 0, 0, 0 };
    int viewH = 0;
    int contentH = 0;
    int scrollY = 0;
    if (!ThumbGeometry(view, viewH, contentH, scrollY))
    {
        return none;
    }
    return ComputeChatScrollThumb(view, viewH, contentH, scrollY);
}

void ChatInputRichEdit::ScrollToThumbTop(int thumbTop)
{
    RECT view = { 0, 0, 0, 0 };
    int viewH = 0;
    int contentH = 0;
    int scrollY = 0;
    if (!ThumbGeometry(view, viewH, contentH, scrollY))
    {
        return;
    }
    SetVScrollPos(ChatScrollYFromThumbTop(thumbTop, view, viewH, contentH));
    Invalidate();
}

bool ChatInputRichEdit::RefreshThumbCache()
{
    if (!m_thumbCacheDirty && ::EqualRect(&m_thumbCacheItem, &m_rcItem))
    {
        return false;
    }
    m_thumbCacheDirty = false;
    m_thumbCacheItem  = m_rcItem;
    m_thumbQuerying   = true;
    ++m_thumbQueryCount;
    m_thumbCacheRect  = ScrollThumbRect();
    m_thumbQuerying   = false;
    return true;
}

void ChatInputRichEdit::OnPaint(HDC hdc, const RECT& rcDirty)
{
    DuiRichEdit::OnPaint(hdc, rcDirty);
    if (!IsVisible())
    {
        return;
    }
    const RECT oldThumb = m_thumbCacheRect;
    if (RefreshThumbCache() && !::EqualRect(&oldThumb, &m_thumbCacheRect))
    {
        //滑块位置或长度变了：本次的脏区不一定盖住旧、新两处，补请求一次重画。下一次绘制时缓存
        //未标脏、不再查询，滑块也不再变化，不会接着请求，因此不会循环
        Invalidate();
    }
    //滑块浮在文字之上，画在右侧内边距里，与消息区的滑块同一种画法
    if (m_thumbCacheRect.bottom > m_thumbCacheRect.top)
    {
        Gdiplus::Graphics g(hdc);
        g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        PaintChatScrollThumb(g, m_thumbCacheRect);
    }
}

void ChatInputRichEdit::TxSiteScrollInfoChanged(bool bVertical)
{
    DuiRichEdit::TxSiteScrollInfoChanged(bVertical);
    //本方法是在排版引擎的回调里被调用的，此时不能反过来查询引擎；这里只把滑块缓存标脏并请求
    //重画，新位置与长度在随后的 OnPaint 里查询。自己查询期间收到的回调忽略（见 m_thumbQuerying）
    if (bVertical && !m_thumbQuerying)
    {
        m_thumbCacheDirty = true;
        Invalidate();
    }
}

void ChatInputRichEdit::SetNotifyTarget(HWND hWnd)
{
    m_hNotifyTarget = hWnd;
}

void ChatInputRichEdit::SetSendOnEnterMsg(UINT nMsg)
{
    m_nSendOnEnterMsg = nMsg;
}

void ChatInputRichEdit::SetSendOnEnterQuery(SendOnEnterQueryFn fn, void* pCtx)
{
    m_pfnSendQuery   = fn;
    m_pSendQueryCtx  = pCtx;
}

void ChatInputRichEdit::SetTypingChangedMsg(UINT nMsg)
{
    m_nTypingChangedMsg = nMsg;
}

void ChatInputRichEdit::SetMentionMsgs(UINT nChangedMsg, UINT nNavMsg, UINT nWheelMsg)
{
    m_nMentionChangedMsg = nChangedMsg;
    m_nMentionNavMsg     = nNavMsg;
    m_nMentionWheelMsg   = nWheelMsg;
}

bool ChatInputRichEdit::HasTarget() const
{
    return m_hNotifyTarget != NULL && ::IsWindow(m_hNotifyTarget);
}

void ChatInputRichEdit::NotifyFocusInput()
{
    if (!HasTarget())
    {
        return;
    }
    ::PostMessage(m_hNotifyTarget, WM_CHAT_FOCUS_AREA,
                  (WPARAM)CHAT_FOCUS_INPUT, 0);
}

void ChatInputRichEdit::NotifyTyping(bool bKillFocus)
{
    if (m_nTypingChangedMsg == 0 || !HasTarget())
    {
        return;
    }
    ::PostMessage(m_hNotifyTarget, m_nTypingChangedMsg,
                  bKillFocus ? 1 : 0, 0);
}

bool ChatInputRichEdit::OnKeyDown(UINT vk, UINT flags)
{
    // ---- Ctrl+C：先问一句这一下归谁 ----
    //
    // 焦点在接收框（用户刚在气泡上拖选完）就由消息区复制并吞掉按键；焦点在
    // 发送框则不拦，输入框照常复制自己的选中文字。焦点归属由聊天窗维护。
    //
    // 用**同步**发送而不是投递：要拿它的返回值当场决定这次按键吞不吞。
    if (vk == 'C' && HasTarget()
        && (::GetKeyState(VK_CONTROL) & kKeyDownMask) != 0)
    {
        const LRESULT lCopied = ::SendMessage(m_hNotifyTarget,
                                              WM_CHAT_COPY_SELECTION, 0, 0);
        if (lCopied != 0)
        {
            return true;   // 已由消息区复制，吞掉这次按键
        }
    }

    // ---- 粘贴：剪贴板里有 Flamingo 聊天内容时按片段插入 ----
    //
    // 从聊天记录复制的内容带着 Flamingo 自己的格式，按片段插入才能把表情、图片还原出来。
    // 只接管这一种；剪贴板里没有这种格式时不拦，仍由基类按原来的方式粘贴。
    if (IsPasteShortcut(vk) && TryPasteChatContent())
    {
        return true;
    }

    // ---- @提醒浮层的导航键 ----
    //
    // 浮层正显示时，方向键 / 回车 / Tab / Esc 归它用来移动高亮或提交选中。
    // 同样必须同步发送 —— 吞不吞得当场知道。注意这一段要排在回车处理**之前**：
    // 浮层显示期间的回车是「选中当前候选」，不是「发送消息」。
    if (m_nMentionNavMsg != 0 && IsMentionNavKey(vk) && HasTarget())
    {
        const LRESULT lTaken = ::SendMessage(m_hNotifyTarget, m_nMentionNavMsg,
                                             (WPARAM)vk, 0);
        if (lTaken != 0)
        {
            return true;   // 浮层消费掉了这次按键
        }
    }

    // ---- 回车：发送还是换行 ----
    //
    // 输入法合成期内不介入 —— 那时的回车是「候选词上屏」，拦下来会让用户
    // 打不出中文。
    if (vk == VK_RETURN && !m_bImeComposing)
    {
        const bool bCtrlDown = (::GetKeyState(VK_CONTROL) & kKeyDownMask) != 0;
        // 这次回车是发送还是换行，交给调用方注入的回调判断（它知道用户选的是
        // 哪种发送模式）。没注册回调时按最常见的约定：不按 Ctrl 即发送。
        const bool bSendNow = (m_pfnSendQuery != NULL)
                            ? m_pfnSendQuery(bCtrlDown, m_pSendQueryCtx)
                            : !bCtrlDown;

        if (!bSendNow || m_nSendOnEnterMsg == 0)
        {
            // 换行：在当前选区 / 光标处插入换行符。
            ReplaceSel(kNewLine);
        }
        else if ((flags & kPrevKeyDownFlag) == 0)
        {
            // 发送。忽略自动重复，避免按住回车连发。
            if (HasTarget())
            {
                ::PostMessage(m_hNotifyTarget, m_nSendOnEnterMsg, 0, 0);
            }
        }
        // 无论发送还是换行，都吞掉这次回车 —— 放行的话排版引擎会再插一个换行。
        return true;
    }

    return DuiRichEdit::OnKeyDown(vk, flags);
}

bool ChatInputRichEdit::OnChar(TCHAR ch)
{
    // 回车产生的字符一并吞掉：按键按下那一步已经处理过了，放行会让引擎再插
    // 一个换行。合成期不拦，交回输入法处理。
    if ((ch == VK_RETURN || ch == 0x0A) && !m_bImeComposing)
    {
        return true;
    }

    // 真的在输入框里打字，也算「回到发送框」。
    //
    // **只认可见字符**：Ctrl+C 之类的控制键同样会产生字符消息（Ctrl+C 是 0x03），
    // 若一并算上，按一次 Ctrl+C 就会把焦点标记改成「发送框」，紧接着再按一次
    // 就复制不到气泡了。
    const TCHAR kFirstPrintable = 0x20;
    if (ch >= kFirstPrintable)
    {
        NotifyFocusInput();
    }

    return DuiRichEdit::OnChar(ch);
}

bool ChatInputRichEdit::OnSetFocus()
{
    NotifyFocusInput();
    return DuiRichEdit::OnSetFocus();
}

bool ChatInputRichEdit::OnKillFocus()
{
    // 失焦时让宿主立即给「正在输入」收尾，否则对端会一直显示「对方正在输入」。
    NotifyTyping(/*bKillFocus=*/true);
    return DuiRichEdit::OnKillFocus();
}

bool ChatInputRichEdit::OnLButtonDown(POINT pt, UINT mkFlags)
{
    // 点输入框同样算「回到发送框」。
    //
    // 光靠获得焦点那条路不够：点消息区时输入框**并没有**失去焦点，再点回
    // 输入框自然也就不会有获得焦点这一步。
    NotifyFocusInput();

    // 落在细滚动条命中带里：开始拖动滑块，不交给基类（否则会被当成在文字上按下、开始选择）。
    // 命中带的判断与消息区共用 HitChatScrollBand，两处手感一致
    RECT view = { 0, 0, 0, 0 };
    int viewH = 0;
    int contentH = 0;
    int scrollY = 0;
    if (ThumbGeometry(view, viewH, contentH, scrollY))
    {
        const RECT thumb = ComputeChatScrollThumb(view, viewH, contentH, scrollY);
        if (HitChatScrollBand(pt, thumb, view))
        {
            if (pt.y >= thumb.top && pt.y < thumb.bottom)
            {
                //抓在滑块上：记住抓点相对滑块上沿的偏移，拖动时滑块不跳
                m_thumbDragOffsetY = pt.y - thumb.top;
            }
            else
            {
                //点在轨道空白处：滑块中心先跳到点击处，再按住继续拖
                m_thumbDragOffsetY = (thumb.bottom - thumb.top) / 2;
                ScrollToThumbTop(pt.y - m_thumbDragOffsetY);
            }
            m_thumbDragging = true;
            Capture();
            return true;
        }
    }
    return DuiRichEdit::OnLButtonDown(pt, mkFlags);
}

bool ChatInputRichEdit::OnMouseMove(POINT pt, UINT mkFlags)
{
    if (!m_thumbDragging)
    {
        return DuiRichEdit::OnMouseMove(pt, mkFlags);
    }
    ScrollToThumbTop(pt.y - m_thumbDragOffsetY);
    return true;
}

bool ChatInputRichEdit::OnLButtonUp(POINT pt, UINT mkFlags)
{
    if (!m_thumbDragging)
    {
        return DuiRichEdit::OnLButtonUp(pt, mkFlags);
    }
    m_thumbDragging = false;
    ReleaseCapture();
    return true;
}

bool ChatInputRichEdit::OnMouseWheel(POINT pt, short zDelta, UINT mkFlags)
{
    // @提醒浮层正显示时，滚轮归它滚候选列表。
    if (m_nMentionWheelMsg != 0 && HasTarget())
    {
        const LRESULT lTaken = ::SendMessage(m_hNotifyTarget, m_nMentionWheelMsg,
                                             (WPARAM)zDelta, 0);
        if (lTaken != 0)
        {
            return true;   // 浮层消费掉了这次滚轮，输入框自身不滚
        }
    }
    return DuiRichEdit::OnMouseWheel(pt, zDelta, mkFlags);
}

bool ChatInputRichEdit::OnRawMessage(UINT uMsg, WPARAM wParam, LPARAM lParam,
                                     LRESULT& lResult)
{
    // 输入法合成期的起止。记这个状态是为了让回车在合成期内放行 ——
    // 那时的回车是候选词上屏，拦下来用户就打不出中文了。
    if (uMsg == WM_IME_STARTCOMPOSITION)
    {
        m_bImeComposing = true;
    }
    else if (uMsg == WM_IME_ENDCOMPOSITION)
    {
        m_bImeComposing = false;
    }
    return DuiRichEdit::OnRawMessage(uMsg, wParam, lParam, lResult);
}

HRESULT ChatInputRichEdit::TxSiteNotify(DWORD iNotify, void* pv)
{
    // 内容变化：一路通知出去。两个接收方各有各的用途 ——
    // 「正在输入」用来决定要不要给对端发提示，@提醒用来重算光标前的 @ 前缀。
    if (iNotify == EN_CHANGE)
    {
        //内容变了，内容高度可能随之变化：滑块缓存标脏，下次绘制时重新查询
        m_thumbCacheDirty = true;
        NotifyTyping(/*bKillFocus=*/false);
        if (m_nMentionChangedMsg != 0 && HasTarget())
        {
            ::PostMessage(m_hNotifyTarget, m_nMentionChangedMsg, 0, 0);
        }
    }
    return DuiRichEdit::TxSiteNotify(iNotify, pv);
}

void ChatInputRichEdit::SetImageStager(StageImageFn fn, void* pCtx)
{
    m_pfnStageImage  = fn;
    m_pStageImageCtx = pCtx;
}

void ChatInputRichEdit::InsertClipSegments(const std::vector<ChatClipSegment>& segs)
{
    for (size_t i = 0; i < segs.size(); ++i)
    {
        const ChatClipSegment& seg = segs[i];
        switch (seg.m_kind)
        {
        //文字：原样插在光标处（有选区时替换选区），可撤销
        case kChatClipText:
            ReplaceSel(seg.m_text.c_str(), true);
            break;

        //表情：与从表情面板选表情走同一个插入口，带编号标记，发送时还原成表情；
        //表情图片找不到时以"[表情]"占位
        case kChatClipFace:
            if (!InsertTaggedImage(FaceImagePathFromId(seg.m_faceId),
                                   FaceTagFromId(seg.m_faceId), kFaceDisplayPx))
            {
                ReplaceSel(kChatClipFacePlaceholder, true);
            }
            break;

        //图片：交给聊天窗登记源文件并插入；文件已不在（缓存被清理）或没有注册回调时以
        //"[图片]"占位，不悄悄丢掉
        case kChatClipImage:
            {
                bool staged = false;
                if (m_pfnStageImage != NULL
                    && ::GetFileAttributesW(seg.m_imagePath.c_str()) != INVALID_FILE_ATTRIBUTES)
                {
                    staged = m_pfnStageImage(m_pStageImageCtx, seg.m_imagePath);
                }
                if (!staged)
                {
                    ReplaceSel(kChatClipImagePlaceholder, true);
                }
            }
            break;

        //不认识的片段类型：跳过
        default:
            break;
        }
    }
    Invalidate();
}

bool ChatInputRichEdit::TryPasteChatContent()
{
    if (IsReadOnly() || !IsChatContentOnClipboard())
    {
        return false;
    }
    //剪贴板归属窗口：宿主窗口有效就用它，否则按"不指定归属"打开。
    HWND owner = NULL;
    if (GetHost() != NULL && ::IsWindow(GetHost()->m_hWnd))
    {
        owner = GetHost()->m_hWnd;
    }
    std::vector<ChatClipSegment> segs;
    if (!ReadChatContentFromClipboard(owner, segs) || segs.empty())
    {
        return false;   //读不出来就交回原来的粘贴，至少还能粘出纯文字
    }
    InsertClipSegments(segs);
    LOG_INFO("chat input: pasted chat content, %u segment(s).", (unsigned)segs.size());
    return true;
}

bool ChatInputRichEdit::OnContextMenuCommand(UINT nId)
{
    //右键「粘贴」：与 Ctrl+V 同口径。只接管带 Flamingo 格式的内容，其余交给基类。
    if (nId == balloonwjui::kRichEditCmdPaste && TryPasteChatContent())
    {
        return true;
    }
    return DuiRichEdit::OnContextMenuCommand(nId);
}

} // namespace balloonchatwnd
