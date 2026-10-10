/**
 *  图片文字识别 exe 侧（Feature/Ocr）的单元测试，用例 OCRH1 ~ OCRH8（docs/图片文字识别方案.md 第 10.2 节）。
 *
 *    - OCRH1 预加载完成之前状态为「加载中」，入口不显示；完成后入口显示
 *    - OCRH2 DLL 不存在时状态为「不可用」，入口不显示；之后不再重试加载
 *    - OCRH3 加载一个不是本模块的系统 DLL（version.dll），状态为「不可用」
 *    - OCRH4 函数表 m_cbSize 小于最小值时状态为「不可用」，不调用 m_init
 *    - OCRH5 引擎报告系统不支持、没有识别语言时入口照常显示，点击后给出对应提示；只有后者询问打开系统设置
 *    - OCRH6 预加载只执行一次，且在后台线程上执行
 *    - OCRH7 识别结果被完整拷入 exe 的结构，m_freeResult 恰好调用一次；图片文件解码与按旋转方向转正；
 *            位图选区转 BGRA；没有引擎、已取消、解码失败三种情况各有结果码
 *    - OCRH8 结果窗口已关闭时结果被释放；识别线程按顺序执行，排队中的任务可以取消
 *
 *  用一张假函数表代替 FlamingoOcr.dll，不依赖系统的识别组件。框架 Catch2 v3，标签 [ocrhost]。
 *  balloonwj@qq.com   2026-10-05
 */
#include "stdafx.h"
#include "catch_amalgamated.hpp"

#include <windows.h>
#include <gdiplus.h>
#include <string>
#include <vector>

#include "../Feature/Ocr/OcrModuleHost.h"
#include "../Feature/Ocr/OcrPrompt.h"
#include "../Feature/Ocr/OcrRecognizer.h"

using namespace ocr;
using namespace flamingoocr;

