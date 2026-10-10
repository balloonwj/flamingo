/**
 *  帖子群窗口左栏的一张帖子卡片 CThreadPostCard（docs/帖子群方案.md 4.6，样稿第 0 期）。
 *
 *  卡片由 balloonui 现成控件组合而成（UI 约定第 2 条）：白底圆角描边的 DuiVBox，从上到下依次是
 *    - 作者行：头像（DuiAvatar）、名字、时间，置顶的帖子右端带「置顶」标记；
 *    - 标题（加粗，单行）；
 *    - 正文（最多三行，超出时末尾为「…」，由 ThreadCardMetrics 预先截好）；
 *    - 缩略图（最多三张，超过三张时第三张上叠「+N」）；
 *    - 底部一行：互动标签（「赞 N」与各个表情 + 人数，本人加过的高亮）与留言数。
 *  没有的部分不加进去，卡片高度与 ThreadCardMetrics 算出的一致。
 *
 *  交互：整张卡片接管鼠标（子控件不单独响应），单击时以 NotifyParent(DUIN_CLICK, 卡片序号) 通知窗口；窗口据序号找到帖子，
 *  并延后到这条通知处理完再选中它（UI 约定第 13 条）。鼠标悬停时描边变深，选中时描边为主色（UI 约定第 9 条：悬停变化自己重画）。
 *
 *  本文件遵循 balloonui 头文件约定：包含它之前调用方须已引入 ATL/WTL（本工程的 stdafx.h）。
 *
 *  balloonwj@qq.com   2026-10-07
 */
#pragma once

#include <stdint.h>
#include <string>
#include <vector>

#include "../../../third-party/balloonui/balloonui/Controls/Layout/DuiLayout.h"
#include "ThreadCardMetrics.h"

namespace threadgroup {

//卡片底部的一个互动标签
struct PostCardChip
{
    bool    m_like;      //true 为「赞」，false 为表情回应
    int     m_reaction;  //互动编号：0 为赞，1000 + 表情编号为表情回应
    HBITMAP m_icon;      //表情图标（借用，归窗口的位图缓存）；「赞」或表情图片读不出来时为 NULL
    int     m_count;     //人数；「赞」没人点过时为 0，标签只写「赞」
    bool    m_mine;      //本人加过（高亮）

    PostCardChip()
        : m_like(false)
        , m_reaction(0)
        , m_icon(NULL)
        , m_count(0)
        , m_mine(false)
    {
    }
};

//建一张卡片要的全部内容（窗口按帖子、宿主与位图缓存准备好）
struct PostCardData
{
    int64_t                   m_postId;        //帖子 ID
    std::wstring              m_authorName;    //作者显示名
    std::wstring              m_avatarGlyph;   //作者字形头像上的字
    COLORREF                  m_avatarColor;   //作者字形头像的底色
    HBITMAP                   m_avatarBitmap;  //作者照片头像（借用）；为 NULL 时画字形
    std::wstring              m_timeText;      //时间文字（如「今天 14:20」）
    bool                      m_pinned;        //是否置顶
    std::wstring              m_title;         //标题；为空时没有标题行
    PostCardMetrics           m_metrics;       //排版结果（含截好的正文与缩略图张数）
    std::vector<HBITMAP>      m_thumbs;        //缩略图（借用；某张还没下载时为 NULL，画占位底色），张数等于 m_metrics.m_thumbCount
    std::vector<PostCardChip> m_chips;         //互动标签，按显示顺序
    std::wstring              m_commentText;   //留言数文字；为空时不显示

    PostCardData()
        : m_postId(0)
        , m_avatarColor(0)
        , m_avatarBitmap(NULL)
        , m_pinned(false)
    {
    }
};

//卡片发出的单击通知的控件 id（DUIN_CLICK 的 extra 为卡片序号）
const UINT kIdPostCard = 7201;

/**
 *  CThreadPostCard —— 一张帖子卡片。只在界面线程上使用。
 */
class CThreadPostCard : public balloonwjui::DuiVBox
{
public:
    /**
     *  @param index 卡片在列表里的序号（单击通知的 extra）
     */
    explicit CThreadPostCard(int index);

    /**
     *  按内容建出卡片里的控件。只调用一次。
     *  @param data      卡片内容
     *  @param bodyFont  正文与名字的字体（借用，须与排版时用的相同）
     *  @param titleFont 标题的字体（借用，须与排版时用的相同）
     *  @param innerWidth 卡片内容区宽度（卡片宽减去左右内边距），单位：像素
     */
    void Build(const PostCardData& data, HFONT bodyFont, HFONT titleFont, int innerWidth);

    //帖子 ID
    int64_t PostId() const;

    //选中与否：选中时描边为主色
    void SetSelected(bool selected);
    bool IsSelected() const;

    //整张卡片接管命中：点在卡片里时返回卡片自己，子控件不单独响应
    virtual balloonwjui::DuiControl* HitTest(POINT ptHostClient) override;
    //按下：记下按在了卡片上
    virtual bool OnLButtonDown(POINT pt, UINT mkFlags) override;
    //抬起：按下与抬起都在卡片上时发出单击通知
    virtual bool OnLButtonUp(POINT pt, UINT mkFlags) override;
    //悬停变化：改描边颜色并重画（UI 约定第 9 条）
    virtual bool OnMouseEnter() override;
    virtual bool OnMouseLeave() override;

private:
    //按选中与悬停状态设描边颜色
    void ApplyBorder();

private:
    int     m_index;     //在列表里的序号
    int64_t m_postId;    //帖子 ID
    bool    m_selected;  //是否选中
    bool    m_hover;     //鼠标是否悬停在卡片上
    bool    m_pressed;   //鼠标是否在卡片上按下（还没抬起）
};

} // namespace threadgroup
