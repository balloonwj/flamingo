/**
 *  屏幕共享房间里与界面无关的规则（第 13 期），ShareRoomPolicy.h
 *
 *  docs/屏幕共享与录制方案.md 第 13 期：
 *    - 共享时长：发起端控制条与观看端工具条都显示，不满 1 小时写「12:34」，满 1 小时写「1:02:05」（与结束提示一致）；
 *      计时以 1726 的 elapsedms 为起点、再加本机流逝的时间，不受两边时钟误差影响；
 *    - 观看者名单：「N 人观看 ▾」点开的名单，每人一行（名字、麦克风状态、发起人可点的「静音」），按座位排序、不含发起人；
 *    - 按人静音：发起人关掉某个观看者的麦克风（1725 事件 9），被静音的人本机关麦并提示，之后可以自己重新打开；
 *    - 强制模式：观看窗没有「离开」「退出全屏」，Esc 与关窗无效，置顶并在切走后拉回前台；加入前先确认（掉线后回来的除外）。
 *  这些判断抽成不依赖窗口的纯函数，便于单元测试，见 flamingoclient/Source/Tests/ShareRoomPolicyTests.cpp。
 *
 *  典型用法：
 *      clock.Start(relay.m_elapsedMs, ::GetTickCount64());
 *      bar.SetDurationText(screenshare::FormatShareDuration(clock.ElapsedSeconds(::GetTickCount64())));
 *
 *  balloonwj@qq.com   2026-10-03
 */
#pragma once

#include <stdint.h>
#include <map>
#include <set>
#include <string>
#include <vector>
#include "ShareProtocolClient.h"

namespace screenshare {

/**
 *  共享时长的文字：不满 1 小时为「mm:ss」（分钟补足两位），满 1 小时为「h:mm:ss」（小时数不补零）。
 *  @param seconds 共享时长，单位秒；负数按 0
 */
std::wstring FormatShareDuration(int64_t seconds);

//共享时长的计时：以服务端告知的已进行时长为起点，再加本机流逝的时间
class ShareDurationClock
{
public:
    ShareDurationClock();

    /**
     *  开始计时（重复调用以最近一次为准，如宽限期内重新登录后收到新的 1726）。
     *  @param elapsedMs 服务端发出 1726 时共享已进行的毫秒数；负数按 0
     *  @param nowTick   本机当前时刻，GetTickCount64 毫秒
     */
    void Start(int64_t elapsedMs, uint64_t nowTick);

    //停止计时；之后 IsRunning 为 false、ElapsedSeconds 为 0
    void Stop();

    //是否在计时
    bool IsRunning() const;

    /**
     *  当前的共享时长，单位秒（向下取整）。
     *  @param nowTick 本机当前时刻，GetTickCount64 毫秒；早于开始时刻时按开始时刻算
     */
    int64_t ElapsedSeconds(uint64_t nowTick) const;

private:
    bool     m_running;     //是否在计时
    int64_t  m_baseMs;      //开始计时那一刻共享已进行的毫秒数
    uint64_t m_baseTick;    //开始计时那一刻的本机时刻，GetTickCount64 毫秒
};

//观看者名单里的一行
struct ShareViewerRow
{
    int64_t      m_userId;     //观看者 userid
    int          m_seat;       //座位号
    std::wstring m_name;       //显示名；不知道时为「用户<id>」
    bool         m_micOn;      //是否开着麦克风
    bool         m_speaking;   //是否正在说话
    bool         m_canMute;    //「静音」能否点：本人是发起人、且对方开着麦克风
    bool         m_canRemove;  //「移出」能否点（第 14 期）：本人是发起人时每一行都可以
    bool         m_canControl; //「允许控制 / 收回控制」能否点（第 18 期）：本人是发起人（画面主人）时每一行都可以
    bool         m_controlling;//这名观看者是否正在控制发起人的屏幕（第 18 期，发起人成员项的 controlledby）：是则按钮为「收回控制」

