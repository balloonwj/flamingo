/**
 *  基于 Windows.Media.Ocr 的识别引擎后端，说明见 WinOcrEngine.h。
 *
 *  只用 C++11 语法、经 WRL 的 ComPtr 调 WinRT 的 ABI 接口（与 FlamingoMedia/Share/WgcWindowCapturer.cpp 相同）。
 *  本翻译单元自己把系统版本宏定到 Win10，只为让 WinRT 头文件声明出所需类型；WinRT 函数一律经 CWinRtRuntime
 *  在运行时取用，DLL 在 Win7 上仍能加载。对外接口只用普通类型，与其它翻译单元的系统版本宏无关。
 *
 *  等待识别完成用轮询 IAsyncInfo 的状态，不注册完成回调：取消时不必担心回调晚到、访问已析构的对象。
 *
 *  balloonwj@qq.com   2026-10-05
 */
#ifdef _WIN32_WINNT
#undef _WIN32_WINNT
#endif
#define _WIN32_WINNT 0x0A00   //Windows 10：只影响本翻译单元能看到的系统声明，运行期接口仍按需动态取用
#ifdef WINVER
#undef WINVER
#endif
#define WINVER 0x0A00

#include "WinOcrEngine.h"

#include <windows.h>
#include <objbase.h>
#include <wrl/client.h>
#include <windows.foundation.h>
#include <windows.foundation.collections.h>
#include <windows.globalization.h>
#include <windows.graphics.imaging.h>
#include <windows.media.ocr.h>
#include <MemoryBuffer.h>
#include <math.h>
#include <string.h>

#include "WinRtRuntime.h"
#include "OcrBitmapScaler.h"
#include "OcrLanguagePolicy.h"
#include "OcrLineAssembler.h"
#include "OcrScalePolicy.h"
#include "../Core/OcrHostLog.h"

using Microsoft::WRL::ComPtr;
namespace AWF = ABI::Windows::Foundation;
namespace AWFC = ABI::Windows::Foundation::Collections;
namespace AWG = ABI::Windows::Globalization;
namespace AWGI = ABI::Windows::Graphics::Imaging;
namespace AWMO = ABI::Windows::Media::Ocr;

namespace flamingoocr {

namespace {

//引擎名称与版本（写日志用）
const wchar_t* const kEngineName = L"Windows.Media.Ocr";
const wchar_t* const kEngineVersion = L"1.0.0";

//没有识别语言时「打开系统设置」的地址：语言与区域设置页，在那里添加语言即可装上对应的识别组件
const wchar_t* const kLanguageSettingsUri = L"ms-settings:regionlanguage";

//每像素字节数（32 位 BGRA）
const uint32_t kBytesPerPixel = 4;

//等待识别完成时每次轮询之间的间隔，单位毫秒
const DWORD kPollIntervalMs = 5;

//一遍识别允许的最长时间，单位毫秒；超时视为引擎错误（第 0 期实测 8000×6000 的图约 4 秒）
const DWORD kRecognizeTimeoutMs = 60000;

//请求取消后等待引擎停下的最长时间，单位毫秒
const DWORD kCancelWaitMs = 3000;

//引擎没有报告上限时使用的最长边（像素），与 Win10 的实测值相同
const uint32_t kDefaultMaxDimension = 10000;

//百分比换算
const float kPercent = 100.0f;

//第二遍是否启用（测试可关闭）
volatile LONG g_secondPassEnabled = 1;

//在作用域内初始化 COM（多线程套间）；调用线程已初始化过（无论哪种套间）时照常工作
class CComScope
{
public:
    CComScope()
    {
        HRESULT hr = ::CoInitializeEx(NULL, COINIT_MULTITHREADED);
        m_needUninit = SUCCEEDED(hr);   //S_OK 与 S_FALSE 都要配对调用 CoUninitialize；RPC_E_CHANGED_MODE 不用
    }
    ~CComScope()
    {
        if (m_needUninit)
            ::CoUninitialize();
    }

private:
    bool m_needUninit;   //是否需要在析构时调用 CoUninitialize
};

//关闭实现了 IClosable 的 WinRT 对象，提前释放其占用的资源
void CloseObject(IUnknown* obj)
{
    if (obj == NULL)
        return;
    ComPtr<AWF::IClosable> closable;
    if (SUCCEEDED(obj->QueryInterface(IID_PPV_ARGS(&closable))))
        closable->Close();
}

//取消标志是否已置位
bool IsCancelled(volatile long* cancelFlag)
{
    return cancelFlag != NULL && *cancelFlag != 0;
}

//一遍识别的结果：各行的词（坐标为本遍所用图像的像素）与倾斜角
struct PassResult
{
    std::vector<std::vector<OcrWordBox> > m_lines;   //各行的词
    float                                 m_angle;   //文字倾斜角（度）
    DWORD                                 m_ms;      //本遍耗时（毫秒）

