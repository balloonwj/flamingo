/**
 *  Markdown 消息卡片共用部分的实现，说明见 ChatMarkdownCard.h。2026-10-07 从 ChatBubble.cpp 抽出；2026-10-14 起按聊天卡片
 *  美化方案换成新版式（docs/聊天卡片美化方案.md 5.5）：白色底板、图标块、标题、摘要与底部一行，样式常量取自 ChatCardStyle.h。
 *
 *  balloonwj@qq.com   2026-10-07
 */
#include "stdafx.h"
#include "ChatMarkdownCard.h"

#include <objidl.h>
#include <gdiplus.h>

#include <memory>

#include "ChatCardModel.h"                             // ChatCardTypeStyleOf / ChatCardTypeLabel：图标块与底部类型标签
#include "ChatCardPaint.h"                             // 卡片底板、图标块与底部一行的绘制
#include "ChatCardStyle.h"                             // 卡片样式常量
#include "ChatMarkdown.h"                              // MarkdownSourceFromBody / MarkdownImageLinkPrefix / net::kTextFormatMarkdownValue
#include "ChatPaintUtil.h"                             // DrawGdiText
#include "../../net/Protocol.h"                        // ::message_type_text
#include "../../third-party/jsoncpp-1.9.0/json.h"      // 读本地库 content 列里的 format
#include "../../Common/I18n/I18n.h"                    // TR / UiFontName
#include "../../Feature/Markdown/MarkdownDocument.h"   // CMarkdownDocument / IsMarkdownAvailable

