/**
 *  录音（只有音频流）的字幕支持单元测试（标签 [captionexport]），CaptionAudioOnlyTests.cpp
 *
 *  覆盖 docs/语音字幕方案.md 14.13（屏幕共享方案第 24 期只录声音、第 54 条录音也能生成字幕）：
 *    - CAPAO1 纯文本导出：带 BOM、CRLF，按开始时刻排序，每条一行「[时:分:秒] 文字」，带说话人时为「说话人：文字」，
 *             字幕里的换行换成空格，文字为空的跳过；
 *    - CAPAO2 按扩展名认出录音（.m4a，不区分大小写）；
 *    - CAPAO3 只有音频流的分段式录音可以原样转封装成普通 MP4：结果能打开、有音频、没有视频；ProbeVideo 对录音返回 false
 *             （导出硬字幕版与字幕轨版的调用方据此知道录音不能做这两种）；
 *    - CAPAO4 录音导出 SRT 与纯文本：两项都成功、文件都在，处理顺序为 SRT、纯文本；
 *    - CAPAO5 导出设置里「纯文本」默认不勾，读写往返保留；
 *    - CAPAO6 草稿里种类为纯文本的导出记录能读回。
 *  CAPAO3、CAPAO4 写临时文件（系统临时目录），用完删除。
 *  框架 Catch2 v3。
 *  balloonwj@qq.com   2026-10-06
 */
#include "stdafx.h"

#include "catch_amalgamated.hpp"

#include <windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>

#include <chrono>
#include <condition_variable>
#include <mutex>
#include <string>
#include <vector>

#include "../Export/CaptionExportJob.h"
#include "../Export/CaptionMediaIo.h"
#include "../Model/CaptionDraft.h"
#include "../Model/CaptionFiles.h"
#include "../Model/CaptionSettings.h"
#include "../../FlamingoMedia/Record/RecordMp4Writer.h"
#include "../../FlamingoMedia/Record/RecordPolicy.h"

#pragma comment(lib, "mfplat.lib")
#pragma comment(lib, "mfreadwrite.lib")
#pragma comment(lib, "mfuuid.lib")

using namespace caption;

namespace {

//测试录音的长度：50 段，每段 20 毫秒，共 1 秒
const int kAoChunks = 50;
const int kAoChunkFrames = 960;
const int64_t kAoChunkHns = 200000;
//导出等待的上限（毫秒）
const int kAoTimeoutMs = 60000;

//本用例期间初始化 COM 与 Media Foundation
class AoMfScope
{
public:
    AoMfScope()
        : m_co(::CoInitializeEx(NULL, COINIT_MULTITHREADED))
        , m_mf(::MFStartup(MF_VERSION))
    {
    }

    ~AoMfScope()
    {
        if (SUCCEEDED(m_mf))
            ::MFShutdown();
        if (SUCCEEDED(m_co))
            ::CoUninitialize();
    }

    bool Ok() const
    {
        return SUCCEEDED(m_mf);
    }

private:
    HRESULT m_co;   //CoInitializeEx 的结果
    HRESULT m_mf;   //MFStartup 的结果
};

std::wstring AoTempDir()
{
    wchar_t dir[MAX_PATH] = { 0 };
    ::GetTempPathW(MAX_PATH, dir);
    return dir;
}

bool AoFileExists(const std::wstring& path)
{
    return ::GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES;
}

std::string AoReadFile(const std::wstring& path)
{
    std::string out;
    HANDLE h = ::CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (h == INVALID_HANDLE_VALUE)
        return out;
    char buf[4096];
    DWORD got = 0;
    while (::ReadFile(h, buf, sizeof(buf), &got, NULL) && got > 0)
        out.append(buf, got);
    ::CloseHandle(h);
    return out;
}

//用录制模块的写入器生成一段 1 秒的只有音频的分段式录音
bool AoMakeRecording(const std::wstring& path)
{
    screenrecord::RecordMp4Writer writer;
    if (!writer.OpenAudioOnly(path))
        return false;
    std::vector<int16_t> stereo(static_cast<size_t>(kAoChunkFrames) * screenrecord::kRecordAudioChannels, 300);
    for (int i = 0; i < kAoChunks; ++i)
    {
        if (!writer.WriteAudio(&stereo[0], kAoChunkFrames, static_cast<int64_t>(i) * kAoChunkHns))
            return false;
    }
    return writer.Close();
}

//读回文件里有没有音频流、视频流
void AoStreams(const std::wstring& path, bool& opened, bool& hasAudio, bool& hasVideo)
{
    opened = false;
    hasAudio = false;
    hasVideo = false;
    IMFSourceReader* reader = NULL;
    if (FAILED(::MFCreateSourceReaderFromURL(path.c_str(), NULL, &reader)))
        return;
    opened = true;
    IMFMediaType* type = NULL;
    if (SUCCEEDED(reader->GetCurrentMediaType(static_cast<DWORD>(MF_SOURCE_READER_FIRST_AUDIO_STREAM), &type)))
    {
        hasAudio = true;
        type->Release();
        type = NULL;
    }
    if (SUCCEEDED(reader->GetCurrentMediaType(static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM), &type)))
    {
        hasVideo = true;
        type->Release();
    }
    reader->Release();
}

