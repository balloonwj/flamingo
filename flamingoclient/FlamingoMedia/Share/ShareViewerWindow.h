/**
 *  屏幕共享观看窗，ShareViewerWindow.h
 *
 *  观看者加入共享后打开（docs/屏幕共享与录制方案.md 5.2 第 7 步）：默认全屏，铺满主面板所在的显示器；ESC 退出全屏、
 *  F11 切换；按钮放在一条浮动工具条上（CViewerFloatBar，2026-10-12 起，此前窗口模式放在标题栏上、全屏时借用远程桌面的
 *  CRdViewerToolbar），窗口模式与全屏都是这一条，风格与共享者的控制条一致。客户区是一个
 *  自绘画面控件 CShareVideoCanvas，按窗口大小等比缩放绘制最新一帧，没有画面或对方暂停、最小化、掉线时在画面中央
 *  显示提示文字。收流管线 ShareViewerReceiver 由本窗口持有：中转、解码在它的接收线程上完成，本窗口只在 UI 线程上取
 *  最新一帧绘制。
 *
 *  画面控件是 DuiControl 的子类：balloonui 没有视频画面一类的控件，远程桌面观看窗（RdViewerCanvas）是同样的做法。
 *  画面上叠画房间里的标注（第 6 期）；进入标注模式后画面控件接住鼠标，换成归一化坐标交给标注输入接口（控制器）。
 *
 *  关窗与工具条「离开」不在本窗口里发信令，而是向通知窗口（屏幕共享控制器）投 kMsgShareViewerCommand，由控制器发
 *  离开请求并关闭本窗口。
 *
 *  本文件遵循 balloonui 头文件约定：包含它之前调用方须已引入 ATL/WTL（即在 .cpp 里先 #include "stdafx.h"）。
 *
 *  balloonwj@qq.com   2026-10-02
 */
#pragma once

#include <stdint.h>
#include <atomic>
#include <map>
#include <mutex>
#include <string>
#include <vector>
#include "ShareViewerReceiver.h"
#include "ShareScreenFeed.h"   // IShareScreenFeed：本人那一块的本机预览来源（2026-10-12）
#include "ShareCameraPanel.h"
#include "ShareAnnotationOverlay.h"
#include "ShareInfoPopup.h"
#include "ShareRemoteControl.h"
#include "ShareWhiteboard.h"   // 白板画面的视野（第 23 期）
#include "ShareRoomPolicy.h"   // ShareBoardCloseItem：「主持人 ▾」菜单的「关闭白板 ▸」（2026-10-08）
#include "ViewerFloatBar.h"   // CViewerFloatBar：浮动观看工具条（2026-10-12）
#include "ShareFrameDrawer.h"   // ShareFrameDrawer：缩小显示 2K、4K 画面时不走 HALFTONE（2026-10-15）
#include "ShareBarPopupGuard.h"   // CShareBarPopupGuard：字幕菜单开着时观看窗被关闭（2026-10-07）
#include "../../../third-party/balloonui/balloonui/Controls/Window/DuiFrameWindow.h"
#include "../../../third-party/balloonui/balloonui/DuiControl.h"
#include "../../../third-party/balloonui/balloonui/DuiNotify.h"
#include "../../Source/UI/BalloonMain/TitleBarIcon.h"   // ProgramWindowIcons：标题栏程序图标（2026-10-07）

namespace screenshare {

class ShareAnnotationSession;
class ShareCaptionSink;

//观看者一侧控制键鼠的状态（第 18 期），决定观看窗「申请控制」按钮的文字与是否把键鼠转给控制器
enum share_viewer_control_state
{
    share_viewer_control_none      = 0,   //没有申请也没有控制：按钮为「申请控制」
    share_viewer_control_requested = 1,   //已申请、等画面主人答复：按钮为「取消申请」
    share_viewer_control_active    = 2,   //正在控制：按钮为「结束控制」，画面上的键鼠转给控制器发出去
};

//观看者「共享我的屏幕」按钮的状态（第 22 期多人共享）
enum share_viewer_mine_state
{
    share_viewer_mine_none      = 0,   //没有申请也没在共享：按钮为「共享我的屏幕」，点了向主持人申请
    share_viewer_mine_requested = 1,   //已申请、等主持人答复：按钮为「取消共享申请」
    share_viewer_mine_sharing   = 2,   //正在共享自己的屏幕：按钮为「停止我的共享」
};

//「共享我的屏幕」按钮的状态个数
const int kShareMineStateCount = 3;

//控制中观看窗把换算好的键鼠输入交给它（屏幕共享控制器实现，UI 线程调用）
class IShareControlInput
{
public:
    virtual ~IShareControlInput()
    {
    }

    /**
     *  观看窗在控制中产生了一条输入（已换算成相对画面的归一化坐标）。
     *  @param ev 输入
     */
    virtual void OnControlInput(const ShareInputEvent& ev) = 0;
};

/**
 *  控制中把窗口上的键鼠消息换算成输入、交给控制输入接口（2026-10-07 从观看窗抽出，观看窗与主持人的「其他人的屏幕」窗口
 *  共用）。记下已发出按下、还没发出抬起的鼠标按钮与按键，失去焦点或结束控制时补发抬起；按下鼠标后抓住鼠标，全部按钮
 *  松开后放开。鼠标移动被限频挡下时，用一个以本对象地址为编号的窗口定时器（TIMERPROC 回调，不经窗口的 WM_TIMER 处理）
 *  在间隔到后补发最后的位置（2026-10-17），使用方不需要为此改动消息映射。
 *
 *  典型用法（窗口的键鼠消息处理里）：
 *      if (!m_forwarder.Handle(m_hWnd, uMsg, wParam, lParam, videoRect, m_controlInput)) bHandled = FALSE;
 */
class ShareControlForwarder
{
public:
    ShareControlForwarder();

    //停掉补发定时器：定时器回调按编号找回本对象，不能留到对象销毁之后
    ~ShareControlForwarder();

    /**
     *  处理一条键鼠消息。
     *  @param hwnd   收到消息的窗口：鼠标位置按它的客户区换算，按下鼠标后对它 SetCapture
     *  @param msg    窗口消息（鼠标与键盘范围内的消息）
     *  @param wParam 原始 wParam
     *  @param lParam 原始 lParam（鼠标消息里的位置；滚轮消息为屏幕坐标，内部换算）
     *  @param video  被控制的画面在窗口客户区里的矩形；空矩形表示它没有显示出来，此时只转发键盘
     *  @param input  控制输入的接收方，不拥有；为 NULL 时不处理
     *  @return 拦下了这条消息返回 true（调用方不再交给默认处理）；画面以外、又没有按着按钮的鼠标消息返回 false
     */
    bool Handle(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam, const RECT& video, IShareControlInput* input);

