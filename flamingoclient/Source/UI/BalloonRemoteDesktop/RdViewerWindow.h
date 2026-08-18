/**
 *  远程桌面控制端观看窗，RdViewerWindow.h
 *
 *  控制端在收到中转参数(cmd_rd_relay_info, role=控制端)后用本窗口观看对方屏幕：
 *  基于 balloonui DuiFrameWindow 的可缩放窗口外壳 + 一个自绘画面控件
 *  (RdViewerCanvas)，连上 imgserver 中转通道，把收到的屏幕帧喂给 RdScreenDecoder
 *  解码，并在画面控件里按窗口大小缩放绘制(StretchDIBits)。
 *
 *  线程模型：中转接收线程通过 RdRelayClientHandler::OnRelayEngineFrame 把屏幕帧
 *  回调上来——本类把该帧拷到堆上并 PostMessage 到 UI 线程，由 UI 线程独占地
 *  apply 解码 + 触发画面控件重绘，故 RdScreenDecoder 无需加锁。
 *
 *  本文件遵循 balloonui 头文件约定：包含它之前调用方须已引入 ATL/WTL（即在 .cpp
 *  里先 #include "stdafx.h"）。
 *
 *  balloonwj@qq.com   2026-06-09
 */
#pragma once

#include <string>
#include <vector>
#include "../../Feature/RemoteDesktop/RdRelayClient.h"      // winsock2.h + RdRelayClient / RdRelayClientHandler
#include "../../Feature/RemoteDesktop/RdScreenDecoder.h"     // RdScreenDecoder（含 windows.h）
#include "../../../third-party/balloonui/balloonui/Controls/Window/DuiFrameWindow.h"
#include "../../../third-party/balloonui/balloonui/DuiControl.h"
#include "../../../third-party/balloonui/balloonui/DuiNotify.h"   // WM_DUI_NOTIFY / DuiNotify（全屏按钮点击）
#include "RdViewerToolbar.h"                        // CRdViewerToolbar（m_toolbar 成员需完整类型）

//自绘画面控件：把当前解码画面按本控件矩形缩放绘制；未就绪时填黑底。
class RdViewerCanvas : public balloonwjui::DuiControl
{
public:
    RdViewerCanvas();

    //设置画面解码器（不拥有；生命周期由观看窗保证，长于本控件）。
    void SetDecoder(remotedesktop::RdScreenDecoder* decoder) { m_decoder = decoder; }

    //自绘：就绪则 StretchDIBits 缩放画面，否则黑底。
    virtual void OnPaint(HDC hdc, const RECT& rcDirty) override;

private:
    remotedesktop::RdScreenDecoder* m_decoder;  //画面解码器（不拥有）
};

