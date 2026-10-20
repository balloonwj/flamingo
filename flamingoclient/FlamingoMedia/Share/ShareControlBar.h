/**
 *  屏幕共享发起人的控制条，ShareControlBar.h
 *
 *  共享开始后，在被共享内容所在显示器的工作区顶部居中浮一条置顶控制条（docs/屏幕共享与录制方案.md 5.2 第 2 步）：
 *  「正在共享 12:34」（第 13 期显示共享时长；设了加入密码时后接「· 密码 482915」，第 12 期；强制模式时后接「· 强制模式」）
 *  +「N 人观看 ▾」按钮（第 13 期，点开观看者名单）+ 麦克风、摄像头、电脑声音（第 4、5 期）、标注（第 6 期）、录制与「更多」（第 7 期；菜单里是
 *  全员静音、禁止 / 允许他人录制）+「暂停 / 继续」「切换内容」「结束共享」。
 *  按钮点击不直接操作推流，而是向通知窗口（屏幕共享控制器的消息窗口）PostMessage kMsgShareBarCommand，由控制器统一处理。
 *  标注的清空与权限在标注工具条上。「更多」菜单用 balloonui 的 DuiMenu。
 *  2026-10-07 起与录制条统一样式：左侧状态区是状态点（共享中绿色、暂停时灰色）、录下这场共享时闪烁的录制红点与状态文字，
 *  右侧是一排方形图标按钮（ShareBarButtonArt），原来的按钮文字改作悬停提示；开关打开时按钮为蓝底（录制为红底），麦克风等
 *  关着时图标带斜杠；字幕能使用时多一个「为我的发言生成字幕」按钮。
 *  「更多」菜单（2026-10-07 修正）：点击时先 PostMessage kMsgShowMoreMenu 给自己，等鼠标事件处理完再弹；菜单开着时被要求
 *  隐藏（控制器结束共享），先关掉菜单、只隐藏窗口，等 TrackPopup 返回后再销毁（CShareBarPopupGuard）；菜单开着时点了条上
 *  的任意按钮也先关菜单。修正前菜单会一直留在屏幕上，再次发起共享时 atlwin.h 断言 m_hWnd == 0。
 *
 *  形态与远程桌面受控提示条（CRdControlledBar）同款：基于 balloonui DuiFrameWindow 的 WS_POPUP 无边框小窗，
 *  置顶、不抢焦点、不进任务栏；另外排除在屏幕采集之外，不会被采进共享画面。2026-10-14 起建窗、摆位与收起 / 隐藏由
 *  公共基类 CShareTopBar（ShareTopBar.h）负责，与共享者控制条、观看端浮动条一致。
 *
 *  本文件遵循 balloonui 头文件约定：包含它之前调用方须已引入 ATL/WTL（即在 .cpp 里先 #include "stdafx.h"）。
 *
 *  balloonwj@qq.com   2026-10-02
 */
#pragma once

#include <string>
#include "../../../third-party/balloonui/balloonui/Controls/Window/DuiFrameWindow.h"
#include "../../../third-party/balloonui/balloonui/DuiControl.h"
#include "../../../third-party/balloonui/balloonui/DuiNotify.h"
#include "ShareTopBar.h"           // CShareTopBar：顶部浮动条的公共基类（建窗、摆位、收起 / 隐藏，2026-10-14）
#include "ShareMouseEffects.h"     // ShareMouseEffectSettings（「更多」菜单里的鼠标效果，第 16 期）
#include "ShareBarPopupGuard.h"    // CShareBarPopupGuard：菜单开着时被隐藏的处理（2026-10-07）
#include "ShareBarButtonArt.h"     // ShareBarButtonArtCache：图标按钮与闪烁的录制红点（2026-10-07）
#include "ShareRoomPolicy.h"       // ShareBoardCloseItem：「更多」菜单的「关闭白板 ▸」（2026-10-08）

