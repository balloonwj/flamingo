/**
 *  FlamingoOcr.dll 导出函数表的单元测试，用例 OCRA1 ~ OCRA3（docs/图片文字识别方案.md 第 10.1 节）。
 *
 *    - OCRA1 函数表：m_cbSize 等于 sizeof(OcrApi)，函数指针全部非空，两次取得的是同一张表
 *    - OCRA2 参数检查：空图、结构大小不足、宽或高为 0、像素指针为空、行跨度小于宽 ×4、出参为 NULL 时
 *            返回 ocr_bad_argument，不崩溃，出参置为 NULL；未初始化与引擎不可用时返回 ocr_unavailable；
 *            后端抛出的异常不越过函数表
 *    - OCRA3 结果的组装与释放：m_freeResult(NULL) 安全；多行多词的结果各字段与后端给出的一致；
 *            释放后不留泄漏；不是本模块分配的指针被拒绝释放
 *
 *  直接编译 OcrApiTable.cpp 等源文件，换上本文件里的假后端，不依赖系统的识别组件。框架 Catch2 v3，标签 [ocrdll]。
 *  balloonwj@qq.com   2026-10-05
 */
#include "catch_amalgamated.hpp"

#include <windows.h>
#include <new>
#include <stdexcept>
#include <string>
#include <vector>

#include "../FlamingoOcrApi.h"
#include "../OcrApiTable.h"
#include "../OcrBackend.h"
#include "../Core/OcrResultBuilder.h"

using namespace flamingoocr;

namespace {

//测试图像的宽、高（像素）
const uint32_t kImageWidth = 8;
const uint32_t kImageHeight = 4;

//每像素字节数
const uint32_t kBytesPerPixel = 4;

//假后端在 Recognize 里的行为
enum FakeMode
{
    fake_ok = 0,          //返回预先设定的结果
    fake_throw = 1,       //抛出 std::runtime_error
    fake_bad_alloc = 2,   //抛出 std::bad_alloc
};

//假后端：可用性、识别结果与异常都可配置
class FakeBackend : public IOcrBackend
{
public:
    FakeBackend()
        : m_initOk(true)
        , m_initCalls(0)
        , m_availability(ocr_available)
        , m_mode(fake_ok)
        , m_recognizeCalls(0)
    {
    }

    virtual bool Init(const wchar_t* uiLanguage)
    {
        ++m_initCalls;
        m_uiLanguage = (uiLanguage != NULL) ? uiLanguage : L"";
        return m_initOk;
    }
    virtual const wchar_t* EngineName() const
    {
        return L"fake";
    }

    virtual const wchar_t* EngineVersion() const
    {
        return L"9.9";
    }

    virtual int QueryAvailability(const wchar_t** settingsUri)
    {
        *settingsUri = (m_availability == ocr_no_language) ? L"ms-settings:regionlanguage" : NULL;
        return m_availability;
    }

    virtual uint32_t LanguageCount() const
    {
        return 1;
    }

    virtual const wchar_t* LanguageTag(uint32_t index) const
    {
        return index == 0 ? L"zh-Hans-CN" : NULL;
    }

    virtual const wchar_t* LanguageName(uint32_t index) const
    {
        return index == 0 ? L"Chinese" : NULL;
    }

    virtual const wchar_t* DefaultLanguage() const
    {
        return L"zh-Hans-CN";
    }

    virtual int Recognize(const OcrImage& image, const wchar_t* languageTag, volatile long* cancelFlag,
                          OcrResultData& out)
    {
        (void)image;
        (void)cancelFlag;
        ++m_recognizeCalls;
        m_lastLanguage = (languageTag != NULL) ? languageTag : L"(null)";
        if (m_mode == fake_throw)
            throw std::runtime_error("fake failure");
        if (m_mode == fake_bad_alloc)
            throw std::bad_alloc();
        out = m_result;
        return ocr_ok;
    }

