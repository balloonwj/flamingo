/**
 *  BalloonClosePromptDlg.h
 *
 *  "关闭确认"对话框 —— 对照 design/退出确认1.png / 退出确认2.png。基于 balloonui
 *  DuiFrameWindow。两张可点选的关闭方式卡片（最小化到任务栏 / 退出 FlamingoIM，
 *  整卡可点、选中整卡高亮）+ "记住我的选择"复选框 + 取消 / 确认按钮；确认按钮颜色
 *  跟随选中项变化（最小化→蓝、退出→红）。
 *
 *  与主面板的关系：用户点主面板右上角关闭按钮时，CBalloonMainPanel::OnCloseMsg
 *  在"还需提示"时创建并显示本对话框；点"确认"后本对话框把选择写回 UserConfig 并
 *  落盘，再隐藏主面板（最小化）或 PostMessage(WM_CLOSE_MAINDLG) 触发整体退出。
 *
 *  纯决策逻辑见 ClosePromptDecision.h（可单测）；本文件只管 UI 与动作派发。
 *
 *  本文件遵循 balloonui 头文件约定：包含它之前调用方须已引入 ATL/WTL。
 *
 *  balloonwj@qq.com   2026-06-04
 */
#pragma once

#include "../../balloonui/balloonui/Controls/Window/DuiFrameWindow.h"
#include "../../balloonui/balloonui/Controls/Basic/DuiButton.h"
#include "../../balloonui/balloonui/DuiNotify.h"

class CFlamingoClient;

namespace balloonmain {

/**
 *  ClosePromptOptionCard —— 关闭确认对话框里的一张关闭方式卡片。
 *
 *  继承 DuiButton 复用悬停 / 点击 / 通知逻辑（点击发 DUIN_CLICK 给对话框，由
 *  对话框据 ctrlId 切换选中），只覆写 OnPaint 自绘：左侧线性图标 + 标题 + 副标题
 *  + 右侧单选圆点；按"选中 / 悬停 / 默认"三态切换底色、描边与强调色。
 */
class ClosePromptOptionCard : public balloonwjui::DuiButton
{
public:
    ClosePromptOptionCard();

    //设置卡片内容：iconKind 取值见 MainPanelIcons.h 的 IconKind；title 为主标题，
    //subtitle 为副标题说明。
    void SetCardContent(int iconKind, LPCTSTR title, LPCTSTR subtitle);

    //设置强调色（选中态的描边 / 标题 / 图标 / 单选点颜色）。最小化卡传蓝、退出卡传红。
    void SetAccent(COLORREF accent);

    //设置 / 读取选中态。选中 → 整卡换强调色描边 + 淡强调底 + 实心单选点。
    void SetSelected(bool selected);
    bool IsSelected() const { return m_selected; }

    // DuiControl 覆写：自绘整张卡片。
    void OnPaint(HDC hdc, const RECT& rcDirty) override;

    // DuiControl 覆写：进入 / 离开时除基类维护 hover 态外主动重绘（基类不重绘的坑）。
    bool OnMouseEnter() override;
    bool OnMouseLeave() override;

private:
    int      m_iconKind;   //左侧线性图标类型（IconKind），-1 表示不画
    CString  m_title;      //主标题（如"最小化到任务栏"）
    CString  m_subtitle;   //副标题说明（如"继续在后台运行，可从任务栏图标重新打开"）
    COLORREF m_accent;     //强调色（选中态用）
    bool     m_selected;   //是否为当前选中卡片
};

} // namespace balloonmain

/**
 *  CBalloonClosePromptDlg —— 关闭确认对话框（非模态浮窗，owner = 主面板）。
 */
class CBalloonClosePromptDlg : public balloonwjui::DuiFrameWindow
{
public:
    CBalloonClosePromptDlg();
    ~CBalloonClosePromptDlg();

    /**
     *  创建并显示对话框（已显示则前置 + 复位选中态）。
     *    ownerPanel：主面板窗口句柄；"最小化 / 退出"动作执行时隐藏它。
     *    hMainDlg：  隐藏的消息中枢 CMainDlg 句柄；"退出"时向它 PostMessage
     *                WM_CLOSE_MAINDLG 触发整体收尾。
     *    client：    所属客户端对象；读 / 写 m_UserConfig 偏好并落盘，本类只引用不持有。
     */
    void OpenForClose(HWND ownerPanel, HWND hMainDlg, CFlamingoClient* client);

    BEGIN_MSG_MAP(CBalloonClosePromptDlg)
        MESSAGE_HANDLER(WM_CLOSE, OnCloseMsg)
        MESSAGE_HANDLER(WM_KEYDOWN, OnKeyDownMsg)
        MESSAGE_HANDLER(WM_DUI_NOTIFY, OnDuiNotify)
        CHAIN_MSG_MAP(balloonwjui::DuiFrameWindow)
    END_MSG_MAP()

private:
    LRESULT OnCloseMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnKeyDownMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnDuiNotify(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

    //构建一次静态骨架（副标题 + 两张卡片 + 底部复选框/按钮栏）。
    void BuildUiOnce();

    //选中某张卡片（取值见 ClosePromptDecision.h 的 CloseAction）：切换两卡选中态、
    //更新 m_selectedAction 与确认按钮变体（蓝/红）。
    void SelectAction(int action);

    //点"确认"：据选中项 + "记住我的选择"算决策 → 写回 UserConfig 落盘 → 关本框 →
    //隐藏主面板（最小化）或 PostMessage 触发退出。
    void DoConfirm();

private:
    CFlamingoClient* m_lpFMGClient;   //所属客户端（取 / 存 UserConfig），引用不持有
    HWND             m_panelHwnd;     //主面板窗口句柄（执行动作时隐藏它）
    HWND             m_mainDlgHwnd;   //CMainDlg 句柄（退出时 PostMessage WM_CLOSE_MAINDLG）

    //控件指针（借用，控件树持有）：切换选中 / 读复选框 / 改确认按钮变体时用。
    balloonmain::ClosePromptOptionCard* m_cardMinimize;   //"最小化到任务栏"卡片
    balloonmain::ClosePromptOptionCard* m_cardExit;       //"退出 FlamingoIM"卡片
    balloonwjui::DuiButton*             m_remember;       //"记住我的选择"复选框
    balloonwjui::DuiButton*             m_confirm;        //"确认"按钮（变体随选中项）

    int    m_selectedAction;   //当前选中的关闭方式（CloseAction）
    bool   m_built;            //骨架是否已构建

    //标题栏图标位图（程序图标 IDR_MAINFRAME 合成到标题栏底色上）。SetIcon 仅借用，
    //故由本类持有、析构时 DeleteObject。
    HBITMAP m_titleIconBmp;
};
