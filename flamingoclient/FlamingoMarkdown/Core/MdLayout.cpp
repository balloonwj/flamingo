/**
 *  Markdown 文档排版的实现，说明见 MdLayout.h。
 *
 *  行内排版（LayoutRuns）的做法：
 *    1. 把一组 TextRun 切成「原子」：连续的 ASCII 与拉丁字母组成一个单词；中日韩文字、全角符号、emoji 等每个
 *       字符单独成一个原子；连续空格成一个原子；强制换行单独成一个原子。
 *    2. 贪心填行：放得下就放；放不下的空格直接换行并丢弃；放不下的单词移到下一行；比整行还宽的单词（长网址、
 *       长数字）在字符之间断开。换行处行首、行尾的空格不显示（它们仍在渲染后文字里，只是不落在任何片段上）。
 *    3. 每行把同一 TextRun、同一字体、位置连续的原子合并成片段，用 GetTextExtentExPointW 一次测出片段宽度与
 *       每个字符的右边缘，作为绘制与命中测试的依据。行高取这一行用到的各字体中最大的上部高度与下部高度之和，
 *       再加行距；各片段按基线对齐。
 *
 *  所有间距都按正文字号的百分比换算，再受界面缩放影响的只有线宽与圆角，这样宿主调大聊天字号时整体比例不变。
 *
 *  balloonwj@qq.com   2026-10-06
 */
#include "MdLayout.h"

#include <algorithm>
#include <climits>

#include "MdAlert.h"
#include "MdCodeColor.h"
#include "MdFontCache.h"
#include "MdText.h"

namespace flamingomd {

namespace {

//百分比换算的分母
const int kPercent = 100;

//默认代码字体（宿主没有指定时）
const wchar_t* const kDefaultCodeFont = L"Consolas";

//正文字号的下限与上限（像素），防止宿主传入离谱的值
const int kMinFontPx = 8;
const int kMaxFontPx = 72;

//代码字号比正文小 1 像素：等宽字体在同样字号下显得偏大
const int kCodeFontShrinkPx = 1;

//各项间距占正文字号的百分比
const int kBlockGapPercent = 55;        //相邻块之间的间距
const int kLineGapPercent = 25;         //行距（行高超出字体高度的部分）
const int kItemGapPercent = 15;         //紧凑列表相邻项之间的间距
const int kListIndentPercent = 160;     //列表的缩进（项目符号所占的宽度）
const int kMarkerGapPercent = 40;       //有序列表编号与正文之间的空隙
const int kQuoteIndentPercent = 100;    //引用块的缩进
const int kCodePadHPercent = 70;        //代码块左右内边距
const int kCodePadVPercent = 55;        //代码块上下内边距
const int kCellPadHPercent = 55;        //表格单元格左右内边距
const int kCellPadVPercent = 30;        //表格单元格上下内边距
const int kHeadingRuleGapPercent = 30;  //一二级标题与其下方细线之间的间距
const int kRuleAreaPercent = 100;       //分隔线所占的高度（细线画在中间）
const int kCopyButtonPercent = 130;     //代码块「复制」按钮的边长
const int kBulletPercent = 35;          //实心圆点与空心圆圈的直径
const int kTaskBoxPercent = 85;         //任务框的边长
const int kMinDocWidthPercent = 200;    //文档的最小宽度
const int kMaxCellWordPercent = 800;    //计算表格最小列宽时，单个单词宽度的上限
const int kMinColumnPercent = 150;      //表格列宽的下限（不含内边距）
const int kAlertPadVPercent = 45;       //警告框上下内边距
const int kAlertPadHPercent = 70;       //警告框右内边距（左边与引用块一样按引用缩进）
const int kAlertIconPercent = 100;      //警告框图标的边长
const int kAlertIconGapPercent = 35;    //警告框图标与标题之间的空隙
const int kAlertTitleGapPercent = 20;   //警告框标题行与正文之间的间距

//各级标题字号占正文字号的百分比，下标为级别减 1
const int kHeadingPercents[] = { 150, 130, 115, 100, 100, 100 };
const int kHeadingLevelCount = 6;

//只出现下方细线的标题级别上限（一级、二级）
const int kRuledHeadingMaxLevel = 2;

//按界面缩放换算的尺寸（像素，100% 时的值）
const int kLineWidthPx = 1;        //细线宽度
const int kQuoteBarPx = 3;         //引用竖条宽度
const int kCornerRadiusPx = 4;     //圆角半径
const int kInlineCodePadPx = 3;    //行内代码底色比文字左右各宽出的距离
const int kCopyMarginPx = 4;       //「复制」按钮与代码块右上角的距离

//中日韩文字区间的起点：码点不小于它的字符（以及代理对）每个字符单独成一个原子
const wchar_t kCjkStart = 0x2E80;

//无序列表三层项目符号对应的装饰
const int kBulletKinds[] = { md_deco_bullet_disc, md_deco_bullet_circle, md_deco_bullet_square };
const int kBulletKindCount = 3;

//宽度不限时使用的「无穷大」宽度
const int kUnboundedWidth = INT_MAX / 4;

//按百分比换算
int PercentOf(int value, int percent)
{
    return value * percent / kPercent;
}

//按界面缩放换算，至少为 1
int Scaled(int px, int scalePercent)
{
    const int v = (px * scalePercent + kPercent / 2) / kPercent;
    return (v < 1) ? 1 : v;
}

//判断字符是否为空白（空格或 Tab）
bool IsSpaceChar(wchar_t ch)
{
    return ch == L' ' || ch == L'\t';
}

//判断字符是否单独成一个原子（中日韩文字、全角符号、emoji 等）
bool IsStandaloneChar(wchar_t ch)
{
    return ch >= kCjkStart;
}

/**
 *  行内排版的输入：一段样式一致的文字，或一处强制换行。文字指针指向模型里的字符串，排版期间有效。
 */
struct TextRun
{
    const wchar_t* m_text;         //文字起点
    int            m_length;       //字符数
    uint32_t       m_style;        //样式位
    int            m_linkIndex;    //链接序号；-1 表示不是链接
    int            m_flatBegin;    //第一个字符在渲染后文字中的下标
    bool           m_isBreak;      //是否为强制换行
    bool           m_mono;         //是否用代码字体
    bool           m_inCodeBlock;  //是否属于代码块
    const unsigned char* m_tokenKinds;  //代码块语法着色：与 m_text 逐字符对齐的着色种类（见 MdCodeColor.h），
                                        //指向排版期间有效的数组；NULL 表示不着色

    TextRun()
        : m_text(NULL), m_length(0), m_style(0), m_linkIndex(-1), m_flatBegin(0), m_isBreak(false),
          m_mono(false), m_inCodeBlock(false), m_tokenKinds(NULL)
    {
    }
};

/**
 *  行内排版的原子。
 */
struct Atom
{
    int  m_run;        //所属 TextRun 的下标
    int  m_begin;      //在 TextRun 中的起点
    int  m_count;      //字符数
    int  m_font;       //字体序号
    int  m_width;      //宽度（含内边距）
    int  m_padLeft;    //左内边距
    int  m_padRight;   //右内边距
    bool m_space;      //是否为空格
    bool m_break;      //是否为强制换行

    Atom()
        : m_run(0), m_begin(0), m_count(0), m_font(-1), m_width(0), m_padLeft(0), m_padRight(0),
          m_space(false), m_break(false)
    {
    }
};

/**
 *  排好的一行（尚未生成片段）。
 */
struct LinePlan
{
    std::vector<Atom> m_atoms;        //这一行的原子
    bool              m_endsWithBreak; //是否以强制换行结束
    int               m_flatStart;    //行首在渲染后文字中的下标（空行用）

