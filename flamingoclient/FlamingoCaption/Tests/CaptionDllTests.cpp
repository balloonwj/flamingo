/**
 *  CaptionDllTests.cpp
 *
 *  FlamingoCaption.dll 的集成测试（Suite CAPDLL，标签 [captiondll]）：真正加载同配置的 FlamingoCaption.dll 与
 *  FlamingoCaptionModel.dll，用一段约 10 秒的中英混说录音（Tests/data/caption_test_monday.wav）验证：
 *    CAPDLL1 —— 工厂函数的版本检查、Init 的参数检查；
 *    CAPDLL2 —— 模型加载（引擎状态变为可用）；
 *    CAPDLL3 —— 识别流：中间结果、定稿结果的文字与时间轴（推入时的时间戳从 5000 毫秒起）；
 *    CAPDLL4 —— 录制草稿：边推入麦克风声音边识别，Finish 后后台写出带字幕的草稿；
 *    CAPDLL5 —— 文件状态、录像改名与删除时草稿跟着处理；
 *    CAPDLL6 —— 观看窗叠加的字幕绘制；
 *    CAPDLL7 —— 开关类偏好：默认值、修改后经宿主保存、值不变时不保存、不认识的偏好；
 *    CAPDLL8 —— 字幕编辑器窗口的打开、重复打开（激活原窗口）与模块关闭时保存草稿（隐藏用例，会弹出窗口，
 *               须单独运行 "[captioneditor-wnd]"；窗口立即挪到屏幕外）。
 *
 *  DLL 的位置：测试程序在 <配置目录>\Tests\（Win32 为 Debug_Win32\Tests\，x64 为 Debug\Tests\），字幕 DLL 在它的
 *  上一级，即同配置的 exe 输出目录；可用环境变量 FLAMINGO_CAPTION_DIR 指定。DLL 不存在、或编译时没有识别引擎（静态库未准备）时，
 *  需要识别的用例跳过（WARN），其余照常。模块在本进程里只加载一次，进程退出前统一关闭。
 *
 *  balloonwj@qq.com   2026-10-06
 */

#include "stdafx.h"

#include "catch_amalgamated.hpp"

#include <windows.h>
#include <stdio.h>
#include <mutex>
#include <string>
#include <vector>

#include "../CaptionApi.h"
#include "../Model/CaptionDraft.h"
#include "../Model/CaptionText.h"

using namespace caption;

namespace {

//等模型加载、等识别结果的最长时间（毫秒）
const DWORD kWaitReadyMs = 60000;
const DWORD kWaitResultMs = 60000;
//轮询间隔（毫秒）
const DWORD kPollMs = 50;
//推入声音的块长：0.1 秒（48 kHz 下 4800 个采样）
const size_t kChunkSamples = 4800;
//识别流测试里第一个采样的时刻（毫秒）
const int64_t kStreamBaseMs = 5000;
//推完录音后补的静音（毫秒），让端点检测给出定稿
const int kTrailingSilenceMs = 2000;
//48 kHz 与 16 kHz 的倍数
const int kUpsample = 3;
//推入声音的采样率（Hz）
const int64_t kRate48k = 48000;
//WAV 文件头里 data 块之前固定部分的字节数（RIFF 头 12 字节）
const size_t kRiffHeaderBytes = 12;

//测试用的宿主：记下日志与保存的设置
class TestHost : public ICaptionHost
{
public:
    virtual void Log(int level, const char* text) override
    {
        (void)level;
        m_logs.push_back(text != NULL ? text : "");
    }
    virtual const wchar_t* Translate(const wchar_t* context, const wchar_t* source) override
    {
        (void)context;
        return source;
    }
    virtual bool GetUiLanguage(wchar_t* langBuf, uint32_t langChars, wchar_t* fontBuf, uint32_t fontChars) override
    {
        wcsncpy_s(langBuf, langChars, L"zh-CN", _TRUNCATE);
        wcsncpy_s(fontBuf, fontChars, L"Microsoft YaHei", _TRUNCATE);
        return true;
    }
    virtual void SaveSettings(const char* settingsJson) override
    {
        m_saved = settingsJson != NULL ? settingsJson : "";
    }
    virtual bool PickConversationAndSendFile(const wchar_t* path) override
    {
        (void)path;
        return false;
    }
    virtual HWND GetMainWindow() override
    {
        return NULL;
    }

