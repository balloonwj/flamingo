/**
 *  屏幕共享实时字幕的调度（docs/语音字幕方案.md 第 9 节），ShareCaptionController.h
 *
 *  屏幕共享控制器在房间语音开始时调 BeginSession、结束时调 EndSession，本类负责：
 *    - 本人的发言（打开了「为我的发言生成字幕」时）：从房间语音会话的字幕旁路取回声消除之后的麦克风声音，送进一路
 *      实时识别流；识别线程交来的中间结果按限速、最终结果每句都编成字幕控制帧经房间中转发出，同时显示在本机；
 *    - 别人的发言：收流线程交来的字幕帧按座位号查成员名字后显示；
 *    - 显示：字幕板（ShareCaptionBoard）管理各座位当前的字幕，浮层窗口（CShareCaptionOverlay）画在宿主给出的区域底部，
 *      共享者本人的浮层排除在采集之外；「显示字幕」关掉时不显示（自己的识别照常发送）；
 *    - 录制共享时（宿主给出字幕草稿会话）：每句最终结果按收到时的录制时刻写进草稿（AddRemoteCaption）；
 *    - 字幕变为不可用（管理员关闭、退出登录）时停止识别、清掉显示（第 2 节 D5），之后收到的字幕帧丢弃。
 *  识别线程、收流线程、采集线程交来的内容先放进加锁的队列，再投递消息到本类的消息窗口，在 UI 线程上处理。
 *
 *  典型用法（屏幕共享控制器里）：
 *      m_caption.Init(this);
 *      m_caption.BeginSession(transport, seat, &m_audio);
 *      m_caption.SetSelfEnabled(true);
 *      m_caption.EndSession();
 *
 *  balloonwj@qq.com   2026-10-06
 */
#pragma once

#include <stdint.h>

#include <mutex>
#include <string>
#include <vector>

#include "MediaCaptionBridge.h"
#include "ShareCaptionBoard.h"
#include "ShareCaptionFrame.h"
#include "ShareCaptionOverlay.h"
#include "../Share/ShareAudioSession.h"
#include "../Share/ShareAudioTransport.h"

namespace screenshare {

//字幕控制器的消息窗口收到的消息：识别线程或收流线程排进了新的字幕，请 UI 线程取出（窗口私有消息，见开发约定文档 UI 约定第 6 条）
const UINT kMsgShareCaptionPending = WM_APP + 0xE8;

/**
 *  字幕控制器向屏幕共享控制器要的信息（UI 线程上调用）。
 */
class IShareCaptionHost
{
public:
    /**
     *  座位号对应的成员显示名。
     *  @return 名字；不认识的座位返回空串
     */
    virtual std::wstring CaptionSeatName(int seat) = 0;

    /**
     *  字幕浮层的显示区域。
     *  @param area               [出参] 屏幕坐标
     *  @param excludeFromCapture [出参] 是否排除在屏幕采集之外（共享者本人为 true）
     *  @param owner              [出参] 浮层的所有者窗口（观看者为观看窗，浮层不置顶）；共享者本人为 NULL（浮层置顶）
     *  @return 没有可显示的区域（观看窗最小化等）时返回 false
     */
    virtual bool CaptionOverlayArea(RECT& area, bool& excludeFromCapture, HWND& owner) = 0;

    /**
     *  录制共享时的字幕草稿会话与当前录制时刻。
     *  @param recordedMs [出参] 当前录制时刻（毫秒，不含暂停）
     *  @return 没有在录制共享、或这次录制没有字幕草稿时返回 NULL
     */
    virtual caption::ICaptionRecording* CaptionRecording(int64_t& recordedMs) = 0;

