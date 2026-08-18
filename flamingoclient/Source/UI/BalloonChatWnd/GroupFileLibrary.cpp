/**
 *  GroupFileLibrary.cpp
 *
 *  群文件库纯逻辑实现。筛选 / 搜索 / 分页全在内存进行，不触及 UI / 网络。
 *
 *  本 .cpp 是纯 std + net 类型，不 include Flamingo 的 stdafx.h、也不需要 ATL；
 *  被 Flamingo.vcxproj 与 FlamingoUnit.vcxproj 共用，主工程里标记
 *  PrecompiledHeader=NotUsing。
 *
 *  balloonwj@qq.com   2026-06-08
 */

#include "GroupFileLibrary.h"
#include "../../net/Protocol.h"   // group_member_role（group_role_admin 阈值，全局命名空间）
#include <algorithm>   // std::find_if

namespace balloonchatwnd {

namespace {

// 把 ASCII 字母转小写（中文等非 ASCII 字节原样保留；用于扩展名 / 搜索的大小写不敏感）。
std::string ToLowerAscii(const std::string& s)
{
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i)
    {
        char c = s[i];
        if (c >= 'A' && c <= 'Z')
        {
            c = (char)(c - 'A' + 'a');
        }
        out.push_back(c);
    }
    return out;
}

} // anonymous namespace

GroupFileLibrary::GroupFileLibrary()
{
}

void GroupFileLibrary::SetAll(const std::vector<net::GroupFileEntry>& all)
{
    m_all = all;
}

void GroupFileLibrary::Upsert(const net::GroupFileEntry& e)
{
    // 按 entryid 查找：已存在则覆盖（同一条目的元数据更新），否则尾部追加。
    for (size_t i = 0; i < m_all.size(); ++i)
    {
        if (m_all[i].m_entryid == e.m_entryid)
        {
            m_all[i] = e;
            return;
        }
    }
    m_all.push_back(e);
}

bool GroupFileLibrary::RemoveByEntryId(const std::string& entryid)
{
    for (size_t i = 0; i < m_all.size(); ++i)
    {
        if (m_all[i].m_entryid == entryid)
        {
            m_all.erase(m_all.begin() + i);
            return true;
        }
    }
    return false;
}

void GroupFileLibrary::Clear()
{
    m_all.clear();
}

const net::GroupFileEntry* GroupFileLibrary::FindByEntryId(const std::string& entryid) const
{
    for (size_t i = 0; i < m_all.size(); ++i)
    {
        if (m_all[i].m_entryid == entryid)
        {
            return &m_all[i];
        }
    }
    return NULL;
}

std::string GroupFileLibrary::EffectiveExtLower(const net::GroupFileEntry& e)
{
    // 优先用服务端给的扩展名；为空时从文件名取最后一个 '.' 之后的部分。
    if (!e.m_ext.empty())
    {
        return ToLowerAscii(e.m_ext);
    }
    const std::string& name = e.m_name;
    std::string::size_type dot = name.find_last_of('.');
    if (dot == std::string::npos || dot + 1 >= name.size())
    {
        return std::string();
    }
    return ToLowerAscii(name.substr(dot + 1));
}

bool GroupFileLibrary::ExtMatchesCategory(const std::string& extLower, int category)
{
    if (category == kGroupFileCatAll)
    {
        return true;
    }
    if (category == kGroupFileCatPdf)
    {
        return extLower == "pdf";
    }
    if (category == kGroupFileCatDoc)
    {
        return extLower == "doc"  || extLower == "docx" ||
               extLower == "ppt"  || extLower == "pptx" ||
               extLower == "txt"  || extLower == "rtf"  || extLower == "md";
    }
    if (category == kGroupFileCatSheet)
    {
        return extLower == "xls" || extLower == "xlsx" || extLower == "csv";
    }
    if (category == kGroupFileCatDesign)
    {
        return extLower == "fig"    || extLower == "psd" || extLower == "ai" ||
               extLower == "sketch" || extLower == "xd"  || extLower == "cdr";
    }
    // 未知分类一律不匹配（防御）。
    return false;
}

bool GroupFileLibrary::NameMatchesSearch(const std::string& name, const std::string& search)
{
    if (search.empty())
    {
        return true;
    }
    std::string nameLow   = ToLowerAscii(name);
    std::string searchLow = ToLowerAscii(search);
    return nameLow.find(searchLow) != std::string::npos;
}

bool GroupFileLibrary::CanDeleteGroupFile(int myGroupRole, bool isOwnUpload)
{
    // 群主 / 管理员（role >= group_role_admin）可删任意；登记者本人可删自己上传的。
    // 与服务端 ChatSession::onDeleteGroupFile 的 (role>=admin)||(uploader==self) 一致。
    return (myGroupRole >= group_role_admin) || isOwnUpload;
}

void GroupFileLibrary::Query(int category, const std::string& search, int page, int pageSize,
                             std::vector<net::GroupFileEntry>& outItems,
                             int& outTotalFiltered, int& outPageCount, int& outCurPage) const
{
    outItems.clear();

    // 1) 过滤：分类 + 文件名搜索。
    std::vector<const net::GroupFileEntry*> filtered;
    filtered.reserve(m_all.size());
    for (size_t i = 0; i < m_all.size(); ++i)
    {
        const net::GroupFileEntry& e = m_all[i];
        if (!ExtMatchesCategory(EffectiveExtLower(e), category))
        {
            continue;
        }
        if (!NameMatchesSearch(e.m_name, search))
        {
            continue;
        }
        filtered.push_back(&e);
    }

    const int total = (int)filtered.size();
    outTotalFiltered = total;

    // 2) 分页：每页条数至少 1；空结果也算 1 页（避免 0 页 / 除零）。
    int size = pageSize;
    if (size <= 0)
    {
        size = 1;
    }
    int pageCount = (total + size - 1) / size;
    if (pageCount < 1)
    {
        pageCount = 1;
    }
    outPageCount = pageCount;

    // 3) 钳定当前页到 [0, pageCount-1]。
    int cur = page;
    if (cur < 0)
    {
        cur = 0;
    }
    if (cur > pageCount - 1)
    {
        cur = pageCount - 1;
    }
    outCurPage = cur;

    // 4) 取本页区间 [start, end) 的条目。
    const int start = cur * size;
    int end = start + size;
    if (end > total)
    {
        end = total;
    }
    for (int i = start; i < end; ++i)
    {
        outItems.push_back(*filtered[i]);
    }
}

} // namespace balloonchatwnd