    LinePlan()
        : m_endsWithBreak(false), m_flatStart(0)
    {
    }
};

/**
 *  行内排版的参数。
 */
struct InlineParams
{
    int  m_fontPx;          //字号（像素）
    bool m_bold;            //整段加粗（标题、表头）
    int  m_colorRole;       //非链接文字的颜色用途
    int  m_align;           //水平对齐，取值见 enum MdAlign
    int  m_codeIndex;       //属于代码块时为代码块序号，否则为 -1
    int  m_hangingIndent;   //第二行起的缩进（代码行折行时保持原缩进）
    int  m_emptyFlatPos;    //没有任何文字时，空行在渲染后文字中的位置
    bool m_monoDefault;     //空行取哪种字体的度量：true 为代码字体
    bool m_keepLeadingSpaces; //第一行行首的空格是否保留（代码行的缩进）；折行产生的续行一律不保留

    InlineParams()
        : m_fontPx(0), m_bold(false), m_colorRole(md_color_text), m_align(md_align_default), m_codeIndex(-1),
          m_hangingIndent(0), m_emptyFlatPos(0), m_monoDefault(false), m_keepLeadingSpaces(false)
    {
    }
};

/**
 *  块级排版的上下文。
 */
struct BlockContext
{
    int m_colorRole;   //非链接文字的颜色用途（引用块里为次要文字颜色）
    int m_listDepth;   //外层无序列表的层数（决定项目符号的样式）

    BlockContext()
        : m_colorRole(md_color_text), m_listDepth(0)
    {
    }
};

/**
 *  排版引擎：持有一次排版用到的全部状态。
 */
class LayoutEngine
{
public:
    LayoutEngine(const MdModel& model, HDC hdc, const MdResolvedStyle& style, MdLayoutResult& out)
        : m_model(model), m_hdc(hdc), m_style(style), m_out(out), m_currentFont(-1), m_oldFont(NULL)
    {
        const int px = style.m_fontPx;
        const int scale = style.m_scalePercent;
        m_blockGap = PercentOf(px, kBlockGapPercent);
        m_lineGap = PercentOf(px, kLineGapPercent);
        m_itemGap = PercentOf(px, kItemGapPercent);
        m_listIndent = PercentOf(px, kListIndentPercent);
        m_markerGap = PercentOf(px, kMarkerGapPercent);
        m_quoteIndent = PercentOf(px, kQuoteIndentPercent);
        m_codePadH = PercentOf(px, kCodePadHPercent);
        m_codePadV = PercentOf(px, kCodePadVPercent);
        m_cellPadH = PercentOf(px, kCellPadHPercent);
        m_cellPadV = PercentOf(px, kCellPadVPercent);
        m_headingRuleGap = PercentOf(px, kHeadingRuleGapPercent);
        m_ruleArea = PercentOf(px, kRuleAreaPercent);
        m_copyButton = PercentOf(px, kCopyButtonPercent);
        m_bulletSize = PercentOf(px, kBulletPercent);
        m_taskSize = PercentOf(px, kTaskBoxPercent);
        m_alertPadV = PercentOf(px, kAlertPadVPercent);
        m_alertPadH = PercentOf(px, kAlertPadHPercent);
        m_alertIcon = PercentOf(px, kAlertIconPercent);
        m_alertIconGap = PercentOf(px, kAlertIconGapPercent);
        m_alertTitleGap = PercentOf(px, kAlertTitleGapPercent);
        m_lineWidth = Scaled(kLineWidthPx, scale);
        m_quoteBar = Scaled(kQuoteBarPx, scale);
        m_radius = Scaled(kCornerRadiusPx, scale);
        m_inlineCodePad = Scaled(kInlineCodePadPx, scale);
        m_copyMargin = Scaled(kCopyMarginPx, scale);
    }

    ~LayoutEngine()
    {
        if (m_oldFont != NULL)
            ::SelectObject(m_hdc, m_oldFont);
    }

    //排版整个文档
    void Run(int maxWidth)
    {
        m_out.Clear();
        m_out.m_cornerRadius = m_radius;
        m_out.m_lineWidth = m_lineWidth;
        const size_t codeCount = m_model.m_codeBlocks.size();
        RECT empty = { 0, 0, 0, 0 };
        m_out.m_codeRects.assign(codeCount, empty);
        m_out.m_copyButtons.assign(codeCount, empty);

        const MdBlock& root = m_model.m_blocks[0];
        const int minWidth = PercentOf(m_style.m_fontPx, kMinDocWidthPercent);
        int natural = 0;
        for (size_t i = 0; i < root.m_children.size(); ++i)
            natural = (std::max)(natural, NaturalWidth(root.m_children[i], 0));
        //默认收缩到内容宽度（聊天气泡）；宿主要求撑满时直接取最大宽度（预览窗口、导出）
        int width = m_style.m_fillWidth ? maxWidth : (std::max)(natural, minWidth);
        width = (std::min)(width, (std::max)(maxWidth, 1));

        BlockContext ctx;
        const int height = LayoutChildren(root, 0, 0, width, ctx, m_blockGap);
        m_out.m_width = width;
        m_out.m_height = height;
    }

private:
    //把字体选进设备上下文（与当前相同时不重复选）
    void UseFont(int fontIndex)
    {
        if (fontIndex == m_currentFont || fontIndex < 0)
            return;
        HFONT font = MdFontCache::Instance().Font(fontIndex);
        if (font == NULL)
            return;
        HGDIOBJ old = ::SelectObject(m_hdc, font);
        if (m_oldFont == NULL)
            m_oldFont = old;
        m_currentFont = fontIndex;
    }

    //字体是否带下划线：链接，以及 <u>…</u> 里的文字
    static bool UnderlinedStyle(uint32_t style)
    {
        return (style & (md_style_link | md_style_underline)) != 0;
    }

    //取正文字体（按样式位与整段加粗）
    int BodyFont(int px, bool bold, uint32_t style)
    {
        MdFontKey key(m_style.m_fontName, px, bold || (style & md_style_bold) != 0, (style & md_style_italic) != 0,
                      UnderlinedStyle(style), (style & md_style_strike) != 0);
        return MdFontCache::Instance().Acquire(m_hdc, key);
    }

    //取代码字体（按样式位）
    int MonoFont(bool bold, uint32_t style)
    {
        MdFontKey key(m_style.m_codeFontName, m_style.m_codeFontPx, bold || (style & md_style_bold) != 0,
                      (style & md_style_italic) != 0, UnderlinedStyle(style), (style & md_style_strike) != 0);
        return MdFontCache::Instance().Acquire(m_hdc, key);
    }

    //代码字体缺字形时的替代字体：同字号的正文字体
    int MonoFallbackFont(bool bold, uint32_t style)
    {
        return BodyFont(m_style.m_codeFontPx, bold, style);
    }

    //测量一段文字的宽度（字体已选入）
    int MeasureText(const wchar_t* text, int count)
    {
        SIZE size = { 0, 0 };
        if (count > 0)
            ::GetTextExtentPoint32W(m_hdc, text, count, &size);
        return size.cx;
    }

