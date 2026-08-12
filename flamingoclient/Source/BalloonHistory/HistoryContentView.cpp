/**
 *  「消息记录」列表行里的多行内容块实现，HistoryContentView.cpp
 *
 *  排版与绘制全部委托给 balloonchatwnd 的内联内容引擎（ChatInlineText.h），本文件
 *  只负责三件事：把正文解析成段序列并缓存、按折叠 / 展开态算行数上限与高度、
 *  在引擎画完之后补上高亮底色与折叠省略号。
 *
 *  balloonwj@qq.com   2026-08-05
 */
#include "stdafx.h"
#include "HistoryContentView.h"

#include <string>

#include "../BalloonChatWnd/ChatFaceTag.h"          // kFaceDisplayPx
#include "../BalloonChatWnd/ChatTextSelection.h"    // SelLayout / BuildSelectionRects / HitTestCharRanges
#include "../CustomMsgDef.h"                        // WM_CHAT_OPEN_LINK（点链接后请宿主窗打开）
#include "../../balloonui/balloonui/DuiHost.h"      // GetHost()->m_hWnd（投消息给宿主窗）

namespace balloonhistory {

//关键字高亮底色：淡黄。原样搬自 HistoryHighlightLabel（本控件将取代它），
//两处观感保持一致 —— 比正文浅得多，压不住字，又足以一眼看见。
static const COLORREF kHighlightColor = RGB(0xFF, 0xF1, 0xA8);

//正文默认颜色：深灰，与聊天气泡正文同色。
static const COLORREF kDefaultTextColor = RGB(0x33, 0x33, 0x33);

//超链接文字色。与聊天气泡里的 kLinkFg 同一档蓝 —— 同一条消息在气泡里和在消息记录
//里必须长得一样，两处的色值不能各定各的。
static const COLORREF kLinkTextColor = RGB(0x2F, 0x6F, 0xD0);

//折叠态末行结尾的省略号（U+2026 HORIZONTAL ELLIPSIS）。
//
//写成码点转义而不是直接敲那个字符：主客户端工程只有 Debug|Win32 那份配置加了
//编译开关 /utf-8（Flamingo.vcxproj:75），Release 那份没加；转义形式与编译选项
//无关，换哪个配置编出来都是同一个字符。
static const wchar_t* const kCollapsedEllipsis = L"\x2026";

//省略号的字符个数。U+2026 是单个 UTF-16 字符，量宽度时按 1 传给 GDI。
static const int kCollapsedEllipsisLen = 1;

//传给排版引擎表示"不限行"的行数上限取值（见 LayoutInlineContent 的 maxLines 注释）。
static const int kNoLineLimit = 0;

HistoryContentView::HistoryContentView()
    : m_hasFace(false)
    , m_textColor(kDefaultTextColor)
    , m_fontPx(kHistoryContentDefaultFontPx)
    , m_expanded(false)
    , m_measuredWidth(-1)
    , m_measuredExpanded(false)
    , m_measuredHeight(0)
    , m_measuredNeedExpand(false)
    , m_linkLayoutValid(false)
{
    m_linkOrigin.x = 0;
    m_linkOrigin.y = 0;
}

void HistoryContentView::SetContent(const CString& fullText)
{
    //只在这里解析一次。绘制每秒可能跑很多遍，逐次重解析正文纯属浪费。
    m_segs = balloonchat::ParseSendBody(fullText);

    m_hasFace = false;
    for (size_t i = 0; i < m_segs.size(); ++i)
    {
        if (m_segs[i].m_bFace)
        {
            m_hasFace = true;
            break;
        }
    }

    //顺带识别正文里的超链接（与纯文本入口共用同一段）。
    RebuildLinks();

    //内容换了，之前量出来的高度与"要不要展开链接"一概作废。
    InvalidateMeasureCache();
    Invalidate();
}

void HistoryContentView::SetPlainContent(const CString& plainText)
{
    //纯文本：整串就是一个文字段，不解析表情标记、不把 "//" 折成一个斜杠。
    m_segs.clear();
    if (!plainText.IsEmpty())
    {
        m_segs.push_back(balloonchat::ChatSegment::Text(plainText));
    }
    m_hasFace = false;

    RebuildLinks();
    InvalidateMeasureCache();
    Invalidate();
}

CString HistoryContentView::GetFlatText() const
{
    const std::wstring flat = balloonchatwnd::BuildFlatText(m_segs);
    return CString(flat.c_str());
}

void HistoryContentView::RebuildLinks()
{
    //口径与高亮区间一致（扁平文本下标），故渲染时能直接拿去要矩形、命中时能直接
    //拿去做区间命中。识别开关关掉时得到空表，后面渲染与命中两处自然什么都不做。
    m_links.clear();
    if (balloonchatwnd::ChatLinkOptionEnabled())
    {
        m_links = balloonchatwnd::DetectLinks(balloonchatwnd::BuildFlatText(m_segs));
    }
    //上一份内容的排版模型对新内容无意义，先作废，等下次绘制重建。
    m_linkLayoutValid = false;
}

void HistoryContentView::SetHighlightRanges(const std::vector<HighlightRange>& ranges)
{
    //高亮只是在原位置铺一层底色，不改折行、不改行高，故不动测量缓存。
    m_ranges = ranges;
    Invalidate();
}

void HistoryContentView::SetExpanded(bool expanded)
{
    if (m_expanded == expanded)
    {
        return;
    }
    m_expanded = expanded;
    //测量缓存的键里带了折叠态，这里不必显式作废：下次 MeasureHeight 会因为键不同
    //而自动重测，而切回原来那一态时还能命中上一次的结果。
    Invalidate();
}

void HistoryContentView::SetTextColor(COLORREF color)
{
    m_textColor = color;
    Invalidate();
}

void HistoryContentView::SetFontPixelSize(int px)
{
    if (px <= 0)
    {
        return;
    }
    if (m_fontPx == px)
    {
        return;
    }
    m_fontPx = px;
    //字号变了折行位置与行高都会变，测量缓存必须作废。
    InvalidateMeasureCache();
    Invalidate();
}

int HistoryContentView::MeasureHeight(int availWidth) const
{
    EnsureMeasured(availWidth);
    return m_measuredHeight;
}

bool HistoryContentView::NeedsExpandLink(int availWidth) const
{
    EnsureMeasured(availWidth);
    return m_measuredNeedExpand;
}

//本控件整块重画，不按脏矩形做局部裁剪 —— 一行内容至多几十个放置项，
//为它维护一套"哪几行脏了"的账本不划算。
void HistoryContentView::OnPaint(HDC hdc, const RECT& /*rcDirty*/)
{
    const RECT rc = GetRect();
    const int availW = rc.right - rc.left;
    if (availW <= 0)
    {
        return;
    }

    const int maxLines = EffectiveMaxLines();
    const int faceSize = EffectiveFaceSize();

    //需要自己排一趟版的有三件事：铺高亮底色、折叠态末行补省略号、把超链接重画成
    //链接色。三件都不沾时（无高亮 + 展开态 + 无链接）就省掉这一趟，绘制交给
    //DrawInlineContent 内部那趟即可。
    const bool needPlaced = (!m_ranges.empty() || maxLines > kNoLineLimit || !m_links.empty());

    std::vector<balloonchatwnd::InlinePlaced> placed;
    bool  truncated = false;
    HFONT font      = NULL;
    HFONT oldFont   = NULL;
    if (needPlaced)
    {
        font = CreateContentFont();
    }
    if (font != NULL)
    {
        //排版引擎按<u>当前选进 hdc 的字体</u>做测量，故必须先选字体再排版。
        oldFont = (HFONT)::SelectObject(hdc, font);
        balloonchatwnd::LayoutInlineContent(hdc, m_segs, availW, faceSize,
                                            &placed, maxLines, &truncated);
        //底色必须先于文字铺，反过来会把刚画好的字盖掉。
        PaintHighlights(hdc, rc, placed);
    }

    //文字与表情整套交给气泡那边同一个绘制入口，两处渲染口径一致。它内部自建字体、
    //自己再排一趟版，画完还原设备上下文 —— 两趟排版的入参（字体、宽度、表情格边长、
    //行数上限）完全相同，结果必然一致，上面那趟只是拿来给底色与省略号定位。
    balloonchatwnd::InlineTextStyle st;
    st.sizePx = m_fontPx;
    st.color  = m_textColor;
    balloonchatwnd::DrawInlineContent(hdc, m_segs, rc, st, faceSize, maxLines);

    if (font != NULL)
    {
        if (maxLines > kNoLineLimit && truncated)
        {
            PaintCollapsedEllipsis(hdc, rc, placed);
        }
        //超链接放在最后画：盖在正文之上，与聊天气泡那边的层次一致。
        PaintLinks(hdc, rc, placed);
        ::SelectObject(hdc, oldFont);
        ::DeleteObject(font);
    }
}

void HistoryContentView::PaintLinks(HDC hdc, const RECT& rc,
                                    const std::vector<balloonchatwnd::InlinePlaced>& placed)
{
    if (m_links.empty() || placed.empty())
    {
        return;
    }

    //与高亮底色走同一条路：排版结果 → 字符级模型 → 按字符区间取矩形。模型同时
    //存下来给命中测试用（鼠标事件里没有 HDC，排不了版）。
    const std::wstring flat = balloonchatwnd::BuildFlatText(m_segs);
    balloonchatwnd::BuildSelLayoutFromPlaced(hdc, placed, (int)flat.size(), m_linkLayout);
    m_linkOrigin.x    = rc.left;
    m_linkOrigin.y    = rc.top;
    m_linkLayoutValid = true;

    balloonchatwnd::InlineTextStyle ls;
    ls.sizePx    = m_fontPx;
    ls.color     = kLinkTextColor;
    ls.underline = true;

    const int faceSize = EffectiveFaceSize();
    for (size_t i = 0; i < m_links.size(); ++i)
    {
        balloonchatwnd::DrawInlineContentRestyledRange(
            hdc, m_segs, m_linkLayout,
            m_links[i].m_off, m_links[i].m_off + m_links[i].m_len,
            rc, ls, faceSize);
    }
}

int HistoryContentView::LinkAtPoint(POINT pt) const
{
    if (m_links.empty() || !m_linkLayoutValid || m_linkLayout.m_lines.empty())
    {
        return -1;
    }

    std::vector<std::pair<int, int> > ranges;
    ranges.reserve(m_links.size());
    for (size_t i = 0; i < m_links.size(); ++i)
    {
        ranges.push_back(std::make_pair(m_links[i].m_off,
                                        m_links[i].m_off + m_links[i].m_len));
    }
    return balloonchatwnd::HitTestCharRanges(m_linkLayout, ranges,
                                             pt.x - m_linkOrigin.x,
                                             pt.y - m_linkOrigin.y);
}

bool HistoryContentView::OnSetCursor(POINT pt)
{
    if (LinkAtPoint(pt) >= 0)
    {
        ::SetCursor(::LoadCursor(NULL, IDC_HAND));
        return true;
    }
    return false;
}

bool HistoryContentView::OnLButtonUp(POINT pt, UINT /*mkFlags*/)
{
    const int idx = LinkAtPoint(pt);
    if (idx < 0)
    {
        return false;   // 没点在链接上：不消费，交回给列表行自己的点击处理
    }

    balloonwjui::DuiHost* host = GetHost();
    HWND hwnd = (host != NULL) ? host->m_hWnd : NULL;
    if (hwnd == NULL)
    {
        return false;
    }

    //与聊天气泡同一条路：本控件不碰 ShellExecute，URL 交宿主窗过协议白名单后再打开。
    std::wstring* pUrl = new std::wstring(m_links[(size_t)idx].m_url);
    if (!::PostMessage(hwnd, WM_CHAT_OPEN_LINK, 0, (LPARAM)pUrl))
    {
        delete pUrl;
    }
    return true;
}

HFONT HistoryContentView::CreateContentFont() const
{
    //参数与 DrawInlineContent 内部建的那支逐项一致（微软雅黑、常规字重、
    //ClearType）—— 测量与绘制用的字体但凡不是同一套，量出来的宽高就与画出来的对不上。
    return ::CreateFontW(-m_fontPx, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                         DEFAULT_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS,
                         CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE,
                         L"Microsoft YaHei");
}

int HistoryContentView::EffectiveMaxLines() const
{
    if (m_expanded)
    {
        return kNoLineLimit;
    }
    return kHistoryCollapsedMaxLines;
}

int HistoryContentView::EffectiveFaceSize() const
{
    if (!m_hasFace)
    {
        return 0;
    }
    return balloonchatwnd::kFaceDisplayPx;
}

void HistoryContentView::InvalidateMeasureCache()
{
    m_measuredWidth      = -1;
    m_measuredHeight     = 0;
    m_measuredNeedExpand = false;
}

void HistoryContentView::EnsureMeasured(int availWidth) const
{
    int useWidth = availWidth;
    if (useWidth < 1)
    {
        useWidth = 1;
    }

    //缓存命中：宽度与折叠态都没变就直接复用（内容 / 字号若变过，
    //InvalidateMeasureCache 已把 m_measuredWidth 置 -1，下式不成立而强制重测）。
    if (m_measuredWidth == useWidth && m_measuredExpanded == m_expanded)
    {
        return;
    }

    //先落一份退化结果：下面任何一步拿不到 GDI 资源都按"至少一行"收场。行高绝不能
    //是 0 —— 那一行会整个塌掉，用户看到的是一条什么都没有的空行。
    m_measuredWidth      = useWidth;
    m_measuredExpanded   = m_expanded;
    m_measuredHeight     = m_fontPx;
    m_measuredNeedExpand = false;

    //自建内存 DC 来量：行高是在布局阶段问的，那时本控件未必已经进树，也就拿不到
    //宿主窗的 DC；而字体度量只取决于字体本身，与画在哪个 DC 上无关。
    HDC memDc = ::CreateCompatibleDC(NULL);
    if (memDc == NULL)
    {
        return;
    }
    HFONT font = CreateContentFont();
    if (font == NULL)
    {
        ::DeleteDC(memDc);
        return;
    }
    HFONT oldFont = (HFONT)::SelectObject(memDc, font);

    const int faceSize = EffectiveFaceSize();

    //折叠态那一趟必测：它既给出折叠态的高度，其 truncated 出参又正好就是
    //"内容超没超过折叠上限"——也就是要不要画那枚展开链接（该判据与当前是折叠
    //还是展开无关，故两态都按折叠口径算）。
    bool truncated = false;
    SIZE szCollapsed = balloonchatwnd::LayoutInlineContent(
        memDc, m_segs, useWidth, faceSize, NULL,
        kHistoryCollapsedMaxLines, &truncated);
    m_measuredNeedExpand = truncated;
    m_measuredHeight     = szCollapsed.cy;

    //只有"展开态 + 确实被截断过"才需要再排一趟不限行的。没截断说明折叠那趟排出来
    //的已经是全貌，两态高度本就相同，再排一趟纯属白费。
    if (m_expanded && truncated)
    {
        SIZE szFull = balloonchatwnd::LayoutInlineContent(
            memDc, m_segs, useWidth, faceSize, NULL, kNoLineLimit, NULL);
        m_measuredHeight = szFull.cy;
    }

    ::SelectObject(memDc, oldFont);
    ::DeleteObject(font);
    ::DeleteDC(memDc);
}

void HistoryContentView::PaintHighlights(HDC hdc, const RECT& rc,
                                         const std::vector<balloonchatwnd::InlinePlaced>& placed)
{
    if (m_ranges.empty() || placed.empty())
    {
        return;
    }

    //走"排版结果 → 选区模型 → 高亮矩形"这条路，而不是像单行标签那样自己按字宽
    //累加：选区模型已经把两件麻烦事处理好了 —— 一段命中横跨两行时会自动切成两块
    //矩形；命中区间里夹着表情时那一格按整格宽算，不会被当成文字去量宽度。
    const std::wstring flat = balloonchatwnd::BuildFlatText(m_segs);
    balloonchatwnd::SelLayout layout;
    balloonchatwnd::BuildSelLayoutFromPlaced(hdc, placed, (int)flat.size(), layout);

    HBRUSH brush = ::CreateSolidBrush(kHighlightColor);
    if (brush == NULL)
    {
        return;
    }

    for (size_t i = 0; i < m_ranges.size(); ++i)
    {
        const int begin = m_ranges[i].m_begin;
        const int len   = m_ranges[i].m_len;
        //越界或长度非正的区间直接跳过 —— 由本类兜住，调用方不必预先过滤。
        //右端越界不必自己夹，BuildSelectionRects 内部会夹到全文长度。
        if (begin < 0 || len <= 0)
        {
            continue;
        }

        const std::vector<balloonchatwnd::SelRect> rects =
            balloonchatwnd::BuildSelectionRects(layout, begin, begin + len);
        for (size_t k = 0; k < rects.size(); ++k)
        {
            //选区矩形是相对文本区原点的，平移到本控件的绝对坐标。
            //这里<u>不</u>再像单行标签那样上下各外扩 2px：那是因为单行时矩形只有
            //字高、显得贴着字；多行的矩形取的是整个行框（含行距），再外扩就会和
            //上下相邻行的底色粘连成一片。
            RECT hl;
            hl.left   = rc.left + rects[k].m_left;
            hl.right  = rc.left + rects[k].m_right;
            hl.top    = rc.top  + rects[k].m_top;
            hl.bottom = rc.top  + rects[k].m_bottom;

            //超出本控件可视区的部分不画，免得糊到隔壁列上。
            if (hl.left >= rc.right)
            {
                continue;
            }
            if (hl.right > rc.right)
            {
                hl.right = rc.right;
            }
            if (hl.bottom > rc.bottom)
            {
                hl.bottom = rc.bottom;
            }
            ::FillRect(hdc, &hl, brush);
        }
    }

    ::DeleteObject(brush);
}

void HistoryContentView::PaintCollapsedEllipsis(HDC hdc, const RECT& rc,
                                                const std::vector<balloonchatwnd::InlinePlaced>& placed)
{
    if (placed.empty())
    {
        return;
    }

    //末行就是放置项里 top 最大的那一行（放置项按文档顺序产出，末行恒在最下）。
    int lastTop = placed[0].rc.top;
    for (size_t i = 1; i < placed.size(); ++i)
    {
        if (placed[i].rc.top > lastTop)
        {
            lastTop = placed[i].rc.top;
        }
    }

    //省略号紧跟在末行最右一项之后；行框下沿取该行各项 bottom 的最大值，
    //这样纵向居中的口径与正文一致。
    int lastRight  = 0;
    int lastBottom = 0;
    for (size_t i = 0; i < placed.size(); ++i)
    {
        if (placed[i].rc.top != lastTop)
        {
            continue;
        }
        if (placed[i].rc.right > lastRight)
        {
            lastRight = placed[i].rc.right;
        }
        if (placed[i].rc.bottom > lastBottom)
        {
            lastBottom = placed[i].rc.bottom;
        }
    }

    SIZE dot;
    dot.cx = 0;
    dot.cy = 0;
    ::GetTextExtentPoint32W(hdc, kCollapsedEllipsis, kCollapsedEllipsisLen, &dot);

    const int availW = rc.right - rc.left;
    int x = lastRight;
    //末行正好排满时省略号会被挤出右边界，往左挪到贴着边界为止。此时它会压住行尾
    //一两个字符 —— 这是有意的取舍：省略号本来就代表"后面还有"，宁可盖住行尾一点，
    //也不能让它跑到内容区之外去（那样它会画到隔壁列上）。
    if (x + dot.cx > availW)
    {
        x = availW - dot.cx;
    }
    if (x < 0)
    {
        x = 0;
    }

    RECT dotRc;
    dotRc.left   = rc.left + x;
    dotRc.right  = dotRc.left + dot.cx;
    dotRc.top    = rc.top + lastTop;
    dotRc.bottom = rc.top + lastBottom;

    const int      oldBkMode = ::SetBkMode(hdc, TRANSPARENT);
    const COLORREF oldColor  = ::SetTextColor(hdc, m_textColor);
    ::DrawTextW(hdc, kCollapsedEllipsis, kCollapsedEllipsisLen, &dotRc,
                DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_NOCLIP);
    ::SetTextColor(hdc, oldColor);
    ::SetBkMode(hdc, oldBkMode);
}

} // namespace balloonhistory