    bool          m_initOk;           //Init 的返回值
    int           m_initCalls;        //Init 被调用的次数
    std::wstring  m_uiLanguage;       //Init 收到的界面语言
    int           m_availability;     //QueryAvailability 报告的可用性
    FakeMode      m_mode;             //Recognize 的行为
    int           m_recognizeCalls;   //Recognize 被调用的次数
    std::wstring  m_lastLanguage;     //Recognize 收到的识别语言（NULL 记为 "(null)"）
    OcrResultData m_result;           //Recognize 返回的结果
};

//日志回调收到的全部日志
std::vector<std::string> g_logs;

void __cdecl CollectLog(int level, const char* text)
{
    (void)level;
    g_logs.push_back(text != NULL ? text : "");
}

//在作用域内换上假后端，离开时卸掉，避免影响别的用例
class BackendScope
{
public:
    explicit BackendScope(IOcrBackend* backend)
    {
        SetOcrBackendForTest(backend);
        g_logs.clear();
    }
    ~BackendScope()
    {
        SetOcrBackendForTest(NULL);
    }
};

//按默认参数初始化函数表
int InitApi(const OcrApi* api, const wchar_t* uiLanguage)
{
    OcrHostInfo host;
    host.m_cbSize = sizeof(host);
    host.m_log = CollectLog;
    host.m_uiLanguage = uiLanguage;
    return api->m_init(&host);
}

//一张合法的测试图像
struct TestImage
{
    std::vector<uint8_t> m_pixels;   //像素数据
    OcrImage             m_image;    //指向 m_pixels 的图像描述

    TestImage()
        : m_pixels(kImageWidth * kImageHeight * kBytesPerPixel, 0xFF)
    {
        m_image.m_cbSize = sizeof(OcrImage);
        m_image.m_width = kImageWidth;
        m_image.m_height = kImageHeight;
        m_image.m_stride = kImageWidth * kBytesPerPixel;
        m_image.m_pixels = &m_pixels[0];
    }
};

//构造一个矩形
OcrRect MakeRect(float x, float y, float w, float h)
{
    OcrRect r;
    r.m_x = x;
    r.m_y = y;
    r.m_w = w;
    r.m_h = h;
    return r;
}

//构造一个词
OcrWordData MakeWord(const wchar_t* text, float x, float y, float w, float h)
{
    OcrWordData word;
    word.m_text = text;
    word.m_rect = MakeRect(x, y, w, h);
    return word;
}

//一个指针，表示「出参尚未被函数改写」
OcrResult* SentinelResult()
{
    return reinterpret_cast<OcrResult*>(static_cast<uintptr_t>(0x1));
}

//检查日志全是 ASCII（日志约定，避免编码混杂）
bool AllLogsAscii()
{
    for (size_t i = 0; i < g_logs.size(); ++i)
    {
        for (size_t j = 0; j < g_logs[i].size(); ++j)
        {
            if (static_cast<unsigned char>(g_logs[i][j]) >= 0x80)
                return false;
        }
    }
    return true;
}

} // namespace

TEST_CASE("OCRA1 函数表大小正确、函数指针齐全、每次取得同一张表", "[ocrdll]")
{
    const OcrApi* api = GetOcrApiTable();
    REQUIRE(api != NULL);
    CHECK(api == GetOcrApiTable());
    CHECK(api->m_cbSize == sizeof(OcrApi));
    CHECK(api->m_cbSize >= kOcrApiMinSize);

    CHECK(api->m_init != NULL);
    CHECK(api->m_getEngineName != NULL);
    CHECK(api->m_getEngineVersion != NULL);
    CHECK(api->m_queryAvailability != NULL);
    CHECK(api->m_getLanguageCount != NULL);
    CHECK(api->m_getLanguageTag != NULL);
    CHECK(api->m_getLanguageName != NULL);
    CHECK(api->m_getDefaultLanguage != NULL);
    CHECK(api->m_recognize != NULL);
    CHECK(api->m_freeResult != NULL);

    //m_freeResult 是第一版的最后一个字段
    CHECK(OcrApiHasField(api, kOcrApiMinSize));
    CHECK_FALSE(OcrApiHasField(NULL, kOcrApiMinSize));
}