    //把一组 TextRun 切成原子
    void BuildAtoms(const std::vector<TextRun>& runs, const InlineParams& params, std::vector<Atom>& atoms)
    {
        for (size_t r = 0; r < runs.size(); ++r)
        {
            const TextRun& run = runs[r];
            if (run.m_isBreak)
            {
                Atom a;
                a.m_run = static_cast<int>(r);
                a.m_break = true;
                atoms.push_back(a);
                continue;
            }

            const int bodyFont = run.m_mono ? -1 : BodyFont(params.m_fontPx, params.m_bold, run.m_style);
            const int monoFont = run.m_mono ? MonoFont(params.m_bold, run.m_style) : -1;
            const size_t firstAtom = atoms.size();

            int i = 0;
            while (i < run.m_length)
            {
                const wchar_t ch = run.m_text[i];
                Atom a;
                a.m_run = static_cast<int>(r);
                a.m_begin = i;
                if (IsSpaceChar(ch))
                {
                    int j = i + 1;
                    while (j < run.m_length && IsSpaceChar(run.m_text[j]))
                        ++j;
                    a.m_count = j - i;
                    a.m_space = true;
                }
                else if (IsHighSurrogate(ch) || IsLowSurrogate(ch) || IsStandaloneChar(ch))
                {
                    a.m_count = (IsHighSurrogate(ch) && i + 1 < run.m_length && IsLowSurrogate(run.m_text[i + 1])) ? 2 : 1;
                }
                else
                {
                    int j = i + 1;
                    while (j < run.m_length)
                    {
                        const wchar_t c = run.m_text[j];
                        if (IsSpaceChar(c) || IsHighSurrogate(c) || IsLowSurrogate(c) || IsStandaloneChar(c))
                            break;
                        ++j;
                    }
                    a.m_count = j - i;
                }

                //选字体：代码字体缺这个字的字形时改用同字号的正文字体
                int font = bodyFont;
                if (run.m_mono)
                {
                    font = monoFont;
                    if (a.m_count == 1 && !a.m_space)
                    {
                        UseFont(monoFont);
                        if (!MdFontCache::Instance().HasGlyph(m_hdc, monoFont, ch))
                            font = MonoFallbackFont(params.m_bold, run.m_style);
                    }
                    else if (a.m_count == 2 && IsHighSurrogate(ch))
                    {
                        font = MonoFallbackFont(params.m_bold, run.m_style);
                    }
                }
                a.m_font = font;
                UseFont(font);
                if (a.m_count <= 2 && !a.m_space)
                    a.m_width = MdFontCache::Instance().CharWidth(m_hdc, font, run.m_text + i, a.m_count);
                else
                    a.m_width = MeasureText(run.m_text + i, a.m_count);
                atoms.push_back(a);
                i += a.m_count;
            }

            //行内代码：底色比文字左右各宽一点，宽度计入首尾两个原子
            if ((run.m_style & md_style_code) != 0 && !run.m_inCodeBlock && atoms.size() > firstAtom)
            {
                atoms[firstAtom].m_padLeft = m_inlineCodePad;
                atoms[firstAtom].m_width += m_inlineCodePad;
                atoms.back().m_padRight = m_inlineCodePad;
                atoms.back().m_width += m_inlineCodePad;
            }
        }
    }

    //在 avail 宽度内能放下原子的前几个字符；保证不把代理对拆开
    int FitChars(const TextRun& run, const Atom& atom, int avail)
    {
        UseFont(atom.m_font);
        const int textAvail = avail - atom.m_padLeft - atom.m_padRight;
        if (textAvail <= 0)
            return 0;
        int fit = 0;
        SIZE size = { 0, 0 };
        ::GetTextExtentExPointW(m_hdc, run.m_text + atom.m_begin, atom.m_count, textAvail, &fit, NULL, &size);
        if (fit > 0 && fit < atom.m_count && IsHighSurrogate(run.m_text[atom.m_begin + fit - 1]))
            --fit;
        return fit;
    }

    //从原子里切出前 count 个字符作为一块，并重新测量两块的宽度
    void SplitAtom(const TextRun& run, Atom& atom, int count, Atom& head)
    {
        head = atom;
        head.m_count = count;
        head.m_padRight = 0;
        UseFont(atom.m_font);
        head.m_width = MeasureText(run.m_text + atom.m_begin, count) + head.m_padLeft;

        atom.m_begin += count;
        atom.m_count -= count;
        atom.m_padLeft = 0;
        atom.m_width = MeasureText(run.m_text + atom.m_begin, atom.m_count) + atom.m_padRight;
    }

    //去掉行尾的空格原子
    static void TrimTrailingSpaces(LinePlan& line)
    {
        while (!line.m_atoms.empty() && line.m_atoms.back().m_space)
            line.m_atoms.pop_back();
    }

    //第 lineIndex 行可用的宽度（第二行起扣掉悬挂缩进）
    static int LineWidthFor(size_t lineIndex, int width, const InlineParams& params)
    {
        if (lineIndex == 0)
            return width;
        const int w = width - params.m_hangingIndent;
        return (w < 1) ? 1 : w;
    }

    //贪心填行
    void FillLines(const std::vector<TextRun>& runs, std::vector<Atom>& atoms, int width,
                   const InlineParams& params, std::vector<LinePlan>& lines)
    {
        LinePlan cur;
        cur.m_flatStart = runs.empty() ? params.m_emptyFlatPos : runs[0].m_flatBegin;
        int curW = 0;

        for (size_t k = 0; k < atoms.size(); ++k)
        {
            Atom atom = atoms[k];
            const TextRun& run = runs[atom.m_run];
            int lineW = LineWidthFor(lines.size(), width, params);

            if (atom.m_break)
            {
                TrimTrailingSpaces(cur);
                cur.m_endsWithBreak = true;
                lines.push_back(cur);
                cur = LinePlan();
                cur.m_flatStart = run.m_flatBegin + 1;
                curW = 0;
                continue;
            }

            if (atom.m_space)
            {
                //行首的空格不显示；代码行第一行的缩进例外
                const bool keep = params.m_keepLeadingSpaces && lines.empty();
                if (cur.m_atoms.empty() && !keep)
                    continue;
                if (curW + atom.m_width <= lineW)
                {
                    cur.m_atoms.push_back(atom);
                    curW += atom.m_width;
                }
                else
                {
                    TrimTrailingSpaces(cur);
                    lines.push_back(cur);
                    cur = LinePlan();
                    cur.m_flatStart = run.m_flatBegin + atom.m_begin + atom.m_count;
                    curW = 0;
                }
                continue;
            }

            if (curW + atom.m_width <= lineW)
            {
                cur.m_atoms.push_back(atom);
                curW += atom.m_width;
                continue;
            }

            if (atom.m_width <= LineWidthFor(lines.size() + 1, width, params) && !cur.m_atoms.empty())
            {
                //放到下一行
                TrimTrailingSpaces(cur);
                lines.push_back(cur);
                cur = LinePlan();
                cur.m_flatStart = run.m_flatBegin + atom.m_begin;
                cur.m_atoms.push_back(atom);
                curW = atom.m_width;
                continue;
            }

            //比整行还宽：在字符之间断开，先填满当前行的剩余空间
            while (atom.m_count > 0)
            {
                lineW = LineWidthFor(lines.size(), width, params);
                if (curW + atom.m_width <= lineW)
                {
                    cur.m_atoms.push_back(atom);
                    curW += atom.m_width;
                    break;
                }
                int fit = FitChars(run, atom, lineW - curW);
                if (fit <= 0)
                {
                    if (!cur.m_atoms.empty())
                    {
                        TrimTrailingSpaces(cur);
                        lines.push_back(cur);
                        cur = LinePlan();
                        cur.m_flatStart = run.m_flatBegin + atom.m_begin;
                        curW = 0;
                        continue;
                    }
                    //空行也放不下一个字符：至少放一个字符（代理对放两个）
                    fit = (IsHighSurrogate(run.m_text[atom.m_begin]) && atom.m_count > 1) ? 2 : 1;
                }
                if (fit >= atom.m_count)
                {
                    cur.m_atoms.push_back(atom);
                    curW += atom.m_width;
                    break;
                }
                Atom head;
                SplitAtom(run, atom, fit, head);
                cur.m_atoms.push_back(head);
                lines.push_back(cur);
                cur = LinePlan();
                cur.m_flatStart = run.m_flatBegin + atom.m_begin;
                curW = 0;
            }
        }

        TrimTrailingSpaces(cur);
        if (!cur.m_atoms.empty() || lines.empty() || lines.back().m_endsWithBreak)
            lines.push_back(cur);
    }