    ShareViewerRow();
};

/**
 *  由房间快照算出观看者名单：去掉发起人，按座位号排序。
 *  @param state         最近一次 1725 的房间快照
 *  @param names         userid → 显示名；查不到的人显示「用户<id>」
 *  @param speakingSeats 正在说话的座位号
 *  @param asHost        本人是否是发起人（只有发起人有「静音」）
 *  @param out           [出参] 名单，原有内容会被清空
 */
void BuildShareViewerRows(const ShareStateNotify& state, const std::map<int64_t, std::wstring>& names,
                          const std::set<int>& speakingSeats, bool asHost, std::vector<ShareViewerRow>& out);

//名单一行的状态文字：「正在控制」（第 18 期，优先）「正在说话」「麦克风开着」「麦克风已关」
std::wstring ShareViewerStatusText(const ShareViewerRow& row);

/**
 *  「N 人观看 ▾」按钮上的文字；没有观看者时为「暂无观看者 ▾」。
 *  @param viewerCount 观看者人数，不含发起人；负数按 0
 */
std::wstring ShareViewerCountText(int viewerCount);

/**
 *  收到 1725 时，本人是否因被发起人单独静音而要关掉麦克风。
 *  @param event      1725 的事件类型，取值见 share_state_event
 *  @param target     1725 的 target（事件 9 为被静音的人）
 *  @param selfId     本人 userid
 *  @param micEnabled 本人麦克风当前是否开着
 *  @return 事件 9、被静音的是本人、且本人麦克风开着时返回 true
 */
bool ShouldMuteSelfByHost(int event, int64_t target, int64_t selfId, bool micEnabled);

//观看窗在当前模式下的外观与行为
struct ShareViewerChrome
{
    bool m_showLeave;            //是否有「离开」按钮
    bool m_showExitFullscreen;   //是否有「退出全屏」按钮
    bool m_escExitsFullscreen;   //Esc 是否退出全屏
    bool m_closeLeaves;          //关窗（标题栏关闭、Alt+F4）是否离开共享
    bool m_topmost;              //是否置顶
    bool m_pullBackToFront;      //切到别的程序后是否拉回前台

