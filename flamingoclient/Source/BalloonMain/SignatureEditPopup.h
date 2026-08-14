/**
 *  SignatureEditPopup.h
 *
 *  主面板"点个性签名→原地编辑→回车提交"的内联编辑弹层。子类化 balloonui
 *  DuiPopupHost（无边框 WS_POPUP 浮层，自带 ESC / 失焦自动关闭），内部承载一个
 *  单行 DuiEditHost；OpenForSignature 把弹层精确盖在签名文字的屏幕矩形上，带入
 *  当前签名并聚焦全选，达到"签名就地变成编辑框"的观感。
 *
 *  提交时机（与产品决策一致）：
 *    · 回车         → 提交（吞掉回车避免单行 EDIT 的提示音）。
 *    · 失焦/点别处  → 也提交（DuiPopupHost 失焦自动关闭，关闭原因 LostFocus）。
 *    · ESC          → 取消（不提交）。
 *  弹层只负责"把编辑框里的文本回吐给调用方"，至于是否真的发包（trim / 是否变化 /
 *  离线拦截）由调用方（CBalloonMainPanel）跑 DecideSignatureCommit 决定。
 *
 *  为何用独立弹层而非在主面板控件树里就地放编辑框：balloonui 是盒式布局、没有
 *  "浮动/绝对定位子控件"，在树里放覆盖层会被布局重新摆位；DuiPopupHost 是本仓库
 *  既有的浮层基座（搜索弹窗 / 菜单同款），定位/ESC/失焦关闭都现成，最稳。
 *
 *  本文件遵循 balloonui 头文件约定：包含它之前调用方须已引入 ATL/WTL。
 *
 *  balloonwj@qq.com   2026-06-03
 */
#pragma once

#include "../../../third-party/balloonui/balloonui/Controls/Feedback/DuiPopupHost.h"

namespace balloonwjui { class DuiEditHost; }

/**
 *  CSignatureEditPopup —— 个性签名内联编辑弹层。
 */
class CSignatureEditPopup : public balloonwjui::DuiPopupHost
{
public:
    // 提交回调：回车 / 失焦提交时触发；newText 为编辑框当前文本（未判定）。
    // 调用方据此跑提交判定并决定发包与否。userdata 为注册时传入的不透明指针。
    typedef void (*CommitCallback)(void* userdata, const CString& newText);

    CSignatureEditPopup();
    ~CSignatureEditPopup();

    // 注册提交回调；nullptr 取消。
    void SetCommitCallback(CommitCallback cb, void* userdata);

    /**
     *  在签名文字的屏幕矩形处打开内联编辑。
     *    signRectScreen：签名文字的屏幕矩形（调用方把 host 客户区矩形转屏幕后传入）。
     *    owner：         owner 窗口句柄（主面板），让弹层随父窗口销毁、不进任务栏。
     *    currentSign：   当前签名文本，带入编辑框并全选。
     */
    void OpenForSignature(const RECT& signRectScreen, HWND owner, LPCTSTR currentSign);

    // 供内部 EDIT 子类化过程回调：回车提交 / ESC 取消。
    void OnEditEnter();
    void OnEditEscape();

    BEGIN_MSG_MAP(CSignatureEditPopup)
        CHAIN_MSG_MAP(balloonwjui::DuiPopupHost)
    END_MSG_MAP()

private:
    // 首次打开时构建一次内容树（一个单行 DuiEditHost）。
    void BuildContentOnce();

    // DuiPopupHost dismiss 回调：失焦关闭→提交；ESC / 程序化关闭→不提交。
    static void OnDismissThunk(void* userdata, balloonwjui::DuiPopupHost::Reason r);
    void        OnDismiss(balloonwjui::DuiPopupHost::Reason r);

    // 读编辑框文本并回吐给调用方；幂等（m_handled 守卫，防回车后失焦重复提交）。
    void DoCommit();

private:
    balloonwjui::DuiEditHost* m_edit;        // 单行编辑框（借用，内容树持有）
    bool           m_built;                  // 内容树是否已构建
    bool           m_editCreated;            // 内部 Win32 EDIT 是否已建
    bool           m_keyHooked;              // 是否已给 EDIT 挂键盘子类化（回车/ESC）
    bool           m_handled;                // 本次会话是否已提交/取消（防重复提交）
    CommitCallback m_commitCb;               // 提交回调
    void*          m_commitUd;               // 回调用户数据
};
