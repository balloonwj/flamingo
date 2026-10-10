/**
 *  主持人看其他共享者屏幕的窗口（第 22 期多人共享，docs/屏幕共享与录制方案.md 第 2 节第 27、28 条），ShareScreensWindow.h
 *
 *  主持人自己在共享时没有观看窗；别人经同意共享屏幕后，主持人的推流管线（它的中转连接也会收到其他共享者的画面）按画面座位解码，
 *  本窗口从它取画面，用与观看窗相同的画面控件（CShareVideoCanvas）按人数并列显示，每块标上共享人名字，双击放大、再双击还原。
 *  用户关窗只是隐藏，画面清单再变化（有人新开始共享）时重新显示。
 *  第 23 期起各块画面叠上它自己的标注，白板也作为一块画面显示（可各自滚动缩放、跟随创建者）。
 *  2026-10-07 起（方案第 14.3 节第 4 条）窗口顶部多一行按钮：「标注」在这里对别人的画面标注（落笔在哪块画面就标在哪块上），
 *  「申请控制」向当前那一块（放大的那块，没有放大时为第一块）的共享人申请控制，对方同意后在这里直接操作他的屏幕。
 *  两个按钮只把命令经 kMsgShareScreensCommand 交给控制器，由控制器决定状态，再经 SetAnnotating / SetControlState 告知本窗口。
 *
 *  典型用法（屏幕共享控制器里）：
 *      m_screensWindow.SetScreens(seats, names);
 *      if (!seats.empty()) m_screensWindow.Open(mainWindow(), &m_streamer, m_hWnd);
 *      ...（推流管线解出新帧，控制器在 UI 线程上）m_screensWindow.OnFeedFrame();
 *
 *  本文件遵循 balloonui 头文件约定：包含它之前调用方须已引入 ATL/WTL（即在 .cpp 里先 #include "stdafx.h"）。
 *
 *  balloonwj@qq.com   2026-10-05
 */
#pragma once

#include <stdint.h>
#include <map>
#include <string>
#include <vector>
#include "ShareScreenFeed.h"
#include "ShareViewerWindow.h"
#include "../../Source/UI/BalloonMain/TitleBarIcon.h"   // ProgramWindowIcons：标题栏程序图标（2026-10-07）

namespace balloonwjui {
class DuiButton;
}

namespace screenshare {

//主持人看其他共享者屏幕的窗口
class CShareScreensWindow : public balloonwjui::DuiFrameWindow
{
public:
    //顶部按钮交给控制器的命令（kMsgShareScreensCommand 的 wParam，2026-10-07）
    enum Command
    {
        kCmdAnnotate = 1,   //点了「标注 / 退出标注」：控制器进入或退出在本窗口里的标注
        kCmdControl  = 2,   //点了「申请控制 / 取消申请 / 结束控制」：控制器按当前状态决定发什么
    };

    //顶部两个按钮的控件 id（派发点击通知时连同 ctrlId 一起判，见开发约定文档「UI 约定」第 4 条；用例据此找到按钮）
    enum ControlId
    {
        kIdAnnotate = 1001,   //「标注 / 退出标注」按钮
        kIdControl  = 1002,   //「申请控制 / 取消申请 / 结束控制」按钮
    };

    CShareScreensWindow();
    ~CShareScreensWindow();

    /**
     *  打开（或提到前面）：在 anchor 所在显示器的工作区居中。
     *  @param anchor    定位用的窗口（exe 主窗口），只用来确定显示器、不作为所有者；可以为 NULL
     *  @param feed      画面来源（主持人的推流管线），不拥有，须在 Close 之前一直有效
     *  @param notifyWnd 顶部按钮的命令发给谁（屏幕共享控制器的消息窗口），不拥有；为 NULL 时按钮不起作用
     */
    void Open(HWND anchor, IShareScreenFeed* feed, HWND notifyWnd);

    //关闭并销毁；可重复调用。标注与控制的状态一并复位（不补发抬起，由控制器先结束控制）
    void Close();

    //窗口是否开着（含被用户关掉之后的隐藏状态为 false）
    bool IsOpen() const;

    /**
     *  画面清单：按顺序摆放，每块标上共享人名字。
     *  @param seats 画面座位，按显示顺序
     *  @param names 与 seats 一一对应的共享人名字
     */
    void SetScreens(const std::vector<int>& seats, const std::vector<std::wstring>& names);

    //画面来源有新帧（UI 线程上调用）：逐块取最新一帧，帧序号变了的才换
    void OnFeedFrame();

    /**
     *  标注来源（第 23 期）：每块画面叠它自己的标注，白板按它的笔迹画出来。不拥有，须在 Close 之前一直有效；可为 NULL。
     */
    void SetAnnotation(const ShareAnnotationSession* session);

    //画面清单里哪几块是白板（第 23 期），见 CShareVideoCanvas::SetBoards
    void SetBoards(const std::vector<int>& seats, const std::vector<int>& ownerSeats);

    //笔迹有变化：重画
    void AnnotationChanged();