    /**
     *  补发还按着的鼠标按钮与按键的抬起，并清空记录、放开鼠标。结束控制或窗口失去焦点时调用。
     *  @param input 控制输入的接收方，不拥有；为 NULL 时只清空不补发
     */
    void Release(IShareControlInput* input);

    //清空记录并放开鼠标，不补发抬起（窗口关闭、房间已离开时调用）；被限频挡下的移动也不再补发
    void Reset();

private:
    ShareControlForwarder(const ShareControlForwarder&);
    ShareControlForwarder& operator=(const ShareControlForwarder&);

    //补发定时器的回调（界面线程上，由窗口的消息循环派发）；idEvent 是 ShareControlForwarder 对象的地址
    static VOID CALLBACK FlushTimerProc(HWND hwnd, UINT msg, UINT_PTR idEvent, DWORD tick);

    /**
     *  有被限频挡下的移动、补发定时器又没在走时，启动一次（单次，到点后由 onFlushTimer 停掉）。
     *  @param hwnd  定时器所在的窗口（收到消息的窗口）
     *  @param input 补发时的接收方，不拥有
     */
    void armFlushTimer(HWND hwnd, IShareControlInput* input);

    //停掉补发定时器（没有在走时什么都不做）
    void killFlushTimer();

    //补发定时器到点：取出到期的移动交给接收方；还没到期（期间又发出过移动）时重新启动
    void onFlushTimer();

    ShareViewerInputTranslator m_translator;   //把窗口消息换成输入（画面外按下不发、移动限频等）
    std::vector<uint32_t>      m_keysDown;     //已发出按下、还没发出抬起的键
    bool                       m_captured;     //按下鼠标后是否已 SetCapture（全部按钮松开后释放）
    HWND                       m_flushHwnd;    //补发定时器所在的窗口；定时器没在走时为 NULL
    IShareControlInput*        m_flushInput;   //补发时的接收方（最近一次 Handle 传入的），不拥有；定时器没在走时无意义
};

//画面控件：等比缩放绘制最新一帧（黑边留白），有提示文字时在中央叠一块半透明底的文字
class CShareVideoCanvas : public balloonwjui::DuiControl
{
public:
    CShareVideoCanvas();

    /**
     *  换上新的一帧（交换缓冲，不拷贝）。
     *  @param bgra   自上而下的 BGRA，行跨度 = 宽 × 4；调用后内容与本控件原来的缓冲互换
     *  @param width  宽
     *  @param height 高
     */
    void SwapFrame(std::vector<uint8_t>& bgra, int width, int height);

    //设置提示文字；为空时不显示
    void SetStatusText(const std::wstring& text);

    //是否已有画面
    bool HasFrame() const;

    /**
     *  设置收画面的链路实际路径（2026-10-04 起）：直连时在画面左上角叠一枚「P2P」徽章。有变化才重绘。
     *  @param linkPath 取值同 ShareViewerReceiver::LinkPath
     */
    void SetLinkPath(int linkPath);

    //标注来源（屏幕共享控制器的标注会话）；不拥有，为 NULL 时不画标注
    void SetAnnotation(const ShareAnnotationSession* session);

    /**
     *  进入或退出标注模式：进入后在画面上按下、拖动、松开左键都交给 input。
     *  @param input 鼠标动作的接收方，不拥有；退出时可传 NULL
     */
    void SetAnnotating(bool on, IShareAnnotationInput* input);

    //画面最近一次实际画在哪里（宿主客户区坐标，去掉黑边之后）；还没画过时为空矩形。控制键鼠据此换算坐标（第 18 期）。
    //多人共享（第 22 期）时为「当前那一块」（放大的那块，没有放大时为第一块别人的画面）的画面矩形
    RECT VideoRect() const;

    /**
     *  多人共享的画面清单（第 22 期）：按清单顺序摆放，每块标上共享人名字。只有一块时与原来一样铺满。清单里还在的画面保留已有的帧。
     *  @param seats 画面座位，按显示顺序
     *  @param names 与 seats 一一对应的共享人名字
     */
    void SetScreens(const std::vector<int>& seats, const std::vector<std::wstring>& names);

    /**
     *  换上某一块的新一帧（第 22 期，交换缓冲，不拷贝）。只有一块画面时同 SwapFrame。
     *  @return 这个画面座位在清单里返回 true
     */
    bool SwapScreenFrame(int seat, std::vector<uint8_t>& bgra, int width, int height);

    //画面清单里的座位，按显示顺序
    std::vector<int> ScreenSeats() const;

    /**
     *  画面清单是否「有过画面、现在一块都没有了」：经移交成为主持人的人在没有别人共享时，观看窗据此提示
     *  「目前没有其他人在共享屏幕」，而不是一声不响地停在最后一帧。从来没有设过清单（老的单画面路径）时为 false。
     */
    bool ScreensGone() const;

    //当前那一块的画面座位（第 22 期）：放大的那块，没有放大时为第一块别人的画面（2026-10-12 起清单里有本人那一块，
    //见 SetOwnScreen；只有本人那一块时就是它）；没有清单时为 -1
    int ActiveScreenSeat() const;

    /**
     *  标出本人那一块（2026-10-12）：它只能看，没有放大时「当前那一块」跳过它，申请控制、录制默认作用在别人的画面上。
     *  @param seat 本人画面的座位；-1 表示清单里没有本人那一块
     */
    void SetOwnScreen(int seat);

    /**
     *  多块画面时名字标签要避开的窗口（2026-10-13）：观看窗的浮动工具条浮在画面上方，标签与它重叠时画到它下方。
     *  绘制时取它当前的位置（不可见时不避开）。
     *  @param wnd 要避开的窗口对象，不拥有，生命周期须长于本控件；NULL 表示不避开
     */
    void SetLabelObstacle(const ATL::CWindow* wnd);

    //要避开的窗口自上次绘制后挪了位置、改了大小或显隐（如工具条收起）时重画，让标签回到合适的位置；由宿主定时调用
    void RefreshLabelObstacle();

    /**
     *  画面清单里哪几块是白板（第 23 期）：白板没有视频，按标注会话里它的笔迹与最近收到的白板视野（建立者广播的背景与可见区域）
     *  画出来；每块白板各自滚动（滚轮，按住 Shift 左右）与缩放（按住 Ctrl 滚轮），左上角名字标签上点一下切换「跟随创建者」
     *  （默认跟随，自己滚动或缩放后改为自由查看）。在 SetScreens 之后调用。
     *  @param seats      白板的画面座位
     *  @param ownerSeats 与 seats 一一对应的建立者成员座位：只认他发的白板视野
     */
    void SetBoards(const std::vector<int>& seats, const std::vector<int>& ownerSeats);

    //当前那一块是不是白板（第 23 期）：白板不能申请控制
    bool ActiveIsBoard() const;

