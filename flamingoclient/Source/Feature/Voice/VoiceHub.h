/**
 *  VoiceHub.h
 *
 *  exe 侧的语音消息中心（语音消息第 3b 步，方案见 docs/语音消息方案.md 第 7.3 节）：聊天窗的录音条与
 *  FlamingoMedia.dll 之间关于录音的唯一衔接点。
 *    - 开始录音：录音文件先写成 ChatVoice\rec_<clientmsgid>.ogg，交给 DLL 录；
 *    - 停止录音：DLL 写好文件后算 md5，改名为 ChatVoice\<md5>.ogg（即方案 V5 的缓存文件名，发送方自己的
 *      语音不用再下载）；不到 kVoiceMinDurationMs 时删掉文件、不发送；
 *    - 取消录音：DLL 不写文件，本类删掉可能残留的临时文件；
 *    - 宿主适配器收到的录音音量、录满两个回调转给正在录音的那个录音条（模块的回调不带会话，由本类记住）。
 *    - 播放（第 3e 步）：同一时刻只播一条，开播新的一条时通知原来的聊天窗「已停止」；模块的进度、播完两个回调
 *      按播放编号核对后转给发起播放的聊天窗，旧编号的丢弃；聊天窗关闭、开始录音时停止播放。
 *  上传不在这里（第 3c 步，见 net::FileTransfer::EnqueueSendVoice）。
 *
 *  进程内唯一（GetInstance），所有方法只在 UI 线程调用。模块指针由主面板在 SetMediaModule 时登记，DLL 未加载时
 *  开始录音返回 media_voice_record_not_ready。
 *
 *  典型用法（聊天窗录音条）：
 *      int r = balloonvoice::CVoiceHub::GetInstance().StartRecord(this, voiceDir, clientMsgId);
 *      ...
 *      balloonvoice::VoiceRecordOutput out;
 *      if (balloonvoice::CVoiceHub::GetInstance().StopRecord(out) == balloonvoice::voice_stop_ok) { ... }
 *
 *  balloonwj@qq.com   2026-10-04
 */
#pragma once

#include <stdint.h>
#include <string>
#include <vector>

namespace flamingomedia { class IMediaModule; }

namespace balloonvoice {

//语音消息的最短时长（毫秒）：不到 1 秒点「发送」不发，见方案第 3.1 节
const int64_t kVoiceMinDurationMs = 1000;

/**
 *  录音条要接收的通知。由 CVoiceHub 在 UI 线程上调用。
 */
class IVoiceRecordListener
{
public:
    virtual ~IVoiceRecordListener()
    {
    }

    /**
     *  录音中的音量，约每 100 毫秒一次。
     *  @param level 0~255
     */
    virtual void OnVoiceRecordLevel(int level) = 0;

    //已录满 2 分钟，录音停在这里等用户点「发送」或「取消」；只通知一次
    virtual void OnVoiceRecordLimitReached() = 0;
};

//StopRecord 的结果
enum VoiceStopResult
{
    //已停止，文件已改名为 ChatVoice\<md5>.ogg，可以发送
    voice_stop_ok = 0,
    //时长不到 kVoiceMinDurationMs，文件已删掉，不发送（录音条提示「说话时间太短」）
    voice_stop_too_short = 1,
    //DLL 写文件失败或算 md5、改名失败，文件已删掉
    voice_stop_failed = 2,
    //当前没有在录音
    voice_stop_not_recording = 3,
};

/**
 *  一次录音的结果（StopRecord 返回 voice_stop_ok 时有效）。
 */
struct VoiceRecordOutput
{
    std::wstring         m_path;        //缓存文件的完整路径：ChatVoice\<md5>.ogg
    std::string          m_md5;         //文件内容 MD5（小写十六进制）
    int64_t              m_durationMs;  //有效时长（毫秒）
    int64_t              m_fileBytes;   //文件字节数
    std::vector<uint8_t> m_wave;        //波形值，每个 0~255
    bool                 m_reusedExisting;  //改名时缓存里已有同内容的文件、直接用了它（DiscardRecorded 时不删）

