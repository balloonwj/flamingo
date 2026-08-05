/**
 *  通话窗视频界面（调试入口）实现，CallVideoUiDebug.cpp
 *  balloonwj@qq.com   2026-06-16
 */
#include "stdafx.h"
#include "CallVideoUiDebug.h"
#include "BalloonCallDlg.h"
#include "CallVideoEngine.h"
#include "CallVideoDisplaySink.h"

#include "../IULog.h"

namespace call {

//目检用视频编码参数（同真实通话）。
static const int kUiVideoFps     = 30;
static const int kUiVideoBitrate = 2000000;
//消息循环空闲时的让出时长（毫秒）：无消息时短睡避免忙等，30fps 帧消息会被及时泵出。
static const DWORD kIdleSleepMs  = 5;

//适配 sink：把本机 CallVideoEngine 接到通话窗——本端采集帧喂本地预览，编码帧本机回灌解码，
//解出帧喂远端画面区。于是单机即可同时看到本地预览与「自采自解」的远端画面。
class CallVideoUiLoopbackSink : public CallVideoEngineSink
{
public:
    CallVideoUiLoopbackSink(CallVideoEngine* engine, CallVideoDisplaySink* dlgSink)
        : m_engine(engine)
        , m_dlgSink(dlgSink)
    {
    }

    virtual void OnEncodedVideo(const uint8_t* data, size_t len, bool /*keyframe*/, int64_t ptsHns) override
    {
        //采集线程：编码帧本机回灌解码（同 --videoloop），解出帧经 OnRemoteVideoFrame 交付
        if (m_engine != NULL)
            m_engine->OnRemoteEncodedVideo(data, len, ptsHns);
    }

    virtual void OnLocalVideoFrame(const uint8_t* nv12, int width, int height, int stride) override
    {
        //采集线程：本端采集帧 → 通话窗本地预览小窗
        if (m_dlgSink != NULL)
            m_dlgSink->OnLocalVideoFrame(nv12, width, height, stride);
    }

    virtual void OnRemoteVideoFrame(const uint8_t* nv12, int width, int height, int stride) override
    {
        //解码线程（本回环里即采集线程）：解出帧 → 通话窗远端大画面区
        if (m_dlgSink != NULL)
            m_dlgSink->OnRemoteVideoFrame(nv12, width, height, stride);
    }

private:
    CallVideoEngine*      m_engine;    //回灌目标引擎（不拥有）
    CallVideoDisplaySink* m_dlgSink;   //通话窗显示去向（不拥有）
};

bool RunCallVideoUiDebug()
{
    //以「视频通话中」形态打开通话窗（须先 SetVideoCall 再 OpenConnected）
    CBalloonCallDlg dlg;
    dlg.SetVideoCall(true);
    dlg.OpenConnected(NULL);
    if (!dlg.IsWindow())
    {
        ::MessageBox(NULL, _T("打开视频通话窗失败，详见日志。"),
                     _T("视频界面目检"), MB_OK | MB_ICONWARNING);
        return false;
    }

    //起本机视频引擎，并把通话窗作为本地 / 远端帧的显示去向（经回环 sink）
    CallVideoEngine engine;
    CallVideoUiLoopbackSink sink(&engine, dlg.GetVideoDisplaySink());
    if (!engine.Start(&sink, kUiVideoFps, kUiVideoBitrate))
    {
        ::MessageBox(dlg.m_hWnd, _T("摄像头打开或编解码初始化失败，详见日志。"),
                     _T("视频界面目检"), MB_OK | MB_ICONWARNING);
        if (dlg.IsWindow())
            dlg.DestroyWindow();
        return false;
    }

    LOG_INFO("CallVideoUi: running, %dx%d.", engine.Width(), engine.Height());

    //嵌套消息循环：泵消息直到窗口关闭。DuiFrameWindow 的 WM_CLOSE→DestroyWindow 不一定
    //PostQuitMessage，故以 IsWindow 轮询为退出判据；无消息时短睡让出 CPU。
    MSG msg;
    while (dlg.IsWindow())
    {
        while (::PeekMessage(&msg, NULL, 0, 0, PM_REMOVE))
        {
            ::TranslateMessage(&msg);
            ::DispatchMessage(&msg);
        }
        if (!dlg.IsWindow())
            break;
        ::Sleep(kIdleSleepMs);
    }

    //先停引擎（join 采集线程→不再有帧投到已销毁窗口），再随函数返回析构通话窗对象
    engine.Stop();
    return true;
}

} // namespace call
