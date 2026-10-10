/**
 *  ChatCodeMessage.h
 *
 *  聊天「代码片段」消息与界面之间的纯逻辑（docs/代码片段方案.md §5.2、§5.5、§5.6）：
 *    · 按协议的文本格式与语言标识填写 ChatMessage 的代码片段字段（收消息、拉历史、读本地库共用）；
 *    · 另存为：默认文件名与保存对话框（气泡右键菜单与代码查看窗共用）。
 *
 *  除另存为对话框外不碰控件与网络，FlamingoUnit 可以直接断言（Tests/CodeMessageTests.cpp，标签 [codemsg]）。
 *
 *  典型用法：
 *      balloonchatwnd::ChatMessage msg = MakeTextMessage(false, Utf8ToCString(notify.m_text));
 *      balloonchatwnd::ApplyCodeFields(msg, notify.m_textFormat, notify.m_codeLang);
 *
 *  本文件遵循 balloonui 头文件约定：包含它之前调用方须已引入 ATL/WTL（ChatMockData.h 用到 CString）。
 *
 *  balloonwj@qq.com   2026-10-06
 */
#pragma once

#include <string>

#include "ChatMockData.h"   // ChatMessage
#include "../../Common/Code/CodeNotes.h"   // codesnippet::CodeNote
#include "../../net/MessageProtocolClient.h"   // net::ReplyQuote