namespace balloonwjui
{
    class DuiLabel;
    class DuiButton;
    class DuiBadge;
    class DuiHBox;
}

namespace screenshare {

//屏幕共享发起人的控制条
class CShareControlBar : public CShareTopBar
{
public:
    //按钮命令：随 kMsgShareBarCommand 的 wParam 发给控制器
    enum Command
    {
        kCmdStop   = 1,   //结束共享
        kCmdPause  = 2,   //暂停
        kCmdResume = 3,   //继续
        kCmdSwitch = 4,   //切换共享内容
        kCmdMicOn  = 5,   //打开麦克风
        kCmdMicOff = 6,   //关闭麦克风
        kCmdMuteAll = 7,  //全员静音（关掉发起人以外所有人的麦克风）
        kCmdSysAudioOn  = 8,   //开始共享电脑声音
        kCmdSysAudioOff = 9,   //停止共享电脑声音
        kCmdCameraOn  = 10,    //打开摄像头
        kCmdCameraOff = 11,    //关闭摄像头
        kCmdAnnotate  = 12,    //进入或退出标注模式
        kCmdRecordStart = 13,  //开始录制
        kCmdRecordStop  = 14,  //停止录制
        kCmdRecordPermission = 15,   //「更多」菜单：切换他人的录制权限，lParam 为 1 允许、0 禁止
        kCmdViewers   = 16,    //点了「N 人观看 ▾」：打开或关闭观看者名单（第 13 期）
        kCmdEditInfo  = 17,    //「更多」菜单：修改共享标题与描述（第 15 期），控制器打开修改小窗
        kCmdMouseEffect = 18,  //「更多」菜单：鼠标效果的开关或颜色（第 16 期），lParam 为菜单项 id（见 kMenuMouseBase）
        kCmdRevokeControl = 19,   //「收回控制」（第 18 期）：有人在控制本人屏幕时才出现，控制器发 1735 收回
        kCmdStopMine    = 20,  //「更多」菜单：停止我的共享（第 22 期第 29 条）：只停发起人自己的画面，仍是主持人、留在房间
        kCmdResumeMine  = 21,  //「更多」菜单：恢复我的共享（停掉自己的画面之后才出现）
        kCmdOtherScreens = 22, //「更多」菜单：其他人的屏幕（第 22 期）：重新打开看其他共享者画面的窗口，有人在共享时才出现
        kCmdBoard       = 23,  //「更多」菜单：新建白板 / 关闭我的白板（第 23 期）
        kCmdScreenBoard = 24,  //「更多」菜单：屏幕当白板 / 退出屏幕白板（第 23 期，第 31 条）：只在共享一个窗口时出现
        kCmdCaptionSelf = 25,  //「更多」菜单：为我的发言生成字幕（语音字幕），lParam 为 1 打开、0 关闭；字幕能使用时才出现
        kCmdCaptionShow = 26,  //「更多」菜单：显示字幕（语音字幕），lParam 为 1 打开、0 关闭；字幕能使用时才出现
        kCmdLink        = 27,  //「更多」菜单：分享链接一组（第 25 期），lParam 取值见 LinkMenuAction
        kCmdCloudRecord = 28,  //「更多」菜单：云端录制一组（第 27b 期），lParam 为 Protocol.h 的 cloudrecord_control_action
        kCmdAllowCapture = 29, //「更多」菜单：允许截图（2026-10-08），lParam 为 1 允许、0 恢复不允许
        kCmdCloseBoard   = 30, //「更多」菜单「关闭白板 ▸」选了一块（2026-10-08），lParam 为白板的建立者 userid
    };

    CShareControlBar();
    ~CShareControlBar();

    /**
     *  在指定显示器的工作区顶部居中显示控制条（重复调用会重建内容并重新摆位）。
     *  @param sourceRect 被共享内容的矩形，控制条摆在它所在的显示器上
     *  @param notifyWnd  通知窗口，按钮点击时向它 PostMessage kMsgShareBarCommand；不拥有
     */
    void Show(const RECT& sourceRect, HWND notifyWnd);

