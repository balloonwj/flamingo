/**
 *  聊天内容的剪贴板读写，ChatClipboardIo.h
 *
 *  ChatClipboardContent 负责把聊天内容拆成片段、序列化与解析（纯计算）；本文件负责
 *  真正读写系统剪贴板：
 *    · 复制时在同一次打开剪贴板里写入：纯文字（CF_UNICODETEXT）、Flamingo 自己的格式；
 *      复制的恰好是一张图片时再加一份位图（CF_BITMAP），方便粘到画图、Word 等其它程序；
 *    · 粘贴时读出 Flamingo 自己的格式并还原成片段。
 *
 *  典型用法（复制）：
 *      WriteChatContentToClipboard(hwnd, segs);
 *  典型用法（粘贴）：
 *      std::vector<ChatClipSegment> segs;
 *      if (ReadChatContentFromClipboard(hwnd, segs)) { 按顺序插入输入框; }
 *
 *  读写剪贴板会影响用户正在用的剪贴板内容，所以本文件不做单元测试；可测的部分都在
 *  ChatClipboardContent 里。
 *
 *  balloonwj@qq.com   2026-09-30
 */

#pragma once

#include <windows.h>
#include <vector>

#include "ChatClipboardContent.h"   // ChatClipSegment

namespace balloonchatwnd {

/**
 *  申请一块可移动的全局内存并把 data 拷进去，供 SetClipboardData、拖拽数据对象使用。
 *    data / bytes：要拷贝的数据与字节数；bytes 为 0 时仍申请一块空内存。
 *  @return 成功返回句柄，交出去之前所有权在调用方（失败时须 GlobalFree）；失败返回 NULL。
 */
HGLOBAL AllocGlobalCopy(const void* data, size_t bytes);

/**
 *  取 Flamingo 聊天内容的剪贴板格式编号（按 kChatClipFormatName 注册，进程内只注册一次）。
 *  @return 格式编号；注册失败时返回 0。
 */
UINT GetChatClipFormat();

/**
 *  当前剪贴板里是否有 Flamingo 聊天内容。不打开剪贴板，开销很小。
 */
bool IsChatContentOnClipboard();

/**
 *  把聊天内容写进剪贴板（会先清空剪贴板原有内容）。
 *    owner：剪贴板的归属窗口，传宿主聊天窗的句柄；可以为 NULL（归属当前任务）。
 *    segs： 片段序列；为空时什么都不写。
 *  @return 至少写入了纯文字时返回 true；序列为空、剪贴板被别的程序占着打不开时返回 false。
 *  副作用：清空剪贴板；写入纯文字与 Flamingo 格式，恰好只有一张图片时再写一份位图
 *          （位图生成失败只是少这一份，不影响其余两种）。
 */
bool WriteChatContentToClipboard(HWND owner, const std::vector<ChatClipSegment>& segs);

/**
 *  从剪贴板读出 Flamingo 聊天内容。
 *    owner：打开剪贴板时的归属窗口，可以为 NULL。
 *    out：  [出参] 还原出的片段序列；读取失败时被清空。
 *  @return 剪贴板里有 Flamingo 格式、并且解析成功时返回 true（片段可能为空，例如全部片段
 *          都非法被丢弃）；没有这种格式、打不开剪贴板、解析失败时返回 false。
 */
bool ReadChatContentFromClipboard(HWND owner, std::vector<ChatClipSegment>& out);

} // namespace balloonchatwnd
