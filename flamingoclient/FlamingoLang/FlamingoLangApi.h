/**
 *  FlamingoLang.dll（界面语言包）与宿主之间的接口约定。
 *
 *  FlamingoLang.dll 装着繁体中文、英文、日文、韩文四种界面语言的译文与每种语言的推荐界面字体（方案见
 *  docs/多语言方案.md）。简体中文就是代码里的原文，不在 DLL 里。宿主（Flamingo.exe、FlamingoUpdater.exe）
 *  按需用 LoadLibrary 加载它，经唯一的导出函数 FlamingoLang_GetApi 取得一张函数表；DLL 不存在或加载失败时，
 *  宿主一律显示简体中文原文。
 *
 *  与 FlamingoMedia、FlamingoFileShare 的接口不同，本接口<u>不</u>要求双方版本号完全相同：语言包要能单独替换，
 *  旧语言包配新 exe、新语言包配旧 exe 都必须能用。为此约定：
 *    - 函数表 LangApi 与语言信息 LangInfo 都以 m_cbSize 开头，取值为 DLL 编译时的结构大小；
 *    - 以后只在结构<u>末尾</u>追加字段，不修改、不删除、不重排已有字段；
 *    - 宿主使用某个字段之前，先按 m_cbSize 判断 DLL 是否提供了它（见 LangApiHasField）。
 *
 *  两边都静态链接 CRT、各有独立的堆，因此接口只使用 C 类型：
 *    - DLL 返回的字符串指针归 DLL 所有，在 DLL 卸载之前一直有效（宿主加载后不卸载）；
 *    - 宿主传入的字符串只在本次调用期间有效；
 *    - 不传递 STL 对象，异常不越过模块边界。
 *
 *  典型用法（宿主侧）：
 *
 *      FlamingoLangGetApiFn getApi = (FlamingoLangGetApiFn)::GetProcAddress(hDll, FLAMINGO_LANG_ENTRY_NAME);
 *      const flamingolang::LangApi* api = (getApi != NULL) ? getApi() : NULL;
 *      if (api != NULL && api->m_cbSize >= flamingolang::kLangApiMinSize
 *          && api->m_selectLanguage(L"en-US") == flamingolang::lang_select_ok)
 *      {
 *          const wchar_t* text = api->m_lookup(NULL, L"记住密码");   // NULL 表示没有译文，显示原文
 *      }
 *
 *  balloonwj@qq.com   2026-10-03
 */
#pragma once

#include <stddef.h>
#include <stdint.h>

//DLL 文件名，与宿主 exe 放在同一目录。
#define FLAMINGO_LANG_DLL_NAME      L"FlamingoLang.dll"

//导出函数名（GetProcAddress 使用 ANSI 名称），由 FlamingoLang.def 固定，32 位与 64 位相同。
#define FLAMINGO_LANG_ENTRY_NAME    "FlamingoLang_GetApi"

namespace flamingolang {

//一种语言最多给出的候选界面字体个数。宿主按顺序取第一个本机已安装的字体。
const uint32_t kMaxFontCandidates = 6;

//伪语言的代码。伪语言不出现在语言列表里，只能由宿主按代码选定，用来发现漏标记与被截断的文字。
#define FLAMINGO_LANG_PSEUDO_CODE   L"qps-ploc"

//选定语言的结果。
enum LangSelectResult
{
    lang_select_ok           = 0,   //选定成功，此后 m_lookup 按该语言查找
    lang_select_bad_argument = 1,   //语言代码为 NULL 或空串
    lang_select_unknown      = 2,   //语言包里没有这种语言
    lang_select_no_data      = 3,   //有这种语言，但取不到它的译文数据（资源缺失），当前选定的语言保持不变
};

/**
 *  一种语言的信息。由 DLL 持有，指针在 DLL 卸载前一直有效。
 *  以后只在末尾追加字段；读取新字段前先比较 m_cbSize。
 */
struct LangInfo
{
    uint32_t       m_cbSize;                            //本结构在 DLL 编译时的大小（字节）
    const wchar_t* m_code;                              //语言代码，如 L"en-US"，与 flamingo.ini 的 [ui] language 取值一致
    const wchar_t* m_nativeName;                        //该语言自己的名称，如 L"English"、L"日本語"，用于语言选择菜单
    uint32_t       m_fontCount;                         //m_fonts 中有效的候选字体个数，0 ~ kMaxFontCandidates
    const wchar_t* m_fonts[kMaxFontCandidates];         //候选界面字体名，按优先顺序排列；宿主取第一个已安装的
};

/**
 *  选定语言后的统计信息，宿主写日志用。
 */
struct LangStats
{
    uint32_t m_cbSize;      //调用方填入本结构的大小（字节）；DLL 只填写不超过该大小的字段
    uint32_t m_entries;     //当前语言有效的译文条数
    uint32_t m_errors;      //解析时跳过的坏条目数（格式错误、编码错误等）
    uint32_t m_warnings;    //解析时的警告数（重复条目等，不影响使用）
};

/**
 *  DLL 提供的函数表。由 DLL 持有，指针在 DLL 卸载前一直有效。
 *  所有函数都可以在任意线程调用；m_selectLanguage 与 m_lookup 可以并发调用，但宿主通常只在启动时选定一次。
 */
struct LangApi
{
    //本结构在 DLL 编译时的大小（字节）。宿主读取某个函数指针前先确认它落在这个大小之内。
    uint32_t m_cbSize;