    std::vector<std::string> m_logs;   //收到的日志
    std::string              m_saved;  //最后一次保存的设置
};

//识别流测试的回调：记下中间结果与定稿结果
class TestListener : public ICaptionStreamListener
{
public:
    TestListener()
        : m_partials(0)
    {
        InitializeCriticalSection(&m_lock);
    }
    ~TestListener()
    {
        DeleteCriticalSection(&m_lock);
    }
    virtual void OnCaptionPartial(const wchar_t* text) override
    {
        (void)text;
        EnterCriticalSection(&m_lock);
        ++m_partials;
        LeaveCriticalSection(&m_lock);
    }
    virtual void OnCaptionFinal(const wchar_t* text, int64_t startMs, int64_t endMs) override
    {
        EnterCriticalSection(&m_lock);
        m_text += text;
        m_starts.push_back(startMs);
        m_ends.push_back(endMs);
        LeaveCriticalSection(&m_lock);
    }
    std::wstring Text()
    {
        EnterCriticalSection(&m_lock);
        std::wstring t = m_text;
        LeaveCriticalSection(&m_lock);
        return t;
    }

    CRITICAL_SECTION     m_lock;       //保护下面几项（识别线程写、测试线程读）
    int                  m_partials;   //中间结果的次数
    std::wstring         m_text;       //全部定稿文字
    std::vector<int64_t> m_starts;     //各句的开始时刻
    std::vector<int64_t> m_ends;       //各句的结束时刻
};

//本进程里唯一的一份 DLL 与模块：第一次用到时加载，进程退出时关闭
struct ModuleHolder
{
    ModuleHolder()
        : m_dll(NULL)
        , m_create(NULL)
        , m_module(NULL)
        , m_inited(false)
    {
    }
    ~ModuleHolder()
    {
        if (m_module != NULL && m_inited)
            m_module->Shutdown();
        if (m_dll != NULL)
            FreeLibrary(m_dll);
    }

    HMODULE                 m_dll;      //FlamingoCaption.dll
    FlamingoCaptionCreateFn m_create;   //工厂函数
    ICaptionModule*         m_module;   //模块（已 Init 时非空）
    bool                    m_inited;   //是否已 Init
    TestHost                m_host;     //宿主
    std::wstring            m_dir;      //DLL 所在目录
};

ModuleHolder g_holder;

std::wstring ExeDir()
{
    wchar_t buf[MAX_PATH] = { 0 };
    GetModuleFileNameW(NULL, buf, MAX_PATH);
    std::wstring p = buf;
    return p.substr(0, p.find_last_of(L'\\'));
}

std::wstring CaptionDllDir()
{
    wchar_t env[MAX_PATH] = { 0 };
    if (GetEnvironmentVariableW(L"FLAMINGO_CAPTION_DIR", env, MAX_PATH) > 0)
        return env;
    return ExeDir() + L"\\..";
}

//只加载 DLL、取工厂函数（不 Init）；成功返回 true
bool LoadDllOnly()
{
    if (g_holder.m_create != NULL)
        return true;
    if (g_holder.m_dll == NULL)
    {
        g_holder.m_dir = CaptionDllDir();
        g_holder.m_dll = LoadLibraryW((g_holder.m_dir + L"\\" + FLAMINGO_CAPTION_DLL_NAME).c_str());
        if (g_holder.m_dll == NULL)
            return false;
    }
    g_holder.m_create = reinterpret_cast<FlamingoCaptionCreateFn>(
        GetProcAddress(g_holder.m_dll, FLAMINGO_CAPTION_FACTORY_NAME));
    return g_holder.m_create != NULL;
}

//加载 DLL 并 Init；DLL 不存在时返回 NULL
ICaptionModule* Module()
{
    if (g_holder.m_inited)
        return g_holder.m_module;
    if (!LoadDllOnly())
        return NULL;
    ICaptionModule* m = g_holder.m_create(kApiVersion);
    if (m == NULL)
        return NULL;
    CaptionInitParams params;
    params.m_cbSize = sizeof(params);
    params.m_appDir = g_holder.m_dir.c_str();
    if (!m->Init(&g_holder.m_host, &params))
        return NULL;
    g_holder.m_module = m;
    g_holder.m_inited = true;
    return m;
}

//读 16 kHz 单声道 16 位 WAV，升采样到 48 kHz（线性插值）
std::vector<int16_t> LoadTestWav48k()
{
    std::string path = __FILE__;
    path = path.substr(0, path.find_last_of("\\/")) + "\\data\\caption_test_monday.wav";
    std::vector<int16_t> out;
    FILE* f = NULL;
    if (fopen_s(&f, path.c_str(), "rb") != 0 || f == NULL)
        return out;
    std::vector<char> bytes;
    char buf[4096];
    size_t n = 0;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0)
        bytes.insert(bytes.end(), buf, buf + n);
    fclose(f);
    //找 data 块
    size_t pos = kRiffHeaderBytes;
    while (pos + 8 <= bytes.size())
    {
        const uint32_t size = *reinterpret_cast<const uint32_t*>(&bytes[pos + 4]);
        if (memcmp(&bytes[pos], "data", 4) == 0)
        {
            const int16_t* s = reinterpret_cast<const int16_t*>(&bytes[pos + 8]);
            const size_t count = size / sizeof(int16_t);
            for (size_t i = 0; i < count; ++i)
            {
                const int next = (i + 1 < count) ? s[i + 1] : s[i];
                for (int k = 0; k < kUpsample; ++k)
                    out.push_back(static_cast<int16_t>(s[i] + (next - s[i]) * k / kUpsample));
            }
            break;
        }
        pos += 8 + size;
    }
    return out;
}

