/**
 *  录制中的摄像头小窗，RecordCameraWindow.h
 *
 *  2026-10-05 用户要求：录制中可以随时打开摄像头，打开后屏幕上出现摄像头画面，小窗可以拖动；录像里的画中画按小窗在被录
 *  内容里的位置与大小合成（所见即所得，换算见 RecordPolicy.h 的 MapPipWindowToContent）。小窗本身排除在屏幕采集之外，
 *  录整屏或框选区域时不会被录进去两遍。
 *
 *  形态：置顶、不进任务栏、不抢焦点的无边框小窗，外圈一道白边；整个窗口都可以按住拖动（WM_NCHITTEST 回 HTCAPTION）。
 *  画面由定时器每隔 kRecordCameraFrameMs 向画面来源取一帧，有新帧时重画；还没有画面时写「摄像头」。
 *  位置变了（拖动、程序挪动）在 UI 线程上回调 IRecordCameraWindowSink，不占用窗口消息号。
 *
 *  典型用法（录屏控制器，UI 线程）：
 *      m_cameraWindow.Show(DefaultPipWindowRect(recorded), &m_camera, this);
 *      ... OnCameraWindowMoved(rect) 里按 MapPipWindowToContent 重算画中画摆放 ...
 *      m_cameraWindow.Hide();
 *
 *  balloonwj@qq.com   2026-10-05
 */
#pragma once

#include <atlbase.h>
#include <atlwin.h>
#include <stdint.h>
#include <vector>
#include "RecordSession.h"   // IRecordVideoSource

namespace screenrecord {

//摄像头小窗取帧的间隔（毫秒）：约 15 帧每秒，预览够用
const UINT kRecordCameraFrameMs = 66;

//摄像头小窗位置变化的接收方（UI 线程上回调）
class IRecordCameraWindowSink
{
public:
    virtual ~IRecordCameraWindowSink()
    {
    }

    /**
     *  小窗的位置或大小变了。
     *  @param screenRect 小窗新的屏幕矩形
     */
    virtual void OnCameraWindowMoved(const RECT& screenRect) = 0;
};

//录制中的摄像头小窗
class CRecordCameraWindow : public CWindowImpl<CRecordCameraWindow, CWindow, CWinTraits<WS_POPUP, 0> >
{
public:
    DECLARE_WND_CLASS_EX(_T("FlamingoRecordCamera"), 0, -1)

    CRecordCameraWindow();
    ~CRecordCameraWindow();

    /**
     *  显示（已显示时挪到新位置）。
     *  @param screenRect 屏幕矩形
     *  @param source     画面来源（摄像头），不拥有，Hide 之前须一直有效
     *  @param sink       位置变化的接收方，不拥有，可为 NULL
     *  @return 窗口建好并显示返回 true
     */
    bool Show(const RECT& screenRect, IRecordVideoSource* source, IRecordCameraWindowSink* sink);

    //关闭；可重复调用
    void Hide();

    //是否显示着
    bool IsShown() const;

    //当前的屏幕矩形；没显示时为空矩形
    RECT ScreenRect() const;

    BEGIN_MSG_MAP(CRecordCameraWindow)
        MESSAGE_HANDLER(WM_PAINT, OnPaint)
        MESSAGE_HANDLER(WM_ERASEBKGND, OnEraseBkgnd)
        MESSAGE_HANDLER(WM_NCHITTEST, OnNcHitTest)
        MESSAGE_HANDLER(WM_MOVE, OnMove)
        MESSAGE_HANDLER(WM_TIMER, OnTimer)
        MESSAGE_HANDLER(WM_MOUSEACTIVATE, OnMouseActivate)
    END_MSG_MAP()

private:
    LRESULT OnPaint(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnEraseBkgnd(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    //整个窗口都当标题栏：按住任意位置都能拖动
    LRESULT OnNcHitTest(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    //位置变了：通知接收方
    LRESULT OnMove(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    //取帧定时器：有新帧时重画
    LRESULT OnTimer(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    //点击时不激活，不抢走正在录的程序的焦点
    LRESULT OnMouseActivate(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

    IRecordVideoSource*      m_source;   //画面来源（不拥有）
    IRecordCameraWindowSink* m_sink;     //位置变化的接收方（不拥有）
    std::vector<uint8_t>     m_frame;    //最近一帧：自上而下的 BGRA
    int                      m_frameW;   //最近一帧的宽，像素；还没有画面时为 0
    int                      m_frameH;   //最近一帧的高
    uint64_t                 m_serial;   //最近一帧的序号，用来判断有没有新帧
};

} // namespace screenrecord