    /**
     *  把当前那一块白板按它现在的可见区域画成一张图（2026-10-07，方案第 14.1 节 ④-2：观看者录制时当前那块是白板，录像里也要
     *  有画面）。画法与屏幕上相同：白底、背景线、笔迹；尺寸为这一块现在的大小，还没显示过时为 1280×720。只在界面线程上调用。
     *  @param bgra   [出参] 自上而下的 BGRA，不透明，大小为 width × height × 4
     *  @param width  [出参] 图宽（像素）
     *  @param height [出参] 图高（像素）
     *  @return 当前那一块是白板且画成功返回 true；否则返回 false，出参不变
     */
    bool RenderActiveBoard(std::vector<uint8_t>& bgra, int& width, int& height);

    /**
     *  某块画面最近一次实际画在哪里（宿主客户区坐标，第 22 期）。控制键鼠按被控制的那一块换算坐标。
     *  @param seat 画面座位
     *  @param out  [出参] 矩形
     *  @return 这块画面在清单里且已画过返回 true；没有清单时单画面就是要找的那一块
     */
    bool ScreenVideoRect(int seat, RECT& out) const;

    /**
     *  某块画面当前那一帧的尺寸（2026-10-12）。
     *  @param seat   画面座位
     *  @param width  [出参] 帧宽
     *  @param height [出参] 帧高
     *  @return 这块画面在清单里且已有帧返回 true
     */
    bool ScreenFrameSize(int seat, int& width, int& height) const;

    /**
     *  某块画面的名字标签最近一次画在哪里（宿主客户区坐标，2026-10-13，单元测试核对它不被浮动工具条盖住）。
     *  @param seat 画面座位
     *  @param out  [出参] 标签矩形
     *  @return 这块画面在清单里且已画过返回 true
     */
    bool ScreenLabelRect(int seat, RECT& out) const;

    virtual void OnPaint(HDC hdc, const RECT& rcDirty) override;
    virtual bool OnLButtonDown(POINT pt, UINT mkFlags) override;
    virtual bool OnMouseMove(POINT pt, UINT mkFlags) override;
    virtual bool OnLButtonUp(POINT pt, UINT mkFlags) override;
    virtual bool OnSetCursor(POINT pt) override;
    //双击左键：标注模式下完成折线、曲线（第 19 期）；多块画面时放大或还原某一块（第 22 期）；
    //其余情况上报 DUIN_DBLCLK，观看窗据此切换全屏（2026-10-12）
    virtual bool OnLButtonDblClk(POINT pt, UINT mkFlags) override;
    //标注模式下按下右键：完成折线、曲线（第 19 期）
    virtual bool OnRButtonDown(POINT pt, UINT mkFlags) override;
    //光标离开画面控件：标注模式下告诉房间里的人不再显示本人的画笔（第 19 期）
    virtual bool OnMouseLeave() override;
    //滚轮在白板上（第 23 期）：上下滚动，按住 Shift 左右滚动，按住 Ctrl 缩放；之后这块白板改为自由查看
    virtual bool OnMouseWheel(POINT pt, short zDelta, UINT mkFlags) override;

private:
    //多人共享时的一块画面（第 22 期）
    struct ScreenTile
    {
        int                  m_seat;     //画面座位
        std::wstring         m_name;     //共享人名字
        std::vector<uint8_t> m_frame;    //最新一帧（BGRA）
        int                  m_width;    //帧宽
        int                  m_height;   //帧高
        bool                 m_isBoard;  //是不是白板（第 23 期）
        int                  m_ownerSeat;//白板建立者的成员座位（第 23 期）：只认他发的白板视野
        ShareWhiteboardView  m_view;     //白板的视野（第 23 期）：各人各自滚动、缩放
        bool                 m_viewReady;//m_view 是否已按这块的大小初始化
        bool                 m_follow;   //是否跟随创建者的可见区域（第 23 期），默认跟随
        RECT                 m_labelRect;//名字标签最近一次画在哪里（白板在这里点一下切换跟随）

        ScreenTile();
    };

    /**
     *  把一帧等比缩放画进 area，四周黑边。
     *  @return 画面实际所占的矩形
     */
    RECT drawFrameIn(HDC hdc, const RECT& area, const std::vector<uint8_t>& frame, int width, int height);
    //多块画面的绘制（第 22 期）
    void paintScreens(HDC hdc, const RECT& rc);
    //当前那一块在 m_tiles 里的下标；没有清单时为 -1
    int activeTileIndex() const;
    //把鼠标动作换成归一化坐标交给标注输入
    void deliverPointer(int phase, POINT pt);
    /**
     *  画一块白板（第 23 期）：画布外的底色、白色画布、按背景画线；跟随创建者时按他的可见区域显示。
     *  @param area    这块的矩形
     *  @param t       这块（视野在这里初始化与更新）
     *  @param content [出参] 整个世界在控件里的矩形（标注按它换算，可以比 area 大）
     */
    void drawBoardIn(HDC hdc, const RECT& area, ScreenTile& t, RECT& content);
    //名字标签要避开的窗口当前在宿主客户区里的矩形；没有设置、窗口不存在或不可见时为空矩形
    RECT labelObstacleRect() const;
    //点在哪块白板上；不在白板上返回 -1
    int boardTileAt(POINT pt) const;
    /**
     *  鼠标所在的那块画面接标注（第 22 期 22e）：记下它的矩形与画面座位，并经 OnAnnotCanvas 告诉标注输入。
     *  @return 点在某块画面上返回 true
     */
    bool pickAnnotTile(POINT pt);
    //第 index 块的画面座位；没有清单（单画面）时为 kAnnotDefaultCanvas
    int canvasOfTile(int index) const;

