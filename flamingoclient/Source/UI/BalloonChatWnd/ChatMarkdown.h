/**
 *  ChatMarkdown.h
 *
 *  聊天窗一侧的 Markdown 辅助函数（docs/Markdown消息方案.md 第 6 节、第 11 节）：把发送正文转成交给
 *  FlamingoMarkdown.dll 的源文本、给 Markdown 编辑器 / 预览窗口 / 导出生成排版样式、生成会话列表等处用的摘要。
 *  真正的解析、排版与绘制都在 DLL 里，DLL 不可用时这里的摘要函数原样返回正文，调用方无需另做判断。
 *
 *  典型用法（卡片取标题时）：
 *      const std::string source = balloonchatwnd::MarkdownSourceFromBody(m_msg.text);
 *      m_markdown.Parse(source, balloonchatwnd::MarkdownImageLinkPrefix());
 *      m_markdown.GetCardText(title, summary);
 *
 *  本文件遵循 balloonui 头文件约定：包含它之前调用方须已引入 ATL/WTL（用到 CString）。
 *
 *  balloonwj@qq.com   2026-10-06
 */
#pragma once

#include <map>
#include <string>

#include "../../../FlamingoMarkdown/FlamingoMarkdownApi.h"
#include "ChatMarkdownContent.h"   // IsMarkdownContentJson / MarkdownDigestFromUtf8Body（不依赖 ATL 的部分）
#include "../../net/MessageProtocolClient.h"   // net::MessageNotify / digestForNotify

namespace balloonwjui { class DuiControl; }

namespace balloonchatwnd {

/**
 *  把发送正文（content.text，含 /f["id"] 表情标记与转义斜杠）转成交给 DLL 的 Markdown 源文本：
 *  表情换成「[表情]」（第一版不在 Markdown 里画表情），转义斜杠还原，其余字符原样保留。
 *  @param body 发送正文
 *  @return UTF-8 源文本
 */
std::string MarkdownSourceFromBody(const CString& body);

/**
 *  图片语法 ![说明](地址) 显示成链接时加在说明前面的文字（已按界面语言翻译，如「[图片] 」）。
 */
std::wstring MarkdownImageLinkPrefix();

/**
 *  白底上的 Markdown 正文排版样式：Markdown 编辑器右侧预览、预览窗口、导出 .png / .pdf 都用它。
 *  字体名取 i18n::UiFontName()；设了 md_layout_fill_width，文档撑满排版宽度。
 *  @param fontPx    正文字号（像素；导出 PDF 时调用方已按打印机 DPI 换算）
 *  @param textColor 正文颜色（COLORREF）
 *  @return 填好 m_cbSize 的样式；其中的字体名指针在进程内一直有效
 */
flamingomd::MdStyle MakePreviewMarkdownStyle(int fontPx, COLORREF textColor);

/**
 *  Markdown 消息的摘要：DLL 可用时为「[Markdown] 标题」，标题与卡片相同（docs/Markdown消息方案.md 第 11.3 节）；
 *  DLL 不可用时与 balloonchat::BodyToDigest 相同（只换表情、还原转义斜杠）。会话列表、通知、引用预览都用它。
 *  @param body 发送正文
 *  @return 摘要（不截断，调用方按需截断）
 */
CString MarkdownDigestFromBody(const CString& body);

/**
 *  按消息卡片的规则从 Markdown 源文本取标题与摘要（FlamingoMarkdown.dll 的 m_getCardText）：标题取第一个标题，
 *  没有标题时取第一行文字；摘要是其余文字压成一行，不含标题。Markdown 编辑器的选发送对象窗口用它，与卡片一致。
 *  @param sourceUtf8 UTF-8 源文本
 *  @param title      输出标题；失败时置空
 *  @param summary    输出摘要；失败时置空
 *  @return DLL 可用、解析成功且 DLL 支持卡片接口时返回 true；否则返回 false，调用方自行退回到其它取法
 */
bool MarkdownCardTextFromSource(const std::string& sourceUtf8, std::wstring& title, std::wstring& summary);

//MarkdownDigestFromUtf8Body（MarkdownDigestFromBody 的 UTF-8 版）声明在 ChatMarkdownContent.h，本文件已包含它

/**
 *  一条消息的摘要（UTF-8）：Markdown 文字消息按 MarkdownDigestFromUtf8Body 去掉标记，其余消息与
 *  net::MessageProtocolClient::digestForNotify 相同。会话列表、字幕、合并卡片预览行用它代替 digestForNotify。
 *  @param notify 消息
 *  @return 摘要（UTF-8）
 */
std::string DigestForNotify(const net::MessageNotify& notify);

/**
 *  由聊天字体设置得出 Markdown 正文的字号与颜色：设过字体时取它的字号与颜色，否则取气泡默认（14 像素、#2A2A2A）。
 *  输入区预览用它与输入框保持一致。
 *  @param font   聊天字体设置（颜色为 0xRRGGBB）
 *  @param fontPx [出参] 字号（像素）
 *  @param color  [出参] 颜色（COLORREF）
 */
void MarkdownTextStyleFromFont(const net::TextFont& font, int& fontPx, COLORREF& color);

/**
 *  按 FlamingoMarkdown.dll 是否可用设置聊天窗工具栏「Markdown 编辑器」按钮的显隐，并对它的父容器重排
 *  （SetVisible 不会重排，UI 约定第 5 条）。单聊窗与群聊窗在 InitUi 时调用（docs/Markdown消息方案.md 第 11.3 节）。
 *  @param root     客户区根控件，借用；为 NULL 时什么也不做
 *  @param buttonId 按钮的控件编号（单聊 kIdTbMarkdown，群聊 kGIdTbMarkdown）
 *  @return 按钮显示时返回 true；找不到按钮或 DLL 不可用时返回 false
 */
bool ApplyMarkdownEditorButton(balloonwjui::DuiControl* root, UINT buttonId);

/**
 *  记录聊天窗在首屏历史渲染之前发出的 Markdown 文档，判断何时该重新拉一次最新一页。单聊窗、群聊窗各持有一个。
 *
 *  背景（2026-10-06 实机核对，据实证见测试客户端日志）：从 Markdown 编辑器发送时，主面板先打开目标会话的聊天窗、
 *  紧接着发送。聊天窗打开时请求的首屏历史早于这条消息，首屏渲染时清空列表、按服务端快照重建，刚插入的卡片随之消失。
 *  做法：首屏渲染之前发出的文档记下 clientmsgid；首屏已渲染、这些消息都已收到发送确认且至少一条成功时，
 *  聊天窗重新拉一次最新一页 —— 服务端此时已经入库，新快照里有这些消息。只在界面线程上使用。
 *
 *  典型用法（聊天窗）：
 *      发送后：            m_mdEarlySends.OnSent(clientMsgId);
 *      收到确认：          m_mdEarlySends.OnAck(clientMsgId, ok);  然后检查 TakeReloadNeeded()
 *      首屏渲染完：        m_mdEarlySends.OnFirstPageRendered();  然后检查 TakeReloadNeeded()
 *      重新请求最新一页：  m_mdEarlySends.OnReloadRequested();
 */
class MarkdownEarlySendTracker
{
public:
    MarkdownEarlySendTracker();

