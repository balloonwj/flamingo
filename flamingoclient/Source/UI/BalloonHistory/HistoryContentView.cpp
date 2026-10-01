/**
 *  「消息记录」列表行里的多行内容块实现，HistoryContentView.cpp
 *
 *  排版与绘制全部委托给 balloonchatwnd 的内联内容引擎（ChatInlineText.h），本文件
 *  负责：把正文解析成段序列并缓存、按折叠 / 展开态算行数上限与高度、在引擎画完
 *  之前 / 之后补上高亮底色、选区底色、折叠省略号与超链接，以及鼠标拖选。
 *
 *  balloonwj@qq.com   2026-08-05
 */
#include "stdafx.h"
#include "HistoryContentView.h"

#include <string>

#include "../BalloonChatWnd/ChatFaceTag.h"          // kFaceDisplayPx
#include "../BalloonChatWnd/ChatTextSelection.h"    // SelLayout / BuildSelectionRects / CharIndexFromPoint
#include "../../App/CustomMsgDef.h"                        // WM_CHAT_OPEN_LINK（点链接后请宿主窗打开）
#include "../../../third-party/balloonui/balloonui/DuiHost.h"      // GetHost()->m_hWnd（投消息给宿主窗）

namespace balloonhistory {

//关键字高亮底色：淡黄。原样搬自 HistoryHighlightLabel（本控件将取代它），
//两处观感保持一致 —— 比正文浅得多，压不住字，又足以一眼看见。
static const COLORREF kHighlightColor = RGB(0xFF, 0xF1, 0xA8);

//选区底色：浅蓝。与聊天气泡里别人消息的选区同色（ChatBubble.cpp 的
//kSelHighlightOnOther）—— 消息记录的行是白底，与那种气泡的底色最接近。
static const COLORREF kSelectionColor = RGB(0xCC, 0xE0, 0xF8);

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

//单行模式下的行数上限。
static const int kSingleLineMaxLines = 1;

HistoryContentView::HistoryContentView()
    : m_hasFace(false)
    , m_textColor(kDefaultTextColor)
    , m_fontPx(kHistoryContentDefaultFontPx)
    , m_expanded(false)
    , m_singleLine(false)
    , m_measuredWidth(-1)
    , m_measuredExpanded(false)
    , m_measuredHeight(0)
    , m_measuredNeedExpand(false)
    , m_charLayoutValid(false)
    , m_visibleEnd(0)
    , m_selAnchor(-1)
    , m_selCaret(-1)
    , m_selDragging(false)
    , m_linkPressIdx(-1)
{
    m_charOrigin.x = 0;
    m_charOrigin.y = 0;
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

    //旧选区的下标对新内容没有意义。
    ResetSelectionState();

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
    ResetSelectionState();
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
    m_charLayoutValid = false;
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

void HistoryContentView::SetSingleLine(bool singleLine)
{
    if (m_singleLine == singleLine)
    {
        return;
    }
    m_singleLine = singleLine;
    //行数上限变了，量出来的高度与"要不要展开链接"都要重算。
    InvalidateMeasureCache();
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

    //每次绘制都自己排一趟版，建出字符级排版模型：拖选随时可能发生，鼠标事件里没有
    //HDC、排不了版，只能用绘制时存下的模型。高亮底色、选区底色、折叠省略号与超链接
    //也都靠这份模型定位。一行内容至多几十个放置项，多排一趟的开销可以忽略。
    std::vector<balloonchatwnd::InlinePlaced> placed;
    bool  truncated = false;
    HFONT font      = CreateContentFont();
    HFONT oldFont   = NULL;
    if (font != NULL)
    {
        //排版引擎按<u>当前选进 hdc 的字体</u>做测量，故必须先选字体再排版。
        oldFont = (HFONT)::SelectObject(hdc, font);
        balloonchatwnd::LayoutInlineContent(hdc, m_segs, availW, faceSize,
                                            &placed, maxLines, &truncated);

        const std::wstring flat = balloonchatwnd::BuildFlatText(m_segs);
        balloonchatwnd::BuildSelLayoutFromPlaced(hdc, placed, (int)flat.size(), m_charLayout);
        m_charOrigin.x    = rc.left;
        m_charOrigin.y    = rc.top;
        m_charLayoutValid = true;

        //可见部分的末尾：各段末尾下标的最大值。折叠态下它小于全文长度。
        m_visibleEnd = 0;
        for (size_t i = 0; i < m_charLayout.m_lines.size(); ++i)
        {
            const balloonchatwnd::SelLine& line = m_charLayout.m_lines[i];
            for (size_t k = 0; k < line.m_runs.size(); ++k)
            {
                const int runEnd = line.m_runs[k].m_charBegin + line.m_runs[k].m_charCount;
                if (runEnd > m_visibleEnd)
                {
                    m_visibleEnd = runEnd;
                }
            }
        }

        //底色必须先于文字铺，反过来会把刚画好的字盖掉。选区铺在高亮之上：用户正在
        //选的那一段要看得出来，哪怕它恰好是搜索命中处。
        PaintHighlights(hdc, rc);
        PaintSelection(hdc, rc);
    }

    //文字与表情整套交给气泡那边同一个绘制入口，两处渲染口径一致。它内部自建字体、
    //自己再排一趟版，画完还原设备上下文 —— 两趟排版的入参（字体、宽度、表情格边长、
    //行数上限）完全相同，结果必然一致，上面那趟只是拿来给底色、省略号与链接定位。
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
        PaintLinks(hdc, rc);
        ::SelectObject(hdc, oldFont);
        ::DeleteObject(font);
    }
}

void HistoryContentView::PaintLinks(HDC hdc, const RECT& rc)
{
    if (m_links.empty() || !m_charLayoutValid || m_charLayout.m_lines.empty())
    {
        return;
    }

    balloonchatwnd::InlineTextStyle ls;
    ls.sizePx    = m_fontPx;
    ls.color     = kLinkTextColor;
    ls.underline = true;

    //所有链接一次画完、整条正文只排版一次。本控件没有渲染缓存，每次绘制都要画链接，
    //逐个链接调用的话，几十行、每行一个链接的消息展开后滚动一帧就要几秒
    std::vector<balloonchatwnd::InlineCharRange> ranges;
    ranges.reserve(m_links.size());
    for (size_t i = 0; i < m_links.size(); ++i)
    {
        ranges.push_back(balloonchatwnd::InlineCharRange(m_links[i].m_off, m_links[i].m_off + m_links[i].m_len));
    }
    balloonchatwnd::DrawInlineContentRestyledRanges(hdc, m_segs, m_charLayout, ranges, rc, ls,
                                                    EffectiveFaceSize());
}

int HistoryContentView::LinkAtPoint(POINT pt) const
{
    if (m_links.empty() || !m_charLayoutValid || m_charLayout.m_lines.empty())
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
    return balloonchatwnd::HitTestCharRanges(m_charLayout, ranges,
                                             pt.x - m_charOrigin.x,
                                             pt.y - m_charOrigin.y);
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

bool HistoryContentView::OnLButtonDown(POINT pt, UINT /*mkFlags*/)
{
    //还没画过就没有排版模型，落点映射不成字符下标，这一下不处理。
    if (!m_charLayoutValid)
    {
        return false;
    }

    //记下按下时命中的链接（没命中为 -1）。按下这一下<u>不</u>当作点击：从链接上起手
    //照样可以拖选，只有抬起时仍在同一条链接上、且没有选出文字，才算点了链接。
    m_linkPressIdx = LinkAtPoint(pt);

    //同一时刻整棵树里只留一处选区：在这一条上开始拖选，先清掉其它条的。
    ClearTextSelectionsInTree(TreeRoot(), this);

    const int idx = CharIndexAtPoint(pt);
    m_selAnchor   = idx;
    m_selCaret    = idx;
    m_selDragging = true;
    //抓取鼠标：拖出本控件乃至拖出窗口都要继续收到移动事件，否则一出界选区就断了。
    Capture();
    Invalidate();
    return true;
}

bool HistoryContentView::OnMouseMove(POINT pt, UINT /*mkFlags*/)
{
    if (!m_selDragging)
    {
        return false;   //不是在拖选：交给框架继续分发（行的悬停等）
    }
    const int idx = CharIndexAtPoint(pt);
    if (idx != m_selCaret)
    {
        m_selCaret = idx;
        Invalidate();
    }
    return true;
}

bool HistoryContentView::OnLButtonUp(POINT pt, UINT /*mkFlags*/)
{
    //没有在拖选（例如按下时还没画过、或双击刚把拖选状态收掉）：链接也不在这里打开 ——
    //链接点击只认"按下与抬起都落在同一条链接上"这一条路，见 OnLButtonDown。
    if (!m_selDragging)
    {
        return false;
    }

    m_selCaret    = CharIndexAtPoint(pt);
    m_selDragging = false;
    ReleaseCapture();

    //这一趟没有选出文字、且抬起处仍在按下的那条链接上：当成点击，请宿主窗打开它。
    const int pressed = m_linkPressIdx;
    m_linkPressIdx = -1;
    if (pressed >= 0 && !HasTextSelection() && LinkAtPoint(pt) == pressed)
    {
        OpenLinkByIndex(pressed);
    }

    Invalidate();
    return true;
}

bool HistoryContentView::OnLButtonDblClk(POINT /*pt*/, UINT /*mkFlags*/)
{
    //双击的第一下已经走过一遍按下 / 抬起；这里把可能残留的拖选状态收掉。
    if (m_selDragging)
    {
        m_selDragging = false;
        ReleaseCapture();
    }
    m_linkPressIdx = -1;

    //双击 = 选中整条（与聊天气泡一致）。想选其中一段仍然靠按下拖动。
    ClearTextSelectionsInTree(TreeRoot(), this);
    SelectAllText();
    return true;
}

bool HistoryContentView::HasTextSelection() const
{
    if (m_selAnchor < 0 || m_selCaret < 0)
    {
        return false;
    }
    return m_selAnchor != m_selCaret;
}

void HistoryContentView::ClearTextSelection()
{
    if (m_selAnchor < 0 && m_selCaret < 0 && !m_selDragging)
    {
        return;
    }
    if (m_selDragging)
    {
        ReleaseCapture();
    }
    ResetSelectionState();
    Invalidate();
}

void HistoryContentView::SelectAllText()
{
    //整条包括折叠态下没显示出来的部分："选中整条"复制的就是这一整条消息。
    //画出来的底色由 BuildSelectionRects 夹在可见部分之内，不会画到控件外面。
    m_selAnchor   = 0;
    m_selCaret    = (int)balloonchatwnd::BuildFlatText(m_segs).size();
    m_selDragging = false;
    Invalidate();
}

bool HistoryContentView::PointInTextSelection(POINT pt) const
{
    if (!HasTextSelection() || !m_charLayoutValid)
    {
        return false;
    }
    int s = 0;
    int e = 0;
    balloonchatwnd::NormalizeSelRange(m_selAnchor, m_selCaret, s, e);
    const std::vector<balloonchatwnd::SelRect> rects =
        balloonchatwnd::BuildSelectionRects(m_charLayout, s, e);

    const int x = pt.x - m_charOrigin.x;
    const int y = pt.y - m_charOrigin.y;
    for (size_t i = 0; i < rects.size(); ++i)
    {
        if (x >= rects[i].m_left && x < rects[i].m_right
            && y >= rects[i].m_top && y < rects[i].m_bottom)
        {
            return true;
        }
    }
    return false;
}

std::vector<balloonchatwnd::ChatClipSegment> HistoryContentView::GetSelectedClipSegments() const
{
    if (!HasTextSelection())
    {
        return std::vector<balloonchatwnd::ChatClipSegment>();
    }
    int s = 0;
    int e = 0;
    balloonchatwnd::NormalizeSelRange(m_selAnchor, m_selCaret, s, e);
    return balloonchatwnd::BuildClipSegmentsFromSelection(m_segs, s, e);
}

void HistoryContentView::ClearTextSelectionsInTree(balloonwjui::DuiControl* root,
                                                   const HistoryContentView* keep)
{
    if (root == NULL)
    {
        return;
    }
    HistoryContentView* view = dynamic_cast<HistoryContentView*>(root);
    if (view != NULL && view != keep)
    {
        view->ClearTextSelection();
    }
    const std::vector<std::unique_ptr<balloonwjui::DuiControl>>& kids = root->Children();
    for (size_t i = 0; i < kids.size(); ++i)
    {
        ClearTextSelectionsInTree(kids[i].get(), keep);
    }
}

const balloonchatwnd::SelLayout* HistoryContentView::Test_GetSelLayout() const
{
    return m_charLayoutValid ? &m_charLayout : NULL;
}

int HistoryContentView::CharIndexAtPoint(POINT pt) const
{
    int idx = balloonchatwnd::CharIndexFromPoint(m_charLayout,
                                                 pt.x - m_charOrigin.x,
                                                 pt.y - m_charOrigin.y);
    //映射规则把"末行之下"归到全文末尾；折叠态下全文末尾在看不见的地方，
    //终点只能停在可见部分的末尾。
    if (idx > m_visibleEnd)
    {
        idx = m_visibleEnd;
    }
    return idx;
}

balloonwjui::DuiControl* HistoryContentView::TreeRoot()
{
    balloonwjui::DuiControl* node = this;
    while (node->GetParent() != NULL)
    {
        node = node->GetParent();
    }
    return node;
}

void HistoryContentView::OpenLinkByIndex(int index)
{
    if (index < 0 || (size_t)index >= m_links.size())
    {
        return;
    }
    balloonwjui::DuiHost* host = GetHost();
    HWND hwnd = (host != NULL) ? host->m_hWnd : NULL;
    if (!::IsWindow(hwnd))
    {
        return;
    }

    //与聊天气泡同一条路：本控件不碰 ShellExecute，URL 交宿主窗过协议白名单后再打开。
    std::wstring* pUrl = new std::wstring(m_links[(size_t)index].m_url);
    if (!::PostMessage(hwnd, WM_CHAT_OPEN_LINK, 0, (LPARAM)pUrl))
    {
        delete pUrl;
    }
}

void HistoryContentView::ResetSelectionState()
{
    m_selAnchor    = -1;
    m_selCaret     = -1;
    m_selDragging  = false;
    m_linkPressIdx = -1;
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
    if (m_singleLine)
    {
        return kSingleLineMaxLines;
    }
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
    //单行模式下"折叠态"就是那一行：高度与省略号判据都按一行算，与绘制一致。
    const int collapsedLines = m_singleLine ? kSingleLineMaxLines : kHistoryCollapsedMaxLines;
    bool truncated = false;
    SIZE szCollapsed = balloonchatwnd::LayoutInlineContent(
        memDc, m_segs, useWidth, faceSize, NULL,
        collapsedLines, &truncated);
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

void HistoryContentView::PaintHighlights(HDC hdc, const RECT& rc)
{
    if (m_ranges.empty())
    {
        return;
    }

    //走"排版模型 → 按字符区间取矩形"这条路，而不是像单行标签那样自己按字宽累加：
    //选区模型已经把两件麻烦事处理好了 —— 一段命中横跨两行时会自动切成两块矩形；
    //命中区间里夹着表情时那一格按整格宽算，不会被当成文字去量宽度。
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
        FillCharRanges(hdc, rc, begin, begin + len, brush);
    }

    ::DeleteObject(brush);
}

void HistoryContentView::PaintSelection(HDC hdc, const RECT& rc)
{
    if (!HasTextSelection())
    {
        return;
    }
    int s = 0;
    int e = 0;
    balloonchatwnd::NormalizeSelRange(m_selAnchor, m_selCaret, s, e);

    HBRUSH brush = ::CreateSolidBrush(kSelectionColor);
    if (brush == NULL)
    {
        return;
    }
    FillCharRanges(hdc, rc, s, e, brush);
    ::DeleteObject(brush);
}

void HistoryContentView::FillCharRanges(HDC hdc, const RECT& rc, int start, int end, HBRUSH brush)
{
    const std::vector<balloonchatwnd::SelRect> rects =
        balloonchatwnd::BuildSelectionRects(m_charLayout, start, end);
    for (size_t k = 0; k < rects.size(); ++k)
    {
        //选区矩形是相对文本区原点的，平移到本控件的绝对坐标。
        //这里<u>不</u>再像单行标签那样上下各外扩 2px：那是因为单行时矩形只有
        //字高、显得贴着字；多行的矩形取的是整个行框（含行距），再外扩就会和
        //上下相邻行的底色粘连成一片。
        RECT fill;
        fill.left   = rc.left + rects[k].m_left;
        fill.right  = rc.left + rects[k].m_right;
        fill.top    = rc.top  + rects[k].m_top;
        fill.bottom = rc.top  + rects[k].m_bottom;

        //超出本控件可视区的部分不画，免得糊到隔壁列上。
        if (fill.left >= rc.right)
        {
            continue;
        }
        if (fill.right > rc.right)
        {
            fill.right = rc.right;
        }
        if (fill.bottom > rc.bottom)
        {
            fill.bottom = rc.bottom;
        }
        ::FillRect(hdc, &fill, brush);
    }
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