    std::vector<uint8_t> m_frame;    //当前画面
    int                  m_width;    //画面宽
    int                  m_height;   //画面高
    ShareFrameDrawer     m_drawer;   //画面的绘制器（2026-10-15）：2K、4K 画面缩小显示时用 SSE2 缩放代替 HALFTONE，单画面与多画面共用
    std::wstring         m_status;   //提示文字
    RECT                 m_videoRect;   //最近一次画面实际画在哪里（控件所在宿主的客户区坐标）
    const ShareAnnotationSession* m_annot;   //标注来源（不拥有）
    IShareAnnotationInput* m_annotInput;     //标注模式下鼠标动作的接收方（不拥有）
    bool                 m_annotating;  //是否在标注模式
    bool                 m_pressed;     //标注模式下是否按着左键
    bool                 m_hovering;    //标注模式下光标是否在画面上（报过悬停、还没报离开，第 19 期）
    int                  m_linkPath;    //收画面的链路实际路径（见 SetLinkPath），观看窗每秒刷新一次
    std::vector<ScreenTile> m_tiles;    //多人共享的画面清单（第 22 期）；不多于一块时按原来的单画面绘制
    int                  m_enlarged;    //放大的那一块在 m_tiles 里的下标（第 22 期）；-1 表示并列
    bool                 m_screensGone; //画面清单有过画面、现在一块都没有（见 ScreensGone）；清单又非空时清除
    int                  m_ownSeat;     //本人那一块的画面座位（2026-10-12，见 SetOwnScreen）；-1 表示没有
    const ATL::CWindow*  m_labelObstacle;   //名字标签要避开的窗口（2026-10-13，见 SetLabelObstacle），不拥有；NULL 表示不避开
    RECT                 m_labelObstacleRect;   //最近一次绘制时要避开的矩形（宿主客户区坐标），空矩形表示没有；RefreshLabelObstacle 据此判断变化
    std::vector<RECT>    m_tileVideoRects;   //各块画面最近一次实际所占的矩形（第 22 期），与 m_tiles 一一对应；单画面时只有一个元素
    RECT                 m_annotRect;   //标注模式下正在接鼠标动作的那块画面的矩形（第 22 期 22e，宿主客户区坐标）；
                                        //按下或悬停时按鼠标所在的画面更新，按着期间不变
    int                  m_annotCanvas; //m_annotRect 那块画面的座位（第 22 期 22e）
    RECT                 m_annotContent;//m_annotRect 那块画面上标注换算用的矩形：画面为画面所占的矩形，白板为整个世界（第 23 期）
    std::vector<RECT>    m_tileContentRects; //各块标注换算用的矩形（第 23 期），与 m_tileVideoRects 一一对应
};

//屏幕共享观看窗
class CShareViewerWindow : public balloonwjui::DuiFrameWindow, private ShareViewerSink
{
public:
    //发给控制器的命令（kMsgShareViewerCommand 的 wParam）
    enum Command
    {
        kCmdLeave  = 1,   //用户关窗或点了「离开」
        kCmdMicOn  = 2,   //用户要打开麦克风（浮动工具条的按钮）
        kCmdMicOff = 3,   //用户要关闭麦克风
        kCmdCameraOn  = 4,   //用户要打开摄像头（浮动工具条的按钮）
        kCmdCameraOff = 5,   //用户要关闭摄像头
        kCmdAnnotate  = 6,   //用户要进入或退出标注模式（浮动工具条的按钮）
        kCmdRecordStart = 7, //用户要开始录制（浮动工具条的按钮）
        kCmdRecordStop  = 8, //用户要停止录制
        kCmdViewers     = 9, //点了工具条的「N 人观看 ▾」：打开或关闭观看者名单（第 13 期）
        kCmdControl     = 10, //点了「申请控制 / 取消申请 / 结束控制」（第 18 期），控制器按当前状态决定发什么
        kCmdShareMine   = 11, //点了「共享我的屏幕 / 取消共享申请 / 停止我的共享」（第 22 期），控制器按当前状态决定发什么
        kCmdBoard       = 12, //点了「新建白板 / 取消白板申请 / 关闭我的白板」（第 23 期），控制器按当前状态决定发什么
        kCmdCaptionSelf = 13, //工具条「字幕」菜单：「为我的发言生成字幕」，lParam 为 1 打开、0 关闭（语音字幕）
        kCmdCaptionShow = 14, //工具条「字幕」菜单：「显示字幕」，lParam 为 1 打开、0 关闭（语音字幕）
        kCmdHostMenu    = 15, //「主持人 ▾」菜单选了一项（2026-10-07，经移交成为主持人的人），lParam 为 share_host_menu_item
        kCmdCloseBoard  = 16, //「主持人 ▾」菜单「关闭白板 ▸」选了一块（2026-10-08），lParam 为白板的建立者 userid
    };

    CShareViewerWindow();
    ~CShareViewerWindow();

    /**
     *  打开观看窗并开始收流：建窗、全屏铺满参照窗口所在的显示器、连上房间中转。连接失败时窗口照常打开并显示提示，
     *  同时向通知窗口投 kMsgShareViewerEvent（share_viewer_relay_failed）。
     *  @param relay        1726 下发的中转参数
     *  @param serverPubKey 服务端 32 字节公钥；为 NULL 时不加密。只在本次调用期间读取
     *  @param hostName     发起人显示名，用于标题
     *  @param notifyWnd    通知窗口（屏幕共享控制器）；不拥有
     *  @param monitorRef   参照窗口：全屏铺满它所在的显示器；可为 NULL（主显示器）
     *  @param p2pSlot      单聊共享里本人与发起人之间的 P2P 链路槽（P2P 方案第 6 期）；为空时只走房间中转。交给收流管线，
     *                      由它在后台线程上先等链路
     */
    bool Open(const ShareRelayInfo& relay, const unsigned char* serverPubKey, const std::wstring& hostName,
              HWND notifyWnd, HWND monitorRef,
              const std::shared_ptr<balloonp2p::P2PLinkSlot>& p2pSlot = std::shared_ptr<balloonp2p::P2PLinkSlot>());

    //停止收流并关窗；可重复调用
    void CloseViewer();

    //发起人在线状态变化（1725 的 hostonline）
    void SetHostOnline(bool online);

    //房间号（控制器据此核对）
    const std::string& RoomId() const;

    //刷新麦克风状态：浮动工具条上的按钮随之变化
    void SetMicOn(bool on);

    /**
     *  刷新说话人提示（如「李四 正在说话」），接在窗口标题与浮动工具条的标题条的后面。
     *  @param text 说话人提示；为空表示没有人在说话
     */
    void SetSpeakingText(const std::wstring& text);

    //刷新观看人数（不含发起人，第 13 期），显示在浮动工具条的「N 人观看 ▾」上
    void SetViewerCount(int count);

    /**
     *  刷新共享时长（第 13 期），接在窗口标题与浮动工具条的标题条里的发起人名后面。
     *  @param text 时长文字（见 FormatShareDuration）；为空表示还没开始计时
     */
    void SetDurationText(const std::wstring& text);

    /**
     *  设置是否强制模式（第 13 期，来自 1725 的 forced）。强制模式下：只能全屏，工具条没有「退出全屏」「离开」，
     *  Esc、F11 与关窗无效，窗口置顶，切到别的程序后拉回前台。
     */
    void SetForced(bool forced);

    //是否强制模式
    bool IsForced() const;

    //浮动工具条上各按钮的编号（2026-10-12），随 WM_SHARE_VIEWER_TOOLBAR 的 wParam 回传；单元测试按它找按钮
    enum ViewerBarButtonId
    {
        kBarMic = kViewerBarUserIdFirst,   //打开 / 关闭麦克风
        kBarCamera,       //打开 / 关闭摄像头
        kBarAnnotate,     //标注 / 退出标注
        kBarRecord,       //录制 / 停止录制
        kBarCaption,      //字幕：弹出「为我的发言生成字幕」「显示字幕」两个勾选项；字幕能使用时才有
        kBarControl,      //申请控制 / 取消申请 / 结束控制（第 18 期）
        kBarShareMine,    //共享我的屏幕 / 取消共享申请 / 停止我的共享（第 22 期）
        kBarBoard,        //新建白板 / 取消白板申请 / 关闭我的白板（第 23 期）
        kBarViewers,      //「N 人观看 ▾」：打开或关闭观看者名单（第 13 期）
        kBarInfo,         //共享说明：共享有描述时才有（第 15 期）
        kBarHost,         //主持人菜单：经移交成为主持人时才有（2026-10-07）
        kBarFullscreen,   //全屏 / 退出全屏：强制模式下没有
        kBarLeave,        //离开共享：强制模式下没有
    };