    //默认字体（空行取它的度量）
    int DefaultFont(const InlineParams& params)
    {
        return params.m_monoDefault ? MonoFont(params.m_bold, 0) : BodyFont(params.m_fontPx, params.m_bold, 0);
    }

    /**
     *  把一个已测量好的片段放进结果。kinds 不为 NULL 时（代码块语法着色）按着色种类的变化把片段切成几段，
     *  各段的横坐标与宽度取自原片段的 m_caret，因此折行、行高与行的范围都与不着色时相同。
     *  @param frag  已测量好的片段
     *  @param kinds 与 frag.m_text 逐字符对齐的着色种类；NULL 表示不着色、原样放入
     */
    void EmitFragment(const MdFragment& frag, const unsigned char* kinds)
    {
        const int count = static_cast<int>(frag.m_text.size());
        if (kinds == NULL || count == 0)
        {
            m_out.m_fragments.push_back(frag);
            return;
        }
        int a = 0;
        while (a < count)
        {
            const int b = static_cast<int>(MdCodeColorRunEnd(frag.m_text.c_str(), static_cast<size_t>(count), kinds,
                                                             static_cast<size_t>(a)));
            //base：本段左边相对原片段 m_x 的偏移，即前一个字符的右边缘（第一段为 0，左内边距已含在 m_caret 里）
            const int base = (a == 0) ? 0 : frag.m_caret[a - 1];
            MdFragment piece(frag);
            piece.m_text = frag.m_text.substr(static_cast<size_t>(a), static_cast<size_t>(b - a));
            piece.m_tokenKind = kinds[a];
            piece.m_x = frag.m_x + base;
            piece.m_flatBegin = (frag.m_flatBegin < 0) ? -1 : frag.m_flatBegin + a;
            piece.m_padLeft = (a == 0) ? frag.m_padLeft : 0;
            piece.m_padRight = (b == count) ? frag.m_padRight : 0;
            piece.m_caret.assign(frag.m_caret.begin() + a, frag.m_caret.begin() + b);
            for (size_t c = 0; c < piece.m_caret.size(); ++c)
                piece.m_caret[c] -= base;
            piece.m_width = frag.m_caret[b - 1] - base + piece.m_padRight;
            m_out.m_fragments.push_back(piece);
            a = b;
        }
    }

    /**
     *  行内排版。emit 为 false 时只测量：返回值无意义，naturalWidth 写入不折行时最宽一行的宽度，
     *  minWidth 写入最宽的单词宽度；emit 为 true 时按 width 折行并输出片段与行，返回总高度。
     */
    int LayoutRuns(const std::vector<TextRun>& runs, const InlineParams& params, int x, int y, int width,
                   bool emit, int* naturalWidth, int* minWidth)
    {
        std::vector<Atom> atoms;
        BuildAtoms(runs, params, atoms);

        if (!emit)
        {
            int natural = 0;
            int widest = 0;
            int lineW = 0;
            for (size_t k = 0; k < atoms.size(); ++k)
            {
                if (atoms[k].m_break)
                {
                    natural = (std::max)(natural, lineW);
                    lineW = 0;
                    continue;
                }
                lineW += atoms[k].m_width;
                if (!atoms[k].m_space)
                    widest = (std::max)(widest, atoms[k].m_width);
            }
            natural = (std::max)(natural, lineW);
            if (naturalWidth != NULL)
                *naturalWidth = natural;
            if (minWidth != NULL)
                *minWidth = widest;
            return 0;
        }

        if (width < 1)
            width = 1;
        std::vector<LinePlan> lines;
        FillLines(runs, atoms, width, params, lines);

        const MdFontMetrics defaultMetrics = MdFontCache::Instance().Metrics(DefaultFont(params));
        int cursor = y;
        for (size_t li = 0; li < lines.size(); ++li)
        {
            const LinePlan& plan = lines[li];
            int ascent = 0;
            int descent = 0;
            for (size_t k = 0; k < plan.m_atoms.size(); ++k)
            {
                const MdFontMetrics m = MdFontCache::Instance().Metrics(plan.m_atoms[k].m_font);
                ascent = (std::max)(ascent, m.m_ascent);
                descent = (std::max)(descent, m.m_descent);
            }
            if (plan.m_atoms.empty())
            {
                ascent = defaultMetrics.m_ascent;
                descent = defaultMetrics.m_descent;
            }
            const int lineHeight = ascent + descent + m_lineGap;
            const int baseline = cursor + m_lineGap / 2 + ascent;
            const int lineX = x + ((li == 0) ? 0 : params.m_hangingIndent);
            const int lineAvail = LineWidthFor(li, width, params);

            MdLine line;
            line.m_top = cursor;
            line.m_bottom = cursor + lineHeight;
            line.m_left = x;
            line.m_right = x + width;
            line.m_firstFragment = static_cast<int>(m_out.m_fragments.size());
            line.m_codeIndex = params.m_codeIndex;
            line.m_flatBegin = plan.m_flatStart;
            line.m_flatEnd = plan.m_flatStart;

            //合并原子成片段并测量
            int penX = lineX;
            size_t k = 0;
            while (k < plan.m_atoms.size())
            {
                const Atom& first = plan.m_atoms[k];
                size_t end = k + 1;
                while (end < plan.m_atoms.size())
                {
                    const Atom& prev = plan.m_atoms[end - 1];
                    const Atom& next = plan.m_atoms[end];
                    if (next.m_run != first.m_run || next.m_font != first.m_font
                        || next.m_begin != prev.m_begin + prev.m_count)
                        break;
                    ++end;
                }
                const TextRun& run = runs[first.m_run];
                const Atom& last = plan.m_atoms[end - 1];
                const int begin = first.m_begin;
                const int count = last.m_begin + last.m_count - begin;

                MdFragment frag;
                frag.m_text.assign(run.m_text + begin, static_cast<size_t>(count));
                frag.m_fontIndex = first.m_font;
                frag.m_style = run.m_style;
                frag.m_linkIndex = run.m_linkIndex;
                frag.m_colorRole = (run.m_linkIndex >= 0) ? md_color_link : params.m_colorRole;
                frag.m_flatBegin = run.m_flatBegin + begin;
                frag.m_inCodeBlock = run.m_inCodeBlock;
                frag.m_padLeft = first.m_padLeft;
                frag.m_padRight = last.m_padRight;
                frag.m_caret.resize(static_cast<size_t>(count));
                UseFont(first.m_font);
                SIZE size = { 0, 0 };
                ::GetTextExtentExPointW(m_hdc, frag.m_text.c_str(), count, 0, NULL, &frag.m_caret[0], &size);
                for (int c = 0; c < count; ++c)
                    frag.m_caret[c] += frag.m_padLeft;
                frag.m_x = penX;
                frag.m_width = size.cx + frag.m_padLeft + frag.m_padRight;
                frag.m_top = cursor;
                frag.m_height = lineHeight;
                frag.m_baseline = baseline;
                penX += frag.m_width;
                line.m_flatEnd = frag.FlatEnd();
                EmitFragment(frag, (run.m_tokenKinds != NULL) ? run.m_tokenKinds + begin : NULL);
                k = end;
            }

            //对齐：整行平移
            const int used = penX - lineX;
            int shift = 0;
            if (params.m_align == md_align_right)
                shift = lineAvail - used;
            else if (params.m_align == md_align_center)
                shift = (lineAvail - used) / 2;
            if (shift > 0)
            {
                for (size_t f = static_cast<size_t>(line.m_firstFragment); f < m_out.m_fragments.size(); ++f)
                    m_out.m_fragments[f].m_x += shift;
            }

            line.m_fragmentCount = static_cast<int>(m_out.m_fragments.size()) - line.m_firstFragment;
            m_out.m_lines.push_back(line);
            cursor += lineHeight;
        }
        return cursor - y;
    }