    PassResult()
        : m_angle(0.0f)
        , m_ms(0)
    {
    }
};

//一张待识别的图像：紧凑排列的 BGRA 像素
struct Bgra
{
    std::vector<uint8_t> m_pixels;   //像素，行跨度为 m_width × 4
    uint32_t             m_width;    //宽（像素）
    uint32_t             m_height;   //高（像素）

    Bgra()
        : m_width(0)
        , m_height(0)
    {
    }
};

class CWinOcrEngine : public IOcrBackend
{
public:
    CWinOcrEngine()
        : m_availability(ocr_engine_failed)
        , m_maxDimension(kDefaultMaxDimension)
    {
    }

    virtual bool Init(const wchar_t* uiLanguage)
    {
        CComScope com;
        const CWinRtRuntime& rt = CWinRtRuntime::Get();
        if (!rt.IsAvailable())
        {
            m_availability = ocr_os_unsupported;
            OcrLogInfo("winocr init: WinRT entry points not found in combase.dll, os unsupported.");
            return true;
        }

        ComPtr<AWMO::IOcrEngineStatics> statics;
        HRESULT hr = rt.GetActivationFactory(RuntimeClass_Windows_Media_Ocr_OcrEngine, IID_PPV_ARGS(&statics));
        if (FAILED(hr) || !statics)
        {
            m_availability = ocr_os_unsupported;
            OcrLogInfo("winocr init: OcrEngine activation factory unavailable (hr=0x%08X), os unsupported.",
                       static_cast<unsigned>(hr));
            return true;
        }

        UINT32 maxDimension = 0;
        if (SUCCEEDED(statics->get_MaxImageDimension(&maxDimension)) && maxDimension > 0)
            m_maxDimension = maxDimension;

        LoadLanguages(statics.Get());
        if (m_tags.empty())
        {
            m_availability = ocr_no_language;
            OcrLogWarning("winocr init: no recognizer language installed.");
            return true;
        }

        PickDefaultLanguage(statics.Get(), uiLanguage);

        if (!EnsureOcrGdiplus())
        {
            m_availability = ocr_engine_failed;
            OcrLogError("winocr init: GdiplusStartup failed.");
            return true;
        }

        m_availability = ocr_available;
        std::string tags;
        for (size_t i = 0; i < m_tags.size(); ++i)
        {
            if (i > 0)
                tags += ",";
            tags += Narrow(m_tags[i]);
        }
        OcrLogInfo("winocr init: available, max dimension %u, languages [%s], default %s.",
                   static_cast<unsigned>(m_maxDimension), tags.c_str(), Narrow(m_defaultLanguage).c_str());
        return true;
    }

    virtual const wchar_t* EngineName() const
    {
        return kEngineName;
    }

    virtual const wchar_t* EngineVersion() const
    {
        return kEngineVersion;
    }

    virtual int QueryAvailability(const wchar_t** settingsUri)
    {
        if (settingsUri != NULL)
            *settingsUri = (m_availability == ocr_no_language) ? kLanguageSettingsUri : NULL;
        return m_availability;
    }

    virtual uint32_t LanguageCount() const
    {
        return static_cast<uint32_t>(m_tags.size());
    }

