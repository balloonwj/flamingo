/**
 *  一次录制，RecordSession.h
 *
 *  把画面来源、三路声音、标注与摄像头画中画合成后写进分段式 MP4（docs/屏幕共享与录制方案.md 6.1）。独立录屏与共享录制
 *  共用本类，区别只在于画面与声音从哪里来：
 *    - 画面：IRecordVideoSource，取最新一帧（独立录屏为采集线程的结果，发起人为推流管线采到的原始画面，观看者为解码结果）；
 *    - 声音：调用方把各路 48 kHz 单声道推进 Audio()（RecordAudioMixer）；
 *    - 标注：UI 线程在笔迹变化时把一份拷贝交给 SetAnnotations；
 *    - 画中画：IRecordVideoSource（摄像头预览或当前发言人的摄像头），可为空。
 *  写入线程按录制时长推进：每 1/帧率 秒写一帧（期间没有新画面就重复上一帧），每 20 毫秒写一段混好的声音；暂停的时段
 *  不计入时长；每 5 秒检查一次磁盘剩余空间，不够时自动停止并保存。结束时经 RecordSessionListener 回报（写入线程上）。
 *
 *  典型用法：
 *      RecordOptions opt; opt.m_path = ...; opt.m_sourceWidth = w; opt.m_sourceHeight = h;
 *      if (session.Start(opt, &videoSource, NULL, this)) ...
 *      session.Audio().Push(record_audio_mic, pcm, n);
 *      session.Stop();
 *
 *  balloonwj@qq.com   2026-10-03
 */
#pragma once

#include <stdint.h>
#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "RecordAudioMixer.h"
#include "RecordPolicy.h"   // RecordPipPlacement（画中画摆放，2026-10-05）
#include "../Share/ShareAnnotation.h"

namespace screenrecord {

//录制中检查磁盘剩余空间的默认间隔（毫秒）
const int kRecordDiskCheckMs = 5000;

//停止录制时等写入线程退出：等了这么久（毫秒）还没退出，写第一条诊断日志（bugs.md BUG-120）
const int kRecordStopStallFirstMs = 3000;
//第一条诊断日志之后仍未退出，每隔这么久（毫秒）再写一条
const int kRecordStopStallIntervalMs = 5000;
//等写入线程退出时，两次检查之间的间隔（毫秒）
const int kRecordStopPollMs = 50;

/**
 *  写入线程当前所处的步骤（bugs.md BUG-120 的诊断）。写入线程每进入一步就记下步骤与时刻；停止录制时等它退出太久，
 *  日志里写出它停在哪一步、停了多久，据此判断卡在取画面、字幕旁路、编码器还是收尾。
 */
enum RecordWriterStage
{
    record_stage_not_started = 0,    //写入线程还没有启动（Start 之前，或 Start 失败）
    record_stage_opening     = 1,    //创建文件、初始化合成器与编码器
    record_stage_idle        = 2,    //两轮之间的等待，以及暂停期间的等待
    record_stage_get_frame   = 3,    //向画面来源与画中画来源取最新一帧
    record_stage_compose     = 4,    //装饰画面（鼠标效果）、合成画面并叠上标注与画中画
    record_stage_write_video = 5,    //把一帧交给编码器写进文件
    record_stage_pull_audio  = 6,    //从混音器取一段声音；语音字幕的分路旁路在这一步里同步调用
    record_stage_write_audio = 7,    //把一段声音交给编码器写进文件
    record_stage_disk_check  = 8,    //查询磁盘剩余空间
    record_stage_closing     = 9,    //收尾：编码器写完剩余数据、关闭文件
    record_stage_notifying   = 10,   //回报录制结束（调用 RecordSessionListener::OnRecordStopped）
    record_stage_done        = 11,   //写入线程即将退出
};

/**
 *  写入线程步骤的英文名，用于日志。
 *  @param stage 取值见 RecordWriterStage
 *  @return 不为 NULL 的 ASCII 字符串；不认识的取值返回 "unknown"
 */
const char* RecordWriterStageName(int stage);

/**
 *  停止录制时等写入线程退出，现在是否该写一条诊断日志（纯函数）。
 *  @param waitedMs     已经等了多久，毫秒
 *  @param lastReportMs 上一条诊断日志写在等到第几毫秒时；还没写过传 -1
 *  @param firstMs      等到多久写第一条，毫秒（默认 kRecordStopStallFirstMs）
 *  @param intervalMs   之后每隔多久写一条，毫秒（默认 kRecordStopStallIntervalMs）
 *  @return 该写返回 true
 */
bool RecordStopStallReportDue(int64_t waitedMs, int64_t lastReportMs, int firstMs, int intervalMs);

//画面来源（任意线程调用，实现须线程安全）
class IRecordVideoSource
{
public:
    virtual ~IRecordVideoSource()
    {
    }

