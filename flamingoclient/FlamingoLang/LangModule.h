/**
 *  语言包模块：登记语言包里有哪些语言、按需解析某种语言的译文、记录当前选定的语言，并提供导出给宿主的函数表。
 *
 *  进程内唯一（Instance）。各语言的译文在第一次选定时解析，此后一直保留到进程结束，所以切换语言之后，
 *  先前 Lookup 返回的指针仍然有效。译文数据（.po 原始字节）经 PoSourceFn 取得：DLL 里取自资源
 *  （见 DllMain.cpp），单元测试里换成内存中的数据。
 *
 *  线程：SelectLanguage 与 Lookup 可以在任意线程并发调用。当前语言以指针形式原子地切换，Lookup 不加锁。
 *
 *  典型用法：
 *      flamingolang::CLangModule::Instance().SetPoSource(LoadPoFromResource);
 *      const flamingolang::LangApi* api = flamingolang::GetLangApiTable();
 *
 *  balloonwj@qq.com   2026-10-03
 */
#pragma once

#include <windows.h>
#include <stddef.h>
#include <stdint.h>
#include <map>
#include <memory>
#include <string>

#include "FlamingoLangApi.h"
#include "LangTable.h"
#include "PseudoLang.h"

namespace flamingolang {

/**
 *  取某种语言的 .po 原始字节。
 *  @param code       语言代码，如 L"en-US"
 *  @param resourceId 该语言在语言包资源里的编号（见 resource.h）
 *  @param data       [出参] 数据首地址；数据须在进程结束前保持有效（资源与静态数据都满足）
 *  @param size       [出参] 数据字节数
 *  @return 取到时返回 true
 */
typedef bool (*PoSourceFn)(const wchar_t* code, int resourceId, const char** data, size_t* size);

//语言包版本，写日志、排查问题用。修改译文或语言列表时递增最后一段。
#define FLAMINGO_LANG_PACK_VERSION  L"1.0.0"

/**
 *  语言包模块。进程内唯一，不可复制。
 */
class CLangModule
{
public:
    //取进程内唯一的实例（C++11 局部静态对象，首次调用时构造，线程安全）。
    static CLangModule& Instance();

    CLangModule(const CLangModule&) = delete;
    CLangModule& operator=(const CLangModule&) = delete;

    /**
     *  设置译文数据来源。应在第一次 SelectLanguage 之前调用；已解析的语言不受影响。
     *  @param fn 数据来源；传 NULL 表示没有数据来源，此后首次选定任何真实语言都会返回 lang_select_no_data
     */
    void SetPoSource(PoSourceFn fn);

    /**
     *  清空全部已解析的语言与当前选定，恢复到刚构造时的状态。<u>只供单元测试使用</u>：调用后，
     *  此前 Lookup 返回的指针全部失效，且调用时不得有其它线程正在使用本模块。
     */
    void ResetForTest();

    //以下五个方法与 LangApi 中的同名函数一一对应，参数与返回值的含义见 FlamingoLangApi.h。

    //语言包版本。
    const wchar_t* GetPackVersion() const;

    //语言个数（不含伪语言）。
    uint32_t GetLanguageCount() const;

    //第 index 种语言的信息，越界时返回 NULL。
    const LangInfo* GetLanguageInfo(uint32_t index) const;

    //选定当前语言，返回 enum LangSelectResult 的取值。
    int SelectLanguage(const wchar_t* code);

    //按当前语言查找译文，没有时返回 NULL。
    const wchar_t* Lookup(const wchar_t* context, const wchar_t* source);

    //取当前语言的统计信息。
    bool GetStats(LangStats* stats);

private:
    //一种已解析的语言。创建后不再改变，直到 ResetForTest 或进程结束。
    struct LoadedLang
    {
        std::wstring                m_code;     //语言代码
        bool                        m_isPseudo; //是否为伪语言；为 true 时 m_table 为空，查找交给 m_pseudo
        std::unique_ptr<CLangTable> m_table;    //真实语言的译文表
        uint32_t                    m_errors;   //解析与建表时跳过的坏条目总数
    };

    CLangModule();
    ~CLangModule();

    //按代码查语言包里的登记下标，找不到返回 -1。
    static int FindLanguage(const wchar_t* code);

    mutable CRITICAL_SECTION                          m_lock;      //保护 m_loaded、m_poSource 与选定过程，构造时初始化
    PoSourceFn                                        m_poSource;  //译文数据来源，可为 NULL
    std::map<std::wstring, std::unique_ptr<LoadedLang> > m_loaded; //语言代码 -> 已解析的语言；只增不减（ResetForTest 除外）
    LoadedLang* volatile                              m_current;   //当前选定的语言，未选定时为 NULL；以 InterlockedExchangePointer 写入
    CPseudoLang                                       m_pseudo;    //伪语言的译文生成与缓存
};

/**
 *  取导出给宿主的函数表。表中的函数全部转发给 CLangModule::Instance()。
 *  @return 静态对象，永不为 NULL
 */
const LangApi* GetLangApiTable();

} // namespace flamingolang
