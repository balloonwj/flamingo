/**
 *  屏幕共享中控制键鼠的纯逻辑（第 18 期，docs/屏幕共享与录制方案.md 第 2 节第 24～26 条），ShareRemoteControl.h
 *
 *  观看者得到控制权后，在观看窗里的鼠标、键盘操作编成「键鼠输入控制帧」经房间中转只发给画面主人；画面主人核对
 *  发送者确实是当前控制者之后，把坐标换回自己屏幕上的位置，过滤掉系统级按键，再交给远程桌面的 RdInputInjector 注入。
 *  画面主人自己动了鼠标或键盘（不是注入出来的输入）即收回控制。本文件包含：
 *    - ShareRemoteInput：输入控制帧的组帧与解析、观看窗坐标与归一化坐标的换算、归一化坐标换回共享内容的屏幕坐标；
 *    - ShareInputKeyFilter：跟踪注入过的按键，拦下 Win 键、Alt+Tab、Ctrl+Esc、Ctrl+Alt+Del 等系统级按键，
 *      结束控制时列出仍按着的键以便补发抬起；
 *    - DecideShareInput：一条输入该不该注入（发送者是不是控制者、只共享一个窗口时点在不在窗口上、窗口在不在前台）；
 *    - ShareLocalActivityDetector：判断画面主人本机有没有动静。
 *  全部为纯逻辑，不调用任何注入或钩子 API，单元测试见 Source/Tests/ShareRemoteControlTests.cpp（标签 [sharectl]）。
 *
 *  帧格式：1860 包体 = 11 字节帧头（轨道为控制帧）+ 载荷 [screenshare_control_input][目标座位号][一条输入命令]，
 *  输入命令的字节格式同远程桌面（RemoteDesktop/RdInputDef.h），只是鼠标坐标换成相对共享内容的 0～kAnnotCoordMax。
 *  服务端只把它转发给目标座位（imgserversrc/RoomRelayFrame::parseInputTarget），其他观看者收不到键盘输入。
 *
 *  典型用法：
 *      观看端：int nx, ny;
 *              if (ShareRemoteInput::ViewToNormalized(videoRect, pt, nx, ny))
 *                  发送 ShareRemoteInput::BuildInputBody(mySeat, ownerSeat, ShareInputEvent::Mouse(WM_LBUTTONDOWN, nx, ny, 0));
 *      画面主人：int target; ShareInputEvent ev;
 *              if (ShareRemoteInput::ParseInputFrame(frame, target, ev) && target == mySeat
 *                  && DecideShareInput(...) == share_input_inject)
 *                  注入;
 *
 *  balloonwj@qq.com   2026-10-05
 */
#pragma once

#include <stdint.h>
#include <string>
#include <vector>
#include <windows.h>

#include "ShareFraming.h"

namespace screenshare {

//一条键鼠输入（观看端产生、画面主人注入）
struct ShareInputEvent
{
    int      m_type;      //输入类型：remotedesktop::rd_input_mouse（1）或 rd_input_key（2）
    uint32_t m_message;   //Win32 消息：鼠标为 WM_MOUSEMOVE / WM_LBUTTONDOWN 等，键盘为 WM_KEYDOWN / WM_KEYUP 等
    int      m_x;         //鼠标横坐标：相对共享内容的归一化坐标 0～kAnnotCoordMax；键盘输入不用
    int      m_y;         //鼠标纵坐标：同上
    int      m_wheel;     //滚轮增量（WM_MOUSEWHEEL 用，WHEEL_DELTA 的倍数，可为负）；其余输入为 0
    uint32_t m_vkey;      //虚拟键码；只有键盘输入有

    ShareInputEvent();

    /**
     *  构造一条鼠标输入。
     *  @param message WM_* 鼠标消息
     *  @param x、y    归一化坐标（0～kAnnotCoordMax）
     *  @param wheel   滚轮增量；不是滚轮时为 0
     */
    static ShareInputEvent Mouse(uint32_t message, int x, int y, int wheel);