CaptionCue AoCue(int64_t startMs, int64_t endMs, const std::wstring& text, const std::wstring& speaker)
{
    CaptionCue c;
    c.m_startMs = startMs;
    c.m_endMs = endMs;
    c.m_text = text;
    c.m_speaker = speaker;
    return c;
}

//记录导出回报
class AoListener : public ICaptionExportListener
{
public:
    AoListener()
        : m_done(false)
    {
    }

    virtual void OnExportProgress(int kind, int permille, int overall) override
    {
        (void)kind;
        (void)permille;
        (void)overall;
    }

    virtual void OnExportFinished(const CaptionExportResult& result) override
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_result = result;
        m_done = true;
        m_cv.notify_all();
    }

    bool WaitDone(int timeoutMs)
    {
        std::unique_lock<std::mutex> lock(m_mutex);
        const std::chrono::steady_clock::time_point deadline =
            std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
        while (!m_done)
        {
            if (m_cv.wait_until(lock, deadline) == std::cv_status::timeout)
                break;
        }
        return m_done;
    }

    std::mutex              m_mutex;    //保护下面两项
    std::condition_variable m_cv;       //导出结束的通知
    bool                    m_done;     //是否已结束
    CaptionExportResult     m_result;   //结果
};

} // namespace

TEST_CASE("CAPAO1 plain text export", "[captionexport]")
{
    std::vector<CaptionCue> cues;
    cues.push_back(AoCue(65000, 66000, L"second line", L"Bob"));
    cues.push_back(AoCue(1500, 2500, L"first\nline", L"Alice"));
    cues.push_back(AoCue(3725000, 3726000, L"", L"Nobody"));
    cues.push_back(AoCue(3725000, 3726000, L"after an hour", L""));
    const std::string withSpeaker = CaptionPlainText::Write(cues, true);
    const std::string expected = std::string("\xEF\xBB\xBF") + "[00:00:01] Alice\xEF\xBC\x9A" "first line\r\n" +
                                 "[00:01:05] Bob\xEF\xBC\x9A" "second line\r\n" + "[01:02:05] after an hour\r\n";
    CHECK(withSpeaker == expected);
    const std::string noSpeaker = CaptionPlainText::Write(cues, false);
    CHECK(noSpeaker == std::string("\xEF\xBB\xBF") + "[00:00:01] first line\r\n[00:01:05] second line\r\n" +
                           "[01:02:05] after an hour\r\n");
    CHECK(CaptionPlainText::Write(std::vector<CaptionCue>(), true) == std::string("\xEF\xBB\xBF"));
}

TEST_CASE("CAPAO2 audio recordings are recognised by extension", "[captionexport]")
{
    CHECK(IsAudioOnlyMediaPath(L"C:\\v\\FlamingoRecord_20261006_090507.m4a"));
    CHECK(IsAudioOnlyMediaPath(L"C:\\v\\x.M4A"));
    CHECK_FALSE(IsAudioOnlyMediaPath(L"C:\\v\\x.mp4"));
    CHECK_FALSE(IsAudioOnlyMediaPath(L".m4a"));
    CHECK_FALSE(IsAudioOnlyMediaPath(L""));
}

