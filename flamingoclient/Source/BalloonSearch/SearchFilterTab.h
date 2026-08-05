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

#include "../../balloonui/balloonui/Controls/Basic/DuiButton.h"

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
    void SetCount(bool show, int count);

    // 设置置灰（不可用）：置灰标签文字浅灰，且不参与选中/点击高亮。
    void SetGrayed(bool grayed);
    bool IsGrayed() const { return m_grayed; }

    // DuiControl 覆写：自绘文字 + 选中下划线 + 计数。
    void OnPaint(HDC hdc, const RECT& rcDirty) override;

    // DuiControl 覆写：标签用普通箭头光标（不抢手型）。
    bool OnSetCursor(POINT pt) override { return false; }

private:
    CString m_label;     // 标签文字
    bool    m_active;    // 是否选中
    bool    m_showCount; // 是否显示计数
    int     m_count;     // 计数值
    bool    m_grayed;    // 是否置灰不可用
};

} // namespace balloonsearch
