/**
 *  伪语言（qps-ploc）的实现，见 PseudoLang.h。
 *
 *  balloonwj@qq.com   2026-10-03
 */
#include "PseudoLang.h"
#include "ScopedLock.h"

namespace flamingolang {

namespace {

//伪译文前后的括号：« 与 »。微软雅黑里有这两个字形（单元测试 LD15 实测），不会显示成方框
const wchar_t kOpenMark = L'\x00AB';
const wchar_t kCloseMark = L'\x00BB';

//填充字符
const wchar_t kPadChar = L'~';

//每个宽字符补的填充字符个数。界面字号下一个汉字约是两个 '~' 的宽度，补两个使总宽度约为原文的 2 倍
const size_t kPadPerWideChar = 2;

//码位不小于它的 UTF-16 码元按宽字符计：U+2E80 起是 CJK 部首、汉字、假名、谚文与全角符号
const wchar_t kFirstWideCodeUnit = L'\x2E80';

} // namespace

CPseudoLang::CPseudoLang()
{
    ::InitializeCriticalSection(&m_lock);
}

CPseudoLang::~CPseudoLang()
{
    ::DeleteCriticalSection(&m_lock);
}

std::wstring CPseudoLang::MakePseudoText(const wchar_t* source)
{
    size_t wideCount = 0;
    for (const wchar_t* p = source; *p != L'\0'; ++p)
    {
        if (*p >= kFirstWideCodeUnit)
            ++wideCount;
    }

    std::wstring text;
    text.reserve(wcslen(source) + wideCount * kPadPerWideChar + 2);
    text.push_back(kOpenMark);
    text.append(source);
    text.append(wideCount * kPadPerWideChar, kPadChar);
    text.push_back(kCloseMark);
    return text;
}

const wchar_t* CPseudoLang::Lookup(const wchar_t* context, const wchar_t* source)
{
    (void)context;
    if (source == NULL)
        return NULL;

    CScopedLock guard(&m_lock);
    std::unordered_map<std::wstring, std::unique_ptr<std::wstring> >::const_iterator it = m_cache.find(source);
    if (it != m_cache.end())
        return it->second->c_str();

    std::unique_ptr<std::wstring> text(new std::wstring(MakePseudoText(source)));
    const wchar_t* result = text->c_str();
    m_cache[source] = std::move(text);
    return result;
}

uint32_t CPseudoLang::GetEntryCount() const
{
    CScopedLock guard(&m_lock);
    return (uint32_t)m_cache.size();
}

void CPseudoLang::Clear()
{
    CScopedLock guard(&m_lock);
    m_cache.clear();
}

} // namespace flamingolang
