/**
 *  MarkdownEditorModel.h
 *
 *  Markdown 编辑器的纯逻辑部分（docs/Markdown消息方案.md 第 11.3 节），不依赖窗口，FlamingoUnit 直接测试：
 *    - .md 文件的读写编码：打开时识别 UTF-8（带不带 BOM 均可）、UTF-16LE（带 BOM），否则按系统 ANSI 代码页；
 *      保存为不带 BOM 的 UTF-8，换行写成 CRLF；
 *    - 编辑框文字与源文本互转：RichEdit 里段落之间是单个 \r，源文本（发送、解析、导出用）统一用 \n；
 *    - 源文本与聊天正文互转：正文里字面的「/」在紧跟表情标记时要写成「//」，统一交给 BuildSendBody 处理；
 *    - 窗口标题、底部发送对象的文字、发送前检查。
 *
 *  典型用法：
 *      std::string bytes = ...读文件...;
 *      int encoding = 0;
 *      editor->SetText(balloonmd::DecodeMarkdownFileBytes(bytes, &encoding).c_str());
 *      ...
 *      const std::string source = balloonmd::EditorTextToMarkdownUtf8((LPCWSTR)editor->GetText());
 *
 *  balloonwj@qq.com   2026-10-06
 */
#pragma once

#include <string>
#include <vector>

#include "MarkdownEditorManager.h"   // MarkdownSendTarget

namespace balloonmd {

//打开 .md 文件时识别出的编码
enum MarkdownFileEncoding
{
    md_file_utf8       = 0,   //没有 BOM、字节序列是合法的 UTF-8（纯 ASCII 也算这一种）
    md_file_utf8_bom   = 1,   //以 UTF-8 BOM（EF BB BF）开头
    md_file_utf16le    = 2,   //以 UTF-16LE BOM（FF FE）开头
    md_file_ansi       = 3,   //没有 BOM 且不是合法的 UTF-8：按系统 ANSI 代码页解码
};

//发送前检查的结果
enum MarkdownSendCheck
{
    md_send_ok        = 0,   //可以发送
    md_send_offline   = 1,   //未登录或连接不可用
    md_send_empty     = 2,   //文档为空或只有空白
    md_send_too_large = 3,   //超过单条消息的体积上限（MESSAGE_CONTENT_MAX_BYTES）
    md_send_no_target = 4,   //还没有选择发送对象
};

/**
 *  把 .md 文件的内容解码为编辑框文字（UTF-16）。BOM 被去掉；换行原样保留，交给编辑框处理。
 *  @param bytes    文件的全部字节
 *  @param encoding [出参，可为 NULL] 识别出的编码，取值见 enum MarkdownFileEncoding
 *  @return 解码后的文字
 */
std::wstring DecodeMarkdownFileBytes(const std::string& bytes, int* encoding);

/**
 *  把编辑框文字编码为要保存的文件内容：不带 BOM 的 UTF-8；\r、\n、\r\n 一律写成 \r\n。
 *  @param editorText 编辑框文字
 *  @return 文件字节
 */
std::string EncodeMarkdownForSave(const std::wstring& editorText);

/**
 *  编辑框文字转为源文本（UTF-8）：\r 与 \r\n 统一成 \n。发送、解析、导出都用源文本。
 *  @param editorText 编辑框文字
 *  @return UTF-8 源文本
 */
std::string EditorTextToMarkdownUtf8(const std::wstring& editorText);

/**
 *  源文本（UTF-8）转为编辑框文字：\r\n、\r、\n 一律写成 \r\n（RichEdit 按段落接收）。
 *  @param sourceUtf8 UTF-8 源文本
 *  @return 编辑框文字
 */
std::wstring MarkdownUtf8ToEditorText(const std::string& sourceUtf8);

/**
 *  源文本转为聊天正文（BuildSendBody 的口径，字面「/f["」写成「//f["」），即 MarkdownSourceFromBody 的逆运算。
 *  @param sourceUtf8 UTF-8 源文本
 *  @return UTF-8 正文，直接作为文字消息的 text 发出
 */
std::string MarkdownBodyFromSourceUtf8(const std::string& sourceUtf8);

/**
 *  没有 DLL 帮助时从源文本粗取一个标题：第一行非空文字，去掉行首的 # 与空白，最长 kMarkdownTitleMaxChars 个字符
 *  （超出时末尾加「…」）。用于窗口与选择窗里的文档名，正式的卡片标题由 DLL 给出。
 *  @param sourceUtf8 UTF-8 源文本
 *  @return 标题；文档没有文字时为空串
 */
std::wstring MarkdownTitleFromSource(const std::string& sourceUtf8);

//MarkdownTitleFromSource 取标题时的最大字符数（UTF-16 个数，不含末尾的「…」）
const size_t kMarkdownTitleMaxChars = 40;

/**
 *  窗口标题：「Markdown 编辑器 - <文件名或「未命名」>」，有未保存修改时文件名前加「*」。
 *  @param filePath 文档的完整路径；未保存过时为空
 *  @param dirty    是否有未保存的修改
 *  @return 标题文字（已按界面语言翻译）
 */
std::wstring MarkdownEditorCaption(const std::wstring& filePath, bool dirty);

/**
 *  底部发送对象一栏的文字。
 *  @param loggedIn 是否已登录
 *  @param targets  已选的发送对象
 *  @return 未登录时为「登录后可发送」；没有对象时为「未选择发送对象」；一个对象时为「发送给：张三」；
 *          多个时为「发送给：张三 等 3 个」（均已按界面语言翻译）
 */
std::wstring MarkdownSendTargetsText(bool loggedIn, const std::vector<MarkdownSendTarget>& targets);

/**
 *  发送前检查。顺序：未登录、空文档、超限、没有对象。
 *  @param loggedIn    是否已登录
 *  @param sourceUtf8  UTF-8 源文本
 *  @param targetCount 已选的发送对象个数
 *  @return 取值见 enum MarkdownSendCheck
 */
int CheckMarkdownSend(bool loggedIn, const std::string& sourceUtf8, size_t targetCount);

/**
 *  预览窗口正文上方的一行说明「发送者 · 时间」。
 *  @param sender      发送者显示名，可为空
 *  @param timestampMs 发送时间（毫秒，UTC 起点）；不大于 0 时不写时间
 *  @return 两者都有时为「张三 · 2026-10-06 10:20」（本地时间），只有一项时只写那一项，都没有时为空串
 */
std::wstring MarkdownPreviewMeta(const std::wstring& sender, long long timestampMs);

/**
 *  发送前检查不通过时给用户看的说明。
 *  @param check CheckMarkdownSend 的返回值
 *  @return 说明文字（已按界面语言翻译）；md_send_ok 时为空串
 */
std::wstring MarkdownSendCheckText(int check);

} // namespace balloonmd