    virtual const wchar_t* LanguageTag(uint32_t index) const
    {
        return (index < m_tags.size()) ? m_tags[index].c_str() : NULL;
    }

    virtual const wchar_t* LanguageName(uint32_t index) const
    {
        return (index < m_names.size()) ? m_names[index].c_str() : NULL;
    }

    virtual const wchar_t* DefaultLanguage() const
    {
        return m_defaultLanguage.empty() ? NULL : m_defaultLanguage.c_str();
    }

    virtual int Recognize(const OcrImage& image, const wchar_t* languageTag, volatile long* cancelFlag,
                          OcrResultData& out)
    {
        if (m_availability != ocr_available)
            return ocr_unavailable;
        if (IsCancelled(cancelFlag))
            return ocr_cancelled;

        //识别语言：未指定时用默认语言；指定的必须在可用列表里
        std::wstring tag = (languageTag != NULL) ? std::wstring(languageTag) : m_defaultLanguage;
        int tagIndex = -1;
        for (size_t i = 0; i < m_tags.size(); ++i)
        {
            if (SameLanguageTag(m_tags[i], tag))
                tagIndex = static_cast<int>(i);
        }
        if (tagIndex < 0)
        {
            OcrLogWarning("winocr recognize: language %s is not available.", Narrow(tag).c_str());
            return ocr_language_unsupported;
        }
        tag = m_tags[tagIndex];

        CComScope com;
        const CWinRtRuntime& rt = CWinRtRuntime::Get();
        ComPtr<AWMO::IOcrEngine> engine;
        HRESULT hr = CreateEngine(rt, tag, engine);
        if (FAILED(hr) || !engine)
        {
            OcrLogError("winocr recognize: cannot create engine for %s (hr=0x%08X).", Narrow(tag).c_str(),
                        static_cast<unsigned>(hr));
            return ocr_language_unsupported;
        }

        //第一遍所用的图像：复制、铺白底，最长边超过上限时先缩小
        Bgra base;
        const float downscale = ComputeDownscale(image.m_width, image.m_height, m_maxDimension);
        if (!PrepareBase(image, downscale, base))
        {
            OcrLogError("winocr recognize: failed to prepare a %ux%u image.", image.m_width, image.m_height);
            return ocr_out_of_memory;
        }

        PassResult first;
        int code = RunPass(rt, engine.Get(), base, cancelFlag, first);
        if (code != ocr_ok)
            return code;

        //第二遍：按第一遍的行高中位数决定放大倍数
        const float median = MedianLineHeight(first.m_lines);
        uint32_t factor = ChooseUpscaleFactor(median, base.m_width, base.m_height, m_maxDimension);
        if (g_secondPassEnabled == 0)
            factor = 1;

        const PassResult* used = &first;
        PassResult second;
        float totalScale = downscale;
        if (factor > 1)
        {
            if (IsCancelled(cancelFlag))
                return ocr_cancelled;
            Bgra up;
            up.m_width = base.m_width * factor;
            up.m_height = base.m_height * factor;
            if (!ScaleBgra(&base.m_pixels[0], base.m_width, base.m_height, base.m_width * kBytesPerPixel,
                           up.m_width, up.m_height, up.m_pixels))
            {
                OcrLogError("winocr recognize: upscale x%u failed, keeping the first pass.", factor);
                factor = 1;
            }
            else
            {
                code = RunPass(rt, engine.Get(), up, cancelFlag, second);
                if (code == ocr_cancelled)
                    return code;
                //第二遍失败或一行都没读出来而第一遍有结果时，保留第一遍
                if (code == ocr_ok && (!second.m_lines.empty() || first.m_lines.empty()))
                {
                    used = &second;
                    totalScale = downscale * static_cast<float>(factor);
                }
                else
                {
                    factor = 1;
                }
            }
        }

        FillResult(*used, totalScale, tag, out);
        OcrLogInfo("winocr recognize: %ux%u, %s, downscale %.3f, pass1 %u lines %u ms (median line height %.1f), "
                   "upscale x%u%s, %u lines in result.",
                   image.m_width, image.m_height, Narrow(tag).c_str(), downscale,
                   static_cast<unsigned>(first.m_lines.size()), static_cast<unsigned>(first.m_ms), median, factor,
                   (used == &second) ? FormatPass(second).c_str() : "", static_cast<unsigned>(out.m_lines.size()));
        return ocr_ok;
    }

private:
    //把宽字符串里的 ASCII 部分转成窄字符串（语言代码都是 ASCII），写日志用；非 ASCII 字符记为 '?'
    static std::string Narrow(const std::wstring& text)
    {
        std::string out;
        out.reserve(text.size());
        for (size_t i = 0; i < text.size(); ++i)
            out += (text[i] < 0x80) ? static_cast<char>(text[i]) : '?';
        return out;
    }