TEST_CASE("OCRA1 初始化只生效一次，界面语言传给后端，引擎名称来自后端", "[ocrdll]")
{
    FakeBackend backend;
    BackendScope scope(&backend);
    const OcrApi* api = GetOcrApiTable();

    CHECK(InitApi(api, L"en-US") == ocr_ok);
    CHECK(InitApi(api, L"ja-JP") == ocr_ok);
    CHECK(backend.m_initCalls == 1);
    CHECK(backend.m_uiLanguage == L"en-US");
    CHECK(std::wstring(api->m_getEngineName()) == L"fake");
    CHECK(std::wstring(api->m_getEngineVersion()) == L"9.9");
    CHECK(api->m_getLanguageCount() == 1);
    CHECK(std::wstring(api->m_getLanguageTag(0)) == L"zh-Hans-CN");
    CHECK(api->m_getLanguageTag(1) == NULL);
    CHECK(std::wstring(api->m_getDefaultLanguage()) == L"zh-Hans-CN");
}

TEST_CASE("OCRA2 非法参数一律返回 ocr_bad_argument 且出参置空", "[ocrdll]")
{
    FakeBackend backend;
    BackendScope scope(&backend);
    const OcrApi* api = GetOcrApiTable();
    REQUIRE(InitApi(api, L"zh-CN") == ocr_ok);

    //初始化与可用性查询的参数
    CHECK(api->m_init(NULL) == ocr_bad_argument);
    OcrHostInfo tinyHost;
    tinyHost.m_cbSize = 1;
    CHECK(api->m_init(&tinyHost) == ocr_bad_argument);
    CHECK(api->m_queryAvailability(NULL) == ocr_bad_argument);
    OcrAvailabilityInfo tinyInfo;
    tinyInfo.m_cbSize = 1;
    CHECK(api->m_queryAvailability(&tinyInfo) == ocr_bad_argument);

    //出参为 NULL
    TestImage good;
    CHECK(api->m_recognize(&good.m_image, NULL, NULL, NULL) == ocr_bad_argument);

    //图像为 NULL
    OcrResult* result = SentinelResult();
    CHECK(api->m_recognize(NULL, NULL, NULL, &result) == ocr_bad_argument);
    CHECK(result == NULL);

    //结构大小不足
    //变量名不用 small：rpcndr.h 里有 #define small char
    TestImage tooSmall;
    tooSmall.m_image.m_cbSize = sizeof(uint32_t);
    result = SentinelResult();
    CHECK(api->m_recognize(&tooSmall.m_image, NULL, NULL, &result) == ocr_bad_argument);
    CHECK(result == NULL);

    //宽为 0
    TestImage zeroW;
    zeroW.m_image.m_width = 0;
    result = SentinelResult();
    CHECK(api->m_recognize(&zeroW.m_image, NULL, NULL, &result) == ocr_bad_argument);
    CHECK(result == NULL);

    //高为 0
    TestImage zeroH;
    zeroH.m_image.m_height = 0;
    result = SentinelResult();
    CHECK(api->m_recognize(&zeroH.m_image, NULL, NULL, &result) == ocr_bad_argument);
    CHECK(result == NULL);

    //像素指针为空
    TestImage noPixels;
    noPixels.m_image.m_pixels = NULL;
    result = SentinelResult();
    CHECK(api->m_recognize(&noPixels.m_image, NULL, NULL, &result) == ocr_bad_argument);
    CHECK(result == NULL);

    //行跨度小于宽 ×4
    TestImage shortStride;
    shortStride.m_image.m_stride = kImageWidth * kBytesPerPixel - 1;
    result = SentinelResult();
    CHECK(api->m_recognize(&shortStride.m_image, NULL, NULL, &result) == ocr_bad_argument);
    CHECK(result == NULL);

    //宽度极大、乘 4 会溢出 32 位时也能正确判为非法
    TestImage huge;
    huge.m_image.m_width = 0x40000001u;
    huge.m_image.m_stride = 4;
    result = SentinelResult();
    CHECK(api->m_recognize(&huge.m_image, NULL, NULL, &result) == ocr_bad_argument);
    CHECK(result == NULL);

    //以上非法调用都没有进到后端
    CHECK(backend.m_recognizeCalls == 0);
    CHECK(AllLogsAscii());
}