    /**
     *  取最新一帧。
     *  @param bgra   [出参] 自上而下的 BGRA，行跨度 = 宽 × 4
     *  @param serial [出参] 帧序号，调用方据此判断是不是新画面
     *  @return 已有画面返回 true
     */
    virtual bool GetRecordFrame(std::vector<uint8_t>& bgra, int& width, int& height, uint64_t& serial) = 0;
};

//录制结束的原因
enum RecordStopReason
{
    record_stop_user      = 1,   //调用方调了 Stop
    record_stop_disk_full = 2,   //磁盘剩余空间不够，自动停止
    record_stop_error     = 3,   //写入失败（文件打不开、编码器出错）
};

/**
 *  录制画面的装饰（屏幕共享第 16 期）：独立录屏在写每一帧之前，把鼠标效果画到画面上（见 RecordMouseDecorator）。
 *  录制线程把画面来源给的原始画面拷一份交给它，画完再交给合成器，原始画面不改。
 */
class IRecordFrameDecorator
{
public:
    virtual ~IRecordFrameDecorator()
    {
    }

    /**
     *  在画面上就地画（录制线程，写每一帧之前调用一次）。
     *  @param bgra   画面第一行像素，自上而下，行跨度 = width × 4
     *  @param width  画面宽，单位像素
     *  @param height 画面高，单位像素
     *  @param nowMs  本机时刻（毫秒，单调递增）
     */
    virtual void Decorate(uint8_t* bgra, int width, int height, int64_t nowMs) = 0;
};

//录制结束的回报（写入线程上调用，实现方自行转到 UI 线程）
class RecordSessionListener
{
public:
    virtual ~RecordSessionListener()
    {
    }

    /**
     *  录制已结束、文件已关闭。
     *  @param reason     取值见 RecordStopReason
     *  @param path       文件路径
     *  @param durationMs 录到的时长（不含暂停），毫秒
     */
    virtual void OnRecordStopped(int reason, const std::wstring& path, int64_t durationMs) = 0;
};

//录制参数
struct RecordOptions
{
    std::wstring m_path;           //输出文件路径
    int          m_sourceWidth;    //画面来源的宽（决定输出尺寸，见 ComputeRecordSize）；只录声音时不用
    int          m_sourceHeight;   //画面来源的高；只录声音时不用
    int          m_fps;            //帧率（夹到 1~30）；只录声音时不用
    int          m_quality;        //画质，取值见 RecordQuality；只录声音时不用
    bool         m_audioOnly;      //只录声音（第 24 期）：不要画面来源，文件里只有音频流（路径由调用方以 .m4a 结尾）

    RecordOptions();
};

//一次录制
class RecordSession
{
public:
    RecordSession();
    ~RecordSession();

    RecordSession(const RecordSession&) = delete;
    RecordSession& operator=(const RecordSession&) = delete;

    /**
     *  开始录制：检查磁盘空间、起写入线程（写入线程里创建文件）。
     *  @param options  参数
     *  @param video    画面来源，不拥有，Stop 返回之前须一直有效；只录声音（options.m_audioOnly）时传 NULL，传了也不用
     *  @param pip      画中画来源（可为 NULL），同上
     *  @param listener 结束回报，不拥有，同上；可为 NULL
     *  @return 已开始返回 true；正在录制、参数不对或磁盘空间不够返回 false（FailReason 说明原因）
     */
    bool Start(const RecordOptions& options, IRecordVideoSource* video, IRecordVideoSource* pip,
               RecordSessionListener* listener);

    //停止并保存（等写入线程收尾）；可重复调用
    void Stop();

    bool IsRunning() const;

    //暂停或继续；暂停的时段不计入时长
    void SetPaused(bool paused);
    bool IsPaused() const;

    //已录时长（不含暂停），毫秒
    int64_t RecordedMs() const;

    //三路声音的混音器（调用方推入各路声音、打开或关闭各路）
    RecordAudioMixer& Audio();

    //要叠的标注（UI 线程在笔迹变化时交一份拷贝；共享录制且勾选了「标注」时才调用）
    void SetAnnotations(const std::vector<screenshare::AnnotationStroke>& strokes);

    //是否画画中画（来源为 Start 时给的 pip）
    void SetPipEnabled(bool on);

    /**
     *  画中画的摆放（2026-10-05，任意线程）：写入线程从下一帧起按它摆画中画。
     *  @param placement 摆放，见 RecordPolicy.h 的 RecordPipPlacement
     */
    void SetPipPlacement(const RecordPipPlacement& placement);

