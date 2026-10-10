/**
 *  白板窗口，ShareWhiteboardWindow.h
 *
 *  录屏时选「白板」打开的一块白板（第 21 期，docs/屏幕共享与录制方案.md 第 2 节第 39、41 条）：
 *    - 窗口上方一行：背景「纯白 / 网格 / 横线」、「工具条」（工具条被关掉后重新显示）与操作提示；
 *    - 下面是白板画布（CShareWhiteboardCanvas）：画布外为浅灰，画布里按背景画线；拖画布的右边、下边或右下角可以扩展画布，
 *      滚轮上下滚动、按住 Shift 滚轮左右滚动；画布上的鼠标动作交给本机标注（工具与屏幕共享相同，只给本人用）；
 *    - 标注工具条浮在窗口下方（放不下时放进窗口底部），跟着窗口移动；Delete 删除选中的对象、Esc 取消选择。
 *  白板自带一份本机标注（CShareLocalAnnotator），线宽按世界与可见区域的比例缩小。录屏控制器只管打开它、按窗口录制它。
 *  第 23 期起还有「房间模式」（OpenShared）：屏幕共享房间里的共享白板，由建立者打开。画的是房间标注会话里这块白板（画面座位）
 *  的笔迹，世界宽高比固定 16:9、画布即世界，按住 Ctrl 滚轮缩放；背景按钮、「工具条」、滚动缩放与关窗都经 kMsgShareBoardCommand
 *  交给屏幕共享控制器，由它广播背景与可见区域、显示标注工具条、在关闭前提示保存。
 *  画布是手绘的 DuiControl 子类，与观看窗的画面控件（CShareVideoCanvas）同一做法：balloonui 没有可以画笔迹、背景线并滚动的控件。
 *
 *  本文件遵循 balloonui 头文件约定：包含它之前调用方须已引入 ATL/WTL（即在 .cpp 里先 #include "stdafx.h"）。
 *
 *  balloonwj@qq.com   2026-10-05
 */
#pragma once

#include "ShareLocalAnnotator.h"
#include "ShareWhiteboard.h"
#include "../../../third-party/balloonui/balloonui/Controls/Window/DuiFrameWindow.h"
#include "../../../third-party/balloonui/balloonui/DuiControl.h"
#include "../../../third-party/balloonui/balloonui/DuiNotify.h"
#include "../../Source/UI/BalloonMain/TitleBarIcon.h"   // ProgramWindowIcons：标题栏程序图标（2026-10-07）

namespace balloonwjui
{
    class DuiButton;
}

namespace screenshare {

//白板窗口与白板画布发给屏幕共享控制器的命令（kMsgShareBoardCommand 的 wParam，第 23 期房间模式）
enum ShareBoardCommand
{
    share_board_cmd_background = 1,   //点了背景按钮：lParam 为新背景（WhiteboardBackground），控制器广播出去
    share_board_cmd_toolbar    = 2,   //点了「工具条」：控制器重新显示标注工具条
    share_board_cmd_view       = 3,   //可见区域变了（滚动、缩放、窗口大小）：控制器广播新的可见区域（限频）
    share_board_cmd_close      = 4,   //点了关闭：控制器提示保存后关闭这块白板
    share_board_cmd_moved      = 5,   //窗口移动或改变大小：控制器把标注工具条挪过来
};

//白板画布：画背景、笔迹与选择提示，把鼠标动作交给本机标注
class CShareWhiteboardCanvas : public balloonwjui::DuiControl
{
public:
    CShareWhiteboardCanvas();

    /**
     *  标注来源与鼠标动作的接收方。
     *  @param session 本机标注的会话（不拥有）
     *  @param input   鼠标动作的接收方（不拥有）
     */
    void SetAnnotation(const ShareAnnotationSession* session, IShareAnnotationInput* input);

    //背景，取值见 WhiteboardBackground
    void SetBackground(int background);
    int Background() const;

    //视图（世界、画布、滚动）
    const ShareWhiteboardView& View() const;

    /**
     *  房间模式（第 23 期）：画标注会话里这块白板的笔迹（带别人的画笔位置），世界 16:9、画布即世界、按住 Ctrl 滚轮缩放；
     *  交出鼠标动作之前经 OnAnnotCanvas 告诉接收方画在这块白板上。在第一次排版之前调用。
     *  @param canvas    白板的画面座位
     *  @param notifyWnd 可见区域变了（滚动、缩放、尺寸）时向它投 kMsgShareBoardCommand（share_board_cmd_view）；可为 NULL
     */
    void SetShared(int canvas, HWND notifyWnd);

    virtual void OnPaint(HDC hdc, const RECT& rcDirty) override;
    virtual bool OnLButtonDown(POINT pt, UINT mkFlags) override;
    virtual bool OnMouseMove(POINT pt, UINT mkFlags) override;
    virtual bool OnLButtonUp(POINT pt, UINT mkFlags) override;
    //双击左键或按下右键：选择工具下改文字，其余完成折线、曲线
    virtual bool OnLButtonDblClk(POINT pt, UINT mkFlags) override;
    virtual bool OnRButtonDown(POINT pt, UINT mkFlags) override;
    //滚轮：上下滚动，按住 Shift 时左右滚动
    virtual bool OnMouseWheel(POINT pt, short zDelta, UINT mkFlags) override;
    virtual bool OnSetCursor(POINT pt) override;

private:
    //控件尺寸变了：初始化或更新视图
    void syncViewSize();
    //控件坐标（宿主客户区）换成可见区域坐标
    POINT toView(POINT pt) const;
    //可见区域里的一点是否在画布上
    bool inCanvas(POINT view) const;
    //把鼠标动作换成归一化坐标交给本机标注
    void deliver(int phase, POINT pt);
    //房间模式：可见区域变了，告诉控制器
    void notifyViewChanged();

