/**
 *  群通话控制器，GroupCallController.h
 *
 *  FlamingoMedia.dll 内群通话的总调度（docs/群音视频通话方案.md 6.1），把 chatserver 的信令（1740~1751）、媒体会话
 *  （GroupCallMediaSession）、来电窗（CGroupCallIncomingDlg）与通话窗（CGroupCallWindow）串起来：
 *    - 发起：exe 调 StartCall（群聊窗选好人之后）→ 发 1740 → 应答成功后等 1746 → 启动媒体会话、打开通话窗；
 *      群里已有通话（722）时改为加入那一场；
 *    - 来电：1741 → 右下角来电窗 + 来电铃声 → 接听发 1742、拒绝发 1743；别的设备接听或拒绝、振铃超时时按 1745 收起；
 *    - 加入：exe 调 JoinCall（群聊窗横幅、通话卡片的「加入」）→ 发 1742 → 1746 后同上；
 *    - 通话中：1745 刷新宫格；麦克风、摄像头开关经 1748 上报；邀请成员请 exe 弹选人框，选好后发 1747；
 *      发起人、群主、管理员可以静音或移出别人（1751，第 6 期）；被移出、被静音时提示；
 *    - 群内状态：1749 查询与 1750 推送都经 IMediaHost::OnGroupCallStatus 告诉 exe，exe 据此刷新群聊横幅与通话卡片；
 *      结束时改写后的卡片经 IMediaHost::OnGroupCallCardChanged 交给 exe 改本地库；
 *    - 中转断开时重新加入一次（换一份新令牌）再连，连续失败几次后退出通话。
 *  一个用户同时只能在一处（服务端规则），所以本类同一时间只有一场群通话（发起中、振铃中或已加入）。
 *
 *  所有方法都在 exe 的 UI 线程上调用；媒体会话的事件与各窗口的按钮经本类的消息窗口（HWND_MESSAGE，消息号见
 *  GroupCallMessages.h）转回 UI 线程处理。
 *
 *  典型用法（CMediaModule 里）：
 *      m_groupCall.reset(new groupcall::CGroupCallController());
 *      m_groupCall->Init(host);
 *      if (groupcall::CGroupCallController::IsGroupCallSignal(cmd)) m_groupCall->OnSignal(cmd, json);
 *
 *  balloonwj@qq.com   2026-10-04
 */
#pragma once

#include <deque>
#include <map>
#include <set>
#include <string>
#include <vector>
#include "GroupCallIncomingDlg.h"
#include "GroupCallLayout.h"
#include "GroupCallMediaSession.h"
#include "GroupCallMessages.h"
#include "GroupCallProtocolClient.h"
#include "GroupCallWindow.h"
#include "../Api/FlamingoMediaApi.h"