namespace {

//等待后台线程的最长时间（毫秒）
const DWORD kWaitMs = 5000;

//测试窗口接收结果用的消息号
const UINT kTestResultMsg = WM_APP + 0x1F0;

//每像素字节数（32 位 BGRA）
const uint32_t kBytesPerPixel = 4;

//假函数表的可配置状态与调用记录
struct FakeOcrState
{
    int            m_availability;    //m_queryAvailability 报告的可用性
    const wchar_t* m_settingsUri;     //m_queryAvailability 报告的设置地址
    volatile long  m_initCalls;       //m_init 被调用的次数
    DWORD          m_initThread;      //m_init 所在的线程
    HANDLE         m_initGate;        //m_init 等待这个事件（为 NULL 时不等）
    volatile long  m_recognizeCalls;  //m_recognize 被调用的次数
    HANDLE         m_recognizeGate;   //m_recognize 等待这个事件（为 NULL 时不等）
    volatile long  m_freeCalls;       //m_freeResult 被调用的次数
    volatile long  m_providerCalls;   //函数表来源被调用的次数
    uint32_t       m_cbSize;          //函数表来源返回的表的 m_cbSize
};

FakeOcrState g_fake;

//恢复假函数表的默认状态
void ResetFake()
{
    g_fake.m_availability = ocr_available;
    g_fake.m_settingsUri = NULL;
    g_fake.m_initCalls = 0;
    g_fake.m_initThread = 0;
    g_fake.m_initGate = NULL;
    g_fake.m_recognizeCalls = 0;
    g_fake.m_recognizeGate = NULL;
    g_fake.m_freeCalls = 0;
    g_fake.m_providerCalls = 0;
    g_fake.m_cbSize = sizeof(OcrApi);
}

//假结果：两行，第一行两个词
OcrWord g_words[2] =
{
    { L"会议", { 10.0f, 20.0f, 32.0f, 16.0f } },
    { L"纪要", { 44.0f, 20.0f, 32.0f, 16.0f } },
};
OcrLine g_lines[2] =
{
    { L"会议纪要", { 10.0f, 20.0f, 66.0f, 16.0f }, 2, g_words },
    { L"Please reply", { 10.0f, 50.0f, 90.0f, 14.0f }, 0, NULL },
};
OcrResult g_result = { sizeof(OcrResult), 2, g_lines, L"zh-Hans-CN", 1.5f, 200 };

int __cdecl FakeInit(const OcrHostInfo* host)
{
    (void)host;
    ::InterlockedIncrement(&g_fake.m_initCalls);
    g_fake.m_initThread = ::GetCurrentThreadId();
    if (g_fake.m_initGate != NULL)
        ::WaitForSingleObject(g_fake.m_initGate, kWaitMs);
    return ocr_ok;
}
const wchar_t* __cdecl FakeName()
{
    return L"fake";
}

const wchar_t* __cdecl FakeVersion()
{
    return L"1.2.3";
}

int __cdecl FakeQuery(OcrAvailabilityInfo* out)
{
    out->m_availability = g_fake.m_availability;
    out->m_settingsUri = g_fake.m_settingsUri;
    return ocr_ok;
}

uint32_t __cdecl FakeLanguageCount()
{
    return 2;
}

const wchar_t* __cdecl FakeLanguageTag(uint32_t i)
{
    return (i == 0) ? L"zh-Hans-CN" : ((i == 1) ? L"en-US" : NULL);
}

const wchar_t* __cdecl FakeLanguageName(uint32_t i)
{
    return (i == 0) ? L"Chinese" : ((i == 1) ? L"English" : NULL);
}

const wchar_t* __cdecl FakeDefaultLanguage()
{
    return L"zh-Hans-CN";
}

int __cdecl FakeRecognize(const OcrImage* image, const wchar_t* languageTag, volatile long* cancelFlag, OcrResult** result)
{
    (void)image;
    (void)languageTag;
    ::InterlockedIncrement(&g_fake.m_recognizeCalls);
    if (g_fake.m_recognizeGate != NULL)
        ::WaitForSingleObject(g_fake.m_recognizeGate, kWaitMs);
    if (cancelFlag != NULL && *cancelFlag != 0)
    {
        *result = NULL;
        return ocr_cancelled;
    }
    *result = &g_result;
    return ocr_ok;
}
void __cdecl FakeFree(OcrResult* result)
{
    if (result != NULL)
        ::InterlockedIncrement(&g_fake.m_freeCalls);
}

const OcrApi g_fakeApi =
{
    sizeof(OcrApi), FakeInit, FakeName, FakeVersion, FakeQuery, FakeLanguageCount, FakeLanguageTag, FakeLanguageName,
    FakeDefaultLanguage, FakeRecognize, FakeFree,
};

//m_cbSize 被改小的函数表（模拟旧版 DLL）
OcrApi g_tinyApi;

//函数表来源：按 g_fake.m_cbSize 返回正常表或改小的表
const OcrApi* FakeProvider(void* context, const char** failure)
{
    (void)context;
    (void)failure;
    ::InterlockedIncrement(&g_fake.m_providerCalls);
    if (g_fake.m_cbSize == sizeof(OcrApi))
        return &g_fakeApi;
    g_tinyApi = g_fakeApi;
    g_tinyApi.m_cbSize = g_fake.m_cbSize;
    return &g_tinyApi;
}

//识别线程用的函数表来源
const OcrApi* FakeApiSource()
{
    return &g_fakeApi;
}

//一张 4×4 的白色测试图
OcrBgraImage WhiteImage()
{
    OcrBgraImage image;
    image.m_width = 4;
    image.m_height = 4;
    image.m_pixels.assign(4 * 4 * kBytesPerPixel, 0xFF);
    return image;
}

//本测试程序所在的目录（带结尾反斜杠）
std::wstring ExeDirectory()
{
    wchar_t path[MAX_PATH] = { 0 };
    ::GetModuleFileNameW(NULL, path, MAX_PATH);
    std::wstring dir(path);
    size_t pos = dir.find_last_of(L"\\/");
    return (pos == std::wstring::npos) ? std::wstring() : dir.substr(0, pos + 1);
}

//建一个只收消息的窗口，接收识别结果
HWND CreateSinkWindow()
{
    return ::CreateWindowExW(0, L"STATIC", L"ocr-test-sink", 0, 0, 0, 0, 0, HWND_MESSAGE, NULL, NULL, NULL);
}

//取出投给 sink 的全部结果（按投递顺序）
std::vector<OcrTextResult*> DrainResults(HWND sink)
{
    std::vector<OcrTextResult*> out;
    MSG msg;
    while (::PeekMessageW(&msg, sink, kTestResultMsg, kTestResultMsg, PM_REMOVE))
        out.push_back(reinterpret_cast<OcrTextResult*>(msg.lParam));
    return out;
}

//取 GDI+ 某种编码器的 CLSID
bool GetEncoderClsid(const wchar_t* mime, CLSID& clsid)
{
    UINT count = 0;
    UINT bytes = 0;
    Gdiplus::GetImageEncodersSize(&count, &bytes);
    if (bytes == 0)
        return false;
    std::vector<BYTE> buffer(bytes);
    Gdiplus::ImageCodecInfo* codecs = reinterpret_cast<Gdiplus::ImageCodecInfo*>(&buffer[0]);
    Gdiplus::GetImageEncoders(count, bytes, codecs);
    for (UINT i = 0; i < count; ++i)
    {
        if (wcscmp(codecs[i].MimeType, mime) == 0)
        {
            clsid = codecs[i].Clsid;
            return true;
        }
    }
    return false;
}

//进程内 GDI+ 的启停（多次 GdiplusStartup 是安全的）
class GdiplusScope
{
public:
    GdiplusScope()
        : m_token(0)
    {
        Gdiplus::GdiplusStartupInput input;
        Gdiplus::GdiplusStartup(&m_token, &input, NULL);
    }
    ~GdiplusScope()
    {
        Gdiplus::GdiplusShutdown(m_token);
    }

private:
    ULONG_PTR m_token;   //GdiplusStartup 返回的令牌
};

} // namespace

