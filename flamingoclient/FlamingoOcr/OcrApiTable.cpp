/**
 *  FlamingoOcr.dll 导出函数表的实现，说明见 OcrApiTable.h。
 *
 *  每个导出函数的结构相同：先检查参数，再在 try 块里调用后端；std::bad_alloc 转成 ocr_out_of_memory，
 *  其它异常转成 ocr_engine_error 并写一条错误日志。异常不越过模块边界。
 *
 *  balloonwj@qq.com   2026-10-05
 */
#include "OcrApiTable.h"

#include <windows.h>
#include <new>

#include "Core/OcrHostLog.h"
#include "Core/OcrResultBuilder.h"

namespace flamingoocr {

namespace {

//没有后端时报告的引擎名称与版本
const wchar_t* const kNoEngineName = L"none";
const wchar_t* const kNoEngineVersion = L"0";

//每像素字节数（32 位 BGRA）
const uint64_t kBytesPerPixel = 4;

//初始化状态。m_init 在预加载线程上调用，其余函数可能在别的线程读取，故用读写锁保护
SRWLOCK g_stateLock = SRWLOCK_INIT;
IOcrBackend* g_backend = NULL;       //当前后端，归安装方所有；NULL 表示没有后端
bool g_initDone = false;             //m_init 是否已执行过
int g_initResult = ocr_engine_error; //第一次 m_init 的结果；之后的调用直接返回它

//读取当前后端与初始化状态的快照
void Snapshot(IOcrBackend*& backend, bool& initDone)
{
    ::AcquireSRWLockShared(&g_stateLock);
    backend = g_backend;
    initDone = g_initDone;
    ::ReleaseSRWLockShared(&g_stateLock);
}

//初始化成功后才可用的后端；未初始化或初始化失败时返回 NULL
IOcrBackend* ReadyBackend()
{
    IOcrBackend* backend = NULL;
    bool initDone = false;
    Snapshot(backend, initDone);
    if (!initDone)
        return NULL;
    ::AcquireSRWLockShared(&g_stateLock);
    const int initResult = g_initResult;
    ::ReleaseSRWLockShared(&g_stateLock);
    return (initResult == ocr_ok) ? backend : NULL;
}

int __cdecl ApiInit(const OcrHostInfo* host)
{
    if (host == NULL || host->m_cbSize < kOcrHostInfoMinSize)
        return ocr_bad_argument;

    ::AcquireSRWLockExclusive(&g_stateLock);
    if (g_initDone)
    {
        const int previous = g_initResult;
        ::ReleaseSRWLockExclusive(&g_stateLock);
        return previous;
    }

    SetOcrLogSink(host->m_log);
    const wchar_t* uiLanguage = NULL;
    if (host->m_cbSize >= offsetof(OcrHostInfo, m_uiLanguage) + sizeof(host->m_uiLanguage))
        uiLanguage = host->m_uiLanguage;

    int result = ocr_ok;
    try
    {
        if (g_backend == NULL)
        {
            OcrLogError("ocr init: no engine backend installed.");
            result = ocr_engine_error;
        }
        else if (!g_backend->Init(uiLanguage))
        {
            OcrLogError("ocr init: engine backend failed to initialize.");
            result = ocr_engine_error;
        }
    }
    catch (const std::bad_alloc&)
    {
        result = ocr_out_of_memory;
    }
    catch (...)
    {
        OcrLogError("ocr init: unexpected exception.");
        result = ocr_engine_error;
    }

    g_initDone = true;
    g_initResult = result;
    ::ReleaseSRWLockExclusive(&g_stateLock);
    return result;
}

const wchar_t* __cdecl ApiGetEngineName()
{
    IOcrBackend* backend = NULL;
    bool initDone = false;
    Snapshot(backend, initDone);
    return (backend != NULL) ? backend->EngineName() : kNoEngineName;
}

const wchar_t* __cdecl ApiGetEngineVersion()
{
    IOcrBackend* backend = NULL;
    bool initDone = false;
    Snapshot(backend, initDone);
    return (backend != NULL) ? backend->EngineVersion() : kNoEngineVersion;
}

int __cdecl ApiQueryAvailability(OcrAvailabilityInfo* out)
{
    if (out == NULL || out->m_cbSize < kOcrAvailabilityInfoMinSize)
        return ocr_bad_argument;

    const bool hasUriField = out->m_cbSize >= offsetof(OcrAvailabilityInfo, m_settingsUri) + sizeof(out->m_settingsUri);
    if (hasUriField)
        out->m_settingsUri = NULL;

    IOcrBackend* backend = ReadyBackend();
    if (backend == NULL)
    {
        out->m_availability = ocr_engine_failed;
        return ocr_ok;
    }

    try
    {
        const wchar_t* uri = NULL;
        out->m_availability = backend->QueryAvailability(&uri);
        if (hasUriField)
            out->m_settingsUri = uri;
    }
    catch (...)
    {
        OcrLogError("ocr query availability: unexpected exception.");
        out->m_availability = ocr_engine_failed;
    }
    return ocr_ok;
}

uint32_t __cdecl ApiGetLanguageCount()
{
    IOcrBackend* backend = ReadyBackend();
    if (backend == NULL)
        return 0;
    try
    {
        return backend->LanguageCount();
    }
    catch (...)
    {
        return 0;
    }
}

const wchar_t* __cdecl ApiGetLanguageTag(uint32_t index)
{
    IOcrBackend* backend = ReadyBackend();
    if (backend == NULL)
        return NULL;
    try
    {
        return backend->LanguageTag(index);
    }
    catch (...)
    {
        return NULL;
    }
}

const wchar_t* __cdecl ApiGetLanguageName(uint32_t index)
{
    IOcrBackend* backend = ReadyBackend();
    if (backend == NULL)
        return NULL;
    try
    {
        return backend->LanguageName(index);
    }
    catch (...)
    {
        return NULL;
    }
}

const wchar_t* __cdecl ApiGetDefaultLanguage()
{
    IOcrBackend* backend = ReadyBackend();
    if (backend == NULL)
        return NULL;
    try
    {
        return backend->DefaultLanguage();
    }
    catch (...)
    {
        return NULL;
    }
}

//检查输入图像的各字段是否合法
bool IsValidImage(const OcrImage* image)
{
    if (image == NULL || image->m_cbSize < kOcrImageMinSize)
        return false;
    if (image->m_width == 0 || image->m_height == 0 || image->m_pixels == NULL)
        return false;
    //用 64 位计算，避免宽度很大时乘法溢出
    if (static_cast<uint64_t>(image->m_stride) < static_cast<uint64_t>(image->m_width) * kBytesPerPixel)
        return false;
    return true;
}

int __cdecl ApiRecognize(const OcrImage* image, const wchar_t* languageTag, volatile long* cancelFlag,
                         OcrResult** result)
{
    if (result == NULL)
        return ocr_bad_argument;
    *result = NULL;
    if (!IsValidImage(image))
        return ocr_bad_argument;

    IOcrBackend* backend = ReadyBackend();
    if (backend == NULL)
        return ocr_unavailable;

    if (languageTag != NULL && languageTag[0] == L'\0')
        languageTag = NULL;

    try
    {
        const wchar_t* uri = NULL;
        if (backend->QueryAvailability(&uri) != ocr_available)
            return ocr_unavailable;

        OcrResultData data;
        const int code = backend->Recognize(*image, languageTag, cancelFlag, data);
        if (code != ocr_ok)
            return code;

        OcrResult* built = BuildOcrResult(data);
        if (built == NULL)
        {
            OcrLogError("ocr recognize: out of memory while building the result (%u lines).",
                        static_cast<unsigned>(data.m_lines.size()));
            return ocr_out_of_memory;
        }
        *result = built;
        return ocr_ok;
    }
    catch (const std::bad_alloc&)
    {
        OcrLogError("ocr recognize: out of memory.");
        return ocr_out_of_memory;
    }
    catch (...)
    {
        OcrLogError("ocr recognize: unexpected exception.");
        return ocr_engine_error;
    }
}

void __cdecl ApiFreeResult(OcrResult* result)
{
    try
    {
        if (!FreeOcrResult(result))
            OcrLogWarning("ocr free result: pointer was not allocated by this module or was already freed.");
    }
    catch (...)
    {
        OcrLogError("ocr free result: unexpected exception.");
    }
}

//函数表本体；字段顺序与 OcrApi 的声明一致
const OcrApi g_apiTable =
{
    sizeof(OcrApi),
    ApiInit,
    ApiGetEngineName,
    ApiGetEngineVersion,
    ApiQueryAvailability,
    ApiGetLanguageCount,
    ApiGetLanguageTag,
    ApiGetLanguageName,
    ApiGetDefaultLanguage,
    ApiRecognize,
    ApiFreeResult,
};

} // namespace

const OcrApi* GetOcrApiTable()
{
    return &g_apiTable;
}

void InstallOcrBackend(IOcrBackend* backend)
{
    if (backend == NULL)
        return;
    ::AcquireSRWLockExclusive(&g_stateLock);
    if (g_backend == NULL)
        g_backend = backend;
    ::ReleaseSRWLockExclusive(&g_stateLock);
}

IOcrBackend* SetOcrBackendForTest(IOcrBackend* backend)
{
    ::AcquireSRWLockExclusive(&g_stateLock);
    IOcrBackend* previous = g_backend;
    g_backend = backend;
    g_initDone = false;
    g_initResult = ocr_engine_error;
    ::ReleaseSRWLockExclusive(&g_stateLock);
    SetOcrLogSink(NULL);
    return previous;
}

} // namespace flamingoocr
