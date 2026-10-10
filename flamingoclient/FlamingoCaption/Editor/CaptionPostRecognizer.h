/**
 *  字幕编辑器的事后识别（docs/语音字幕方案.md 6.3），CaptionPostRecognizer.h
 *
 *  没有勾「生成字幕」录下的录像、以及任意 MP4，都可以在编辑器里「识别字幕」；已有字幕时为「重新识别」（覆盖前由
 *  编辑器确认）。后台线程用 Source Reader 把声音解成 48 kHz 单声道 16 位（解不出这种格式时取原格式自己混成单声道、
 *  线性插值到 48 kHz），尽快送进一路识别流（用途为事后识别）。识别流有排队上限，这里按排队量控制送入速度：
 *  排队超过 kPostRecMaxQueuedSec 秒就等一等。全部送完后让识别流处理完剩余的声音，定稿结果经与录制相同的拼装规则
 *  （CaptionAssembler：整理、替换表、行首标点并回上一条、切分）变成字幕条，来源记为事后识别、不分说话人。
 *  0g 实测多线程不能加速流式识别，这里只开一路；按实时率约 0.16 估算，10 分钟的录像约 1.6 分钟识别完。
 *
 *  进度与结束经 PostMessage 投给编辑器窗口：wParam 为 CaptionPostRecEvent，进度时 lParam 为千分比。结束后编辑器在
 *  UI 线程上调 TakeResult 取结果。
 *
 *  典型用法：
 *      m_recognizer.Start(videoPath, replaceTable, m_hWnd, kMsgCaptionPostRec);
 *      ...（收到 caption_postrec_done）
 *      std::vector<CaptionCue> cues;  bool cancelled = false;  std::string error;
 *      if (m_recognizer.TakeResult(cues, cancelled, error)) model.ReplaceAllCues(cues);
 *
 *  balloonwj@qq.com   2026-10-06
 */
#pragma once

#include <windows.h>
#include <stdint.h>

#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "../CaptionApi.h"
#include "../Engine/CaptionWorker.h"
#include "../Model/CaptionSegmenter.h"
#include "../Model/CaptionTrack.h"

namespace caption {

//送入识别流时排队的上限（秒）：超过就等识别线程消化
const int kPostRecMaxQueuedSec = 10;

//投给编辑器窗口的事件（消息的 wParam）
enum CaptionPostRecEvent
{
    caption_postrec_progress = 1,   //进度：lParam 为千分比
    caption_postrec_done     = 2,   //结束（成功、失败或取消）：随后调 TakeResult
};

/**
 *  CaptionPostRecognizer —— 一次事后识别。
 */
class CaptionPostRecognizer : private ICaptionStreamListener, private ICaptionFlushListener
{
public:
    CaptionPostRecognizer();
    ~CaptionPostRecognizer();

    CaptionPostRecognizer(const CaptionPostRecognizer&) = delete;
    CaptionPostRecognizer& operator=(const CaptionPostRecognizer&) = delete;

    /**
     *  开始识别（UI 线程）。
     *  @param videoPath 录像路径
     *  @param replace   自动替换表（拷贝保存）
     *  @param notifyWnd 接收进度与结束的窗口
     *  @param msg       消息号
     *  @return 已开始返回 true；正在识别时返回 false
     */
    bool Start(const std::wstring& videoPath, const CaptionReplaceTable& replace, HWND notifyWnd, UINT msg);

    //要求取消（任意线程）
    void Cancel();

    //是否正在识别
    bool IsRunning() const;

    //等后台线程结束（析构与关闭编辑器时调用）
    void Wait();

    /**
     *  取结果（收到 caption_postrec_done 之后在 UI 线程调用）。
     *  @param cues      [出参] 识别出的字幕（成功时）
     *  @param cancelled [出参] 是否被取消
     *  @param error     [出参] 失败原因（英文）
     *  @return 成功识别（可能没有任何字幕）返回 true；失败或取消返回 false
     */
    bool TakeResult(std::vector<CaptionCue>& cues, bool& cancelled, std::string& error);

private:
    void threadProc();
    //解码并送入识别流；返回 false 表示失败或取消（原因写进 m_error）
    bool feed(CaptionStreamImpl* stream);
    void post(int event, LPARAM param);
    //ICaptionStreamListener
    virtual void OnCaptionPartial(const wchar_t* text) override;
    virtual void OnCaptionFinal(const wchar_t* text, int64_t startMs, int64_t endMs) override;
    //ICaptionFlushListener：识别流处理完了剩余的声音
    virtual void OnStreamFlushed() override;

    std::wstring                 m_videoPath;    //录像路径（Start 时拷贝）
    CaptionReplaceTable          m_replace;      //自动替换表（Start 时拷贝）
    HWND                         m_notifyWnd;    //接收事件的窗口
    UINT                         m_msg;          //消息号
    std::unique_ptr<std::thread> m_thread;       //后台线程
    std::atomic<bool>            m_running;      //是否正在识别
    std::atomic<bool>            m_cancel;       //是否已要求取消
    std::atomic<bool>            m_flushed;      //识别流是否已处理完剩余声音
    int64_t                      m_durationMs;   //录像时长（进度用）
    std::mutex                   m_resultMutex;  //保护以下四项（识别线程写，UI 线程取）
    CaptionTrack                 m_track;        //识别出的字幕
    CaptionAssembler             m_assembler;    //识别结果装配（引用 m_replace）
    bool                         m_ok;           //是否成功
    bool                         m_cancelled;    //是否被取消
    std::string                  m_error;        //失败原因
};

} // namespace caption
