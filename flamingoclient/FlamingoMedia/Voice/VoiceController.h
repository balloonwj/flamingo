/**
 *  语音消息的控制器（语音消息第 2d 步，方案见 docs/语音消息方案.md 第 4 节 V6、第 7.1 节）。
 *
 *  模块（MediaModule）通过它录音与播放：
 *    - 开始录音前先查麦克风是否被通话或屏幕共享占用（IVoiceMicBusyQuery），占用时不录（方案 V6），播放不受限制；
 *    - 录音器、播放器的通知在采集 / 渲染线程上产生，控制器把它们排进队列，再向自己的消息窗口投递
 *      kMsgVoiceEvent，到 UI 线程上取出来交给 IVoiceControllerSink。投递的消息不带堆对象，窗口销毁后排着的
 *      事件随控制器一起释放，不会泄漏；
 *    - 播放器播完后，在 UI 线程上停止设备，再通知宿主；已经停止或已换成别的文件时，排着的旧事件直接丢弃。
 *  麦克风与扬声器设备可以注入（单元测试用不碰真实设备的假设备），不注入时用 WASAPI。
 *
 *  线程：除录音器、播放器的回调外，所有方法都在 UI 线程（模块所在线程）上调用。
 *
 *  典型用法：
 *      voicemsg::CVoiceController voice(&busyQuery, &sink, NULL, NULL);
 *      voice.Init();
 *      if (voice.StartRecord(path) == voicemsg::voice_record_ok) ...
 *      voicemsg::VoiceRecordResult result;
 *      voice.StopRecord(result);
 *      voice.Play(path, token);
 *      voice.Shutdown();
 *
 *  balloonwj@qq.com   2026-10-04
 */
#pragma once

#include <windows.h>
#include <stdint.h>
#include <deque>
#include <memory>
#include <mutex>
#include <string>

#include "VoicePlayer.h"
#include "VoiceRecorder.h"

namespace voicemsg {

//控制器的消息窗口收到的「有新的语音事件」通知（窗口私有消息，号段见开发约定文档 UI 约定第 6 条）
const UINT kMsgVoiceEvent = WM_APP + 0x96;

//StartRecord 的结果
enum VoiceRecordStartResult
{
    //已开始录音
    voice_record_ok = 0,
    //麦克风正被通话或屏幕共享使用，不录音（方案 V6）
    voice_record_mic_busy = 1,
    //麦克风打不开（没有设备、被独占、系统隐私设置禁止）或编码器初始化失败
    voice_record_device_error = 2,
    //已经在录音，本次请求被忽略
    voice_record_already = 3,
    //控制器还没有初始化或已经关闭
    voice_record_not_ready = 4,
    //继续录音（ResumeRecord）时没有已暂停的录音
    voice_record_not_paused = 5,
};

/**
 *  查询麦克风是否被占用。由模块实现：一对一通话的媒体会话在运行、或屏幕共享开着麦克风时返回 true。
 */
class IVoiceMicBusyQuery
{
public:
    virtual ~IVoiceMicBusyQuery()
    {
    }

    //麦克风是否正被通话或屏幕共享使用
    virtual bool IsMicrophoneBusy() = 0;
};

/**
 *  语音事件的接收方（模块把它们转给宿主 exe）。所有方法都在 UI 线程上调用。
 */
class IVoiceControllerSink
{
public:
    virtual ~IVoiceControllerSink()
    {
    }

    /**
     *  录音中的音量，约每 100 毫秒一次。
     *  @param level 0~kVoiceLevelMax
     */
    virtual void OnVoiceRecordLevel(int level) = 0;

    //已录满 kVoiceMaxSeconds 秒，录音停在这里等用户确认发送或取消；只通知一次
    virtual void OnVoiceRecordLimitReached() = 0;

