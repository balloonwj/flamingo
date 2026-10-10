/**
 *  ChatPasteImage.h
 *
 *  聊天输入框粘贴剪贴板里的图片（bugs.md BUG-106，docs/截图独立模块方案.md 第 7 节）。
 *
 *  输入框按 Ctrl+V 时按键直接交给 RichEdit 引擎；剪贴板里只有位图时，引擎把它作为原生图片插入，发送时读出来只是一个
 *  空格，消息发不出去。改为：剪贴板里没有文字、只有图片时，把图片存成临时 PNG，交给聊天窗按截图「发送到对话框」的
 *  同一路径插入输入框。本文件提供这条路上的三个函数：
 *    · DecideChatPaste：按剪贴板里有哪些内容决定怎么粘贴（纯函数，用例 PIM1）；
 *    · SaveDibAsPng：把 CF_DIB 数据存成 PNG（不碰剪贴板，用例 PIM2）；
 *    · ReadClipboardImageToPng：从剪贴板取 CF_DIB，存到临时目录。
 *
 *  典型用法（输入框按下 Ctrl+V 时）：
 *      ChatPasteKind kind = DecideChatPaste(IsChatContentOnClipboard(), ClipboardHasText(), ClipboardHasImage());
 *      if (kind == kChatPasteImage && ReadClipboardImageToPng(owner, path)) 交给宿主插图;
 *
 *  balloonwj@qq.com   2026-10-08
 */
#pragma once

#include <windows.h>
#include <string>

namespace balloonchatwnd {

// 一次粘贴按什么处理
enum ChatPasteKind
{
    kChatPasteDefault = 0,   // 交给原来的粘贴：剪贴板里是文字（照「默认按纯文本粘贴」的设置）或什么都没有
    kChatPasteContent,       // 剪贴板里是 Flamingo 自己的聊天内容格式：按图文片段插入（TryPasteChatContent）
    kChatPasteImage,         // 剪贴板里没有文字、只有图片：存成临时 PNG，交给聊天窗按「发送到对话框」插入
};

/**
 *  按剪贴板里有哪些内容决定这次粘贴怎么处理。优先级：Flamingo 聊天内容 > 文字 > 图片 > 原来的粘贴。
 *  有文字时一律按文字处理（例如从 Word 复制的内容同时带一张位图），只有「没有文字、只有图片」才按图片处理。
 *  @param hasChatContent 剪贴板里有 Flamingo 自己的聊天内容格式
 *  @param hasText        剪贴板里有文字（CF_UNICODETEXT 或 CF_TEXT）
 *  @param hasImage       剪贴板里有位图（CF_DIB、CF_DIBV5 或 CF_BITMAP）
 *  @return 处理方式
 */
ChatPasteKind DecideChatPaste(bool hasChatContent, bool hasText, bool hasImage);

/**
 *  剪贴板里有没有文字（CF_UNICODETEXT 或 CF_TEXT）。不需要先打开剪贴板。
 */
bool ClipboardHasText();

/**
 *  剪贴板里有没有位图（CF_DIB、CF_DIBV5 或 CF_BITMAP）。不需要先打开剪贴板。
 */
bool ClipboardHasImage();

/**
 *  把 CF_DIB 数据（BITMAPINFOHEADER，可能带调色板或位掩码，紧跟像素）存成 PNG。
 *  @param dib   CF_DIB 数据的起始地址，不可为 NULL
 *  @param bytes 数据的总字节数
 *  @param path  要写的 PNG 文件完整路径；已存在时覆盖
 *  @return 成功返回 true；数据不完整（字节数不够、宽高为 0、位数不支持）或写文件失败时返回 false，且不留下文件
 */
bool SaveDibAsPng(const void* dib, size_t bytes, const std::wstring& path);

/**
 *  从剪贴板取图片（CF_DIB，系统会从 CF_BITMAP 转出），存成临时目录下的 flamingo_paste_<序号>.png。
 *  @param owner   打开剪贴板时的归属窗口，可为 NULL
 *  @param outPath [出参] 存好的 PNG 完整路径；失败时为空串
 *  @return 成功返回 true；剪贴板打不开、没有图片或存文件失败时返回 false（已记日志）
 */
bool ReadClipboardImageToPng(HWND owner, std::wstring& outPath);

} // namespace balloonchatwnd
