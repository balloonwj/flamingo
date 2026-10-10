/**
 *  屏幕共享观看窗，ShareViewerWindow.h
 *
 *  观看者加入共享后打开（docs/屏幕共享与录制方案.md 5.2 第 7 步）：默认全屏，铺满主面板所在的显示器；ESC 退出全屏、
 *  F11 切换；全屏时顶部浮一条工具条（复用远程桌面的 CRdViewerToolbar，文字改成「屏幕共享」「离开」）。客户区是一个
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
#include "ShareCameraPanel.h"
#include "ShareAnnotationOverlay.h"
#include "ShareInfoPopup.h"
#include "ShareRemoteControl.h"
#include "ShareWhiteboard.h"   // 白板画面的视野（第 23 期）
#include "ShareRoomPolicy.h"   // ShareBoardCloseItem：「主持人 ▾」菜单的「关闭白板 ▸」（2026-10-08）
#include "../RemoteDesktop/RdViewerToolbar.h"
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
 *  松开后放开。
 *
 *  典型用法（窗口的键鼠消息处理里）：
 *      if (!m_forwarder.Handle(m_hWnd, uMsg, wParam, lParam, videoRect, m_controlInput)) bHandled = FALSE;
 */
class ShareControlForwarder
{
public:
    ShareControlForwarder();

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

    //清空记录并放开鼠标，不补发抬起（窗口关闭、房间已离开时调用）
    void Reset();

private:
    ShareViewerInputTranslator m_translator;   //把窗口消息换成输入（画面外按下不发、移动限频等）
    std::vector<uint32_t>      m_keysDown;     //已发出按下、还没发出抬起的键
    bool                       m_captured;     //按下鼠标后是否已 SetCapture（全部按钮松开后释放）
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
    //多人共享（第 22 期）时为「当前那一块」（放大的那块，没有放大时为第一块）的画面矩形
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

    //当前那一块的画面座位（第 22 期）：放大的那块，没有放大时为第一块；没有清单时为 -1
    int ActiveScreenSeat() const;

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

    virtual void OnPaint(HDC hdc, const RECT& rcDirty) override;
    virtual bool OnLButtonDown(POINT pt, UINT mkFlags) override;
    virtual bool OnMouseMove(POINT pt, UINT mkFlags) override;
    virtual bool OnLButtonUp(POINT pt, UINT mkFlags) override;
    virtual bool OnSetCursor(POINT pt) override;
    //标注模式下双击左键：完成折线、曲线（第 19 期）
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
        kCmdMicOn  = 2,   //用户要打开麦克风（全屏工具条按钮或标题栏图标）
        kCmdMicOff = 3,   //用户要关闭麦克风
        kCmdCameraOn  = 4,   //用户要打开摄像头（全屏工具条按钮或标题栏图标）
        kCmdCameraOff = 5,   //用户要关闭摄像头
        kCmdAnnotate  = 6,   //用户要进入或退出标注模式（全屏工具条按钮或标题栏图标）
        kCmdRecordStart = 7, //用户要开始录制（全屏工具条按钮或标题栏图标）
        kCmdRecordStop  = 8, //用户要停止录制
        kCmdViewers     = 9, //点了「N 人观看 ▾」或标题栏「观看者」图标：打开或关闭观看者名单（第 13 期）
        kCmdControl     = 10, //点了「申请控制 / 取消申请 / 结束控制」（第 18 期），控制器按当前状态决定发什么
        kCmdShareMine   = 11, //点了「共享我的屏幕 / 取消共享申请 / 停止我的共享」（第 22 期），控制器按当前状态决定发什么
        kCmdBoard       = 12, //点了「新建白板 / 取消白板申请 / 关闭我的白板」（第 23 期），控制器按当前状态决定发什么
        kCmdCaptionSelf = 13, //标题栏「字幕」菜单：「为我的发言生成字幕」，lParam 为 1 打开、0 关闭（语音字幕）
        kCmdCaptionShow = 14, //标题栏「字幕」菜单：「显示字幕」，lParam 为 1 打开、0 关闭（语音字幕）
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

    //刷新麦克风状态：全屏工具条按钮的文字与标题栏图标随之变化
    void SetMicOn(bool on);

