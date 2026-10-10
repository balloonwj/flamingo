/**
 *  WinRT 基础函数的运行时取用，说明见 WinRtRuntime.h。
 *
 *  balloonwj@qq.com   2026-10-05
 */
#include "WinRtRuntime.h"

#include <wchar.h>

namespace flamingoocr {

namespace {

//WinRT 基础函数所在的系统 DLL（Win8 起才有）
const wchar_t* const kCombaseDll = L"combase.dll";

} // namespace

CWinRtRuntime::CWinRtRuntime()
    : m_getFactory(NULL)
    , m_stringRef(NULL)
    , m_rawBuffer(NULL)
    , m_deleteString(NULL)
{
    //combase.dll 在 Win8 起的每个进程里本来就已载入；这里取了句柄不释放，进程内只取一次
    HMODULE combase = ::LoadLibraryW(kCombaseDll);
    if (combase == NULL)
        return;
    m_getFactory = reinterpret_cast<RoGetActivationFactoryFn>(::GetProcAddress(combase, "RoGetActivationFactory"));
    m_stringRef = reinterpret_cast<WindowsCreateStringReferenceFn>(
        ::GetProcAddress(combase, "WindowsCreateStringReference"));
    m_rawBuffer = reinterpret_cast<WindowsGetStringRawBufferFn>(::GetProcAddress(combase, "WindowsGetStringRawBuffer"));
    m_deleteString = reinterpret_cast<WindowsDeleteStringFn>(::GetProcAddress(combase, "WindowsDeleteString"));
}

const CWinRtRuntime& CWinRtRuntime::Get()
{
    //C++11 起函数内静态对象的初始化是线程安全的
    static CWinRtRuntime s_runtime;
    return s_runtime;
}

bool CWinRtRuntime::IsAvailable() const
{
    return m_getFactory != NULL && m_stringRef != NULL && m_rawBuffer != NULL && m_deleteString != NULL;
}

HRESULT CWinRtRuntime::GetActivationFactory(const wchar_t* className, REFIID iid, void** out) const
{
    if (out == NULL || className == NULL)
        return E_POINTER;
    *out = NULL;
    if (!IsAvailable())
        return E_NOTIMPL;

    HSTRING_HEADER header;
    HSTRING name = NULL;
    HRESULT hr = CreateStringReference(className, &header, &name);
    if (FAILED(hr))
        return hr;
    return m_getFactory(name, iid, out);
}

HRESULT CWinRtRuntime::CreateStringReference(const wchar_t* text, HSTRING_HEADER* header, HSTRING* out) const
{
    if (text == NULL || header == NULL || out == NULL)
        return E_POINTER;
    if (!IsAvailable())
        return E_NOTIMPL;
    return m_stringRef(text, static_cast<UINT32>(wcslen(text)), header, out);
}

std::wstring CWinRtRuntime::ToWString(HSTRING value) const
{
    if (value == NULL || m_rawBuffer == NULL)
        return std::wstring();
    UINT32 length = 0;
    PCWSTR raw = m_rawBuffer(value, &length);
    if (raw == NULL || length == 0)
        return std::wstring();
    return std::wstring(raw, length);
}

void CWinRtRuntime::DeleteString(HSTRING value) const
{
    if (value != NULL && m_deleteString != NULL)
        m_deleteString(value);
}

CHStringHolder::CHStringHolder()
    : m_value(NULL)
{
}

CHStringHolder::~CHStringHolder()
{
    CWinRtRuntime::Get().DeleteString(m_value);
}

HSTRING* CHStringHolder::Put()
{
    CWinRtRuntime::Get().DeleteString(m_value);
    m_value = NULL;
    return &m_value;
}

std::wstring CHStringHolder::ToWString() const
{
    return CWinRtRuntime::Get().ToWString(m_value);
}

} // namespace flamingoocr
