/**
 *  ChatInlineText.h
 *
 *  「文字 + 内联表情」的排版与绘制引擎。把一条消息正文（形如
 *  `明天见 /f["5"] 记得带上资料`）解析出的段序列，按给定的最大宽度折行排版成一串
 *  带坐标的放置项，再照着放置项把文字画出来、把表情画成 程序目录/Face 下的小图。
 *
 *  典型用法（气泡与消息记录窗都是这个路子）：
 *      std::vector<balloonchat::ChatSegment> segs = balloonchat::ParseSendBody(text);
 *      balloonchatwnd::InlineTextStyle st;
 *      st.sizePx = 14;
 *      st.color  = RGB(0x33, 0x33, 0x33);
 *      balloonchatwnd::DrawInlineContent(hdc, segs, rcText, st,
 *                                        balloonchatwnd::kFaceDisplayPx);
 *
 *  ------------------------------------------------------------------
 *  为什么单独成一个文件
 *
 *  这套代码原先是 `ChatBubble.cpp` 里一段匿名 namespace 的私有实现，只有聊天气泡
 *  用得到。消息记录窗也要"把表情画成表情"（见 docs/消息记录内容交互方案.md F2）之后，
 *  它就得被两个模块共用，故原样平移出来成为公开模块 —— 平移时<u>没有改动任何一行
 *  排版逻辑</u>，只额外增加了"最多排几行"这个入参（气泡侧传 0 表示不限，行为不变）。
 *
 *  同一套排版必须两处共用，不能各写一份：折行规则、表情与文字的基线对齐、扁平文本
 *  的字符口径三者是咬合的，一旦两份实现漂移，"气泡里选中复制拿到的文字"与"消息记录
 *  里看到的文字"就会对不上。
 *
 *  2026-10-14 起聊天窗的气泡改用带选项的 LayoutInlineContentEx（逐行行高、折行避头尾，见 InlineLayoutOptions），
 *  消息记录窗等其余调用方仍走 LayoutInlineContent 的原有规则。
 *  ------------------------------------------------------------------
 *
 *  字符口径：全文按 UTF-16 字符计数，<u>一个内联表情占一个字符位</u>，在扁平文本里
 *  用 U+FFFC 占位（与 ChatTextSelection.h 一致）。放置项上的 charBegin / charCount
 *  就是这个口径，选中复制靠它把屏幕坐标映射回原文下标。
 *
 *  本文件遵循 balloonui 头文件约定：包含它之前调用方须已引入 ATL/WTL（用到 CString）。
 *
 *  balloonwj@qq.com   2026-08-05
 */
#pragma once

#include <string>
#include <vector>

#include "ChatTextSelection.h"                   // SelLayout：排版结果→选区模型
#include "../BalloonChat/BalloonChatMsgFormat.h" // balloonchat::ChatSegment

namespace balloonchatwnd {

/**
 *  绘制一段内联内容时用的字体与颜色。
 *
 *  与消息协议里的 `net::TextFont` 不是一回事 —— 那个是"这条消息带的字体设置"，
 *  本结构是"这一次绘制实际用什么画"，两者之间的换算由调用方负责（气泡里由
 *  BubbleTextStyle 那条路做，消息记录窗直接按列表样式填）。
 */
struct InlineTextStyle
{
    int      sizePx;     // 字号（像素，传给 CreateFont 的 lfHeight 取负）
    bool     bold;       // 是否粗体
    bool     italic;     // 是否斜体
    bool     underline;  // 是否加下划线
    bool     strike;     // 是否加删除线
    COLORREF color;      // 文字颜色

    InlineTextStyle()
        : sizePx(14)
        , bold(false)
        , italic(false)
        , underline(false)
        , strike(false)
        , color(RGB(0x33, 0x33, 0x33))
    {
    }
};

/**
 *  一个排版后的放置项（坐标相对布局原点 (0,0)，绘制时由调用方平移）。
 */
struct InlinePlaced
{
    bool    isFace;   // true=表情格（取 faceId）；false=文字段（取 text）
    int     faceId;   // isFace 时有效：系统表情 id，对应 程序目录/Face/face<id>.png
    CString text;     // 非 isFace 时有效：本段要画的文字
    RECT    rc;       // 相对布局原点的矩形

    // 本项在"扁平文本"里占的区间。扁平文本 = 各段文字原样拼接、每个表情占一个
    // U+FFFC 占位字符（见 ChatTextSelection.h 的口径说明）。选中复制靠它把屏幕
    // 坐标映射回原文下标。表情项 charCount 恒为 1。
    int     charBegin;
    int     charCount;