    //第二遍的日志片段
    static std::string FormatPass(const PassResult& pass)
    {
        char buf[64];
        _snprintf_s(buf, sizeof(buf), _TRUNCATE, ", pass2 %u lines %u ms", static_cast<unsigned>(pass.m_lines.size()),
                    static_cast<unsigned>(pass.m_ms));
        return buf;
    }

    //列出可用的识别语言（代码与显示名）
    void LoadLanguages(AWMO::IOcrEngineStatics* statics)
    {
        m_tags.clear();
        m_names.clear();
        ComPtr<AWFC::IVectorView<AWG::Language*> > languages;
        HRESULT hr = statics->get_AvailableRecognizerLanguages(&languages);
        if (FAILED(hr) || !languages)
        {
            OcrLogError("winocr init: AvailableRecognizerLanguages failed (hr=0x%08X).", static_cast<unsigned>(hr));
            return;
        }
        unsigned count = 0;
        languages->get_Size(&count);
        for (unsigned i = 0; i < count; ++i)
        {
            ComPtr<AWG::ILanguage> language;
            if (FAILED(languages->GetAt(i, &language)) || !language)
                continue;
            CHStringHolder tag;
            CHStringHolder name;
            language->get_LanguageTag(tag.Put());
            language->get_DisplayName(name.Put());
            std::wstring tagText = tag.ToWString();
            if (tagText.empty())
                continue;
            m_tags.push_back(tagText);
            m_names.push_back(name.ToWString());
        }
    }

    //选默认识别语言：先按界面语言找，找不到用系统用户语言对应的引擎，再不行取列表第一项
    void PickDefaultLanguage(AWMO::IOcrEngineStatics* statics, const wchar_t* uiLanguage)
    {
        int index = FindOcrLanguageByPrefix(m_tags, OcrLanguagePrefixForUi(uiLanguage));
        if (index >= 0)
        {
            m_defaultLanguage = m_tags[index];
            return;
        }

        ComPtr<AWMO::IOcrEngine> profileEngine;
        if (SUCCEEDED(statics->TryCreateFromUserProfileLanguages(&profileEngine)) && profileEngine)
        {
            ComPtr<AWG::ILanguage> language;
            if (SUCCEEDED(profileEngine->get_RecognizerLanguage(&language)) && language)
            {
                CHStringHolder tag;
                language->get_LanguageTag(tag.Put());
                std::wstring tagText = tag.ToWString();
                for (size_t i = 0; i < m_tags.size(); ++i)
                {
                    if (SameLanguageTag(m_tags[i], tagText))
                    {
                        m_defaultLanguage = m_tags[i];
                        return;
                    }
                }
            }
        }
        m_defaultLanguage = m_tags[0];
    }