TEST_CASE("OCRA2 未初始化、引擎不可用与后端异常都被函数表拦下", "[ocrdll]")
{
    FakeBackend backend;
    BackendScope scope(&backend);
    const OcrApi* api = GetOcrApiTable();
    TestImage image;

    //未初始化：可用性报告引擎失败，识别返回不可用
    OcrAvailabilityInfo info;
    info.m_cbSize = sizeof(info);
    REQUIRE(api->m_queryAvailability(&info) == ocr_ok);
    CHECK(info.m_availability == ocr_engine_failed);
    CHECK(info.m_settingsUri == NULL);
    OcrResult* result = SentinelResult();
    CHECK(api->m_recognize(&image.m_image, NULL, NULL, &result) == ocr_unavailable);
    CHECK(result == NULL);
    CHECK(api->m_getLanguageCount() == 0);
    CHECK(api->m_getDefaultLanguage() == NULL);

    //初始化后没有识别语言：可用性带出系统设置地址，识别返回不可用
    REQUIRE(InitApi(api, L"zh-CN") == ocr_ok);
    backend.m_availability = ocr_no_language;
    REQUIRE(api->m_queryAvailability(&info) == ocr_ok);
    CHECK(info.m_availability == ocr_no_language);
    REQUIRE(info.m_settingsUri != NULL);
    CHECK(std::wstring(info.m_settingsUri) == L"ms-settings:regionlanguage");
    CHECK(api->m_recognize(&image.m_image, NULL, NULL, &result) == ocr_unavailable);
    CHECK(result == NULL);

    //后端抛出普通异常：转成引擎错误
    backend.m_availability = ocr_available;
    backend.m_mode = fake_throw;
    CHECK(api->m_recognize(&image.m_image, NULL, NULL, &result) == ocr_engine_error);
    CHECK(result == NULL);

    //后端抛出 bad_alloc：转成内存不足
    backend.m_mode = fake_bad_alloc;
    CHECK(api->m_recognize(&image.m_image, NULL, NULL, &result) == ocr_out_of_memory);
    CHECK(result == NULL);

    //空串语言按 NULL 交给后端（表示默认语言）
    backend.m_mode = fake_ok;
    REQUIRE(api->m_recognize(&image.m_image, L"", NULL, &result) == ocr_ok);
    CHECK(backend.m_lastLanguage == L"(null)");
    api->m_freeResult(result);

    CHECK_FALSE(g_logs.empty());   //异常都留下了错误日志
    CHECK(AllLogsAscii());
}

TEST_CASE("OCRA2 后端初始化失败时 m_init 报告失败，之后一律不可用", "[ocrdll]")
{
    FakeBackend backend;
    backend.m_initOk = false;
    BackendScope scope(&backend);
    const OcrApi* api = GetOcrApiTable();

    CHECK(InitApi(api, L"zh-CN") == ocr_engine_error);
    CHECK(InitApi(api, L"zh-CN") == ocr_engine_error);
    CHECK(backend.m_initCalls == 1);

    OcrAvailabilityInfo info;
    info.m_cbSize = sizeof(info);
    REQUIRE(api->m_queryAvailability(&info) == ocr_ok);
    CHECK(info.m_availability == ocr_engine_failed);

    TestImage image;
    OcrResult* result = SentinelResult();
    CHECK(api->m_recognize(&image.m_image, NULL, NULL, &result) == ocr_unavailable);
    CHECK(result == NULL);
}