    InlinePlaced()
        : isFace(false)
        , faceId(-1)
        , charBegin(0)
        , charCount(0)
    {
        ::SetRectEmpty(&rc);
    }
};

/**
 *  消息正文里是否含内联表情。
 *
 *  调用方据它决定走内联排版还是原生 DrawText 快路径 —— 绝大多数消息不含表情，
 *  没必要为它们付一次分段 + 逐 token 排版的代价。
 *    text：消息正文（形如 `明天见 /f["5"]`，未解析）。
 *  @return 含至少一个表情段返回 true。
 */
bool MessageTextHasFace(const CString& text);

/**
 *  把消息段序列按 maxW 折行排版。
 *
 *  <u>调用前须已把目标字体 select 进 hdc</u> —— 所有测量都按该字体做，与随后绘制
 *  时用的字体必须是同一套，否则量出来的宽度和画出来的对不上。
 *
 *    hdc：       已选好目标字体的设备上下文。
 *    segs：      正文解析出的段序列（balloonchat::ParseSendBody 的产物）。
 *    maxW：      可用宽度（像素）。小于 1 时按 1 处理。
 *    faceSize：  表情格边长（像素）。行高取 max(字体行高, faceSize)。
 *    outPlaced： 非 NULL 时填入各放置项（绘制 / 选区用）；NULL 时只测尺寸。
 *    maxLines：  最多排几行；<= 0 表示<u>不限行</u>（气泡走这条，行为与抽取前一致）。
 *                消息记录窗的折叠态传 5，排满 5 行即停。
 *    outTruncated：非 NULL 时回填"是否因为行数上限而没排完"。不限行时恒为 false。
 *
 *  @return 内容整体尺寸（不含内边距）。受 maxLines 限制时高度恰为 maxLines 行。
 */
SIZE LayoutInlineContent(HDC hdc, const std::vector<balloonchat::ChatSegment>& segs,
                         int maxW, int faceSize, std::vector<InlinePlaced>* outPlaced,
                         int maxLines = 0, bool* outTruncated = NULL);

/**
 *  排版选项（docs/聊天气泡美化方案.md 第 1 节第 4 条，2026-10-14 起聊天窗的气泡使用）。缺省值就是 LayoutInlineContent
 *  的原有规则：每行的行高都取字体行高与表情格边长中较大的一个，折行时不处理标点。
 */
struct InlineLayoutOptions
{
    // 没有表情的行的行高（像素）。<= 0 表示按原有规则；大于 0 时逐行计算：没有表情的行取它与字体行高中较大的一个，
    // 含表情的行再与表情格边长比较取较大的一个
    int  m_textLineH;
    // 折行时是否避头尾：为 true 时句号、逗号、右引号、右括号等不放在行首（连同前一个字换到下一行），
    // 左引号、左括号不留在行尾（提前换行）
    bool m_kinsoku;

