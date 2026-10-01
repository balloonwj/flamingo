/**
 *  「清理聊天记录」对话框的流程状态，ChatCleanFlow.h
 *
 *  把对话框里容易出错的判断从界面代码里拿出来单独测试（docs/清理聊天记录方案.md §11.3）：
 *    · 「清理」按钮什么时候可用、显示什么文字，以及按钮下方提示为什么不能清理；
 *    · 点「清理」之后的"清理中"、成功、失败、超时、断线几种走向；
 *    · 选预设档位时程序会自动改写各个下拉框，而 DuiComboBox 在程序赋值时也会发变化通知，
 *      须把"程序填入"与"用户手动修改"区分开，只有后者才把档位切到「自定义」。
 *  纯逻辑，不碰窗口、定时器与网络：对话框把事件转告本类，再按本类的查询结果刷新界面。
 *  单元测试见 flamingoclient/Source/Tests/ChatCleanFlowTests.cpp（Suite CF，标签 [chatclean]）。
 *
 *  典型用法（对话框内）：
 *      m_flow.BeginProgramFill();
 *      ...给各个下拉框赋值...
 *      m_flow.EndProgramFill();
 *      ...
 *      if (m_flow.OnCleanClicked(CustomToRange(...)))
 *          CIUSocket::GetInstance().DeleteMessagesByTime(beginMs, endMs);
 *      RefreshButton(m_flow.IsCleanEnabled(), m_flow.CleanButtonText(), m_flow.Hint());
 *
 *  balloonwj@qq.com   2026-10-01
 */

#pragma once

#include <string>

namespace balloonsettings {

/**
 *  清理流程所处的阶段。
 */
enum ChatCleanStage
{
    kCleanStageIdle    = 0,   // 等待用户操作：刚打开、失败或超时之后都回到这里
    kCleanStageRunning = 1,   // 已发出清理请求、正在等应答
    kCleanStageDone    = 2    // 服务端报告清理成功；对话框据此提示结果并关闭
};

/**
 *  ChatCleanFlow —— 清理对话框的流程状态机（每次打开对话框时重新构造或 Reset）。
 */
class ChatCleanFlow
{
public:
    ChatCleanFlow();

    /**
     *  回到刚打开对话框时的状态：等待操作、档位为 initialPreset、没有在途请求、没有提示文字。
     *  连接状态不变，由调用方随后按实际情况 SetConnected。
     *    initialPreset：打开时默认选中的档位，取值见 ChatCleanPreset。
     */
    void Reset(int initialPreset);

    /**
     *  告知当前是否已连接服务器（打开对话框时，以及连接状态变化时调用）。
     *  未连接时「清理」置灰并提示原因。清理中断开连接时，这次请求的应答不会再到达，
     *  流程回到等待操作，提示"与服务器的连接已断开，请重连后重试"。
     *    connected：true 表示已连接且已登录。
     */
    void SetConnected(bool connected);

    // 当前是否已连接服务器。
    bool IsConnected() const;

    /**
     *  开始 / 结束一段"程序填入"：两次调用之间收到的下拉框变化通知一律视为程序赋值，
     *  不切换档位。支持嵌套，以最外层的 End 为准。
     */
    void BeginProgramFill();
    void EndProgramFill();

    /**
     *  用户在「时间范围」下拉框里选了一个档位（程序填入期间的调用被忽略）。
     *    preset：档位，取值见 ChatCleanPreset；越界时忽略。
     *  @return 档位确实被改成了 preset 时返回 true。调用方据此用 PresetToRange 填好两组时间。
     */
    bool OnPresetSelected(int preset);

    /**
     *  开始或结束那组时间下拉框里某一个发生了变化。
     *  程序填入期间视为程序赋值，不做任何事；否则视为用户手动修改，档位切到「自定义」，
     *  其他下拉框保持不变。
     *  @return 档位因此被切到「自定义」时返回 true（调用方据此把「时间范围」下拉框改为自定义）。
     */
    bool OnTimeFieldChanged();

    // 当前档位，取值见 ChatCleanPreset。
    int Preset() const;

    /**
     *  用户点了「清理」（确认框之前调用）。
     *    rangeError：当前时间范围的校验结果，取值见 ChatCleanRangeError；预设档位传 kCleanRangeOk。
     *  @return true 表示可以弹确认框并发起请求；未连接、已在清理中、已完成时返回 false；区间不合法时
     *          返回 false 并把原因放进 Hint()。
     */
    bool CanStartClean(int rangeError);

    /**
     *  用户在确认框里点了确定、请求已经发出：进入"清理中"，按钮置灰并显示"清理中…"。
     *  调用方随后自行启动超时定时器（时长见 kChatCleanTimeoutMs）。
     */
    void OnRequestSent();

    /**
     *  收到清理应答。
     *  没有在途请求时（如对话框重新打开过）忽略。成功时进入"完成"；失败时回到等待操作，
     *  并按错误码给出原因。超时之后才到的应答照常处理 —— 服务端其实已经执行过了。
     *    code：应答里的 code，0 表示成功，其余取值见 Protocol.h 的 enum error_code。
     *  @return 应答被处理（未被忽略）时返回 true。
     */
    bool OnResult(int code);

    /**
     *  等应答超时（调用方的定时器到点时调用）。仍在清理中时回到等待操作，提示"服务器没有响应，
     *  请稍后重试"；这次请求仍算在途，之后应答才到时照常处理。不在清理中时忽略。
     */
    void OnTimeout();

    // 当前阶段，取值见 ChatCleanStage。
    int Stage() const;

    // 「清理」按钮是否可用：已连接且处于等待操作时可用。
    bool IsCleanEnabled() const;

    // 「清理」按钮上的文字：清理中为"清理中…"，其余为"清理"。
    const wchar_t* CleanButtonText() const;

    /**
     *  按钮下方的提示文字：未连接、区间不合法、失败、超时、断线时说明原因；没有要说明的时候为空串。
     *  用户重新选择档位或修改时间后清空。
     */
    const std::wstring& Hint() const;

private:
    // 按当前连接状态重新计算提示：未连接时写入未连接的原因，已连接时清掉未连接的原因。
    void RefreshOfflineHint();

    int          m_stage;          // 当前阶段，取值见 ChatCleanStage
    int          m_preset;         // 当前档位，取值见 ChatCleanPreset
    bool         m_connected;      // 是否已连接服务器；构造时为 false，由对话框打开时按实际情况设置
    bool         m_awaitingResult; // 是否有一个已发出、尚未收到应答的请求（超时后仍为 true，断线后清掉）
    int          m_fillDepth;      // "程序填入"的嵌套层数；大于 0 时下拉框变化通知不视为用户修改
    std::wstring m_hint;           // 按钮下方的提示文字，空串表示不显示
};

// 等清理应答的超时时长（毫秒）：15 秒，与方案 §11.3 一致
const unsigned int kChatCleanTimeoutMs = 15000;

} // namespace balloonsettings
