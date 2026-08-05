/**
 *  远程桌面会话提示对话框，BalloonRemoteDesktopDlg.h
 *
 *  基于 balloonui DuiFrameWindow 的非模态小对话框，一个类两种用途（由 Mode 区分）：
 *    - kModeCalling（发起方）：显示"正在呼叫 XXX…"，底部一个"取消"按钮；点取消
 *      向服务端发 cmd_rd_cancel 撤回邀请。
 *    - kModeAuth（被控端）：显示"XXX 请求远程控制你的电脑"，底部"拒绝 / 接受"两个
 *      按钮；点击向服务端发 cmd_rd_response（accept 0 / 1）。
 *
 *  本对话框只负责"显示 + 把用户的取消 / 接受 / 拒绝转成一条 v2 远程桌面信令发出"，
 *  通过 CIUSocket 单例发送；不持有会话状态机（那在 CMainDlg 的远程桌面协调逻辑里）。
 *  协调器在收到服务端应答 / 推送（邀请应答 / 中转参数 / 状态变更）时调用 CloseDlg
 *  主动关闭本对话框，并在邀请应答回来后用 SetSessionId 回填会话 id（发起方取消时用）。
 *
 *  仿 CBalloonClosePromptDlg 实现（DuiFrameWindow + 程序化控件树 + OnDuiNotify 派发）。
 *  本文件遵循 balloonui 头文件约定：包含它之前调用方须已引入 ATL/WTL。
 *
 *  balloonwj@qq.com   2026-06-09
 */
#pragma once

#include <string>
#include "../../balloonui/balloonui/Controls/Window/DuiFrameWindow.h"
#include "../../balloonui/balloonui/Controls/Basic/DuiButton.h"
#include "../../balloonui/balloonui/Controls/Basic/DuiLabel.h"
#include "../../balloonui/balloonui/DuiNotify.h"

class CBalloonRemoteDesktopDlg : public balloonwjui::DuiFrameWindow
{
public:
    //对话框用途
    enum Mode
    {
        kModeCalling = 0,   //发起方：正在呼叫，等待对方接受
        kModeAuth    = 1,   //被控端：收到邀请，等待本人授权
    };

    CBalloonRemoteDesktopDlg();
    ~CBalloonRemoteDesktopDlg();

    /**
     *  以"发起方呼叫中"形态显示（已显示则更新内容并前置）。
     *  @param owner      owner 窗口（一般为主面板，居中参照）
     *  @param peerName   被控端昵称（显示在提示文字里）
     *  @param peerUserId 被控端 userid
     */
    void OpenCalling(HWND owner, const CString& peerName, int64_t peerUserId);

    /**
     *  以"被控端授权请求"形态显示（已显示则更新内容并前置）。
     *  @param owner      owner 窗口
     *  @param fromName   控制端昵称
     *  @param sessionId  会话 id（应答时原样回带）
     *  @param fromUserId 控制端 userid
     */
    void OpenAuth(HWND owner, const CString& fromName, const std::string& sessionId, int64_t fromUserId);

    //协调器主动关闭对话框（收到中转参数 / 状态变更时）。未显示则无操作。
    void CloseDlg();

    //协调器在邀请应答回来后回填会话 id（发起方取消时要带上它发 cmd_rd_cancel）。
    void SetSessionId(const std::string& sessionId) { m_sessionId = sessionId; }
    const std::string& GetSessionId() const { return m_sessionId; }
    int GetMode() const { return m_mode; }

    BEGIN_MSG_MAP(CBalloonRemoteDesktopDlg)
        MESSAGE_HANDLER(WM_CLOSE, OnCloseMsg)
        MESSAGE_HANDLER(WM_DUI_NOTIFY, OnDuiNotify)
        CHAIN_MSG_MAP(balloonwjui::DuiFrameWindow)
    END_MSG_MAP()

private:
    LRESULT OnCloseMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnDuiNotify(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

    //确保窗口已创建（首次调用时 Create + 构建控件树）。
    void EnsureWindow(HWND owner);
    //构建一次控件树（提示文字 + 底部两按钮）。
    void BuildUiOnce();
    //据当前 m_mode 设置提示文字与两个按钮的文案 / 可见性。
    void ApplyMode();

private:
    int         m_mode;         //对话框用途，见 enum Mode
    std::string m_sessionId;    //会话 id
    int64_t     m_peerUserId;   //对端 userid（calling=被控端，auth=控制端）
    CString     m_peerName;     //对端昵称（提示文字用）
    bool        m_built;        //控件树是否已构建

    balloonwjui::DuiLabel*  m_msgLabel;   //主提示文字（切模式时改）
    balloonwjui::DuiButton* m_btnLeft;    //次按钮：auth=拒绝；calling 隐藏
    balloonwjui::DuiButton* m_btnRight;   //主按钮：auth=接受；calling=取消
};