    //按语言代码创建引擎
    static HRESULT CreateEngine(const CWinRtRuntime& rt, const std::wstring& tag, ComPtr<AWMO::IOcrEngine>& engine)
    {
        ComPtr<AWMO::IOcrEngineStatics> statics;
        HRESULT hr = rt.GetActivationFactory(RuntimeClass_Windows_Media_Ocr_OcrEngine, IID_PPV_ARGS(&statics));
        if (FAILED(hr))
            return hr;
        ComPtr<AWG::ILanguageFactory> languageFactory;
        hr = rt.GetActivationFactory(RuntimeClass_Windows_Globalization_Language, IID_PPV_ARGS(&languageFactory));
        if (FAILED(hr))
            return hr;

        HSTRING_HEADER header;
        HSTRING tagString = NULL;
        hr = rt.CreateStringReference(tag.c_str(), &header, &tagString);
        if (FAILED(hr))
            return hr;
        ComPtr<AWG::ILanguage> language;
        hr = languageFactory->CreateLanguage(tagString, &language);
        if (FAILED(hr))
            return hr;
        hr = statics->TryCreateFromLanguage(language.Get(), &engine);
        if (SUCCEEDED(hr) && !engine)
            hr = E_FAIL;   //语言不受支持时接口返回成功但给出空指针
        return hr;
    }

    //复制输入图像并铺白底；需要时缩小
    static bool PrepareBase(const OcrImage& image, float downscale, Bgra& base)
    {
        Bgra copy;
        copy.m_width = image.m_width;
        copy.m_height = image.m_height;
        const size_t rowBytes = static_cast<size_t>(image.m_width) * kBytesPerPixel;
        copy.m_pixels.resize(rowBytes * image.m_height);
        for (uint32_t y = 0; y < image.m_height; ++y)
            memcpy(&copy.m_pixels[y * rowBytes], image.m_pixels + static_cast<size_t>(y) * image.m_stride, rowBytes);
        FlattenOnWhite(&copy.m_pixels[0], copy.m_width, copy.m_height, static_cast<uint32_t>(rowBytes));

        if (downscale >= 1.0f)
        {
            base.m_width = copy.m_width;
            base.m_height = copy.m_height;
            base.m_pixels.swap(copy.m_pixels);
            return true;
        }

        ScaledSize(copy.m_width, copy.m_height, downscale, base.m_width, base.m_height);
        return ScaleBgra(&copy.m_pixels[0], copy.m_width, copy.m_height, static_cast<uint32_t>(rowBytes),
                         base.m_width, base.m_height, base.m_pixels);
    }

    //把像素拷进一个新建的 SoftwareBitmap（BGRA8）
    static HRESULT CreateSoftwareBitmap(const CWinRtRuntime& rt, const Bgra& image, ComPtr<AWGI::ISoftwareBitmap>& bitmap)
    {
        ComPtr<AWGI::ISoftwareBitmapFactory> factory;
        HRESULT hr = rt.GetActivationFactory(RuntimeClass_Windows_Graphics_Imaging_SoftwareBitmap, IID_PPV_ARGS(&factory));
        if (FAILED(hr))
            return hr;
        //像素已铺白底、完全不透明，预乘与否结果相同；BGRA8 + 预乘是识别引擎直接接受的格式
        hr = factory->CreateWithAlpha(AWGI::BitmapPixelFormat_Bgra8, static_cast<INT32>(image.m_width),
                                      static_cast<INT32>(image.m_height), AWGI::BitmapAlphaMode_Premultiplied, &bitmap);
        if (FAILED(hr))
            return hr;

        ComPtr<AWGI::IBitmapBuffer> buffer;
        hr = bitmap->LockBuffer(AWGI::BitmapBufferAccessMode_Write, &buffer);
        if (FAILED(hr))
            return hr;
        AWGI::BitmapPlaneDescription plane;
        hr = buffer->GetPlaneDescription(0, &plane);
        ComPtr<AWF::IMemoryBuffer> memory;
        if (SUCCEEDED(hr))
            hr = buffer.As(&memory);
        ComPtr<AWF::IMemoryBufferReference> reference;
        if (SUCCEEDED(hr))
            hr = memory->CreateReference(&reference);
        ComPtr< ::Windows::Foundation::IMemoryBufferByteAccess> access;
        if (SUCCEEDED(hr))
            hr = reference.As(&access);
        BYTE* data = NULL;
        UINT32 capacity = 0;
        if (SUCCEEDED(hr))
            hr = access->GetBuffer(&data, &capacity);

        if (SUCCEEDED(hr))
        {
            const size_t rowBytes = static_cast<size_t>(image.m_width) * kBytesPerPixel;
            const size_t need = static_cast<size_t>(plane.StartIndex)
                                + static_cast<size_t>(plane.Stride) * (image.m_height - 1) + rowBytes;
            if (data == NULL || plane.Stride < static_cast<INT32>(rowBytes) || need > capacity)
            {
                hr = E_UNEXPECTED;
            }
            else
            {
                for (uint32_t y = 0; y < image.m_height; ++y)
                {
                    memcpy(data + plane.StartIndex + static_cast<size_t>(y) * plane.Stride, &image.m_pixels[y * rowBytes],
                           rowBytes);
                }
            }
        }
        //先关闭引用、再关闭缓冲，位图才会解锁
        CloseObject(reference.Get());
        CloseObject(buffer.Get());
        return hr;
    }