    /**
     *  刷新说话人提示（如「李四 正在说话」），接在标题与全屏工具条状态行的后面。
     *  @param text 说话人提示；为空表示没有人在说话
     */
    void SetSpeakingText(const std::wstring& text);

    //刷新观看人数（不含发起人，第 13 期），显示在全屏工具条的「N 人观看 ▾」上
    void SetViewerCount(int count);

    /**
     *  刷新共享时长（第 13 期），接在标题与全屏工具条状态行里的发起人名后面。
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

    /**
     *  取观看者名单的锚点：全屏时为工具条上「N 人观看 ▾」按钮的屏幕矩形，窗口模式时为标题栏右侧一块。
     *  @param out [出参] 屏幕坐标
     *  @return 窗口存在时返回 true
     */
    bool GetViewersAnchor(RECT& out) const;

    /**
     *  设置共享标题与描述（第 15 期，来自 1721 / 1725 / 1727 与 1734）。标题栏与全屏工具条状态行的主体文字随之变化；
     *  描述由无变有或由有变无时，标题栏「共享说明」图标与全屏工具条「共享说明」按钮随之出现或消失；
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

    //刷新摄像头状态：全屏工具条按钮的文字与标题栏图标随之变化
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
     *  「共享我的屏幕」按钮的状态（第 22 期）：标题栏图标与全屏工具条按钮的文字随之变化。
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
     *  经移交成为主持人时的主持人入口（2026-10-07，docs/屏幕共享与录制方案.md 第 14.3 节第 2 条）：打开时全屏工具条末尾多一个
     *  「主持人 ▾」按钮、标题栏多一个皇冠图标，点它弹出修改标题与描述、全员静音、录制与标注权限的菜单。
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
     *  「新建白板」按钮的状态（第 23 期）：标题栏图标与全屏工具条按钮的文字随之变化。
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

    //刷新录制状态：全屏工具条按钮的文字与标题栏图标随之变化；停止时一并清掉录制时长
    void SetRecording(bool on);

    /**
     *  刷新给录制用的白板图（2026-10-07，方案第 14.1 节 ④-2，界面线程）：当前那块是白板时按当前可见区域画一张放进缓冲，
     *  之后 GetRecordFrame 取它；不是白板时清掉标记，GetRecordFrame 改取视频帧。录制期间由定时器每 200 毫秒调用；
     *  开始录制前探测有没有画面时，控制器先调用一次。
     */
    void RefreshRecordBoardFrame();