    ShareViewerChrome();
};

/**
 *  观看窗的外观与行为：普通共享各项照旧；强制模式下没有「离开」「退出全屏」，Esc 与关窗无效，置顶并拉回前台。
 *  @param forced 是否强制模式
 */
ShareViewerChrome MakeShareViewerChrome(bool forced);

/**
 *  点「加入」时是否先弹确认「这是强制观看的共享，加入后要等发起人结束才能离开」。
 *  @param forced 是否强制模式
 *  @param rejoin 是否是强制模式下掉线后重新登录、服务端让其回到观看（此时直接加入）
 */
bool ShouldConfirmForcedJoin(bool forced, bool rejoin);

/**
 *  单聊右下角「有人开始共享」提示的文字（第 14 期说清强制模式的效果）。
 *  @param hostName 发起人显示名；为空时写「对方」
 *  @param locked   加入是否需要密码：是的话名字后标出「（需要密码）」
 *  @param forced   是否强制模式：是的话后接「这是强制模式：加入后全屏观看、不能中途退出，直到对方结束共享。」
 */
std::wstring MakeShareStartedToastText(const std::wstring& hostName, bool locked, bool forced);

/**
 *  被发起人移出后弹窗的文字（第 14 期）。
 *  @param hostName  发起人显示名；为空时写「发起人」
 *  @param canRejoin 是否允许再次加入：不允许时说明不能再加入这场共享
 */
std::wstring MakeShareRemovedNotice(const std::wstring& hostName, bool canRejoin);

/**
 *  发起人移出某个观看者时确认框的提问文字（第 14 期），如「确定将 李四 移出屏幕共享吗？」。
 *  @param viewerName 观看者显示名；为空时写「这位观看者」
 */
std::wstring MakeShareRemovePrompt(const std::wstring& viewerName);

/**
 *  1729 的结束原因是否表示「本人被发起人移出」（第 14 期，原因 3 / 4）。此时共享仍在进行，客户端只关掉本人的观看窗、
 *  弹出被移出的提示，不把会话标成「没有共享」（卡片与横幅照常显示，允许再次加入时还能点）。
 *  @param reason 1729 的 reason，取值见 share_end_reason
 */
bool IsShareRemovedReason(int reason);

//共享标题的字数上限（第 15 期，按字符计，一个汉字或一个表情算一个；与服务端 ScreenShareRules 一致）
const size_t kShareTitleMaxChars = 30;
//共享描述的字数上限（第 15 期，按字符计）
const size_t kShareDescMaxChars = 100;

/**
 *  选择窗里共享标题的默认值（第 15 期）：「韩雪飞的屏幕共享」；不知道本人显示名时为「我的屏幕共享」。
 *  @param hostName 本人显示名
 */
std::wstring MakeDefaultShareTitle(const std::wstring& hostName);

/**
 *  数一段文字的字符个数（第 15 期）：UTF-16 的代理对（如表情）算一个字符，与服务端按 UTF-8 数出的个数一致。
 *  @param text 文字
 */
size_t CountShareInfoChars(const std::wstring& text);

/**
 *  规整并检查共享标题与描述（第 15 期，选择窗「开始共享」与修改窗口「确定」能否点、发出前的规整共用）：
 *  去掉两者首尾的空白（含全角空格），标题不能为空且不超过 kShareTitleMaxChars 字，描述不超过 kShareDescMaxChars 字。
 *  @param title [入参 / 出参] 标题，返回时已去掉首尾空白
 *  @param desc  [入参 / 出参] 描述，返回时已去掉首尾空白
 *  @return 合格返回 true
 */
bool NormalizeShareInfo(std::wstring& title, std::wstring& desc);

/**
 *  单聊右下角「有人开始共享」提示的文字，带共享标题（第 15 期），如「韩雪飞 正在共享：周会演示」；
 *  需要密码、强制模式的写法同三个参数的版本。标题为空（老服务端）时与三个参数的版本相同。
 *  @param hostName 发起人显示名；为空时写「对方」
 *  @param title    共享标题
 */
std::wstring MakeShareStartedToastText(const std::wstring& hostName, const std::wstring& title, bool locked,
                                       bool forced);

/**
 *  观看窗标题栏的主体文字（第 15 期）：有共享标题时就是标题；没有时为「某某 的屏幕共享」，发起人名也不知道时为「屏幕共享」。
 *  观看窗在其后接时长与帧率。
 *  @param title    共享标题
 *  @param hostName 发起人显示名
 */
std::wstring MakeShareViewerCaption(const std::wstring& title, const std::wstring& hostName);

/**
 *  发起人控制条标题栏的文字（第 15 期联调后由状态行挪来）：有共享标题时为「屏幕共享 · 周会演示」，没有时为「屏幕共享」。
 *  标题栏横跨整条控制条，30 字的标题也放得下，不会像放在状态行里那样挤掉共享时长。
 *  @param title 共享标题
 */
std::wstring MakeShareBarCaption(const std::wstring& title);

/**
 *  发起人控制条左侧的状态文字，如「正在共享 12:34 · 密码 482915 · 强制模式 · 李四 正在说话」。共享标题写在标题栏
 *  （见 MakeShareBarCaption），不在这里。
 *  @param paused   是否已暂停：是的话写「已暂停」，否则写「正在共享」
 *  @param duration 共享时长文字（见 FormatShareDuration）；为空时不写
 *  @param password 加入密码；为空时不写
 *  @param forced   是否强制模式
 *  @param speaking 当前说话人的提示文字；为空时不写
 */
std::wstring MakeShareBarStatusText(bool paused, const std::wstring& duration, const std::wstring& password, bool forced,
                                    const std::wstring& speaking);

/**
 *  主工具条上「录下这场共享」的一段文字（2026-10-07，docs/屏幕共享与录制方案.md 第 14.2 节：共享中的录制不再弹出单独的
 *  录制条，录制时长显示在主持人的控制条与观看者的观看窗上），如「录制 03:21」。
 *  @param duration 已录时长文字（FormatRecordDuration 的结果）；为空时返回空串
 *  @param withDot  是否在前面加圆点：观看窗的标题栏与全屏工具条只能写文字，用圆点表示正在录制；主持人的控制条另有
 *                  红色的录制圆点，传 false
 *  @param dotOn    圆点此刻是实心「●」（true）还是空心「○」（false），每秒交替一次表示闪烁；withDot 为 false 时不用
 *  @return 拼好的文字（已取译文）
 */
std::wstring MakeShareRecordingText(const std::wstring& duration, bool withDot, bool dotOn);

/**
 *  观看窗「主持人 ▾」菜单的菜单项（2026-10-07，docs/屏幕共享与录制方案.md 第 14.3 节第 2 条）：经移交成为主持人的人
 *  仍在观看窗里，没有发起人的控制条，主持人操作从这个菜单进入。观看窗把选中的菜单项经 kCmdHostMenu 交给控制器。
 */
enum share_host_menu_item
{
    //修改共享的标题与描述：控制器打开「修改标题与描述」小窗
    share_host_menu_edit_info   = 1,
    //全员静音：关掉主持人以外所有人的麦克风
    share_host_menu_mute_all    = 2,
    //「允许他人录制」勾选项：控制器把房间的录制权限改成当前的反面
    share_host_menu_record_perm = 3,
    //「允许他人标注」勾选项：控制器把房间的标注权限改成当前的反面
    share_host_menu_annot_perm  = 4,
    //「允许截图」勾选项（2026-10-08，第 14.11 节第 5 条）：本机的工具条与控制条是否允许截图，只对这一场共享有效
    share_host_menu_allow_capture = 5,
};

//观看窗「主持人 ▾」菜单的一项
struct ShareHostMenuItem
{
    int          m_id;        //菜单项，取值见 share_host_menu_item
    std::wstring m_text;      //菜单文字（已取译文）
    bool         m_checkable; //是否勾选项
    bool         m_checked;   //勾选项此刻是否打勾；不是勾选项时为 false
    bool         m_separatorBefore;   //这一项之前是否画分隔线

