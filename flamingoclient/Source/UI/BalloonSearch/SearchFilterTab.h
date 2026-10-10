/**
 *  SearchFilterTab.h
 *
 *  搜索弹窗顶部的文字筛选标签（全部 / 联系人 N / 群组 / 消息 / 文件）。
 *  继承 balloonui DuiButton 复用悬停 / 点击 / 通知逻辑，只覆写 OnPaint 自绘
 *  "文字 + 选中橙色下划线 + 可选计数"。MainPanelTab 不绘制文字，不适用本场景，
 *  故另建本控件。
 *
 *  本文件遵循 balloonui 头文件约定：包含它之前调用方须已引入 ATL/WTL。
 *
 *  balloonwj@qq.com   2026-06-01
 */
#pragma once

#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiButton.h"

namespace balloonsearch {

/**
 *  SearchFilterTab —— 搜索分类筛选文字标签。
 */
class SearchFilterTab : public balloonwjui::DuiButton
{
public:
    SearchFilterTab();

    // 设置标签文字（如"全部""联系人"）。
    void SetLabel(LPCTSTR text);

    // 设置选中态：选中者文字加深并画橙色下划线。
    void SetActive(bool active);
    bool IsActive() const { return m_active; }

    // 设置计数：show 为 true 且 count>=0 时在文字右侧显示数字（如"联系人 1"）。
    // 副作用：期望宽度随显示文字变化（见 GetDesiredSize），按自动宽度排版的宿主须自行
    // 让标签所在的那一行重新排版，新宽度才会生效。
    void SetCount(bool show, int count);

    // 设置置灰（不可用）：置灰标签文字浅灰，且不参与选中/点击高亮。
    void SetGrayed(bool grayed);
    bool IsGrayed() const { return m_grayed; }

    // DuiControl 覆写：自绘文字 + 选中下划线 + 计数。
    void OnPaint(HDC hdc, const RECT& rcDirty) override;

    // DuiControl 覆写：标签用普通箭头光标（不抢手型）。
    bool OnSetCursor(POINT pt) override { return false; }

    // DuiControl 覆写：报告按当前显示文字（"标签" 或 "标签 N"）量出的期望宽度（像素，含
    // 左右空白、不小于最小宽度），高度报 0（由宿主那一行决定）。宿主用 Hint().Auto() 排版时，
    // 计数位数变多标签会跟着变宽，文字不会被截掉。
    SIZE GetDesiredSize() const override;

private:
    // 当前显示的文字："标签"，或带计数时的"标签 N"。
    CString DisplayText() const;

    // 按当前显示文字重算 m_desiredWidth。标签文字或计数变化时调用。
    void RecomputeDesiredWidth();

private:
    CString m_label;        // 标签文字
    bool    m_active;       // 是否选中
    bool    m_showCount;    // 是否显示计数
    int     m_count;        // 计数值
    bool    m_grayed;       // 是否置灰不可用
    int     m_desiredWidth; // 按显示文字量出的期望宽度（像素），GetDesiredSize 返回它
};

} // namespace balloonsearch