TEST_CASE("OCRH1 预加载完成之前入口不显示，完成后显示", "[ocrhost]")
{
    ResetFake();
    g_fake.m_initGate = ::CreateEventW(NULL, TRUE, FALSE, NULL);
    COcrModuleHost host;
    CHECK(host.GetState() == ocr_host_not_started);
    CHECK_FALSE(host.CanShowEntry());

    const DWORD start = ::GetTickCount();
    host.StartPreloadWithProvider(FakeProvider, NULL, L"zh-CN");
    CHECK(::GetTickCount() - start < kWaitMs / 2);   //m_init 被卡住，调用方仍立即返回
    CHECK(host.GetState() == ocr_host_loading);
    CHECK_FALSE(host.CanShowEntry());
    CHECK(host.GetApi() == NULL);

    ::SetEvent(g_fake.m_initGate);
    REQUIRE(host.WaitForPreload(kWaitMs));
    CHECK(host.GetState() == ocr_host_ready);
    CHECK(host.CanShowEntry());
    CHECK(host.GetApi() == &g_fakeApi);

    OcrHostSnapshot snap = host.GetSnapshot();
    CHECK(snap.m_availability == ocr_available);
    CHECK(snap.m_engineName == L"fake");
    CHECK(snap.m_engineVersion == L"1.2.3");
    CHECK(snap.m_defaultLanguage == L"zh-Hans-CN");
    REQUIRE(snap.m_languages.size() == 2);
    CHECK(snap.m_languages[1].m_tag == L"en-US");
    CHECK(snap.m_languages[1].m_name == L"English");
    ::CloseHandle(g_fake.m_initGate);
    g_fake.m_initGate = NULL;
}

TEST_CASE("OCRH2 DLL 不存在时不可用且不再重试", "[ocrhost]")
{
    ResetFake();
    COcrModuleHost host;
    host.StartPreload(ExeDirectory() + L"NoSuchFlamingoOcr_test.dll", L"zh-CN");
    REQUIRE(host.WaitForPreload(kWaitMs));
    CHECK(host.GetState() == ocr_host_unavailable);
    CHECK_FALSE(host.CanShowEntry());
    CHECK(host.GetApi() == NULL);
    CHECK_FALSE(host.GetSnapshot().m_failure.empty());

    //再次启动（哪怕换成能成功的来源）也不会重试
    host.StartPreloadWithProvider(FakeProvider, NULL, L"zh-CN");
    CHECK(host.GetState() == ocr_host_unavailable);
    CHECK(g_fake.m_providerCalls == 0);
}

TEST_CASE("OCRH3 加载不是识别模块的系统 DLL 时不可用", "[ocrhost]")
{
    ResetFake();
    wchar_t systemDir[MAX_PATH] = { 0 };
    REQUIRE(::GetSystemDirectoryW(systemDir, MAX_PATH) > 0);
    COcrModuleHost host;
    host.StartPreload(std::wstring(systemDir) + L"\\version.dll", L"zh-CN");
    REQUIRE(host.WaitForPreload(kWaitMs));
    CHECK(host.GetState() == ocr_host_unavailable);
    CHECK_FALSE(host.CanShowEntry());
    CHECK_FALSE(host.GetSnapshot().m_failure.empty());
}