    /**
     *  录下这场共享的已录时长（2026-10-07，方案第 14.2 节：共享中的录制不再弹出单独的录制条）。录制中写在标题栏与全屏工具条
     *  的状态行里，如「● 录制 03:21」，圆点每次调用在实心与空心之间交替（控制器每秒调用一次，即每秒闪一下）。
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
     *  设置控制键鼠的状态（第 18 期，控制器决定）：全屏工具条按钮与标题栏图标随之变化；进入控制时开始把画面上的键鼠转给
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
     *  字幕的状态（语音字幕）：可用时标题栏放一个「字幕」图标，点它弹出「为我的发言生成字幕」「显示字幕」两个勾选项；
     *  不可用（企业关闭了字幕、字幕组件缺失）时不放图标。只在窗口模式下有入口，全屏时沿用已选的状态。
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
        WM_SHARE_VIEWER_TOOLBAR = WM_APP + 3,   //全屏工具条按钮：wParam = CRdViewerToolbar::ToolbarCmd
        WM_SHARE_VIEWER_CAPTION_MENU = WM_APP + 4,   //点了标题栏「字幕」图标：等点击通知处理完再弹菜单（语音字幕）
        WM_SHARE_VIEWER_HOST_MENU = WM_APP + 5,      //点了「主持人 ▾」按钮或标题栏皇冠图标：等点击通知处理完再弹菜单（2026-10-07）
    };

    BEGIN_MSG_MAP(CShareViewerWindow)
        //控制中（第 18 期）：画面上的鼠标与全部键盘消息先交给控制输入，排在其余处理之前；不在控制中时原样放过
        MESSAGE_RANGE_HANDLER(WM_MOUSEFIRST, WM_MOUSELAST, OnControlInputMsg)
        MESSAGE_RANGE_HANDLER(WM_KEYFIRST, WM_KEYLAST, OnControlInputMsg)
        MESSAGE_HANDLER(WM_KILLFOCUS, OnKillFocusMsg)
        MESSAGE_HANDLER(WM_SHARE_VIEWER_FRAME, OnFrameMsg)
        MESSAGE_HANDLER(WM_SHARE_VIEWER_EVENT, OnEventMsg)
        MESSAGE_HANDLER(WM_SHARE_VIEWER_TOOLBAR, OnToolbarCmd)
        MESSAGE_HANDLER(WM_SHARE_VIEWER_CAPTION_MENU, OnCaptionMenuMsg)
        MESSAGE_HANDLER(WM_SHARE_VIEWER_HOST_MENU, OnHostMenuMsg)
        MESSAGE_HANDLER(WM_CLOSE, OnCloseMsg)
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
    LRESULT OnToolbarCmd(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnCloseMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnKeyDown(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnTimer(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    //标题栏麦克风图标被点击；其余通知交给基类
    LRESULT OnDuiNotify(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    //按麦克风状态重新放一个标题栏图标（先移除旧的）
    void refreshMicIcon();
    //请控制器切换麦克风
    void requestMicToggle();
    //请控制器切换摄像头
    void requestCameraToggle();
    //全屏工具条额外按钮的文字（麦克风、摄像头、标注）
    std::vector<std::wstring> toolbarExtraTexts() const;
    //请控制器切换标注模式
    void requestAnnotateToggle();
    //请控制器开始或停止录制
    void requestRecordToggle();
    //请控制器打开或关闭观看者名单（第 13 期）
    void requestViewersToggle();
    //标题栏「字幕」图标被点击：在光标处弹出两个勾选项，选中的结果通知控制器（语音字幕）
    void showCaptionMenu();
    LRESULT OnCaptionMenuMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    //「主持人 ▾」按钮或皇冠图标被点击（2026-10-07）：在按钮下方或光标处弹出主持人菜单，选中的一项通知控制器
    void showHostMenu();
    LRESULT OnHostMenuMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    //「主持人 ▾」是全屏工具条上第几个额外按钮：排在最后，「共享说明」出现时往后挪一个
    size_t hostExtraIndex() const;
    //打开或关闭「共享说明」浮窗（第 15 期，本窗口自己处理，不经过控制器）
    void toggleInfoPopup();
    /**
     *  取浮窗的锚点：全屏时为工具条上第 extraIndex 个额外按钮的屏幕矩形，窗口模式时为标题栏右侧一块。
     *  @param extraIndex 全屏工具条额外按钮的下标
     *  @param out        [出参] 屏幕坐标
     *  @return 窗口存在时返回 true
     */
    bool popupAnchor(size_t extraIndex, RECT& out) const;
    //按 m_forced 调整全屏、工具条按钮、置顶与拉回前台的定时器（第 13 期）
    void applyForced();
    //强制模式下切到别的程序时把观看窗拉回前台（拉回前台定时器调用）
    void pullBackToFront();

    void EnsureWindow();
    //按当前状态建客户区的控件树（画面与摄像头小窗条）并放好标题栏图标；新建窗口与复用等待销毁的窗口时调用
    void buildContent();
    void toggleFullscreen();
    void refreshStatus();
    //标题文字：「周会演示 · 12:34   N fps」，没有共享标题时主体为「某某 的屏幕共享」（见 MakeShareViewerCaption），
    //标题栏与全屏工具条的状态行共用
    std::wstring titleText() const;
    void requestLeave();