    VoiceRecordOutput();
};

/**
 *  一次播放对应的语音：msgid 大于 0 时按 msgid 认，否则按 clientMsgId 认（还没回执的本人语音）。
 */
struct VoicePlayKey
{
    long long   m_msgid;        //服务器消息ID；还没回执的本人语音为 0
    std::string m_clientMsgId;  //客户端消息ID；收到的语音可以为空

    VoicePlayKey()
        : m_msgid(0)
    {
    }
};

//一次播放结束的原因（IVoicePlayListener::OnVoicePlayEnded 的 reason）
enum VoicePlayEnd
{
    //播到了结尾：聊天窗据此接着播下一条未听的
    voice_play_end_finished = 0,
    //被停止：用户再点了一下、开播了别的一条、开始录音、模块卸载；不接着播
    voice_play_end_stopped = 1,
};

/**
 *  发起播放的聊天窗要接收的通知。由 CVoiceHub 在 UI 线程上调用。
 */
class IVoicePlayListener
{
public:
    virtual ~IVoicePlayListener()
    {
    }

    /**
     *  播放进度，约每 100 毫秒一次。
     *  @param key      正在播的那条语音
     *  @param playedMs 已播放的毫秒数
     *  @param totalMs  总毫秒数
     */
    virtual void OnVoicePlayProgress(const VoicePlayKey& key, int64_t playedMs, int64_t totalMs) = 0;

    /**
     *  播放结束。调用时语音中心已清掉播放状态，可以在这里直接开播下一条。
     *  @param key    结束的那条语音
     *  @param reason 取值见 VoicePlayEnd
     */
    virtual void OnVoicePlayEnded(const VoicePlayKey& key, int reason) = 0;
};

/**
 *  exe 侧的语音消息中心，用法见文件头。
 */
class CVoiceHub
{
public:
    //取进程内唯一的对象（函数内静态变量）
    static CVoiceHub& GetInstance();

    /**
     *  登记 FlamingoMedia.dll 的模块接口。
     *  @param module 模块接口，不拥有；传 NULL 表示模块不可用（卸载前调用；正在录音时先取消）
     */
    void AttachModule(flamingomedia::IMediaModule* module);

    //模块是否已加载；未加载时聊天窗隐藏「语音消息」按钮
    bool IsModuleReady() const;

    /**
     *  开始录音。
     *  @param listener    录音条（不持有），须一直有效到 StopRecord、CancelRecord 或 DetachListener
     *  @param voiceDir    ChatVoice 目录的完整路径（以反斜杠结尾），不存在时创建
     *  @param clientMsgId 这条语音消息的客户端消息 id，用作临时文件名；不能为空
     *  @return 取值见 flamingomedia::MediaVoiceRecordStart；已经有别的录音条在录时返回 media_voice_record_already
     */
    int StartRecord(IVoiceRecordListener* listener, const std::wstring& voiceDir, const std::string& clientMsgId);

    /**
     *  停止录音：DLL 写好文件后算 md5、改名为 ChatVoice\<md5>.ogg（已有同名文件时直接用它、删掉临时文件）。
     *  @param out [出参] 录音结果，返回 voice_stop_ok 时有效
     *  @return 取值见 enum VoiceStopResult
     */
    int StopRecord(VoiceRecordOutput& out);

    //取消录音：DLL 不写文件，删掉可能残留的临时文件。没有在录音时什么也不做；暂停中同样可以取消
    void CancelRecord();

    /**
     *  暂停录音（语音消息第 6e 步，录音条的「暂停」）：交给模块关闭麦克风采集，已录的内容保留；暂停中照常可以
     *  StopRecord（写出已录的部分）或 CancelRecord。
     *  @return 正在录音（未暂停）并已暂停时返回 true；没有在录音、已经暂停、或模块拒绝时返回 false
     */
    bool PauseRecord();