    //当前的画中画摆放（任意线程）
    RecordPipPlacement PipPlacement() const;

    /**
     *  登记画面装饰（第 16 期：独立录屏的鼠标效果）。在 Start 之前调用；共享录制不登记（画面里已有效果）。
     *  @param decorator 不拥有，Stop 返回之前须一直有效；传 NULL 取消
     */
    void SetDecorator(IRecordFrameDecorator* decorator);

    //Start 失败的原因：record_fail_*
    int FailReason() const;

    //测试用：替换取磁盘剩余空间的函数（参数为文件所在目录）
    typedef uint64_t (*FreeSpaceFn)(const std::wstring& dir);
    void SetFreeSpaceFn(FreeSpaceFn fn);

    //测试用：录制中检查磁盘空间的间隔（毫秒），默认 kRecordDiskCheckMs
    void SetDiskCheckIntervalMs(int ms);

    //写入线程当前所处的步骤（任意线程），取值见 RecordWriterStage；用于诊断与测试
    int WriterStage() const;

    //写入线程进入当前步骤已经多久（任意线程），毫秒；写入线程没有启动时为 0
    int64_t WriterStageElapsedMs() const;

    /**
     *  上一次 Stop 等写入线程退出时，最后一条诊断日志里记下的步骤（只在调用 Stop 的线程上读）。
     *  @return 取值见 RecordWriterStage；上一次 Stop 没有等到需要写诊断日志的程度时为 -1
     */
    int LastStopStallStage() const;

    //测试用：停止时等写入线程多久（毫秒）写第一条诊断日志，默认 kRecordStopStallFirstMs
    void SetStopStallWarnMs(int ms);

private:
    void threadProc();
    uint64_t freeSpace() const;
    int64_t elapsedHns() const;
    //写入线程进入一个新步骤：记下步骤与时刻
    void enterStage(int stage);
    //Stop 里等写入线程退出：等太久时按 RecordStopStallReportDue 写诊断日志；返回时写入线程已退出 threadProc 的主体
    void waitWriterExit();

    RecordOptions              m_options;       //参数
    IRecordVideoSource*        m_video;         //画面来源（不拥有）
    IRecordVideoSource*        m_pip;           //画中画来源（不拥有）
    RecordSessionListener*     m_listener;      //结束回报（不拥有）
    RecordAudioMixer           m_audio;         //混音
    std::unique_ptr<std::thread> m_thread;      //写入线程
    std::atomic<bool>          m_stop;          //通知写入线程停止
    std::atomic<bool>          m_running;       //是否在录制
    std::atomic<bool>          m_paused;        //是否暂停
    std::atomic<bool>          m_pipEnabled;    //是否画画中画
    std::atomic<int64_t>       m_recordedHns;   //已录时长（写入线程更新），100 纳秒单位
    int                        m_failReason;    //Start 失败的原因
    FreeSpaceFn                m_freeSpaceFn;   //取磁盘剩余空间（测试可替换）
    int                        m_diskCheckMs;   //录制中检查磁盘空间的间隔（毫秒）
    mutable std::mutex         m_annotMutex;    //保护 m_annotations
    mutable std::mutex         m_pipMutex;      //保护 m_pipPlacement（UI 线程写、写入线程每帧读）
    RecordPipPlacement         m_pipPlacement;  //画中画的摆放（2026-10-05）；默认右下角
    std::vector<screenshare::AnnotationStroke> m_annotations;   //要叠的标注
    bool                       m_annotationsSet;   //是否设置过标注（没设置过就不画）
    std::atomic<IRecordFrameDecorator*> m_decorator;   //画面装饰（第 16 期，不拥有）；为 NULL 时不装饰
    std::atomic<int>           m_writerStage;      //写入线程当前所处的步骤（RecordWriterStage），写入线程写、任意线程读
    std::atomic<int64_t>       m_writerStageSinceMs;   //写入线程进入当前步骤时的 GetTickCount64，毫秒
    std::atomic<bool>          m_writerExited;     //写入线程已走完 threadProc 的主体（Start 时清掉）；Stop 据此判断还要不要等
    int                        m_stopStallFirstMs; //停止时等多久写第一条诊断日志（毫秒），测试可改
    int                        m_lastStopStallStage;   //上一次 Stop 最后一条诊断日志里的步骤，没写过为 -1（只在调用 Stop 的线程上读写）
};

//Start 失败的原因
enum RecordFailReason
{
    record_fail_none      = 0,   //没有失败
    record_fail_running   = 1,   //已在录制
    record_fail_argument  = 2,   //参数不对（路径为空；不是只录声音时画面来源为空、尺寸不对）
    record_fail_disk_full = 3,   //磁盘剩余空间不够
};

} // namespace screenrecord