    //浮动工具条的窗口（2026-10-12，单元测试用）；没有显示时为 NULL
    HWND BarWindowForTest() const;

    //浮动工具条对象（2026-10-12，截图用例把它的控件树画进内存位图）
    CViewerFloatBar& BarForTest();

    /**
     *  取浮动工具条上某个按钮的屏幕矩形（2026-10-12，单元测试用）。
     *  @param id  按钮编号，取值见 ViewerBarButtonId
     *  @param out [出参] 屏幕坐标
     *  @return 工具条可见且有这个按钮时返回 true
     */
    bool BarButtonRectForTest(UINT id, RECT& out) const;

    //某块画面当前那一帧的尺寸（2026-10-12，单元测试用），见 CShareVideoCanvas::ScreenFrameSize；窗口没打开时返回 false
    bool ScreenFrameSizeForTest(int seat, int& width, int& height) const;

    //某块画面的名字标签最近一次画在哪里（2026-10-13，单元测试用，本窗口客户区坐标），见 CShareVideoCanvas::ScreenLabelRect
    bool ScreenLabelRectForTest(int seat, RECT& out) const;

    /**
     *  取观看者名单的锚点：工具条上「N 人观看 ▾」按钮的屏幕矩形；工具条收起、隐藏时为窗口右上角往左的一块。
     *  @param out [出参] 屏幕坐标
     *  @return 窗口存在时返回 true
     */
    bool GetViewersAnchor(RECT& out) const;

    /**
     *  设置共享标题与描述（第 15 期，来自 1721 / 1725 / 1727 与 1734）。窗口标题与浮动工具条标题条的主体文字随之变化；
     *  描述由无变有或由有变无时，浮动工具条的「共享说明」按钮随之出现或消失；
     *  「共享说明」浮窗开着时内容随之更新，描述变空时关闭浮窗。窗口未建时只记下，打开窗口时生效。
     *  @param title 共享标题；空串表示老版本发起端没有设标题，标题栏显示「某某 的屏幕共享」
     *  @param desc  共享描述；空串表示没有描述
     */
    void SetShareInfo(const std::wstring& title, const std::wstring& desc);

    //本窗口收流管线的中转连接，作本人麦克风帧的发送通道（随窗口存在，不拥有）
    ShareAudioTransport* AudioTransport();

    /**
     *  登记房间语音会话：收流管线收到的声音帧交给它。传 NULL 取消登记。
     *  @param audio 语音会话，不拥有；取消登记之前须一直有效
     */
    void SetAudioSession(ShareAudioSession* audio);

    //刷新摄像头状态：浮动工具条上的按钮随之变化
    void SetCameraOn(bool on);

    /**
     *  设置右侧摄像头小窗条要显示的人；没有人开摄像头时整条收起、画面占满窗口。
     *  @param tiles         小窗（含本人）
     *  @param overflowCount 超出同屏上限、没有显示画面的人数
     */
    void SetCameraTiles(const std::vector<ShareCameraTile>& tiles, int overflowCount);

    //摄像头画面的来源（屏幕共享控制器）；不拥有
    void SetCameraSource(IShareCameraFrames* source);

    /**
     *  登记摄像头的收发端：收流管线收到的摄像头帧交给 receiver，摄像头轨的关键帧请求交给 sender。传 NULL 取消登记。
     */
    void SetCameraEndpoints(ShareCameraSender* sender, ShareCameraReceiver* receiver);

    /**
     *  登记标注：画面上叠画 session 里的笔迹，标注模式下鼠标动作交给 input，收流管线收到的标注帧交给 sink。
     *  三者都不拥有；传 NULL 取消登记。
     */
    void SetAnnotation(const ShareAnnotationSession* session, IShareAnnotationInput* input, ShareAnnotationSink* sink);

    /**
     *  多人共享的画面清单（第 22 期），见 CShareVideoCanvas::SetScreens。
     */
    void SetScreens(const std::vector<int>& seats, const std::vector<std::wstring>& names);

    //当前那一块的画面座位（第 22 期）：控制键鼠、录制作用在这一块上；没有清单时为 -1
    int ActiveScreenSeat() const;

    /**
     *  本人那一块的画面来源（2026-10-12，docs/远程桌面控制权限与观看工具条方案.md 第 2.2 节 C）：本人也在共享屏幕时，
     *  这个座位的画面改从 feed（本人的共享者推流管线，按限帧率留下的本机画面）取，不再取中转转回来的那一份。
     *  feed 有新帧时由调用方调 OnViewerFrame 通知本窗口取帧。关窗时复位。
     *  @param seat 本人画面的座位；小于 0 表示本人没在共享屏幕
     *  @param feed 画面来源，不拥有，取消（seat 小于 0 或传 NULL）之前须一直有效
     */
    void SetLocalScreen(int seat, IShareScreenFeed* feed);

    //本人那一块的来源有了新的一帧（2026-10-12，界面线程）：投给自己，稍后与收流管线的帧一起取
    void OnLocalScreenFrame();

    /**
     *  是否把观看窗与它的浮动工具条排除在屏幕采集之外（2026-10-12），由控制器按 ShouldExcludeShareView 决定。
     *  窗口还没建时记下，建好后生效；关窗时复位为不排除。
     *  @param excluded true 排除，false 允许采集
     */
    void SetCaptureExcluded(bool excluded);

    //观看窗当前是否排除在屏幕采集之外（2026-10-12，单元测试用）
    bool IsCaptureExcluded() const;

    //画面清单里哪几块是白板（第 23 期），见 CShareVideoCanvas::SetBoards；窗口重建时沿用
    void SetBoards(const std::vector<int>& seats, const std::vector<int>& ownerSeats);

    //当前那一块是不是白板（第 23 期）
    bool ActiveIsBoard() const;

    /**
     *  取某块画面最近解出的一帧（第 23 期「保存为图片」）。
     *  @param seat          画面座位；没有多人共享信息时发起人的画面为 0
     *  @param bgra          [出参] 自上而下的 BGRA
     *  @param width、height [出参] 尺寸
     *  @return 有画面返回 true
     */
    bool GetScreenFrame(int seat, std::vector<uint8_t>& bgra, int& width, int& height);

    /**
     *  被控制的画面（第 22 期）：控制中的鼠标按这块画面的矩形换算坐标，它没有显示出来时鼠标不发送（键盘照常发送）。
     *  @param seat 画面座位；-1 表示按当前那一块（没有多人共享信息时）
     */
    void SetControlScreen(int seat);

