/**
 *  聊天窗输入框控件 ChatInputRichEdit —— 无窗口富文本控件的聊天定制子类。
 *
 *  它承接的是原先挂在输入框子窗口上的那几段子类过程（那套实现位于已删除的
 *  ChatInputKeyFilter.h / .cpp，可在 2026-08-16 之前的提交里查阅）：回车发送 /
 *  换行、「正在输入」通知、焦点区域上报、以及群聊的「@提醒」键盘与滚轮转发。
 *  输入框改用无窗口富文本控件之后没有子窗口可挂，这些判断改由控件自己的按键、
 *  字符、焦点、滚轮处理函数完成。
 *
 *  **与聊天窗之间的消息协议原样保留** —— 发什么消息、什么时机发、同步还是异步、
 *  返回值怎么解释，全部与子类过程版本一致。因此聊天窗那一侧接消息的代码不需要
 *  任何改动，这次替换只换实现机制、不换契约。
 *
 *  典型用法（聊天窗构建输入区时）：
 *      std::unique_ptr<ChatInputRichEdit> edit(new ChatInputRichEdit());
 *      edit->SetMultiLine(true);
 *      edit->SetWordWrap(true);
 *      edit->SetNotifyTarget(m_hWnd);
 *      edit->SetSendOnEnterMsg(kSendOnEnterMsg);
 *      edit->SetTypingChangedMsg(kInputChangedMsg);
 *      // 群聊还要：
 *      edit->SetMentionMsgs(kMentionChangedMsg, kMentionNavMsg, kMentionWheelMsg);
 *
 *  balloonwj@qq.com   2026-08-16
 */
#pragma once

#include "../../../third-party/balloonui/balloonui/Controls/Input/DuiRichEdit.h"

namespace balloonchatwnd {

/**
 *  聊天输入框控件。除下列定制外，行为与基类完全一致。
 *
 *  几个消息号都默认为 0，表示「不启用该项」—— 单聊窗不设 @ 提醒那三个即可，
 *  不必为此再分出一个子类。
 */
class ChatInputRichEdit : public balloonwjui::DuiRichEdit
{
public:
    ChatInputRichEdit();

    /**
     *  设置接收各类通知的宿主窗口。
     *    hWnd：聊天窗句柄；所有权不转移，调用方须保证其生命周期覆盖本控件。
     *          传 NULL 表示不再通知任何窗口（本控件的其余行为不受影响）。
     */
    void SetNotifyTarget(HWND hWnd);

    /**
     *  设置「按回车发送」时投递给宿主的消息号。
     *    nMsg：自定义消息号；0 表示不启用发送（回车一律当换行处理）。
     *
     *  这次回车究竟是发送还是换行，由 SetSendOnEnterQuery 注册的回调判断；
     *  没注册时按最常见的约定处理（不按 Ctrl 即发送）。
     */
    void SetSendOnEnterMsg(UINT nMsg);

    /**
     *  「这次回车是发送还是换行」的查询回调原型。
     *    bCtrlDown：按下回车时 Ctrl 是否也按着。
     *    pCtx：     注册时传入的上下文，原样透传。
     *    返回：     true 表示发送，false 表示插入换行。
     */
    typedef bool (*SendOnEnterQueryFn)(bool bCtrlDown, void* pCtx);

    /**
     *  注册「回车含义」的查询回调。
     *
     *  之所以由调用方注入、而不是控件自己去读全局的发送模式设置：那个设置存在
     *  配置文件里，读它要牵进配置文件读写与主程序目录这一整条依赖链，而本控件
     *  会被编进单元测试工程 —— 那边刻意不引入这些。把判断挪到调用方之后，控件
     *  只剩纯粹的键盘处理，测试工程也不必为此链接一堆无关模块。
     *
     *    fn：  查询回调；传 NULL 表示恢复默认（不按 Ctrl 即发送）。
     *    pCtx：透传给回调的上下文，可为 NULL。
     */
    void SetSendOnEnterQuery(SendOnEnterQueryFn fn, void* pCtx);

    /**
     *  设置「正在输入」通知的消息号。
     *    nMsg：自定义消息号；0 表示不启用。
     *
     *  投递（异步）给宿主，wParam 为 1 表示输入框失焦（宿主应立即收尾），
     *  为 0 表示普通的内容变化。本控件只报「可能变了」，什么时候真发包由
     *  宿主的节流器决定。
     */
    void SetTypingChangedMsg(UINT nMsg);

    /**
     *  设置群聊「@提醒」用的三个消息号。单聊窗不必调用。
     *    nChangedMsg：内容可能变化时**投递**给宿主，宿主自行重算 @ 前缀。
     *    nNavMsg：    方向键 / 回车 / Tab / Esc 时**同步发送**给宿主，
     *                 wParam 为虚拟键码。宿主返回非 0 表示这次按键归它
     *                 （浮层正显示、用来移动高亮或提交选中），本控件随即
     *                 吞掉该键。用同步发送是因为「吞不吞」必须当场决定。
     *    nWheelMsg：  滚轮时**同步发送**给宿主，wParam 为滚轮增量。宿主返回
     *                 非 0 表示滚轮归浮层，本控件不滚自己。
     *                 滚轮要拐这一道的原因：@ 浮层从不取焦点，滚轮消息全都
     *                 落在输入框上，不转发的话候选列表根本滚不动。
     *  三者任一为 0 表示该项不启用。
     */
    void SetMentionMsgs(UINT nChangedMsg, UINT nNavMsg, UINT nWheelMsg);

    // ---- 基类覆写 ----
    bool OnKeyDown(UINT vk, UINT flags) override;
    bool OnChar(TCHAR ch) override;
    bool OnSetFocus() override;
    bool OnKillFocus() override;
    bool OnLButtonDown(POINT pt, UINT mkFlags) override;
    bool OnMouseWheel(POINT pt, short zDelta, UINT mkFlags) override;
    bool OnRawMessage(UINT uMsg, WPARAM wParam, LPARAM lParam,
                      LRESULT& lResult) override;
    HRESULT TxSiteNotify(DWORD iNotify, void* pv) override;

private:
    //告诉宿主「用户现在在发送框操作」。宿主据此决定 Ctrl+C 归谁。
    void NotifyFocusInput();

    //投递一次「正在输入」通知。
    //  bKillFocus：true 表示这次是失焦引发的，宿主应立即收尾。
    void NotifyTyping(bool bKillFocus);

    //宿主窗口是否可用。
    bool HasTarget() const;

    HWND m_hNotifyTarget;      // 宿主聊天窗；不持有所有权
    UINT m_nSendOnEnterMsg;    // 按回车发送时投递的消息号；0 = 不启用
    UINT m_nTypingChangedMsg;  // 「正在输入」通知的消息号；0 = 不启用
    UINT m_nMentionChangedMsg; // @提醒：内容变化，投递；0 = 不启用
    UINT m_nMentionNavMsg;     // @提醒：导航键，同步发送；0 = 不启用
    UINT m_nMentionWheelMsg;   // @提醒：滚轮，同步发送；0 = 不启用

    //是否正处于输入法合成期。合成期内的回车是「候选词上屏」，不能当成发送。
    bool m_bImeComposing;

    SendOnEnterQueryFn m_pfnSendQuery;   // 回车含义的查询回调；NULL = 用默认判断
    void*              m_pSendQueryCtx;  // 上述回调的上下文；不持有所有权
};

} // namespace balloonchatwnd