    //隐藏并销毁控制条；可重复调用
    void Hide();

    //刷新观看人数（不含发起人自己），显示在「N 人观看 ▾」按钮上
    void SetViewerCount(int count);

    /**
     *  刷新共享时长（第 13 期），显示在提示文字开头「正在共享 12:34」。
     *  @param text 时长文字（见 FormatShareDuration）；为空时只写「正在共享」
     */
    void SetDurationText(const std::wstring& text);

    /**
     *  设置画面发给观看者的链路实际路径（2026-10-04 起）：直连时在提示文字后显示「P2P」徽章。控制器每秒刷新时长时一并调用。
     *  @param linkPath 取值同 ShareHostStreamer::P2PViewerLinkPath
     */
    void SetP2PLinkPath(int linkPath);

    //是否强制模式（第 13 期）：是的话提示文字里标出「强制模式」
    void SetForced(bool forced);

    /**
     *  共享标题（第 15 期），写在提示文字开头，如「周会演示 · 正在共享 12:34」。
     *  @param title 共享标题；为空时不写标题一段
     */
    void SetShareTitle(const std::wstring& title);

    //分享链接菜单项（kCmdLink 的 lParam，第 25 期，docs/屏幕共享与录制方案.md 第 12.4.5 节）
    enum LinkMenuAction
    {
        kLinkCopy       = 1,   //复制分享链接（没有链接时由控制器先向服务端取得）
        kLinkSendInvite = 2,   //发送邀请到…：选会话后以文字消息发出
        kLinkReset      = 3,   //重置链接：旧链接作废
        kLinkDisable    = 4,   //停用链接
        kLinkNoPassword = 5,   //切换「点链接加入免输密码」（房间设了密码时才有这一项）
    };

    /**
     *  分享链接的当前状态（第 25 期）：「更多」菜单据此决定显示哪几项、免输密码是否打勾。只记下，下次打开菜单时生效。
     *  @param active     主持人是否已经生成了有效的链接（有时才显示「重置链接」「停用链接」）
     *  @param noPassword 是否打开了「点链接加入免输密码」
     */
    void SetLinkState(bool active, bool noPassword);

    /**
     *  云端录制的状态（第 27b 期）：决定「更多」菜单里云端录制一组怎么显示，以及状态文字前面的「云端录制中」。
     *  @param available 本企业开通了云端录制（没开通且没在录时不显示这一组）
     *  @param recording 这场共享正在云端录制（含暂停）
     *  @param paused    云端录制暂停中
     */
    void SetCloudRecordState(bool available, bool recording, bool paused);

    /**
     *  只设置本企业是否开通了云端录制，不改变正在录制、暂停的状态。主持人开始推流时调用：那时还没有收到 1725，
     *  SetCloudRecordState 尚未被调用过。只记下，下次打开「更多」菜单时生效。
     *  @param available 本企业开通了云端录制
     */
    void SetCloudRecordAvailable(bool available);

    /**
     *  「更多」菜单「关闭白板 ▸」子菜单的各项（2026-10-08）：别人建的白板，控制器在每次 1725 后设置。
     *  为空时不显示这一项。只记下，下次打开「更多」菜单时生效。
     *  @param items 子菜单各项，见 BuildShareBoardCloseItems
     */
    void SetOtherBoards(const std::vector<ShareBoardCloseItem>& items);

    //「更多」菜单里鼠标效果菜单项 id 的起点（第 16 期）：200～224 由 BuildMouseEffectMenu 分配，200～299 都留给它
    static const UINT kMenuMouseBase = 200;
    //鼠标效果菜单项 id 占用的范围大小
    static const UINT kMenuMouseSpan = 100;

    /**
     *  鼠标效果的当前设置（第 16 期）：「更多」菜单据此给开关与颜色打勾。只记下，不影响画面。
     *  @param settings 当前设置
     */
    void SetMouseEffects(const ShareMouseEffectSettings& settings);

