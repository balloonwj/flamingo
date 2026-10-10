/**
 *  FlamingoOcr.dll（图片文字识别引擎）与宿主之间的接口约定。
 *
 *  FlamingoOcr.dll 把图片里的文字识别出来，第一个引擎是 Windows 10 起自带的 Windows.Media.Ocr（方案见
 *  docs/图片文字识别方案.md）。宿主（Flamingo.exe）在登录成功后于后台线程用 LoadLibrary 加载它，经唯一的
 *  导出函数 FlamingoOcr_GetApi 取得一张函数表；DLL 不存在或加载失败时，宿主不显示任何识别入口。
 *
 *  本接口同时是「替换引擎」的约定：任何实现了本头文件所述函数表的同名 DLL，放进安装目录即可替换识别引擎，
 *  宿主不用修改。为此与 FlamingoLang 一样，<u>不</u>要求双方版本号完全相同：
 *    - 带 m_cbSize 的结构都以它开头，取值为填写方编译时的结构大小；
 *    - 以后只在这些结构的<u>末尾</u>追加字段，不修改、不删除、不重排已有字段；
 *    - 读取某个字段之前，先按 m_cbSize 判断对方是否提供了它（见 OcrApiHasField）；
 *    - OcrWord、OcrLine、OcrRect 以数组形式出现，大小永久固定，不再追加字段；需要扩展时在 OcrResult 末尾
 *      追加新的并列数组。
 *
 *  两边都静态链接 CRT、各有独立的堆，因此接口只使用 C 类型：
 *    - m_recognize 返回的结果整块归 DLL 所有，只能经 m_freeResult 释放；
 *    - DLL 返回的其它字符串指针归 DLL 所有，在 DLL 卸载之前一直有效（宿主加载后不卸载）；
 *    - 宿主传入的指针只在本次调用期间有效；
 *    - 不传递 STL 对象，异常不越过模块边界（每个导出函数都在边界上捕获并转成返回码）。
 *
 *  线程约定：m_init、m_queryAvailability 与语言查询在宿主的预加载线程上调用；m_recognize 在宿主的识别线程上
 *  调用，宿主保证同一时刻最多只有一个 m_recognize 调用；m_log 回调可以在任意线程发生。调用 m_recognize 的
 *  线程已由宿主初始化 COM（多线程套间）。
 *
 *  典型用法（宿主侧）：
 *
 *      FlamingoOcrGetApiFn getApi = (FlamingoOcrGetApiFn)::GetProcAddress(hDll, FLAMINGO_OCR_ENTRY_NAME);
 *      const flamingoocr::OcrApi* api = (getApi != NULL) ? getApi() : NULL;
 *      if (api != NULL && api->m_cbSize >= flamingoocr::kOcrApiMinSize && api->m_init(&host) == flamingoocr::ocr_ok)
 *      {
 *          flamingoocr::OcrResult* result = NULL;
 *          if (api->m_recognize(&image, NULL, &cancelFlag, &result) == flamingoocr::ocr_ok)
 *          {
 *              ...读取 result->m_lines...
 *              api->m_freeResult(result);
 *          }
 *      }
 *
 *  balloonwj@qq.com   2026-10-05
 */
#pragma once

#include <stddef.h>
#include <stdint.h>

//DLL 文件名，与宿主 exe 放在同一目录
#define FLAMINGO_OCR_DLL_NAME      L"FlamingoOcr.dll"

//导出函数名（GetProcAddress 使用 ANSI 名称），由 FlamingoOcr.def 固定，32 位与 64 位相同
#define FLAMINGO_OCR_ENTRY_NAME    "FlamingoOcr_GetApi"

