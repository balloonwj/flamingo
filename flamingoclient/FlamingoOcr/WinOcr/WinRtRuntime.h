/**
 *  WinRT 基础函数的运行时取用：RoGetActivationFactory 与 HSTRING 的几个函数都在 combase.dll 里，
 *  Win7 上没有这个 DLL。为保证 FlamingoOcr.dll 在 Win7 上仍能加载，这些函数一律在运行时用 GetProcAddress 取，
 *  不做静态导入（单元测试 OCRG2 检查导入表）。写法与 FlamingoMedia/Share/WgcWindowCapturer.cpp 的 WinRtApi 相同。
 *
 *  典型用法：
 *
 *      const CWinRtRuntime& rt = CWinRtRuntime::Get();
 *      if (!rt.IsAvailable())
 *          return ocr_os_unsupported;
 *      ComPtr<IOcrEngineStatics> statics;
 *      HRESULT hr = rt.GetActivationFactory(L"Windows.Media.Ocr.OcrEngine", IID_PPV_ARGS(&statics));
 *
 *  本头文件只依赖 <windows.h> 与 <hstring.h>，不引入任何 WinRT 类型。
 *
 *  balloonwj@qq.com   2026-10-05
 */
#pragma once

#include <windows.h>
#include <hstring.h>
#include <string>

namespace flamingoocr {

/**
 *  运行时取得的 WinRT 基础函数。进程内只取一次，之后只读，可在任意线程使用。
 */
class CWinRtRuntime
{
public:
    /**
     *  取进程内唯一的实例；第一次调用时加载 combase.dll 并取函数地址（之后不释放该 DLL）。
     *  @return 实例引用，生命周期与进程相同
     */
    static const CWinRtRuntime& Get();

    /**
     *  所需函数是否都已取到。为 false 表示系统不支持 WinRT（如 Win7）。
     */
    bool IsAvailable() const;

    /**
     *  按运行时类名取激活工厂（静态接口或工厂接口）。调用线程须已初始化 COM。
     *  @param className 运行时类名，如 L"Windows.Media.Ocr.OcrEngine"，不可为 NULL
     *  @param iid       要取的接口
     *  @param out       [出参] 接口指针，调用方负责 Release
     *  @return HRESULT；IsAvailable 为 false 时返回 E_NOTIMPL
     */
    HRESULT GetActivationFactory(const wchar_t* className, REFIID iid, void** out) const;

    /**
     *  为一段字符串建立「引用型」HSTRING（不复制内容，原字符串须活到 HSTRING 用完）。
     *  @param text   字符串，以 0 结尾，不可为 NULL
     *  @param header [出参] 由调用方提供存储的 HSTRING 头，须与 HSTRING 同样长寿
     *  @param out    [出参] 引用型 HSTRING，不需要也不能释放
     *  @return HRESULT；IsAvailable 为 false 时返回 E_NOTIMPL
     */
    HRESULT CreateStringReference(const wchar_t* text, HSTRING_HEADER* header, HSTRING* out) const;

    /**
     *  把 HSTRING 的内容复制成 std::wstring。
     *  @param value HSTRING，可为 NULL（表示空串）
     *  @return 内容副本
     */
    std::wstring ToWString(HSTRING value) const;

    /**
     *  释放 WinRT 接口返回的 HSTRING。传 NULL 是安全的。
     */
    void DeleteString(HSTRING value) const;

private:
    CWinRtRuntime();

    typedef HRESULT(WINAPI* RoGetActivationFactoryFn)(HSTRING, REFIID, void**);
    typedef HRESULT(WINAPI* WindowsCreateStringReferenceFn)(PCWSTR, UINT32, HSTRING_HEADER*, HSTRING*);
    typedef PCWSTR(WINAPI* WindowsGetStringRawBufferFn)(HSTRING, UINT32*);
    typedef HRESULT(WINAPI* WindowsDeleteStringFn)(HSTRING);

    RoGetActivationFactoryFn       m_getFactory;   //combase!RoGetActivationFactory；取不到时为 NULL
    WindowsCreateStringReferenceFn m_stringRef;    //combase!WindowsCreateStringReference；取不到时为 NULL
    WindowsGetStringRawBufferFn    m_rawBuffer;    //combase!WindowsGetStringRawBuffer；取不到时为 NULL
    WindowsDeleteStringFn          m_deleteString; //combase!WindowsDeleteString；取不到时为 NULL
};

/**
 *  持有 WinRT 接口返回的 HSTRING，离开作用域时释放。不可复制。
 */
class CHStringHolder
{
public:
    CHStringHolder();
    ~CHStringHolder();

    //取地址交给 get_Xxx(HSTRING*) 一类的接口；调用前先释放已持有的字符串
    HSTRING* Put();

    //内容副本
    std::wstring ToWString() const;

private:
    CHStringHolder(const CHStringHolder&);
    CHStringHolder& operator=(const CHStringHolder&);

    HSTRING m_value;   //持有的字符串；NULL 表示没有
};

} // namespace flamingoocr