TEST_CASE("OCRH4 函数表过小时不可用且不初始化", "[ocrhost]")
{
    ResetFake();
    g_fake.m_cbSize = sizeof(uint32_t) * 2;
    COcrModuleHost host;
    host.StartPreloadWithProvider(FakeProvider, NULL, L"zh-CN");
    REQUIRE(host.WaitForPreload(kWaitMs));
    CHECK(host.GetState() == ocr_host_unavailable);
    CHECK_FALSE(host.CanShowEntry());
    CHECK(g_fake.m_initCalls == 0);
}

TEST_CASE("OCRH5 系统不支持与没有识别语言时入口显示、点击给出提示", "[ocrhost]")
{
    ResetFake();
    g_fake.m_availability = ocr_os_unsupported;
    COcrModuleHost host;
    host.StartPreloadWithProvider(FakeProvider, NULL, L"zh-CN");
    REQUIRE(host.WaitForPreload(kWaitMs));
    CHECK(host.CanShowEntry());

    //系统不支持：只有「确定」，不询问打开设置
    OcrPrompt prompt;
    CHECK_FALSE(CheckOcrBeforeRecognize(host.GetSnapshot(), prompt));
    CHECK_FALSE(prompt.m_message.empty());
    CHECK_FALSE(prompt.m_title.empty());
    CHECK(prompt.m_message.find(L"Windows 10") != std::wstring::npos);
    CHECK_FALSE(prompt.m_askOpenSettings);

    //没有识别语言、引擎给出了设置地址：询问打开设置
    OcrHostSnapshot snap = host.GetSnapshot();
    snap.m_availability = ocr_no_language;
    snap.m_settingsUri = L"ms-settings:regionlanguage";
    CHECK_FALSE(CheckOcrBeforeRecognize(snap, prompt));
    CHECK(prompt.m_askOpenSettings);
    CHECK(prompt.m_settingsUri == L"ms-settings:regionlanguage");

    //没有识别语言、没有设置地址：只提示
    snap.m_settingsUri.clear();
    CHECK_FALSE(CheckOcrBeforeRecognize(snap, prompt));
    CHECK_FALSE(prompt.m_askOpenSettings);

    //引擎初始化失败
    snap.m_availability = ocr_engine_failed;
    CHECK_FALSE(CheckOcrBeforeRecognize(snap, prompt));
    CHECK_FALSE(prompt.m_message.empty());
    CHECK_FALSE(prompt.m_askOpenSettings);

    //可用：直接开始识别
    snap.m_availability = ocr_available;
    CHECK(CheckOcrBeforeRecognize(snap, prompt));

    //宿主尚未就绪或不可用（入口本不显示的时序情况）也给出提示
    snap.m_state = ocr_host_loading;
    CHECK_FALSE(CheckOcrBeforeRecognize(snap, prompt));
    CHECK_FALSE(prompt.m_message.empty());
    snap.m_state = ocr_host_unavailable;
    CHECK_FALSE(CheckOcrBeforeRecognize(snap, prompt));
    CHECK_FALSE(prompt.m_message.empty());
}

TEST_CASE("OCRH6 预加载只执行一次且在后台线程上", "[ocrhost]")
{
    ResetFake();
    COcrModuleHost host;
    host.StartPreloadWithProvider(FakeProvider, NULL, L"zh-CN");
    host.StartPreloadWithProvider(FakeProvider, NULL, L"en-US");
    host.StartPreload(L"C:\\never\\used.dll", L"ja-JP");
    REQUIRE(host.WaitForPreload(kWaitMs));
    CHECK(g_fake.m_providerCalls == 1);
    CHECK(g_fake.m_initCalls == 1);
    CHECK(g_fake.m_initThread != 0);
    CHECK(g_fake.m_initThread != ::GetCurrentThreadId());
    CHECK(host.GetState() == ocr_host_ready);
}