    /**
     *  继续已暂停的录音。与开始录音一样，先停止正在播放的语音（一边放一边录会把扬声器的声音录进去），
     *  并通知发起播放的窗口「已停止」。
     *  @return 取值见 flamingomedia::MediaVoiceRecordStart：成功为 media_voice_record_ok；麦克风被通话或屏幕共享
     *          占用、打不开时为对应的值，录音保持暂停；没有已暂停的录音为 media_voice_record_not_paused；
     *          模块未加载为 media_voice_record_not_ready
     */
    int ResumeRecord();

    //录音是否已暂停（PauseRecord 成功之后、ResumeRecord 成功或停止 / 取消之前）
    bool IsRecordPaused() const;

    /**
     *  把已暂停的录音里已录的部分另存成临时文件，供录音条在暂停时试听（2026-10-05）。文件在录音临时文件旁边
     *  （VoicePausedSnapshotPath），再次另存时覆盖；继续录音、停止或取消时删除。录音保持暂停。
     *  @param outPath       [出参] 另存文件的完整路径，返回 true 时有效（交给 StartPlay 播放）
     *  @param outDurationMs [出参] 另存文件的时长（毫秒），返回 true 时有效
     *  @return 录音已暂停、模块另存成功时返回 true；没有已暂停的录音、模块未加载或另存失败时返回 false
     */
    bool SnapshotPausedRecord(std::wstring& outPath, int64_t& outDurationMs);

    /**
     *  丢弃一段已经停止、还没发送的录音（录音条试听状态下确认放弃）：删除 StopRecord 写出的缓存文件。
     *  改名时缓存里本来就有同内容的文件、直接用了它的（m_reusedExisting 为 true）不删，那是别的消息在用的文件。
     *  @param out StopRecord 返回 voice_stop_ok 时得到的结果
     *  @return 删除了文件返回 true；不该删、路径为空或删除失败时返回 false
     */
    bool DiscardRecorded(const VoiceRecordOutput& out);

    /**
     *  某个录音条是否正在录音。
     *  @param listener 录音条
     *  @return 正在录音且就是它发起的返回 true
     */
    bool IsRecordingFor(const IVoiceRecordListener* listener) const;

    /**
     *  录音条要销毁（聊天窗关闭）：它正在录音时按取消处理，之后不再给它发通知。
     *  @param listener 录音条
     */
    void DetachListener(IVoiceRecordListener* listener);

    /**
     *  宿主适配器转来的录音音量（UI 线程），转给正在录音的录音条。
     *  @param level 0~255
     */
    void OnRecordLevel(int level);

    //宿主适配器转来的「已录满」（UI 线程），转给正在录音的录音条
    void OnRecordLimitReached();

    /**
     *  播放一条语音（语音消息第 3e 步）。正在播别的时先结束它并通知原来的聊天窗「已停止」。
     *  @param listener 发起播放的聊天窗（不持有），须一直有效到播放结束或 DetachPlayListener
     *  @param key      这条语音，原样带回进度与结束通知
     *  @param path     语音文件的完整路径
     *  @return 模块打开文件与扬声器成功时返回 true；失败时不进入播放
     */
    bool StartPlay(IVoicePlayListener* listener, const VoicePlayKey& key, const std::wstring& path);

    //停止播放，通知发起播放的聊天窗「已停止」。没有在播放时什么也不做
    void StopPlay();

    /**
     *  某个聊天窗是否正在播放。
     *  @param listener 聊天窗
     *  @return 正在播放且是它发起的返回 true
     */
    bool IsPlayingFor(const IVoicePlayListener* listener) const;

    /**
     *  聊天窗要销毁：它正在播放时停止，之后不再给它发通知。
     *  @param listener 聊天窗
     */
    void DetachPlayListener(IVoicePlayListener* listener);

