/**
 *  发起人本机的标注层，ShareAnnotationOverlay.h
 *
 *  发起人的屏幕正被共享，笔迹不能画进视频，于是在被共享内容的上方盖两个窗口（docs/屏幕共享与录制方案.md 5.9）：
 *    - 显示层：颜色键分层窗口，键色以外的像素就是笔迹；鼠标完全穿透（WS_EX_TRANSPARENT），一直显示；
 *    - 输入层：整窗不透明度为 1 的分层窗口，肉眼看不见，只在发起人进入标注模式时显示，接住鼠标交给标注输入接口。
 *  两个窗口都排除在屏幕采集之外。逐像素透明的分层窗口（UpdateLayeredWindow）设置排除采集会失败（2h 联调实测，
 *  错误码 8），所以显示层原先用颜色键，笔迹边缘只能全透明或全不透明，屏幕上看到的是锯齿（2026-10-05 联调反馈）。
 *  现在显示层优先建成「不要重定向位图」的分层窗口、内容走 DirectComposition（CShareCompositionLayer），笔迹边缘按
 *  逐像素透明度与下层混合，且能排除采集（SSAA1 实测）；系统低于 Windows 8 或任何一步失败时回落到颜色键窗口。
 *
 *  标注输入接口 IShareAnnotationInput 也给观看窗的画面控件用：两者都把鼠标位置换成归一化坐标后交给控制器。
 *
 *  balloonwj@qq.com   2026-10-03
 */
#pragma once

#include <atlbase.h>
#include <atlwin.h>

#include "ShareAnnotation.h"
#include "ShareCompositionLayer.h"

namespace screenshare {

class ShareAnnotationSession;

//鼠标动作
enum AnnotationPointerPhase
{
    annot_pointer_down = 1,   //按下左键
    annot_pointer_move = 2,   //按着左键移动
    annot_pointer_up   = 3,   //松开左键（含失去鼠标捕获）
    annot_pointer_hover  = 4, //没按着时移动（第 19 期）：广播本人的画笔位置，折线与曲线的最后一点跟着走
    annot_pointer_finish = 5, //双击左键或按下右键（第 19 期）：完成正在添加顶点的折线、曲线
    annot_pointer_leave  = 6, //光标离开标注区域（第 19 期）：告诉房间里的人不再显示本人的画笔
};

//标注输入：发起人的输入层与观看窗的画面控件把鼠标动作交给它（UI 线程上调用）
class IShareAnnotationInput
{
public:
    virtual ~IShareAnnotationInput()
    {
    }

    /**
     *  一次鼠标动作。
     *  @param phase     取值见 AnnotationPointerPhase
     *  @param pt        归一化坐标（已夹到共享内容以内）
     *  @param tolerance 橡皮的命中容差（约 8 个显示像素换成的归一化单位）
     *  @param aspect    共享内容显示的宽高比（宽 ÷ 高，第 20 期）：文字的近似尺寸、连接线裁边等按它换算
     *  @param screenPt  屏幕坐标（文字工具据此摆放输入框）
     */
    virtual void OnAnnotPointer(int phase, const AnnotationPoint& pt, int tolerance, double aspect, POINT screenPt) = 0;

    /**
     *  标注模式下按了一个键（第 20 期）：Delete 删除选中的对象，Esc 取消选择。
     *  @param vk 虚拟键码
     *  @return 处理了返回 true（调用方不再做别的处理）；没有选中对象等情况返回 false
     */
    virtual bool OnAnnotKey(UINT vk) = 0;

    /**
     *  当前工具的光标（第 19 期，第 33 条：光标变成当前工具的样子，笔尖带当前颜色）。
     *  @return 光标句柄，归实现方所有、调用方不释放；取不到时返回 NULL，调用方改用十字光标
     */
    virtual HCURSOR AnnotCursor() = 0;

    /**
     *  接下来的鼠标动作落在哪块画面上（第 22 期 22e，多人同时共享时每块画面各有自己的标注）。输入来源在交出按下与悬停
     *  之前调用；同一块画面重复调用不产生效果。只有一块画面的场合（独立录屏的画笔、白板）不需要实现。
     *  @param canvas 画面在中转里的座位
     */
    virtual void OnAnnotCanvas(int canvas)
    {
        (void)canvas;
    }
};

//橡皮命中容差对应的显示像素
const int kAnnotEraserTolerancePx = 8;

//显示层：画笔迹。优先为逐像素透明（DirectComposition），否则为颜色键分层窗口
class CShareAnnotationLayer : public CWindowImpl<CShareAnnotationLayer, CWindow, CWinTraits<WS_POPUP, 0> >
{
public:
    DECLARE_WND_CLASS_EX(_T("FlamingoShareAnnotation"), 0, -1)

    explicit CShareAnnotationLayer(const ShareAnnotationSession* session);

    //画哪块画面的标注（第 22 期 22e）；默认 kAnnotDefaultCanvas
    void SetCanvas(int canvas);

    /**
     *  为已创建的窗口（须带 WS_EX_NOREDIRECTIONBITMAP）接上 DirectComposition，之后按逐像素透明度绘制。
     *  @return 接上了返回 true；失败时窗口里什么也显示不出来，调用方应销毁它、改建颜色键窗口
     */
    bool AttachAlpha();

    //是否为逐像素透明的显示层
    bool IsAlpha() const;

