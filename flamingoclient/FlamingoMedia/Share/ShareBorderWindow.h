/**
 *  屏幕共享发起人本机的醒目边框，ShareBorderWindow.h
 *
 *  共享开始后，在被共享的显示器或窗口四周画一圈醒目的绿色边框，提醒发起人「这一块正在被别人看」
 *  （docs/屏幕共享与录制方案.md 5.2 第 2 步）。边框窗口本身：
 *    - 颜色键分层窗口（SetLayeredWindowAttributes + LWA_COLORKEY）：中间涂成透明色键，只有四条边可见；鼠标点击穿透
 *      （WS_EX_TRANSPARENT），不抢焦点、不进任务栏、置顶；
 *    - 排除在一切屏幕采集之外（ExcludeWindowFromCapture），不会被采进共享画面。
 *  不用 UpdateLayeredWindow 逐像素透明的原因：那种分层窗口设置 WDA_EXCLUDEFROMCAPTURE 一律失败（错误码 8），边框会被
 *  采进共享整块显示器的画面；颜色键分层窗口则可以排除（据实证：Source/Tests/ShareSourcePolicyTests.cpp 的 SSSP5，
 *  2026-10-03 在 Win10 19045 上对比过几种窗口形态）。
 *  共享整块显示器时边框画在显示器边缘以内；共享窗口时画在窗口可见外框外侧，并由控制器定时调用 Follow 跟随窗口移动、
 *  缩放，窗口最小化时隐藏。
 *
 *  这不是 balloonui 控件，而是一个独立的 Win32 透明覆盖窗口（balloonui 没有分层透明窗口一类的能力，
 *  也不需要任何控件），所以直接用 ATL 的 CWindowImpl。
 *
 *  典型用法（UI 线程）：
 *      m_border.Show(rect, true);
 *      m_border.Follow(newRect, false);
 *      m_border.Hide();
 *
 *  balloonwj@qq.com   2026-10-02
 */
#pragma once

#include <atlbase.h>
#include <atlwin.h>

namespace screenshare {

//被共享内容四周的边框窗口
class CShareBorderWindow : public CWindowImpl<CShareBorderWindow>
{
public:
    DECLARE_WND_CLASS_EX(_T("FlamingoShareBorder"), 0, -1)

    CShareBorderWindow();
    ~CShareBorderWindow();

    /**
     *  显示边框。
     *  @param rect        被共享内容的矩形（虚拟桌面坐标，物理像素）
     *  @param insideEdges true：边框画在矩形以内（共享整块显示器，外侧已是屏幕之外）；false：画在矩形外侧（共享窗口）
     */
    void Show(const RECT& rect, bool insideEdges);

    /**
     *  跟随被共享内容：矩形变了就重画，hidden 为 true 时隐藏（窗口最小化）。
     */
    void Follow(const RECT& rect, bool hidden);

    //隐藏并销毁边框窗口；可重复调用
    void Hide();

    BEGIN_MSG_MAP(CShareBorderWindow)
        MESSAGE_HANDLER(WM_NCHITTEST, OnNcHitTest)
        MESSAGE_HANDLER(WM_ERASEBKGND, OnEraseBkgnd)
        MESSAGE_HANDLER(WM_PAINT, OnPaint)
    END_MSG_MAP()

private:
    //点击一律穿透到下面的窗口
    LRESULT OnNcHitTest(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    //不擦背景：整个客户区在 WM_PAINT 里画
    LRESULT OnEraseBkgnd(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    //中间涂透明色键，四条边涂边框颜色
    LRESULT OnPaint(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

    //按当前矩形摆放窗口并重画
    void redraw();

    RECT m_rect;          //被共享内容的矩形
    bool m_insideEdges;   //边框画在矩形以内还是外侧
    bool m_hidden;        //是否因窗口最小化而隐藏
};

} // namespace screenshare