    //「为我的发言生成字幕」「显示字幕」的状态变了（刷新控制条、观看窗的菜单）
    virtual void OnCaptionStateChanged() = 0;

protected:
    ~IShareCaptionHost()
    {
    }
};

/**
 *  CShareCaptionController —— 屏幕共享实时字幕的调度。
 */
class CShareCaptionController : public CWindowImpl<CShareCaptionController, CWindow, CWinTraits<0, 0> >,
                                public ShareCaptionSink,
                                private ShareAudioRecordTap,
                                private caption::ICaptionStreamListener,
                                private flamingomedia::ICaptionAvailabilityObserver
{
public:
    CShareCaptionController();
    ~CShareCaptionController();

    /**
     *  初始化：建消息窗口，登记为字幕可用状态的观察者，读入「为我的发言生成字幕」「显示字幕」的记住的选择。
     *  @param host 宿主（不拥有，须在 Shutdown 之前一直有效）
     */
    bool Init(IShareCaptionHost* host);

    //结束会话、销毁浮层与消息窗口；可重复调用
    void Shutdown();

    /**
     *  房间语音开始（发起人或观看者）。
     *  @param transport 发帧通道（不拥有，EndSession 之前须一直有效）
     *  @param selfSeat  本人座位号
     *  @param audio     房间语音会话（不拥有，同上）；本类登记为它的字幕旁路
     */
    void BeginSession(ShareAudioTransport* transport, int selfSeat, ShareAudioSession* audio);

    //房间语音结束：停止识别、清掉显示
    void EndSession();

    //字幕当前能否使用（企业开关、字幕 DLL）
    bool IsAvailable() const;

    //「为我的发言生成字幕」：打开时建识别流（记住选择，D2）
    void SetSelfEnabled(bool on);
    bool IsSelfEnabled() const;

    //「显示字幕」：关掉时不显示任何人的字幕（记住选择，D2）
    void SetShowEnabled(bool on);
    bool IsShowEnabled() const;

    //某个成员离开房间：清掉他的字幕
    void OnSeatLeft(int seat);

    //ShareCaptionSink：收流线程上收到别人的字幕帧，排队后投递给 UI 线程
    virtual void OnCaptionFrame(int senderSeat, const ShareCaptionText& caption) override;

    BEGIN_MSG_MAP(CShareCaptionController)
        MESSAGE_HANDLER(kMsgShareCaptionPending, OnPendingMsg)
        MESSAGE_HANDLER(WM_TIMER, OnTimer)
    END_MSG_MAP()

private:
    //排队等 UI 线程处理的一条字幕
    struct Pending
    {
        int          m_seat;        //发言人座位号（本人的为 m_selfSeat）
        bool         m_self;        //是不是本人的识别结果
        ShareCaptionText m_caption; //内容
    };

    //ShareAudioRecordTap：采集线程上把本人麦克风的声音送进识别流
    virtual void OnRoomAudio(int kind, const int16_t* mono, int samples) override;
    //ICaptionStreamListener：识别线程上收到本人的识别结果，发帧并排队显示
    virtual void OnCaptionPartial(const wchar_t* text) override;
    virtual void OnCaptionFinal(const wchar_t* text, int64_t startMs, int64_t endMs) override;
    //ICaptionAvailabilityObserver：字幕变为不可用时停止识别、清掉显示
    virtual void OnCaptionAvailabilityChanged(bool available) override;

    //按当前状态建或关识别流
    void updateStream();
    void closeStream();
    //本人的一条识别结果：发帧（中间结果限速）并排队显示
    void emitSelf(const std::wstring& text, bool final, uint32_t durationMs);
    void enqueue(const Pending& p);
    //重画浮层
    void refreshOverlay();
    LRESULT OnPendingMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnTimer(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

    IShareCaptionHost*        m_host;          //宿主（不拥有）
    ShareCaptionBoard         m_board;         //各座位当前的字幕（UI 线程）
    CShareCaptionOverlay      m_overlay;       //浮层窗口（UI 线程）
    bool                      m_inSession;     //是否在房间语音会话中（UI 线程）
    bool                      m_selfEnabled;   //「为我的发言生成字幕」
    bool                      m_showEnabled;   //「显示字幕」
    ShareAudioSession*        m_audio;         //房间语音会话（不拥有）
    std::mutex                m_mutex;         //保护以下各项（多线程访问）
    ShareAudioTransport*      m_transport;     //发帧通道（不拥有）
    int                       m_selfSeat;      //本人座位号
    caption::ICaptionStream*  m_stream;        //本人的识别流；没有识别时为 NULL
    ShareCaptionThrottle      m_throttle;      //中间结果限速（识别线程）
    uint32_t                  m_sentenceId;    //本人当前这一句的编号（识别线程）
    int64_t                   m_sentenceStartMs;  //本人当前这一句开始送声音的时刻（识别线程，用来算时长）
    std::vector<Pending>      m_pending;       //等 UI 线程处理的字幕
    bool                      m_posted;        //是否已投递过还没处理的消息
};

} // namespace screenshare
