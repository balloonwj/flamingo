/**
 *  ChatInputKeyFilter.cpp —— 见同名头文件说明。
 *  balloonwj@qq.com   2026-06-02
 */
#include "stdafx.h"
#include "ChatInputKeyFilter.h"
#include "ChatSendMode.h"   // GetChatSendMode（按当前模式决定回车是发送还是换行）
#include "../CustomMsgDef.h"   // WM_CHAT_COPY_SELECTION（Ctrl+C 时问一句消息区有没有选区）
#include <commctrl.h>   // SetWindowSubclass / DefSubclassProc 等

// SetWindowSubclass 系列在 comctl32 里；客户端为 MFC 程序通常已链接，
// 这里显式声明一次，避免该模块单独编译时漏链。
#pragma comment(lib, "comctl32.lib")

namespace balloonchatwnd {

namespace {

// WM_KEYDOWN 的 lParam 第 30 位：1 表示按键在本次消息前已处于按下态，
// 即「自动重复」。按住 Enter 不应连发多条消息，故发送路径据此忽略重复。
const LPARAM kPrevKeyDownBit = 0x40000000;

// Ctrl+Enter 在光标处插入的换行串：CR+LF，与 Win32 多行 EDIT 内部换行表示一致。
const TCHAR* const kNewLine = _T("\r\n");

// 本子类过程在同一 EDIT 子窗口上的唯一标识。
const UINT_PTR kEnterFilterSubclassId = 1;

/**
 *  回车过滤器的私有状态，随子类过程挂在 EDIT 子窗口上（dwRefData），
 *  EDIT 销毁时在 WM_NCDESTROY 里释放。
 */
struct EnterFilterState
{
    HWND m_hNotifyTarget;   // 按 Enter 发送时 PostMessage 的目标窗口
    UINT m_sendMsg;         // 发送时投递的自定义消息号
    bool m_imeComposing;    // 是否正处于 IME 输入合成中：合成中的回车是「候选词上屏」，不当发送