//等引擎可用；不可用（没编进识别引擎、缺模型）时返回 false
bool WaitEngineReady(ICaptionModule* m)
{
    m->Preload();
    const ULONGLONG t0 = GetTickCount64();
    while (GetTickCount64() - t0 < kWaitReadyMs)
    {
        const int st = m->GetEngineState();
        if (st == caption_engine_ready)
            return true;
        if (st == caption_engine_failed)
            return false;
        Sleep(kPollMs);
    }
    return false;
}

std::wstring TempPath(const wchar_t* name)
{
    wchar_t dir[MAX_PATH] = { 0 };
    GetTempPathW(MAX_PATH, dir);
    return std::wstring(dir) + name;
}

} // namespace

TEST_CASE("CAPDLL1 工厂函数拒绝不同的接口版本；Init 拒绝不对的参数；已初始化后再次 Init 直接返回成功", "[captiondll]")
{
    if (!LoadDllOnly())
    {
        WARN("FlamingoCaption.dll not found, skipped");
        return;
    }
    CHECK(g_holder.m_create(kApiVersion + 1) == NULL);
    ICaptionModule* raw = g_holder.m_create(kApiVersion);
    REQUIRE(raw != NULL);
    CHECK(raw->GetApiVersion() == kApiVersion);
    CaptionInitParams bad;
    bad.m_cbSize = sizeof(bad) + 1;
    bad.m_appDir = L"x";
    //本用例先于其他用例运行时模块尚未初始化，可以检查参数不对时 Init 失败
    if (!g_holder.m_inited)
    {
        CHECK_FALSE(raw->Init(&g_holder.m_host, &bad));
        CHECK_FALSE(raw->Init(NULL, NULL));
        bad.m_cbSize = sizeof(bad);
        bad.m_appDir = NULL;
        CHECK_FALSE(raw->Init(&g_holder.m_host, &bad));
    }
    ICaptionModule* m = Module();
    REQUIRE(m == raw);
    CHECK(m->Init(&g_holder.m_host, &bad));
}

TEST_CASE("CAPDLL2 模型加载：引擎状态变为可用", "[captiondll]")
{
    ICaptionModule* m = Module();
    if (m == NULL)
    {
        WARN("FlamingoCaption.dll not found, skipped");
        return;
    }
    const bool ready = WaitEngineReady(m);
    if (!ready && m->GetEngineState() == caption_engine_failed)
    {
        WARN("caption engine not available (built without sherpa-onnx or model missing), skipped");
        return;
    }
    REQUIRE(ready);
    bool logged = false;
    for (size_t i = 0; i < g_holder.m_host.m_logs.size(); ++i)
    {
        if (g_holder.m_host.m_logs[i].find("recognizer ready") != std::string::npos)
            logged = true;
    }
    CHECK(logged);
}