    /**
     *  取「N 人观看 ▾」按钮在屏幕上的矩形，观看者名单贴在它下方弹出。
     *  @param out [出参] 屏幕坐标
     *  @return 控制条已显示且按钮存在时返回 true
     */
    bool GetViewersButtonScreenRect(RECT& out) const;

    //刷新暂停状态：暂停时按钮显示「继续」，提示文字显示「已暂停」
    void SetPaused(bool paused);

    //按被共享内容的新矩形重新摆位（切换了共享内容、或共享的窗口挪到了别的显示器）
    void MoveToSource(const RECT& sourceRect);

    //刷新麦克风状态：打开时按钮显示「关闭麦克风」，关闭时显示「打开麦克风」
    void SetMicOn(bool on);

    //刷新电脑声音状态：共享中按钮显示「停止电脑声音」，否则显示「共享电脑声音」
    void SetSystemAudioOn(bool on);

    //刷新摄像头状态：打开时按钮显示「关闭摄像头」，关闭时显示「打开摄像头」
    void SetCameraOn(bool on);

    //刷新标注模式：标注中按钮显示「退出标注」，否则显示「标注」
    void SetAnnotating(bool on);

    //刷新录制状态：录制中按钮显示「停止录制」，否则显示「录制」；停止时一并清掉录制时长
    void SetRecording(bool on);

    /**
     *  录下这场共享的已录时长（2026-10-07，方案第 14.2 节：共享中的录制不再弹出单独的录制条）。录制中写在状态文字最前面，
     *  如「录制 03:21 · 正在共享 12:34」；控制器每秒刷新一次。没在录制时不显示。
     *  @param text 已录时长文字（FormatRecordDuration 的结果）；为空表示不显示
     */
    void SetRecordDuration(const std::wstring& text);

    //状态文字当前的内容（单元测试用）；窗口没建时为空
    std::wstring TipTextForTest() const;

    //「更多」菜单里录制权限一项的文字：他人能录制时为「禁止他人录制」
    void SetRecordAllowed(bool allowed);

    /**
     *  刷新说话人提示，接在提示文字后面（如「李四 正在说话」）。
     *  @param text 说话人提示；为空表示没有人在说话
     */
    void SetSpeakingText(const std::wstring& text);

    /**
     *  设置要显示的加入密码（第 12 期），接在提示文字的时长后面（如「· 密码 482915」）。控制条排除在屏幕采集之外，
     *  密码不会被共享出去。
     *  @param password 6 位数字密码；为空表示没设密码
     */
    void SetPassword(const std::wstring& password);

    /**
     *  设置正在控制本人屏幕的人（第 18 期）：非空时提示文字开头写「某某 正在控制你的屏幕」，并显示「收回控制」按钮；
     *  为空时两者都去掉。有变化才重排。
     *  @param controllerName 控制者的显示名；为空表示没人在控制
     */
    void SetControlledBy(const std::wstring& controllerName);

    //「更多」菜单里自己画面一项的文字（第 22 期）：停掉了自己的画面时为「恢复我的共享」，否则为「停止我的共享」
    void SetMineStopped(bool stopped);

    /**
     *  其他人正在共享的画面数（第 22 期）：大于 0 时「更多」菜单里出现「共享画面」（2026-10-12 前为「其他人的屏幕（N）」；
     *  窗口里也有主持人自己的那一块，不再写人数）。
     *  @param count 主持人以外正在共享的人数
     */
    void SetOtherScreenCount(int count);

    //「更多」菜单里白板一项的文字（第 23 期）：本人的白板开着时为「关闭我的白板」，否则为「新建白板」
    void SetBoardOpen(bool open);

