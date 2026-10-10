/**
 *  选择集 ShareSelection 与拖出条目表 ShareDragModel 的实现，见 ShareDragModel.h。
 *  balloonwj@qq.com   2026-10-03
 */
#include "stdafx.h"

#include "ShareDragModel.h"

#include "ShareSearch.h"     //kShareTreeMaxItems
#include "ShareWinPath.h"
#include "../../Source/Common/I18n/I18n.h"   // TR：界面文字的多语言查找

namespace fileshare {

namespace {

//相对路径的分隔符与落地名字的分隔符
const char kRelSep = '/';
const wchar_t kDropSep = L'\\';

//拒绝原因文字的缓冲区字符数
const size_t kRefusalBufChars = 128;

//相对路径的上级目录（"a/b/c" 得 "a/b"，"a" 得空串）
std::string ParentOf(const std::string& path)
{
    const size_t pos = path.rfind(kRelSep);
    return (pos == std::string::npos) ? std::string() : path.substr(0, pos);
}

} // namespace

ShareSelection::ShareSelection()
    : m_anchor(-1)
    , m_primary(-1)
{
}

void ShareSelection::Click(int index, bool ctrl, bool shift)
{
    if (index < 0)
        return;
    if (shift && m_anchor >= 0)
    {
        //连选：从起点到这一项；不按 Ctrl 时替换原有选择
        if (!ctrl)
            m_items.clear();
        const int lo = (m_anchor < index) ? m_anchor : index;
        const int hi = (m_anchor < index) ? index : m_anchor;
        for (int i = lo; i <= hi; ++i)
            m_items.insert(i);
        m_primary = index;
        return;
    }
    if (ctrl)
    {
        //加选或减选这一项
        if (m_items.count(index) != 0)
            m_items.erase(index);
        else
            m_items.insert(index);
        m_anchor = index;
        m_primary = index;
        return;
    }
    SelectOnly(index);
}

void ShareSelection::SelectOnly(int index)
{
    m_items.clear();
    if (index >= 0)
        m_items.insert(index);
    m_anchor = index;
    m_primary = index;
}

void ShareSelection::Clear()
{
    SelectOnly(-1);
}

bool ShareSelection::Contains(int index) const
{
    return m_items.count(index) != 0;
}

size_t ShareSelection::Count() const
{
    return m_items.size();
}

std::vector<int> ShareSelection::Sorted() const
{
    return std::vector<int>(m_items.begin(), m_items.end());
}

int ShareSelection::Primary() const
{
    return m_primary;
}

int ShareDragModel::Build(const std::vector<ShareDragSource>& sources,
                          const std::map<std::string, std::vector<ShareEntry> >& trees, std::vector<ShareDragItem>& out)
{
    out.clear();
    if (sources.empty())
        return share_drag_build_empty;
    std::vector<ShareDragItem> items;
    //已用过的顶层名字（转成小写比较：本机文件系统不区分大小写）
    std::set<std::wstring> topNames;
    for (size_t i = 0; i < sources.size(); ++i)
    {
        const ShareDragSource& src = sources[i];
        const std::string baseDir = ParentOf(src.m_sharePath);
        ShareDragItem top;
        top.m_sharePath = src.m_sharePath;
        top.m_dropName = DropNameFor(src.m_sharePath, baseDir);
        std::wstring lower(top.m_dropName);
        if (!lower.empty())
            ::CharLowerBuffW(&lower[0], static_cast<DWORD>(lower.size()));
        if (!topNames.insert(lower).second)
            return share_drag_build_duplicate;
        top.m_isDir = src.m_entry.m_isDir;
        top.m_size = src.m_entry.m_isDir ? 0 : src.m_entry.m_size;
        top.m_mtime = src.m_entry.m_mtime;
        items.push_back(top);
        if (!src.m_entry.m_isDir)
            continue;
        std::map<std::string, std::vector<ShareEntry> >::const_iterator it = trees.find(src.m_sharePath);
        if (it == trees.end())
            return share_drag_build_missing_tree;
        for (size_t k = 0; k < it->second.size(); ++k)
        {
            const ShareEntry& e = it->second[k];
            ShareDragItem item;
            item.m_sharePath = e.m_name;
            item.m_dropName = DropNameFor(e.m_name, baseDir);
            item.m_isDir = e.m_isDir;
            item.m_size = e.m_isDir ? 0 : e.m_size;
            item.m_mtime = e.m_mtime;
            items.push_back(item);
            //超过上限就不必再往下展开
            if (items.size() > static_cast<size_t>(kShareTreeMaxItems))
                return share_drag_build_too_many;
        }
    }
    if (items.size() > static_cast<size_t>(kShareTreeMaxItems))
        return share_drag_build_too_many;
    out.swap(items);
    return share_drag_build_ok;
}

std::wstring ShareDragModel::RefusalText(int result)
{
    switch (result)
    {
    //条目太多：资源管理器的虚拟文件描述表要一次给全，太大时改用「下载」
    case share_drag_build_too_many:
    {
        wchar_t buf[kRefusalBufChars] = { 0 };
        _snwprintf_s(buf, kRefusalBufChars, _TRUNCATE, TR(L"选中的内容超过 %d 项，无法拖出，请使用「下载」"), kShareTreeMaxItems);
        return buf;
    }

    //目录树没取到（读取失败或还在读取）
    case share_drag_build_missing_tree:
        return TR(L"文件夹的内容还没有读取完成，请稍后再拖出");

    //没有选中
    case share_drag_build_empty:
        return TR(L"请先选中要拖出的文件或文件夹");

    //顶层同名
    case share_drag_build_duplicate:
        return TR(L"选中的条目里有同名的，无法一起拖出，请分开拖出");

    //成功或不认识的值：不显示
    default:
        return std::wstring();
    }
}

std::wstring ShareDragModel::DropNameFor(const std::string& sharePath, const std::string& baseDir)
{
    std::string rel;
    if (baseDir.empty())
        rel = sharePath;
    else if (sharePath.size() > baseDir.size() + 1 && sharePath.compare(0, baseDir.size(), baseDir) == 0
             && sharePath[baseDir.size()] == kRelSep)
        rel = sharePath.substr(baseDir.size() + 1);
    else
    {
        const size_t pos = sharePath.rfind(kRelSep);
        rel = (pos == std::string::npos) ? sharePath : sharePath.substr(pos + 1);
    }
    std::wstring wide;
    ShareUtf8ToWide(rel, wide);
    for (size_t i = 0; i < wide.size(); ++i)
    {
        if (wide[i] == L'/')
            wide[i] = kDropSep;
    }
    return wide;
}

} // namespace fileshare