TEST_CASE("OCRH7 识别结果完整拷入 exe 的结构并恰好释放一次", "[ocrhost]")
{
    ResetFake();
    const long liveBefore = OcrTextResult::LiveCount();

    OcrRequest request;
    request.m_image = WhiteImage();
    OcrTextResult* result = RunOcrRequest(&g_fakeApi, request, NULL, 7);
    REQUIRE(result != NULL);
    CHECK(result->m_taskId == 7);
    CHECK(result->m_code == ocr_ok);
    CHECK(g_fake.m_freeCalls == 1);
    CHECK(result->m_languageTag == L"zh-Hans-CN");
    CHECK(result->m_textAngle == Catch::Approx(1.5f));
    CHECK(result->m_scalePercent == 200);
    CHECK(result->m_imageWidth == 4);
    CHECK(result->m_imageHeight == 4);
    REQUIRE(result->m_lines.size() == 2);
    CHECK(result->m_lines[0].m_text == L"会议纪要");
    CHECK(result->m_lines[0].m_w == Catch::Approx(66.0f));
    REQUIRE(result->m_lines[0].m_words.size() == 2);
    CHECK(result->m_lines[0].m_words[1].m_text == L"纪要");
    CHECK(result->m_lines[0].m_words[1].m_x == Catch::Approx(44.0f));
    CHECK(result->m_lines[1].m_words.empty());
    CHECK(result->JoinedText() == L"会议纪要\r\nPlease reply");
    delete result;
    CHECK(OcrTextResult::LiveCount() == liveBefore);

    //没有引擎
    result = RunOcrRequest(NULL, request, NULL, 8);
    CHECK(result->m_code == ocr_task_no_engine);
    delete result;

    //已取消：不调用 m_recognize
    volatile long cancel = 1;
    const long callsBefore = g_fake.m_recognizeCalls;
    result = RunOcrRequest(&g_fakeApi, request, &cancel, 9);
    CHECK(result->m_code == ocr_cancelled);
    CHECK(g_fake.m_recognizeCalls == callsBefore);
    delete result;

    //图片文件打不开
    OcrRequest missing;
    missing.m_filePath = ExeDirectory() + L"no_such_image_for_ocr.png";
    result = RunOcrRequest(&g_fakeApi, missing, NULL, 10);
    CHECK(result->m_code == ocr_task_decode_failed);
    delete result;
    CHECK(OcrTextResult::LiveCount() == liveBefore);
}

TEST_CASE("OCRH7 图片文件解码并按旋转方向转正，位图选区转成 BGRA", "[ocrhost]")
{
    GdiplusScope gdiplus;

    //3×2 的 PNG：左上角红色，其余白色
    Gdiplus::Bitmap bmp(3, 2, PixelFormat32bppARGB);
    for (INT y = 0; y < 2; ++y)
    {
        for (INT x = 0; x < 3; ++x)
            bmp.SetPixel(x, y, Gdiplus::Color(255, 255, 255, 255));
    }
    bmp.SetPixel(0, 0, Gdiplus::Color(255, 255, 0, 0));
    CLSID png;
    REQUIRE(GetEncoderClsid(L"image/png", png));
    wchar_t tempDir[MAX_PATH] = { 0 };
    REQUIRE(::GetTempPathW(MAX_PATH, tempDir) > 0);
    const std::wstring path = std::wstring(tempDir) + L"flamingo_ocr_decode_test.png";
    REQUIRE(bmp.Save(path.c_str(), &png, NULL) == Gdiplus::Ok);

    OcrBgraImage plain;
    REQUIRE(DecodeImageFileToBgra(path, 0, plain));
    CHECK(plain.m_width == 3);
    CHECK(plain.m_height == 2);
    CHECK(plain.m_pixels[2] == 255);   //左上角红色（BGRA 的 R）
    CHECK(plain.m_pixels[0] == 0);

    //顺时针转 90°：宽高互换，原来的左上角到了右上角
    OcrBgraImage turned;
    REQUIRE(DecodeImageFileToBgra(path, 1, turned));
    CHECK(turned.m_width == 2);
    CHECK(turned.m_height == 3);
    const size_t topRight = (turned.m_width - 1) * kBytesPerPixel;
    CHECK(turned.m_pixels[topRight + 2] == 255);
    CHECK(turned.m_pixels[topRight + 0] == 0);
    ::DeleteFileW(path.c_str());

    //位图选区：4×3 的 DIB 段，取 (1,1)-(3,3)
    BITMAPINFO bmi;
    memset(&bmi, 0, sizeof(bmi));
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = 4;
    bmi.bmiHeader.biHeight = -3;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;
    void* bits = NULL;
    HBITMAP dib = ::CreateDIBSection(NULL, &bmi, DIB_RGB_COLORS, &bits, NULL, 0);
    REQUIRE(dib != NULL);
    uint8_t* px = static_cast<uint8_t*>(bits);
    for (int i = 0; i < 4 * 3; ++i)
    {
        px[i * 4 + 0] = static_cast<uint8_t>(i);   //B 通道写像素序号，便于核对位置
        px[i * 4 + 1] = 0;
        px[i * 4 + 2] = 0;
        px[i * 4 + 3] = 0;                          //GDI 位图的透明度字节无意义
    }
    RECT crop = { 1, 1, 3, 3 };
    OcrBgraImage part;
    REQUIRE(BitmapToBgra(dib, &crop, part));
    CHECK(part.m_width == 2);
    CHECK(part.m_height == 2);
    CHECK(part.m_pixels[0] == 5);                   //(1,1) 是第 5 个像素
    CHECK(part.m_pixels[kBytesPerPixel] == 6);      //(2,1)
    CHECK(part.m_pixels[2 * kBytesPerPixel] == 9);  //(1,2)
    CHECK(part.m_pixels[3] == 255);                 //透明度置为不透明
    RECT outside = { 10, 10, 20, 20 };
    CHECK_FALSE(BitmapToBgra(dib, &outside, part));
    ::DeleteObject(dib);
}