    /**
     *  宿主适配器转来的播放进度（UI 线程）。编号不是当前播放的（已停止或已改播别的）时丢弃。
     *  @param token    IMediaModule::PlayVoice 时传入的编号
     *  @param playedMs 已播放的毫秒数
     *  @param totalMs  总毫秒数
     */
    void OnPlayProgress(uint32_t token, int64_t playedMs, int64_t totalMs);

    /**
     *  宿主适配器转来的「播完」（UI 线程）。编号不是当前播放的时丢弃。
     *  @param token IMediaModule::PlayVoice 时传入的编号
     */
    void OnPlayFinished(uint32_t token);

private:
    CVoiceHub();

    CVoiceHub(const CVoiceHub&) = delete;
    CVoiceHub& operator=(const CVoiceHub&) = delete;

    //清掉录音状态（不调用 DLL），并删除暂停中试听另存的临时文件
    void resetRecordState();

    //删除暂停中试听另存的临时文件（没有时什么也不做）；调用前须已停止播放它
    void deletePausedSnapshot();

    /**
     *  结束当前的播放：先清掉播放状态，再按需停止模块、通知聊天窗。没有在播放时什么也不做。
     *  @param reason     取值见 VoicePlayEnd
     *  @param notify     是否通知发起播放的聊天窗
     *  @param stopModule 是否调用模块的 StopVoicePlay（模块已播完或马上要播新文件时不必）
     */
    void endPlay(int reason, bool notify, bool stopModule);

private:
    flamingomedia::IMediaModule* m_module;     //模块接口，不拥有；DLL 未加载时为 NULL
    IVoiceRecordListener*        m_listener;   //正在录音的录音条，不拥有；没有在录音时为 NULL
    std::wstring                 m_recPath;    //正在录音的临时文件路径；没有在录音时为空
    std::wstring                 m_voiceDir;   //正在录音的 ChatVoice 目录
    bool                         m_recPaused;  //正在录音的这一段是否已暂停；没有在录音时为 false
    std::wstring                 m_snapshotPath;  //暂停中试听另存的临时文件；没有另存过、或已删除时为空

    IVoicePlayListener*          m_playListener;  //发起当前播放的聊天窗，不持有；没有在播放时为 NULL
    VoicePlayKey                 m_playKey;       //当前播放的语音
    uint32_t                     m_playToken;     //当前播放的编号；没有在播放时为 0
    uint32_t                     m_lastToken;     //最近分配的播放编号，只增不减
};

/**
 *  录音的临时文件路径。
 *  @param voiceDir    ChatVoice 目录（以反斜杠结尾）
 *  @param clientMsgId 客户端消息 id
 *  @return voiceDir + "rec_" + clientMsgId + ".ogg"
 */
std::wstring VoiceRecordTempPath(const std::wstring& voiceDir, const std::string& clientMsgId);

/**
 *  暂停中试听另存文件的路径（2026-10-05）：录音临时文件名去掉扩展名后加上「_paused.ogg」。
 *  @param recPath 录音的临时文件路径（VoiceRecordTempPath 的结果）
 *  @return 如 voiceDir + "rec_" + clientMsgId + "_paused.ogg"
 */
std::wstring VoicePausedSnapshotPath(const std::wstring& recPath);

/**
 *  语音的缓存文件路径（收发两端相同，见方案 V5）。
 *  @param voiceDir ChatVoice 目录（以反斜杠结尾）
 *  @param md5      文件内容 MD5（小写十六进制）
 *  @return voiceDir + md5 + ".ogg"
 */
std::wstring VoiceCachePath(const std::wstring& voiceDir, const std::string& md5);

/**
 *  计算语音文件的 MD5（整个读进内存计算；语音文件最大约 1 MiB）。
 *  @param path 文件完整路径
 *  @param md5  [出参] 小写十六进制，返回 true 时有效
 *  @return 读文件成功返回 true
 */
bool ComputeVoiceFileMd5(const std::wstring& path, std::string& md5);

} // namespace balloonvoice
