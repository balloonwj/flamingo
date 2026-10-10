/**
 *  屏幕共享房间里的语音，ShareAudioSession.h
 *
 *  每个房间成员（发起人与观看者）各有一个：麦克风打开时采集、回声消除、Opus 编码，按麦克风轨（screenshare_track_mic）
 *  整帧一片发进房间中转；收到其他成员的声音帧时按「座位 × 轨道」分路解码、混音播放；定时按各路电平判断谁在说话
 *  （docs/屏幕共享与录制方案.md 5.7、第 4 期 4b / 4c）。引擎复用一对一通话的 CallAudioEngine（4a 起播放端已是多路混音）。
 *
 *  帧怎么发由调用方决定：发起人经 ShareHostStreamer、观看者经 ShareViewerReceiver 的房间中转连接发出（实现
 *  ShareAudioTransport）；它们收到声音帧后交给 OnRemoteFrame。
 *
 *  线程：Start / Stop / SetMicEnabled / UpdateSpeaking / RemoveSeat 在 UI 线程调用；OnRemoteFrame 在中转接收线程调用；
 *  编码出的帧在采集线程上经 ShareAudioTransport 发出。Stop 之前调用方须先停掉会调用 OnRemoteFrame 的中转接收。
 *  无设备模式（单元测试与联调用例）不打开麦克风与扬声器，由 FeedMic / PullPlayback 代替设备。
 *
 *  典型用法：
 *      m_audio.Start(&m_streamer, seat, micOn, false);
 *      m_audio.SetMicEnabled(true);
 *      std::vector<int> talking = m_audio.UpdateSpeaking(nowMs);
 *
 *  balloonwj@qq.com   2026-10-03
 */
#pragma once

#include <stdint.h>
#include <map>
#include <mutex>
#include <string>
#include <vector>

#include "ShareFraming.h"
#include "ShareAudioTransport.h"
#include "ShareSystemAudioCapture.h"
#include "../Call/CallAudioEngine.h"

namespace screenshare {

//录制用的房间声音旁路的来源
enum ShareAudioTapKind
{
    share_audio_tap_mic    = 0,   //本人麦克风（处理后）
    share_audio_tap_others = 1,   //混好的其他人的声音（观看者这一路里含发起人共享的电脑声音）
    share_audio_tap_system = 2,   //本人共享的电脑声音（发起人）
};

//录制用的房间声音旁路（屏幕共享录制，第 7 期 7d）：在采集、渲染、回环采集线程上调用，实现须线程安全且不能阻塞
class ShareAudioRecordTap
{
public:
    virtual ~ShareAudioRecordTap()
    {
    }