    ShareHostMenuItem();
};

/**
 *  组出观看窗「主持人 ▾」菜单的各项（2026-10-07，第 14.3 节第 2 条）：修改标题与描述、全员静音、分隔线、允许他人录制、
 *  允许他人标注。房间密码不在其中：服务端不向成员下发密码，经移交成为主持人的人无从得知；强制模式在开始共享时选定，
 *  发起人中途也不能修改，同样不在其中。
 *  @param recordAllowed   房间此刻是否允许他人录制（来自 1725）
 *  @param annotateAllowed 房间此刻是否允许他人标注（来自 1725）
 *  @param captureAllowed  本机的「允许截图」此刻是否打开（2026-10-08，排在最后、前面画分隔线）
 *  @param out             [出参] 按显示顺序排列的菜单项；先清空
 */
void BuildShareHostMenu(bool recordAllowed, bool annotateAllowed, bool captureAllowed,
                        std::vector<ShareHostMenuItem>& out);

/**
 *  本人是不是「经移交成为主持人、仍在观看窗里」的那种主持人（2026-10-07，第 14.3 节第 2 条）。这种主持人没有控制条，
 *  观看窗要放「主持人 ▾」入口；观看者名单可以静音与移出别人；不受强制模式约束，也不受「他人录制」权限的限制。
 *  @param hostStreaming 本人是否以发起人身份在推流（是时有控制条，返回 false）
 *  @param watching      本人是否在观看窗里
 *  @param viewerRoomId  观看窗所在的房间号
 *  @param stateRoomId   最近一次 1725 的房间号
 *  @param stateHostId   最近一次 1725 的主持人
 *  @param selfId        本人
 *  @return 是这种主持人返回 true
 */
bool IsTransferredShareHost(bool hostStreaming, bool watching, const std::string& viewerRoomId,
                            const std::string& stateRoomId, int64_t stateHostId, int64_t selfId);

//收到 1726 中转参数时怎么处理（2026-10-07，方案第 14.3 节第 3 条），DecideShareRelayInfoAction 的结果
enum share_relay_info_action
{
    //不是本人发起、正在加入或正在观看的房间：记日志后忽略
    share_relay_info_ignore           = 0,
    //本人发起的房间，推流还没开始或已经停了：开始推流（发起人宽限期内重新登录时服务端会再下发一次）
    share_relay_info_start_streaming  = 1,
    //本人发起的房间，推流还在进行：不用处理
    share_relay_info_host_running     = 2,
    //正在加入的房间：打开观看窗
    share_relay_info_open_viewer      = 3,
    //正在观看的房间又收到 1726：经移交成为主持人的人掉线后重新登录，服务端按主持人回来处理并签了新令牌，
    //观看窗换用新的中转参数重连（原来的中转连接多半已随掉线断开，旧令牌也可能已过期）
    share_relay_info_reconnect_viewer = 4,
};

/**
 *  收到 1726 时决定怎么处理（2026-10-07，方案第 14.3 节第 3 条）。发起一侧优先于观看一侧。
 *  @param roomId          1726 的房间号
 *  @param hostActive      发起一侧是否在进行中（不是 host_idle）
 *  @param hostRoomId      发起一侧的房间号
 *  @param hostWaiting     发起一侧是否在等 1726（host_waiting）
 *  @param streamerRunning 推流管线是否在运行
 *  @param viewerJoining   观看一侧是否在加入中（viewer_joining）
 *  @param viewerWatching  观看一侧是否在观看中（viewer_watching）
 *  @param viewerRoomId    观看一侧的房间号
 *  @return 取值见 share_relay_info_action
 */
int DecideShareRelayInfoAction(const std::string& roomId, bool hostActive, const std::string& hostRoomId,
                               bool hostWaiting, bool streamerRunning, bool viewerJoining, bool viewerWatching,
                               const std::string& viewerRoomId);

/**
 *  加入强制模式的共享前，确认框里的文字（第 15 期起写上共享标题与描述）。
 *  @param title 共享标题；为空时不写标题一行
 *  @param desc  共享描述；为空时不写描述一行
 */
std::wstring MakeForcedJoinConfirmText(const std::wstring& title, const std::wstring& desc);

/**
 *  把共享标题、描述等界面上输入的文字转成协议里用的 UTF-8（第 15 期，选择窗与修改窗口发出结果时用）。
 *  @param text 文字（UTF-16）
 *  @return UTF-8 字符串；text 为空或转换失败时为空串
 */
std::string ShareTextToUtf8(const std::wstring& text);

/**
 *  单聊右下角「有人开始共享」提示的文字要占几行（第 15 期）：普通共享一行，强制模式的说明较长占三行，有描述时再加两行。
 *  @param forced  是否强制模式
 *  @param hasDesc 是否有共享描述（描述另起一行，最多两行）
 */
int ShareToastTextLines(bool forced, bool hasDesc);

//各场共享的标题与描述（第 15 期）：从 1721 / 1725 / 1727 / 1734 记下，加入强制模式的共享前的确认、观看窗、名单据此显示
class ShareRoomInfoCache
{
public:
    /**
     *  记下某场共享的标题与描述。标题为空（老服务端不带）时不改已记下的内容。
     *  @param roomId 房间号；为空时什么也不做
     *  @param title  共享标题
     *  @param desc   共享描述
     */
    void Remember(const std::string& roomId, const std::wstring& title, const std::wstring& desc);

