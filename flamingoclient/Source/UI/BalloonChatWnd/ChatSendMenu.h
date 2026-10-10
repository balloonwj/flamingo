/**
 *  ChatSendMenu.h
 *
 *  单聊窗、群聊窗发送按钮右侧 ▾ 菜单（docs/快捷回复方案.md §3.2）。两个窗口原来各写一份「按 Enter / Ctrl+Enter
 *  发送」的菜单，加入快捷回复后合并到这里：
 *
 *    · BuildChatSendMenuModel：按快捷回复与当前发送键生成菜单项（纯逻辑，便于单测）；
 *    · QuickReplyMenuText：一条快捷回复在菜单里显示的文字（单行、截断、转义 '&'）；
 *    · ResolveChatSendMenuCommand：把 DuiMenu 返回的命令 id 换回「点了哪一项」；
 *    · PopupChatSendMenu：在发送按钮上方弹出菜单并返回结果（同步阻塞，内部调用 DuiMenu）。
 *
 *  菜单从上到下：标题「快捷回复」、非空的快捷回复、分隔线、两个发送键勾选项、分隔线、「设置快捷回复…」。
 *  三条快捷回复都为空时去掉标题和第一条分隔线。
 *
 *  典型用法（聊天窗收到 ▾ 被点的通知后）：
 *      std::vector<CString> replies = QuickRepliesFromConfig(...);
 *      ChatSendMenuCommand cmd = PopupChatSendMenu(m_hWnd, anchorTopLeft, replies, GetChatSendMode() == kSendOnEnter);
 *      if (cmd.m_kind == kSendMenuQuickReply)
 *          SendQuickReply(replies[cmd.m_replyIndex]);
 *
 *  balloonwj@qq.com   2026-10-06
 */
#pragma once

#include <vector>
#include <windows.h>

namespace balloonchatwnd {

// 快捷回复的条数：固定三条（2026-10-06 用户确认）
const int kQuickReplyCount = 3;

// 每条快捷回复最多多少个字（UTF-16 字符，设置页输入框的上限）
const int kQuickReplyMaxChars = 50;

// 菜单里一条快捷回复最多显示多少个字，超过时截断并加「…」；点击后发送的仍是完整原文
const int kQuickReplyMenuMaxChars = 20;

// ▾ 菜单一项的种类
enum ChatSendMenuItemKind
{
    // 什么都没点（菜单被取消），只用于 ResolveChatSendMenuCommand 的返回值
    kSendMenuNone = 0,
    // 标题行「快捷回复」：灰色小字，不可点
    kSendMenuHeader = 1,
    // 一条快捷回复：点击后直接发送
    kSendMenuQuickReply = 2,
    // 分隔线
    kSendMenuSeparator = 3,
    // 「按 Enter 发送消息」勾选项
    kSendMenuEnterKey = 4,
    // 「按 Ctrl+Enter 发送消息」勾选项
    kSendMenuCtrlEnterKey = 5,
    // 「设置快捷回复…」：打开系统设置的「聊天」页
    kSendMenuSettings = 6,
};

// 菜单命令 id。0 是 DuiMenu「没有选」的返回值，不能用
const UINT kSendMenuCmdEnter = 1;            // 按 Enter 发送消息
const UINT kSendMenuCmdCtrlEnter = 2;        // 按 Ctrl+Enter 发送消息
const UINT kSendMenuCmdSettings = 3;         // 设置快捷回复…
const UINT kSendMenuCmdQuickReplyBase = 10;  // 第 i 条快捷回复的 id 为 10 + i（i 从 0 起）

/**
 *  ▾ 菜单的一项（BuildChatSendMenuModel 的输出）。
 */
struct ChatSendMenuItem
{
    int     m_kind;         // 种类，取值见 ChatSendMenuItemKind
    UINT    m_cmdId;        // 命令 id；标题与分隔线为 0
    CString m_text;         // 显示文字（快捷回复已经过 QuickReplyMenuText 处理）；分隔线为空
    bool    m_checked;      // 发送键两项是否打勾；其它项为 false
    int     m_replyIndex;   // 快捷回复是第几条（0 起，对应配置里的位置）；其它项为 -1

    ChatSendMenuItem()
        : m_kind(kSendMenuNone)
        , m_cmdId(0)
        , m_checked(false)
        , m_replyIndex(-1)
    {
    }
};

/**
 *  菜单点击结果（ResolveChatSendMenuCommand / PopupChatSendMenu 的输出）。
 */
struct ChatSendMenuCommand
{
    int m_kind;         // 点了哪一种，取值见 ChatSendMenuItemKind；kSendMenuNone 表示取消
    int m_replyIndex;   // m_kind 为 kSendMenuQuickReply 时是第几条快捷回复（0 起）；其它为 -1

    ChatSendMenuCommand()
        : m_kind(kSendMenuNone)
        , m_replyIndex(-1)
    {
    }
};

/**
 *  一条快捷回复在菜单里显示的文字：换行（\r、\n）变成空格，超过 kQuickReplyMenuMaxChars 个字时截断并加「…」
 *  （代理对按 2 个 UTF-16 字符计，不会从中间切开），'&' 转义为 "&&"（DuiMenu 把 '&' 当成助记符）。
 *    reply：快捷回复原文。
 *  @return 显示文字。本函数无副作用。
 */
CString QuickReplyMenuText(const CString& reply);

/**
 *  生成 ▾ 菜单的各项。
 *    quickReplies：快捷回复，按配置里的位置排列（通常 kQuickReplyCount 条）；空串的位置不出菜单项，但后面各条
 *                  的 m_replyIndex 仍按原位置编号。
 *    enterToSend： 当前发送键是否为 Enter（决定哪一项打勾）。
 *  @return 菜单项，从上到下。本函数无副作用。
 */
std::vector<ChatSendMenuItem> BuildChatSendMenuModel(const std::vector<CString>& quickReplies, bool enterToSend);

/**
 *  把 DuiMenu::TrackPopup 返回的命令 id 换回点击结果。
 *    cmdId：命令 id；0 表示菜单被取消。
 *    replyCount：快捷回复的条数（用于判断快捷回复的 id 是否越界）。
 *  @return 点击结果；不认识的 id 返回 kSendMenuNone。
 */
ChatSendMenuCommand ResolveChatSendMenuCommand(UINT cmdId, int replyCount);

/**
 *  把一条快捷回复的原文编码成发送正文，规则与输入框发送的纯文字相同（balloonchat::BuildSendBody：
 *  只给会被误认成表情标记的斜杠加转义）。快捷回复是纯文字，不含表情。
 *    reply：快捷回复原文。
 *  @return 发送正文；原文为空时返回空串。本函数无副作用。
 */
CString QuickReplyBody(const CString& reply);

/**
 *  在发送按钮上方弹出 ▾ 菜单，同步阻塞到用户点了某项或取消。菜单底边与锚点之间留一小段间隙，菜单高度按
 *  DuiMenu::MeasureSize 计算；越出屏幕时由 DuiMenu 自己翻转。
 *    owner：        所有者窗口（菜单关闭后焦点回到这里）。
 *    anchorTopLeft：发送按钮左上角的屏幕坐标。
 *    quickReplies： 快捷回复（同 BuildChatSendMenuModel）。
 *    enterToSend：  当前发送键是否为 Enter。
 *  @return 点击结果。
 */
ChatSendMenuCommand PopupChatSendMenu(HWND owner, POINT anchorTopLeft,
                                      const std::vector<CString>& quickReplies, bool enterToSend);

} // namespace balloonchatwnd
