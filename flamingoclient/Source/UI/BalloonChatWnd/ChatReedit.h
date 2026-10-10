/**
 *  ChatReedit.h
 *
 *  聊天气泡右键「重新编辑」的逻辑（docs/敏感词方案.md 第 8.4 节）：发送失败的文字消息，把原文放回输入框，
 *  让用户改了再发。
 *
 *    · 只对<u>自己发出、发送失败</u>的文字消息（文字与大表情）给出「重新编辑」，见 CanReeditMessage；
 *    · 原文按「文字 / 表情」拆成片段，与「复制」后粘贴回输入框走同一个插入口，表情还原成表情图片；
 *    · 群聊里正文中的 @ 片段按气泡上记录的 ats 重新登记为待发的 @ 候选，发送时照常按最终文本重扫；
 *    · 引用<u>不恢复</u>：引用需要被引用消息的 msgid 与发送者，失败气泡上只有渲染用的快照；
 *    · 插入位置是输入框当前的光标处：输入框为空时等于直接填入，不为空时已有内容不动。
 *
 *  典型用法（宿主聊天窗收到气泡投来的 WM_CHAT_REEDIT_REQUEST 时）：
 *      ChatReeditData* data = reinterpret_cast<ChatReeditData*>(lParam);
 *      ApplyReeditToInput(*data, input, &m_pendingMentions);   //单聊传 NULL
 *      delete data;
 *
 *  balloonwj@qq.com   2026-10-02
 */
#pragma once

#include <string>
#include <vector>

#include "ChatClipboardContent.h"   // ChatClipSegment
#include "ChatMockData.h"           // ChatMessage
#include "GroupMentionModel.h"      // MentionCandidate / RenderSpan

namespace balloonchatwnd {

class ChatInputRichEdit;

/**
 *  「重新编辑」要放回输入框的内容（WM_CHAT_REEDIT_REQUEST 的 lParam，堆对象，宿主处理后 delete）。
 */
struct ChatReeditData
{
    std::vector<ChatClipSegment>  m_segments;   // 正文按「文字 / 表情」拆成的片段，按原文顺序
    std::vector<MentionCandidate> m_mentions;   // 正文里的 @（群聊），按出现顺序；单聊为空
    bool                          m_isMarkdown = false;   // 原消息是不是 Markdown：是的话宿主改为打开 Markdown 编辑器，不放回输入框
                                                          // （docs/Markdown消息方案.md 第 11.3 节）
    std::string                   m_markdownSource;       // Markdown 源文本（UTF-8，已把正文里的转义还原）；m_isMarkdown 为 false 时为空
    bool                          m_isCode = false;       // 原消息是不是代码片段：是的话宿主改为打开发送代码窗口，不放回输入框
                                                          // （docs/代码片段方案.md 第 3.1 节）
    int                           m_codeLang = 0;         // 代码片段的语言，取值见 codesnippet::CodeLanguage；m_isCode 为 false 时无意义
    std::wstring                  m_code;                 // 代码片段的原文；m_isCode 为 false 时为空
    std::vector<codesnippet::CodeNote> m_codeNotes;       // 代码片段的批注，行号指 m_code 的行（docs/代码片段方案.md 第 10.5 节）；
                                                          // m_isCode 为 false 或没有批注时为空
    long long                     m_codeQuoteMsgid = 0;   // 代码片段引用的那条消息的 msgid；追加批注的消息（第 11.3 节）引用的
                                                          // 就是原消息，宿主据此回到批注模式。没有引用时为 0
    long long                     m_codeQuoteFrom = 0;    // 被引用那条消息的发送者 userid；没有引用时为 0
};

/**
 *  判断一条消息的右键菜单里该不该出现「重新编辑」：自己发出、发送失败、是文字或大表情。
 *    msg：消息。
 *  @return 该出现返回 true。本函数无副作用。
 */
bool CanReeditMessage(const ChatMessage& msg);

/**
 *  按气泡上记录的 @ 片段，从正文里取出各片段的显示名，生成待发的 @ 候选。
 *    text：消息正文（发送正文格式，与 ats 的偏移同一口径）。
 *    ats： 渲染侧的 @ 片段（UTF-16 偏移，片段以 '@' 开头）；越界、长度不足 2 的片段跳过。
 *  @return 候选列表，按片段顺序；同一个人出现多次时只保留第一次。本函数无副作用。
 */
std::vector<MentionCandidate> MentionsFromRenderSpans(const CString& text, const std::vector<RenderSpan>& ats);

/**
 *  从一条发送失败的消息生成「重新编辑」要放回输入框的内容。
 *    msg：消息；不满足 CanReeditMessage 时返回空内容。
 *  @return 片段与 @ 候选。本函数无副作用。
 */
ChatReeditData BuildReeditData(const ChatMessage& msg);

/**
 *  按消息内容生成要放回输入框的内容，不检查是不是发送失败。BuildReeditData 与撤回后重新编辑
 *  （RecallEdit.h 的 BuildRecallDraft，docs/撤回后重新编辑方案.md）共用这一份生成规则。
 *    msg：文字或大表情消息；其他类型的消息生成的内容没有意义，由调用方事先判断。
 *  @return 片段、@ 候选、Markdown 源文本与代码片段原文。本函数无副作用。
 */
ChatReeditData BuildReeditPayload(const ChatMessage& msg);

/**
 *  把「重新编辑」的内容插到输入框的光标处，并把 @ 候选追加到宿主的待发候选里（已有的同名同人不重复追加）。
 *    data：要放回的内容。
 *    input：聊天输入框；为 NULL 时什么都不做。所有权归调用方。
 *    pendingMentions：[入出参，可为 NULL] 宿主的待发 @ 候选；单聊传 NULL。
 */
void ApplyReeditToInput(const ChatReeditData& data, ChatInputRichEdit* input,
                        std::vector<MentionCandidate>* pendingMentions);

} // namespace balloonchatwnd