    /**
     *  聊天窗发出一条 Markdown 文档后调用。首屏已渲染时这条会正常留在列表里，不记录。
     *  @param clientMsgId 这条消息的 clientmsgid；为空时不记录
     */
    void OnSent(const std::string& clientMsgId);

    /**
     *  收到一条消息的发送确认后调用（成功或失败）。失败的消息服务端没有入库，直接删去记录。
     *  @param clientMsgId 确认里的 clientmsgid
     *  @param ok          发送是否成功
     *  @return 这条是本对象记录过的消息时返回 true
     */
    bool OnAck(const std::string& clientMsgId, bool ok);

    // 首屏历史渲染完成后调用
    void OnFirstPageRendered();

    // 聊天窗重新请求最新一页（之后到达的应答会再按首屏处理）时调用：回到「首屏未渲染」
    void OnReloadRequested();

    /**
     *  现在是否该重新拉一次最新一页：首屏已渲染、记录里没有仍在等确认的、且至少一条已确认成功。
     *  @return 返回 true 时同时清空记录，调用方随即重新请求最新一页
     */
    bool TakeReloadNeeded();

    // 记录里还有多少条（等确认的与已确认成功的合计），供日志与单测使用
    size_t PendingCount() const;

private:
    bool m_firstPageRendered;                 // 首屏历史是否已渲染；初始为 false，OnReloadRequested 时恢复为 false
    std::map<std::string, bool> m_sends;      // 首屏渲染前发出的文档：clientmsgid → 是否已确认成功
};

} // namespace balloonchatwnd