    ShareViewerReceiver   m_receiver;          //收流管线
    CShareVideoCanvas*    m_canvas;            //画面控件（控件树持有，本指针仅引用）
    CRdViewerToolbar      m_toolbar;           //全屏工具条
    HWND                  m_notifyWnd;         //通知窗口（不拥有）
    std::string           m_roomId;            //房间号
    std::wstring          m_hostName;          //发起人显示名
    std::vector<uint8_t>  m_scratch;           //取帧缓冲（与画面控件交换）
    uint64_t              m_lastSerial;        //上次取到的帧序号
    std::map<int, uint64_t> m_lastSerials;     //多人共享（第 22 期）：各画面座位上次取到的帧序号
    std::vector<int>      m_screenSeats;       //多人共享的画面清单（第 22 期）：画面座位，按显示顺序
    std::vector<std::wstring> m_screenNames;   //与 m_screenSeats 一一对应的共享人名字
    std::vector<int>      m_boardSeats;        //画面清单里白板的座位（第 23 期），窗口重建时交给画面控件；关窗时清空
    std::vector<int>      m_boardOwnerSeats;   //与 m_boardSeats 一一对应的建立者成员座位
    int                   m_controlScreen;     //被控制的画面座位（第 22 期）；-1 表示按当前那一块；关窗时复位
    bool                  m_hostOnline;        //发起人是否在线
    int                   m_sourceState;       //发起人的共享内容状态
    bool                  m_relayBroken;       //房间中转是否已断开或连不上
    int                   m_framesSinceTick;   //本秒内画出的帧数（标题栏显示帧率）
    int                   m_lastFps;           //上一秒画出的帧数，打开时为 0
    bool                  m_micOn;             //麦克风是否打开（控制器经 SetMicOn 告知）
    std::wstring          m_speaking;          //说话人提示；为空表示没有人在说话
    HBITMAP               m_micOnIcon;         //标题栏图标：麦克风已打开（本类生成、析构释放）
    HBITMAP               m_micOffIcon;        //标题栏图标：麦克风已关闭（同上）
    int                   m_micIconId;         //标题栏麦克风图标的 id；没有放图标时为 0
    bool                  m_cameraOn;          //摄像头是否打开（控制器经 SetCameraOn 告知）
    HBITMAP               m_camOnIcon;         //标题栏图标：摄像头已打开（本类生成、析构释放）
    HBITMAP               m_camOffIcon;        //标题栏图标：摄像头已关闭（同上）
    int                   m_camIconId;         //标题栏摄像头图标的 id；没有放图标时为 0
    CShareCameraPanel*    m_cameraPanel;       //右侧摄像头小窗条（控件树持有，本指针仅引用）
    IShareCameraFrames*   m_cameraSource;      //摄像头画面的来源（不拥有）
    const ShareAnnotationSession* m_annotSession;   //标注来源（不拥有）
    IShareAnnotationInput* m_annotInput;       //标注模式下鼠标动作的接收方（不拥有）
    bool                  m_annotating;        //是否在标注模式
    bool                  m_annotPointer;      //标注模式下是否处于「鼠标」状态（2026-10-07）：是时画面控件不拦截标注输入
    HBITMAP               m_penIcon;           //标题栏图标：标注（本类生成、析构释放）
    HBITMAP               m_penOnIcon;         //标题栏图标：标注模式中（同上）
    int                   m_penIconId;         //标题栏标注图标的 id；没有放图标时为 0
    bool                  m_recording;         //是否在录制（控制器经 SetRecording 告知）
    std::wstring          m_recordDuration;    //录下这场共享的已录时长文字（2026-10-07）；没在录制时为空
    bool                  m_recordDotOn;       //标题里录制圆点此刻是否实心，每次 SetRecordDuration 交替一次
    std::mutex            m_recordBoardMutex;  //保护下面三项白板图（界面线程写、录制线程读，2026-10-07）
    std::vector<uint8_t>  m_recordBoardFrame;  //给录制用的白板图（BGRA，自上而下）；当前那块不是白板时不用
    int                   m_recordBoardW;      //白板图的宽（像素）
    int                   m_recordBoardH;      //白板图的高（像素）
    uint64_t              m_recordBoardSerial; //白板图的序号，每画一次加一（录制线程据此判断是不是新画面）；同样受锁保护
    std::atomic<bool>     m_recordBoardActive; //当前那块是不是白板且白板图可用：是时 GetRecordFrame 取白板图
    HBITMAP               m_recIcon;           //标题栏图标：录制（本类生成、析构释放）
    HBITMAP               m_recOnIcon;         //标题栏图标：录制中（同上）
    int                   m_recIconId;         //标题栏录制图标的 id；没有放图标时为 0
    int                   m_viewerCount;       //观看人数，不含发起人（第 13 期，控制器经 SetViewerCount 告知）
    std::wstring          m_duration;          //共享时长文字（第 13 期）；为空表示还没开始计时
    bool                  m_forced;            //是否强制模式（第 13 期）
    bool                  m_cloudRecording;    //这场共享是否正在云端录制（第 27b 期，含暂停）
    bool                  m_cloudRecordPaused; //云端录制是否暂停中
    bool                  m_pullBackFailed;    //强制模式下上一次拉回前台是否失败（只在由成功变失败时记一次日志）
    HBITMAP               m_peopleIcon;        //标题栏图标：观看者（第 13 期，本类生成、析构释放）
    int                   m_peopleIconId;      //标题栏观看者图标的 id；没有放图标时为 0
    std::wstring          m_shareTitle;        //共享标题（第 15 期，控制器经 SetShareInfo 告知）；为空表示发起端没有设标题
    std::wstring          m_shareDesc;         //共享描述（第 15 期）；为空表示没有描述，此时不放「共享说明」入口
    HBITMAP               m_infoIcon;          //标题栏图标：共享说明（第 15 期，本类生成、析构释放）
    int                   m_infoIconId;        //标题栏共享说明图标的 id；没有放图标时为 0
    CShareInfoPopup       m_infoPopup;         //「共享说明」浮窗（第 15 期），随观看窗关闭
    int                   m_controlState;      //控制键鼠的状态（第 18 期），取值见 share_viewer_control_state
    IShareControlInput*   m_controlInput;      //控制中键鼠输入的接收方（不拥有）
    ShareControlForwarder m_forwarder;         //控制中把键鼠换算后交给控制输入，记着按下未抬起的按钮与按键
    HBITMAP               m_ctrlIcons[3];      //标题栏控制图标，按 share_viewer_control_state 取（本类生成、析构释放）
    int                   m_ctrlIconId;        //标题栏控制图标的 id；没有放图标时为 0
    int                   m_mineState;         //「共享我的屏幕」按钮的状态（第 22 期），取值见 share_viewer_mine_state
    HBITMAP               m_mineIcons[kShareMineStateCount];   //标题栏「共享我的屏幕」图标，按状态取（本类生成、析构释放）
    int                   m_mineIconId;        //标题栏「共享我的屏幕」图标的 id；没有放图标时为 0
    int                   m_boardState;        //「新建白板」按钮的状态（第 23 期），取值同 share_viewer_mine_state
    HBITMAP               m_boardIcons[kShareMineStateCount];   //标题栏「新建白板」图标，按状态取（本类生成、析构释放）
    int                   m_boardIconId;       //标题栏「新建白板」图标的 id；没有放图标时为 0
    bool                  m_captionAvailable;  //字幕能否使用（控制器经 SetCaptionState 告知）；不能使用时不放「字幕」图标
    //标题栏「字幕」菜单与「主持人 ▾」菜单的弹出状态（2026-10-07，两个菜单不会同时弹出，共用一份）：菜单开着时共享结束、
    //控制器调 CloseViewer，先关菜单、只隐藏窗口，等 TrackPopup 返回后再销毁；期间又打开观看窗时直接复用这个窗口。
    //原因同屏幕共享控制条（方案 13.1，用例 MMD2、MMD4）
    CShareBarPopupGuard   m_captionPopup;
    bool                  m_captionSelf;       //「为我的发言生成字幕」是否打开
    bool                  m_captionShow;       //「显示字幕」是否打开
    HBITMAP               m_captionIcon;       //标题栏图标：字幕，没有为本人生成字幕（本类生成、析构释放）
    HBITMAP               m_captionOnIcon;     //标题栏图标：正在为本人生成字幕（同上）
    int                   m_captionIconId;     //标题栏「字幕」图标的 id；没有放图标时为 0
    bool                  m_hostTools;         //是否放主持人入口（2026-10-07，经移交成为主持人时由控制器打开）；关窗时复位
    bool                  m_hostRecordAllowed; //主持人菜单里「允许他人录制」的勾选状态
    bool                  m_hostAnnotAllowed;  //主持人菜单里「允许他人标注」的勾选状态
    std::vector<ShareBoardCloseItem> m_hostBoards;   //主持人菜单「关闭白板 ▸」的各项（2026-10-08）；为空时不显示这一项
    HBITMAP               m_hostIcon;          //标题栏图标：皇冠，主持人入口（本类生成、析构释放）
    int                   m_hostIconId;        //标题栏皇冠图标的 id；没有放图标时为 0
    balloonmain::ProgramWindowIcons m_programIcons;   //标题栏（与任务栏）的程序图标（2026-10-07）；本类持有，析构时释放
};

} // namespace screenshare
