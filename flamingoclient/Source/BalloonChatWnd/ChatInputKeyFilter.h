/**
 *  ChatInputKeyFilter.h
 *
 *  聊天输入框回车键过滤器 —— 实现「按 Enter 发送、按 Ctrl+Enter 插入换行」。
 *
 *  聊天输入框用的是 balloonui 的 DuiEditHost，它内部托管一个真 Win32 EDIT 子
 *  窗口；多行模式下该 EDIT 带 ES_WANTRETURN，按 Enter 默认就是插入换行。要把
 *  「Enter=发送、Ctrl+Enter=换行」做出来，必须在那个 EDIT 子窗口那一层拦键盘。
 *  本模块不改 balloonui：通过 DuiEditHost 暴露的 HwndHostControl::GetHostedHwnd()
 *  拿到 EDIT 句柄后，用 SetWindowSubclass 挂一段子类过程拦 VK_RETURN。
 *
 *  单聊窗（CBalloonChatWindow）与群聊窗（CBalloonGroupChatWindow）共用本模块，
 *  避免两处各写一份键盘拦截。
 *
 *  典型用法（窗口在 EnsureCreated 成功之后调一次）：
 *      input->SetMultiLine(true);     // 这两步须在 EnsureCreated 之前
 *      input->SetWordWrap(true);
 *      input->EnsureCreated(m_hWnd);
 *      balloonchatwnd::InstallChatInputEnterToSend(
 *          input->GetHostedHwnd(), m_hWnd, kSendOnEnterMsg);
 *  窗口收到 kSendOnEnterMsg 自定义消息即调自己的 DoSend()。EDIT 子窗口销毁时
 *  子类过程会自摘并释放内部状态，调用方无需显式卸载；若因 SetMultiLine 等触发
 *  了 HWND 重建，则重建后须再调一次本函数。
 *
 *  balloonwj@qq.com   2026-06-02
 */
#pragma once

#include <windows.h>