    /**
     *  一段 48 kHz 单声道声音。
     *  @param kind 来源，取值见 ShareAudioTapKind
     */
    virtual void OnRoomAudio(int kind, const int16_t* mono, int samples) = 0;
};

//屏幕共享房间里的语音会话
class ShareAudioSession : private call::CallAudioEngineSink, private ShareSystemAudioHandler,
                          private call::CallAudioPcmTap
{
public:
    ShareAudioSession();
    ~ShareAudioSession();

    ShareAudioSession(const ShareAudioSession&) = delete;
    ShareAudioSession& operator=(const ShareAudioSession&) = delete;

    /**
     *  混音器里的路号：每个座位的每条声音轨各一路。
     *  @param seat  座位号（0~254）
     *  @param track 轨道（screenshare_track_mic / screenshare_track_system_audio）
     */
    static int StreamIdOf(int seat, int track);

    /**
     *  开始：打开扬声器（播放别人的声音），按 micOn 决定是否打开麦克风。
     *  @param transport      发帧的通道；不拥有，须在 Stop 之前一直有效
     *  @param selfSeat       本人的座位号（写进帧头，便于排查；服务端会改写为登记的座位）
     *  @param micOn          麦克风一开始是否打开
     *  @param withoutDevices 为 true 时不打开任何声音设备（测试用），由 FeedMic / PullPlayback 代替
     *  @return 成功返回 true；声音设备打不开时返回 false（记日志，画面共享不受影响）
     */
    bool Start(ShareAudioTransport* transport, int selfSeat, bool micOn, bool withoutDevices);

    //停止并关闭声音设备；可重复调用
    void Stop();

    //是否在运行
    bool IsRunning() const;

    /**
     *  打开或关闭麦克风（关闭时真正停掉麦克风设备）。
     *  @return 打开失败（设备不可用）返回 false，麦克风保持关闭
     */
    bool SetMicEnabled(bool on);

    //麦克风是否打开
    bool IsMicEnabled() const;

    /**
     *  打开或关闭麦克风轨的 Opus DTX（静音时不发帧）。群通话打开它，屏幕共享不调用（docs/群音视频通话方案.md 第 5 期）。
     *  须在 Start 之前调用；电脑声音轨不受影响。
     *  @param on 是否打开
     */
    void SetDtx(bool on);

    /**
     *  收到房间里一帧（中转接收线程）。只处理别人的麦克风轨与电脑声音轨，其余忽略。
     */
    void OnRemoteFrame(const ShareMediaFrame& frame);

    //删除某个座位的全部声音路（成员离开时）
    void RemoveSeat(int seat);

    /**
     *  按各路最近的电平更新发言判定（UI 定时器上每 100 毫秒左右调一次）。
     *  @param nowMs 当前时间（毫秒，单调递增）
     *  @return 当前在说话的座位号（升序；本人在说话时含本人座位）
     */
    std::vector<int> UpdateSpeaking(int64_t nowMs);

    /**
     *  打开或关闭「共享电脑声音」（发起人用，docs/屏幕共享与录制方案.md 5.7）。打开时优先用排除本进程树的回环采集，
     *  只采 Flamingo 以外程序的声音；系统不支持时退回普通回环，此时房间里有人说话就把共享出去的电脑声音压低 20 dB
     *  （闪避），减少把别人的声音从扬声器再采回去送出去。无设备模式下只切换开关，由 FeedSystemAudio 代替采集。
     *  @return 打开失败（采集不可用）返回 false，保持关闭
     */
    bool SetSystemAudioEnabled(bool on);

    //「共享电脑声音」是否打开
    bool IsSystemAudioEnabled() const;

    //电脑声音是否以「排除本进程树」的方式采集；false 表示退回了普通回环（闪避生效）
    bool IsSystemAudioExcludingSelf() const;

    //无设备模式：喂入一段 48 kHz 单声道的电脑声音（实际运行时由回环采集线程调用同一处理）
    void FeedSystemAudio(const int16_t* pcm, int samples);

    //无设备模式：指定电脑声音按哪种采集方式处理（决定是否闪避），默认按「排除本进程树」
    void SetSystemAudioModeForTest(bool excludingSelf);

    /**
     *  闪避的增益：排除本进程树时不需要闪避；退回普通回环时，有人说话压低 20 dB（增益 10%），没人说话不变。
     *  @return 增益百分比（100 或 10）
     */
    static int DuckingGainPercent(bool excludingSelf, bool othersSpeaking);

    //按百分比调整一段采样的音量（结果限幅到 16 位）
    static void ApplyGainPercent(int16_t* pcm, int samples, int percent);

    //无设备模式：喂入一段 48 kHz 单声道的麦克风采集
    void FeedMic(const int16_t* pcm, int samples);

    //无设备模式：取出一段 48 kHz 单声道的播放
    void PullPlayback(int16_t* pcm, int samples);

    /**
     *  登记录制用的声音旁路（任意线程）；传 NULL 取消。旁路对象须在取消登记之后才能销毁。
     */
    void SetRecordTap(ShareAudioRecordTap* tap);

    /**
     *  登记屏幕共享实时字幕用的声音旁路（任意线程，docs/语音字幕方案.md 第 9 节）：本人麦克风（回声消除之后）的每一帧
     *  以 share_audio_tap_mic 交给它，送进本人的识别流。与录制旁路互不影响；传 NULL 取消。旁路对象须在取消登记之后才能销毁。
     */
    void SetCaptionTap(ShareAudioRecordTap* tap);

private:
    //CallAudioEngineSink：采集线程上编码出一帧，封成麦克风轨的包体发出
    virtual void OnEncodedAudio(const unsigned char* data, size_t len, uint64_t frameIndex) override;
    //ShareSystemAudioHandler：回环采集线程上交来一段电脑声音
    virtual void OnSystemAudio(const int16_t* mono, int samples) override;
    //CallAudioPcmTap：转给录制旁路
    virtual void OnCapturedPcm(const int16_t* mono, int samples) override;
    virtual void OnRenderedPcm(const int16_t* mono, int samples) override;
    //别人是否正在说话（任一路麦克风最近一次混音的电平超过门限），闪避用
    bool othersSpeaking();
    //关闭电脑声音的采集与编码
    void stopSystemAudio();

    call::CallAudioEngine              m_engine;      //采集、编码、多路解码与混音
    ShareAudioTransport*               m_transport;   //发帧通道（不拥有）
    int                                m_selfSeat;    //本人座位号
    uint64_t                           m_sentFrames;  //已发出的麦克风帧数（只在采集线程使用，供排查；2026-10-06 起帧头时间戳改用引擎给的采集帧序号）
    std::mutex                         m_mutex;       //保护 m_detectors 与 m_knownSeats
    std::map<int, call::SpeakingDetector> m_detectors;   //路号（本人用 -1）→ 发言判定
    std::map<int, bool>                m_knownSeats;  //收到过声音帧的座位（UpdateSpeaking 遍历用）
    bool                               m_withoutDevices;   //是否无设备模式（Start 时记下）
    //共享电脑声音
    ShareSystemAudioCapture            m_sysCapture;   //回环采集（无设备模式下不用）
    call::OpusAudioCodec               m_sysCodec;     //电脑声音的编码器（音乐模式；只在回环采集线程或测试线程上使用）
    std::vector<int16_t>               m_sysAccum;     //攒成整帧再编码（同上）
    std::atomic<bool>                  m_sysEnabled;   //是否在共享电脑声音
    std::atomic<bool>                  m_sysExcludingSelf;   //是否排除本进程树（false 时有人说话就闪避）
    uint64_t                           m_sysFrames;    //已发出的电脑声音帧数（作帧头时间戳的序号）
    std::atomic<ShareAudioRecordTap*>  m_recordTap;    //录制用的声音旁路（不拥有）；没有录制时为 NULL
    std::atomic<ShareAudioRecordTap*>  m_captionTap;   //实时字幕用的声音旁路（不拥有，只收本人麦克风）；没开字幕时为 NULL
};

} // namespace screenshare