    /**
     *  「共享我的屏幕」按钮的状态（第 22 期）：浮动工具条上的按钮随之变化。
     *  @param state 取值见 share_viewer_mine_state
     */
    void SetShareMineState(int state);

    /**
     *  换用新的中转参数重连（2026-10-07，方案第 14.3 节第 3 条）：经移交成为主持人的人掉线后在宽限期内重新登录，服务端
     *  补发 1726，观看窗不关闭，只把收流管线断开后按新的令牌重新连接；语音、标注、摄像头仍经这个收流管线收发。只走房间中转。
     *  @param relay        新的 1726 中转参数
     *  @param serverPubKey 服务端 32 字节公钥；为 NULL 时不加密
     *  @return 窗口开着且重新报到成功返回 true；失败时标题栏显示中转断开
     */
    bool Reconnect(const ShareRelayInfo& relay, const unsigned char* serverPubKey);

    /**
     *  经移交成为主持人时的主持人入口（2026-10-07，docs/屏幕共享与录制方案.md 第 14.3 节第 2 条）：打开时浮动工具条上多一个
     *  「主持人」按钮（皇冠图标），点它弹出修改标题与描述、全员静音、录制与标注权限的菜单。
     *  @param on              是否放主持人入口
     *  @param recordAllowed   房间此刻是否允许他人录制（菜单里的勾选状态）
     *  @param annotateAllowed 房间此刻是否允许他人标注（菜单里的勾选状态）
     */
    void SetHostTools(bool on, bool recordAllowed, bool annotateAllowed);

    //此刻是否放着主持人入口（用例与控制器查询）
    bool HasHostTools() const;

    /**
     *  「主持人 ▾」菜单「关闭白板 ▸」子菜单的各项（2026-10-08）：别人建的白板，控制器在每次 1725 后设置。
     *  为空时不显示这一项。只记下，下次打开菜单时生效。
     *  @param items 子菜单各项，见 BuildShareBoardCloseItems
     */
    void SetHostBoards(const std::vector<ShareBoardCloseItem>& items);

    /**
     *  「新建白板」按钮的状态（第 23 期）：浮动工具条上的按钮随之变化。
     *  @param state 取值同 share_viewer_mine_state：0 没有白板，1 已申请，2 白板开着
     */
    void SetBoardState(int state);

    //进入或退出标注模式（控制器决定）：画面控件接不接鼠标、按钮与图标的文字随之变化；进入时清掉「鼠标」状态
    void SetAnnotating(bool on);

    /**
     *  标注模式下的「鼠标」状态（2026-10-07，标注工具条最左边的按钮）：画面控件不再拦截标注输入，标注模式、按钮与图标不变。
     *  @param on true 进入「鼠标」状态；false 回到标注
     */
    void SetAnnotPointer(bool on);

    //笔迹有变化：重画画面
    void AnnotationChanged();

    //刷新录制状态：浮动工具条上的按钮随之变化；停止时一并清掉录制时长
    void SetRecording(bool on);

    /**
     *  刷新给录制用的白板图（2026-10-07，方案第 14.1 节 ④-2，界面线程）：当前那块是白板时按当前可见区域画一张放进缓冲，
     *  之后 GetRecordFrame 取它；不是白板时清掉标记，GetRecordFrame 改取视频帧。录制期间由定时器每 200 毫秒调用；
     *  开始录制前探测有没有画面时，控制器先调用一次。
     */
    void RefreshRecordBoardFrame();

    /**
     *  录下这场共享的已录时长（2026-10-07，方案第 14.2 节：共享中的录制不再弹出单独的录制条）。录制中写在窗口标题与浮动工具条的
     *  标题条里，如「● 录制 03:21」，圆点每次调用在实心与空心之间交替（控制器每秒调用一次，即每秒闪一下）。
     *  @param text 已录时长文字（FormatRecordDuration 的结果）；为空表示不显示
     */
    void SetRecordDuration(const std::wstring& text);

    /**
     *  这场共享的云端录制状态（第 27b 期）：标题栏在时长之后写「云端录制中」或「云端录制已暂停」，房间里的人都能看到。
     *  @param recording 正在云端录制（含暂停）
     *  @param paused    暂停中
     */
    void SetCloudRecordState(bool recording, bool paused);

    /**
     *  设置控制键鼠的状态（第 18 期，控制器决定）：浮动工具条上的按钮随之变化；进入控制时开始把画面上的键鼠转给
     *  控制输入接口，离开控制时补发还按着的鼠标按钮与按键的抬起（发给控制器，由它决定还能不能发出去）。
     *  @param state 取值见 share_viewer_control_state
     */
    void SetControlState(int state);

    //登记控制输入的接收方（屏幕共享控制器）；不拥有，传 NULL 取消登记
    void SetControlInput(IShareControlInput* input);

    /**
     *  登记字幕帧的接收方（语音字幕，docs/语音字幕方案.md 第 9 节）：收流管线收到的字幕控制帧交给它。传 NULL 取消登记。
     *  @param sink 接收方，不拥有；取消登记之前须一直有效
     */
    void SetCaptionSink(ShareCaptionSink* sink);

    /**
     *  字幕的状态（语音字幕）：可用时浮动工具条上有「字幕」按钮，点它弹出「为我的发言生成字幕」「显示字幕」两个勾选项；
     *  不可用（企业关闭了字幕、字幕组件缺失）时没有这个按钮。窗口模式与全屏都有入口。
     *  @param available 字幕能否使用
     *  @param selfOn    「为我的发言生成字幕」是否打开
     *  @param showOn    「显示字幕」是否打开
     */
    void SetCaptionState(bool available, bool selfOn, bool showOn);

    /**
     *  字幕浮层的显示区域（语音字幕）：画面控件所占的矩形（不含标题栏与右侧摄像头小窗条），屏幕坐标。
     *  @param out [出参] 屏幕坐标
     *  @return 窗口不存在、不可见或已最小化时返回 false
     */
    bool CaptionArea(RECT& out) const;

    /**
     *  取收流管线解出的最新一帧（共享录制用，任意线程）。
     *  @return 已有画面返回 true
     */
    bool GetRecordFrame(std::vector<uint8_t>& bgra, int& width, int& height, uint64_t& serial);

    //本窗口私有消息（WM_APP 起，只在本窗口内使用）
    enum
    {
        WM_SHARE_VIEWER_FRAME   = WM_APP + 1,   //收流管线解出了新的一帧
        WM_SHARE_VIEWER_EVENT   = WM_APP + 2,   //收流管线事件：wParam = share_viewer_event，lParam = 附加值
        WM_SHARE_VIEWER_TOOLBAR = WM_APP + 3,   //浮动工具条的按钮（2026-10-12 起）：wParam = ViewerBarButtonId
        //WM_APP + 4、WM_APP + 5 已作废、不得复用：原为点了标题栏「字幕」「主持人」图标后延后弹菜单的消息。
        //浮动工具条本身用 PostMessage 投递点击（WM_SHARE_VIEWER_TOOLBAR），2026-10-14 删除
        WM_SHARE_VIEWER_FULLSCREEN = WM_APP + 6,     //双击了画面：等通知处理完再切换全屏（2026-10-12）
    };

