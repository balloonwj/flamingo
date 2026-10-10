/**
 *  录制条，RecordBar.h
 *
 *  录制开始后浮在被录内容所在显示器顶部居中的一条置顶小窗（docs/屏幕共享与录制方案.md 6.2）：红点 +「录制中 mm:ss」
 *  （暂停时为「已暂停 mm:ss」）+「暂停 / 继续」「停止」。排除在屏幕采集之外，不会被录进去。按钮点击向通知窗口
 *  （录屏控制器）PostMessage kMsgRecordBarCommand，由控制器处理。独立录屏与共享录制共用。
 *
 *  形态与共享控制条（CShareControlBar）同款：基于 balloonui DuiFrameWindow 的 WS_POPUP 无边框小窗，置顶、不抢焦点、
 *  不进任务栏，按钮与文字都是 balloonui 控件。
 *  2026-10-07 起与共享控制条统一样式：左侧状态区是闪烁的红点（暂停时变灰、不闪）+ 状态文字，右侧是一排方形图标按钮
 *  （ShareBarButtonArt），原来的按钮文字改作悬停提示；开关打开时按钮为蓝底，关着时图标带斜杠。
 *
 *  只录声音（第 24 期，docs/屏幕共享与录制方案.md 第 12.5 节）：Show 之前调 SetAudioOnly(true)，文字改为「录音中 mm:ss」，
 *  没有「鼠标效果」「画笔」「摄像头」；麦克风、电脑声音两个开关下面各有一条细电平条（DuiProgressBar），由控制器的电平
 *  定时器经 SetLevels 刷新。
 *
 *  收起与隐藏（2026-10-07，方案第 14.2 节，与共享控制条相同，状态由 screenshare::TopBarCollapse 计算）：最右端是「隐藏」
 *  「收起」两个按钮。收起后缩成顶边正中只剩标题栏的小把手，标题写录制时长，标题栏图标是闪烁的红点（暂停时为灰点）；鼠标
 *  移到把手上临时展开，离开 2 秒收回，临时展开时点「固定」回到一直展开。隐藏后屏幕上什么都不留，鼠标推到录制条原来所在的
 *  屏幕边缘时临时出现，离开 2 秒后再隐藏。每次开始录制（窗口新建）都从一直展开开始。
 *
 *  本文件遵循 balloonui 头文件约定：包含它之前调用方须已引入 ATL/WTL（即在 .cpp 里先 #include "stdafx.h"）。
 *
 *  balloonwj@qq.com   2026-10-03
 */
#pragma once

#include <stdint.h>
#include "../Share/ShareMouseEffects.h"   // ShareMouseEffectSettings（「鼠标效果」菜单，第 16 期）
#include "../Share/ShareBarPopupGuard.h"  // CShareBarPopupGuard：菜单开着时被隐藏的处理（2026-10-07）
#include "../Share/ShareBarButtonArt.h"   // ShareBarButtonArtCache：图标按钮的位图（2026-10-07）
#include "../Share/ShareTopBarCollapse.h" // TopBarCollapse：收起 / 隐藏的状态（2026-10-07，方案第 14.2 节）
#include "../../../third-party/balloonui/balloonui/Controls/Window/DuiFrameWindow.h"
#include "../../../third-party/balloonui/balloonui/DuiControl.h"
#include "../../../third-party/balloonui/balloonui/DuiNotify.h"

namespace balloonwjui
{
    class DuiLabel;
    class DuiButton;
    class DuiHBox;
    class DuiProgressBar;
}