TEST_CASE("OCRA3 多行多词的结果完整交给宿主，释放后不留泄漏", "[ocrdll]")
{
    FakeBackend backend;
    OcrLineData line1;
    line1.m_text = L"会议时间";
    line1.m_rect = MakeRect(10, 20, 80, 16);
    line1.m_words.push_back(MakeWord(L"会", 10, 20, 16, 16));
    line1.m_words.push_back(MakeWord(L"议", 30, 20, 16, 16));
    line1.m_words.push_back(MakeWord(L"时", 50, 20, 16, 16));
    line1.m_words.push_back(MakeWord(L"间", 74, 20, 16, 16));
    OcrLineData line2;
    line2.m_text = L"Please reply";
    line2.m_rect = MakeRect(10, 50, 120, 14);
    line2.m_words.push_back(MakeWord(L"Please", 10, 50, 60, 14));
    line2.m_words.push_back(MakeWord(L"reply", 76, 50, 54, 14));
    OcrLineData emptyLine;   //没有词的行也要能原样传过去
    emptyLine.m_text = L"";
    backend.m_result.m_lines.push_back(line1);
    backend.m_result.m_lines.push_back(line2);
    backend.m_result.m_lines.push_back(emptyLine);
    backend.m_result.m_languageTag = L"zh-Hans-CN";
    backend.m_result.m_textAngle = 1.5f;
    backend.m_result.m_scalePercent = 200;

    BackendScope scope(&backend);
    const OcrApi* api = GetOcrApiTable();
    REQUIRE(InitApi(api, L"zh-CN") == ocr_ok);

    const long liveBefore = OcrResultLiveCount();
    TestImage image;
    OcrResult* result = NULL;
    REQUIRE(api->m_recognize(&image.m_image, L"zh-Hans-CN", NULL, &result) == ocr_ok);
    REQUIRE(result != NULL);
    CHECK(OcrResultLiveCount() == liveBefore + 1);
    CHECK(backend.m_lastLanguage == L"zh-Hans-CN");

    CHECK(result->m_cbSize == sizeof(OcrResult));
    REQUIRE(result->m_lineCount == 3);
    REQUIRE(result->m_lines != NULL);
    CHECK(std::wstring(result->m_languageTag) == L"zh-Hans-CN");
    CHECK(result->m_textAngle == Catch::Approx(1.5f));
    CHECK(result->m_scalePercent == 200);

    const OcrLine& l1 = result->m_lines[0];
    CHECK(std::wstring(l1.m_text) == L"会议时间");
    CHECK(l1.m_rect.m_x == Catch::Approx(10));
    CHECK(l1.m_rect.m_y == Catch::Approx(20));
    CHECK(l1.m_rect.m_w == Catch::Approx(80));
    CHECK(l1.m_rect.m_h == Catch::Approx(16));
    REQUIRE(l1.m_wordCount == 4);
    CHECK(std::wstring(l1.m_words[0].m_text) == L"会");
    CHECK(std::wstring(l1.m_words[3].m_text) == L"间");
    CHECK(l1.m_words[3].m_rect.m_x == Catch::Approx(74));

    const OcrLine& l2 = result->m_lines[1];
    CHECK(std::wstring(l2.m_text) == L"Please reply");
    REQUIRE(l2.m_wordCount == 2);
    CHECK(std::wstring(l2.m_words[1].m_text) == L"reply");
    CHECK(l2.m_words[1].m_rect.m_w == Catch::Approx(54));

    const OcrLine& l3 = result->m_lines[2];
    CHECK(std::wstring(l3.m_text).empty());
    CHECK(l3.m_wordCount == 0);
    CHECK(l3.m_words == NULL);

    api->m_freeResult(result);
    CHECK(OcrResultLiveCount() == liveBefore);
}

TEST_CASE("OCRA3 释放 NULL 是安全的，外来指针被拒绝释放", "[ocrdll]")
{
    FakeBackend backend;
    BackendScope scope(&backend);
    const OcrApi* api = GetOcrApiTable();
    REQUIRE(InitApi(api, L"zh-CN") == ocr_ok);

    const long liveBefore = OcrResultLiveCount();
    api->m_freeResult(NULL);
    CHECK(OcrResultLiveCount() == liveBefore);

    //一块全为 0 的内存冒充结果：校验头对不上，必须拒绝释放（否则会把栈上的内存交给 free）
    uint64_t fake[16] = { 0 };
    OcrResult* foreign = reinterpret_cast<OcrResult*>(&fake[8]);
    CHECK_FALSE(FreeOcrResult(foreign));
    api->m_freeResult(foreign);
    CHECK(OcrResultLiveCount() == liveBefore);
    CHECK_FALSE(g_logs.empty());   //拒绝时留下了警告日志

    //没有识别出文字的结果：0 行、m_lines 为 NULL
    OcrResult* result = NULL;
    TestImage image;
    REQUIRE(api->m_recognize(&image.m_image, NULL, NULL, &result) == ocr_ok);
    REQUIRE(result != NULL);
    CHECK(result->m_lineCount == 0);
    CHECK(result->m_lines == NULL);
    CHECK(std::wstring(result->m_languageTag).empty());
    api->m_freeResult(result);
    CHECK(OcrResultLiveCount() == liveBefore);
}