    EnterFilterState(HWND target, UINT msg)
        : m_hNotifyTarget(target)
        , m_sendMsg(msg)
        , m_imeComposing(false)
    {
    }
};

/**
 *  告诉宿主窗"用户现在在发送框操作"（进焦点 / 点击 / 打字时调）。
 *  state 为空、或宿主窗已销毁时安静返回。
 */
void NotifyFocusInput(EnterFilterState* state)
{
    if (state == NULL || state->m_hNotifyTarget == NULL
        || !::IsWindow(state->m_hNotifyTarget))
    {
        return;
    }
    ::PostMessage(state->m_hNotifyTarget, WM_CHAT_FOCUS_AREA,
                  (WPARAM)CHAT_FOCUS_INPUT, 0);
}

/**
 *  EDIT 子窗口的子类过程：拦回车，区分 Enter（发送）与 Ctrl+Enter（换行）。
 */
LRESULT CALLBACK EnterFilterProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam,
                                 UINT_PTR /*uIdSubclass*/, DWORD_PTR dwRefData)
{
    EnterFilterState* state = reinterpret_cast<EnterFilterState*>(dwRefData);

    switch (msg)
    {
    case WM_IME_STARTCOMPOSITION:
        // 进入 IME 合成期：期间的回车留给 IME 上屏，本过滤器不介入
        if (state != NULL)
            state->m_imeComposing = true;
        break;

    case WM_IME_ENDCOMPOSITION:
        // 退出 IME 合成期
        if (state != NULL)
            state->m_imeComposing = false;
        break;

    case WM_SETFOCUS:
        // 焦点进到输入框：告诉宿主窗"用户现在在发送框操作"。
        NotifyFocusInput(state);
        break;

    case WM_LBUTTONDOWN:
        // 点输入框同样算"回到发送框" —— 光靠 WM_SETFOCUS 不够：点消息区时输入框
        // <u>并没有</u>失去系统焦点（DuiHost 不调 ::SetFocus），再点回输入框自然
        // 也就不会有 WM_SETFOCUS。
        NotifyFocusInput(state);
        break;

    case WM_KEYDOWN:
        // Ctrl+C：问一句宿主窗这一下归谁 —— 焦点在接收框（用户刚在气泡上拖选完）
        // 就由消息区复制并吞掉按键；焦点在发送框则不拦，输入框照常复制自己的
        // 选中文字。焦点归属由窗口维护（见 WM_CHAT_FOCUS_AREA 的说明）。
        //
        // 用<u>同步</u> SendMessage 而不是 Post：要拿它的返回值决定这次按键吞不吞。
        if (wParam == 'C' && state != NULL
            && (::GetKeyState(VK_CONTROL) & 0x8000) != 0
            && state->m_hNotifyTarget != NULL && ::IsWindow(state->m_hNotifyTarget))
        {
            const LRESULT copied = ::SendMessage(state->m_hNotifyTarget,
                                                 WM_CHAT_COPY_SELECTION, 0, 0);
            if (copied != 0)
            {
                return 0;   // 已由消息区复制，吞掉这次按键
            }
        }
        if (wParam == VK_RETURN && state != NULL && !state->m_imeComposing)
        {
            bool ctrlDown = (::GetKeyState(VK_CONTROL) & 0x8000) != 0;
            // 按当前全局发送模式决定这次回车是「发送」还是「换行」：
            //   Enter 发送模式 —— 不按 Ctrl 即发送、按 Ctrl 换行；
            //   Ctrl+Enter 发送模式 —— 按住 Ctrl 才发送、单按 Enter 换行。
            bool sendNow = (GetChatSendMode() == kSendOnEnter) ? !ctrlDown : ctrlDown;
            if (!sendNow)
            {
                // 换行：在当前选区/光标处插入换行符
                ::SendMessage(hwnd, EM_REPLACESEL, TRUE,
                              reinterpret_cast<LPARAM>(kNewLine));
            }
            else if ((lParam & kPrevKeyDownBit) == 0)
            {
                // 发送（忽略自动重复，避免按住回车连发）：通知聊天窗口发送
                ::PostMessage(state->m_hNotifyTarget, state->m_sendMsg, 0, 0);
            }
            // 吞掉这次回车键，阻止 EDIT 默认按 ES_WANTRETURN 插入换行
            return 0;
        }
        break;

    case WM_CHAR:
        // 回车产生的字符（CR=0x0D / LF=0x0A）一并吞掉：WM_KEYDOWN 已处理过，
        // 若放行字符消息会让 EDIT 再插一个换行。合成期不拦，交回 IME/默认处理。
        if ((wParam == VK_RETURN || wParam == 0x0A)
            && state != NULL && !state->m_imeComposing)
            return 0;
        // 真的在输入框里打字，也算"回到发送框"。只认可见字符：Ctrl+C 之类的控制键
        // 同样会产生 WM_CHAR（Ctrl+C 是 0x03），若一并算上，按一次 Ctrl+C 就会把
        // 焦点标记改成"发送框"，紧接着再按一次就复制不到气泡了。
        if (wParam >= 0x20)
        {
            NotifyFocusInput(state);
        }
        break;

    case WM_NCDESTROY:
        // EDIT 子窗口销毁：摘子类过程、释放状态，避免悬挂与泄漏
        ::RemoveWindowSubclass(hwnd, EnterFilterProc, kEnterFilterSubclassId);
        delete state;
        break;

    default:
        break;
    }

    return ::DefSubclassProc(hwnd, msg, wParam, lParam);
}

} // namespace

namespace {

// 「@提醒」钩子在同一 EDIT 上的子类标识。与回车过滤器的 id 不同，两者可共存。
const UINT_PTR kMentionHookSubclassId = 2;

/**
 *  「@提醒」钩子的私有状态，随子类过程挂在 EDIT 上，WM_NCDESTROY 时释放。
 *  刻意<u>不</u>存"当前是否在 @ 中"之类的状态 —— 那份判断只在宿主一处维护，
 *  两边各存一份迟早会不同步。
 */
struct MentionHookState
{
    HWND m_hNotifyTarget;   // 宿主群聊窗
    UINT m_changedMsg;      // 输入可能变化：PostMessage 给宿主
    UINT m_navMsg;          // 导航键：SendMessage 给宿主，返回非 0 表示被消费
    UINT m_wheelMsg;        // 鼠标滚轮：SendMessage 给宿主，返回非 0 表示被消费

