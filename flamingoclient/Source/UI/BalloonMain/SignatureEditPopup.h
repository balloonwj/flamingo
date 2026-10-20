/**
 *  SignatureEditPopup.h
 *
 *  主面板"点个性签名→原地编辑→回车提交"的内联编辑弹层。子类化 balloonui
 *  DuiPopupHost（无边框 WS_POPUP 浮层，自带 ESC / 失焦自动关闭），内部承载一个
 *  单行 DuiEdit；OpenForSignature 把弹层精确盖在签名文字的屏幕矩形上，带入
 *  当前签名并聚焦全选，达到"签名就地变成编辑框"的观感。
 *
 *  提交时机（与产品决策一致）：
 *    · 回车         → 提交（编辑框发出 DUIN_EDIT_ENTER，本弹层在通知处理里接住）。
 *    · 失焦/点别处  → 也提交（DuiPopupHost 失焦自动关闭，关闭原因 LostFocus）。
 *    · ESC          → 取消，不提交（DuiPopupHost 自带的 ESC 关闭，原因 Escape）。
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

namespace balloonwjui { class DuiEdit; }

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
     *    owner：         调用方（主面板）的窗口句柄，仅用于标识来源；本弹层实际以
     *                    NULL 作 owner 创建（历史原因，见 .cpp 里 Show 处的注释）。
     *    currentSign：   当前签名文本，带入编辑框并全选。
     */
    void OpenForSignature(const RECT& signRectScreen, HWND owner, LPCTSTR currentSign);

    // 编辑框回车提交 / ESC 取消。由本弹层的 DUI 通知处理调用，外部不要直接调。
    void OnEditEnter();
    void OnEditEscape();

    BEGIN_MSG_MAP(CSignatureEditPopup)
        MESSAGE_HANDLER(WM_DUI_NOTIFY, OnChildNotify)
        CHAIN_MSG_MAP(balloonwjui::DuiPopupHost)
    END_MSG_MAP()

private:
    /**
     *  内容树里的编辑框发来的 DUI 通知：接回车（提交并关闭）与 ESC（取消）。
     *
     *  判定条件同时比对通知码与控件编号 —— balloonui 的自定义通知码按控件各自
     *  编号，不同控件的同序号自定义码数值相同，只比通知码会把别的控件的通知
     *  一并截住。
     *    uMsg / wParam：未使用。
     *    lParam：balloonwjui::DuiNotify 的地址，仅在本次同步调用期间有效。
     *    bHandled：出参，未匹配时置 FALSE 让消息继续走基类的处理链。
     *    返回：恒为 0。
     */
    LRESULT OnChildNotify(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

    // 首次打开时构建一次内容树（一个单行 DuiEdit）。
    void BuildContentOnce();

    // DuiPopupHost dismiss 回调：失焦关闭→提交；ESC / 程序化关闭→不提交。
    static void OnDismissThunk(void* userdata, balloonwjui::DuiPopupHost::Reason r);
    void        OnDismiss(balloonwjui::DuiPopupHost::Reason r);

    // 读编辑框文本并回吐给调用方；幂等（m_handled 守卫，防回车后失焦重复提交）。
    void DoCommit();

private:
    balloonwjui::DuiEdit* m_edit;        // 单行编辑框（借用，内容树持有）
    bool           m_built;                  // 内容树是否已构建
    bool           m_handled;                // 本次会话是否已提交/取消（防重复提交）
    CommitCallback m_commitCb;               // 提交回调
    void*          m_commitUd;               // 回调用户数据
};