    /**
     *  构造一条键盘输入。
     *  @param message WM_KEYDOWN / WM_KEYUP / WM_SYSKEYDOWN / WM_SYSKEYUP
     *  @param vkey    虚拟键码
     */
    static ShareInputEvent Key(uint32_t message, uint32_t vkey);
};

//画面主人一侧键鼠输入帧的接收方：推流管线在中转接收线程上收到输入帧、解析之后交给它（屏幕共享控制器实现，
//实现方只做拷贝与 PostMessage，核对与注入在 UI 线程上做）
class ShareInputSink
{
public:
    virtual ~ShareInputSink()
    {
    }

    /**
     *  收到一帧键鼠输入（中转接收线程上调用）。
     *  @param senderSeat 发送者座位号（服务端或 P2P 链路已改写，不能冒充）
     *  @param targetSeat 帧里写的目标座位号；不是本人座位时实现方丢弃
     *  @param ev         输入
     */
    virtual void OnInputFrame(int senderSeat, int targetSeat, const ShareInputEvent& ev) = 0;
};

//键鼠输入控制帧的组帧、解析与坐标换算；全部为静态方法，不可实例化
class ShareRemoteInput
{
public:
    /**
     *  组一帧键鼠输入控制帧（1860 的包体，未加密）：轨道为控制帧、首尾分片位都置上、时间戳 0。
     *  @param senderSeat 本人（控制者）的座位号；服务端会改写为发送连接登记的座位
     *  @param targetSeat 画面主人的座位号（0～254），服务端只转发给这个座位
     *  @param ev         输入
     *  @return 包体字节串；输入类型非法或座位号越界时返回空串
     */
    static std::string BuildInputBody(int senderSeat, int targetSeat, const ShareInputEvent& ev);

    /**
     *  解析一帧键鼠输入控制帧（重组之后的帧）。
     *  @param frame      重组之后的帧（ShareFrameReassembler 的输出）
     *  @param targetSeat [出参] 目标座位号
     *  @param out        [出参] 输入；鼠标坐标夹到 0～kAnnotCoordMax
     *  @return 是键鼠输入控制帧且输入命令完整、类型合法时返回 true
     */
    static bool ParseInputFrame(const ShareMediaFrame& frame, int& targetSeat, ShareInputEvent& out);

    /**
     *  观看窗里的点换成相对共享内容的归一化坐标（控制者一侧）。
     *  @param video 画面在观看窗里实际占的矩形（去掉黑边之后）
     *  @param pt    观看窗客户区坐标
     *  @param nx、ny [出参] 归一化坐标（0～kAnnotCoordMax）
     *  @return 点在画面矩形里（含左上边、不含右下边）且矩形非空时返回 true；画面以外的点不发送
     */
    static bool ViewToNormalized(const RECT& video, POINT pt, int& nx, int& ny);

    /**
     *  归一化坐标换回共享内容在画面主人屏幕上的位置（画面主人一侧）。
     *  @param source 共享内容的屏幕矩形：共享整块屏幕时为该显示器的矩形（多显示器时可为负坐标），只共享一个窗口时
     *                为窗口当前的矩形
     *  @param nx、ny 归一化坐标；越界的值先夹到 0～kAnnotCoordMax
     *  @return 屏幕坐标，落在 source 之内（右、下边缘取 right - 1、bottom - 1）
     */
    static POINT NormalizedToSource(const RECT& source, int nx, int ny);

    /**
     *  把一条输入换成远程桌面输入命令的字节（鼠标坐标为屏幕绝对坐标），交给 RdInputInjector::inject 注入。
     *  @param ev       输入
     *  @param screenPt 鼠标的屏幕坐标（NormalizedToSource 的结果）；键盘输入忽略
     *  @return 命令字节；输入类型非法时返回空串
     */
    static std::string ToRdCommand(const ShareInputEvent& ev, POINT screenPt);

private:
    ShareRemoteInput();
};

/**
 *  观看端（控制者一侧）把观看窗收到的窗口消息换成一条输入。规则：
 *    - 画面以外按下的鼠标按钮不发送；已发送过按下的按钮，松开时即使在画面以外也照发（坐标夹到画面边缘），
 *      避免画面主人那边的按钮一直按着；没发送过按下的松开不发送；
 *    - 双击消息当作一次按下发送：画面主人那边的系统会按两次点击的间隔自己合成双击；
 *    - 鼠标移动限频：距上次发送移动不足 kShareMoveIntervalMs 或位置没变时不发送；按钮与滚轮不限频；
 *    - 键盘的按下与抬起（含系统键）原样发送，字符消息不发送（画面主人那边由按键自己产生字符）。
 *  不加锁，只在观看窗的界面线程上使用。
 */
class ShareViewerInputTranslator
{
public:
    //鼠标移动的最短发送间隔，单位毫秒（约 60 次每秒，足够跟手，又不至于把中转塞满）
    static const uint32_t kShareMoveIntervalMs = 15;