    //识别一遍：建位图、发起异步识别、轮询等待、取出各行各词
    static int RunPass(const CWinRtRuntime& rt, AWMO::IOcrEngine* engine, const Bgra& image, volatile long* cancelFlag,
                       PassResult& pass)
    {
        const DWORD start = ::GetTickCount();
        ComPtr<AWGI::ISoftwareBitmap> bitmap;
        HRESULT hr = CreateSoftwareBitmap(rt, image, bitmap);
        if (FAILED(hr))
        {
            OcrLogError("winocr pass: cannot create a %ux%u SoftwareBitmap (hr=0x%08X).", image.m_width, image.m_height,
                        static_cast<unsigned>(hr));
            return ocr_engine_error;
        }

        ComPtr<AWF::IAsyncOperation<AWMO::OcrResult*> > operation;
        hr = engine->RecognizeAsync(bitmap.Get(), &operation);
        if (FAILED(hr) || !operation)
        {
            OcrLogError("winocr pass: RecognizeAsync failed (hr=0x%08X).", static_cast<unsigned>(hr));
            CloseObject(bitmap.Get());
            return ocr_engine_error;
        }
        ComPtr<AWF::IAsyncInfo> info;
        hr = operation.As(&info);
        if (FAILED(hr))
        {
            CloseObject(bitmap.Get());
            return ocr_engine_error;
        }

        AsyncStatus status = AsyncStatus::Started;
        bool cancelRequested = false;
        DWORD cancelAt = 0;
        for (;;)
        {
            if (FAILED(info->get_Status(&status)) || status != AsyncStatus::Started)
                break;
            const DWORD now = ::GetTickCount();
            if (!cancelRequested && (IsCancelled(cancelFlag) || now - start > kRecognizeTimeoutMs))
            {
                info->Cancel();
                cancelRequested = true;
                cancelAt = now;
            }
            if (cancelRequested && now - cancelAt > kCancelWaitMs)
                break;   //引擎迟迟不停：放弃等待，异步操作由引擎自己收尾
            ::Sleep(kPollIntervalMs);
        }
        pass.m_ms = ::GetTickCount() - start;

        int code = ocr_ok;
        if (status == AsyncStatus::Completed && !cancelRequested)
        {
            ComPtr<AWMO::IOcrResult> result;
            hr = operation->GetResults(&result);
            if (FAILED(hr) || !result)
            {
                OcrLogError("winocr pass: GetResults failed (hr=0x%08X).", static_cast<unsigned>(hr));
                code = ocr_engine_error;
            }
            else
            {
                ReadResult(rt, result.Get(), pass);
            }
        }
        else if (cancelRequested && IsCancelled(cancelFlag))
        {
            code = ocr_cancelled;
        }
        else if (cancelRequested)
        {
            OcrLogError("winocr pass: timed out after %u ms.", static_cast<unsigned>(pass.m_ms));
            code = ocr_engine_error;
        }
        else
        {
            HRESULT error = S_OK;
            info->get_ErrorCode(&error);
            OcrLogError("winocr pass: recognition ended with status %d (hr=0x%08X).", static_cast<int>(status),
                        static_cast<unsigned>(error));
            code = ocr_engine_error;
        }
        info->Close();
        CloseObject(bitmap.Get());
        return code;
    }