    BEGIN_MSG_MAP(CShareAnnotationLayer)
        MESSAGE_HANDLER(WM_PAINT, OnPaint)
        MESSAGE_HANDLER(WM_ERASEBKGND, OnEraseBkgnd)
        MESSAGE_HANDLER(WM_DESTROY, OnDestroy)
    END_MSG_MAP()

private:
    LRESULT OnPaint(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnEraseBkgnd(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    //窗口销毁前释放 DirectComposition 对象（它们引用着这个窗口）
    LRESULT OnDestroy(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    //逐像素透明时的绘制：笔迹画进全透明的 32 位位图，再整帧交给 DirectComposition
    void paintAlpha(int width, int height);

    const ShareAnnotationSession* m_session;      //笔迹来源（不拥有）
    int                           m_canvas;       //画哪块画面的标注（第 22 期 22e）
    CShareCompositionLayer        m_composition;  //逐像素透明的显示（2026-10-05）；颜色键窗口时不用
};

//输入层：整窗不透明度为 1 的分层窗口，接住鼠标
class CShareAnnotationCatcher : public CWindowImpl<CShareAnnotationCatcher, CWindow, CWinTraits<WS_POPUP, 0> >
{
public:
    //CS_DBLCLKS：双击完成折线、曲线（第 19 期）
    DECLARE_WND_CLASS_EX(_T("FlamingoShareAnnotationInput"), CS_DBLCLKS, COLOR_WINDOWTEXT)

    CShareAnnotationCatcher();

    //鼠标动作的接收方（不拥有）
    void SetInput(IShareAnnotationInput* input);

    //鼠标动作落在哪块画面上（第 22 期 22e），交出动作之前经 OnAnnotCanvas 告诉接收方；默认 kAnnotDefaultCanvas
    void SetCanvas(int canvas);

    BEGIN_MSG_MAP(CShareAnnotationCatcher)
        MESSAGE_HANDLER(WM_LBUTTONDOWN, OnLButtonDown)
        MESSAGE_HANDLER(WM_MOUSEMOVE, OnMouseMove)
        MESSAGE_HANDLER(WM_LBUTTONUP, OnLButtonUp)
        MESSAGE_HANDLER(WM_CAPTURECHANGED, OnCaptureChanged)
        MESSAGE_HANDLER(WM_SETCURSOR, OnSetCursor)
        MESSAGE_HANDLER(WM_MOUSEACTIVATE, OnMouseActivate)
        MESSAGE_HANDLER(WM_LBUTTONDBLCLK, OnFinishClick)
        MESSAGE_HANDLER(WM_RBUTTONDOWN, OnFinishClick)
        MESSAGE_HANDLER(WM_MOUSELEAVE, OnMouseLeave)
    END_MSG_MAP()

private:
    LRESULT OnLButtonDown(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnMouseMove(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnLButtonUp(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnCaptureChanged(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnSetCursor(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnMouseActivate(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    //双击左键或按下右键：完成折线、曲线（第 19 期）
    LRESULT OnFinishClick(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    //光标离开输入层（第 19 期）
    LRESULT OnMouseLeave(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    void deliver(int phase, LPARAM lParam);

    IShareAnnotationInput* m_input;     //鼠标动作的接收方（不拥有）
    bool                   m_pressed;   //是否按着左键
    POINT                  m_last;      //最近一次的客户区坐标（失去捕获时用来收尾）
    bool                   m_tracking;  //是否已请求光标离开通知（TrackMouseEvent），收到 WM_MOUSELEAVE 后复位
    int                    m_canvas;    //鼠标动作落在哪块画面上（第 22 期 22e）
};

//发起人本机的标注层：显示层 + 输入层
class CShareAnnotationOverlay
{
public:
    explicit CShareAnnotationOverlay(const ShareAnnotationSession* session);
    ~CShareAnnotationOverlay();

    /**
     *  显示（或挪到）被共享内容的矩形上方。
     *  @param rect    被共享内容的矩形（屏幕坐标）
     *  @param visible 是否显示（共享的窗口最小化、暂停共享时传 false）
     */
    void Place(const RECT& rect, bool visible);

    /**
     *  进入或退出标注模式：进入时显示输入层接住鼠标（放在置顶窗口的最上面，调用方随后要把控制条等需要点击的
     *  置顶小窗重新提到它上面），退出时隐藏，鼠标回到被共享的程序。
     *  @param input 鼠标动作的接收方，不拥有；退出时可传 NULL
     */
    void SetInputMode(bool on, IShareAnnotationInput* input);

    //笔迹有变化：重画显示层
    void Redraw();

    /**
     *  这层盖在哪块画面上（第 22 期 22e）：显示层只画这块画面的标注，输入层的鼠标动作落在这块画面上。
     *  @param canvas 画面在中转里的座位；默认 kAnnotDefaultCanvas（建房间的主持人的画面）
     */
    void SetCanvas(int canvas);

    //关闭两个窗口；可重复调用
    void Close();

    //当前覆盖的矩形（屏幕坐标）
    const RECT& Rect() const;

    //显示层是否为逐像素透明（DirectComposition，笔迹边缘抗锯齿）；为 false 时是颜色键窗口（系统低于 Windows 8 等情况）
    bool IsAlphaLayer() const;

private:
    void ensureWindows();
    //建逐像素透明的显示层：建窗、接 DirectComposition、排除采集，任何一步失败都销毁窗口并返回 false
    bool createAlphaLayer();
    //建颜色键显示层（逐像素透明建不起来时）
    void createColorKeyLayer();

    CShareAnnotationLayer   m_layer;     //显示层
    CShareAnnotationCatcher m_catcher;   //输入层
    RECT                    m_rect;      //覆盖的矩形
    bool                    m_visible;   //是否显示
    bool                    m_inputOn;   //是否在标注模式
};

} // namespace screenshare