namespace groupcall {

//群通话控制器；消息窗口只收消息
class CGroupCallController : public CWindowImpl<CGroupCallController, CWindow, CWinTraits<0, 0> >,
                             private IGroupCallFrameSource
{
public:
    CGroupCallController();
    ~CGroupCallController();

    CGroupCallController(const CGroupCallController&) = delete;
    CGroupCallController& operator=(const CGroupCallController&) = delete;

    /**
     *  初始化：建消息窗口。
     *  @param host 宿主接口（发信令、取公钥与本人用户号、回报群通话状态），不拥有，须在 Shutdown 之前一直有效
     *  @return 消息窗口建好返回 true
     */
    bool Init(flamingomedia::IMediaHost* host);

    //结束进行中的群通话（发离开或拒绝信令）、关闭全部窗口、销毁消息窗口；可重复调用
    void Shutdown();

    //命令号是否归群通话（1740~1751）
    static bool IsGroupCallSignal(uint16_t cmd);

    /**
     *  处理一条群通话信令。
     *  @return 处理了返回 true
     */
    bool OnSignal(uint16_t cmd, const std::string& json);

    /**
     *  发起群通话（群聊窗选好人之后）。
     *  @param groupId   群 id
     *  @param groupName 群名，用于窗口标题
     *  @param media     媒体类型，取值见 enum gc_media
     *  @param invitees  要振铃的人（不含自己）
     *  @return 已发出请求返回 true；已在群通话里、参数非法或信令发送失败返回 false（已向用户提示原因）
     */
    bool StartCall(int64_t groupId, const std::wstring& groupName, int media, const std::vector<int64_t>& invitees);

    /**
     *  加入某群进行中的群通话（群聊窗横幅、通话卡片的「加入」）。
     *  @param groupId   群 id
     *  @param groupName 群名，用于窗口标题
     *  @param roomId    房间号
     *  @return 已发出请求返回 true；已在这场通话里时把通话窗提到前面并返回 true；已在别的群通话里返回 false（已提示）
     */
    bool JoinCall(int64_t groupId, const std::wstring& groupName, const std::string& roomId);

    //查询某群有无进行中的群通话；结果经 IMediaHost::OnGroupCallStatus 回报
    void Query(int64_t groupId);

    /**
     *  追加邀请（exe 的选人框确认后调用）。
     *  @param groupId 群 id，须是当前通话所在的群
     *  @param userIds 要振铃的人
     *  @return 已发出请求返回 true
     */
    bool InviteMembers(int64_t groupId, const std::vector<int64_t>& userIds);

    //用户登出：结束进行中的群通话，模块保持初始化
    void ResetForLogout();

    //是否在群通话里（发起中、振铃中或已加入）；一对一通话、屏幕共享据此在本地拦住
    bool IsBusy() const;

    //麦克风是否正被群通话使用（语音消息据此决定能不能录音）
    bool IsMicrophoneInUse() const;

    /**
     *  有用户或群的头像变了（IMediaModule::OnAvatarsUpdated 转来）：来电窗与通话窗重新取头像。窗口没开时什么也不做。
     */
    void OnAvatarsUpdated();

    BEGIN_MSG_MAP(CGroupCallController)
        MESSAGE_HANDLER(kMsgGroupCallMediaEvent, OnMediaEventMsg)
        MESSAGE_HANDLER(kMsgGroupCallIncomingCommand, OnIncomingCommandMsg)
        MESSAGE_HANDLER(kMsgGroupCallWindowCommand, OnWindowCommandMsg)
        MESSAGE_HANDLER(kMsgGroupCallDeferred, OnDeferredMsg)
        MESSAGE_HANDLER(WM_TIMER, OnTimer)
    END_MSG_MAP()

private:
    //本端的阶段
    enum Phase
    {
        phase_idle      = 0,   //没有群通话
        phase_starting  = 1,   //已发 1740，等应答
        phase_incoming  = 2,   //收到邀请，来电窗开着
        phase_joining   = 3,   //已发 1742，等应答
        phase_waiting   = 4,   //1740 / 1742 成功，等 1746 中转参数
        phase_in_call   = 5,   //媒体会话已启动，通话窗开着
    };

    //IGroupCallFrameSource：通话窗取画面，转给媒体会话
    virtual bool GetTileFrame(int seat, bool self, std::vector<uint8_t>& bgra, int& width, int& height,
                              uint64_t& serial) override;

    LRESULT OnMediaEventMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnIncomingCommandMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnWindowCommandMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnDeferredMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnTimer(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

    //各条信令的处理
    void onStartResult(const std::string& json);
    void onInvite(const std::string& json);
    void onJoinResult(const std::string& json);
    void onStateNotify(const std::string& json);
    void onRelayInfo(const std::string& json);
    void onInviteMoreResult(const std::string& json);
    void onMemberMediaResult(const std::string& json);
    void onQueryResult(const std::string& json);
    void onStatusNotify(const std::string& json);
    void onManageResult(const std::string& json);

    //发信令；失败记日志
    bool send(uint16_t cmd, const std::string& body);
    //发加入请求（接听、横幅加入、断线重连共用）
    bool sendJoin(bool mic, bool camera);
    //接听来电
    void acceptInvite();
    //拒绝来电（发 1743）并回到空闲
    void declineInvite();
    //离开通话：发 1744（notifyServer 为 true 时）并在本地收尾
    void leaveCall(bool notifyServer);
    //本地收尾：停媒体、关窗口、停铃声、清状态，回到空闲
    void resetLocal();

    /**
     *  按当前阶段向宿主回报本人的群通话状态（IMediaHost::OnCallPresence，主面板据此显示「通话中」徽章）：
     *  处于通话中阶段时为「在本群的群通话里」，否则为不在。与上次回报的相同时不重复回报。进入通话、刷新画面
     *  （媒体类型以房间快照为准）、本地收尾之后各调一次。
     */
    void syncPresence();
    //启动或重启媒体会话
    void startMedia(const GroupCallRelayInfo& relay);
    //按最近的快照与本地状态刷新通话窗、订阅与回铃
    void refreshView();
    //定时刷新：说话人、时长、订阅
    void onSlowTick();
    //把本人的麦克风与摄像头状态经 1748 告诉服务端
    void reportMedia();
    //打开或关闭本人麦克风 / 摄像头
    void setMic(bool on);
    void setCamera(bool on);
    //回报群内状态给宿主
    void reportStatus(int64_t groupId, const std::string& roomId, bool active, int media, int count,
                      int64_t initiatorId, int64_t startTime);
    //显示一条临时提示（占用状态行几秒）
    void showNotice(const std::wstring& text);
    //弹出错误提示框
    void showError(const std::wstring& text);
    //把错误码转成提示文字
    std::wstring errorText(int code) const;
    //取显示名（经宿主，带缓存）
    std::wstring displayName(int64_t userId);
    //一组人的显示名，用顿号连接，最多列几个，超出时写「等 N 人」
    std::wstring namesOf(const std::vector<int64_t>& userIds);
    //本人在最近的快照里的那一项；不在时返回 NULL
    const GroupCallMember* selfMember() const;
    //本人是否能静音、移出别人
    bool canManage();
    //是否要放回铃（发起人一个人在等别人接听）
    bool shouldRingBack() const;
    //按 shouldRingBack 开关回铃
    void syncRingBack();
    HWND mainWindow() const;
    //媒体会话打开哪些设备（enum gc_device_mode）：Release 版一律全部打开；Debug 版取环境变量
    //FLAMINGO_GROUPCALL_NO_DEVICES 的值，1 为什么都不开、2 为不开声音设备，其余为全部打开（联调用）
    static int deviceMode();
    static std::wstring utf8ToWide(const std::string& s);

    flamingomedia::IMediaHost* m_host;           //宿主接口（不拥有）
    Phase                      m_phase;          //本端的阶段
    int64_t                    m_groupId;        //当前群通话所在的群；空闲时为 0
    std::wstring               m_groupName;      //群名
    int                        m_media;          //媒体类型
    std::string                m_roomId;         //房间号；发起应答之前为空
    int64_t                    m_inviterId;      //来电的邀请人（振铃中有效）
    GroupCallState             m_state;          //最近一次房间快照（1741 或 1745）
    bool                       m_wantMic;        //本人想不想开麦克风（加入时的初值、之后按按钮）
    bool                       m_wantCamera;     //本人想不想开摄像头
    GroupCallMediaSession      m_mediaSession;   //媒体会话
    LPARAM                     m_mediaToken;     //当前媒体会话的代号，随事件回传
    int                        m_relayRetries;   //中转连续失败后已重新加入的次数
    int64_t                    m_callStartTick;  //本人看到的通话开始时刻（GetTickCount64 毫秒，按 elapsedms 倒推）
    std::set<int>              m_speakingSeats;  //最近一次判定的正在说话的座位号
    std::wstring               m_notice;         //临时提示
    int64_t                    m_noticeUntil;    //临时提示显示到的时刻（GetTickCount64 毫秒）
    bool                       m_ringBack;       //是否正在放回铃
    int                        m_cachedRole;     //本人在当前群里的角色（经宿主查过一次后缓存）；没查过为 -2
    CGroupCallIncomingDlg      m_incoming;       //来电窗
    CGroupCallWindow           m_window;         //通话窗
    std::deque<int64_t>        m_pendingQueries; //已发出、还没收到应答的查询（服务端按序应答）
    std::map<int64_t, std::wstring> m_nameCache; //userid → 显示名（登出时清空）
    int64_t                    m_presenceGroup;  //上次向宿主回报「通话中」的群 id；没有回报过或已回报结束时为 0
    int                        m_presenceMedia;  //上次回报的媒体类型：1 群语音、2 群视频；m_presenceGroup 为 0 时无意义
};

} // namespace groupcall