    MentionHookState(HWND target, UINT changedMsg, UINT navMsg, UINT wheelMsg)
        : m_hNotifyTarget(target)
        , m_changedMsg(changedMsg)
        , m_navMsg(navMsg)
        , m_wheelMsg(wheelMsg)
    {
    }
};

/**
 *  EDIT 子窗口的「@提醒」子类过程。
 */
LRESULT CALLBACK MentionHookProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam,
                                 UINT_PTR /*uIdSubclass*/, DWORD_PTR dwRefData)
{
    MentionHookState* state = reinterpret_cast<MentionHookState*>(dwRefData);

    switch (msg)
    {
    case WM_KILLFOCUS:
        // 输入框失焦（用户点到了别处 / 切了窗口）：让宿主收起浮层。wParam 传 1 表示
        // "强制收起"，宿主不必再去算前缀 —— 此刻光标可能还停在 @ 片段里，按前缀算
        // 反而会认为该继续显示。
        // 注意：浮层自身是 WS_EX_NOACTIVATE 的，点它不会让 EDIT 失焦，所以不会误伤
        // "点浮层选人"这条正常路径。
        if (state != NULL)
        {
            ::PostMessage(state->m_hNotifyTarget, state->m_changedMsg, 1, 0);
        }
        break;

    case WM_KEYDOWN:
        if (state != NULL)
        {
            // 只有这几个键在浮层显示时才有特殊含义；其余键一律放行。
            if (wParam == VK_UP || wParam == VK_DOWN || wParam == VK_RETURN
                || wParam == VK_TAB || wParam == VK_ESCAPE)
            {
                // 同步问宿主：浮层在不在、这次按键要不要给它。返回非 0 = 已消费。
                const LRESULT consumed = ::SendMessage(state->m_hNotifyTarget,
                                                       state->m_navMsg, wParam, 0);
                if (consumed != 0)
                {
                    return 0;   // 吞掉，不让 EDIT 再处理（回车不会换行、Tab 不会跳焦点）
                }
            }
        }
        break;

    case WM_MOUSEWHEEL:
        if (state != NULL)
        {
            // 同步问宿主：@ 浮层在不在、这次滚轮归不归它。返回非 0 = 已消费。
            // 消费掉就不让 EDIT 再处理 —— 否则候选列表滚了、底下的输入框也跟着滚。
            const short zDelta = (short)HIWORD(wParam);
            const LRESULT consumed = ::SendMessage(state->m_hNotifyTarget,
                                                   state->m_wheelMsg, (WPARAM)zDelta, 0);
            if (consumed != 0)
            {
                return 0;
            }
        }
        break;

    default:
        break;
    }

    // 先让 EDIT 把这次消息处理完（文本已经变了），再通知宿主重算前缀 ——
    // 顺序反了的话宿主读到的还是变化<u>之前</u>的文本。
    const LRESULT ret = ::DefSubclassProc(hwnd, msg, wParam, lParam);

    // 方向键 / 回车 / Tab / Esc 的<u>抬起</u>不算"文本可能变了"—— 这几个键根本改不了
    // 文本（按下时就已经被上面吞掉交给浮层了）。少了这道排除，按一下方向键会在抬起
    // 时投出"内容变了"，宿主重算前缀（还是原样）、照样刷新浮层，刚移动的高亮立刻被
    // 打回第一项，方向键等于废掉。
    const bool isNavKeyUp = (msg == WM_KEYUP)
                         && (wParam == VK_UP || wParam == VK_DOWN || wParam == VK_RETURN
                             || wParam == VK_TAB || wParam == VK_ESCAPE);

    if (state != NULL && !isNavKeyUp
        && (msg == WM_CHAR || msg == WM_KEYUP || msg == WM_IME_ENDCOMPOSITION
            || msg == WM_PASTE || msg == WM_CUT || msg == WM_LBUTTONUP))
    {
        // 打字、退格、粘贴、剪切都可能改动文本；点击会挪光标，同样要重算
        // （光标挪出 @ 片段就该收起浮层）。宿主自己去 EDIT 取文本与光标。
        ::PostMessage(state->m_hNotifyTarget, state->m_changedMsg, 0, 0);
    }

    if (msg == WM_NCDESTROY)
    {
        ::RemoveWindowSubclass(hwnd, MentionHookProc, kMentionHookSubclassId);
        delete state;
    }

    return ret;
}

// 「正在输入」钩子在同一 EDIT 子窗口上的唯一标识（1 回车过滤器、2 @ 提醒钩子）。
const UINT_PTR kTypingHookSubclassId = 3;

/**
 *  「正在输入」钩子的私有状态，随子类过程挂在 EDIT 上，WM_NCDESTROY 时释放。
 *  与 @ 提醒钩子同理，这里<u>不</u>存"当前是否在输入"之类的判断 —— 那份状态由宿主
 *  的节流器一处维护，两边各存一份迟早不同步。
 */
struct TypingHookState
{
    HWND m_hNotifyTarget;   // 宿主聊天窗
    UINT m_changedMsg;      // 输入可能变化 / 失焦：PostMessage 给宿主

    TypingHookState(HWND target, UINT changedMsg)
        : m_hNotifyTarget(target)
        , m_changedMsg(changedMsg)
    {
    }
};