    /**
     *  「更多」菜单里「屏幕当白板」一项（第 23 期）。
     *  @param available 是否显示这一项（只共享一个窗口、或已在屏幕白板里时显示）
     *  @param active    是否已在屏幕白板里：是时文字为「退出屏幕白板」
     */
    void SetScreenBoard(bool available, bool active);

    /**
     *  「更多」菜单里字幕的两个勾选项（语音字幕，docs/语音字幕方案.md 第 9 节）。只记下，菜单弹出时据此生成。
     *  @param available 字幕能否使用：不能使用（企业关闭了字幕、字幕组件缺失）时菜单里没有这两项
     *  @param selfOn    「为我的发言生成字幕」是否打开
     *  @param showOn    「显示字幕」是否打开
     */
    void SetCaptionState(bool available, bool selfOn, bool showOn);

    //投给自己的「弹出更多菜单」（2026-10-07）：等「更多」按钮的鼠标事件处理完再弹，菜单的嵌套消息循环不再套在按钮的
    //点击通知里（窗口私有消息，登记见开发约定文档「UI 约定」第 6 条）
    static const UINT kMsgShowMoreMenu = WM_APP + 0xD1;

    //菜单或取色框是否正在弹出（单元测试用）
    bool IsPopupActive() const;

    //录制红点当前的状态，取值见 ShareRecordDotState（单元测试用）
    int RecordDotState() const;

    /**
     *  按某一时刻刷新录制红点的亮暗（闪烁定时器调用；单元测试直接传时刻）。状态没变时不重画。
     *  @param nowMs 当前时刻（毫秒，GetTickCount64 的值）
     */
    void TickBlink(int64_t nowMs);

    BEGIN_MSG_MAP(CShareControlBar)
        MESSAGE_HANDLER(WM_DUI_NOTIFY, OnDuiNotify)
        MESSAGE_HANDLER(WM_TIMER, OnTimerMsg)
        MESSAGE_HANDLER(kMsgShowMoreMenu, OnShowMoreMenu)
        CHAIN_MSG_MAP(CShareTopBar)
    END_MSG_MAP()

    //按钮的控件 id（对外可见，单元测试按它找按钮）：避开 DuiFrameWindow 标题栏保留的 1 / 2 / 3
    enum
    {
        kIdStop   = 10,   //结束共享
        kIdPause  = 11,   //暂停 / 继续
        kIdSwitch = 12,   //切换内容
        kIdMic    = 13,   //打开 / 关闭麦克风
        kIdMuteAllRetired = 14,   //原「全员静音」按钮，2026-10-03 移入「更多」菜单，编号留空不再使用
        kIdSysAudio = 15, //共享 / 停止电脑声音
        kIdCamera = 16,   //打开 / 关闭摄像头
        kIdAnnotate = 17, //标注 / 退出标注
        kIdRecord   = 18, //录制 / 停止录制
        kIdMore     = 19, //更多（弹出菜单）
        kIdToggle   = 20, //收起 / 固定（第 10 期 10c）
        kIdViewers  = 21, //「N 人观看 ▾」（第 13 期）
        kIdRevokeControl = 22,   //「收回控制」（第 18 期），只在有人控制本人屏幕时可见
        kIdCaption  = 23, //为我的发言生成字幕（2026-10-07），字幕能使用时才有
        kIdHide     = 24, //隐藏控制条（2026-10-07，方案第 14.2 节），在「收起 / 固定」左边
    };

private:
    //「更多」菜单的命令
    enum
    {
        kMenuMuteAll    = 101,   //全员静音
        kMenuRecordPerm = 102,   //禁止 / 允许他人录制
        kMenuEditInfo   = 103,   //修改标题与描述（第 15 期）
        kMenuMine       = 104,   //停止我的共享 / 恢复我的共享（第 22 期）
        kMenuOthers     = 105,   //其他人的屏幕（第 22 期）
        kMenuBoard      = 106,   //新建白板 / 关闭我的白板（第 23 期）
        kMenuScreenBoard = 107,  //屏幕当白板 / 退出屏幕白板（第 23 期）
        kMenuCaptionSelf = 108,  //为我的发言生成字幕（语音字幕，勾选项）
        kMenuCaptionShow = 109,  //显示字幕（语音字幕，勾选项）
        kMenuLinkCopy    = 110,  //复制分享链接（第 25 期）
        kMenuLinkSend    = 111,  //发送邀请到…（第 25 期）
        kMenuLinkNoPassword = 112,  //点链接加入免输密码（第 25 期，勾选项）
        kMenuLinkReset   = 113,  //重置链接（第 25 期）
        kMenuLinkDisable = 114,  //停用链接（第 25 期）
        kMenuCloudStart  = 115,  //开始云端录制（第 27b 期）
        kMenuCloudPause  = 116,  //暂停 / 继续云端录制
        kMenuCloudStop   = 117,  //停止云端录制
        kMenuAllowCapture = 118, //允许截图（2026-10-08，勾选项）
        kMenuCloseBoardRoot = 129, //「关闭白板 ▸」子菜单的入口项（2026-10-08）
        kMenuCloseBoardBase = 130, //「关闭白板 ▸」子菜单的第一项，第 i 块别人的白板为 kMenuCloseBoardBase + i（2026-10-08）
    };