namespace flamingoocr {

//DLL 经 OcrHostInfo::m_log 写日志时的级别
enum OcrLogLevel
{
    ocr_log_info    = 1,   //普通信息：初始化结果、可用性、每次识别的尺寸与耗时等关键路径
    ocr_log_warning = 2,   //警告：可以继续运行的异常情况，如某种识别语言创建失败
    ocr_log_error   = 3,   //错误：某次识别无法继续，如引擎返回失败的 HRESULT
};

//引擎可用性：DLL 已加载、初始化之后，引擎报告的「能不能识别」，宿主据此决定点击入口后的提示
enum OcrAvailability
{
    ocr_available      = 0,   //可以识别
    ocr_os_unsupported = 1,   //操作系统不支持（如 Win7 上没有 Windows.Media.Ocr）
    ocr_no_language    = 2,   //引擎在，但没有任何可用的识别语言（如英文系统没有装中文识别组件）
    ocr_engine_failed  = 3,   //引擎初始化失败（模型文件缺失、尚未初始化等），详情已写日志
};

//各导出函数的返回码
enum OcrResultCode
{
    ocr_ok                   = 0,   //成功；识别出 0 行文字也算成功
    ocr_bad_argument         = 1,   //参数非法：指针为 NULL、结构大小不足、宽高为 0、行跨度不足等
    ocr_unavailable          = 2,   //引擎不可用（可用性不是 ocr_available），宿主应先查询可用性
    ocr_language_unsupported = 3,   //指定的识别语言不在可用列表里
    ocr_cancelled            = 4,   //宿主在识别过程中置位了取消标志
    ocr_engine_error         = 5,   //引擎内部错误，详情已写日志
    ocr_out_of_memory        = 6,   //内存不足，无法组装结果
};

/**
 *  DLL 写日志的回调。由宿主实现，可能在任意线程被调用。
 *  @param level 日志级别，取值见 enum OcrLogLevel；不认识的值按普通信息处理
 *  @param text  日志正文，ASCII 英文、以 0 结尾，只在本次调用期间有效
 */
typedef void (__cdecl* OcrLogFn)(int level, const char* text);

/**
 *  宿主交给 DLL 的信息，m_init 时传入。m_cbSize 由宿主填为它编译时的结构大小。
 */
struct OcrHostInfo
{
    uint32_t       m_cbSize;        //本结构在宿主编译时的大小（字节）
    OcrLogFn       m_log;           //写日志的回调；为 NULL 时 DLL 不写日志
    const wchar_t* m_uiLanguage;    //当前界面语言代码，如 L"zh-CN"、L"en-US"，用于选默认识别语言；可为 NULL
};

/**
 *  可用性查询的出参。调用方先把 m_cbSize 置为它编译时的结构大小，DLL 只填写不超过该大小的字段。
 */
struct OcrAvailabilityInfo
{
    uint32_t       m_cbSize;          //调用方填入本结构的大小（字节）
    int            m_availability;    //可用性，取值见 enum OcrAvailability
    const wchar_t* m_settingsUri;     //可选：「打开系统设置」的地址，如 L"ms-settings:regionlanguage"；没有时为 NULL。归 DLL 所有
};

/**
 *  输入图像：32 位 BGRA（B、G、R、A 各一字节，A 为不预乘的透明度），自上而下逐行排列。
 *  m_cbSize 由宿主填写。像素数据归宿主所有，只在本次调用期间有效。
 */
struct OcrImage
{
    uint32_t       m_cbSize;    //本结构在宿主编译时的大小（字节）
    uint32_t       m_width;     //宽，像素，大于 0
    uint32_t       m_height;    //高，像素，大于 0
    uint32_t       m_stride;    //每行字节数，不小于 m_width * 4
    const uint8_t* m_pixels;    //第一行第一个像素的地址，不可为 NULL
};

/**
 *  矩形，单位为输入图像的像素，原点在左上角。大小永久固定。
 */
struct OcrRect
{
    float m_x;   //左边
    float m_y;   //上边
    float m_w;   //宽
    float m_h;   //高
};

/**
 *  识别出的一个词。对中文而言通常是一个字，对英文而言是一个单词。大小永久固定。
 */
struct OcrWord
{
    const wchar_t* m_text;   //词的文字，以 0 结尾
    OcrRect        m_rect;   //词的外接矩形
};

/**
 *  识别出的一行。大小永久固定。
 */
struct OcrLine
{
    const wchar_t* m_text;        //整行文字，已按词框间距做过拼行整理（汉字之间不留空格等），以 0 结尾
    OcrRect        m_rect;        //行的外接矩形，即所有词框的并集
    uint32_t       m_wordCount;   //m_words 的元素个数
    const OcrWord* m_words;       //本行的词，按从左到右排列；m_wordCount 为 0 时可为 NULL
};

/**
 *  一次识别的结果。整块内存归 DLL 所有，经 m_freeResult 释放；释放前其中所有指针都有效。
 *  以后只在末尾追加字段，读取新字段前先比较 m_cbSize。
 */
struct OcrResult
{
    uint32_t       m_cbSize;          //本结构在 DLL 编译时的大小（字节）
    uint32_t       m_lineCount;       //m_lines 的元素个数；没有识别出文字时为 0
    const OcrLine* m_lines;           //各行，按阅读顺序排列；m_lineCount 为 0 时可为 NULL
    const wchar_t* m_languageTag;     //实际使用的识别语言（BCP-47 代码），以 0 结尾
    float          m_textAngle;       //引擎估计的文字倾斜角（度，顺时针为正）；估计不出时为 0
    uint32_t       m_scalePercent;    //识别前对图像实际做的缩放，百分比（100 表示原样，200 表示放大 2 倍），写日志用
};

/**
 *  DLL 提供的函数表。由 DLL 持有，指针在 DLL 卸载前一直有效。
 */
struct OcrApi
{
    //本结构在 DLL 编译时的大小（字节）。宿主读取某个函数指针前先确认它落在这个大小之内
    uint32_t m_cbSize;

    /**
     *  初始化：记下宿主信息，准备引擎。只有第一次调用生效，之后的调用直接返回第一次的结果。
     *  引擎在本机不能用（如 Win7）时仍返回 ocr_ok，原因由 m_queryAvailability 报告。
     *  @param host 宿主信息，不可为 NULL，m_cbSize 至少覆盖到 m_log
     *  @return ocr_ok 表示成功；host 非法时返回 ocr_bad_argument；其它值表示 DLL 无法工作，宿主应视为不可用
     */
    int (__cdecl* m_init)(const OcrHostInfo* host);