    /**
     *  查某场共享的标题与描述。
     *  @param title [出参] 标题（返回 true 时有效）
     *  @param desc  [出参] 描述（返回 true 时有效）
     *  @return 记过这场共享返回 true
     */
    bool Find(const std::string& roomId, std::wstring& title, std::wstring& desc) const;

    //共享结束后忘掉这一场
    void Forget(const std::string& roomId);

    //全部忘掉（登出时）
    void Clear();

private:
    std::map<std::string, std::pair<std::wstring, std::wstring> > m_rooms;   //房间号 → （标题，描述）
};

/**
 *  共享中云端录制（1754，第 27b 期）失败时给主持人看的说明。
 *  @param code   应答的错误码；736 没开通、737 已在录或没在录、738 云端存储不可用、604 配额已满、716 不是主持人
 *  @param action 动作，取值见 Protocol.h 的 cloudrecord_control_action（737 按开始与其它动作分别说明）
 *  @return 说明文字；code 为 0 时为空串
 */
std::wstring MakeCloudRecordErrorText(int code, int action);

/**
 *  云端录制被动结束（1725 事件 15 带 cloudrecordcode）时给主持人看的说明。
 *  @param code cloudrecordcode；0 表示主持人自己停止，返回空串
 */
std::wstring MakeCloudRecordStoppedText(int code);

//主持人「关闭白板 ▸」子菜单的一项（2026-10-08，docs/屏幕共享与录制方案.md 第 14.11 节第 1 条）
struct ShareBoardCloseItem
{
    int64_t      m_ownerId;   //白板的建立者 userid：选中后经确认框发 1737（停止，target 为建立者，kind 为白板）
    std::wstring m_text;      //菜单文字「某某 的白板」（已取译文）

    ShareBoardCloseItem();
};

/**
 *  组出主持人「关闭白板 ▸」子菜单的各项（2026-10-08）：原主持人控制条的「更多」与经移交主持人的「主持人 ▾」共用。
 *  只列别人建的白板（本人的白板用「关闭我的白板」），顺序与 1725 快照里的白板顺序相同；没有别人的白板时为空，调用方不显示这一项。
 *  @param boards 1725 快照里的白板
 *  @param selfId 本人 userid
 *  @param names  建立者 userid 到显示名的对照；找不到时写 userid
 *  @param out    [出参] 子菜单各项；先清空
 */
void BuildShareBoardCloseItems(const std::vector<ShareWhiteboardInfo>& boards, int64_t selfId,
                               const std::map<int64_t, std::wstring>& names, std::vector<ShareBoardCloseItem>& out);

/**
 *  观看者名单里「允许控制」能不能用（2026-10-08）：它把本人正在共享的屏幕交给某位观看者控制，本人须是主持人、
 *  而且确实在推自己的屏幕——原主持人推流中且没有停掉自己的画面，或经移交成为主持人、经共享者管线推流中。
 *  @param roomHost          本人是不是这一场的主持人（原主持人或经移交成为的主持人）
 *  @param hostStreaming     本人是否以发起人身份推流（原主持人）
 *  @param hostScreenStopped 原主持人是否停掉了自己的画面（「停止我的共享」）
 *  @param publishStreaming  本人是否经共享者管线推流（多人共享里共享自己屏幕的人，含经移交成为主持人的人）
 *  @return 可用返回 true
 */
bool ShareViewerListCanGrantControl(bool roomHost, bool hostStreaming, bool hostScreenStopped, bool publishStreaming);

} // namespace screenshare
