/**
 *  ChatInlineText.h
 *
 *  「文字 + 内联表情」的排版与绘制引擎。把一条消息正文（形如
 *  `明天见 /f["5"] 记得带上资料`）解析出的段序列，按给定的最大宽度折行排版成一串
 *  带坐标的放置项，再照着放置项把文字画出来、把表情画成 Bin/Face 下的小图。
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
    int     faceId;   // isFace 时有效：系统表情 id，对应 Bin/Face/face<id>.png
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
 *  把已排好版的内容里<u>某一段字符区间</u>用另一种样式重画一遍。
 *
 *  做法是「按该区间占的矩形设裁剪区、再把整段内容重画一遍」，而不是只画那几个字的
 *  子串 —— 后者遇到区间跨行就得自己按行拆、还得复刻本引擎的折行规则；裁剪重画沿用
 *  同一次排版，落在矩形里的字形恰好就是那几个字，跨行天然正确。
 *
 *  代价是每个矩形都要重跑一次 DrawInlineContent（内含一次折行排版）。一条消息里
 *  这类区间（@提醒、超链接）通常个位数，且整行绘制结果有渲染缓存兜着、不是每帧都跑，
 *  这个代价可以接受。
 *
 *  <u>本函数是 @提醒高亮与超链接渲染共用的那一份</u> —— 两处都要"把某几个字换个样子
 *  画"，实现必须只有一份，否则两边的折行 / 裁剪细节迟早漂移。
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

} // namespace balloonchatwnd
