/**
 *  Markdown 消息卡片共用部分的实现，说明见 ChatMarkdownCard.h。尺寸、颜色与绘制取自 ChatBubble.cpp 原来的 Markdown
 *  卡片分支（2026-10-07 抽出），数值与合并转发卡片（ChatBubble.cpp 的 kMerged*）相同。
 *
 *  balloonwj@qq.com   2026-10-07
 */
#include "stdafx.h"
#include "ChatMarkdownCard.h"

#include <objidl.h>
#include <gdiplus.h>

#include <memory>

#include "ChatIcons.h"                                 // DrawChatIcon / kChatIconMarkdown
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

const int kCardPadH            = 14;   // 水平内边距（每侧）
const int kCardTitleTop        = 10;   // 标题距卡片顶
const int kCardTitleH          = 22;   // 标题行高
const int kCardTitlePx         = 14;   // 标题字号
const int kCardLinesGap        = 4;    // 标题与摘要之间的间距
const int kCardLinePx          = 12;   // 摘要字号
const int kCardFooterGap       = 8;    // 摘要与分隔线之间的间距
const int kCardFooterH         = 28;   // 分隔线下方底部文字行的高度
const int kCardFooterPx        = 11;   // 底部文字字号
const int kCardSummaryMaxLines = 3;    // 摘要最多显示几行
const int kCardIconSize        = 14;   // 底部「M↓」图标的边长
const int kCardIconGap         = 6;    // 底部图标与文字之间的间距
const Gdiplus::REAL kCardIconStroke = 2.0f;                      // 底部图标的描边宽度（以 viewBox 24 为基准）
const COLORREF kCardTitleColor = RGB(0x2A, 0x2A, 0x2A);           // 标题颜色，与气泡文字相同
const COLORREF kCardLineColor  = RGB(0x8A, 0x8A, 0x8A);           // 摘要与底部文字的灰色
const Gdiplus::Color kCardDivider(255, 0xE2, 0xDE, 0xDA);        // 分隔线颜色

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
    return MeasureWrappedTextHeight(hdc, summary, cardWidth - kCardPadH * 2, kCardLinePx, kCardSummaryMaxLines);
}

int MarkdownCardHeight(int summaryH)
{
    return kCardTitleTop + kCardTitleH + (summaryH > 0 ? kCardLinesGap + summaryH : 0) + kCardFooterGap + kCardFooterH;
}

void PaintMarkdownCardContent(HDC hdc, const RECT& rcCard, const std::wstring& title, const std::wstring& summary,
                              int summaryH)
{
    const int textL = rcCard.left + kCardPadH;
    const int textR = rcCard.right - kCardPadH;

    //标题：一行，过长以省略号收尾
    int lineTop = rcCard.top + kCardTitleTop;
    RECT titleRc = { textL, lineTop, textR, lineTop + kCardTitleH };
    DrawGdiText(hdc, title.c_str(), titleRc, kCardTitlePx, true, kCardTitleColor,
                DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);

    //摘要：自动折行，高度已在测量时按最多三行算好，最后一行超出时以省略号收尾
    lineTop += kCardTitleH;
    if (summaryH > 0)
    {
        lineTop += kCardLinesGap;
        RECT sumRc = { textL, lineTop, textR, lineTop + summaryH };
        DrawGdiText(hdc, summary.c_str(), sumRc, kCardLinePx, false, kCardLineColor,
                    DT_LEFT | DT_TOP | DT_WORDBREAK | DT_EDITCONTROL | DT_END_ELLIPSIS | DT_NOPREFIX);
    }

    //分隔线与底部图标用 GDI+ 画，画完先释放 Graphics 再画 GDI 文字
    const int footTop = rcCard.bottom - kCardFooterH;
    const int iconTop = footTop + (kCardFooterH - kCardIconSize) / 2;
    {
        Gdiplus::Graphics g(hdc);
        g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        Gdiplus::Pen divider(kCardDivider, 1.0f);
        g.DrawLine(&divider, (Gdiplus::REAL)textL, (Gdiplus::REAL)footTop, (Gdiplus::REAL)textR, (Gdiplus::REAL)footTop);
        const Gdiplus::Color iconColor(255, GetRValue(kCardLineColor), GetGValue(kCardLineColor),
                                       GetBValue(kCardLineColor));
        DrawChatIcon(g, kChatIconMarkdown, (Gdiplus::REAL)textL, (Gdiplus::REAL)iconTop, (Gdiplus::REAL)kCardIconSize,
                     iconColor, kCardIconStroke);
    }
    RECT footRc = { textL + kCardIconSize + kCardIconGap, footTop, textR, rcCard.bottom };
    DrawGdiText(hdc, TR(L"Markdown 文档"), footRc, kCardFooterPx, false, kCardLineColor,
                DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
}

} // namespace balloonchatwnd
