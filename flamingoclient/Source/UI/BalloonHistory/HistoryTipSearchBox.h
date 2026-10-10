/**
 *  带悬停说明的搜索框，HistoryTipSearchBox.h
 *
 *  聊天记录管理器的三个搜索框（右侧会话内搜索、左栏跨会话搜索、文件 / 链接 / 与我有关页的搜索）鼠标悬停时显示搜索
 *  语法说明（docs/聊天记录管理器方案.md §12.3）。balloonui 的 DuiToolTipMgr 要求控件销毁前取消登记，否则悬停跟踪
 *  会碰到已销毁的指针；本类在构造时登记、析构时取消，调用方像普通 DuiSearchBox 一样建它即可。说明文字可以随页面
 *  更换（SetTipText），例如文件页只认词与排除词，链接页认全部语法。
 *
 *  典型用法：
 *      std::unique_ptr<HistoryTipSearchBox> search(new HistoryTipSearchBox(HistorySearchSyntaxTip()));
 *
 *  本文件遵循 balloonui 头文件约定：包含它之前调用方须已引入 ATL/WTL。
 *
 *  balloonwj@qq.com   2026-10-06
 */
#pragma once

#include "../../../third-party/balloonui/balloonui/Controls/Input/DuiSearchBox.h"

namespace balloonhistory {

/**
 *  HistoryTipSearchBox —— 鼠标悬停时显示一段说明的 DuiSearchBox。
 */
class HistoryTipSearchBox : public balloonwjui::DuiSearchBox
{
public:
    /**
     *  构造并登记悬停说明。
     *  @param tip  说明文字（可含 '\n' 分行）；空串表示不显示说明
     */
    explicit HistoryTipSearchBox(LPCTSTR tip);

    //析构时取消登记
    virtual ~HistoryTipSearchBox();

    /**
     *  更换悬停说明。
     *  @param tip  新的说明文字；空串表示不再显示说明
     */
    void SetTipText(LPCTSTR tip);
};

//完整搜索语法的说明（会话内搜索、跨会话搜索、链接页与与我有关页用），已按界面语言翻译
LPCTSTR HistorySearchSyntaxTip();

//文件名搜索的说明（文件页用：只认多个词、排除词与引号），已按界面语言翻译
LPCTSTR HistoryFileNameSearchTip();

} // namespace balloonhistory
