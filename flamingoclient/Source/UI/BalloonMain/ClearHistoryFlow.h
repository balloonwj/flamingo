/**
 *  ClearHistoryFlow.h
 *
 *  「清空聊天记录」（本人视角按会话清空，cmd_clear_conversation=1523）流程里不碰窗口的那部分逻辑：
 *    · 确认框文字：说明只清空本人这一方、本人登录的其他设备同步清空、对方或其他群成员不受影响、
 *      清空后无法恢复；
 *    · 失败提示：按服务端错误码给出具体原因；
 *    · ClearHistoryTracker：登记"本端发出、正在等服务端应答"的清空请求。失败应答只带 code / msg、
 *      不带会话，故按请求帧 seq 对上是哪个会话，以便提示里写出会话名、并把提示框弹在发起的窗口上。
 *
 *  清空成功后的界面变化（删本地库、清空已打开聊天窗、移除会话行等）不在这里：本设备与本人其他设备
 *  统一在收到 cmd_messages_deleted_notify 的清空形式时处理，见 CBalloonMainPanel::OnMessagesDeleted。
 *
 *  由 CBalloonMainPanel 使用；纯函数与纯数据，不创建窗口，故可被 Flamingo.vcxproj 与
 *  FlamingoUnit.vcxproj 两个工程逐字共用（在 Flamingo.vcxproj 里 .cpp 需标记 PrecompiledHeader=NotUsing）。
 *
 *  典型用法：
 *      std::wstring tip = BuildClearHistoryConfirmText(ctype, name);   // 弹确认框
 *      uint32_t seq = CIUSocket::GetInstance().ClearConversation(ctype, cid);
 *      tracker.Begin(seq, pending);                                     // 发出之后登记
 *      ClearHistoryOutcome r = tracker.OnResult(seq, code);             // 收到应答时
 *      if (r.m_matched && !r.m_succeeded) { 提示 r.m_errorText; }
 *
 *  balloonwj@qq.com   2026-10-02
 */
#pragma once

#include <stdint.h>
#include <map>
#include <string>
#include <windows.h>   // HWND

namespace balloonmain {

/**
 *  PendingClearHistory —— 一条已发出、正在等服务端应答的清空请求。
 */
struct PendingClearHistory
{
    int          m_ctype;   // 会话类型：1 单聊 2 群聊
    int64_t      m_cid;     // 本人视角的会话ID（单聊为对方 userid，群聊为群ID）
    std::wstring m_name;    // 发出时的会话名（联系人显示名或群名），用于失败提示；可为空
    HWND         m_ownerHwnd;   // 发起清空时确认框所属的窗口（如消息记录窗），失败提示据此决定弹在哪里，
                                // 见 ChooseClearFailureOwner；只作比较与判断有效性用，本结构不持有它

    PendingClearHistory()
        : m_ctype(0)
        , m_cid(0)
        , m_ownerHwnd(NULL)
    {
    }
};

/**
 *  ClearHistoryOutcome —— ClearHistoryTracker::OnResult 对一条应答给出的处理结论。
 */
struct ClearHistoryOutcome
{
    bool                m_matched;    // 是否对上了本端登记过的请求；false 表示该 seq 没有登记（重连前发出的旧请求等），调用方只记日志
    bool                m_succeeded;  // 服务端是否执行成功（code == 0）
    std::wstring        m_errorText;  // 失败时给用户的提示；成功或未对上时为空
    PendingClearHistory m_op;         // 对上的那条登记（m_matched 为 false 时无意义）

    ClearHistoryOutcome()
        : m_matched(false)
        , m_succeeded(false)
    {
    }
};

/**
 *  构造「清空聊天记录」确认框的正文。
 *    ctype：会话类型，1 单聊 2 群聊；其它值按单聊的说法处理。
 *    name： 会话名（单聊为联系人显示名，群聊为群名）；为空时改用"该联系人" / "该群"的说法，
 *           不会出现空的书名号。
 *  @return 确认框正文：第一段是问句，第二段说明影响范围与不可恢复。
 */
std::wstring BuildClearHistoryConfirmText(int ctype, const std::wstring& name);

/**
 *  按服务端错误码构造清空失败的提示。
 *    code： cmd_clear_conversation 应答里的 code（enum error_code）；0 表示成功。
 *    ctype：会话类型，1 单聊 2 群聊，用于选择说法。
 *    name： 会话名；为空时提示里不写会话名。
 *  @return 提示文字；code 为 0 时返回空串。
 */
std::wstring BuildClearHistoryErrorText(int code, int ctype, const std::wstring& name);

/**
 *  ClearHistoryTracker —— 登记本端发出的清空请求，应答到达时按请求帧 seq 对上是哪个会话。
 *
 *  不用先进先出队列而用 seq：连接断开时已发出的请求收不到应答，队列会从此错位；按 seq 对上则
 *  只会留下几条永远等不到应答的登记，不影响之后的请求。非线程安全，只在 UI 线程使用。
 */
class ClearHistoryTracker
{
public:
    /**
     *  登记一条刚发出的请求。
     *    seq：    CIUSocket 发出该请求时用的帧序号；为 0（表示没有发出）时不登记。
     *    pending：请求的上下文（会话类型、会话ID、会话名），按值复制保存。
     */
    void Begin(uint32_t seq, const PendingClearHistory& pending);

    /**
     *  收到一条清空应答：按 seq 取出对应的登记并给出结论。对上的登记随即删除，同一 seq 再来一次
     *  不会重复认领。
     *    seq： 应答帧的序号（服务端原样带回请求帧的 seq）。
     *    code：应答里的 code。
     *  @return 处理结论，见 ClearHistoryOutcome。
     */
    ClearHistoryOutcome OnResult(uint32_t seq, int code);

    /**
     *  当前仍在等待应答的登记条数（供单元测试与日志使用）。
     */
    size_t PendingCount() const;

private:
    std::map<uint32_t, PendingClearHistory> m_pending;  // 帧序号 → 请求上下文；收到应答时删除，生命周期与主面板相同
};

/**
 *  清空失败时提示框该弹在哪个窗口上。消息记录窗发起的清空，提示应当弹在消息记录窗上；主面板右键
 *  菜单发起的（传的是主面板自己），沿用原来的规则：弹在该会话的聊天窗上，没有聊天窗时弹在主面板上。
 *    requester         发起清空时的确认框所属窗口（PendingClearHistory::m_ownerHwnd）
 *    requesterIsWindow requester 现在是否仍是有效窗口（调用方用 ::IsWindow 判断）
 *    mainPanel         主面板窗口句柄
 *    conversationOwner 按会话找到的窗口：该会话的聊天窗，没有时为主面板
 *  @return requester 仍有效、且不是主面板时返回 requester；否则返回 conversationOwner
 */
HWND ChooseClearFailureOwner(HWND requester, bool requesterIsWindow, HWND mainPanel, HWND conversationOwner);

} // namespace balloonmain
