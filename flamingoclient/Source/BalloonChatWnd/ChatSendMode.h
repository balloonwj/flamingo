/**
 *  ChatSendMode.h
 *
 *  聊天「发送键模式」—— 全局共享、持久化到 flamingo.ini。决定在聊天输入框里
 *  按哪个键发送、按哪个键换行；单聊 / 群聊所有窗口共用同一份模式。
 *
 *  在发送按钮的下拉菜单里切换；切换后写回 flamingo.ini（键值为 ASCII，遵守
 *  flamingo.ini 须保持 ASCII 的约束），客户端重启后保留上次选择。回车过滤器
 *  ChatInputKeyFilter 每次按回车时读取本模块决定发送/换行；窗口据本模块刷新
 *  左下角提示文字与输入框 placeholder。
 *
 *  balloonwj@qq.com   2026-06-03
 */
#pragma once

#include <windows.h>

namespace balloonchatwnd {

//发送键模式：决定按哪个键发送、哪个键换行。
enum SendKeyMode
{
    kSendOnEnter     = 0,   //按 Enter 发送，Ctrl+Enter 换行（默认）
    kSendOnCtrlEnter = 1,   //按 Ctrl+Enter 发送，Enter 换行
};

/**
 *  取当前全局发送键模式。首次调用时从 flamingo.ini 读入并缓存到进程内，
 *  之后直接返回缓存值。
 *  返回：当前发送键模式（默认为 kSendOnEnter）。
 */
SendKeyMode GetChatSendMode();

/**
 *  设置全局发送键模式并写回 flamingo.ini。立即生效——所有聊天窗口下次按回车
 *  即按新模式处理（回车过滤器实时读取本模块，无需逐窗口通知）。
 *    mode：要切换到的发送键模式。
 */
void SetChatSendMode(SendKeyMode mode);

/**
 *  取某模式下「左下角提示」应显示的界面文案。
 *    mode：发送键模式。
 *  返回：kSendOnEnter → "提示：按 Ctrl+Enter 换行"；
 *        kSendOnCtrlEnter → "提示：按 Enter 换行"。
 */
LPCTSTR GetChatSendHintText(SendKeyMode mode);

/**
 *  取某模式下输入框 placeholder 应显示的界面文案（与提示保持一致，避免空框时
 *  提示与实际行为矛盾）。
 *    mode：发送键模式。
 *  返回：kSendOnEnter → "输入消息，Enter 发送，Ctrl+Enter 换行"；
 *        kSendOnCtrlEnter → "输入消息，Ctrl+Enter 发送，Enter 换行"。
 */
LPCTSTR GetChatSendPlaceholder(SendKeyMode mode);

} // namespace balloonchatwnd