TEST_CASE("CAPDLL3 识别流：有中间结果；定稿文字含 Monday 与 tomorrow；时间落在推入的时间轴上", "[captiondll]")
{
    ICaptionModule* m = Module();
    if (m == NULL || !WaitEngineReady(m))
    {
        WARN("caption engine not available, skipped");
        return;
    }
    const std::vector<int16_t> pcm = LoadTestWav48k();
    REQUIRE(pcm.size() > kChunkSamples);
    TestListener listener;
    ICaptionStream* s = m->CreateStream(caption_purpose_live, &listener);
    REQUIRE(s != NULL);
    int64_t pts = kStreamBaseMs;
    for (size_t i = 0; i < pcm.size(); i += kChunkSamples)
    {
        const size_t n = (pcm.size() - i < kChunkSamples) ? pcm.size() - i : kChunkSamples;
        s->PushPcm(&pcm[i], static_cast<uint32_t>(n), pts);
        pts += static_cast<int64_t>(n) * 1000 / kRate48k;
    }
    const std::vector<int16_t> silence(kChunkSamples, 0);
    for (int ms = 0; ms < kTrailingSilenceMs; ms += 100)
    {
        s->PushPcm(silence.data(), static_cast<uint32_t>(silence.size()), pts);
        pts += 100;
    }
    const ULONGLONG t0 = GetTickCount64();
    while (GetTickCount64() - t0 < kWaitResultMs && listener.Text().find(L"星期三") == std::wstring::npos)
        Sleep(kPollMs);
    s->Close();
    const std::wstring text = listener.Text();
    CHECK(text.find(L"Monday") != std::wstring::npos);
    CHECK(text.find(L"tomorrow") != std::wstring::npos);
    CHECK(text.find(L"星期三") != std::wstring::npos);
    CHECK(listener.m_partials > 0);
    REQUIRE_FALSE(listener.m_starts.empty());
    const int64_t audioEndMs = kStreamBaseMs + static_cast<int64_t>(pcm.size()) * 1000 / kRate48k;
    for (size_t i = 0; i < listener.m_starts.size(); ++i)
    {
        CHECK(listener.m_starts[i] >= kStreamBaseMs);
        CHECK(listener.m_ends[i] > listener.m_starts[i]);
        CHECK(listener.m_ends[i] <= audioEndMs + kTrailingSilenceMs);
    }
    //第一句从开头 2 秒内开始
    CHECK(listener.m_starts[0] < kStreamBaseMs + 2000);
}

namespace {

//录制草稿的实时结果观察者（CAPDLL10）：记下每次回调的来源、是否定稿与文字
class LiveObserver : public ICaptionRecordingObserver
{
public:
    struct Event
    {
        int          m_source;   //哪一路
        bool         m_final;    //是否定稿
        std::wstring m_text;     //文字
    };

    virtual void OnRecordingPartial(int source, const wchar_t* text) override
    {
        add(source, false, text);
    }

    virtual void OnRecordingFinal(int source, const wchar_t* text) override
    {
        add(source, true, text);
    }

    std::vector<Event> Events()
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_events;
    }

private:
    void add(int source, bool final, const wchar_t* text)
    {
        Event e;
        e.m_source = source;
        e.m_final = final;
        e.m_text = text != NULL ? text : L"";
        std::lock_guard<std::mutex> lock(m_mutex);
        m_events.push_back(e);
    }

    std::mutex         m_mutex;    //保护 m_events（识别线程写，测试线程读）
    std::vector<Event> m_events;   //收到的回调，按先后
};

//等实时结果不再增加：连续这么久（毫秒）没有新回调就认为识别线程已处理完推入的声音
const DWORD kLiveQuietMs = 1500;