    BEGIN_MSG_MAP(CShareViewerWindow)
        //控制中（第 18 期）：画面上的鼠标与全部键盘消息先交给控制输入，排在其余处理之前；不在控制中时原样放过
        MESSAGE_RANGE_HANDLER(WM_MOUSEFIRST, WM_MOUSELAST, OnControlInputMsg)
        MESSAGE_RANGE_HANDLER(WM_KEYFIRST, WM_KEYLAST, OnControlInputMsg)
        MESSAGE_HANDLER(WM_KILLFOCUS, OnKillFocusMsg)
        MESSAGE_HANDLER(WM_SHARE_VIEWER_FRAME, OnFrameMsg)
        MESSAGE_HANDLER(WM_SHARE_VIEWER_EVENT, OnEventMsg)
        MESSAGE_HANDLER(WM_SHARE_VIEWER_TOOLBAR, OnBarCmd)
        MESSAGE_HANDLER(WM_SHARE_VIEWER_FULLSCREEN, OnFullscreenMsg)
        MESSAGE_HANDLER(WM_CLOSE, OnCloseMsg)
        MESSAGE_HANDLER(WM_MOVE, OnMoveSizeMsg)
        MESSAGE_HANDLER(WM_SIZE, OnMoveSizeMsg)
        MESSAGE_HANDLER(WM_KEYDOWN, OnKeyDown)
        MESSAGE_HANDLER(WM_TIMER, OnTimer)
        MESSAGE_HANDLER(WM_DUI_NOTIFY, OnDuiNotify)
        CHAIN_MSG_MAP(balloonwjui::DuiFrameWindow)
    END_MSG_MAP()

private:
    //ShareViewerSink：接收线程上回调，只做 PostMessage
    virtual void OnViewerFrame();
    virtual void OnViewerEvent(int event, int value);

