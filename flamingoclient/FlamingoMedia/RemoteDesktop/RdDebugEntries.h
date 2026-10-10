/**
 *  远程桌面的命令行调试入口（只在 Debug 构建中真正做事）。
 *
 *  迁入 FlamingoMedia.dll 之前这 8 个入口写在 exe 的 App/DebugEntryPoints.cpp 里；迁入后由
 *  IMediaModule::RunDebugEntry 调用本类的 Run，exe 在检查自己的调试入口之前先交给模块。用法与迁移前相同：
 *    --rddlg [--calling]  弹出远程桌面会话对话框（假数据）；默认被控端授权形态，带 --calling 为发起方形态
 *    --rdpselftest        远程桌面引擎自环测试窗口（抓本机屏 → 编码 → 解码 → 绘制）
 *    --rdrelayselftest    进程内开两条中转连接连本机 imgserver（127.0.0.1:20003）互发字节
 *    --rdbar              在屏幕顶部弹出被控端受控提示条，点「结束控制」关掉它
 *    --rdtoolbar          在主显示器顶部弹出控制端全屏工具条
 *    --dxgicap            用 DXGI 抓一帧屏幕存成 exe 同目录的 dxgicap.bmp
 *    --dxgipipe           本地走通整条抓屏链路，还原画面存成 dxgipipe.bmp
 *    --dxgipipegdi        同上但强制走 GDI 降级后端，存成 dxgipipegdi.bmp
 *
 *  与通话的调试入口不同，这里有 4 个入口留下预览窗口（会话对话框、自环测试窗、受控提示条、全屏工具条）。
 *  这些窗口归本类持有，模块关闭时由 DestroyWindows 统一销毁：它们的窗口过程都在本 DLL 里，DLL 卸载后
 *  窗口不能再存活；ATL 窗口对象析构时仍持有 HWND 也会触发断言。
 *
 *  本类自己是一个仅接收消息的窗口：--rdbar 的受控提示条把「结束控制」投到这里，由本类关掉提示条
 *  （迁移前由 exe 的远程桌面协调器在主面板上处理同一条消息）。
 *
 *  包含本头文件之前调用方须已引入 ATL / WTL（即在 .cpp 里先 #include "stdafx.h"）。
 *
 *  balloonwj@qq.com   2026-10-02
 */
#pragma once

#include "BalloonRemoteDesktopDlg.h"
#include "RdSelfTestWindow.h"
#include "RdControlledBar.h"
#include "RdViewerToolbar.h"
#include "RdMessages.h"
#include "../Api/FlamingoMediaApi.h"

namespace remotedesktop {

class CRdDebugEntries : public CWindowImpl<CRdDebugEntries, CWindow, CWinTraits<0, 0> >
{
public:
    DECLARE_WND_CLASS(_T("FlamingoMediaRdDebugEntries"))

    CRdDebugEntries();

    //析构时销毁全部预览窗口（等同 DestroyWindows）
    ~CRdDebugEntries();

    CRdDebugEntries(const CRdDebugEntries&) = delete;
    CRdDebugEntries& operator=(const CRdDebugEntries&) = delete;

    /**
     *  按命令行运行远程桌面的调试入口。Release 构建恒返回 media_debug_not_handled。
     *  @param commandLine 进程命令行（UTF-16），为 NULL 时返回未处理
     *  @param hwndHost    宿主窗口（结果消息框的属主、预览窗口的居中参照）
     *  @param host        宿主接口（中转自测取服务端公钥用），可为 NULL（此时中转自测按明文连接）
     *  @return 结果，取值见 flamingomedia::MediaDebugEntryResult
     */
    int Run(const wchar_t* commandLine, HWND hwndHost, flamingomedia::IMediaHost* host);

    //销毁全部预览窗口与本类的消息窗口。模块关闭时调用；没有窗口时调用也安全。
    void DestroyWindows();

    BEGIN_MSG_MAP(CRdDebugEntries)
        MESSAGE_HANDLER(kMsgRdControlledEndByUser, OnControlledEndByUserMsg)   //--rdbar 的提示条上点了「结束控制」
    END_MSG_MAP()

private:
    //--rdbar 的提示条上点了「结束控制」：关掉提示条
    LRESULT OnControlledEndByUserMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

    CBalloonRemoteDesktopDlg m_rdDlg;            //--rddlg：会话对话框预览（假数据，不登记操作接收方，按钮只关窗）
    CRdSelfTestWindow        m_selfTestWindow;   //--rdpselftest：引擎自环测试窗（原生 Win32 窗口）
    CRdControlledBar         m_controlledBar;    //--rdbar：受控提示条预览
    CRdViewerToolbar         m_toolbar;          //--rdtoolbar：全屏工具条预览
};

} // namespace remotedesktop