//等到观察者连续 kLiveQuietMs 没有新回调，或总共等了 kWaitResultMs
void WaitLiveQuiet(LiveObserver& obs)
{
    const ULONGLONG t0 = GetTickCount64();
    size_t last = obs.Events().size();
    ULONGLONG lastChange = GetTickCount64();
    while (GetTickCount64() - t0 < kWaitResultMs)
    {
        Sleep(kPollMs);
        const size_t now = obs.Events().size();
        if (now != last)
        {
            last = now;
            lastChange = GetTickCount64();
        }
        else if (GetTickCount64() - lastChange >= kLiveQuietMs)
        {
            return;
        }
    }
}

} // namespace

TEST_CASE("CAPDLL10 录制草稿的实时结果：观察者收到麦克风一路的中间结果与定稿，补静音后这句定稿；取消登记后不再通知",
          "[captiondll]")
{
    ICaptionModule* m = Module();
    if (m == NULL || !WaitEngineReady(m))
    {
        WARN("caption engine not available, skipped");
        return;
    }
    const std::wstring video = TempPath(L"caption_capdll10.mp4");
    const std::wstring draftPath = CaptionDraftIo::DraftPathFor(video);
    DeleteFileW(draftPath.c_str());
    const std::vector<int16_t> pcm = LoadTestWav48k();
    REQUIRE(!pcm.empty());

    CaptionRecordingParams params;
    params.m_cbSize = sizeof(params);
    params.m_videoPath = video.c_str();
    params.m_selfName = L"张三";
    params.m_mic = true;
    params.m_system = false;
    ICaptionRecording* r = m->BeginRecording(&params);
    REQUIRE(r != NULL);
    LiveObserver obs;
    r->SetObserver(&obs);
    //录音之后补静音（录制时关闭字幕期间送的就是静音）：说到一半的这句应当靠端点检测定稿
    int64_t pts = 0;
    for (size_t i = 0; i < pcm.size(); i += kChunkSamples)
    {
        const size_t n = (pcm.size() - i < kChunkSamples) ? pcm.size() - i : kChunkSamples;
        r->PushMic(&pcm[i], static_cast<uint32_t>(n), pts);
        pts += static_cast<int64_t>(n) * 1000 / kRate48k;
    }
    const std::vector<int16_t> silence(kChunkSamples, 0);
    const int64_t kChunkMs = static_cast<int64_t>(kChunkSamples) * 1000 / kRate48k;
    for (int ms = 0; ms < kTrailingSilenceMs; ms += static_cast<int>(kChunkMs))
    {
        r->PushMic(silence.data(), static_cast<uint32_t>(silence.size()), pts);
        pts += kChunkMs;
    }
    WaitLiveQuiet(obs);
    const std::vector<LiveObserver::Event> events = obs.Events();
    REQUIRE_FALSE(events.empty());
    std::wstring finals;
    int partials = 0;
    for (size_t i = 0; i < events.size(); ++i)
    {
        CHECK(events[i].m_source == caption_source_mic);
        if (events[i].m_final)
            finals += events[i].m_text;
        else
            ++partials;
    }
    CHECK(partials > 0);
    CHECK(finals.find(L"Monday") != std::wstring::npos);
    CHECK(finals.find(L"星期三") != std::wstring::npos);
    //最后一次回调是定稿：补的静音没有让新的文字出现
    CHECK(events.back().m_final);

    //取消登记后再推一遍录音：不再收到任何回调
    r->SetObserver(NULL);
    const size_t before = obs.Events().size();
    for (size_t i = 0; i < pcm.size(); i += kChunkSamples)
    {
        const size_t n = (pcm.size() - i < kChunkSamples) ? pcm.size() - i : kChunkSamples;
        r->PushMic(&pcm[i], static_cast<uint32_t>(n), pts);
        pts += static_cast<int64_t>(n) * 1000 / kRate48k;
    }
    Sleep(kLiveQuietMs);
    CHECK(obs.Events().size() == before);
    r->Cancel();
    DeleteFileW(draftPath.c_str());
}

