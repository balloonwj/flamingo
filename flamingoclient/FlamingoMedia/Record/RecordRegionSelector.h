/**
 *  独立录屏的框选遮罩，RecordRegionSelector.h
 *
 *  选择窗里点「框选区域」后，在选定的显示器上盖一层半透明的暗色遮罩（docs/屏幕共享与录制方案.md 6.2）：按住左键拖出
 *  要录制的区域，选区内恢复原样、四周画蓝色边框、左上角标出「宽 × 高」；可以重新拖动改选。回车或在选区内双击确认，
 *  Esc 或右键取消。结果经 kMsgRecordRegionResult 投给通知窗口（录屏控制器）。
 *
 *  遮罩是逐像素透明的分层窗口（UpdateLayeredWindow）：选区内的像素不透明度为 1/255（肉眼看不见、仍能接住鼠标），
 *  选区外为半透明黑色。遮罩只在开始录制之前显示，不需要排除在屏幕采集之外。不是 balloonui 控件：整块显示器大小的
 *  逐像素透明窗口 balloonui 没有对应的控件。
 *
 *  balloonwj@qq.com   2026-10-03
 */
#pragma once

#include <atlbase.h>
#include <atlwin.h>
#include <stdint.h>

namespace screenrecord {

//框选遮罩
class CRecordRegionSelector : public CWindowImpl<CRecordRegionSelector, CWindow, CWinTraits<WS_POPUP, 0> >
{
public:
    DECLARE_WND_CLASS_EX(_T("FlamingoRecordRegion"), CS_DBLCLKS, -1)

    CRecordRegionSelector();
    ~CRecordRegionSelector();

    /**
     *  在指定显示器上打开遮罩并取得焦点。
     *  @param monitor   显示器矩形（屏幕坐标）
     *  @param notifyWnd 结果通知窗口，不拥有
     */
    bool Open(const RECT& monitor, HWND notifyWnd);

    //不发结果直接关闭；可重复调用
    void Close();

    BEGIN_MSG_MAP(CRecordRegionSelector)
        MESSAGE_HANDLER(WM_LBUTTONDOWN, OnLButtonDown)
        MESSAGE_HANDLER(WM_MOUSEMOVE, OnMouseMove)
        MESSAGE_HANDLER(WM_LBUTTONUP, OnLButtonUp)
        MESSAGE_HANDLER(WM_LBUTTONDBLCLK, OnLButtonDblClk)
        MESSAGE_HANDLER(WM_RBUTTONUP, OnRButtonUp)
        MESSAGE_HANDLER(WM_KEYDOWN, OnKeyDown)
        MESSAGE_HANDLER(WM_SETCURSOR, OnSetCursor)
    END_MSG_MAP()

private:
    LRESULT OnLButtonDown(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnMouseMove(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnLButtonUp(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnLButtonDblClk(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnRButtonUp(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnKeyDown(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnSetCursor(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

    //按当前选区重画整张遮罩
    void render();
    //发出结果并关闭
    void finish(bool ok);
    POINT toScreen(LPARAM lParam) const;

    RECT     m_monitor;     //显示器矩形（屏幕坐标）
    HWND     m_notifyWnd;   //通知窗口（不拥有）
    RECT     m_selection;   //当前选区（屏幕坐标）；为空表示还没选
    POINT    m_anchor;      //拖动的起点（屏幕坐标）
    bool     m_dragging;    //是否正在拖动
    bool     m_finished;    //本次是否已发过结果
};

} // namespace screenrecord