    InlineLayoutOptions()
        : m_textLineH(0)
        , m_kinsoku(false)
    {
    }
};

/**
 *  按选项把消息段序列折行排版。参数与返回值同 LayoutInlineContent，多一个 opts；opts 取缺省值时结果与
 *  LayoutInlineContent 完全相同。
 *    opts：排版选项，见 InlineLayoutOptions。
 */
SIZE LayoutInlineContentEx(HDC hdc, const std::vector<balloonchat::ChatSegment>& segs,
                           int maxW, int faceSize, const InlineLayoutOptions& opts,
                           std::vector<InlinePlaced>* outPlaced, int maxLines = 0, bool* outTruncated = NULL);

/**
 *  在 rc 内绘制「文字 + 内联表情」。内部自建字体、排版、绘制，用完还原设备上下文。
 *    hdc：      目标设备上下文。
 *    segs：     正文解析出的段序列。
 *    rc：       文本区矩形（绝对坐标）。内容从左上角开始排，超出部分不裁剪。
 *    st：       字体与颜色。
 *    faceSize： 表情格边长（像素）。
 *    maxLines： 最多画几行；<= 0 表示不限行。语义同 LayoutInlineContent。
 */
void DrawInlineContent(HDC hdc, const std::vector<balloonchat::ChatSegment>& segs,
                       const RECT& rc, const InlineTextStyle& st, int faceSize,
                       int maxLines = 0);

/**
 *  按样式建字体，把消息段序列折行排版成放置项。与 DrawInlineContent 内部的排版完全相同（同一
 *  字体、同一宽度），调用方可以把结果缓存起来，之后用 DrawInlinePlaced 反复绘制而不再排版。
 *    hdc：      用来测量的设备上下文；本函数自建字体、用完还原。
 *    segs：     正文解析出的段序列。
 *    maxW：     可用宽度（像素），绘制时文本区矩形的宽度。
 *    faceSize： 表情格边长（像素）。
 *    st：       字体样式（字号 / 粗体 / 斜体等；颜色不影响排版）。
 *    outPlaced：非 NULL 时填入各放置项。
 *    maxLines： 最多排几行；<= 0 表示不限行。
 *  @return 内容整体尺寸；建字体失败时为 0×0。
 */
SIZE LayoutInlineContentForStyle(HDC hdc, const std::vector<balloonchat::ChatSegment>& segs,
                                 int maxW, int faceSize, const InlineTextStyle& st,
                                 std::vector<InlinePlaced>* outPlaced, int maxLines = 0);

/**
 *  同上，按选项排版（见 InlineLayoutOptions）。聊天窗的气泡测量与绘制都走这一个，两处的选项必须相同。
 *    opts：排版选项。
 */
SIZE LayoutInlineContentForStyle(HDC hdc, const std::vector<balloonchat::ChatSegment>& segs,
                                 int maxW, int faceSize, const InlineTextStyle& st,
                                 const InlineLayoutOptions& opts,
                                 std::vector<InlinePlaced>* outPlaced, int maxLines = 0);

/**
 *  按已排好的放置项绘制「文字 + 内联表情」，不再排版。只画与 hdc 当前裁剪区（左右各放宽一个
 *  字号）相交的项：设了小块裁剪区只重画局部时，耗时只与该区域里的项数有关。
 *    hdc：   目标设备上下文。
 *    placed：LayoutInlineContentForStyle 按同一样式、同一宽度排出的放置项。
 *    rc：    文本区矩形（绝对坐标），放置项相对它的左上角平移。
 *    st：    字体与颜色。
 */
void DrawInlinePlaced(HDC hdc, const std::vector<InlinePlaced>& placed, const RECT& rc,
                      const InlineTextStyle& st);

/**
 *  一段要换样式重画的字符区间（扁平文本口径，右开），DrawInlineContentRestyledRanges 的入参。
 */
struct InlineCharRange
{
    int m_begin;   // 区间起始字符下标
    int m_end;     // 区间结束字符下标（右开）；不大于 m_begin 时该区间不画

    InlineCharRange()
        : m_begin(0)
        , m_end(0)
    {
    }

