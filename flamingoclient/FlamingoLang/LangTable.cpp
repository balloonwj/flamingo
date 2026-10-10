/**
 *  一种语言的译文查找表的实现，见 LangTable.h。
 *
 *  balloonwj@qq.com   2026-10-03
 */
#include "LangTable.h"

#include <windows.h>
#include <string.h>

namespace flamingolang {

namespace {

//64 位 FNV-1a 的初始值与乘数（FNV 算法的标准常量）
const uint64_t kFnvOffsetBasis = 14695981039346656037ULL;
const uint64_t kFnvPrime = 1099511628211ULL;

//上下文与原文之间的分隔符，与 gettext 拼接 msgctxt 与 msgid 时用的 EOT（0x04）相同
const wchar_t kContextSeparator = L'\x0004';

//把一个 UTF-16 码元按 FNV-1a 规则并入哈希值
inline uint64_t FnvMix(uint64_t hash, wchar_t c)
{
    hash ^= (uint64_t)(uint16_t)c;
    hash *= kFnvPrime;
    return hash;
}

/**
 *  UTF-8 转 UTF-16，遇到非法字节返回 false。
 *  @param in  UTF-8 文本
 *  @param out [出参] 转换结果
 */
bool Utf8ToWide(const std::string& in, std::wstring& out)
{
    out.clear();
    if (in.empty())
        return true;
    int n = ::MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, in.data(), (int)in.size(), NULL, 0);
    if (n <= 0)
        return false;
    out.resize((size_t)n);
    return ::MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, in.data(), (int)in.size(), &out[0], n) == n;
}

//把 NULL 上下文统一成空串，便于比较
inline const wchar_t* NormalizeContext(const wchar_t* context)
{
    return (context != NULL) ? context : L"";
}

} // namespace

uint64_t DefaultLangHash(const wchar_t* context, const wchar_t* source)
{
    uint64_t hash = kFnvOffsetBasis;
    if (context != NULL && context[0] != L'\0')
    {
        for (const wchar_t* p = context; *p != L'\0'; ++p)
            hash = FnvMix(hash, *p);
        hash = FnvMix(hash, kContextSeparator);
    }
    for (const wchar_t* p = source; *p != L'\0'; ++p)
        hash = FnvMix(hash, *p);
    return hash;
}

CLangTable::CLangTable(LangHashFn hashFn)
    : m_hashFn((hashFn != NULL) ? hashFn : DefaultLangHash),
      m_errors(0),
      m_warnings(0)
{
}

void CLangTable::Build(const PoParseResult& parsed)
{
    m_items.clear();
    m_index.clear();
    m_errors = 0;
    m_warnings = 0;
    m_items.reserve(parsed.m_entries.size());

    for (size_t i = 0; i < parsed.m_entries.size(); ++i)
    {
        const PoEntry& entry = parsed.m_entries[i];
        Item item;
        if (!Utf8ToWide(entry.m_context, item.m_context) || !Utf8ToWide(entry.m_msgid, item.m_source)
            || !Utf8ToWide(entry.m_msgstr, item.m_text))
        {
            ++m_errors;
            continue;
        }

        const wchar_t* context = item.m_context.empty() ? NULL : item.m_context.c_str();
        uint64_t hash = m_hashFn(context, item.m_source.c_str());
        if (FindIndex(hash, context, item.m_source.c_str()) >= 0)
        {
            //同一「上下文 + 原文」出现多次：保留第一条
            ++m_warnings;
            continue;
        }
        m_index.insert(std::make_pair(hash, (uint32_t)m_items.size()));
        m_items.push_back(item);
    }
}

int CLangTable::FindIndex(uint64_t hash, const wchar_t* context, const wchar_t* source) const
{
    const wchar_t* ctx = NormalizeContext(context);
    std::pair<std::unordered_multimap<uint64_t, uint32_t>::const_iterator,
              std::unordered_multimap<uint64_t, uint32_t>::const_iterator> range = m_index.equal_range(hash);
    for (std::unordered_multimap<uint64_t, uint32_t>::const_iterator it = range.first; it != range.second; ++it)
    {
        const Item& item = m_items[it->second];
        if (wcscmp(item.m_source.c_str(), source) == 0 && wcscmp(item.m_context.c_str(), ctx) == 0)
            return (int)it->second;
    }
    return -1;
}

const wchar_t* CLangTable::Lookup(const wchar_t* context, const wchar_t* source) const
{
    if (source == NULL)
        return NULL;
    int index = FindIndex(m_hashFn(context, source), context, source);
    if (index < 0)
        return NULL;
    return m_items[(size_t)index].m_text.c_str();
}

uint32_t CLangTable::GetEntryCount() const
{
    return (uint32_t)m_items.size();
}

uint32_t CLangTable::GetErrorCount() const
{
    return m_errors;
}

uint32_t CLangTable::GetWarningCount() const
{
    return m_warnings;
}

} // namespace flamingolang