namespace balloonchatwnd {

/**
 *  解析输入框里的 ``` 写法：整条消息以一行 ``` 开头（后面可以紧跟语言名，如 ```python）、以单独一行 ``` 结尾时，
 *  取出中间的代码。前后的空白行忽略；中间再出现以 ``` 开头的行、没有闭合、或中间没有任何内容时都不算。
 *  @param text 输入框里的整条文字（换行可以是 CRLF、CR 或 LF）
 *  @param code [出参] 中间的代码（换行为 LF），只在返回 true 时有效
 *  @param lang [出参] 语言：写了语言名时按名字（不认识为纯文本），没写时自动识别；只在返回 true 时有效
 *  @return 是一段完整的 ``` 代码块时返回 true
 */
bool ParseCodeFence(const std::wstring& text, std::wstring& code, int& lang);

/**
 *  发送时判断整条消息是否按 ``` 写法转成代码片段：Markdown 模式下 ``` 属于 Markdown 正文，不转换；否则同 ParseCodeFence。
 *  @param text         输入框里的整条文字（只有一段文字、没有图片时才调用）
 *  @param markdownMode 输入区当前是否处于 Markdown 模式
 *  @param code         [出参] 中间的代码（换行为 LF），只在返回 true 时有效
 *  @param lang         [出参] 语言，取值见 codesnippet::CodeLanguage，只在返回 true 时有效
 *  @return 应按代码片段发送时返回 true
 */
bool TryCodeFenceSend(const std::wstring& text, bool markdownMode, std::wstring& code, int& lang);

/**
 *  粘贴识别：往输入框里粘贴文字之后，是否在输入框上方显示「以代码片段发送」提示条。
 *  只在粘贴之前输入框是空的、不在 Markdown 模式、且粘贴后的内容像代码（codesnippet::LooksLikeCode）时显示。
 *  @param inputWasEmpty 粘贴之前输入框是否为空（只有空白字符也算空）
 *  @param markdownMode  输入区当前是否处于 Markdown 模式
 *  @param inputText     粘贴之后输入框里的文字（换行可以是 CRLF、CR 或 LF）
 *  @return 应显示提示条时返回 true
 */
bool ShouldOfferCodePaste(bool inputWasEmpty, bool markdownMode, const std::wstring& inputText);

//输入框的粘贴识别通知（docs/代码片段方案.md 第 3.1 节）：ChatInputRichEdit::SetPasteTextMsg 用的消息号，投给宿主聊天窗，
//单聊、群聊共用。wParam 为 1 表示刚往空输入框里粘贴了文字，为 0 表示输入框内容有变化；lParam 未用。
//窗口私有消息（WM_APP + 0x89），已登记在开发约定文档 UI 约定第 6 条
const UINT kMsgChatCodePaste = WM_APP + 0x89;

/**
 *  代码片段的摘要：「[代码] 」加第一行非空内容（去掉首尾空白，过长时截断并加省略号）；代码全是空白时只有「[代码]」。
 *  追加批注的消息（批注里有标为「新」的，docs/代码片段方案.md 第 11.3 节）改写成「[批注] 」加按行号排在最前的那条新批注的
 *  说明（换行与 Tab 换成空格，去掉首尾空白，过长时同样截断）。会话列表、通知、引用块共用。
 *  @param code  代码原文
 *  @param notes 批注；为空或没有「新」批注时按代码写摘要
 */
std::wstring CodeDigestFromCode(const std::wstring& code,
                                const std::vector<codesnippet::CodeNote>& notes = std::vector<codesnippet::CodeNote>());

/**
 *  同 CodeDigestFromCode，输入输出都是 UTF-8，供 DigestForNotify（会话列表、字幕、合并卡片预览）使用。
 *  @param utf8Code 代码原文（UTF-8）
 *  @param notes    协议里的批注（content.notes）；为空或没有「新」批注时按代码写摘要
 *  @return 摘要（UTF-8）
 */
std::string CodeDigestFromUtf8(const std::string& utf8Code,
                               const std::vector<net::CodeNoteData>& notes = std::vector<net::CodeNoteData>());

/**
 *  从一条消息的 content JSON（本地库的 content 列）读出代码片段的语言：文字消息且 format 为 "code" 时是代码片段。
 *  消息记录窗据此把这一行画成代码卡片。
 *  @param msgtype     消息类型，取值见 ::message_type_*；不是文字消息时直接返回 false
 *  @param contentJson content JSON（UTF-8）；为空或解析失败时返回 false
 *  @param lang        [出参] 语言，取值见 codesnippet::CodeLanguage（content.lang 缺省或不认识时为纯文本），只在返回 true 时有效
 *  @param notes       [出参，可以为 NULL] 批注，已按代码行数整理，作者缺省的补成 senderId
 *  @param senderId    这条消息的发送者 userid；0 表示不知道，此时作者缺省的批注作者为 0
 *  @return 是代码片段时返回 true
 */
bool CodeLangFromContentJson(int msgtype, const std::string& contentJson, int& lang,
                             std::vector<codesnippet::CodeNote>* notes = NULL, long long senderId = 0);

/**
 *  把协议里的批注（UTF-8）换成界面用的批注，并按代码的行数整理（越界、相交、超过条数与字数上限的去掉或截断）。
 *  @param data      content.notes 读出的批注
 *  @param lineCount 代码总行数（规范化之后）；不大于 0 时不检查是否越过末行
 *  @param senderId  这条消息的发送者 userid：协议里没写作者（userid 缺省）的批注，作者补成它；为 0 时保持 0
 *  @return 整理后的批注，按起始行升序
 */
std::vector<codesnippet::CodeNote> CodeNotesFromData(const std::vector<net::CodeNoteData>& data, int lineCount,
                                                     long long senderId = 0);

/**
 *  把界面用的批注换成协议里的批注（说明转成 UTF-8）。作者为 0 或等于 senderId 的不写 userid（缺省即为发送者），
 *  其余写出作者；「新」照原样带上。
 *  @param notes    批注
 *  @param senderId 这条消息（将要发出的）的发送者 userid；为 0 时只有作者为 0 的不写 userid
 */
std::vector<net::CodeNoteData> CodeNotesToData(const std::vector<codesnippet::CodeNote>& notes, long long senderId = 0);

/**
 *  代码规范化之后有几行（与卡片、代码查看窗的行号一致）；空代码为 0。
 */
int CodeLineCount(const std::wstring& code);

/**
 *  代码片段发出去是否会超过单条消息的体积上限（与服务端同一口径，见 docs/协议设计-v2.md §10.3）。
 *  @param codeUtf8 代码原文（UTF-8）
 *  @param langId   语言标识（如 "cpp"），计入 content.lang
 *  @param quote    要带的引用回复快照；m_valid 为 true 时计入
 *  @return 超过上限时返回 true
 */
bool IsCodeTooLarge(const std::string& codeUtf8, const std::string& langId, const net::ReplyQuote& quote,
                    const std::vector<net::CodeNoteData>& notes = std::vector<net::CodeNoteData>());

/**
 *  按协议的文本格式与语言标识填写消息的代码片段字段。
 *  @param msg        [出参] 要填写的消息；只改 isCode 与 codeLang，其余字段不动
 *  @param textFormat 协议里的文本格式，取值见 net::text_format；为代码片段时 isCode 为真
 *  @param codeLang   协议里的语言标识（content.lang，可以是别名，不区分大小写）；为空或不认识时按纯文本
 */
void ApplyCodeFields(ChatMessage& msg, int textFormat, const std::string& codeLang,
                     const std::vector<net::CodeNoteData>& notes = std::vector<net::CodeNoteData>(),
                     long long senderId = 0);

/**
 *  这条消息能不能追加批注（docs/代码片段方案.md 第 11.3 节）：是代码片段、且已有服务器 msgid（发送中、发送失败的不能）。
 *  右键菜单「添加批注…」与代码查看窗的「添加批注」按钮据此决定出不出现。
 */
bool CanAnnotateCodeMessage(const ChatMessage& msg);

/**
 *  发送失败的追加批注消息「重新编辑」、而原消息已不在消息列表里时，用来代替原消息批注的那一组：这条消息里没有标「新」的
 *  批注（即原消息上已有的批注）。本人改过的那几条在原消息里的样子无从得知，会作为「新」批注出现。
 *  @param notes 发送失败的那条消息上的批注
 *  @return 没有标「新」的批注，顺序不变
 */
std::vector<codesnippet::CodeNote> BaselineNotesForReedit(const std::vector<codesnippet::CodeNote>& notes);

/**
 *  另存为对话框的默认文件名，如「代码片段.cpp」。
 *  @param lang 语言，取值见 codesnippet::CodeLanguage
 */
std::wstring CodeSaveFileName(int lang);

/**
 *  弹出「另存为」对话框，把代码按 UTF-8（不带 BOM）、CRLF 换行写进用户选的文件；Tab 原样保留。
 *  @param owner 对话框的父窗口
 *  @param code  规范化后的代码（换行为 LF）
 *  @param lang  语言，决定默认扩展名与过滤串，取值见 codesnippet::CodeLanguage
 *  @return 写入成功返回 true；用户取消或写入失败返回 false（失败时记日志）
 */
bool SaveCodeWithDialog(HWND owner, const std::wstring& code, int lang);

} // namespace balloonchatwnd