TEST_CASE("CAPDLL4 录制草稿：边推入边识别，Finish 后在后台写出带字幕的草稿", "[captiondll]")
{
    ICaptionModule* m = Module();
    if (m == NULL || !WaitEngineReady(m))
    {
        WARN("caption engine not available, skipped");
        return;
    }
    const std::wstring video = TempPath(L"caption_capdll4.mp4");
    const std::wstring draftPath = CaptionDraftIo::DraftPathFor(video);
    DeleteFileW(draftPath.c_str());
    const std::vector<int16_t> pcm = LoadTestWav48k();
    REQUIRE(!pcm.empty());

    CaptionRecordingParams params;
    params.m_cbSize = sizeof(params);
    params.m_videoPath = video.c_str();
    params.m_selfName = L"张三";
    params.m_mic = true;
    params.m_system = false;
    ICaptionRecording* r = m->BeginRecording(&params);
    REQUIRE(r != NULL);
    int64_t pts = 0;
    for (size_t i = 0; i < pcm.size(); i += kChunkSamples)
    {
        const size_t n = (pcm.size() - i < kChunkSamples) ? pcm.size() - i : kChunkSamples;
        r->PushMic(&pcm[i], static_cast<uint32_t>(n), pts);
        pts += static_cast<int64_t>(n) * 1000 / kRate48k;
    }
    r->SetPip(true, 0.75, 0.7, 0.98, 0.97);
    r->Finish(pts);

    //草稿在后台写：等到它带上时长并有字幕
    CaptionDraft d;
    bool ok = false;
    const ULONGLONG t0 = GetTickCount64();
    while (GetTickCount64() - t0 < kWaitResultMs)
    {
        if (CaptionDraftIo::LoadFile(draftPath, d, NULL) && d.m_videoDurationMs == pts && !d.m_cues.empty())
        {
            ok = true;
            break;
        }
        Sleep(kPollMs);
    }
    REQUIRE(ok);
    CHECK(d.m_videoName == L"caption_capdll4.mp4");
    CHECK(d.m_pip.m_present);
    std::wstring all;
    for (size_t i = 0; i < d.m_cues.size(); ++i)
    {
        all += d.m_cues[i].m_text;
        CHECK(d.m_cues[i].m_speaker == L"张三");
        CHECK(d.m_cues[i].m_source == 0);
        CHECK(d.m_cues[i].m_startMs >= 0);
        CHECK(d.m_cues[i].m_endMs <= pts + kTrailingSilenceMs);
    }
    CHECK(all.find(L"Monday") != std::wstring::npos);
    CHECK((m->GetFileStatus(video.c_str()) & caption_file_draft) != 0);
    DeleteFileW(draftPath.c_str());
}

TEST_CASE("CAPDLL5 文件状态；录像改名时草稿跟着改名，删除录像时删除草稿", "[captiondll]")
{
    ICaptionModule* m = Module();
    if (m == NULL)
    {
        WARN("FlamingoCaption.dll not found, skipped");
        return;
    }
    const std::wstring a = TempPath(L"caption_capdll5_a.mp4");
    const std::wstring b = TempPath(L"caption_capdll5_b.mp4");
    DeleteFileW(CaptionDraftIo::DraftPathFor(a).c_str());
    DeleteFileW(CaptionDraftIo::DraftPathFor(b).c_str());
    CHECK(m->GetFileStatus(a.c_str()) == caption_file_none);
    CHECK(m->GetFileStatus(NULL) == caption_file_none);
    CaptionDraft d;
    REQUIRE(CaptionDraftIo::SaveFile(CaptionDraftIo::DraftPathFor(a), d));
    CHECK(m->GetFileStatus(a.c_str()) == caption_file_draft);
    m->OnVideoRenamed(a.c_str(), b.c_str());
    CHECK(m->GetFileStatus(a.c_str()) == caption_file_none);
    CHECK(m->GetFileStatus(b.c_str()) == caption_file_draft);
    m->OnVideoDeleted(b.c_str());
    CHECK(m->GetFileStatus(b.c_str()) == caption_file_none);
}