namespace screenrecord {

//录制条
class CRecordBar : public balloonwjui::DuiFrameWindow
{
public:
    //按钮命令（kMsgRecordBarCommand 的 wParam）
    enum Command
    {
        kCmdPause  = 1,   //暂停
        kCmdResume = 2,   //继续
        kCmdStop   = 3,   //停止并保存
        kCmdMouseEffect = 4,   //「鼠标效果」菜单里的开关或颜色（第 16 期），lParam 为菜单项 id（见 kMenuMouseBase）
        kCmdPen    = 5,   //「画笔」（第 21 期）：进入或退出画笔模式
        kCmdMic    = 6,   //麦克风开关（2026-10-05）：lParam 为 1 打开、0 关闭
        kCmdSystemAudio = 7,   //电脑声音开关（2026-10-05）：lParam 为 1 打开、0 关闭
        kCmdCamera = 8,   //摄像头开关（2026-10-05）：lParam 为 1 打开、0 关闭
        kCmdCaption = 9,  //字幕开关（语音字幕 14.12）：lParam 为 1 打开、0 关闭；关闭期间不显示也不生成字幕
        kCmdAllowCapture = 10,   //「鼠标效果」菜单里点了「允许截图」（2026-10-08）：lParam 为 1 允许、0 恢复不允许
    };

    //对外可见的控件 id（单元测试按它找按钮）
    enum
    {
        kIdMouse  = 12,   //鼠标效果（第 16 期，点开菜单）；只录声音时没有（第 24 期起公开，单元测试据此检查）
        kIdMic    = 14,   //麦克风开关（2026-10-05）
        kIdSystem = 15,   //电脑声音开关（2026-10-05）
        kIdCamera = 16,   //摄像头开关（2026-10-05）
        kIdCaption = 17,  //字幕开关（语音字幕 14.12）
        kIdMicLevel    = 18,   //麦克风的电平条（第 24 期，只录声音时才有）
        kIdSystemLevel = 19,   //电脑声音的电平条（同上）
        kIdToggle = 20,   //收起 / 固定（2026-10-07）：最右端
        kIdHide   = 21,   //隐藏（2026-10-07）：在「收起」左边
    };

    //「鼠标效果」菜单项 id 的起点与占用范围（第 16 期，与共享控制条相同：200～224 由 BuildMouseEffectMenu 分配）
    static const UINT kMenuMouseBase = 200;
    static const UINT kMenuMouseSpan = 100;
    //「鼠标效果」菜单末尾「允许截图」（2026-10-08）的菜单项 id，在鼠标效果的范围之外
    static const UINT kMenuAllowCapture = 150;

    CRecordBar();
    ~CRecordBar();

    /**
     *  显示在 anchor 所在显示器工作区的顶部居中（重复调用会重新摆位）。
     *  @param anchor    被录内容的矩形（屏幕坐标）
     *  @param notifyWnd 通知窗口，不拥有
     *  @param topOffset 再往下挪的距离（像素）：共享录制时让出顶部的共享控制条或观看窗工具条
     */
    void Show(const RECT& anchor, HWND notifyWnd, int topOffset = 0);

    //隐藏并销毁；可重复调用
    void Hide();

    //刷新时长与暂停状态
    void SetState(int64_t elapsedMs, bool paused);

    /**
     *  鼠标效果的当前设置（第 16 期）：「鼠标效果」菜单据此给开关与颜色打勾。只记下，不影响画面。
     *  @param settings 当前设置
     */
    void SetMouseEffects(const screenshare::ShareMouseEffectSettings& settings);

    /**
     *  是否显示「画笔」按钮（第 21 期）：独立录屏显示，共享录制不显示（共享有自己的标注）。在 Show 之前调用。
     *  @param available 是否显示
     */
    void SetPenAvailable(bool available);

    //画笔模式是否开着（第 21 期）：开着时「画笔」按钮为主色
    void SetPenActive(bool active);

    /**
     *  录制中开关麦克风与电脑声音（2026-10-05）：available 为 true 时录制条显示「麦克风：开 / 关」「电脑声音：开 / 关」两个
     *  开关，点一下向通知窗口发 kCmdMic / kCmdSystemAudio（lParam 为想要的新状态），由控制器启停采集后再调本函数同步。
     *  共享中的录制不显示（声音由共享房间控制）。是否显示要在 Show 之前设好；开关状态随时可设。
     *  @param available 是否显示两个开关
     *  @param micOn     麦克风当前是否在录
     *  @param systemOn  电脑声音当前是否在录
     */
    void SetAudioToggles(bool available, bool micOn, bool systemOn);

