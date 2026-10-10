/**
 *  一次字幕导出（docs/语音字幕方案.md 第 8 节），CaptionExportJob.h
 *
 *  在后台线程上依次生成用户勾选的几种文件：外挂字幕 SRT（直接写文件）、字幕轨版（原样转封装成普通 MP4 再追加 tx3g
 *  字幕轨；转封装失败时改为解码再编码，画面不叠字）、硬字幕版（解码、叠字幕、再编码）。每种都先写到目标旁边的
 *  「.part」临时文件，成功后才改名为目标文件，失败或取消时删掉临时文件，原录像始终不动。
 *  进度与结果经 ICaptionExportListener 在后台线程上回报，界面自行转到 UI 线程（导出窗口据此更新进度条，结束后把
 *  导出记录写进草稿）。同一个对象同一时间只跑一次导出。
 *
 *  后台线程自己初始化 COM（多线程套间）与 Media Foundation；字幕绘制用的 GDI+ 由模块初始化时启动，进程内共用。
 *
 *  典型用法（UI 线程）：
 *      caption::CaptionExportRequest req;
 *      req.m_videoPath = path;  req.m_cues = draft.m_cues;  req.m_hard = true;  req.m_hardPath = ...;
 *      m_job.Start(req, this);
 *      ...
 *      m_job.Cancel();   //用户点了「取消」
 *
 *  balloonwj@qq.com   2026-10-06
 */
#pragma once

#include <stdint.h>

#include <atomic>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "CaptionMediaIo.h"
#include "../Model/CaptionCue.h"
#include "../Model/CaptionDraft.h"

namespace caption {

//导出请求：要生成哪几种、写到哪里
struct CaptionExportRequest
{
    std::wstring            m_videoPath;    //原录像路径
    std::vector<CaptionCue> m_cues;         //字幕（编辑器里的当前内容）
    CaptionStyle            m_style;        //字幕外观（硬字幕版用；SRT 与字幕轨是否带说话人前缀也取它的 m_showSpeaker）
    CaptionPip              m_pip;          //画中画位置（硬字幕版排版时避开）
    bool                    m_hard;         //是否生成硬字幕版
    bool                    m_track;        //是否生成字幕轨版
    bool                    m_srt;          //是否生成 SRT
    bool                    m_text;         //是否生成纯文本（14.13，录音与录像都可以）
    std::wstring            m_hardPath;     //硬字幕版的目标路径
    std::wstring            m_trackPath;    //字幕轨版的目标路径
    std::wstring            m_srtPath;      //SRT 的目标路径
    std::wstring            m_textPath;     //纯文本的目标路径

    CaptionExportRequest()
        : m_hard(false)
        , m_track(false)
        , m_srt(false)
        , m_text(false)
    {
    }
};

//一种文件的导出结果
struct CaptionExportItemResult
{
    int          m_kind;    //种类，取值见 CaptionExportKind
    bool         m_ok;      //是否成功
    std::wstring m_path;    //目标路径
    std::string  m_error;   //失败原因（英文，写日志用）；成功时为空

    CaptionExportItemResult()
        : m_kind(0)
        , m_ok(false)
    {
    }
};

//整次导出的结果
struct CaptionExportResult
{
    bool                                 m_cancelled;   //是否被取消（取消时 m_items 只含已完成的几项）
    std::vector<CaptionExportItemResult> m_items;       //按处理顺序（SRT、纯文本、字幕轨版、硬字幕版）的各项结果

    CaptionExportResult()
        : m_cancelled(false)
    {
    }
};

/**
 *  导出的进度与结果回报，都在后台线程上调用。
 */
class ICaptionExportListener
{
public:
    /**
     *  进度。
     *  @param kind     正在生成的种类（CaptionExportKind）
     *  @param permille 这一种已完成的千分比
     *  @param overall  整次导出已完成的千分比（按各种文件的大致耗时加权）
     */
    virtual void OnExportProgress(int kind, int permille, int overall) = 0;

    /**
     *  整次导出结束（成功、部分失败或取消）。此后本对象不再回调，可以再次 Start。
     *  @param result 结果
     */
    virtual void OnExportFinished(const CaptionExportResult& result) = 0;

protected:
    ~ICaptionExportListener()
    {
    }
};

/**
 *  CaptionExportJob —— 一次导出的后台任务。
 */
class CaptionExportJob : private ICaptionMediaProgress
{
public:
    CaptionExportJob();
    ~CaptionExportJob();

    CaptionExportJob(const CaptionExportJob&) = delete;
    CaptionExportJob& operator=(const CaptionExportJob&) = delete;

    /**
     *  开始导出（UI 线程）。
     *  @param request  请求（拷贝保存）
     *  @param listener 回报对象（不拥有，须在 OnExportFinished 回调之前一直有效）
     *  @return 已开始返回 true；正在导出、或一种都没勾时返回 false
     */
    bool Start(const CaptionExportRequest& request, ICaptionExportListener* listener);

    //要求取消（任意线程）；正在处理的那一种会尽快停止，已完成的保留
    void Cancel();

    //是否正在导出
    bool IsRunning() const;

    //等后台线程结束（析构与模块关闭时调用）；未在运行时立即返回
    void Wait();

private:
    void threadProc();
    //各种文件的处理；返回 false 表示失败或取消（原因写进 item.m_error）
    bool exportSrt(CaptionExportItemResult& item);
    bool exportText(CaptionExportItemResult& item);
    bool exportTrack(CaptionExportItemResult& item);
    bool exportHard(CaptionExportItemResult& item);
    //ICaptionMediaProgress：转成整次导出的进度
    virtual void OnMediaProgress(int permille) override;
    virtual bool IsMediaCancelled() override;

    CaptionExportRequest          m_request;        //这次的请求（Start 时拷贝，后台线程只读）
    ICaptionExportListener*       m_listener;       //回报对象（不拥有）
    std::unique_ptr<std::thread>  m_thread;         //后台线程
    std::atomic<bool>             m_running;        //是否正在导出
    std::atomic<bool>             m_cancel;         //是否已要求取消
    int                           m_currentKind;    //后台线程正在生成的种类（只在后台线程读写）
    int                           m_doneWeight;     //已完成的几种的权重之和（只在后台线程读写）
    int                           m_totalWeight;    //全部要生成的几种的权重之和（只在后台线程读写）
    int                           m_currentWeight;  //正在生成的这一种的权重（只在后台线程读写）
};

} // namespace caption