    //把文字块的行内元素转成 TextRun
    static void RunsFromInlines(const MdBlock& block, std::vector<TextRun>& runs)
    {
        runs.clear();
        runs.reserve(block.m_inlines.size());
        for (size_t i = 0; i < block.m_inlines.size(); ++i)
        {
            const MdInline& in = block.m_inlines[i];
            TextRun run;
            run.m_text = in.m_text.c_str();
            run.m_length = static_cast<int>(in.m_text.size());
            run.m_style = in.m_style;
            run.m_linkIndex = in.m_linkIndex;
            run.m_flatBegin = in.m_flatBegin;
            run.m_isBreak = in.m_isBreak;
            run.m_mono = (in.m_style & md_style_code) != 0;
            runs.push_back(run);
        }
    }

    //标题字号
    int HeadingPx(int level) const
    {
        int idx = level - 1;
        if (idx < 0)
            idx = 0;
        if (idx >= kHeadingLevelCount)
            idx = kHeadingLevelCount - 1;
        return PercentOf(m_style.m_fontPx, kHeadingPercents[idx]);
    }

    //正文的行高
    int BodyLineHeight()
    {
        const MdFontMetrics m = MdFontCache::Instance().Metrics(BodyFont(m_style.m_fontPx, false, 0));
        return m.m_height + m_lineGap;
    }

    //代码块按 \n 拆行：每行的起点与长度
    static void SplitCodeLines(const std::wstring& code, std::vector<int>& starts, std::vector<int>& lengths)
    {
        starts.clear();
        lengths.clear();
        size_t begin = 0;
        for (;;)
        {
            size_t nl = code.find(L'\n', begin);
            const size_t end = (nl == std::wstring::npos) ? code.size() : nl;
            starts.push_back(static_cast<int>(begin));
            lengths.push_back(static_cast<int>(end - begin));
            if (nl == std::wstring::npos)
                break;
            begin = nl + 1;
        }
    }

    //代码行的悬挂缩进：原缩进再加两列，不超过可用宽度的一半
    int CodeHangingIndent(const wchar_t* text, int length, int width)
    {
        const int kExtraColumns = 2;
        int spaces = 0;
        while (spaces < length && text[spaces] == L' ')
            ++spaces;
        const int monoFont = MonoFont(false, 0);
        UseFont(monoFont);
        const wchar_t kSpace = L' ';
        const int spaceW = MdFontCache::Instance().CharWidth(m_hdc, monoFont, &kSpace, 1);
        const int indent = (spaces + kExtraColumns) * spaceW;
        return (std::min)(indent, width / 2);
    }

    //有序列表编号的文字
    static std::wstring OrderedMarker(unsigned number)
    {
        wchar_t buf[16] = { 0 };
        _snwprintf_s(buf, _countof(buf), _TRUNCATE, L"%u.", number);
        return std::wstring(buf);
    }

    //列表项目符号所占的宽度
    int ListMarkerWidth(const MdBlock& list)
    {
        if (!list.m_ordered)
            return m_listIndent;
        const unsigned last = list.m_startNumber + static_cast<unsigned>(list.m_children.size()) - 1;
        const std::wstring marker = OrderedMarker(last);
        const int font = BodyFont(m_style.m_fontPx, false, 0);
        UseFont(font);
        const int w = MeasureText(marker.c_str(), static_cast<int>(marker.size())) + m_markerGap;
        return (std::max)(m_listIndent, w);
    }

    //表格的列宽：先算每列的自然宽度与最小宽度，再按可用宽度分配
    void TableColumnWidths(const MdBlock& table, int avail, std::vector<int>& widths, bool unbounded)
    {
        const int cols = table.m_columnCount;
        std::vector<int> natural(static_cast<size_t>(cols), 0);
        std::vector<int> minimum(static_cast<size_t>(cols), 0);
        const int padding = m_cellPadH * 2;
        const int wordCap = PercentOf(m_style.m_fontPx, kMaxCellWordPercent);
        const int colFloor = PercentOf(m_style.m_fontPx, kMinColumnPercent) + padding;

        std::vector<TextRun> runs;
        for (size_t r = 0; r < table.m_children.size(); ++r)
        {
            const MdBlock& row = m_model.m_blocks[table.m_children[r]];
            for (size_t c = 0; c < row.m_children.size() && static_cast<int>(c) < cols; ++c)
            {
                const MdBlock& cell = m_model.m_blocks[row.m_children[c]];
                RunsFromInlines(cell, runs);
                InlineParams params;
                params.m_fontPx = m_style.m_fontPx;
                params.m_bold = cell.m_headerRow;
                int nat = 0;
                int minW = 0;
                LayoutRuns(runs, params, 0, 0, kUnboundedWidth, false, &nat, &minW);
                natural[c] = (std::max)(natural[c], nat + padding);
                minimum[c] = (std::max)(minimum[c], (std::min)(minW, wordCap) + padding);
            }
        }

        widths.assign(static_cast<size_t>(cols), 0);
        long long sumNat = 0;
        long long sumMin = 0;
        for (int c = 0; c < cols; ++c)
        {
            natural[c] = (std::max)(natural[c], colFloor);
            minimum[c] = (std::max)((std::min)(minimum[c], natural[c]), colFloor);
            sumNat += natural[c];
            sumMin += minimum[c];
        }

        if (unbounded || sumNat <= avail)
        {
            widths = natural;
            return;
        }
        if (sumMin >= avail)
        {
            //最小宽度都放不下：按最小宽度的比例压缩，每列不低于内边距加一个字的宽度
            const int hardFloor = padding + m_style.m_fontPx;
            for (int c = 0; c < cols; ++c)
                widths[c] = (std::max)(hardFloor, static_cast<int>(minimum[c] * static_cast<long long>(avail) / sumMin));
            return;
        }
        //在最小宽度的基础上，把剩余空间按「自然宽度 - 最小宽度」的比例分给各列
        const long long extra = avail - sumMin;
        const long long range = sumNat - sumMin;
        for (int c = 0; c < cols; ++c)
        {
            const long long grow = (range > 0) ? (natural[c] - minimum[c]) * extra / range : 0;
            widths[c] = minimum[c] + static_cast<int>(grow);
        }
    }