    /**
     *  进入或退出在本窗口里的标注（2026-10-07，控制器决定）：画面控件接不接标注输入、「标注」按钮的文字随之变化。
     *  @param on    是否在本窗口里标注
     *  @param input 鼠标动作的接收方（屏幕共享控制器），不拥有；退出时可传 NULL
     */
    void SetAnnotating(bool on, IShareAnnotationInput* input);

    /**
     *  标注模式下的「鼠标」状态（标注工具条最左边的按钮）：画面控件暂不拦截标注输入，标注模式与按钮文字不变。
     *  @param on true 进入「鼠标」状态；false 回到标注
     */
    void SetAnnotPointer(bool on);

    /**
     *  控制键鼠的状态（2026-10-07，控制器决定）：「申请控制」按钮的文字随之变化；离开控制时先补发还按着的按钮与按键的抬起。
     *  进入控制时把键盘焦点切到本窗口。
     *  @param state 取值见 share_viewer_control_state
     */
    void SetControlState(int state);

    //登记控制输入的接收方（屏幕共享控制器）；不拥有，传 NULL 取消登记
    void SetControlInput(IShareControlInput* input);

    /**
     *  被控制的画面：控制中的鼠标按这块画面的矩形换算坐标，它没有显示出来时鼠标不发送（键盘照常发送）。
     *  @param seat 画面座位；-1 表示没有在控制
     */
    void SetControlScreen(int seat);

    //当前那一块的画面座位：放大的那块，没有放大时为第一块；窗口没开时为 -1
    int ActiveScreenSeat() const;

    //当前那一块是不是白板：白板不能申请控制
    bool ActiveIsBoard() const;

    //窗口在屏幕上的矩形（标注工具条的锚点）；窗口不存在时为空矩形
    RECT WindowRect() const;

    BEGIN_MSG_MAP(CShareScreensWindow)
        //控制中：画面上的鼠标与全部键盘消息先交给控制输入；不在控制中时原样放过
        MESSAGE_RANGE_HANDLER(WM_MOUSEFIRST, WM_MOUSELAST, OnControlInputMsg)
        MESSAGE_RANGE_HANDLER(WM_KEYFIRST, WM_KEYLAST, OnControlInputMsg)
        MESSAGE_HANDLER(WM_KILLFOCUS, OnKillFocusMsg)
        MESSAGE_HANDLER(WM_CLOSE, OnCloseMsg)
        MESSAGE_HANDLER(WM_DUI_NOTIFY, OnDuiNotify)
        CHAIN_MSG_MAP(balloonwjui::DuiFrameWindow)
    END_MSG_MAP()

private:
    LRESULT OnCloseMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    //顶部两个按钮被点击：把命令投给控制器；其余通知交给基类
    LRESULT OnDuiNotify(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    //控制中的鼠标与键盘消息：换算后交给控制输入；画面以外的鼠标消息（标题栏、按钮行）照常放过
    LRESULT OnControlInputMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    //失去焦点：控制中补发还按着的按钮与按键的抬起，随后照常交给基类
    LRESULT OnKillFocusMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    //按标注与控制的状态刷新两个按钮的文字
    void refreshButtons();

    IShareScreenFeed*         m_feed;          //画面来源（不拥有）
    HWND                      m_notifyWnd;     //顶部按钮的命令发给谁（不拥有）；为 NULL 时按钮不起作用
    CShareVideoCanvas*        m_canvas;        //画面控件（控件树持有，本指针仅引用）
    balloonwjui::DuiButton*   m_annotBtn;      //「标注 / 退出标注」按钮（控件树持有，本指针仅引用）
    balloonwjui::DuiButton*   m_controlBtn;    //「申请控制 / 取消申请 / 结束控制」按钮（同上）
    std::vector<int>          m_seats;         //画面座位，按显示顺序
    std::vector<std::wstring> m_names;         //与 m_seats 一一对应的共享人名字
    std::map<int, uint64_t>   m_lastSerials;   //各画面座位上次取到的帧序号
    std::vector<uint8_t>      m_scratch;       //取帧用的缓冲（与画面控件交换）
    const ShareAnnotationSession* m_annot;     //标注来源（第 23 期，不拥有）；为 NULL 时不画标注
    std::vector<int>          m_boardSeats;    //白板的座位（第 23 期）
    std::vector<int>          m_boardOwnerSeats;   //与 m_boardSeats 一一对应的建立者成员座位
    bool                      m_annotating;    //是否在本窗口里标注（2026-10-07）；关窗时复位
    bool                      m_annotPointer;  //标注模式下是否处于「鼠标」状态
    IShareAnnotationInput*    m_annotInput;    //标注输入的接收方（不拥有）
    int                       m_controlState;  //控制键鼠的状态（2026-10-07），取值见 share_viewer_control_state
    int                       m_controlScreen; //被控制的画面座位；-1 表示没有在控制
    IShareControlInput*       m_controlInput;  //控制中键鼠输入的接收方（不拥有）
    ShareControlForwarder     m_forwarder;     //控制中把键鼠换算后交给控制输入
    balloonmain::ProgramWindowIcons m_programIcons;   //标题栏（与任务栏）的程序图标（2026-10-07）；本类持有，析构时释放
};

} // namespace screenshare