TEST_CASE("CAPAO3 audio-only recordings can be remuxed", "[captionexport]")
{
    AoMfScope mf;
    REQUIRE(mf.Ok());
    const std::wstring src = AoTempDir() + L"capao3-src.m4a";
    const std::wstring dst = AoTempDir() + L"capao3-dst.mp4";
    ::DeleteFileW(src.c_str());
    ::DeleteFileW(dst.c_str());
    REQUIRE(AoMakeRecording(src));
    //导出硬字幕版、字幕轨版要有画面：对录音探测视频失败
    CaptionVideoInfo info;
    std::string error;
    CHECK_FALSE(ProbeVideo(src, info, &error));
    //转封装（编辑器预览用）照常可以做，结果只有音频
    error.clear();
    INFO("remux error: " << error);
    REQUIRE(RemuxMp4(src, dst, NULL, &error));
    bool opened = false;
    bool hasAudio = false;
    bool hasVideo = false;
    AoStreams(dst, opened, hasAudio, hasVideo);
    CHECK(opened);
    CHECK(hasAudio);
    CHECK_FALSE(hasVideo);
    ::DeleteFileW(src.c_str());
    ::DeleteFileW(dst.c_str());
}

TEST_CASE("CAPAO4 audio recordings export SRT and plain text", "[captionexport]")
{
    AoMfScope mf;
    REQUIRE(mf.Ok());
    const std::wstring dir = AoTempDir();
    const std::wstring src = dir + L"capao4-src.m4a";
    ::DeleteFileW(src.c_str());
    REQUIRE(AoMakeRecording(src));
    CaptionExportRequest req;
    req.m_videoPath = src;
    req.m_cues.push_back(AoCue(200, 900, L"hello", L"Alice"));
    req.m_srt = true;
    req.m_text = true;
    req.m_srtPath = dir + L"capao4.srt";
    req.m_textPath = dir + L"capao4.txt";
    AoListener listener;
    CaptionExportJob job;
    REQUIRE(job.Start(req, &listener));
    REQUIRE(listener.WaitDone(kAoTimeoutMs));
    job.Wait();
    CHECK_FALSE(listener.m_result.m_cancelled);
    REQUIRE(listener.m_result.m_items.size() == 2);
    CHECK(listener.m_result.m_items[0].m_kind == caption_export_srt);
    CHECK(listener.m_result.m_items[1].m_kind == caption_export_text);
    for (size_t i = 0; i < listener.m_result.m_items.size(); ++i)
    {
        INFO("kind " << listener.m_result.m_items[i].m_kind << " error " << listener.m_result.m_items[i].m_error);
        CHECK(listener.m_result.m_items[i].m_ok);
        CHECK(AoFileExists(listener.m_result.m_items[i].m_path));
    }
    const std::string text = AoReadFile(req.m_textPath);
    CHECK(text.find("[00:00:00] ") != std::string::npos);
    CHECK(text.find("hello") != std::string::npos);
    //只要了纯文本时也能单独导出
    CaptionExportRequest onlyText = req;
    onlyText.m_srt = false;
    AoListener second;
    CaptionExportJob job2;
    REQUIRE(job2.Start(onlyText, &second));
    REQUIRE(second.WaitDone(kAoTimeoutMs));
    job2.Wait();
    REQUIRE(second.m_result.m_items.size() == 1);
    CHECK(second.m_result.m_items[0].m_kind == caption_export_text);
    ::DeleteFileW(src.c_str());
    ::DeleteFileW(req.m_srtPath.c_str());
    ::DeleteFileW(req.m_textPath.c_str());
}

TEST_CASE("CAPAO5 plain text export setting", "[captionexport]")
{
    CaptionSettings s;
    CHECK(s.FromJson(""));
    CHECK_FALSE(s.m_exportText);
    s.m_exportText = true;
    CaptionSettings back;
    REQUIRE(back.FromJson(s.ToJson()));
    CHECK(back.m_exportText);
    CHECK(back.m_exportHard == s.m_exportHard);
}

TEST_CASE("CAPAO6 drafts keep plain text export records", "[captionexport]")
{
    CaptionDraft draft;
    draft.m_videoName = L"a.m4a";
    CaptionExport e;
    e.m_name = L"a.txt";
    e.m_kind = caption_export_text;
    draft.m_exports.push_back(e);
    CaptionDraft back;
    std::string error;
    REQUIRE(CaptionDraftIo::FromJson(CaptionDraftIo::ToJson(draft), back, &error));
    REQUIRE(back.m_exports.size() == 1);
    CHECK(back.m_exports[0].m_kind == caption_export_text);
    CHECK(back.m_exports[0].m_name == L"a.txt");
}
