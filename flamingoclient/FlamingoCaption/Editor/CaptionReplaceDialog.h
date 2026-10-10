/**
 *  字幕编辑器的「自动替换表」窗口（docs/语音字幕方案.md 第 7 节、第 2 节 D4），CaptionReplaceDialog.h
 *
 *  替换表按账号保存：识别出的每一句在进入字幕之前，先按表把常见的误识别（如 Sclight → SQLite）自动替换。
 *  窗口里一个列表显示「原文 → 替换为」，下面两个输入框加「添加」（原文已有时改为更新替换为的文字）、「删除所选」，
 *  底部「关闭」。以模态方式显示，关闭后调用方取 Table() 存回设置。只用 balloonui 现成控件（DuiListBox 等）。
 *
 *  本文件遵循 balloonui 头文件约定：包含它之前调用方须已引入 ATL/WTL（即在 .cpp 里先 #include "stdafx.h"）。
 *
 *  典型用法：
 *      CaptionReplaceDialog dlg(settings.m_replace);
 *      if (dlg.Run(m_hWnd)) { settings.m_replace = dlg.Table(); module.UpdateSettings(settings); }
 *
 *  balloonwj@qq.com   2026-10-06
 */
#pragma once

#include "../Model/CaptionSegmenter.h"
#include "../../Source/UI/BalloonMain/TitleBarIcon.h"   // ProgramWindowIcons：标题栏与任务栏的程序图标
#include "../../../third-party/balloonui/balloonui/Controls/Window/DuiFrameWindow.h"
#include "../../../third-party/balloonui/balloonui/DuiNotify.h"

namespace balloonwjui
{
    class DuiEdit;
    class DuiListBox;
}

namespace caption {

/**
 *  CaptionReplaceDialog —— 自动替换表窗口（一次性使用）。
 */
class CaptionReplaceDialog : public balloonwjui::DuiFrameWindow
{
public:
    /**
     *  @param table 当前的替换表（拷贝保存）
     */
    explicit CaptionReplaceDialog(const CaptionReplaceTable& table);
    ~CaptionReplaceDialog();

    /**
     *  以模态方式显示，直到关闭。
     *  @param owner 属主窗口
     *  @return 表有改动返回 true
     */
    bool Run(HWND owner);

    //关闭后的替换表
    const CaptionReplaceTable& Table() const;

    BEGIN_MSG_MAP(CaptionReplaceDialog)
        MESSAGE_HANDLER(WM_DUI_NOTIFY, OnDuiNotify)
        MESSAGE_HANDLER(WM_CLOSE, OnCloseMsg)
        CHAIN_MSG_MAP(balloonwjui::DuiFrameWindow)
    END_MSG_MAP()

private:
    void buildUi();
    //按当前表重建列表
    void reloadList();
    void addRule();
    void removeSelected();
    void finish();
    LRESULT OnDuiNotify(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnCloseMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

    CaptionReplaceTable       m_table;     //正在编辑的替换表
    bool                      m_changed;   //是否改动过
    bool                      m_done;      //模态是否已结束
    balloonwjui::DuiListBox*  m_list;      //规则列表（归控件树所有）
    balloonwjui::DuiEdit*     m_fromEdit;  //「原文」输入框
    balloonwjui::DuiEdit*     m_toEdit;    //「替换为」输入框
    balloonmain::ProgramWindowIcons m_programIcons;   //标题栏（及任务栏）的程序图标；本类持有，窗口关闭再打开时复用
};

} // namespace caption
