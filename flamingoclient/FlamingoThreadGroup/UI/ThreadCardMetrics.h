/**
 *  帖子卡片的排版计算（docs/帖子群方案.md 4.6 左栏，样稿第 0 期）：给定卡片内容区的宽度与字体，算出卡片各部分的高度，
 *  以及正文最多显示三行时要显示的文字（超出部分在第三行末尾换成「…」）、缩略图显示几张、「+N」是多少。
 *
 *  只依赖 GDI（按 HDC 与字体测量文字），不依赖 balloonui，便于单元测试（TGD12，标签 [threaddll]）在不同宽度下核对排版。
 *  帖子卡片控件（ThreadPostCard）建控件时按这里的结果给各部分定高度。
 *
 *  典型用法：
 *      threadgroup::PostCardMetrics m = threadgroup::MeasurePostCard(hdc, bodyFont, titleFont, innerWidth,
 *                                                                    title, displayText, imageCount);
 *      card->SetHint(textLabel, Hint().Fixed(m.m_textHeight));
 *
 *  balloonwj@qq.com   2026-10-07
 */
#pragma once

#include <windows.h>
#include <string>

namespace threadgroup {

//---- 卡片的固定尺寸（样稿第 0 期），单位：像素 ----
const int kCardPadX = 12;          //卡片左右内边距
const int kCardPadY = 10;          //卡片上下内边距
const int kCardMetaHeight = 26;    //作者行（头像、名字、时间、置顶标记）的高
const int kCardGap = 6;            //卡片里相邻两部分之间的距离
const int kCardThumbPx = 92;       //缩略图边长
const int kCardThumbGap = 6;       //缩略图之间的距离
const int kCardFootHeight = 22;    //底部那一行（互动标签、留言数）的高
const int kCardMaxThumbs = 3;      //卡片最多显示几张缩略图
const int kCardMaxTextLines = 3;   //卡片正文最多显示几行

//一张帖子卡片的排版结果
struct PostCardMetrics
{
    int          m_height;        //卡片总高（含上下内边距）
    int          m_titleHeight;   //标题一行的高；没有标题为 0
    int          m_textHeight;    //正文的高（不超过三行）；没有正文为 0
    std::wstring m_text;          //要显示的正文：放得下时为原文，超过三行时截短并以「…」结尾
    bool         m_textCut;       //正文是否被截短了
    int          m_thumbCount;    //显示几张缩略图（0~3）
    int          m_moreCount;     //第三张缩略图上叠的「+N」里的 N；不需要时为 0
    bool         m_hasFoot;       //有没有底部那一行（互动标签、留言数）

    PostCardMetrics()
        : m_height(0)
        , m_titleHeight(0)
        , m_textHeight(0)
        , m_textCut(false)
        , m_thumbCount(0)
        , m_moreCount(0)
        , m_hasFoot(false)
    {
    }
};

/**
 *  计算一张帖子卡片的排版。
 *  @param hdc         用来测量文字的设备上下文（借用；函数返回前恢复原来选入的字体）
 *  @param bodyFont    正文字体（借用）
 *  @param titleFont   标题字体（借用）
 *  @param innerWidth  卡片内容区的宽度（卡片宽减去左右内边距），单位：像素，至少为 1
 *  @param title       标题；为空时没有标题行
 *  @param displayText 正文的显示文字（表情已换成「[表情名]」）；为空时没有正文
 *  @param imageCount  帖子的图片总数（不只是摘要里带的几张）
 *  @param hasFoot     底部那一行有没有内容（有互动标签或留言数）；没有时不留这一行
 *  @return 排版结果
 */
PostCardMetrics MeasurePostCard(HDC hdc, HFONT bodyFont, HFONT titleFont, int innerWidth, const std::wstring& title,
                                const std::wstring& displayText, int imageCount, bool hasFoot);

/**
 *  把文字截短到按给定宽度自动换行后不超过 maxLines 行，截短时末尾加「…」。
 *  @param hdc      已选入要用的字体的设备上下文（借用）
 *  @param text     原文
 *  @param width    可用宽度，单位：像素
 *  @param maxLines 最多几行（至少为 1）
 *  @param cut      [出参] 是否截短了
 *  @return 放得下时为原文；放不下时为截短后的文字
 */
std::wstring FitTextToLines(HDC hdc, const std::wstring& text, int width, int maxLines, bool& cut);

/**
 *  按给定宽度自动换行后的文字高度，单位：像素。
 *  @param hdc   已选入要用的字体的设备上下文（借用）
 *  @param text  文字；为空时返回 0
 *  @param width 可用宽度，单位：像素
 */
int MeasureWrappedHeight(HDC hdc, const std::wstring& text, int width);

} // namespace threadgroup
