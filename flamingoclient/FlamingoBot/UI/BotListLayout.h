/**
 *  群机器人管理窗与「我的机器人」窗口共用的列表排版（docs/机器人方案.md 4.2、4.3）：
 *    - 窗口骨架：可滚动的列表区，底部一行「说明 | 按钮……」（BuildScrollFrame）；
 *    - 列表里的一行机器人：一张卡片，左侧头像，中间名称与状态、简介、用法说明，右侧操作按钮；
 *    - 分组标题与说明文字。
 *  列表内容每次整体重建（FillXxx 由窗口实现），重建之后按 UI 约定第 12 条重排滚动视图。
 *
 *  本文件遵循 balloonui 头文件约定：包含它之前调用方须已引入 ATL/WTL（本工程的 stdafx.h）。
 *
 *  典型用法：
 *      BotRowView row;
 *      row.m_name = L"值班提醒";
 *      row.m_buttons.push_back(BotRowButton(TR(L"编辑"), id, true));
 *      flamingobot::AddBotRow(frame.m_content, row, font, kManagerContentWidth);
 *
 *  balloonwj@qq.com   2026-10-08
 */
#pragma once

#include <memory>
#include <string>
#include <vector>

#include "BotWidgets.h"

namespace flamingobot {

//---- 窗口尺寸，单位：像素（整窗尺寸，含标题栏，UI 约定第 7 条） ----
const int kListWindowWidth = 580;    //群机器人管理窗与「我的机器人」窗口的宽
const int kListWindowHeight = 600;   //两个窗口的高
//列表区可用的宽度：窗口宽减两侧内边距与滚动条留白
const int kListContentWidth = kListWindowWidth - 2 * kBotPadX - kBotScrollGutter;

//一行机器人右侧的一个操作按钮
struct BotRowButton
{
    std::wstring m_text;     //按钮文字
    UINT         m_id;       //控件 id
    bool         m_enabled;  //是否可用

    BotRowButton()
        : m_id(0)
        , m_enabled(true)
    {
    }

    BotRowButton(const std::wstring& text, UINT id, bool enabled)
        : m_text(text)
        , m_id(id)
        , m_enabled(enabled)
    {
    }
};

//一行机器人要显示的内容
struct BotRowView
{
    std::wstring              m_name;        //名称
    std::wstring              m_glyph;       //头像字形
    std::wstring              m_color;       //头像底色 "#RRGGBB"
    std::wstring              m_state;       //名称右侧的状态文字，如「生效中」「已停用」
    COLORREF                  m_stateColor;  //状态文字的颜色
    std::wstring              m_desc;        //简介；空表示不显示
    std::wstring              m_detail;      //用法说明或其它补充（可多行）；空表示不显示
    std::vector<BotRowButton> m_buttons;     //右侧的操作按钮，从左到右

    BotRowView()
        : m_stateColor(kBotSubText)
    {
    }
};

/**
 *  在列表区里加一行机器人（卡片）。
 *  @param content 列表区的竖排容器（借用）
 *  @param view    要显示的内容
 *  @param font    字体（借用）
 *  @param width   列表区宽度，单位：像素（简介与用法说明按它算换行后的高度）
 */
void AddBotRow(balloonwjui::DuiVBox* content, const BotRowView& view, HFONT font, int width);

/**
 *  在列表区里加一行分组标题（加粗）。
 */
void AddListSection(balloonwjui::DuiVBox* content, const std::wstring& title, HFONT font);

/**
 *  在列表区里加一段说明文字（按宽度自动换行）。
 *  @param color 文字颜色
 */
void AddListNote(balloonwjui::DuiVBox* content, const std::wstring& text, COLORREF color, HFONT font, int width);

} // namespace flamingobot