    ShareViewerInputTranslator();

    /**
     *  换算一条窗口消息。
     *  @param msg      窗口消息：WM_MOUSEMOVE、WM_[LRM]BUTTONDOWN / UP / DBLCLK、WM_MOUSEWHEEL、WM_KEYDOWN / UP、
     *                  WM_SYSKEYDOWN / UP；其余消息返回 false
     *  @param wParam   原始 wParam：滚轮消息从中取增量，键盘消息即虚拟键码
     *  @param clientPt 鼠标位置（观看窗客户区坐标；WM_MOUSEWHEEL 的屏幕坐标须由调用方先换成客户区坐标）；键盘消息忽略
     *  @param video    画面在观看窗里实际占的矩形
     *  @param nowMs    当前时刻，单位毫秒（单调递增即可）
     *  @param out      [出参] 返回 true 时为要发送的输入
     *  @return 要发送返回 true；画面以外、限频、无关消息返回 false
     */
    bool Translate(UINT msg, WPARAM wParam, POINT clientPt, const RECT& video, uint32_t nowMs, ShareInputEvent& out);

    /**
     *  已发送过按下、还没发送松开的鼠标按钮对应的松开消息（WM_LBUTTONUP 等）。结束控制或观看窗失去焦点时据此补发松开。
     */
    std::vector<uint32_t> PendingButtonUps() const;

    //清空状态（开始或结束控制时调用）
    void Reset();

    /**
     *  控制中观看窗是否应当拦下这条消息、不交给自己的默认处理（Alt 不弹系统菜单、Esc 不退出全屏等）。
     *  @return 鼠标与键盘输入消息（含 WM_SYSCHAR、WM_CHAR）返回 true
     */
    static bool IsControlMessage(UINT msg);

private:
    bool     m_hasMove;        //是否发送过鼠标移动
    POINT    m_lastMovePt;     //上次发送的移动位置（客户区坐标）
    uint32_t m_lastMoveMs;     //上次发送移动的时刻，单位毫秒
    std::vector<uint32_t> m_pressed;   //已发送过按下、还没发送松开的按钮，存它们的松开消息（WM_LBUTTONUP 等）
};

/**
 *  画面主人一侧的系统级按键过滤器（第 25 条：普通按键与组合键转发，Win 键、Alt+Tab、Ctrl+Alt+Del 等系统级按键不转发）。
 *  按注入的先后喂入每条键盘输入，内部跟踪 Ctrl、Alt、Shift 的按下状态与已注入、尚未抬起的键。被拦下的按下与它对应的
 *  抬起都不注入。不加锁，只在画面主人的界面线程上使用。
 */
class ShareInputKeyFilter
{
public:
    ShareInputKeyFilter();

    /**
     *  判断一条键盘输入能否注入，并更新内部状态。
     *  @param message WM_KEYDOWN / WM_KEYUP / WM_SYSKEYDOWN / WM_SYSKEYUP；其余消息一律拦下
     *  @param vkey    虚拟键码
     *  @return 能注入返回 true；系统级按键（及它的抬起）返回 false
     */
    bool Allow(uint32_t message, uint32_t vkey);

    /**
     *  已注入按下、还没注入抬起的键，按按下的先后排列。结束控制时据此补发抬起，避免画面主人的电脑上留下按住的键。
     */
    std::vector<uint32_t> HeldKeys() const;

    //清空全部状态（结束控制、换了控制者时调用）
    void Reset();

private:
    //某个键是否在已注入、尚未抬起的列表里
    bool IsHeld(uint32_t vkey) const;