    LRESULT OnDuiNotify(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    //录制红点的闪烁定时器；收起检查的定时器与其余定时器交给基类
    LRESULT OnTimerMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    //kMsgShowMoreMenu：弹出「更多」菜单；菜单期间被要求隐藏时，菜单返回后在这里销毁窗口
    LRESULT OnShowMoreMenu(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    void EnsureWindow();
    void BuildUi();
    void refreshText();
    /**
     *  按按钮种类与开关状态装上外观与悬停提示；按钮为 NULL 时什么也不做。
     *  @param button 按钮（不拥有）
     *  @param action 种类，取值见 ShareBarAction
     *  @param on     开关状态
     */
    void applyArt(balloonwjui::DuiButton* button, int action, bool on);
    //弹出「更多」菜单（在按钮下方）
    void showMoreMenu();

    HWND                    m_notifyWnd;     //通知窗口（不拥有）
    balloonwjui::DuiLabel*  m_tipLabel;      //提示文字（控件树持有，本指针仅引用）
    balloonwjui::DuiBadge*  m_p2pBadge;      //「P2P」徽章（2026-10-04 起，控件树持有，本指针仅引用）
    int                     m_linkPath;      //画面发给观看者的链路实际路径（见 SetP2PLinkPath）；重建界面时据它决定徽章显隐
    balloonwjui::DuiButton* m_pauseButton;   //暂停 / 继续按钮（同上）
    balloonwjui::DuiButton* m_micButton;     //麦克风按钮（同上）
    bool                    m_micOn;         //麦克风是否打开
    balloonwjui::DuiButton* m_sysAudioButton;   //电脑声音按钮（控件树持有，本指针仅引用）
    bool                    m_sysAudioOn;    //是否在共享电脑声音
    balloonwjui::DuiButton* m_cameraButton;  //摄像头按钮（控件树持有，本指针仅引用）
    bool                    m_cameraOn;      //摄像头是否打开
    balloonwjui::DuiButton* m_annotButton;   //标注按钮（控件树持有，本指针仅引用）
    bool                    m_annotating;    //是否在标注模式
    balloonwjui::DuiButton* m_recordButton;  //录制按钮（控件树持有，本指针仅引用）
    bool                    m_recording;     //是否在录制
    std::wstring            m_recordDuration;   //录下这场共享的已录时长文字（2026-10-07）；没在录制时为空
    bool                    m_recordAllowed; //他人能否录制
    balloonwjui::DuiButton* m_moreButton;    //「更多」按钮（控件树持有，本指针仅引用）
    std::wstring            m_speaking;      //说话人提示；为空表示没有人在说话
    std::wstring            m_password;      //加入密码（第 12 期）；为空表示没设密码
    std::wstring            m_duration;      //共享时长文字（第 13 期）；为空表示还没开始计时
    bool                    m_forced;        //是否强制模式（第 13 期）
    std::wstring            m_shareTitle;    //共享标题（第 15 期）；为空表示没有（老服务端或共享已结束）
    ShareMouseEffectSettings m_mouseSettings; //鼠标效果的当前设置（第 16 期），「更多」菜单据此打勾
    balloonwjui::DuiButton* m_viewersButton; //「N 人观看 ▾」按钮（第 13 期，控件树持有，本指针仅引用）
    int                     m_viewerCount;   //观看人数
    bool                    m_paused;        //是否已暂停
    RECT                    m_sourceRect;    //被共享内容的矩形
    balloonwjui::DuiButton* m_revokeButton;  //「收回控制」按钮（第 18 期，控件树持有，本指针仅引用）；没人控制时隐藏
    std::wstring            m_controllerName;   //正在控制本人屏幕的人的显示名（第 18 期）；为空表示没人在控制
    bool                    m_mineStopped;   //发起人是否停掉了自己的画面（第 22 期），「更多」菜单据此显示停止或恢复
    std::vector<ShareBoardCloseItem> m_otherBoards;   //「关闭白板 ▸」子菜单的各项（2026-10-08）；为空时不显示这一项
    int                     m_otherScreens;  //主持人以外正在共享的人数（第 22 期），大于 0 时菜单里有「其他人的屏幕」
    bool                    m_boardOpen;     //本人的白板是否开着（第 23 期），「更多」菜单据此显示新建或关闭
    bool                    m_screenBoardAvailable;   //「更多」菜单里是否显示「屏幕当白板」（第 23 期）
    bool                    m_screenBoardActive;      //是否已在屏幕白板里（第 23 期），菜单据此显示「退出屏幕白板」
    bool                    m_captionAvailable;   //字幕能否使用（语音字幕）；不能使用时「更多」菜单里没有字幕两项
    bool                    m_captionSelf;        //「为我的发言生成字幕」是否打开
    bool                    m_captionShow;        //「显示字幕」是否打开
    bool                    m_rebuildAfterMenu;   //「更多」菜单开着时字幕可用性变了、没能重建界面：菜单关闭后立即重建（2026-10-07）
    bool                    m_linkActive;        //是否已经生成了有效的分享链接（第 25 期），有时菜单里才有「重置链接」「停用链接」
    bool                    m_linkNoPassword;     //是否打开了「点链接加入免输密码」（第 25 期）
    bool                    m_cloudRecordAvailable;   //本企业是否开通了云端录制（第 27b 期），决定菜单里有没有「开始云端录制」
    bool                    m_cloudRecording;     //这场共享是否正在云端录制（含暂停），状态文字前面写「云端录制中」
    bool                    m_cloudRecordPaused;  //云端录制是否暂停中
    CShareBarPopupGuard     m_popup;              //「更多」菜单的弹出状态与推迟销毁（2026-10-07）
    balloonwjui::DuiLabel*  m_statusDot;          //状态点：共享中绿色、暂停时灰色（控件树持有，本指针仅引用）
    balloonwjui::DuiLabel*  m_recordDot;          //录制红点：录下这场共享时显示并闪烁（同上）
    int                     m_recordDotState;     //录制红点当前的状态，取值见 ShareRecordDotState；状态变了才重新上色
    balloonwjui::DuiButton* m_captionButton;      //「为我的发言生成字幕」按钮（同上）；字幕不能使用时为 NULL
    balloonwjui::DuiButton* m_switchButton;       //「切换内容」按钮（同上）
    balloonwjui::DuiButton* m_stopButton;         //「结束共享」按钮（同上）
    HBITMAP                 m_peopleIcon;         //观看者按钮前面的人形图标（本类生成、析构释放）
};

} // namespace screenshare