TEST_CASE("OCRH8 结果窗口已关闭时结果被释放", "[ocrhost]")
{
    ResetFake();
    const long liveBefore = OcrTextResult::LiveCount();

    //直接投递：窗口已销毁
    HWND gone = CreateSinkWindow();
    REQUIRE(gone != NULL);
    ::DestroyWindow(gone);
    CHECK_FALSE(DeliverOcrResult(gone, kTestResultMsg, new OcrTextResult()));
    CHECK(OcrTextResult::LiveCount() == liveBefore);
    CHECK_FALSE(DeliverOcrResult(NULL, kTestResultMsg, new OcrTextResult()));
    CHECK(OcrTextResult::LiveCount() == liveBefore);

    //经识别线程投递给已销毁的窗口
    {
        COcrRecognizer recognizer(FakeApiSource);
        OcrRequest request;
        request.m_image = WhiteImage();
        request.m_target = gone;
        request.m_message = kTestResultMsg;
        CHECK(recognizer.Submit(request) > 0);
        REQUIRE(recognizer.WaitIdle(kWaitMs));
    }
    CHECK(OcrTextResult::LiveCount() == liveBefore);
}

TEST_CASE("OCRH8 识别线程按提交顺序执行，排队中的任务可以取消", "[ocrhost]")
{
    ResetFake();
    const long liveBefore = OcrTextResult::LiveCount();
    HWND sink = CreateSinkWindow();
    REQUIRE(sink != NULL);
    g_fake.m_recognizeGate = ::CreateEventW(NULL, TRUE, FALSE, NULL);
    {
        COcrRecognizer recognizer(FakeApiSource);
        OcrRequest request;
        request.m_image = WhiteImage();
        request.m_target = sink;
        request.m_message = kTestResultMsg;
        const uint32_t first = recognizer.Submit(request);
        const uint32_t second = recognizer.Submit(request);
        const uint32_t third = recognizer.Submit(request);
        CHECK(first > 0);
        CHECK(second == first + 1);
        CHECK(third == second + 1);

        recognizer.Cancel(second);       //第二个还在排队：取消后不再调用 m_recognize
        ::SetEvent(g_fake.m_recognizeGate);
        REQUIRE(recognizer.WaitIdle(kWaitMs));

        std::vector<OcrTextResult*> results = DrainResults(sink);
        REQUIRE(results.size() == 3);
        CHECK(results[0]->m_taskId == first);
        CHECK(results[0]->m_code == ocr_ok);
        CHECK(results[1]->m_taskId == second);
        CHECK(results[1]->m_code == ocr_cancelled);
        CHECK(results[2]->m_taskId == third);
        CHECK(results[2]->m_code == ocr_ok);
        CHECK(g_fake.m_recognizeCalls == 2);
        for (size_t i = 0; i < results.size(); ++i)
            delete results[i];
    }
    ::CloseHandle(g_fake.m_recognizeGate);
    g_fake.m_recognizeGate = NULL;
    ::DestroyWindow(sink);
    CHECK(OcrTextResult::LiveCount() == liveBefore);
}