    //块的自然宽度（不折行时需要的宽度）
    int NaturalWidth(int index, int listDepth)
    {
        const MdBlock& block = m_model.m_blocks[index];
        std::vector<TextRun> runs;
        switch (block.m_kind)
        {
        //段落与标题：最宽一行
        case md_block_paragraph:
        case md_block_heading:
        {
            RunsFromInlines(block, runs);
            InlineParams params;
            params.m_fontPx = (block.m_kind == md_block_heading) ? HeadingPx(block.m_headingLevel) : m_style.m_fontPx;
            params.m_bold = (block.m_kind == md_block_heading);
            int nat = 0;
            LayoutRuns(runs, params, 0, 0, kUnboundedWidth, false, &nat, NULL);
            return nat;
        }

        //代码块：最宽的代码行加左右内边距
        case md_block_code:
        {
            std::vector<int> starts;
            std::vector<int> lengths;
            SplitCodeLines(block.m_code, starts, lengths);
            int widest = 0;
            for (size_t i = 0; i < starts.size(); ++i)
            {
                TextRun run;
                run.m_text = block.m_code.c_str() + starts[i];
                run.m_length = lengths[i];
                run.m_mono = true;
                run.m_inCodeBlock = true;
                runs.assign(1, run);
                InlineParams params;
                params.m_fontPx = m_style.m_codeFontPx;
                params.m_monoDefault = true;
                int nat = 0;
                LayoutRuns(runs, params, 0, 0, kUnboundedWidth, false, &nat, NULL);
                widest = (std::max)(widest, nat);
            }
            return widest + m_codePadH * 2;
        }

        //引用块：缩进加最宽的子块；警告框还要放得下标题行，并加上右内边距
        case md_block_quote:
        {
            int widest = 0;
            for (size_t i = 0; i < block.m_children.size(); ++i)
                widest = (std::max)(widest, NaturalWidth(block.m_children[i], listDepth));
            if (block.m_alertType != md_alert_none)
                return (std::max)(widest, AlertTitleWidth(block.m_alertType)) + m_quoteIndent + m_alertPadH;
            return widest + m_quoteIndent;
        }

        //列表：项目符号宽度加最宽的列表项内容
        case md_block_list:
        {
            int widest = 0;
            for (size_t i = 0; i < block.m_children.size(); ++i)
            {
                const MdBlock& item = m_model.m_blocks[block.m_children[i]];
                for (size_t j = 0; j < item.m_children.size(); ++j)
                    widest = (std::max)(widest, NaturalWidth(item.m_children[j], listDepth + 1));
            }
            return widest + ListMarkerWidth(block);
        }

        //表格：各列自然宽度之和加表格线
        case md_block_table:
        {
            std::vector<int> widths;
            TableColumnWidths(block, kUnboundedWidth, widths, true);
            int sum = 0;
            for (size_t c = 0; c < widths.size(); ++c)
                sum += widths[c];
            return sum + (block.m_columnCount + 1) * m_lineWidth;
        }

        //分隔线与其它块：不要求宽度
        default:
            return 0;
        }
    }

    //依次排版子块，相邻子块之间留 gap，返回总高度
    int LayoutChildren(const MdBlock& parent, int x, int y, int width, const BlockContext& ctx, int gap)
    {
        int cursor = y;
        for (size_t i = 0; i < parent.m_children.size(); ++i)
        {
            if (i > 0)
                cursor += gap;
            cursor += LayoutBlock(parent.m_children[i], x, cursor, width, ctx);
        }
        return cursor - y;
    }

    //生成一个装饰；alertType 只对警告框的三种装饰有意义
    static MdDecoration MakeDecoration(int kind, int left, int top, int right, int bottom, int alertType)
    {
        MdDecoration deco;
        deco.m_kind = kind;
        deco.m_rect.left = left;
        deco.m_rect.top = top;
        deco.m_rect.right = right;
        deco.m_rect.bottom = bottom;
        deco.m_alertType = alertType;
        return deco;
    }

    //加一个装饰（排在已有装饰之后）
    void AddDecoration(int kind, int left, int top, int right, int bottom, int alertType = md_alert_none)
    {
        m_out.m_decorations.push_back(MakeDecoration(kind, left, top, right, bottom, alertType));
    }

    //警告框标题行的宽度：图标、空隙与标题文字
    int AlertTitleWidth(int type)
    {
        const std::wstring title = MdAlertTitle(type);
        const int font = BodyFont(m_style.m_fontPx, true, 0);
        UseFont(font);
        return m_alertIcon + m_alertIconGap + MeasureText(title.c_str(), static_cast<int>(title.size()));
    }

    /**
     *  排版警告框：整块浅色底、左侧彩色竖条，第一行是图标加标题（标题不可选中、不在渲染后文字里），
     *  其下按引用缩进排正文；与普通引用块不同，正文用正文颜色。返回高度。
     */
    int LayoutAlert(const MdBlock& block, int x, int y, int width, const BlockContext& ctx)
    {
        const int type = block.m_alertType;
        const size_t backIndex = m_out.m_decorations.size();
        const int contentX = x + m_quoteIndent;
        const int contentW = (std::max)(width - m_quoteIndent - m_alertPadH, 1);
        int cursor = y + m_alertPadV;

        //标题行：行高取加粗正文字体，图标与标题竖直居中
        const int titleFont = BodyFont(m_style.m_fontPx, true, 0);
        const MdFontMetrics tm = MdFontCache::Instance().Metrics(titleFont);
        const int titleH = tm.m_ascent + tm.m_descent + m_lineGap;
        const int iconTop = cursor + (titleH - m_alertIcon) / 2;
        AddDecoration(md_deco_alert_icon, contentX, iconTop, contentX + m_alertIcon, iconTop + m_alertIcon, type);

        MdFragment title;
        title.m_text = MdAlertTitle(type);
        title.m_fontIndex = titleFont;
        title.m_style = md_style_bold;
        title.m_colorRole = md_color_alert_first + type - 1;
        title.m_flatBegin = -1;
        UseFont(titleFont);
        title.m_width = MeasureText(title.m_text.c_str(), static_cast<int>(title.m_text.size()));
        title.m_x = contentX + m_alertIcon + m_alertIconGap;
        title.m_top = cursor;
        title.m_height = titleH;
        title.m_baseline = cursor + m_lineGap / 2 + tm.m_ascent;
        m_out.m_fragments.push_back(title);
        cursor += titleH;

        if (!block.m_children.empty())
        {
            BlockContext child = ctx;
            child.m_colorRole = md_color_text;
            cursor += m_alertTitleGap;
            cursor += LayoutChildren(block, contentX, cursor, contentW, child, m_blockGap);
        }
        cursor += m_alertPadV;

        //底色插到框内全部装饰之前（绘制时最先画，不会盖住框里的代码块底色等），竖条排在最后
        m_out.m_decorations.insert(m_out.m_decorations.begin() + static_cast<std::ptrdiff_t>(backIndex),
                                   MakeDecoration(md_deco_alert_back, x, y, x + width, cursor, type));
        AddDecoration(md_deco_alert_bar, x, y, x + m_quoteBar, cursor, type);
        return cursor - y;
    }