/**
 *  EDIT 子窗口的「正在输入」子类过程。只观察、不吞任何消息。
 */
LRESULT CALLBACK TypingHookProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam,
                                UINT_PTR /*uIdSubclass*/, DWORD_PTR dwRefData)
{
    TypingHookState* state = reinterpret_cast<TypingHookState*>(dwRefData);

    // 一律先放行给原窗口过程：本钩子只是旁观，文本要先真的变了宿主去取才准。
    LRESULT ret = ::DefSubclassProc(hwnd, msg, wParam, lParam);

    if (state != NULL)
    {
        if (msg == WM_KILLFOCUS)
        {
            // 输入框失焦（切窗口 / 点到别处）：用户多半不再打字了，让宿主立即收尾，
            // 不必等停手定时器。wParam 传 1 与普通变化区分。
            ::PostMessage(state->m_hNotifyTarget, state->m_changedMsg, 1, 0);
        }
        else if (msg == WM_CHAR || msg == WM_KEYUP || msg == WM_IME_ENDCOMPOSITION
                 || msg == WM_PASTE || msg == WM_CUT || msg == WM_CLEAR)
        {
            // 打字、退格、输入法上屏、粘贴、剪切、删除都可能改动文本。这里不判断
            // 内容变没变（判了也没用，宿主还要看空不空），交给宿主统一处理。
            ::PostMessage(state->m_hNotifyTarget, state->m_changedMsg, 0, 0);
        }
    }

    if (msg == WM_NCDESTROY)
    {
        ::RemoveWindowSubclass(hwnd, TypingHookProc, kTypingHookSubclassId);
        delete state;
    }

    return ret;
}

} // namespace

bool InstallChatInputTypingHook(HWND hEdit, HWND hNotifyTarget, UINT changedMsg)
{
    if (hEdit == NULL || !::IsWindow(hEdit))
        return false;

    // 幂等：同一 EDIT 若已挂过本钩子，先释放旧状态再重挂。
    DWORD_PTR oldRef = 0;
    if (::GetWindowSubclass(hEdit, TypingHookProc, kTypingHookSubclassId, &oldRef)
        && oldRef != 0)
    {
        delete reinterpret_cast<TypingHookState*>(oldRef);
    }

    TypingHookState* state = new TypingHookState(hNotifyTarget, changedMsg);
    if (!::SetWindowSubclass(hEdit, TypingHookProc, kTypingHookSubclassId,
                             reinterpret_cast<DWORD_PTR>(state)))
    {
        delete state;
        return false;
    }
    return true;
}

bool InstallChatInputMentionHook(HWND hEdit, HWND hNotifyTarget,
                                 UINT changedMsg, UINT navMsg, UINT wheelMsg)
{
    if (hEdit == NULL || !::IsWindow(hEdit))
        return false;

    // 幂等：同一 EDIT 若已挂过本钩子，先释放旧状态再重挂。
    DWORD_PTR oldRef = 0;
    if (::GetWindowSubclass(hEdit, MentionHookProc, kMentionHookSubclassId, &oldRef)
        && oldRef != 0)
    {
        delete reinterpret_cast<MentionHookState*>(oldRef);
    }

    MentionHookState* state = new MentionHookState(hNotifyTarget, changedMsg, navMsg, wheelMsg);
    if (!::SetWindowSubclass(hEdit, MentionHookProc, kMentionHookSubclassId,
                             reinterpret_cast<DWORD_PTR>(state)))
    {
        delete state;
        return false;
    }
    return true;
}

bool InstallChatInputEnterToSend(HWND hEdit, HWND hNotifyTarget, UINT sendMsg)
{
    if (hEdit == NULL || !::IsWindow(hEdit))
        return false;

    // 幂等：同一 EDIT 若已挂过本子类，先释放旧状态再重挂（HWND 重建后重复调用时安全）。
    DWORD_PTR oldRef = 0;
    if (::GetWindowSubclass(hEdit, EnterFilterProc, kEnterFilterSubclassId, &oldRef)
        && oldRef != 0)
    {
        delete reinterpret_cast<EnterFilterState*>(oldRef);
    }

    EnterFilterState* state = new EnterFilterState(hNotifyTarget, sendMsg);
    if (!::SetWindowSubclass(hEdit, EnterFilterProc, kEnterFilterSubclassId,
                             reinterpret_cast<DWORD_PTR>(state)))
    {
        delete state;
        return false;
    }
    return true;
}

} // namespace balloonchatwnd