    InlineCharRange(int begin, int end)
        : m_begin(begin)
        , m_end(end)
    {
    }
};

/**
 *  把已排好版的内容里<u>若干段字符区间</u>用另一种样式重画一遍（@提醒高亮、超链接）。
 *
 *  做法是「按各区间占的矩形设裁剪区、在裁剪区里重画落在矩形上的那些放置项」，而不是
 *  只画那几个字的子串 —— 后者遇到区间跨行就得自己按行拆、还得复刻本引擎的折行规则；
 *  裁剪重画沿用同一次排版，落在矩形里的字形恰好就是那几个字，跨行天然正确。
 *
 *  整条内容只排版一次，所有区间共用；每个矩形只重画与它相交的放置项（判断时把矩形左右
 *  各扩一个字号，斜体等伸出放置项矩形的字形部分也照样重画）。2026-10-01 之前是每个矩形都
 *  把整条内容重新排版、全部重画一遍，一条几十行、每行一个链接的消息首次绘制要好几秒。
 *
 *  <u>本函数是 @提醒高亮与超链接渲染共用的那一份</u> —— 两处都要"把某几个字换个样子
 *  画"，实现必须只有一份，否则两边的折行 / 裁剪细节迟早漂移。
 *
 *    hdc：       目标设备上下文。
 *    segs：      正文解析出的段序列（与 DrawInlineContent 用的同一份）。
 *    layout：    字符级排版模型（BuildSelLayoutFromPlaced 的产物）。
 *    ranges：    要重画的各段字符区间；为空时不画。
 *    rcText：    文本区矩形（绝对坐标），须与排版时用的是同一块。
 *    style：     这些区间要用的样式（颜色 / 下划线等）；各区间共用同一种样式。
 *    faceSize：  内联表情显示边长（像素）。
 */
void DrawInlineContentRestyledRanges(HDC hdc,
                                     const std::vector<balloonchat::ChatSegment>& segs,
                                     const SelLayout& layout,
                                     const std::vector<InlineCharRange>& ranges,
                                     const RECT& rcText,
                                     const InlineTextStyle& style,
                                     int faceSize);

/**
 *  与 DrawInlineContentRestyledRanges 相同，但使用已排好的放置项，不再排版（调用方缓存了排版
 *  结果时用它）。放置项须是按同一宽度、字号相同的样式排出来的（颜色、下划线不影响排版）。
 *    placed：已排好的放置项；其余参数同 DrawInlineContentRestyledRanges。
 */
void DrawInlinePlacedRestyledRanges(HDC hdc,
                                    const std::vector<InlinePlaced>& placed,
                                    const SelLayout& layout,
                                    const std::vector<InlineCharRange>& ranges,
                                    const RECT& rcText,
                                    const InlineTextStyle& style);

/**
 *  把已排好版的内容里<u>某一段字符区间</u>用另一种样式重画一遍。等同于只传一个区间的
 *  DrawInlineContentRestyledRanges，做法与代价见那里的说明。
 *
 *    hdc：       目标设备上下文。
 *    segs：      正文解析出的段序列（与 DrawInlineContent 用的同一份）。
 *    layout：    字符级排版模型（BuildSelLayoutFromPlaced 的产物）。
 *    charBegin： 区间起始字符下标（扁平文本口径）。
 *    charEnd：   区间结束字符下标（右开）。charEnd <= charBegin 时不画。
 *    rcText：    文本区矩形（绝对坐标），须与排版时用的是同一块。
 *    style：     这一段要用的样式（颜色 / 下划线等）。
 *    faceSize：  内联表情显示边长（像素）。
 */
void DrawInlineContentRestyledRange(HDC hdc,
                                    const std::vector<balloonchat::ChatSegment>& segs,
                                    const SelLayout& layout,
                                    int charBegin, int charEnd,
                                    const RECT& rcText,
                                    const InlineTextStyle& style,
                                    int faceSize);

/**
 *  把消息段序列拼成"扁平文本"：文字原样拼接，每个表情占一个 U+FFFC 占位字符。
 *  选中区间就是这个串上的下标区间，复制时再把占位符换成可读文案（如 "[表情]"，
 *  见 ChatTextSelection.h 的 ExtractSelectedText）。
 *    segs：正文解析出的段序列。
 *  @return 扁平文本。
 */
std::wstring BuildFlatText(const std::vector<balloonchat::ChatSegment>& segs);

/**
 *  取某个系统表情的图片文件路径：<exe 目录>\Face\face<faceId>.png。气泡绘制内联表情与
 *  输入框插入表情（粘贴、拖入聊天内容时）都按这个位置找图，二者必须一致。
 *    faceId：表情编号（Face/FaceConfig.xml 的 id）。
 *  @return 完整路径；不检查文件是否存在。
 */
CString FaceImagePathFromId(int faceId);

/**
 *  把排版放置结果转成选区模型：按 top 分行，每项转成一段，并逐字符量出右边界
 *  （选区左右边界、命中测试都要精确到字符）。
 *
 *  <u>调用前须已把目标字体 select 进 hdc</u>，且必须与排版时是同一套字体。
 *    hdc：        已选好目标字体的设备上下文。
 *    placed：     LayoutInlineContent 产出的放置项（须按文档顺序，本函数据 top 分行）。
 *    totalChars： 扁平文本总字符数（BuildFlatText 的结果长度）。
 *    out：        [出参] 选区模型，函数内部先清空再填。
 */
void BuildSelLayoutFromPlaced(HDC hdc, const std::vector<InlinePlaced>& placed,
                              int totalChars, SelLayout& out);

// ---- 性能探针计数器（只在界面线程上累加，供单元测试核对工作量，不参与任何业务判断）----

// 自进程启动或上次 ResetInlineProbeCounters 以来，LayoutInlineContent 被调用的次数。
unsigned long InlineLayoutCallCount();

// 自进程启动或上次 ResetInlineProbeCounters 以来，绘制放置项（文字段与表情格）的次数。
unsigned long InlinePlacedDrawCount();

// 把上面两个计数器清零。
void ResetInlineProbeCounters();

} // namespace balloonchatwnd