    /**
     *  语言包版本，如 L"1.0.0"，写日志、排查问题用。
     *  @return 静态字符串，归 DLL 所有
     */
    const wchar_t* (__cdecl* m_getPackVersion)();

    /**
     *  语言包里的语言个数（不含伪语言）。
     *  @return 语言个数
     */
    uint32_t (__cdecl* m_getLanguageCount)();

    /**
     *  取第 index 种语言的信息，顺序即语言选择菜单里的顺序。
     *  @param index 从 0 开始的下标，取值 0 ~ m_getLanguageCount() - 1
     *  @return 语言信息，归 DLL 所有；下标越界时返回 NULL
     */
    const LangInfo* (__cdecl* m_getLanguageInfo)(uint32_t index);

    /**
     *  选定当前语言。首次选定某种语言时解析它的译文；可以再次选定别的语言，此前 m_lookup 返回的指针仍然有效。
     *  @param code 语言代码，如 L"en-US"，或伪语言代码 FLAMINGO_LANG_PSEUDO_CODE
     *  @return 结果，取值见 enum LangSelectResult；失败时当前选定的语言保持不变
     */
    int (__cdecl* m_selectLanguage)(const wchar_t* code);

    /**
     *  按上下文与原文查找当前语言的译文。
     *  @param context 上下文，没有上下文时传 NULL 或空串
     *  @param source  简体中文原文，不能为 NULL
     *  @return 译文，归 DLL 所有，在 DLL 卸载前一直有效；尚未选定语言、没有这条译文或译文为空时返回 NULL，
     *          宿主应显示原文
     */
    const wchar_t* (__cdecl* m_lookup)(const wchar_t* context, const wchar_t* source);

    /**
     *  取当前语言的统计信息。
     *  @param stats [出参] 调用方须先把 m_cbSize 置为 sizeof(LangStats)；尚未选定语言时各项为 0
     *  @return 尚未选定语言或 stats 为 NULL 时返回 false
     */
    bool (__cdecl* m_getStats)(LangStats* stats);
};

//宿主可以使用的函数表最小大小：第一版函数表到 m_getStats 为止。
const uint32_t kLangApiMinSize = (uint32_t)(offsetof(LangApi, m_getStats) + sizeof(void*));

/**
 *  判断 DLL 提供的函数表是否包含某个字段（以字段末尾的偏移量判断）。
 *  @param api          函数表，可为 NULL
 *  @param fieldEndOffs 字段末尾的偏移量，即 offsetof(LangApi, 字段) + sizeof(字段)
 *  @return 包含时返回 true
 */
inline bool LangApiHasField(const LangApi* api, size_t fieldEndOffs)
{
    return api != NULL && api->m_cbSize >= fieldEndOffs;
}

} // namespace flamingolang

//导出函数 FlamingoLang_GetApi 的类型。不接收参数，返回 DLL 持有的函数表，永不返回 NULL。
typedef const flamingolang::LangApi* (__cdecl* FlamingoLangGetApiFn)();