TEST_CASE("CAPDLL6 叠加字幕的绘制：底部有不透明像素、顶部全透明；参数不对时返回 false", "[captiondll]")
{
    ICaptionModule* m = Module();
    if (m == NULL)
    {
        WARN("FlamingoCaption.dll not found, skipped");
        return;
    }
    const uint32_t w = 640;
    const uint32_t h = 120;
    std::vector<uint32_t> px(static_cast<size_t>(w) * h, 0xFFFFFFFFu);
    CaptionOverlayLine line;
    line.m_speaker = L"张三";
    line.m_text = L"字幕绘制测试 Caption";
    line.m_partial = false;
    REQUIRE(m->RenderOverlay(&line, 1, w, h, px.data(), 28));
    size_t opaqueBottom = 0;
    size_t opaqueTop = 0;
    for (uint32_t y = 0; y < h; ++y)
    {
        for (uint32_t x = 0; x < w; ++x)
        {
            const uint32_t a = px[static_cast<size_t>(y) * w + x] >> 24;
            if (a == 0)
                continue;
            if (y < h / 3)
                ++opaqueTop;
            else
                ++opaqueBottom;
        }
    }
    CHECK(opaqueTop == 0);
    CHECK(opaqueBottom > 100);
    CHECK_FALSE(m->RenderOverlay(&line, 1, 0, h, px.data(), 28));
    CHECK_FALSE(m->RenderOverlay(NULL, 1, w, h, px.data(), 28));
    CHECK_FALSE(m->RenderOverlay(&line, 1, w, h, NULL, 28));
}

TEST_CASE("CAPDLL7 开关类偏好：默认值、修改后经宿主保存、值不变时不保存", "[captiondll]")
{
    ICaptionModule* m = Module();
    if (m == NULL)
    {
        WARN("FlamingoCaption.dll not found, skipped");
        return;
    }
    m->SetSettings("");
    //默认值（第 2 节 D2、D3）：生成字幕勾、电脑声音不勾、为我的发言生成字幕关、显示别人的字幕开
    CHECK(m->GetPreference(caption_pref_record_caption));
    CHECK_FALSE(m->GetPreference(caption_pref_record_system));
    CHECK_FALSE(m->GetPreference(caption_pref_live_self));
    CHECK(m->GetPreference(caption_pref_live_show));
    CHECK_FALSE(m->GetPreference(99));
    g_holder.m_host.m_saved.clear();
    m->SetPreference(caption_pref_record_system, true);
    CHECK(m->GetPreference(caption_pref_record_system));
    CHECK(g_holder.m_host.m_saved.find("\"system\"") != std::string::npos);
    //值不变：不再保存
    g_holder.m_host.m_saved.clear();
    m->SetPreference(caption_pref_record_system, true);
    CHECK(g_holder.m_host.m_saved.empty());
    m->SetPreference(99, true);
    CHECK(g_holder.m_host.m_saved.empty());
    //保存的设置重新推入后仍然生效
    m->SetPreference(caption_pref_live_self, true);
    const std::string saved = g_holder.m_host.m_saved;
    m->SetSettings("");
    CHECK_FALSE(m->GetPreference(caption_pref_live_self));
    m->SetSettings(saved.c_str());
    CHECK(m->GetPreference(caption_pref_live_self));
    m->SetSettings("");
}

namespace {

//找本线程里标题含 needle 的顶层窗口
struct EditorFind
{
    std::wstring m_needle;   //要找的标题片段
    HWND         m_found;    //找到的窗口
};

BOOL CALLBACK FindEditorProc(HWND hwnd, LPARAM lParam)
{
    EditorFind* f = reinterpret_cast<EditorFind*>(lParam);
    wchar_t title[512] = { 0 };
    GetWindowTextW(hwnd, title, 511);
    if (std::wstring(title).find(f->m_needle) != std::wstring::npos)
    {
        f->m_found = hwnd;
        return FALSE;
    }
    return TRUE;
}

HWND FindEditorWindow(const std::wstring& needle)
{
    EditorFind f;
    f.m_needle = needle;
    f.m_found = NULL;
    EnumThreadWindows(GetCurrentThreadId(), &FindEditorProc, reinterpret_cast<LPARAM>(&f));
    return f.m_found;
}

void PumpFor(DWORD ms)
{
    const DWORD start = GetTickCount();
    while (GetTickCount() - start < ms)
    {
        MSG msg;
        while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE))
        {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        Sleep(10);
    }
}

} // namespace