    //取出各行各词与倾斜角
    static void ReadResult(const CWinRtRuntime& rt, AWMO::IOcrResult* result, PassResult& pass)
    {
        (void)rt;
        ComPtr<AWF::IReference<double> > angle;
        if (SUCCEEDED(result->get_TextAngle(&angle)) && angle)
        {
            double value = 0.0;
            if (SUCCEEDED(angle->get_Value(&value)))
                pass.m_angle = static_cast<float>(value);
        }

        ComPtr<AWFC::IVectorView<AWMO::OcrLine*> > lines;
        if (FAILED(result->get_Lines(&lines)) || !lines)
            return;
        unsigned lineCount = 0;
        lines->get_Size(&lineCount);
        for (unsigned i = 0; i < lineCount; ++i)
        {
            ComPtr<AWMO::IOcrLine> line;
            if (FAILED(lines->GetAt(i, &line)) || !line)
                continue;
            ComPtr<AWFC::IVectorView<AWMO::OcrWord*> > words;
            if (FAILED(line->get_Words(&words)) || !words)
                continue;
            unsigned wordCount = 0;
            words->get_Size(&wordCount);
            std::vector<OcrWordBox> boxes;
            for (unsigned j = 0; j < wordCount; ++j)
            {
                ComPtr<AWMO::IOcrWord> word;
                if (FAILED(words->GetAt(j, &word)) || !word)
                    continue;
                OcrWordBox box;
                AWF::Rect rect;
                if (SUCCEEDED(word->get_BoundingRect(&rect)))
                {
                    box.m_rect.m_x = rect.X;
                    box.m_rect.m_y = rect.Y;
                    box.m_rect.m_w = rect.Width;
                    box.m_rect.m_h = rect.Height;
                }
                CHStringHolder text;
                word->get_Text(text.Put());
                box.m_text = text.ToWString();
                boxes.push_back(box);
            }
            if (!boxes.empty())
                pass.m_lines.push_back(boxes);
        }
    }

    //把选定那一遍的结果换算回原图坐标、拼行整理后填进输出
    static void FillResult(const PassResult& pass, float totalScale, const std::wstring& tag, OcrResultData& out)
    {
        out.m_lines.clear();
        out.m_languageTag = tag;
        out.m_textAngle = pass.m_angle;
        out.m_scalePercent = static_cast<uint32_t>(floor(totalScale * kPercent + 0.5f));
        for (size_t i = 0; i < pass.m_lines.size(); ++i)
        {
            std::vector<OcrWordBox> mapped = SortWordsByX(pass.m_lines[i]);
            for (size_t j = 0; j < mapped.size(); ++j)
                mapped[j].m_rect = MapRectToSource(mapped[j].m_rect, totalScale);

            OcrLineData line;
            line.m_text = AssembleLineText(mapped);
            line.m_rect = UnionRect(mapped);
            for (size_t j = 0; j < mapped.size(); ++j)
            {
                OcrWordData word;
                word.m_text = mapped[j].m_text;
                word.m_rect = mapped[j].m_rect;
                line.m_words.push_back(word);
            }
            out.m_lines.push_back(line);
        }
    }

    int                       m_availability;      //初始化后的可用性，取值见 enum OcrAvailability
    uint32_t                  m_maxDimension;      //引擎允许的最长边（像素）
    std::vector<std::wstring> m_tags;              //可用识别语言的代码
    std::vector<std::wstring> m_names;             //可用识别语言的显示名，与 m_tags 一一对应
    std::wstring              m_defaultLanguage;   //默认识别语言的代码；没有可用语言时为空
};

} // namespace

IOcrBackend* GetWinOcrBackend()
{
    static CWinOcrEngine s_engine;
    return &s_engine;
}

void SetWinOcrSecondPassEnabledForTest(bool enabled)
{
    ::InterlockedExchange(&g_secondPassEnabled, enabled ? 1 : 0);
}

} // namespace flamingoocr