    /**
     *  引擎名称，如 L"Windows.Media.Ocr"，写日志与排查问题用。
     *  @return 静态字符串，归 DLL 所有
     */
    const wchar_t* (__cdecl* m_getEngineName)();

    /**
     *  引擎版本，如 L"1.0.0"，写日志与排查问题用。
     *  @return 静态字符串，归 DLL 所有
     */
    const wchar_t* (__cdecl* m_getEngineVersion)();

    /**
     *  查询引擎可用性。应在 m_init 之后调用；之前调用时报告 ocr_engine_failed。
     *  @param out [出参] 调用方须先把 m_cbSize 置为 sizeof(OcrAvailabilityInfo)
     *  @return ocr_ok；out 为 NULL 或 m_cbSize 不足时返回 ocr_bad_argument
     */
    int (__cdecl* m_queryAvailability)(OcrAvailabilityInfo* out);

    /**
     *  可用的识别语言个数。引擎不可用时为 0。
     *  @return 语言个数
     */
    uint32_t (__cdecl* m_getLanguageCount)();

    /**
     *  第 index 种识别语言的 BCP-47 代码，如 L"zh-Hans-CN"。
     *  @param index 从 0 开始的下标，取值 0 ~ m_getLanguageCount() - 1
     *  @return 归 DLL 所有的字符串；下标越界时返回 NULL
     */
    const wchar_t* (__cdecl* m_getLanguageTag)(uint32_t index);

    /**
     *  第 index 种识别语言的显示名，由引擎按系统语言给出，如 L"中文(简体，中国)"。
     *  @param index 从 0 开始的下标，取值 0 ~ m_getLanguageCount() - 1
     *  @return 归 DLL 所有的字符串；下标越界时返回 NULL
     */
    const wchar_t* (__cdecl* m_getLanguageName)(uint32_t index);

    /**
     *  默认识别语言：按 m_init 时的界面语言从可用列表里选出，选不出时取系统用户语言对应的那一种。
     *  @return 归 DLL 所有的 BCP-47 代码；引擎不可用时返回 NULL
     */
    const wchar_t* (__cdecl* m_getDefaultLanguage)();

    /**
     *  识别一张图片。同步执行，耗时从几十毫秒到数秒不等，只能在宿主的后台线程上调用。
     *  @param image       输入图像，不可为 NULL，字段约束见 OcrImage
     *  @param languageTag 识别语言的 BCP-47 代码；NULL 或空串表示用默认识别语言
     *  @param cancelFlag  取消标志，宿主把它置为非 0 即请求取消；可为 NULL，表示不可取消
     *  @param result      [出参] 成功时置为结果（经 m_freeResult 释放），失败时置为 NULL；不可为 NULL
     *  @return 结果码，取值见 enum OcrResultCode
     */
    int (__cdecl* m_recognize)(const OcrImage* image, const wchar_t* languageTag,
                               volatile long* cancelFlag, OcrResult** result);

    /**
     *  释放 m_recognize 返回的结果。传 NULL 是安全的；传入不是本 DLL 返回的指针时忽略并记一条警告日志。
     *  @param result 要释放的结果，释放后不得再访问
     */
    void (__cdecl* m_freeResult)(OcrResult* result);
};

//宿主可以使用的函数表最小大小：第一版函数表到 m_freeResult 为止
const uint32_t kOcrApiMinSize = (uint32_t)(offsetof(OcrApi, m_freeResult) + sizeof(void*));

//OcrHostInfo 的最小大小：DLL 至少要能读到 m_log
const uint32_t kOcrHostInfoMinSize = (uint32_t)(offsetof(OcrHostInfo, m_log) + sizeof(void*));

//OcrAvailabilityInfo 的最小大小：DLL 至少要能写 m_availability
const uint32_t kOcrAvailabilityInfoMinSize = (uint32_t)(offsetof(OcrAvailabilityInfo, m_availability) + sizeof(int));

//OcrImage 的最小大小：第一版到 m_pixels 为止
const uint32_t kOcrImageMinSize = (uint32_t)(offsetof(OcrImage, m_pixels) + sizeof(void*));

/**
 *  判断 DLL 提供的函数表是否包含某个字段（以字段末尾的偏移量判断）。
 *  @param api          函数表，可为 NULL
 *  @param fieldEndOffs 字段末尾的偏移量，即 offsetof(OcrApi, 字段) + sizeof(字段)
 *  @return 包含时返回 true
 */
inline bool OcrApiHasField(const OcrApi* api, size_t fieldEndOffs)
{
    return api != NULL && api->m_cbSize >= fieldEndOffs;
}

} // namespace flamingoocr

//导出函数 FlamingoOcr_GetApi 的类型。不接收参数，返回 DLL 持有的函数表，永不返回 NULL
typedef const flamingoocr::OcrApi* (__cdecl* FlamingoOcrGetApiFn)();