    ShareWhiteboardView           m_view;        //世界、画布与滚动
    bool                          m_viewReady;   //视图是否已按控件尺寸初始化
    const ShareAnnotationSession* m_session;     //本机标注的会话（不拥有）
    IShareAnnotationInput*        m_input;       //鼠标动作的接收方（不拥有）
    int                           m_background;  //背景，取值见 WhiteboardBackground
    bool                          m_pressed;     //是否按着左键在画
    int                           m_dragEdge;    //正在拖的画布边，取值见 WhiteboardEdge
    bool                          m_shared;      //是否房间模式（第 23 期）
    int                           m_canvasSeat;  //房间模式下这块白板的画面座位
    HWND                          m_notifyWnd;   //房间模式下可见区域变化的通知窗口（不拥有）；为 NULL 时不通知
};

//白板窗口
class CShareWhiteboardWindow : public balloonwjui::DuiFrameWindow, private IShareLocalAnnotatorListener
{
public:
    CShareWhiteboardWindow();
    ~CShareWhiteboardWindow();

    /**
     *  打开白板：在 anchor 所在显示器的工作区居中，大小约为工作区的七成；开始本机标注并显示标注工具条。已打开时提到前面。
     *  @param anchor 定位用的窗口（exe 主窗口），只用来确定显示器、不作为所有者；可以为 NULL
     *  @return 打开成功返回 true
     */
    bool Open(HWND anchor);

    //关闭白板（工具条、输入框一并关闭，笔迹不保留）；可重复调用
    void Close();

    /**
     *  以房间模式打开（第 23 期）：屏幕共享房间里本人建的共享白板。位置与大小同 Open；不带本机标注，鼠标动作交给 input，
     *  背景按钮、「工具条」、滚动缩放、移动与关窗都向 notifyWnd 投 kMsgShareBoardCommand。已打开时提到前面。
     *  @param anchor    定位用的窗口，只用来确定显示器；可以为 NULL
     *  @param session   房间的标注会话（不拥有），画它里面这块白板的笔迹
     *  @param input     鼠标动作与按键的接收方（屏幕共享控制器，不拥有）
     *  @param canvas    白板的画面座位
     *  @param notifyWnd 命令的接收窗口（不拥有）
     *  @return 打开成功返回 true
     */
    bool OpenShared(HWND anchor, const ShareAnnotationSession* session, IShareAnnotationInput* input, int canvas,
                    HWND notifyWnd);

    //是否开着（房间模式与本机模式都算）
    bool IsOpen() const;

    //当前背景，取值见 WhiteboardBackground；没打开时为网格
    int Background() const;

    /**
     *  房间模式下可见区域在世界里的位置（按世界归一化），控制器据此广播「白板视野」。
     *  @return 打开着且已排版返回 true
     */
    bool VisibleRegion(int& left, int& top, int& width) const;

    //笔迹或选择有变化（房间模式由控制器调用）：重画画布
    void Redraw();

    //窗口在屏幕上的矩形（标注工具条的参照）；没打开时为空矩形
    RECT WindowRect() const;

    BEGIN_MSG_MAP(CShareWhiteboardWindow)
        MESSAGE_HANDLER(WM_DUI_NOTIFY, OnDuiNotify)
        MESSAGE_HANDLER(WM_KEYDOWN, OnKeyDown)
        MESSAGE_HANDLER(WM_MOVE, OnMoveOrSize)
        MESSAGE_HANDLER(WM_SIZE, OnMoveOrSize)
        MESSAGE_HANDLER(WM_CLOSE, OnCloseMsg)
        CHAIN_MSG_MAP(balloonwjui::DuiFrameWindow)
    END_MSG_MAP()

private:
    //按钮的控件 id（避开 DuiFrameWindow 标题栏保留的 1 / 2 / 3）
    enum
    {
        kIdPlain   = 10,   //背景：纯白
        kIdGrid    = 11,   //背景：网格
        kIdLines   = 12,   //背景：横线
        kIdToolbar = 13,   //重新显示标注工具条
    };

    //IShareLocalAnnotatorListener：笔迹或选择有变化，重画画布
    virtual void OnLocalAnnotationsChanged() override;
    //IShareLocalAnnotatorListener：工具条上点了「退出标注」，隐藏工具条（可从「工具条」按钮重新显示）
    virtual void OnLocalAnnotatorExit() override;

    LRESULT OnDuiNotify(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnKeyDown(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnMoveOrSize(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnCloseMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    void BuildUi();
    //背景按钮的选中样式
    void refreshBackgroundButtons();
    //窗口在屏幕上的矩形（工具条的参照）
    RECT windowRect() const;
    //建窗口、摆位置并搭界面（Open 与 OpenShared 共用）
    bool createFrame(HWND anchor);
    //房间模式：向控制器投一条命令
    void postBoardCommand(int command, LPARAM value);

    CShareLocalAnnotator     m_annot;           //本机标注（会话、工具条、命令处理）；房间模式下不用
    bool                     m_shared;          //是否房间模式（第 23 期）
    HWND                     m_notifyWnd;       //房间模式下命令的接收窗口（不拥有）
    IShareAnnotationInput*   m_sharedInput;     //房间模式下按键的接收方（不拥有）
    CShareWhiteboardCanvas*  m_canvas;          //白板画布（控件树持有，本指针仅引用）
    balloonwjui::DuiButton*  m_bgButtons[kWhiteboardBackgroundCount];   //背景按钮，按 WhiteboardBackground 排（同上）
    balloonmain::ProgramWindowIcons m_programIcons;   //标题栏（与任务栏）的程序图标（2026-10-07）；本类持有，析构时释放
};

} // namespace screenshare