    /**
     *  录制中开关摄像头（2026-10-05）：available 为 true 时显示「摄像头：开 / 关」，点一下向通知窗口发 kCmdCamera
     *  （lParam 为想要的新状态）。共享中的录制不显示。是否显示要在 Show 之前设好；开关状态随时可设。
     *  @param available 是否显示
     *  @param on        摄像头当前是否开着
     */
    void SetCameraToggle(bool available, bool on);

    /**
     *  录制中开关字幕（语音字幕 14.12）：available 为 true 时显示「字幕：开 / 关」，点一下向通知窗口发 kCmdCaption
     *  （lParam 为想要的新状态），由控制器切换后再调本函数同步。只在这次独立录屏勾了「生成字幕」时显示；共享中的录制不显示
     *  （共享有自己的字幕开关）。是否显示要在 Show 之前设好；开关状态随时可设。
     *  @param available 是否显示
     *  @param on        字幕当前是否打开
     */
    void SetCaptionToggle(bool available, bool on);

    //重新提到所有置顶窗口的最上面（第 21 期）：画笔的输入层盖上来之后调用，免得录制条点不到
    void BringToTop();

    /**
     *  是否是只录声音的录制条（第 24 期）。要在 Show 之前设好；为 true 时没有「鼠标效果」，文字为「录音中」，
     *  显示声音开关时每个开关下面带一条电平条。
     *  @param on 是否只录声音
     */
    void SetAudioOnly(bool on);

    //是否是只录声音的录制条
    bool IsAudioOnly() const;

    /**
     *  刷新两条电平条（第 24 期，只录声音时）。电平没变的那一条不重画。
     *  @param micPercent    麦克风的电平，0~100（RecordLevelPercent 的结果）；麦克风关着时传 0
     *  @param systemPercent 电脑声音的电平，0~100；关着时传 0
     */
    void SetLevels(int micPercent, int systemPercent);

    //当前状态文字（「录制中 mm:ss」「录音中 mm:ss」等，单元测试用）；窗口没建时为空
    std::wstring StatusText() const;

    //菜单是否正在弹出（单元测试用）
    bool IsPopupActive() const;

    //录制红点当前的状态，取值见 screenshare::ShareRecordDotState（单元测试用）
    int DotState() const;

    /**
     *  按某一时刻刷新红点的亮暗（闪烁定时器调用；单元测试直接传时刻）。状态没变时不重画。收起成小把手时，把手标题栏的
     *  红点图标随之切换。
     *  @param nowMs 当前时刻（毫秒，GetTickCount64 的值）
     */
    void TickBlink(int64_t nowMs);

    //收起 / 隐藏的当前状态，取值见 screenshare::TopBarState（单元测试用）
    int CollapseState() const;

    /**
     *  按给定的时刻与光标位置做一次收起 / 隐藏的周期检查（定时器传 GetTickCount64 与 GetCursorPos；单元测试直接传，
     *  不移动真实鼠标）。状态变了就重新摆放窗口。
     *  @param nowMs  当前时刻（毫秒）
     *  @param cursor 光标位置（屏幕坐标）
     */
    void TickCollapseAt(int64_t nowMs, POINT cursor);

    //按钮的控件 id（避开 DuiFrameWindow 标题栏保留的 1 / 2 / 3；2026-10-07 起公开，单元测试按它找按钮）
    enum
    {
        kIdPause = 10,   //暂停 / 继续
        kIdStop  = 11,   //停止
        kIdPen   = 13,   //画笔（第 21 期）
    };