    //排版一个块，返回高度
    int LayoutBlock(int index, int x, int y, int width, const BlockContext& ctx)
    {
        const MdBlock& block = m_model.m_blocks[index];
        std::vector<TextRun> runs;
        switch (block.m_kind)
        {
        //段落
        case md_block_paragraph:
        {
            RunsFromInlines(block, runs);
            InlineParams params;
            params.m_fontPx = m_style.m_fontPx;
            params.m_colorRole = ctx.m_colorRole;
            return LayoutRuns(runs, params, x, y, width, true, NULL, NULL);
        }

        //标题：放大加粗，一二级标题下方加一条细线
        case md_block_heading:
        {
            RunsFromInlines(block, runs);
            InlineParams params;
            params.m_fontPx = HeadingPx(block.m_headingLevel);
            params.m_bold = true;
            params.m_colorRole = ctx.m_colorRole;
            int h = LayoutRuns(runs, params, x, y, width, true, NULL, NULL);
            if (block.m_headingLevel <= kRuledHeadingMaxLevel)
            {
                const int lineTop = y + h + m_headingRuleGap;
                AddDecoration(md_deco_line, x, lineTop, x + width, lineTop + m_lineWidth);
                h += m_headingRuleGap + m_lineWidth;
            }
            return h;
        }

        //引用块：左侧竖条，子块缩进并用次要文字颜色；警告框另行排版
        case md_block_quote:
        {
            if (block.m_alertType != md_alert_none)
                return LayoutAlert(block, x, y, width, ctx);
            BlockContext child = ctx;
            child.m_colorRole = md_color_muted;
            const int h = LayoutChildren(block, x + m_quoteIndent, y, (std::max)(width - m_quoteIndent, 1),
                                         child, m_blockGap);
            AddDecoration(md_deco_line, x, y, x + m_quoteBar, y + h);
            return h;
        }

        //列表
        case md_block_list:
            return LayoutList(block, x, y, width, ctx);

        //代码块
        case md_block_code:
            return LayoutCode(block, x, y, width);

        //分隔线：细线画在所占高度的中间
        case md_block_rule:
        {
            const int lineTop = y + (m_ruleArea - m_lineWidth) / 2;
            AddDecoration(md_deco_line, x, lineTop, x + width, lineTop + m_lineWidth);
            return m_ruleArea;
        }

        //表格
        case md_block_table:
            return LayoutTable(block, x, y, width);

        //其它块（不会单独出现的列表项、行、单元格）：按子块排版
        default:
            return LayoutChildren(block, x, y, width, ctx, m_blockGap);
        }
    }

    //排版列表
    int LayoutList(const MdBlock& list, int x, int y, int width, const BlockContext& ctx)
    {
        const int markerW = ListMarkerWidth(list);
        const int contentX = x + markerW;
        const int contentW = (std::max)(width - markerW, 1);
        const int itemGap = list.m_tight ? m_itemGap : m_blockGap;
        const int bodyFont = BodyFont(m_style.m_fontPx, false, 0);
        const MdFontMetrics bodyMetrics = MdFontCache::Instance().Metrics(bodyFont);
        const int bodyLine = bodyMetrics.m_height + m_lineGap;

        BlockContext child = ctx;
        if (!list.m_ordered)
            child.m_listDepth = ctx.m_listDepth + 1;

        int cursor = y;
        for (size_t i = 0; i < list.m_children.size(); ++i)
        {
            if (i > 0)
                cursor += itemGap;
            const MdBlock& item = m_model.m_blocks[list.m_children[i]];
            const size_t linesBefore = m_out.m_lines.size();
            int h = LayoutChildren(item, contentX, cursor, contentW, child, itemGap);
            if (h < bodyLine)
                h = bodyLine;

            //项目符号对齐到列表项的第一行
            int lineTop = cursor;
            int lineBottom = cursor + bodyLine;
            int baseline = cursor + m_lineGap / 2 + bodyMetrics.m_ascent;
            if (m_out.m_lines.size() > linesBefore)
            {
                const MdLine& first = m_out.m_lines[linesBefore];
                lineTop = first.m_top;
                lineBottom = first.m_bottom;
                if (first.m_fragmentCount > 0)
                    baseline = m_out.m_fragments[first.m_firstFragment].m_baseline;
            }
            const int centerY = (lineTop + lineBottom) / 2;
            const int markerRight = x + markerW - m_markerGap;

            if (item.m_taskState != md_task_none)
            {
                const int left = x + (markerW - m_markerGap - m_taskSize) / 2;
                const int top = centerY - m_taskSize / 2;
                AddDecoration(item.m_taskState == md_task_checked ? md_deco_task_done : md_deco_task_open,
                              left, top, left + m_taskSize, top + m_taskSize);
            }
            else if (!list.m_ordered)
            {
                int depth = ctx.m_listDepth;
                if (depth >= kBulletKindCount)
                    depth = kBulletKindCount - 1;
                const int cx = x + (markerW - m_markerGap) / 2;
                const int half = m_bulletSize / 2;
                AddDecoration(kBulletKinds[depth], cx - half, centerY - half, cx - half + m_bulletSize,
                              centerY - half + m_bulletSize);
            }
            else
            {
                MdFragment frag;
                frag.m_text = OrderedMarker(list.m_startNumber + static_cast<unsigned>(i));
                frag.m_fontIndex = bodyFont;
                frag.m_colorRole = md_color_muted;
                frag.m_flatBegin = -1;
                UseFont(bodyFont);
                frag.m_width = MeasureText(frag.m_text.c_str(), static_cast<int>(frag.m_text.size()));
                frag.m_x = markerRight - frag.m_width;
                frag.m_top = lineTop;
                frag.m_height = lineBottom - lineTop;
                frag.m_baseline = baseline;
                m_out.m_fragments.push_back(frag);
            }
            cursor += h;
        }
        return cursor - y;
    }

    //排版代码块
    int LayoutCode(const MdBlock& block, int x, int y, int width)
    {
        const int innerX = x + m_codePadH;
        const int innerW = (std::max)(width - m_codePadH * 2, 1);
        std::vector<int> starts;
        std::vector<int> lengths;
        SplitCodeLines(block.m_code, starts, lengths);

        //语法着色：写了认得的语言名时，算出显示文字（制表符已展开）里每个字符的着色种类
        std::vector<unsigned char> kinds;
        const bool colored = ComputeMdCodeTokenKinds(block.m_codeLang, block.m_code, kinds);

        int cursor = y + m_codePadV;
        std::vector<TextRun> runs;
        for (size_t i = 0; i < starts.size(); ++i)
        {
            const wchar_t* text = block.m_code.c_str() + starts[i];
            runs.clear();
            if (lengths[i] > 0)
            {
                TextRun run;
                run.m_text = text;
                run.m_length = lengths[i];
                run.m_flatBegin = block.m_codeFlatBegin + starts[i];
                run.m_mono = true;
                run.m_inCodeBlock = true;
                run.m_tokenKinds = colored ? &kinds[static_cast<size_t>(starts[i])] : NULL;
                runs.push_back(run);
            }
            InlineParams params;
            params.m_fontPx = m_style.m_codeFontPx;
            params.m_codeIndex = block.m_codeIndex;
            params.m_hangingIndent = CodeHangingIndent(text, lengths[i], innerW);
            params.m_emptyFlatPos = block.m_codeFlatBegin + starts[i];
            params.m_monoDefault = true;
            params.m_keepLeadingSpaces = true;
            cursor += LayoutRuns(runs, params, innerX, cursor, innerW, true, NULL, NULL);
        }
        cursor += m_codePadV;

        AddDecoration(md_deco_code_block, x, y, x + width, cursor);
        if (block.m_codeIndex >= 0 && static_cast<size_t>(block.m_codeIndex) < m_out.m_codeRects.size())
        {
            RECT bg = { x, y, x + width, cursor };
            m_out.m_codeRects[block.m_codeIndex] = bg;
            const int right = x + width - m_copyMargin;
            const int top = y + m_copyMargin;
            RECT btn = { right - m_copyButton, top, right, top + m_copyButton };
            m_out.m_copyButtons[block.m_codeIndex] = btn;
        }
        return cursor - y;
    }