//控制端观看窗：balloonui 窗口外壳 + 自绘画面控件，连中转收屏幕帧解码显示。
class CRdViewerWindow : public balloonwjui::DuiFrameWindow,
                        public remotedesktop::RdRelayClientHandler
{
public:
    CRdViewerWindow();
    ~CRdViewerWindow();

    /**
     *  启动观看：建窗 + 连中转 + 显示。阻塞完成中转握手后异步收帧显示。
     *  @param ip        中转通道地址（cmd_rd_relay_info 下发）
     *  @param port      中转通道端口（一期 20003）
     *  @param token     relay 令牌（cmd_rd_relay_info 下发，非空）
     *  @param sessionId 会话 id（关窗时发 cmd_rd_close 结束会话用）
     *  @return 建窗并连中转成功返回 true；失败返回 false
     */
    bool Start(const std::string& ip, int port, const std::string& token,
               const std::string& sessionId);

    //关闭观看：断中转连接、关窗。幂等。
    void CloseViewer();

    // ==== RdRelayClientHandler（中转接收线程触发）====

    //收到一帧屏幕引擎帧：拷到堆并 PostMessage 到 UI 线程解码 / 重绘。
    virtual void OnRelayEngineFrame(const unsigned char* data, size_t len) override;

    //中转断开：PostMessage 到 UI 线程提示并关窗。
    virtual void OnRelayClosed() override;

    //本窗口私有消息（WM_APP 起，应用私有、不与系统 / balloonui 冲突）。
    enum
    {
        WM_RD_VIEWER_FRAME       = WM_APP + 1,   //收到屏幕帧（lParam = 堆 std::vector<unsigned char>*）
        WM_RD_VIEWER_CLOSED      = WM_APP + 2,   //中转断开
        WM_RD_VIEWER_TOOLBAR_CMD = WM_APP + 3,   //全屏工具条按钮命令（wParam = CRdViewerToolbar::ToolbarCmd）
    };

    BEGIN_MSG_MAP(CRdViewerWindow)
        MESSAGE_HANDLER(WM_RD_VIEWER_FRAME, OnRdFrameMsg)
        MESSAGE_HANDLER(WM_RD_VIEWER_CLOSED, OnRdClosedMsg)
        MESSAGE_HANDLER(WM_CLOSE, OnCloseMsg)
        MESSAGE_HANDLER(WM_TIMER, OnTimer)
        MESSAGE_HANDLER(WM_DUI_NOTIFY, OnDuiNotify)
        MESSAGE_HANDLER(WM_KEYDOWN, OnKeyDown)
        MESSAGE_HANDLER(WM_KEYUP, OnKeyUp)
        MESSAGE_HANDLER(WM_MOUSEMOVE, OnMouseInput)
        MESSAGE_HANDLER(WM_LBUTTONDOWN, OnMouseInput)
        MESSAGE_HANDLER(WM_LBUTTONUP, OnMouseInput)
        MESSAGE_HANDLER(WM_LBUTTONDBLCLK, OnMouseInput)
        MESSAGE_HANDLER(WM_RBUTTONDOWN, OnMouseInput)
        MESSAGE_HANDLER(WM_RBUTTONUP, OnMouseInput)
        MESSAGE_HANDLER(WM_MBUTTONDOWN, OnMouseInput)
        MESSAGE_HANDLER(WM_MBUTTONUP, OnMouseInput)
        MESSAGE_HANDLER(WM_MOUSEWHEEL, OnMouseInput)
        MESSAGE_HANDLER(WM_RD_VIEWER_TOOLBAR_CMD, OnToolbarCmd)
        CHAIN_MSG_MAP(balloonwjui::DuiFrameWindow)
    END_MSG_MAP()

private:
    //UI 线程：取出堆帧 → 解码 apply → 变化则触发画面控件重绘。
    LRESULT OnRdFrameMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    //UI 线程：中转断开提示并关窗。
    LRESULT OnRdClosedMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    //用户点标题栏关闭：发 cmd_rd_close 结束会话并关窗。
    LRESULT OnCloseMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    //每秒定时：据本秒帧数 / 最近帧大小刷新标题栏的帧率与帧大小。
    LRESULT OnTimer(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    //DUI 通知：标题栏全屏图标按钮点击（DUIFW_CAPTION_ICON_CLICK）→ 切全屏；其余交基类。
    LRESULT OnDuiNotify(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    //键盘按下：F11 切换全屏 / ESC 退出全屏，其余键编码成键盘命令发给被控端。
    LRESULT OnKeyDown(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    //键盘抬起：编码成键盘命令发给被控端（全屏快捷键不转发）。
    LRESULT OnKeyUp(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    //画面区鼠标（移动 / 左右中键 / 双击 / 滚轮）：映射成被控端屏幕坐标并编码发出；
    //旁路不拦截，标题栏按钮 / DuiHost 照常工作。
    LRESULT OnMouseInput(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    //全屏工具条按钮命令：退出全屏 / 断开（wParam = CRdViewerToolbar::ToolbarCmd）。
    LRESULT OnToolbarCmd(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

    //切换全屏并同步全屏工具条：进全屏则在画面顶部显示工具条，出全屏则隐藏。
    void SyncFullscreenToolbar();

    //确保窗口已创建（首次调用 Create + 设外观）。
    void EnsureWindow();
    //构建客户区：一个填满的自绘画面控件。
    void BuildUi();

private:
    remotedesktop::RdRelayClient   m_relay;       //中转连接（控制端这条用于收屏幕帧、发控制命令）
    remotedesktop::RdScreenDecoder m_decoder;     //画面解码器（仅 UI 线程访问，无需加锁）
    RdViewerCanvas*                m_canvas;      //自绘画面控件（SetClientContent 后由控件树持有，本指针仅引用）
    std::string                    m_sessionId;   //会话 id（关窗时发 cmd_rd_close 用）
    int                            m_framesSinceTick;  //本统计周期（1 秒）内收到的帧数，定时器读后清零
    size_t                         m_lastFrameBytes;   //最近一帧引擎帧字节数（标题栏显示帧大小用）
    int                            m_fullscreenIconId; //标题栏全屏按钮的 caption icon id（AddCaptionIcon 返回）
    HBITMAP                        m_fullscreenIcon;   //全屏按钮图标位图（本类生成、析构释放）
    CRdViewerToolbar               m_toolbar;          //全屏态浮于画面顶部的工具条（状态 + 退出全屏 / 断开）
};