    /**
     *  播放进度，约每 100 毫秒一次。
     *  @param token    Play 时传入的编号
     *  @param playedMs 已播放的毫秒数
     *  @param totalMs  总毫秒数
     */
    virtual void OnVoicePlayProgress(uint32_t token, int64_t playedMs, int64_t totalMs) = 0;

    /**
     *  播放到结尾（设备已停止）。调用方主动 StopPlay、或 Play 别的文件时不通知。
     *  @param token Play 时传入的编号
     */
    virtual void OnVoicePlayFinished(uint32_t token) = 0;
};

/**
 *  语音消息的控制器：持有录音器、播放器与消息窗口。用法见文件头。
 */
class CVoiceController : public IVoiceRecorderListener, public IVoicePlayerListener
{
public:
    /**
     *  @param busy          麦克风占用查询，不能为 NULL；调用方持有，须存活到 Shutdown 返回
     *  @param sink          事件接收方，不能为 NULL；调用方持有，须存活到 Shutdown 返回
     *  @param captureDevice 麦克风设备；为 NULL 时用 WASAPI。不为 NULL 时由调用方持有，须存活到 Shutdown 返回
     *  @param renderDevice  扬声器设备；为 NULL 时用 WASAPI。不为 NULL 时由调用方持有，须存活到 Shutdown 返回
     */
    CVoiceController(IVoiceMicBusyQuery* busy, IVoiceControllerSink* sink, IVoiceCaptureDevice* captureDevice,
                     IVoiceRenderDevice* renderDevice);
    ~CVoiceController();

    CVoiceController(const CVoiceController&) = delete;
    CVoiceController& operator=(const CVoiceController&) = delete;

    /**
     *  建立消息窗口。须在 UI 线程上调用。
     *  @return 成功返回 true；重复调用直接返回 true
     */
    bool Init();

    //停止录音（不写文件）与播放，销毁消息窗口，丢弃排着的事件。可以重复调用
    void Shutdown();

    /**
     *  开始录音。
     *  @param path 录音文件的完整路径（停止时才写盘）
     *  @return 取值见 enum VoiceRecordStartResult
     */
    int StartRecord(const std::wstring& path);

    /**
     *  停止录音并写文件。
     *  @param result [出参] 录音结果，返回 true 时有效
     *  @return 写文件成功返回 true；没有在录音、没有录到音频或写文件失败返回 false
     */
    bool StopRecord(VoiceRecordResult& result);

    //取消录音，不写文件。没有在录音时什么也不做；暂停中同样可以取消
    void CancelRecord();

    /**
     *  暂停录音（语音消息第 6d 步）：关闭麦克风采集，已录的内容保留；暂停期间没有音量通知。
     *  @return 正在录音并已暂停时返回 true；没有在录音或已经暂停时返回 false
     */
    bool PauseRecord();

    /**
     *  继续已暂停的录音。与 StartRecord 一样，麦克风正被通话或屏幕共享使用时不继续（暂停期间可能开始了通话）。
     *  @return 取值见 enum VoiceRecordStartResult：继续成功为 voice_record_ok；麦克风被占用为 voice_record_mic_busy；
     *          麦克风打不开为 voice_record_device_error；没有已暂停的录音为 voice_record_not_paused；控制器没有
     *          初始化为 voice_record_not_ready。失败时录音保持暂停
     */
    int ResumeRecord();

    /**
     *  把已暂停的录音里已录的部分另存成文件，供暂停中试听（2026-10-05）。录音保持暂停，之后照常可以继续、停止或取消。
     *  @param path          另存文件的完整路径；已存在时覆盖
     *  @param outDurationMs [出参] 另存文件的时长（毫秒），返回 true 时有效
     *  @return 控制器已初始化、录音已暂停且写文件成功时返回 true
     */
    bool SnapshotPausedRecord(const std::wstring& path, int64_t& outDurationMs);

    //是否有一段录音在进行（暂停中也算）
    bool IsRecording() const;

    //录音是否已暂停
    bool IsRecordPaused() const;

