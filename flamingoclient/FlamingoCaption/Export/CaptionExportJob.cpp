/**
 *  一次字幕导出，见 CaptionExportJob.h。
 *  balloonwj@qq.com   2026-10-06
 */
#include "CaptionExportJob.h"

#include <windows.h>
#include <objbase.h>
#include <mfapi.h>

#include "CaptionTimedText.h"
#include "Mp4TextTrack.h"
#include "../Model/CaptionText.h"
#include "../../Source/Common/Log/IULog.h"   //LOG_INFO 等宏：DLL 内经 Host/IULogBridge.cpp 交给宿主，单测里写客户端日志

namespace caption {

namespace {

//临时文件后缀：生成成功后才改名为目标文件
const wchar_t* const kPartSuffix = L".part";
//各种文件在整次进度里的权重（大致耗时之比）：SRT 与纯文本瞬间完成，字幕轨版只转封装，硬字幕版要解码再编码
const int kWeightSrt = 1;
const int kWeightText = 1;
const int kWeightTrack = 20;
const int kWeightHard = 100;
//进度的满分（千分比）
const int kPermilleFull = 1000;
//字幕轨的语言代码（ISO 639-2）：字幕内容以中文为主
const char* const kTrackLanguage = "chi";

//把文件内容整体写出（先写临时文件再改名）
bool WriteWholeFile(const std::wstring& path, const std::string& bytes, std::string& error)
{
    const std::wstring temp = path + kPartSuffix;
    HANDLE f = ::CreateFileW(temp.c_str(), GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (f == INVALID_HANDLE_VALUE)
    {
        error = "create srt file failed";
        return false;
    }
    DWORD written = 0;
    const BOOL ok = bytes.empty() ? TRUE
                                  : ::WriteFile(f, bytes.data(), static_cast<DWORD>(bytes.size()), &written, NULL);
    ::CloseHandle(f);
    if (!ok || written != static_cast<DWORD>(bytes.size()))
    {
        ::DeleteFileW(temp.c_str());
        error = "write srt file failed";
        return false;
    }
    if (!::MoveFileExW(temp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING))
    {
        ::DeleteFileW(temp.c_str());
        error = "replace srt file failed";
        return false;
    }
    return true;
}

//把生成好的临时文件改名为目标文件
bool CommitPart(const std::wstring& temp, const std::wstring& path, std::string& error)
{
    if (::MoveFileExW(temp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING))
        return true;
    ::DeleteFileW(temp.c_str());
    error = "replace target file failed";
    return false;
}

} // namespace

CaptionExportJob::CaptionExportJob()
    : m_listener(NULL)
    , m_running(false)
    , m_cancel(false)
    , m_currentKind(0)
    , m_doneWeight(0)
    , m_totalWeight(0)
    , m_currentWeight(0)
{
}

CaptionExportJob::~CaptionExportJob()
{
    Cancel();
    Wait();
}

bool CaptionExportJob::Start(const CaptionExportRequest& request, ICaptionExportListener* listener)
{
    if (m_running.load())
        return false;
    if (!request.m_hard && !request.m_track && !request.m_srt && !request.m_text)
        return false;
    //上一次的线程已结束但还没回收
    Wait();
    m_request = request;
    m_listener = listener;
    m_cancel.store(false);
    m_running.store(true);
    m_thread.reset(new std::thread(&CaptionExportJob::threadProc, this));
    return true;
}

void CaptionExportJob::Cancel()
{
    m_cancel.store(true);
}

bool CaptionExportJob::IsRunning() const
{
    return m_running.load();
}

void CaptionExportJob::Wait()
{
    if (m_thread)
    {
        if (m_thread->joinable())
            m_thread->join();
        m_thread.reset();
    }
}

void CaptionExportJob::threadProc()
{
    ::SetThreadPriority(::GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
    const HRESULT co = ::CoInitializeEx(NULL, COINIT_MULTITHREADED);
    const HRESULT mf = ::MFStartup(MF_VERSION);
    LOG_INFO("caption export: started, hard=%d, track=%d, srt=%d, text=%d, %u cues.", m_request.m_hard ? 1 : 0,
             m_request.m_track ? 1 : 0, m_request.m_srt ? 1 : 0, m_request.m_text ? 1 : 0,
             static_cast<unsigned>(m_request.m_cues.size()));

    m_doneWeight = 0;
    m_totalWeight = (m_request.m_srt ? kWeightSrt : 0) + (m_request.m_text ? kWeightText : 0) +
                    (m_request.m_track ? kWeightTrack : 0) + (m_request.m_hard ? kWeightHard : 0);
    CaptionExportResult result;
    //按耗时从短到长：SRT、纯文本、字幕轨版、硬字幕版
    const int kinds[] = { caption_export_srt, caption_export_text, caption_export_track, caption_export_hard };
    for (size_t i = 0; i < sizeof(kinds) / sizeof(kinds[0]); ++i)
    {
        const int kind = kinds[i];
        const bool wanted = (kind == caption_export_srt && m_request.m_srt) ||
                            (kind == caption_export_text && m_request.m_text) ||
                            (kind == caption_export_track && m_request.m_track) ||
                            (kind == caption_export_hard && m_request.m_hard);
        if (!wanted)
            continue;
        if (m_cancel.load())
        {
            result.m_cancelled = true;
            break;
        }
        m_currentKind = kind;
        if (kind == caption_export_srt)
            m_currentWeight = kWeightSrt;
        else if (kind == caption_export_text)
            m_currentWeight = kWeightText;
        else if (kind == caption_export_track)
            m_currentWeight = kWeightTrack;
        else
            m_currentWeight = kWeightHard;
        OnMediaProgress(0);
        CaptionExportItemResult item;
        item.m_kind = kind;
        bool ok = false;
        //每一种：SRT 直接写，字幕轨版与硬字幕版各自处理
        switch (kind)
        {
        //外挂字幕
        case caption_export_srt:
            ok = exportSrt(item);
            break;
        //纯文本（14.13）
        case caption_export_text:
            ok = exportText(item);
            break;
        //字幕轨版
        case caption_export_track:
            ok = exportTrack(item);
            break;
        //硬字幕版
        default:
            ok = exportHard(item);
            break;
        }
        item.m_ok = ok;
        if (!ok && m_cancel.load())
        {
            result.m_cancelled = true;
            LOG_INFO("caption export: kind %d cancelled.", kind);
            break;
        }
        if (ok)
            LOG_INFO("caption export: kind %d done.", kind);
        else
            LOG_ERROR("caption export: kind %d failed: %s.", kind, item.m_error.c_str());
        result.m_items.push_back(item);
        m_doneWeight += m_currentWeight;
        OnMediaProgress(kPermilleFull);
    }

    if (SUCCEEDED(mf))
        ::MFShutdown();
    if (SUCCEEDED(co))
        ::CoUninitialize();
    LOG_INFO("caption export: finished, %u item(s), cancelled=%d.", static_cast<unsigned>(result.m_items.size()),
             result.m_cancelled ? 1 : 0);
    ICaptionExportListener* listener = m_listener;
    m_running.store(false);
    if (listener != NULL)
        listener->OnExportFinished(result);
}

bool CaptionExportJob::exportSrt(CaptionExportItemResult& item)
{
    item.m_path = m_request.m_srtPath;
    if (item.m_path.empty())
    {
        item.m_error = "srt path empty";
        return false;
    }
    const std::string bytes = CaptionSrt::Write(m_request.m_cues, m_request.m_style.m_showSpeaker);
    return WriteWholeFile(item.m_path, bytes, item.m_error);
}

bool CaptionExportJob::exportText(CaptionExportItemResult& item)
{
    item.m_path = m_request.m_textPath;
    if (item.m_path.empty())
    {
        item.m_error = "text path empty";
        return false;
    }
    const std::string bytes = CaptionPlainText::Write(m_request.m_cues, m_request.m_style.m_showSpeaker);
    return WriteWholeFile(item.m_path, bytes, item.m_error);
}

bool CaptionExportJob::exportTrack(CaptionExportItemResult& item)
{
    item.m_path = m_request.m_trackPath;
    if (item.m_path.empty())
    {
        item.m_error = "track path empty";
        return false;
    }
    const std::wstring temp = item.m_path + kPartSuffix;
    CaptionVideoInfo info;
    if (!ProbeVideo(m_request.m_videoPath, info, &item.m_error))
        return false;
    //先原样转封装；不行（如外来视频的编码格式写不进 MP4）改为解码再编码，画面不叠字
    std::string remuxError;
    if (!RemuxMp4(m_request.m_videoPath, temp, this, &remuxError))
    {
        if (m_cancel.load())
        {
            ::DeleteFileW(temp.c_str());
            item.m_error = "cancelled";
            return false;
        }
        LOG_WARNING("caption export: remux failed (%s), re-encoding instead.", remuxError.c_str());
        const std::vector<CaptionSpan> none;
        if (!BurnHardSub(m_request.m_videoPath, temp, none, m_request.m_style, m_request.m_pip, this, &item.m_error))
        {
            ::DeleteFileW(temp.c_str());
            return false;
        }
    }
    //文字样本：显示区间之间补空样本，首尾相接覆盖整段时长
    const std::vector<CaptionSpan> spans = BuildDisplaySpans(m_request.m_cues);
    int64_t durationMs = info.m_durationMs;
    if (!spans.empty() && spans.back().m_endMs > durationMs)
        durationMs = spans.back().m_endMs;
    const std::vector<CaptionSpan> timed = BuildTextSamples(spans, durationMs);
    std::vector<Mp4TextSample> samples;
    samples.reserve(timed.size());
    for (size_t i = 0; i < timed.size(); ++i)
    {
        Mp4TextSample s;
        s.m_durationMs = timed[i].m_endMs - timed[i].m_startMs;
        s.m_utf8 = WideToUtf8(SpanDisplayText(timed[i], m_request.m_style.m_showSpeaker));
        //超长的一条截短（tx3g 单个样本最多 65535 字节，正常字幕远到不了）
        if (s.m_utf8.size() > kMp4TextSampleMaxBytes)
            s.m_utf8.resize(kMp4TextSampleMaxBytes);
        samples.push_back(s);
    }
    if (samples.empty() || !AppendTx3gTrack(temp, samples, info.m_width, info.m_height, kTrackLanguage, &item.m_error))
    {
        if (item.m_error.empty())
            item.m_error = "no duration for text track";
        ::DeleteFileW(temp.c_str());
        return false;
    }
    return CommitPart(temp, item.m_path, item.m_error);
}

bool CaptionExportJob::exportHard(CaptionExportItemResult& item)
{
    item.m_path = m_request.m_hardPath;
    if (item.m_path.empty())
    {
        item.m_error = "hard path empty";
        return false;
    }
    const std::wstring temp = item.m_path + kPartSuffix;
    const std::vector<CaptionSpan> spans = BuildDisplaySpans(m_request.m_cues);
    if (!BurnHardSub(m_request.m_videoPath, temp, spans, m_request.m_style, m_request.m_pip, this, &item.m_error))
    {
        ::DeleteFileW(temp.c_str());
        return false;
    }
    return CommitPart(temp, item.m_path, item.m_error);
}

void CaptionExportJob::OnMediaProgress(int permille)
{
    if (m_listener == NULL)
        return;
    int overall = kPermilleFull;
    if (m_totalWeight > 0)
        overall = (m_doneWeight * kPermilleFull + m_currentWeight * permille) / m_totalWeight;
    if (overall > kPermilleFull)
        overall = kPermilleFull;
    m_listener->OnExportProgress(m_currentKind, permille, overall);
}

bool CaptionExportJob::IsMediaCancelled()
{
    return m_cancel.load();
}

} // namespace caption
