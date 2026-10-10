/**
 *  ChatMarkdown.cpp
 *
 *  聊天窗一侧的 Markdown 辅助函数的实现，说明见 ChatMarkdown.h。
 *
 *  balloonwj@qq.com   2026-10-06
 */
#include "stdafx.h"
#include "ChatMarkdown.h"
#include "ChatCodeMessage.h"                     // CodeDigestFromUtf8：代码片段的摘要

#include <vector>

#include "../BalloonChat/BalloonChatMsgFormat.h"   // ParseSendBody / BodyToDigest
#include "../../net/Protocol.h"                    // ::message_type_text
#include "../../Common/Util/EncodeUtil.h"          // UTF-8 与 UTF-16 互转
#include "../../Common/I18n/I18n.h"                // TR / UiFontName
#include "../../Feature/Markdown/MarkdownDocument.h"   // MarkdownToPlainTextUtf8 / IsMarkdownAvailable
#include "../../../../third-party/balloonui/balloonui/DuiControl.h"   // ApplyMarkdownEditorButton

namespace balloonchatwnd {

namespace {

//界面缩放百分比：聊天气泡的各项尺寸都是固定的逻辑像素，Markdown 正文与之保持一致
const int kChatScalePercent = 100;

//次要文字颜色：引用块里的文字、有序列表编号（与引用内容、合并卡片预览同一种灰）
const COLORREF kMutedColor = RGB(0x8A, 0x8A, 0x8A);

//链接颜色：与普通文字气泡里自动识别出的链接相同（ChatBubble.cpp 的 kLinkFg）
const COLORREF kLinkColor = RGB(0x2F, 0x6F, 0xD0);

//强调色：已勾选任务框、「复制」按钮悬停（主题橙）
const COLORREF kAccentColor = RGB(0xFF, 0x7A, 0x45);

//Markdown 编辑器、预览窗口与导出（白底）里的代码底色、线条色、表头底色、选区底色
//（2026-10-06 消息改为卡片显示后，气泡里不再渲染全文，原先按两种气泡底色配的两套颜色已删除）
const COLORREF kPreviewCodeBack = RGB(0xF2, 0xF1, 0xEE);
const COLORREF kPreviewRule = RGB(0xDD, 0xDB, 0xD6);
const COLORREF kPreviewTableHeader = RGB(0xF6, 0xF5, 0xF2);
const COLORREF kPreviewSelection = RGB(0xCC, 0xE0, 0xF8);

//聊天气泡的默认字号（像素）与正文颜色，与 ChatBubble.cpp 的 kBubbleTextPx、kBubbleText 一致
const int kDefaultBubbleFontPx = 14;
const COLORREF kDefaultBubbleTextColor = RGB(0x2A, 0x2A, 0x2A);

//0xRRGGBB 拆成三个分量用的移位与掩码
const int kRedShift = 16;
const int kGreenShift = 8;
const uint32_t kByteMask = 0xFF;

//把换行与 Tab 换成空格（摘要只占一行）
std::wstring FlattenWhitespace(const std::wstring& text)
{
    std::wstring out;
    out.reserve(text.size());
    for (size_t i = 0; i < text.size(); ++i)
    {
        const wchar_t ch = text[i];
        if (ch == L'\r')
            continue;
        out.push_back((ch == L'\n' || ch == L'\t') ? L' ' : ch);
    }
    return out;
}

//公共部分：字体名、字号、缩放与固定颜色
flamingomd::MdStyle BaseStyle(int fontPx, COLORREF textColor)
{
    flamingomd::MdStyle style;
    memset(&style, 0, sizeof(style));
    style.m_cbSize = sizeof(style);
    style.m_fontName = i18n::UiFontName();
    style.m_codeFontName = NULL;
    style.m_fontPx = fontPx;
    style.m_scalePercent = kChatScalePercent;
    style.m_textColor = textColor;
    style.m_mutedColor = kMutedColor;
    style.m_linkColor = kLinkColor;
    style.m_accentColor = kAccentColor;
    return style;
}

} // namespace

std::string MarkdownSourceFromBody(const CString& body)
{
    const std::vector<balloonchat::ChatSegment> segs = balloonchat::ParseSendBody(body);
    std::wstring source;
    for (size_t i = 0; i < segs.size(); ++i)
    {
        if (segs[i].m_bFace)
            source += TR(L"[表情]");
        else
            source += (LPCWSTR)segs[i].m_strText;
    }
    return EncodeUtil::UnicodeToUtf8(source);
}

std::wstring MarkdownImageLinkPrefix()
{
    return TR(L"[图片] ");
}

flamingomd::MdStyle MakePreviewMarkdownStyle(int fontPx, COLORREF textColor)
{
    flamingomd::MdStyle style = BaseStyle(fontPx, textColor);
    style.m_codeBackColor = kPreviewCodeBack;
    style.m_ruleColor = kPreviewRule;
    style.m_tableHeaderBackColor = kPreviewTableHeader;
    style.m_selectionBackColor = kPreviewSelection;
    //预览与导出的宽度是固定的，文档撑满整个宽度：标题下的细线、代码块底色铺满，不只画到最宽的那一行
    //（2026-10-06 实机核对后用户确定；1.3.0 之前的 DLL 不认这个选项，仍按内容收缩）
    style.m_layoutFlags = flamingomd::md_layout_fill_width;
    return style;
}

bool MarkdownCardTextFromSource(const std::string& sourceUtf8, std::wstring& title, std::wstring& summary)
{
    title.clear();
    summary.clear();
    if (!markdown::IsMarkdownAvailable())
        return false;
    markdown::CMarkdownDocument doc;
    if (!doc.Parse(sourceUtf8, MarkdownImageLinkPrefix()))
        return false;
    if (!doc.GetCardText(title, summary))
    {
        title.clear();
        summary.clear();
        return false;
    }
    return true;
}

CString MarkdownDigestFromBody(const CString& body)
{
    //DLL 不可用时消息按源文本显示，摘要也给源文本，两处一致
    if (!markdown::IsMarkdownAvailable())
        return balloonchat::BodyToDigest(body);
    //与卡片同一个标题：写成「[Markdown] 标题」（docs/Markdown消息方案.md 第 11.3 节）
    std::wstring title;
    markdown::CMarkdownDocument doc;
    if (doc.Parse(MarkdownSourceFromBody(body), MarkdownImageLinkPrefix()))
    {
        std::wstring summary;
        if (!doc.GetCardText(title, summary))
        {
            //旧版 DLL 没有卡片接口：取渲染后文字的第一行
            const std::wstring flat = doc.GetText(0, doc.GetTextLength());
            title = flat.substr(0, flat.find_first_of(L"\r\n"));
        }
    }
    title = FlattenWhitespace(title);
    if (title.empty())
        title = TR(L"Markdown 文档");
    return CString(TRF(L"[Markdown] {0}", title.c_str()).c_str());
}

std::string MarkdownDigestFromUtf8Body(const std::string& utf8Body)
{
    const std::wstring wide = EncodeUtil::Utf8ToUnicode(utf8Body);
    const CString digest = MarkdownDigestFromBody(CString(wide.c_str()));
    return EncodeUtil::UnicodeToUtf8(std::wstring((LPCWSTR)digest));
}

void MarkdownTextStyleFromFont(const net::TextFont& font, int& fontPx, COLORREF& color)
{
    if (font.m_valid)
    {
        fontPx = font.m_size;
        const uint32_t rgb = font.m_colorRgb;
        color = RGB((rgb >> kRedShift) & kByteMask, (rgb >> kGreenShift) & kByteMask, rgb & kByteMask);
        return;
    }
    fontPx = kDefaultBubbleFontPx;
    color = kDefaultBubbleTextColor;
}

std::string DigestForNotify(const net::MessageNotify& notify)
{
    //代码片段写成「[代码] 第一行」（docs/代码片段方案.md 第 3.4 节）；追加批注的写成「[批注] 说明」（第 11.3 节）
    if (notify.m_msgtype == ::message_type_text && notify.m_textFormat == net::text_format_code)
    {
        return CodeDigestFromUtf8(notify.m_text, notify.m_codeNotes);
    }
    if (notify.m_msgtype == ::message_type_text && notify.m_textFormat == net::text_format_markdown)
    {
        return MarkdownDigestFromUtf8Body(notify.m_text);
    }
    return net::MessageProtocolClient::digestForNotify(notify);
}

bool ApplyMarkdownEditorButton(balloonwjui::DuiControl* root, UINT buttonId)
{
    //编号 0 是「未设编号」：FindCtrlById(0) 会找到任意一个没设编号的控件，不能拿它当按钮
    if (root == NULL || buttonId == 0)
        return false;
    balloonwjui::DuiControl* button = root->FindCtrlById(buttonId);
    if (button == NULL)
        return false;
    const bool available = markdown::IsMarkdownAvailable();
    button->SetVisible(available);
    //SetVisible 不会重排：对按钮所在的工具栏按当前矩形强制重排，隐藏时不留空位
    balloonwjui::DuiControl* parent = button->GetParent();
    if (parent != NULL)
        parent->ForceLayout(parent->GetRect());
    return available;
}

MarkdownEarlySendTracker::MarkdownEarlySendTracker()
    : m_firstPageRendered(false)
{
}

void MarkdownEarlySendTracker::OnSent(const std::string& clientMsgId)
{
    if (m_firstPageRendered || clientMsgId.empty())
        return;
    m_sends[clientMsgId] = false;
}

bool MarkdownEarlySendTracker::OnAck(const std::string& clientMsgId, bool ok)
{
    std::map<std::string, bool>::iterator it = m_sends.find(clientMsgId);
    if (it == m_sends.end())
        return false;
    if (ok)
        it->second = true;
    else
        m_sends.erase(it);
    return true;
}

void MarkdownEarlySendTracker::OnFirstPageRendered()
{
    m_firstPageRendered = true;
}

void MarkdownEarlySendTracker::OnReloadRequested()
{
    m_firstPageRendered = false;
}

bool MarkdownEarlySendTracker::TakeReloadNeeded()
{
    if (!m_firstPageRendered || m_sends.empty())
        return false;
    for (std::map<std::string, bool>::const_iterator it = m_sends.begin(); it != m_sends.end(); ++it)
    {
        //还有在等确认的：等它确认后再拉，否则快照里可能还没有它
        if (!it->second)
            return false;
    }
    m_sends.clear();
    return true;
}

size_t MarkdownEarlySendTracker::PendingCount() const
{
    return m_sends.size();
}

} // namespace balloonchatwnd