namespace balloonchatwnd {

/**
 *  给聊天输入框内部的 EDIT 子窗口安装回车过滤器：Enter 发送、Ctrl+Enter 换行。
 *    hEdit：        DuiEditHost 内部 EDIT 子窗口句柄（GetHostedHwnd() 取得）；
 *                   为 NULL 或非窗口时本函数直接返回 false、不做任何事。
 *    hNotifyTarget：按 Enter 发送时 PostMessage 的目标窗口（一般是聊天窗 m_hWnd），
 *                   所有权不转移，调用方须保证其生命周期覆盖 EDIT 子窗口。
 *    sendMsg：      发送时投递给 hNotifyTarget 的自定义消息号（WM_USER 区间）。
 *  返回：true 安装成功；false 句柄非法。
 *  副作用：在 hEdit 上挂一段窗口子类过程，并 new 一小块内部状态，二者均在 EDIT
 *          子窗口 WM_NCDESTROY 时自动清理；对同一 hEdit 重复调用是幂等的（旧状态
 *          先释放再重挂）。
 */
bool InstallChatInputEnterToSend(HWND hEdit, HWND hNotifyTarget, UINT sendMsg);

/**
 *  给群聊输入框内部的 EDIT 子窗口安装「@提醒」键盘钩子。与上面的回车过滤器
 *  <u>相互独立</u>（各自一个子类 id、各自一份状态），互不影响；回车过滤器已稳定，
 *  不去改它。
 *
 *  钩子只做"把键盘事件转给宿主"，不持有任何 @ 状态 —— 浮层显隐、前缀是什么，
 *  全由宿主群聊窗判断。宿主需要的文本与光标位置可以直接从 EDIT 句柄取
 *  （EM_GETSEL + GetWindowText），没必要在钩子里再存一份、还要担心两边不同步。
 *
 *    hEdit：        DuiRichEditHost / DuiEditHost 内部 EDIT 子窗口句柄。
 *    hNotifyTarget：宿主群聊窗 HWND；所有权不转移。
 *    changedMsg：   输入内容可能变化时<u>投递</u>（PostMessage）给宿主的消息号。
 *                   打字、退格、删除都会触发；宿主收到后自行重算"光标前是不是
 *                   正在 @ 某个前缀"，据此显示 / 更新 / 收起浮层。
 *    navMsg：       方向键 / 回车 / Tab / Esc 时<u>同步发送</u>（SendMessage）给宿主的
 *                   消息号，wParam 为虚拟键码。宿主返回非 0 表示"这次按键我消费了"
 *                   （浮层正显示、用它来移动高亮或提交选中），此时本钩子吞掉该键，
 *                   不让 EDIT 再处理；返回 0 则原样放行。
 *                   之所以用同步 SendMessage：要不要吞键必须<u>当场</u>决定，异步投递
 *                   拿不到结果，键早就被 EDIT 处理掉了。
 *    wheelMsg：     鼠标滚轮时<u>同步发送</u>给宿主的消息号，wParam 为滚轮增量
 *                   （zDelta，正数向上）。宿主返回非 0 表示"浮层正显示、这次滚轮
 *                   归它"，此时本钩子吞掉该消息，输入框自身不滚。
 *                   为什么滚轮也要拐这一道：WM_MOUSEWHEEL 由系统投给<u>有焦点的</u>
 *                   窗口，而 @ 浮层是 WS_EX_NOACTIVATE、从不取焦点，滚轮消息全都
 *                   落在输入框上 —— 不转发的话，候选列表根本滚不动。
 *  返回：true 安装成功；false 句柄非法。
 *  副作用：在 hEdit 上挂一段窗口子类过程并 new 一小块状态，二者在 EDIT 的
 *          WM_NCDESTROY 时自动清理；对同一 hEdit 重复调用幂等。
 */
bool InstallChatInputMentionHook(HWND hEdit, HWND hNotifyTarget,
                                 UINT changedMsg, UINT navMsg, UINT wheelMsg);

/**
 *  给聊天输入框内部的 EDIT 子窗口安装「正在输入」钩子：内容可能变化时通知宿主，
 *  由宿主决定要不要向对端发一包 cmd_typing_notify。
 *
 *  与上面两个钩子<u>相互独立</u>（各自一个子类 id、各自一份状态）：回车过滤器与
 *  @ 提醒钩子都已稳定，不去改它们。本钩子刻意做得极薄 —— 只报"可能变了"，什么
 *  时候真发包、发几次全由宿主的节流器判断（见 TypingIndicator.h）。
 *
 *  之所以不复用 @ 提醒钩子的 changedMsg：那个钩子还要同步转发方向键与滚轮给宿主
 *  （@ 浮层要用），单聊窗用不上；而且群聊窗两者都要装时，两份状态各挂各的更清楚。
 *
 *    hEdit：        DuiEditHost / DuiRichEditHost 内部 EDIT 子窗口句柄
 *                   （GetHostedHwnd() 取得）；为 NULL 或非窗口时直接返回 false。
 *    hNotifyTarget：宿主聊天窗 HWND；所有权不转移，调用方须保证其生命周期覆盖
 *                   EDIT 子窗口。
 *    changedMsg：   投递（PostMessage）给宿主的消息号。wParam 为 1 表示输入框
 *                   <u>失焦</u>（宿主应立即收尾、发 state=0），为 0 表示普通的
 *                   内容变化（宿主自行去 EDIT 取文本判断空不空）。
 *  返回：true 安装成功；false 句柄非法。
 *  副作用：在 hEdit 上挂一段窗口子类过程并 new 一小块状态，二者在 EDIT 的
 *          WM_NCDESTROY 时自动清理；对同一 hEdit 重复调用幂等。
 */
bool InstallChatInputTypingHook(HWND hEdit, HWND hNotifyTarget, UINT changedMsg);

} // namespace balloonchatwnd
