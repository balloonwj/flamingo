/**
 *  「清理本地缓存」对话框，BalloonCacheCleanDlg.h
 *
 *  从「系统设置 → 存储 → 立即清理」弹出。列出各类可清缓存及其占用量，每类一个开关，
 *  用户勾选后执行删除并反馈结果。
 *
 *  <u>为什么用开关而不是复选框</u>：balloonui 没有复选框控件（`Controls/Input` 下只有
 *  ComboBox / EditHost / SearchBox / Slider / SpinBox / Switch）。按开发约定文档「最大化
 *  复用 balloonui」的顺序，直接用现成的 `DuiSwitch`——与系统设置页各行观感一致，不新增
 *  控件、不动 balloonui。
 *
 *  职责分工：要删哪几个目录由 CacheCleanPlan 算（纯逻辑、有单测），扫占用量与真正的
 *  删除由 CacheCleanExecutor 做（碰磁盘），本类只管界面与流程串联。
 *
 *  扫描可能耗时几秒，期间不起线程：建窗时同步扫一遍，扫完再把窗口显示出来 —— 一次性
 *  操作，起线程带来的状态同步麻烦不值当。
 *
 *  本文件遵循 balloonui 头文件约定：包含它之前调用方须已引入 ATL/WTL。
 *
 *  balloonwj@qq.com   2026-08-12
 */

#pragma once

#include <vector>

#include "../../balloonui/balloonui/Controls/Window/DuiFrameWindow.h"
#include "CacheCleanPlan.h"

class CFlamingoClient;

/**
 *  CBalloonCacheCleanDlg —— 清理本地缓存对话框（非模态浮窗，owner = 系统设置窗）。
 */
class CBalloonCacheCleanDlg : public balloonwjui::DuiFrameWindow
{
public:
    /**
     *  构造函数。
     *    pFMGClient：所属客户端对象（取登录账号用），本类只引用、不持有。
     */
    explicit CBalloonCacheCleanDlg(CFlamingoClient* pFMGClient);
    ~CBalloonCacheCleanDlg();

    /**
     *  创建并显示对话框（已创建则前置显示）。打开前会同步扫描各类缓存的占用量。
     *    owner：宿主窗口（系统设置窗）句柄，仅作语义留存。
     */
    void OpenDialog(HWND owner);

    BEGIN_MSG_MAP(CBalloonCacheCleanDlg)
        MESSAGE_HANDLER(WM_CLOSE, OnCloseMsg)
        // 开关的滑动动画要靠宿主周期性推进 DuiAnimMgr，否则点了不动（与系统设置窗同处理）。
        MESSAGE_HANDLER(WM_TIMER, OnTimerMsg)
        MESSAGE_HANDLER(WM_DUI_NOTIFY, OnDuiNotify)
        CHAIN_MSG_MAP(balloonwjui::DuiFrameWindow)
    END_MSG_MAP()

private:
    LRESULT OnCloseMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnTimerMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnDuiNotify(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

    // 扫描各类缓存的占用量，结果填进 m_sizes（与 m_plan 一一对应）。同步执行。
    void ScanSizes();

    // 建客户区控件树：每类一行（名称 + 占用量 + 开关），底部合计与两个按钮。
    void InitUi();

    // 执行清理：把开关处于开启状态的类别逐个删掉，随后提示结果并关窗。
    void DoClean();

private:
    CFlamingoClient* m_lpFMGClient;   // 所属客户端（取登录账号），引用不持有
    HWND m_ownerHwnd;                 // 宿主（系统设置窗）句柄，仅作语义留存

    // 本次打开时算出的清理计划，以及与之一一对应的各类占用量（字节）。
    std::vector<balloonsettings::CacheCleanEntry> m_plan;
    std::vector<unsigned long long>               m_sizes;
};