namespace balloonchatwnd {

using chatpaint::DrawGdiText;

namespace {

//本地库 content 列里表示文字格式的键，以及 Markdown 的取值（与 V2MsgQuery::BuildContentJson 写入的一致）
const char* const kContentFormatKey = "format";

const int kCardSummaryGap       = 8;    // 头部与摘要之间的间距
const int kCardSummaryPx        = 12;   // 摘要字号
const int kCardSummaryMaxLines  = 3;    // 摘要最多显示几行
// 头部高：图标块与单行标题取较高者，两者在其中垂直居中
const int kCardHeaderH = (cardstyle::kTileSize > cardstyle::kTitleLineH) ? cardstyle::kTileSize : cardstyle::kTitleLineH;

// 一条 Markdown 消息的样子，用来取图标块与底部类型标签的样式（Markdown 卡片的类型样式只看这两个字段）
ChatMessage MarkdownStyleMessage()
{
    ChatMessage m;
    m.kind = kMsgText;
    m.isMarkdown = true;
    return m;
}

//按界面字体量一段文字在宽度 width 内自动折行后的高度（像素），超过 maxLines 行时按 maxLines 行计。文字为空时返回 0
int MeasureWrappedTextHeight(HDC hdc, const std::wstring& text, int width, int pixelHeight, int maxLines)
{
    if (text.empty() || width <= 0 || pixelHeight <= 0)
        return 0;
    HFONT font = ::CreateFontW(-pixelHeight, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                               OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH,
                               i18n::UiFontName());
    HGDIOBJ old = ::SelectObject(hdc, font);
    TEXTMETRICW tm;
    ::GetTextMetricsW(hdc, &tm);
    RECT rc = { 0, 0, width, 0 };
    ::DrawTextW(hdc, text.c_str(), (int)text.size(), &rc,
                DT_CALCRECT | DT_WORDBREAK | DT_EDITCONTROL | DT_NOPREFIX | DT_LEFT);
    ::SelectObject(hdc, old);
    ::DeleteObject(font);
    const int lineH = (tm.tmHeight > 0) ? (int)tm.tmHeight : pixelHeight;
    int h = rc.bottom - rc.top;
    if (h > lineH * maxLines)
        h = lineH * maxLines;
    return h;
}

} // namespace

bool GetMarkdownCardText(const markdown::CMarkdownDocument& doc, std::wstring& title, std::wstring& summary)
{
    title.clear();
    summary.clear();
    if (!doc.IsParsed())
        return false;
    //标题优先取第一个标题块；旧版 DLL 没有这个接口时取渲染后文字的第一行作标题、其余作摘要
    if (!doc.GetCardText(title, summary))
    {
        const std::wstring flat = doc.GetText(0, doc.GetTextLength());
        const size_t brk = flat.find_first_of(L"\r\n");
        title = (brk == std::wstring::npos) ? flat : flat.substr(0, brk);
        summary = (brk == std::wstring::npos) ? std::wstring() : flat.substr(brk + 1);
        for (size_t i = 0; i < summary.size(); ++i)
        {
            if (summary[i] == L'\r' || summary[i] == L'\n' || summary[i] == L'\t')
                summary[i] = L' ';
        }
    }
    if (title.empty())
        title = TR(L"Markdown 文档");
    return true;
}

bool IsMarkdownContentJson(int msgtype, const std::string& contentJson)
{
    if (msgtype != ::message_type_text || contentJson.empty())
        return false;
    Json::CharReaderBuilder builder;
    std::unique_ptr<Json::CharReader> reader(builder.newCharReader());
    Json::Value root;
    std::string errs;
    if (!reader->parse(contentJson.data(), contentJson.data() + contentJson.size(), &root, &errs) || !root.isObject())
        return false;
    const Json::Value& format = root[kContentFormatKey];
    return format.isString() && format.asString() == net::kTextFormatMarkdownValue;
}

bool MakeMarkdownCardInfo(const CString& body, const std::wstring& sender, bool fromMe, long long timestampMs,
                          long long msgid, MarkdownCardInfo& out)
{
    out = MarkdownCardInfo();
    if (!markdown::IsMarkdownAvailable())
        return false;
    //源文本只转换一次：解析取卡片文字与打开预览窗口共用
    out.m_sourceUtf8 = MarkdownSourceFromBody(body);
    markdown::CMarkdownDocument doc;
    if (!doc.Parse(out.m_sourceUtf8, MarkdownImageLinkPrefix()) || !GetMarkdownCardText(doc, out.m_title, out.m_summary))
    {
        out = MarkdownCardInfo();
        return false;
    }
    out.m_sender = sender;
    out.m_fromMe = fromMe;
    out.m_timestampMs = timestampMs;
    out.m_key = (msgid > 0) ? std::to_string(msgid) : std::string();
    return true;
}

int MeasureMarkdownCardSummary(HDC hdc, const std::wstring& summary, int cardWidth)
{
    return MeasureWrappedTextHeight(hdc, summary, cardWidth - cardstyle::kCardPadH * 2, kCardSummaryPx, kCardSummaryMaxLines);
}

int MarkdownCardHeight(int summaryH)
{
    //头部、摘要（有的话）、下内边距，以及常驻的底部一行（左边是类型标签「Markdown 文档」）
    return cardstyle::kCardPadTop + kCardHeaderH + (summaryH > 0 ? kCardSummaryGap + summaryH : 0) + cardstyle::kCardPadBottom
         + cardstyle::kFooterDividerH + cardstyle::kFooterH;
}

void PaintMarkdownCardContent(HDC hdc, const RECT& rcCard, const std::wstring& title, const std::wstring& summary,
                              int summaryH)
{
    const ChatMessage styleMsg = MarkdownStyleMessage();
    const ChatCardTypeStyle style = ChatCardTypeStyleOf(styleMsg);
    const int left = rcCard.left + cardstyle::kCardPadH;
    const int right = rcCard.right - cardstyle::kCardPadH;
    const int headerTop = rcCard.top + cardstyle::kCardPadTop;
    Gdiplus::Graphics g(hdc);
    g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    cardpaint::PaintCardFrame(g, rcCard);
    cardpaint::PaintCardTile(g, left, headerTop + (kCardHeaderH - cardstyle::kTileSize) / 2, style);

    //标题：一行，在头部里垂直居中，过长以省略号收尾
    const int titleLeft = left + cardstyle::kTileSize + cardstyle::kTileTextGap;
    const int titleTop = headerTop + (kCardHeaderH - cardstyle::kTitleLineH) / 2;
    const RECT titleRc = { titleLeft, titleTop, right, titleTop + cardstyle::kTitleLineH };
    DrawGdiText(hdc, title.c_str(), titleRc, cardstyle::kTitlePx, true, cardstyle::kTitleColor,
                DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);

    //摘要：卡片内宽自动折行，高度已在测量时按最多三行算好，最后一行超出时以省略号收尾
    if (summaryH > 0)
    {
        const int sumTop = headerTop + kCardHeaderH + kCardSummaryGap;
        const RECT sumRc = { left, sumTop, right, sumTop + summaryH };
        DrawGdiText(hdc, summary.c_str(), sumRc, kCardSummaryPx, false, cardstyle::kDescText,
                    DT_LEFT | DT_TOP | DT_WORDBREAK | DT_EDITCONTROL | DT_END_ELLIPSIS | DT_NOPREFIX);
    }

    //底部一行：只有类型标签，没有按钮（方案第 1 节第 5 条：点整张卡片打开预览）
    const RECT footer = { rcCard.left, rcCard.bottom - cardstyle::kFooterH, rcCard.right, rcCard.bottom };
    cardpaint::PaintCardFooter(hdc, g, footer, style, ChatCardTypeLabel(styleMsg), std::vector<ChatCardButton>(), -1);
}
} // namespace balloonchatwnd