    std::vector<uint32_t> m_held;      //已注入按下、尚未注入抬起的键，按按下先后排列
    std::vector<uint32_t> m_blocked;   //按下时被拦下的键：它们的抬起同样不注入，抬起后从这里移除
};

//DecideShareInput 的结论
enum share_input_decision
{
    share_input_inject         = 0,   //注入
    share_input_not_controller = 1,   //发送者不是这块画面当前的控制者（没人在控制、控制已被收回或冒充），丢弃
    share_input_outside_window = 2,   //只共享一个窗口时鼠标点不在这个窗口上，丢弃
    share_input_not_foreground = 3,   //只共享一个窗口时这个窗口不在前台，键盘输入丢弃
    share_input_system_key     = 4,   //系统级按键（ShareInputKeyFilter 拦下），丢弃
};

/**
 *  画面主人一侧决定一条输入该不该注入。键盘输入只在前面几项都通过后才交给按键过滤器，以免被丢弃的按键改动它的状态。
 *  @param ev                输入
 *  @param senderSeat        这帧的发送者座位号（服务端已按发送连接改写，不能冒充）
 *  @param controllerSeat    当前控制者的座位号；没人控制时为 -1
 *  @param windowShare       是否只共享一个窗口
 *  @param pointOnWindow     只共享一个窗口时，鼠标的屏幕位置是否落在这个窗口（或它的子窗口）上；共享整块屏幕时忽略
 *  @param windowForeground  只共享一个窗口时，这个窗口是否在前台；共享整块屏幕时忽略
 *  @param filter            按键过滤器；键盘输入通过前几项检查后由它判断并更新状态
 *  @return 结论，取值见 share_input_decision
 */
int DecideShareInput(const ShareInputEvent& ev, int senderSeat, int controllerSeat, bool windowShare,
                     bool pointOnWindow, bool windowForeground, ShareInputKeyFilter& filter);

/**
 *  画面主人一侧判断本机有没有动静（第 25 条：画面主人动自己的鼠标即收回控制）。低级鼠标、键盘钩子看到的每条输入
 *  都交给它：系统标记为注入的输入（控制者经 RdInputInjector 注入的）一律不算；本机的按键、鼠标按钮立即算；
 *  本机的鼠标移动累计超过 kShareLocalMoveThresholdPx 才算，避免桌面轻微震动造成的一两个像素误判。
 */
class ShareLocalActivityDetector
{
public:
    //本机鼠标移动累计多少像素算画面主人自己在动，单位像素（横纵位移之和）
    static const int kShareLocalMoveThresholdPx = 24;

    ShareLocalActivityDetector();

    /**
     *  鼠标移动。
     *  @param pt       光标的屏幕坐标
     *  @param injected 是否为注入的输入（钩子结构里的 LLMHF_INJECTED 标志）
     *  @return 判定为画面主人本机操作时返回 true（此后一直返回 true，直到 Reset）
     */
    bool OnMouseMove(POINT pt, bool injected);

    /**
     *  鼠标按钮或滚轮。
     *  @param injected 是否为注入的输入
     *  @return 判定为本机操作时返回 true
     */
    bool OnMouseButton(bool injected);

    /**
     *  键盘按键。
     *  @param injected 是否为注入的输入（LLKHF_INJECTED 标志）
     *  @return 判定为本机操作时返回 true
     */
    bool OnKey(bool injected);

    /**
     *  记下注入时把光标挪到的屏幕位置。远程桌面的注入器用 SetCursorPos 挪光标，系统可能把这次挪动报给低级钩子而不带
     *  注入标志；此后落在这个位置上的不带标志的移动按注入处理，并从这个位置起算本机位移。
     *  @param pt 光标被挪到的屏幕坐标
     */
    void NoteInjectedPosition(POINT pt);

    //清空状态（开始控制时调用）
    void Reset();

private:
    bool  m_hasInjected; //m_injectedPt 是否有效
    POINT m_injectedPt;  //最近一次注入时光标被挪到的位置
    bool  m_triggered;   //是否已判定为本机操作
    bool  m_hasLast;     //m_last 是否有效：Reset 之后第一条本机鼠标移动只记位置、不计位移
    POINT m_last;        //上一条本机鼠标移动的位置
    int   m_movedPx;     //Reset 以来本机鼠标移动的累计位移（横纵位移之和），单位像素
};

} // namespace screenshare
