/**
 *  图片文字识别执行端的实现，说明见 OcrRecognizer.h。
 *
 *  本文件是纯逻辑模块，与 FlamingoUnit 测试工程共用，<u>不</u> include stdafx.h
 *  （工程里对应的 PrecompiledHeader 设为 NotUsing）。
 *
 *  balloonwj@qq.com   2026-10-05
 */
#include "OcrRecognizer.h"

#include <objbase.h>
#include <gdiplus.h>
#include <string.h>

#include "OcrModuleHost.h"
#include "../../Common/Log/IULog.h"

namespace ocr {

namespace {

//每像素字节数（32 位 BGRA）
const uint32_t kBytesPerPixel = 4;

//GetDIBits 取位图时每像素的位数
const WORD kBitsPerPixel = 32;

//BGRA 中透明度所在的字节下标
const uint32_t kAlphaIndex = 3;

//一个通道的最大值
const uint8_t kChannelMax = 255;

//一圈有几个 90°
const int kQuarterTurnsPerCircle = 4;

//WaitIdle 轮询的间隔（毫秒）
const DWORD kIdlePollMs = 2;

//仍未释放的 OcrTextResult 个数
volatile long g_liveResults = 0;

//GDI+ 的令牌：本模块惰性启动一次、不关闭（多次 GdiplusStartup 是安全的，与仓库其它模块相同）
ULONG_PTR g_gdiplusToken = 0;
volatile LONG g_gdiplusStarted = 0;

//确保本模块的 GDI+ 已启动
bool EnsureGdiplus()
{
    if (::InterlockedCompareExchange(&g_gdiplusStarted, 1, 0) == 0)
    {
        Gdiplus::GdiplusStartupInput input;
        if (Gdiplus::GdiplusStartup(&g_gdiplusToken, &input, NULL) != Gdiplus::Ok)
        {
            ::InterlockedExchange(&g_gdiplusStarted, 0);
            return false;
        }
    }
    return true;
}

//顺时针旋转次数对应的 GDI+ 旋转方式
Gdiplus::RotateFlipType RotationOf(int quarterTurns)
{
    int turns = quarterTurns % kQuarterTurnsPerCircle;
    if (turns < 0)
        turns += kQuarterTurnsPerCircle;
    switch (turns)
    {
    //顺时针 90°
    case 1:
        return Gdiplus::Rotate90FlipNone;
    //180°
    case 2:
        return Gdiplus::Rotate180FlipNone;
    //顺时针 270°（即逆时针 90°）
    case 3:
        return Gdiplus::Rotate270FlipNone;
    //不旋转
    default:
        return Gdiplus::RotateNoneFlipNone;
    }
}

//宽字符串里的 ASCII 部分转成窄字符串，写日志用
std::string Narrow(const std::wstring& text)
{
    std::string out;
    for (size_t i = 0; i < text.size(); ++i)
        out += (text[i] < 0x80) ? static_cast<char>(text[i]) : '?';
    return out;
}

} // namespace

OcrTextWord::OcrTextWord()
    : m_x(0)
    , m_y(0)
    , m_w(0)
    , m_h(0)
{
}

OcrTextLine::OcrTextLine()
    : m_x(0)
    , m_y(0)
    , m_w(0)
    , m_h(0)
{
}

OcrTextResult::OcrTextResult()
    : m_taskId(0)
    , m_code(flamingoocr::ocr_engine_error)
    , m_textAngle(0.0f)
    , m_scalePercent(100)
    , m_imageWidth(0)
    , m_imageHeight(0)
    , m_elapsedMs(0)
{
    ::InterlockedIncrement(&g_liveResults);
}

OcrTextResult::~OcrTextResult()
{
    ::InterlockedDecrement(&g_liveResults);
}

std::wstring OcrTextResult::JoinedText() const
{
    std::wstring out;
    for (size_t i = 0; i < m_lines.size(); ++i)
    {
        if (i > 0)
            out += L"\r\n";
        out += m_lines[i].m_text;
    }
    return out;
}

long OcrTextResult::LiveCount()
{
    return g_liveResults;
}

OcrBgraImage::OcrBgraImage()
    : m_width(0)
    , m_height(0)
{
}

OcrRequest::OcrRequest()
    : m_quarterTurns(0)
    , m_target(NULL)
    , m_message(0)
{
}

bool HasImageSource(const OcrRequest& request)
{
    if (!request.m_filePath.empty())
        return true;
    return request.m_image.m_width > 0 && request.m_image.m_height > 0 && !request.m_image.m_pixels.empty();
}

bool DecodeImageFileToBgra(const std::wstring& path, int quarterTurns, OcrBgraImage& out)
{
    out = OcrBgraImage();
    if (path.empty() || !EnsureGdiplus())
        return false;

    Gdiplus::Bitmap* bitmap = Gdiplus::Bitmap::FromFile(path.c_str(), FALSE);
    if (bitmap == NULL)
        return false;
    if (bitmap->GetLastStatus() != Gdiplus::Ok || bitmap->GetWidth() == 0 || bitmap->GetHeight() == 0)
    {
        delete bitmap;
        return false;
    }

    Gdiplus::RotateFlipType rotation = RotationOf(quarterTurns);
    if (rotation != Gdiplus::RotateNoneFlipNone)
        bitmap->RotateFlip(rotation);

    const UINT width = bitmap->GetWidth();
    const UINT height = bitmap->GetHeight();
    Gdiplus::BitmapData data;
    Gdiplus::Rect rect(0, 0, static_cast<INT>(width), static_cast<INT>(height));
    if (bitmap->LockBits(&rect, Gdiplus::ImageLockModeRead, PixelFormat32bppARGB, &data) != Gdiplus::Ok)
    {
        delete bitmap;
        return false;
    }

    const size_t rowBytes = static_cast<size_t>(width) * kBytesPerPixel;
    out.m_pixels.resize(rowBytes * height);
    for (UINT y = 0; y < height; ++y)
    {
        memcpy(&out.m_pixels[y * rowBytes], static_cast<const uint8_t*>(data.Scan0) + static_cast<ptrdiff_t>(y) * data.Stride,
               rowBytes);
    }
    bitmap->UnlockBits(&data);
    delete bitmap;
    out.m_width = width;
    out.m_height = height;
    return true;
}

bool BitmapToBgra(HBITMAP bitmap, const RECT* crop, OcrBgraImage& out)
{
    out = OcrBgraImage();
    BITMAP info;
    if (bitmap == NULL || ::GetObject(bitmap, sizeof(info), &info) == 0 || info.bmWidth <= 0 || info.bmHeight == 0)
        return false;
    const LONG fullHeight = (info.bmHeight > 0) ? info.bmHeight : -info.bmHeight;

    RECT area = { 0, 0, info.bmWidth, fullHeight };
    if (crop != NULL)
    {
        RECT bounds = area;
        if (!::IntersectRect(&area, crop, &bounds))
            return false;
    }
    const uint32_t width = static_cast<uint32_t>(area.right - area.left);
    const uint32_t height = static_cast<uint32_t>(area.bottom - area.top);
    if (width == 0 || height == 0)
        return false;

    //整张位图按自上而下的 32 位 DIB 取出，再拷出选区
    std::vector<uint8_t> full(static_cast<size_t>(info.bmWidth) * fullHeight * kBytesPerPixel);
    BITMAPINFO bmi;
    memset(&bmi, 0, sizeof(bmi));
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = info.bmWidth;
    bmi.bmiHeader.biHeight = -fullHeight;   //负值表示自上而下
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = kBitsPerPixel;
    bmi.bmiHeader.biCompression = BI_RGB;
    HDC screen = ::GetDC(NULL);
    const int lines = ::GetDIBits(screen, bitmap, 0, static_cast<UINT>(fullHeight), &full[0], &bmi, DIB_RGB_COLORS);
    ::ReleaseDC(NULL, screen);
    if (lines != fullHeight)
        return false;

    const size_t fullRow = static_cast<size_t>(info.bmWidth) * kBytesPerPixel;
    const size_t rowBytes = static_cast<size_t>(width) * kBytesPerPixel;
    out.m_pixels.resize(rowBytes * height);
    for (uint32_t y = 0; y < height; ++y)
    {
        memcpy(&out.m_pixels[y * rowBytes], &full[(area.top + y) * fullRow + static_cast<size_t>(area.left) * kBytesPerPixel],
               rowBytes);
    }
    //GDI 位图的透明度字节没有意义，一律置为不透明
    for (size_t i = kAlphaIndex; i < out.m_pixels.size(); i += kBytesPerPixel)
        out.m_pixels[i] = kChannelMax;
    out.m_width = width;
    out.m_height = height;
    return true;
}

OcrTextResult* RunOcrRequest(const flamingoocr::OcrApi* api, const OcrRequest& request, volatile long* cancelFlag,
                             uint32_t taskId)
{
    const DWORD start = ::GetTickCount();
    OcrTextResult* result = new OcrTextResult();
    result->m_taskId = taskId;
    if (api == NULL)
    {
        result->m_code = ocr_task_no_engine;
        return result;
    }
    if (cancelFlag != NULL && *cancelFlag != 0)
    {
        result->m_code = flamingoocr::ocr_cancelled;
        return result;
    }

    //图片来源：文件优先，否则用内存中的图像
    OcrBgraImage decoded;
    const OcrBgraImage* image = &request.m_image;
    if (!request.m_filePath.empty())
    {
        if (!DecodeImageFileToBgra(request.m_filePath, request.m_quarterTurns, decoded))
        {
            LOG_WARNING("ocr task %u: cannot decode the image file.", taskId);
            result->m_code = ocr_task_decode_failed;
            return result;
        }
        image = &decoded;
    }
    if (image->m_width == 0 || image->m_height == 0 || image->m_pixels.empty())
    {
        result->m_code = ocr_task_decode_failed;
        return result;
    }
    result->m_imageWidth = image->m_width;
    result->m_imageHeight = image->m_height;

    flamingoocr::OcrImage input;
    input.m_cbSize = sizeof(input);
    input.m_width = image->m_width;
    input.m_height = image->m_height;
    input.m_stride = image->m_width * kBytesPerPixel;
    input.m_pixels = &image->m_pixels[0];

    flamingoocr::OcrResult* raw = NULL;
    const wchar_t* language = request.m_languageTag.empty() ? NULL : request.m_languageTag.c_str();
    result->m_code = api->m_recognize(&input, language, cancelFlag, &raw);
    if (result->m_code == flamingoocr::ocr_ok && raw != NULL)
    {
        result->m_languageTag = (raw->m_languageTag != NULL) ? raw->m_languageTag : L"";
        result->m_textAngle = raw->m_textAngle;
        result->m_scalePercent = raw->m_scalePercent;
        for (uint32_t i = 0; i < raw->m_lineCount; ++i)
        {
            const flamingoocr::OcrLine& src = raw->m_lines[i];
            OcrTextLine line;
            line.m_text = (src.m_text != NULL) ? src.m_text : L"";
            line.m_x = src.m_rect.m_x;
            line.m_y = src.m_rect.m_y;
            line.m_w = src.m_rect.m_w;
            line.m_h = src.m_rect.m_h;
            for (uint32_t j = 0; j < src.m_wordCount; ++j)
            {
                OcrTextWord word;
                word.m_text = (src.m_words[j].m_text != NULL) ? src.m_words[j].m_text : L"";
                word.m_x = src.m_words[j].m_rect.m_x;
                word.m_y = src.m_words[j].m_rect.m_y;
                word.m_w = src.m_words[j].m_rect.m_w;
                word.m_h = src.m_words[j].m_rect.m_h;
                line.m_words.push_back(word);
            }
            result->m_lines.push_back(line);
        }
    }
    if (raw != NULL)
        api->m_freeResult(raw);

    result->m_elapsedMs = ::GetTickCount() - start;
    LOG_INFO("ocr task %u: %ux%u, code %d, %u lines, language %s, scale %u%%, %u ms.", taskId, result->m_imageWidth,
             result->m_imageHeight, result->m_code, static_cast<unsigned>(result->m_lines.size()),
             Narrow(result->m_languageTag).c_str(), result->m_scalePercent, static_cast<unsigned>(result->m_elapsedMs));
    return result;
}

bool DeliverOcrResult(HWND target, UINT message, OcrTextResult* result)
{
    if (result == NULL)
        return false;
    if (target != NULL && ::IsWindow(target)
        && ::PostMessage(target, message, 0, reinterpret_cast<LPARAM>(result)))
        return true;
    LOG_INFO("ocr task %u: target window is gone, result dropped.", result->m_taskId);
    delete result;
    return false;
}

COcrRecognizer::COcrRecognizer(OcrRecognizerApiFn apiSource)
    : m_apiSource(apiSource)
    , m_running(NULL)
    , m_nextId(1)
    , m_stopping(false)
    , m_thread(NULL)
    , m_wakeEvent(::CreateEventW(NULL, FALSE, FALSE, NULL))
{
    ::InitializeSRWLock(&m_lock);
}

COcrRecognizer::~COcrRecognizer()
{
    ::AcquireSRWLockExclusive(&m_lock);
    m_stopping = true;
    if (m_running != NULL)
        ::InterlockedExchange(&m_running->m_cancel, 1);
    ::ReleaseSRWLockExclusive(&m_lock);

    bool stopped = true;
    if (m_thread != NULL)
    {
        ::SetEvent(m_wakeEvent);
        stopped = (::WaitForSingleObject(m_thread, kStopWaitMs) == WAIT_OBJECT_0);
        ::CloseHandle(m_thread);
        m_thread = NULL;
    }

    //线程已退出时，未执行的任务按已取消投递；线程卡在引擎里没有退出时不碰它可能还在用的任务
    if (stopped)
    {
        for (size_t i = 0; i < m_queue.size(); ++i)
        {
            OcrTextResult* result = new OcrTextResult();
            result->m_taskId = m_queue[i]->m_id;
            result->m_code = flamingoocr::ocr_cancelled;
            DeliverOcrResult(m_queue[i]->m_request.m_target, m_queue[i]->m_request.m_message, result);
            delete m_queue[i];
        }
        m_queue.clear();
    }
    if (m_wakeEvent != NULL)
        ::CloseHandle(m_wakeEvent);
}

COcrRecognizer& COcrRecognizer::GetInstance()
{
    //有意不释放：进程退出时不等待可能仍在识别的引擎
    static COcrRecognizer* s_instance = new COcrRecognizer();
    return *s_instance;
}

uint32_t COcrRecognizer::Submit(const OcrRequest& request)
{
    Task* task = new Task();
    task->m_request = request;
    task->m_cancel = 0;

    ::AcquireSRWLockExclusive(&m_lock);
    if (m_thread == NULL && !m_stopping)
    {
        m_thread = ::CreateThread(NULL, 0, ThreadProc, this, 0, NULL);
        if (m_thread == NULL)
            LOG_ERROR("ocr recognizer: CreateThread failed, error %u.", static_cast<unsigned>(::GetLastError()));
    }
    if (m_thread == NULL || m_stopping)
    {
        ::ReleaseSRWLockExclusive(&m_lock);
        delete task;
        return 0;
    }
    task->m_id = m_nextId++;
    if (m_nextId == 0)
        m_nextId = 1;   //任务号回绕时跳过 0（0 表示提交失败）
    const uint32_t id = task->m_id;
    m_queue.push_back(task);
    ::ReleaseSRWLockExclusive(&m_lock);

    ::SetEvent(m_wakeEvent);
    return id;
}

void COcrRecognizer::Cancel(uint32_t taskId)
{
    ::AcquireSRWLockExclusive(&m_lock);
    if (m_running != NULL && m_running->m_id == taskId)
        ::InterlockedExchange(&m_running->m_cancel, 1);
    for (size_t i = 0; i < m_queue.size(); ++i)
    {
        if (m_queue[i]->m_id == taskId)
            ::InterlockedExchange(&m_queue[i]->m_cancel, 1);
    }
    ::ReleaseSRWLockExclusive(&m_lock);
}

bool COcrRecognizer::WaitIdle(DWORD timeoutMs)
{
    const DWORD start = ::GetTickCount();
    for (;;)
    {
        ::AcquireSRWLockShared(&m_lock);
        const bool idle = m_queue.empty() && m_running == NULL;
        ::ReleaseSRWLockShared(&m_lock);
        if (idle)
            return true;
        if (::GetTickCount() - start >= timeoutMs)
            return false;
        ::Sleep(kIdlePollMs);
    }
}

DWORD WINAPI COcrRecognizer::ThreadProc(LPVOID param)
{
    //引擎要用 WinRT，本线程进入多线程套间
    HRESULT hr = ::CoInitializeEx(NULL, COINIT_MULTITHREADED);
    static_cast<COcrRecognizer*>(param)->Run();
    if (SUCCEEDED(hr))
        ::CoUninitialize();
    return 0;
}

const flamingoocr::OcrApi* COcrRecognizer::CurrentApi() const
{
    if (m_apiSource != NULL)
        return m_apiSource();
    return COcrModuleHost::GetInstance().GetApi();
}

void COcrRecognizer::Run()
{
    for (;;)
    {
        ::AcquireSRWLockExclusive(&m_lock);
        if (m_stopping)
        {
            ::ReleaseSRWLockExclusive(&m_lock);
            return;
        }
        Task* task = NULL;
        if (!m_queue.empty())
        {
            task = m_queue.front();
            m_queue.pop_front();
        }
        m_running = task;
        ::ReleaseSRWLockExclusive(&m_lock);

        if (task == NULL)
        {
            ::WaitForSingleObject(m_wakeEvent, INFINITE);
            continue;
        }

        OcrTextResult* result = RunOcrRequest(CurrentApi(), task->m_request, &task->m_cancel, task->m_id);
        DeliverOcrResult(task->m_request.m_target, task->m_request.m_message, result);

        ::AcquireSRWLockExclusive(&m_lock);
        m_running = NULL;
        ::ReleaseSRWLockExclusive(&m_lock);
        delete task;
    }
}

} // namespace ocr