TEST_CASE("CAPDLL9 录制草稿：两路都不识别时只收外部字幕，Finish 时立即写出最终草稿", "[captiondll]")
{
    ICaptionModule* m = Module();
    if (m == NULL)
    {
        WARN("FlamingoCaption.dll not found, skipped");
        return;
    }
    const std::wstring video = TempPath(L"caption_capdll9.mp4");
    const std::wstring draftPath = CaptionDraftIo::DraftPathFor(video);
    DeleteFileW(draftPath.c_str());
    //录像时长与两条字幕的时刻（毫秒）
    const int64_t kDurationMs = 9000;
    const int64_t kFirstStartMs = 1000;
    const int64_t kFirstEndMs = 2500;
    const int64_t kSecondStartMs = 4000;
    const int64_t kSecondEndMs = 6000;

    CaptionRecordingParams params;
    params.m_cbSize = sizeof(params);
    params.m_videoPath = video.c_str();
    params.m_selfName = L"张三";
    params.m_mic = false;
    params.m_system = false;
    ICaptionRecording* r = m->BeginRecording(&params);
    REQUIRE(r != NULL);
    r->AddRemoteCaption(L"李四", L"大家好", kFirstStartMs, kFirstEndMs);
    r->AddRemoteCaption(L"张三", L"开始开会", kSecondStartMs, kSecondEndMs);
    r->Finish(kDurationMs);

    //没有识别流时 Finish 在调用线程上写完最终草稿
    CaptionDraft d;
    REQUIRE(CaptionDraftIo::LoadFile(draftPath, d, NULL));
    CHECK(d.m_videoDurationMs == kDurationMs);
    REQUIRE(d.m_cues.size() == 2);
    CHECK(d.m_cues[0].m_speaker == L"李四");
    CHECK(d.m_cues[0].m_startMs == kFirstStartMs);
    CHECK(d.m_cues[1].m_speaker == L"张三");
    CHECK(d.m_cues[1].m_endMs == kSecondEndMs);
    DeleteFileW(draftPath.c_str());
}

TEST_CASE("CAPDLL8 字幕编辑器窗口：打开、重复打开激活原窗口、模块关闭时保存草稿", "[.][captioneditor-wnd]")
{
    ICaptionModule* m = Module();
    if (m == NULL)
    {
        WARN("FlamingoCaption.dll not found, skipped");
        return;
    }
    //用测试数据里的录音所在目录放一个假的录像文件：编辑器要求文件存在，预览打不开时照样可以编辑
    const std::wstring video = TempPath(L"caption_capdll8.mp4");
    HANDLE f = CreateFileW(video.c_str(), GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
    REQUIRE(f != INVALID_HANDLE_VALUE);
    CloseHandle(f);
    CaptionDraft d;
    CaptionCue c;
    c.m_id = 1;
    c.m_startMs = 0;
    c.m_endMs = 1500;
    c.m_text = L"\u7b2c\u4e00\u53e5";
    d.m_cues.push_back(c);
    d.m_nextId = 2;
    REQUIRE(CaptionDraftIo::SaveFile(CaptionDraftIo::DraftPathFor(video), d));
    CHECK_FALSE(m->OpenEditor(NULL, L"C:\\no\\such\\file.mp4"));
    REQUIRE(m->OpenEditor(NULL, video.c_str()));
    HWND wnd = FindEditorWindow(L"caption_capdll8.mp4");
    REQUIRE(wnd != NULL);
    //挪到屏幕外，不打扰桌面
    const int kOffscreen = -32000;
    SetWindowPos(wnd, NULL, kOffscreen, kOffscreen, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
    PumpFor(1500);
    //重复打开：不另开窗口
    REQUIRE(m->OpenEditor(NULL, video.c_str()));
    CHECK(FindEditorWindow(L"caption_capdll8.mp4") == wnd);
    //关窗：草稿仍在、内容不变
    SendMessageW(wnd, WM_CLOSE, 0, 0);
    PumpFor(300);
    CHECK_FALSE(IsWindow(wnd));
    CaptionDraft back;
    REQUIRE(CaptionDraftIo::LoadFile(CaptionDraftIo::DraftPathFor(video), back, NULL));
    REQUIRE(back.m_cues.size() == 1);
    CHECK(back.m_cues[0].m_text == L"\u7b2c\u4e00\u53e5");
    DeleteFileW(CaptionDraftIo::DraftPathFor(video).c_str());
    DeleteFileW(video.c_str());
}