    LRESULT OnFrameMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnEventMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    //控制中的鼠标与键盘消息（第 18 期）：换算后交给控制输入；画面以外的鼠标消息（标题栏、摄像头小窗条）照常放过
    LRESULT OnControlInputMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    //失去焦点（第 18 期）：控制中补发还按着的按钮与按键的抬起，免得画面主人那边一直按着；随后照常交给基类
    LRESULT OnKillFocusMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    //请控制器按当前状态申请控制、取消申请或结束控制（第 18 期）
    void requestControlToggle();
    //点了「共享我的屏幕」类按钮（第 22 期）：通知控制器
    void requestShareMine();
    //「新建白板 / 取消白板申请 / 关闭我的白板」：通知控制器（第 23 期）
    void requestBoard();
    //补发还按着的鼠标按钮与按键的抬起，并清空记录（第 18 期）
    void releaseControlInput();
    //浮动工具条的按钮（2026-10-12）：wParam 为 ViewerBarButtonId
    LRESULT OnBarCmd(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    //窗口移动或改变大小：浮动工具条跟着挪到新的画面区域顶部（不拦截，照常交给基类）
    LRESULT OnMoveSizeMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnCloseMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnKeyDown(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnTimer(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    //双击画面切换全屏；其余通知交给基类
    LRESULT OnDuiNotify(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    //按当前状态刷新浮动工具条的按钮（2026-10-12 起取代标题栏图标与全屏工具条的文字按钮）
    void refreshBar();
    //浮动工具条的按钮清单：麦克风、摄像头、标注、录制、[字幕]、申请控制、共享我的屏幕、新建白板、N 人观看、
    //[共享说明]、[主持人]、[全屏]、[离开]，方括号内的按条件出现
    std::vector<ViewerBarButtonSpec> barButtons() const;
    //浮动工具条的参照矩形（屏幕坐标）：全屏时为所在显示器，窗口模式时为标题栏以下的画面区域
    RECT barAnchor() const;
    //请控制器切换麦克风
    void requestMicToggle();
    //请控制器切换摄像头
    void requestCameraToggle();
    //请控制器切换标注模式
    void requestAnnotateToggle();
    //请控制器开始或停止录制
    void requestRecordToggle();
    //请控制器打开或关闭观看者名单（第 13 期）
    void requestViewersToggle();
    //工具条「字幕」按钮被点击：在按钮下方弹出两个勾选项，选中的结果通知控制器（语音字幕）
    void showCaptionMenu();
    //工具条「主持人」按钮被点击（2026-10-07）：在按钮下方弹出主持人菜单，选中的一项通知控制器
    void showHostMenu();
    //WM_SHARE_VIEWER_FULLSCREEN（2026-10-12）：双击画面之后切换全屏；强制模式下不切换
    LRESULT OnFullscreenMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    //打开或关闭「共享说明」浮窗（第 15 期，本窗口自己处理，不经过控制器）
    void toggleInfoPopup();
    /**
     *  取浮窗的锚点：浮动工具条上某个按钮的屏幕矩形；工具条收起、隐藏时为标题栏右侧一块。
     *  @param buttonId 按钮编号，取值见 ViewerBarButtonId
     *  @param out        [出参] 屏幕坐标
     *  @return 窗口存在时返回 true
     */
    bool popupAnchor(UINT buttonId, RECT& out) const;
    //按 m_forced 调整全屏、工具条按钮、置顶与拉回前台的定时器（第 13 期）
    void applyForced();
    //强制模式下切到别的程序时把观看窗拉回前台（拉回前台定时器调用）
    void pullBackToFront();

    void EnsureWindow();
    //按当前状态建客户区的控件树（画面与摄像头小窗条）并刷新浮动工具条的按钮；新建窗口与复用等待销毁的窗口时调用
    void buildContent();
    void toggleFullscreen();
    void refreshStatus();
    //标题文字：「周会演示 · 12:34   N fps」，没有共享标题时主体为「某某 的屏幕共享」（见 MakeShareViewerCaption），
    //窗口标题与浮动工具条的标题条共用
    std::wstring titleText() const;
    void requestLeave();
    //按座位取某块画面的最新一帧：本人那一块取本机预览（SetLocalScreen），其余取收流管线（2026-10-12）
    bool pullScreenFrame(int seat, std::vector<uint8_t>& bgra, int& width, int& height, uint64_t& serial);
    //按座位只取帧序号、不拷贝画面，来源与 pullScreenFrame 相同（OnFrameMsg 先比序号，变了才取帧）
    bool peekScreenSerial(int seat, uint64_t& serial) const;

    ShareViewerReceiver   m_receiver;          //收流管线
    CShareVideoCanvas*    m_canvas;            //画面控件（控件树持有，本指针仅引用）
    CViewerFloatBar       m_bar;               //浮动工具条（2026-10-12，窗口模式与全屏共用）
    HWND                  m_notifyWnd;         //通知窗口（不拥有）
    std::string           m_roomId;            //房间号
    std::wstring          m_hostName;          //发起人显示名
    std::vector<uint8_t>  m_scratch;           //取帧缓冲（与画面控件交换）
    uint64_t              m_lastSerial;        //上次取到的帧序号
    std::map<int, uint64_t> m_lastSerials;     //多人共享（第 22 期）：各画面座位上次取到的帧序号
    int                   m_localSeat;         //本人画面的座位（2026-10-12）：这一块取本机预览；本人没在共享屏幕时为 -1，关窗时复位
    IShareScreenFeed*     m_localFeed;         //本人那一块的画面来源（不拥有，本人的共享者推流管线）；关窗时置空
    bool                  m_captureExcluded;   //是否排除在屏幕采集之外（2026-10-12，见 SetCaptureExcluded）；关窗时复位
    std::vector<int>      m_screenSeats;       //多人共享的画面清单（第 22 期）：画面座位，按显示顺序
    std::vector<std::wstring> m_screenNames;   //与 m_screenSeats 一一对应的共享人名字
    std::vector<int>      m_boardSeats;        //画面清单里白板的座位（第 23 期），窗口重建时交给画面控件；关窗时清空
    std::vector<int>      m_boardOwnerSeats;   //与 m_boardSeats 一一对应的建立者成员座位
    int                   m_controlScreen;     //被控制的画面座位（第 22 期）；-1 表示按当前那一块；关窗时复位
    bool                  m_hostOnline;        //发起人是否在线
    int                   m_sourceState;       //发起人的共享内容状态
    bool                  m_relayBroken;       //房间中转是否已断开或连不上
    bool                  m_undecodable;       //有画面判为超出本机系统的解码能力（2026-10-15，Win7 观看大分辨率的共享）
    int                   m_framesSinceTick;   //本秒内画出的帧数（标题栏显示帧率）
    int                   m_lastFps;           //上一秒画出的帧数，打开时为 0
    bool                  m_micOn;             //麦克风是否打开（控制器经 SetMicOn 告知）
    std::wstring          m_speaking;          //说话人提示；为空表示没有人在说话
    bool                  m_cameraOn;          //摄像头是否打开（控制器经 SetCameraOn 告知）
    CShareCameraPanel*    m_cameraPanel;       //右侧摄像头小窗条（控件树持有，本指针仅引用）
    IShareCameraFrames*   m_cameraSource;      //摄像头画面的来源（不拥有）
    const ShareAnnotationSession* m_annotSession;   //标注来源（不拥有）
    IShareAnnotationInput* m_annotInput;       //标注模式下鼠标动作的接收方（不拥有）
    bool                  m_annotating;        //是否在标注模式
    bool                  m_annotPointer;      //标注模式下是否处于「鼠标」状态（2026-10-07）：是时画面控件不拦截标注输入
    bool                  m_recording;         //是否在录制（控制器经 SetRecording 告知）
    std::wstring          m_recordDuration;    //录下这场共享的已录时长文字（2026-10-07）；没在录制时为空
    bool                  m_recordDotOn;       //标题里录制圆点此刻是否实心，每次 SetRecordDuration 交替一次
    std::mutex            m_recordBoardMutex;  //保护下面三项白板图（界面线程写、录制线程读，2026-10-07）
    std::vector<uint8_t>  m_recordBoardFrame;  //给录制用的白板图（BGRA，自上而下）；当前那块不是白板时不用
    int                   m_recordBoardW;      //白板图的宽（像素）
    int                   m_recordBoardH;      //白板图的高（像素）
    uint64_t              m_recordBoardSerial; //白板图的序号，每画一次加一（录制线程据此判断是不是新画面）；同样受锁保护
    std::atomic<bool>     m_recordBoardActive; //当前那块是不是白板且白板图可用：是时 GetRecordFrame 取白板图
    int                   m_viewerCount;       //观看人数，不含发起人（第 13 期，控制器经 SetViewerCount 告知）
    std::wstring          m_duration;          //共享时长文字（第 13 期）；为空表示还没开始计时
    bool                  m_forced;            //是否强制模式（第 13 期）
    bool                  m_cloudRecording;    //这场共享是否正在云端录制（第 27b 期，含暂停）
    bool                  m_cloudRecordPaused; //云端录制是否暂停中
    bool                  m_pullBackFailed;    //强制模式下上一次拉回前台是否失败（只在由成功变失败时记一次日志）
    std::wstring          m_shareTitle;        //共享标题（第 15 期，控制器经 SetShareInfo 告知）；为空表示发起端没有设标题
    std::wstring          m_shareDesc;         //共享描述（第 15 期）；为空表示没有描述，此时不放「共享说明」入口
    CShareInfoPopup       m_infoPopup;         //「共享说明」浮窗（第 15 期），随观看窗关闭
    int                   m_controlState;      //控制键鼠的状态（第 18 期），取值见 share_viewer_control_state
    IShareControlInput*   m_controlInput;      //控制中键鼠输入的接收方（不拥有）
    ShareControlForwarder m_forwarder;         //控制中把键鼠换算后交给控制输入，记着按下未抬起的按钮与按键
    int                   m_mineState;         //「共享我的屏幕」按钮的状态（第 22 期），取值见 share_viewer_mine_state
    int                   m_boardState;        //「新建白板」按钮的状态（第 23 期），取值同 share_viewer_mine_state
    bool                  m_captionAvailable;  //字幕能否使用（控制器经 SetCaptionState 告知）；不能使用时工具条上没有「字幕」按钮
    //工具条「字幕」菜单与「主持人 ▾」菜单的弹出状态（2026-10-07，两个菜单不会同时弹出，共用一份）：菜单开着时共享结束、
    //控制器调 CloseViewer，先关菜单、只隐藏窗口，等 TrackPopup 返回后再销毁；期间又打开观看窗时直接复用这个窗口。
    //原因同屏幕共享控制条（方案 13.1，用例 MMD2、MMD4）
    CShareBarPopupGuard   m_captionPopup;
    bool                  m_captionSelf;       //「为我的发言生成字幕」是否打开
    bool                  m_captionShow;       //「显示字幕」是否打开
    bool                  m_hostTools;         //是否放主持人入口（2026-10-07，经移交成为主持人时由控制器打开）；关窗时复位
    bool                  m_hostRecordAllowed; //主持人菜单里「允许他人录制」的勾选状态
    bool                  m_hostAnnotAllowed;  //主持人菜单里「允许他人标注」的勾选状态
    std::vector<ShareBoardCloseItem> m_hostBoards;   //主持人菜单「关闭白板 ▸」的各项（2026-10-08）；为空时不显示这一项
    balloonmain::ProgramWindowIcons m_programIcons;   //标题栏（与任务栏）的程序图标（2026-10-07）；本类持有，析构时释放
};

} // namespace screenshare