    //排版表格
    int LayoutTable(const MdBlock& table, int x, int y, int width)
    {
        const int cols = table.m_columnCount;
        if (cols <= 0)
            return 0;
        const int avail = (std::max)(width - (cols + 1) * m_lineWidth, cols);
        std::vector<int> widths;
        TableColumnWidths(table, avail, widths, false);
        int tableW = (cols + 1) * m_lineWidth;
        for (int c = 0; c < cols; ++c)
            tableW += widths[c];

        const int bodyLine = BodyLineHeight();
        AddDecoration(md_deco_line, x, y, x + tableW, y + m_lineWidth);
        int cursor = y + m_lineWidth;
        std::vector<TextRun> runs;
        for (size_t r = 0; r < table.m_children.size(); ++r)
        {
            const MdBlock& row = m_model.m_blocks[table.m_children[r]];
            const int rowTop = cursor;
            int rowH = bodyLine;
            int cellX = x + m_lineWidth;
            for (int c = 0; c < cols; ++c)
            {
                if (static_cast<size_t>(c) < row.m_children.size())
                {
                    const MdBlock& cell = m_model.m_blocks[row.m_children[c]];
                    RunsFromInlines(cell, runs);
                    InlineParams params;
                    params.m_fontPx = m_style.m_fontPx;
                    params.m_bold = cell.m_headerRow;
                    params.m_align = cell.m_align;
                    const int h = LayoutRuns(runs, params, cellX + m_cellPadH, rowTop + m_cellPadV,
                                             (std::max)(widths[c] - m_cellPadH * 2, 1), true, NULL, NULL);
                    rowH = (std::max)(rowH, h);
                }
                cellX += widths[c] + m_lineWidth;
            }
            rowH += m_cellPadV * 2;
            if (row.m_headerRow)
                AddDecoration(md_deco_table_header, x + m_lineWidth, rowTop, x + tableW - m_lineWidth, rowTop + rowH);
            cursor = rowTop + rowH;
            AddDecoration(md_deco_line, x, cursor, x + tableW, cursor + m_lineWidth);
            cursor += m_lineWidth;
        }

        //竖线：表格左右边框与列之间的分隔线
        int lineX = x;
        for (int c = 0; c <= cols; ++c)
        {
            AddDecoration(md_deco_line, lineX, y, lineX + m_lineWidth, cursor);
            if (c < cols)
                lineX += m_lineWidth + widths[c];
        }
        return cursor - y;
    }

    const MdModel&         m_model;          //文档模型
    HDC                    m_hdc;            //测量用的设备上下文
    const MdResolvedStyle& m_style;          //排版样式
    MdLayoutResult&        m_out;            //排版结果
    int                    m_currentFont;    //当前选入的字体序号；-1 表示还没有选过
    HGDIOBJ                m_oldFont;        //第一次选字体前设备上下文里的字体，析构时恢复

    //以下间距在构造时按字号与界面缩放换算，单位为像素
    int m_blockGap;        //相邻块之间
    int m_lineGap;         //行距
    int m_itemGap;         //紧凑列表相邻项之间
    int m_listIndent;      //列表缩进
    int m_markerGap;       //编号与正文之间
    int m_quoteIndent;     //引用缩进
    int m_quoteBar;        //引用竖条宽度
    int m_codePadH;        //代码块左右内边距
    int m_codePadV;        //代码块上下内边距
    int m_inlineCodePad;   //行内代码底色的左右外延
    int m_cellPadH;        //单元格左右内边距
    int m_cellPadV;        //单元格上下内边距
    int m_headingRuleGap;  //标题与其下方细线之间
    int m_ruleArea;        //分隔线所占高度
    int m_lineWidth;       //细线宽度
    int m_radius;          //圆角半径
    int m_copyButton;      //「复制」按钮边长
    int m_copyMargin;      //「复制」按钮与代码块右上角的距离
    int m_bulletSize;      //圆点直径
    int m_taskSize;        //任务框边长
    int m_alertPadV;       //警告框上下内边距
    int m_alertPadH;       //警告框右内边距
    int m_alertIcon;       //警告框图标边长
    int m_alertIconGap;    //警告框图标与标题之间
    int m_alertTitleGap;   //警告框标题行与正文之间
};

} // namespace

MdResolvedStyle::MdResolvedStyle()
    : m_fontPx(0), m_codeFontPx(0), m_scalePercent(kPercent), m_textColor(0), m_mutedColor(0), m_linkColor(0),
      m_codeBackColor(0), m_ruleColor(0), m_tableHeaderBackColor(0), m_selectionBackColor(0), m_accentColor(0),
      m_fillWidth(false)
{
}

bool MdResolvedStyle::operator==(const MdResolvedStyle& other) const
{
    return m_fontName == other.m_fontName && m_codeFontName == other.m_codeFontName && m_fontPx == other.m_fontPx
           && m_codeFontPx == other.m_codeFontPx && m_scalePercent == other.m_scalePercent
           && m_textColor == other.m_textColor && m_mutedColor == other.m_mutedColor
           && m_linkColor == other.m_linkColor && m_codeBackColor == other.m_codeBackColor
           && m_ruleColor == other.m_ruleColor && m_tableHeaderBackColor == other.m_tableHeaderBackColor
           && m_selectionBackColor == other.m_selectionBackColor && m_accentColor == other.m_accentColor
           && m_fillWidth == other.m_fillWidth;
}

void ResolveMdStyle(const MdStyle& in, MdResolvedStyle& out)
{
    out.m_fontName = (in.m_fontName != NULL) ? in.m_fontName : L"";
    out.m_codeFontName = (in.m_codeFontName != NULL && in.m_codeFontName[0] != 0) ? in.m_codeFontName : kDefaultCodeFont;
    int px = in.m_fontPx;
    if (px < kMinFontPx)
        px = kMinFontPx;
    if (px > kMaxFontPx)
        px = kMaxFontPx;
    out.m_fontPx = px;
    out.m_codeFontPx = (std::max)(px - kCodeFontShrinkPx, kMinFontPx);
    out.m_scalePercent = (in.m_scalePercent > 0) ? in.m_scalePercent : kPercent;
    out.m_textColor = in.m_textColor;
    out.m_mutedColor = in.m_mutedColor;
    out.m_linkColor = in.m_linkColor;
    out.m_codeBackColor = in.m_codeBackColor;
    out.m_ruleColor = in.m_ruleColor;
    out.m_tableHeaderBackColor = in.m_tableHeaderBackColor;
    out.m_selectionBackColor = in.m_selectionBackColor;
    out.m_accentColor = in.m_accentColor;
    //第二版字段：旧宿主编译时的结构里没有它，不能越界读取
    out.m_fillWidth = (in.m_cbSize >= kMdStyleV2Size) && ((in.m_layoutFlags & md_layout_fill_width) != 0);
}

void LayoutMdModel(const MdModel& model, HDC hdc, const MdResolvedStyle& style, int maxWidth, MdLayoutResult& out)
{
    if (model.m_blocks.empty())
    {
        out.Clear();
        return;
    }
    LayoutEngine engine(model, hdc, style, out);
    engine.Run(maxWidth);
}

} // namespace flamingomd