    //投给自己的「弹出鼠标效果菜单」（2026-10-07）：等按钮的鼠标事件处理完再弹，菜单的嵌套消息循环不再套在按钮的点击通知里；
    //菜单开着时被要求隐藏，先关菜单、只隐藏窗口，菜单返回后再销毁（窗口私有消息，登记见开发约定文档「UI 约定」第 6 条）
    static const UINT kMsgShowMouseMenu = WM_APP + 0xD2;

    BEGIN_MSG_MAP(CRecordBar)
        MESSAGE_HANDLER(WM_DUI_NOTIFY, OnDuiNotify)
        MESSAGE_HANDLER(kMsgShowMouseMenu, OnShowMouseMenu)
        MESSAGE_HANDLER(WM_TIMER, OnTimerMsg)
        CHAIN_MSG_MAP(balloonwjui::DuiFrameWindow)
    END_MSG_MAP()

private:
    LRESULT OnDuiNotify(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    //kMsgShowMouseMenu：弹出「鼠标效果」菜单；菜单期间被要求隐藏时，菜单返回后在这里销毁窗口
    LRESULT OnShowMouseMenu(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    //红点闪烁的定时器；其余定时器交给基类
    LRESULT OnTimerMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    void BuildUi();
    /**
     *  在横条里加一个方形图标按钮（外观由 refresh 装上）。
     *  @param row 放进哪一行（不拥有）
     *  @param id  控件 id
     *  @return 按钮（控件树持有，仅引用）
     */
    balloonwjui::DuiButton* addIconButton(balloonwjui::DuiHBox* row, UINT id);
    /**
     *  按按钮种类与开关状态装上外观与悬停提示；按钮为 NULL 时什么也不做。
     *  @param button 按钮（不拥有）
     *  @param action 种类，取值见 screenshare::ShareBarAction
     *  @param on     开关状态
     */
    void applyArt(balloonwjui::DuiButton* button, int action, bool on);
    /**
     *  建一个声音开关（第 24 期起只录声音时开关下面带电平条）。
     *  @param row     放进哪一行（不拥有）
     *  @param id      开关的控件 id
     *  @param levelId 电平条的控件 id
     *  @param width   开关的宽度
     *  @param btnOut  [出参] 开关（控件树持有，仅引用）
     *  @param levelOut [出参] 电平条（同上）；不是只录声音时为 NULL
     */
    void addAudioToggle(balloonwjui::DuiHBox* row, UINT id, UINT levelId, int width, balloonwjui::DuiButton*& btnOut,
                        balloonwjui::DuiProgressBar*& levelOut);
    //整条的宽度：按有哪些按钮累加
    int barWidth() const;
    //状态文字的宽度：按最长的写法量，非简体中文界面下按译文加宽
    int statusWidth() const;
    void refresh();
    //在「鼠标效果」按钮下方弹出菜单，选中后通知控制器（第 16 期）
    void showMouseMenu();
    //按 m_collapse 的状态摆放窗口（整条、小把手或隐藏），刷新「收起 / 固定」按钮并开关检查鼠标的定时器
    void applyCollapse();
    //收起成小把手时：标题写录制时长，标题栏图标按红点当前的亮暗取
    void applyHandleLook();
    /**
     *  取小把手标题栏上的红点图标（画在条的底色上，第一次用到时生成，析构时释放）。
     *  @param dotState 取值见 screenshare::ShareRecordDotState
     *  @return 位图（本对象持有）；生成失败返回 NULL
     */
    HBITMAP handleDotBitmap(int dotState);

    HWND                    m_notifyWnd;   //通知窗口（不拥有）
    balloonwjui::DuiLabel*  m_label;       //「录制中 mm:ss」（控件树持有，本指针仅引用）
    balloonwjui::DuiButton* m_pauseBtn;    //暂停 / 继续（同上）
    int64_t                 m_elapsedMs;   //已录时长
    bool                    m_paused;      //是否暂停
    HBITMAP                 m_bgBitmap;    //纯色背景图（本类生成、析构释放）
    balloonwjui::DuiButton* m_mouseBtn;    //「鼠标效果 ▾」按钮（控件树持有，本指针仅引用；第 16 期）
    screenshare::ShareMouseEffectSettings m_mouseSettings;   //鼠标效果的当前设置（第 16 期），菜单据此打勾
    balloonwjui::DuiButton* m_penBtn;      //「画笔」按钮（控件树持有，本指针仅引用；第 21 期）；共享录制时没有
    bool                    m_penAvailable;   //是否显示「画笔」按钮（第 21 期）
    bool                    m_penActive;   //画笔模式是否开着（第 21 期）
    bool                    m_audioAvailable;   //是否显示麦克风、电脑声音两个开关（2026-10-05）；共享录制时为 false
    bool                    m_micOn;       //麦克风当前是否在录（控制器同步过来的状态）
    bool                    m_systemOn;    //电脑声音当前是否在录（同上）
    balloonwjui::DuiButton* m_micBtn;      //麦克风开关（控件树持有，本指针仅引用）；没有时为 NULL
    balloonwjui::DuiButton* m_systemBtn;   //电脑声音开关（同上）
    bool                    m_cameraAvailable;   //是否显示摄像头开关（2026-10-05）；共享录制时为 false
    bool                    m_cameraOn;    //摄像头当前是否开着（控制器同步过来的状态）
    balloonwjui::DuiButton* m_cameraBtn;   //摄像头开关（控件树持有，本指针仅引用）；没有时为 NULL
    bool                    m_captionAvailable;   //是否显示字幕开关（语音字幕 14.12）；没勾「生成字幕」或共享录制时为 false
    bool                    m_captionOn;   //字幕当前是否打开（控制器同步过来的状态）
    balloonwjui::DuiButton* m_captionBtn;  //字幕开关（控件树持有，本指针仅引用）；没有时为 NULL
    bool                    m_audioOnly;   //是否是只录声音的录制条（第 24 期）
    balloonwjui::DuiProgressBar* m_micLevel;      //麦克风的电平条（控件树持有，本指针仅引用）；不是只录声音时为 NULL
    balloonwjui::DuiProgressBar* m_systemLevel;   //电脑声音的电平条（同上）
    int                     m_micPercent;     //麦克风电平条当前显示的值（0~100）
    int                     m_systemPercent;  //电脑声音电平条当前显示的值（0~100）
    screenshare::CShareBarPopupGuard m_popup;  //「鼠标效果」菜单的弹出状态与推迟销毁（2026-10-07）
    balloonwjui::DuiLabel*  m_dot;         //录制红点（控件树持有，本指针仅引用，2026-10-07 起闪烁）
    int                     m_dotState;    //红点当前的状态，取值见 screenshare::ShareRecordDotState；状态变了才重新上色
    balloonwjui::DuiButton* m_stopBtn;     //「停止」按钮（控件树持有，本指针仅引用）
    balloonwjui::DuiButton* m_toggleBtn;   //「收起 / 固定」按钮（控件树持有，本指针仅引用，2026-10-07）
    balloonwjui::DuiButton* m_hideBtn;     //「隐藏」按钮（同上）
    screenshare::TopBarCollapse m_collapse;   //收起 / 隐藏的状态（2026-10-07）；Hide 时回到一直展开
    RECT                    m_expandedRect;   //整条显示时的窗口矩形（屏幕坐标），Show 时算出；小把手与边缘感应区据此计算
    RECT                    m_monitorRect;    //录制条所在显示器的整块矩形（屏幕坐标），隐藏后的边缘感应区据此计算
    HBITMAP                 m_handleDots[screenshare::share_record_dot_paused + 1];   //小把手的红点图标，下标为红点状态；
                                                                                     //本对象持有，析构时释放
    screenshare::ShareBarButtonArtCache m_art;   //图标按钮的四态位图（声明在全部按钮指针之后；按钮随窗口销毁，
                                                 //析构时它们已不再引用这些位图）
};

} // namespace screenrecord