    /**
     *  播放一个语音文件；正在播放别的文件时先停止它（不通知 OnVoicePlayFinished）。
     *  @param path  语音文件的完整路径
     *  @param token 调用方用来对应通知的编号，原样带回
     *  @return 文件格式正确、扬声器打开成功时返回 true
     */
    bool Play(const std::wstring& path, uint32_t token);

    //停止播放（不通知 OnVoicePlayFinished）。没有在播放时什么也不做
    void StopPlay();

    /**
     *  当前正在播放的编号。
     *  @return 正在播放时返回 Play 时的编号；没有在播放时返回 0
     */
    uint32_t PlayingToken() const;

    //以下为 IVoiceRecorderListener、IVoicePlayerListener 的实现：在采集 / 渲染线程上调用，只排队并投递消息

    //录音音量：排进队列，UI 线程上转给 OnVoiceRecordLevel
    virtual void OnVoiceLevel(int level) override;

    //录满：排进队列，UI 线程上转给 OnVoiceRecordLimitReached
    virtual void OnVoiceLimitReached() override;

    //播放进度：排进队列（带当前编号），UI 线程上转给 OnVoicePlayProgress
    virtual void OnVoicePlayProgress(int64_t playedMs, int64_t totalMs) override;

    //播完：排进队列（带当前编号），UI 线程上停止设备后转给 OnVoicePlayFinished
    virtual void OnVoicePlayFinished() override;

    /**
     *  在 UI 线程上取出排着的事件并逐个处理（消息窗口收到 kMsgVoiceEvent 时调用；单元测试也可以直接调用）。
     *  录音已结束、播放已停止或已换文件时，对应的旧事件丢弃。
     */
    void DrainEvents();

private:
    //语音事件的种类
    enum EventKind
    {
        //录音音量
        event_record_level = 0,
        //录满
        event_record_limit = 1,
        //播放进度
        event_play_progress = 2,
        //播放到结尾
        event_play_finished = 3,
    };

    //排队的语音事件
    struct Event
    {
        int      m_kind;      //取值见 enum EventKind
        uint32_t m_token;     //播放事件的编号；录音事件为 0
        int64_t  m_value1;    //音量，或已播放毫秒数
        int64_t  m_value2;    //总毫秒数；其它事件为 0
    };

    //排进一个事件并在需要时投递 kMsgVoiceEvent（任意线程）
    void pushEvent(int kind, uint32_t token, int64_t value1, int64_t value2);

    //消息窗口的窗口过程
    static LRESULT CALLBACK wndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

private:
    IVoiceMicBusyQuery*                       m_busy;          //麦克风占用查询，不持有
    IVoiceControllerSink*                     m_sink;          //事件接收方，不持有
    std::unique_ptr<VoiceWasapiCaptureDevice> m_ownCapture;    //没有注入麦克风设备时自建的 WASAPI 设备
    std::unique_ptr<VoiceWasapiRenderDevice>  m_ownRender;     //没有注入扬声器设备时自建的 WASAPI 设备
    IVoiceCaptureDevice*                      m_capture;       //实际使用的麦克风设备（注入的或自建的）
    IVoiceRenderDevice*                       m_render;        //实际使用的扬声器设备（注入的或自建的）
    VoiceRecorder                             m_recorder;      //录音器
    VoicePlayer                               m_player;        //播放器
    HWND                                      m_hwnd;          //消息窗口（HWND_MESSAGE），Init 时建立、Shutdown 时销毁
    uint32_t                                  m_playToken;     //当前播放的编号；没有在播放时为 0（UI 线程读写）
    mutable std::mutex                        m_eventMutex;    //保护 m_events、m_eventPosted、m_currentToken
    std::deque<Event>                         m_events;        //排着的语音事件
    bool                                      m_eventPosted;   //已投递 kMsgVoiceEvent 且尚未处理，避免重复投递
    uint32_t                                  m_currentToken;  //渲染线程排事件时带上的编号（与 m_playToken 同步）
};

} // namespace voicemsg
