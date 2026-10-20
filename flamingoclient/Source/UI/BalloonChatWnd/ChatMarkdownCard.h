/**
 *  Markdown 消息卡片的共用部分（docs/Markdown消息方案.md 第 11.3 节与第 12 节）：卡片宽度、标题与摘要的取法、高度计算
 *  与内容绘制。聊天窗的气泡（ChatBubble 的 Markdown 卡片分支）与消息记录窗、聊天记录管理器、聊天记录查看窗里的独立
 *  卡片控件（ChatMarkdownCardControl）都调用这里，外观与尺寸只有一份。
 *
 *  版式自上而下：标题一行（粗体，过长以省略号收尾）、最多三行摘要（灰，自动折行，最后一行超出时以省略号收尾）、
 *  一条分隔线、底部「M↓ Markdown 文档」（图标加小号灰字）。各尺寸与合并转发卡片相同（ChatBubble.cpp 的 kMerged*）。
 *  底色不在这里画：聊天窗里底色是气泡，独立控件自己画。
 *
 *  典型用法：
 *      std::wstring title, summary;
 *      GetMarkdownCardText(doc, title, summary);
 *      const int summaryH = MeasureMarkdownCardSummary(hdc, summary, kMarkdownCardWidth);
 *      const int height = MarkdownCardHeight(summaryH);
 *      ...
 *      PaintMarkdownCardContent(hdc, rcCard, title, summary, summaryH);
 *
 *  包含本头文件之前，调用方须已引入 ATL（CString）。
 *
 *  balloonwj@qq.com   2026-10-07
 */
#pragma once

#include <windows.h>
#include <string>

#include "ChatMarkdownContent.h"   // IsMarkdownContentJson（判断本地库 content 列是不是 Markdown 消息）

namespace markdown {
class CMarkdownDocument;
} // namespace markdown

namespace balloonchatwnd {

//卡片宽度（像素），与其他聊天卡片相同（docs/聊天卡片美化方案.md 3.2，2026-10-14 起由 280 改为 300）；所在位置放不下时由调用方收窄
const int kMarkdownCardWidth = 300;

/**
 *  独立卡片控件（ChatMarkdownCardControl）显示与打开预览所需的全部内容。消息记录窗与聊天记录查看窗建行时由
 *  MakeMarkdownCardInfo 填好，放在行数据里。
 */
struct MarkdownCardInfo
{
    std::wstring m_title;         //卡片标题（GetMarkdownCardText 的规则）
    std::wstring m_summary;       //卡片摘要，可为空
    std::string  m_sourceUtf8;    //Markdown 源文本（UTF-8，正文转义已还原），打开预览窗口时带上
    std::wstring m_sender;        //发送者的显示名，预览窗口正文上方的「发送者 · 时间」用
    bool         m_fromMe;        //是否本人发的：宿主据此把发送者显示为「我」
    long long    m_timestampMs;   //消息时间（Unix 毫秒）
    std::string  m_key;           //消息标识（服务器 msgid 的十进制串）：同一条消息再次点开时激活已有的预览窗口；可为空

    MarkdownCardInfo()
        : m_fromMe(false), m_timestampMs(0)
    {
    }
};

/**
 *  由消息正文生成独立卡片的内容：正文按 MarkdownSourceFromBody 转成源文本并解析，标题与摘要按 GetMarkdownCardText
 *  取，其余按传入值填写。消息记录窗与聊天记录查看窗建行时用它决定是否画成卡片。
 *  @param body        消息正文（与 ChatMessage::text 相同的格式）
 *  @param sender      发送者的显示名
 *  @param fromMe      是否本人发的
 *  @param timestampMs 消息时间（Unix 毫秒）
 *  @param msgid       服务器消息 ID；不大于 0 时消息标识留空
 *  @param out         [出参] 卡片内容
 *  @return FlamingoMarkdown.dll 可用且解析成功时返回 true；否则返回 false，调用方按普通文字显示
 */
bool MakeMarkdownCardInfo(const CString& body, const std::wstring& sender, bool fromMe, long long timestampMs,
                          long long msgid, MarkdownCardInfo& out);

/**
 *  取卡片的标题与摘要。DLL 有卡片接口（第二版）时按卡片规则取：标题为第一个标题块，没有标题时为第一行文字；摘要为
 *  其余文字压成的一段。旧版 DLL 没有这个接口时，取渲染后文字的第一行作标题、其余作摘要（换行与制表符换成空格）。
 *  标题为空时用「Markdown 文档」。
 *  @param doc     已解析的文档
 *  @param title   [出参] 标题
 *  @param summary [出参] 摘要，可能为空
 *  @return 文档已解析时返回 true；未解析时返回 false，两个出参清空
 */
bool GetMarkdownCardText(const markdown::CMarkdownDocument& doc, std::wstring& title, std::wstring& summary);

/**
 *  摘要按卡片内宽自动折行后的高度，最多三行。
 *  @param hdc       用于测量的设备上下文
 *  @param summary   摘要；为空时返回 0
 *  @param cardWidth 卡片宽度（像素），内宽为它减去两侧内边距
 *  @return 高度（像素）
 */
int MeasureMarkdownCardSummary(HDC hdc, const std::wstring& summary, int cardWidth);

/**
 *  卡片的总高。
 *  @param summaryH 摘要折行后的高度（MeasureMarkdownCardSummary 的结果）；为 0 时只有标题与底部一行
 *  @return 高度（像素）
 */
int MarkdownCardHeight(int summaryH);

/**
 *  画整张卡片（docs/聊天卡片美化方案.md 5.5）：白色底板、图标块与标题、最多三行摘要、底部一行「Markdown 文档」（没有按钮，
 *  点整张卡片打开预览）。2026-10-14 起卡片自己画底板，调用方不再画底色。
 *  @param hdc      目标设备上下文
 *  @param rcCard   卡片矩形（宽为卡片宽度，高为 MarkdownCardHeight 的结果）
 *  @param title    标题
 *  @param summary  摘要，可为空
 *  @param summaryH 摘要折行后的高度，须与测量时相同
 */
void PaintMarkdownCardContent(HDC hdc, const RECT& rcCard, const std::wstring& title, const std::wstring& summary,
                              int summaryH);

} // namespace balloonchatwnd
