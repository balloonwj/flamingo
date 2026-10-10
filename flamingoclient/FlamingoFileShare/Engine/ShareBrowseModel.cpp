/**
 *  浏览窗口纯逻辑 ShareBrowseModel 的实现，见 ShareBrowseModel.h。
 *  balloonwj@qq.com   2026-10-02
 */
#include "stdafx.h"

#include "ShareBrowseModel.h"

#include <windows.h>
#include <stdio.h>    //_snwprintf_s
#include <time.h>     //localtime_s
#include <algorithm>

#include "SharePathGuard.h"
#include "ShareWinPath.h"
#include "../../Source/net/Protocol.h"
#include "../../Source/Common/I18n/I18n.h"   // TR：界面文字的多语言查找

namespace fileshare {

namespace {

//大小换算的进位：1 KB = 1024 B
const double kSizeStep = 1024.0;

//删除确认文字的缓冲区字符数（名字最长 kShareNameMaxChars 个字符，加上固定文字）
const size_t kConfirmTextBufChars = 512;

//本机路径的分隔符
const wchar_t kLocalSep = L'\\';

//小于这个字节数时直接显示字节数
const int64_t kBytesShownExactly = 1024;

//大小的单位，从 KB 起
const wchar_t* const kSizeUnits[] = { L"KB", L"MB", L"GB", L"TB" };

//1 秒等于多少毫秒
const int64_t kMsPerSecond = 1000;

//struct tm 的年份从 1900 起、月份从 0 起
const int kTmYearBase = 1900;
const int kTmMonthBase = 1;

//显示文字的缓冲区字符数
const size_t kTextBufChars = 64;

//写操作结果文字的缓冲区字符数：含一段错误说明，比其余文字长
const size_t kResultTextBufChars = 256;

//按选中的列比较两个条目；目录始终在前
struct EntryLess
{
    int  m_column;      //排序列
    bool m_ascending;   //是否升序

    //名称比较：先不区分大小写，相同再区分大小写，保证结果确定
    static int CompareNames(const ShareEntry& a, const ShareEntry& b)
    {
        std::wstring wa;
        std::wstring wb;
        ShareUtf8ToWide(a.m_name, wa);
        ShareUtf8ToWide(b.m_name, wb);
        const int cmp = ::CompareStringOrdinal(wa.c_str(), static_cast<int>(wa.size()), wb.c_str(),
                                               static_cast<int>(wb.size()), TRUE);
        if (cmp == CSTR_LESS_THAN)
            return -1;
        if (cmp == CSTR_GREATER_THAN)
            return 1;
        return (wa < wb) ? -1 : ((wb < wa) ? 1 : 0);
    }

    bool operator()(const ShareEntry& a, const ShareEntry& b) const
    {
        if (a.m_isDir != b.m_isDir)
            return a.m_isDir;

        int cmp = 0;
        if (m_column == share_sort_size && !a.m_isDir)
            cmp = (a.m_size < b.m_size) ? -1 : ((a.m_size > b.m_size) ? 1 : 0);
        else if (m_column == share_sort_mtime)
            cmp = (a.m_mtime < b.m_mtime) ? -1 : ((a.m_mtime > b.m_mtime) ? 1 : 0);
        //大小、时间相同，或按名称排序：比较名称
        if (cmp == 0)
            cmp = CompareNames(a, b);
        return m_ascending ? (cmp < 0) : (cmp > 0);
    }
};

} // namespace

std::string ShareBrowseModel::JoinPath(const std::string& dir, const std::string& name)
{
    if (dir.empty())
        return name;
    return dir + "/" + name;
}

std::string ShareBrowseModel::ParentPath(const std::string& path)
{
    const size_t pos = path.find_last_of('/');
    if (pos == std::string::npos)
        return std::string();
    return path.substr(0, pos);
}

std::vector<std::string> ShareBrowseModel::SplitPath(const std::string& path)
{
    std::vector<std::string> parts;
    size_t begin = 0;
    while (begin < path.size())
    {
        size_t end = path.find('/', begin);
        if (end == std::string::npos)
            end = path.size();
        if (end > begin)
            parts.push_back(path.substr(begin, end - begin));
        begin = end + 1;
    }
    return parts;
}

void ShareBrowseModel::SortEntries(std::vector<ShareEntry>& entries, int column, bool ascending)
{
    EntryLess less;
    less.m_column = column;
    less.m_ascending = ascending;
    std::stable_sort(entries.begin(), entries.end(), less);
}

std::wstring ShareBrowseModel::FormatSize(int64_t bytes, bool isDir)
{
    if (isDir)
        return std::wstring();
    wchar_t buf[kTextBufChars] = { 0 };
    if (bytes < kBytesShownExactly)
    {
        _snwprintf_s(buf, kTextBufChars, _TRUNCATE, L"%lld B", static_cast<long long>(bytes < 0 ? 0 : bytes));
        return buf;
    }
    double value = static_cast<double>(bytes) / kSizeStep;
    size_t unit = 0;
    const size_t lastUnit = sizeof(kSizeUnits) / sizeof(kSizeUnits[0]) - 1;
    while (value >= kSizeStep && unit < lastUnit)
    {
        value /= kSizeStep;
        ++unit;
    }
    _snwprintf_s(buf, kTextBufChars, _TRUNCATE, L"%.1f %s", value, kSizeUnits[unit]);
    return buf;
}

std::wstring ShareBrowseModel::FormatTimeParts(int year, int month, int day, int hour, int minute)
{
    wchar_t buf[kTextBufChars] = { 0 };
    _snwprintf_s(buf, kTextBufChars, _TRUNCATE, L"%04d-%02d-%02d %02d:%02d", year, month, day, hour, minute);
    return buf;
}

std::wstring ShareBrowseModel::FormatTime(int64_t unixMs)
{
    if (unixMs <= 0)
        return std::wstring();
    const time_t seconds = static_cast<time_t>(unixMs / kMsPerSecond);
    struct tm local;
    if (localtime_s(&local, &seconds) != 0)
        return std::wstring();
    return FormatTimeParts(local.tm_year + kTmYearBase, local.tm_mon + kTmMonthBase, local.tm_mday, local.tm_hour,
                           local.tm_min);
}

std::wstring ShareBrowseModel::ErrorText(int code)
{
    switch (code)
    {
    //功能被管理员关闭
    case error_code_share_disabled:
        return TR(L"文件共享功能已被管理员关闭");
    //共享不存在
    case error_code_share_not_exist:
        return TR(L"共享不存在");
    //共享已被取消
    case error_code_share_canceled:
        return TR(L"共享已被取消");
    //不是成员或已被移出
    case error_code_share_not_member:
        return TR(L"你已不是这个共享的成员");
    //分享者不在线或不在绑定的电脑上
    case error_code_share_unavailable:
        return TR(L"分享者不在线，或不在共享所在的电脑上");
    //共享根已不存在
    case error_code_share_root_missing:
        return TR(L"共享的文件夹在分享者电脑上已不存在");
    //没有权限
    case error_code_share_permission_denied:
        return TR(L"没有这项操作的权限");
    //路径非法
    case error_code_share_path_invalid:
        return TR(L"路径无效");
    //文件或目录不存在
    case error_code_share_path_not_found:
        return TR(L"文件或文件夹不存在");
    //分享者电脑上读写失败
    case error_code_share_io_fail:
        return TR(L"分享者电脑上读取失败，文件可能被占用");
    //服务器的会话数已满
    case error_code_share_server_busy:
        return TR(L"服务器繁忙，请稍后再试");
    //分享者没有响应
    case error_code_share_owner_timeout:
        return TR(L"分享者暂时无法响应");
    //中转通道断开
    case error_code_share_relay_broken:
        return TR(L"与分享者的连接已断开");
    //操作与共享类型不符
    case error_code_share_kind_mismatch:
        return TR(L"这个共享不支持该操作");
    //跨企业
    case error_code_share_cross_enterprise:
        return TR(L"不能访问其他企业的人共享的文件");
    //其余错误码：通用提示并带上错误码，便于排查
    default:
    {
        wchar_t buf[kTextBufChars] = { 0 };
        _snwprintf_s(buf, kTextBufChars, _TRUNCATE, TR(L"操作失败（错误码 %d）"), code);
        return buf;
    }
    }
}

std::wstring ShareBrowseModel::StateText(int state, int reason)
{
    if (state == share_state_unavailable)
    {
        switch (reason)
        {
        //分享者不在线
        case share_unavailable_owner_offline:
            return TR(L"共享暂不可用：分享者不在线");
        //分享者在别的电脑上登录
        case share_unavailable_other_device:
            return TR(L"共享暂不可用：分享者不在共享所在的电脑上");
        //共享根不存在
        case share_unavailable_root_missing:
            return TR(L"共享暂不可用：共享的文件夹在分享者电脑上已不存在");
        //功能被关闭
        case share_unavailable_feature_disabled:
            return TR(L"共享暂不可用：文件共享功能已被管理员关闭");
        //其余原因
        default:
            return TR(L"共享暂不可用");
        }
    }
    if (state == share_state_invalid)
    {
        switch (reason)
        {
        //分享者取消
        case share_invalid_owner_canceled:
            return TR(L"共享已失效：分享者已取消共享");
        //群主或管理员取消
        case share_invalid_group_admin_canceled:
            return TR(L"共享已失效：群主或管理员已取消共享");
        //后台管理员取消
        case share_invalid_admin_canceled:
            return TR(L"共享已失效：管理员已取消共享");
        //群已解散
        case share_invalid_group_dismissed:
            return TR(L"共享已失效：群已解散");
        //分享者已退群
        case share_invalid_owner_left_group:
            return TR(L"共享已失效：分享者已退出该群");
        //本人被移出成员
        case share_invalid_member_removed:
            return TR(L"共享已失效：你已被移出共享成员");
        //本人已退群
        case share_invalid_member_left_group:
            return TR(L"共享已失效：你已退出该群");
        //其余原因
        default:
            return TR(L"共享已失效");
        }
    }
    //可用或未知：不显示横幅
    return std::wstring();
}

int ShareBrowseModel::ContextMenuItems(int perms, int kind, bool onRow, bool rowIsDir, bool rowIsImage)
{
    int items = share_menu_refresh;
    if (onRow)
    {
        items |= share_menu_download;
        if (rowIsDir && kind != share_kind_file)
            items |= share_menu_open;
        if (!rowIsDir && rowIsImage)
            items |= share_menu_preview;
    }
    //单文件共享没有目录，也不支持任何写操作
    if (kind == share_kind_file)
        return items;
    if (onRow && (perms & share_perm_rename) != 0)
        items |= share_menu_rename;
    if (onRow && (perms & share_perm_delete) != 0)
        items |= share_menu_delete;
    if (onRow && (perms & share_perm_move) != 0)
        items |= share_menu_move;
    if ((perms & share_perm_mkdir) != 0)
        items |= share_menu_new_folder;
    return items;
}

bool ShareBrowseModel::CheckNewName(const std::wstring& name, std::wstring& error)
{
    error.clear();
    if (name.empty())
    {
        error = TR(L"名称不能为空");
        return false;
    }
    if (name.size() > kShareNameMaxChars)
    {
        error = TR(L"名称太长");
        return false;
    }
    if (name.find_first_of(L"\\/:*?\"<>|") != std::wstring::npos)
    {
        error = TR(L"名称不能包含下列字符：\\ / : * ? \" < > |");
        return false;
    }
    //其余规则（首尾空格、末尾的点、系统保留的名称、控制字符）与分享者一端的路径检查共用一份
    if (!SharePathGuard::IsValidRelativePath(ShareWideToUtf8(name), false))
    {
        error = TR(L"名称不能以空格或点结尾，也不能使用系统保留的名称");
        return false;
    }
    return true;
}

std::wstring ShareBrowseModel::OpResultText(int opType, int code, const std::vector<ShareOpItem>& items)
{
    const wchar_t* verb = TR(L"移动");
    switch (opType)
    {
    //新建文件夹
    case sf_mkdir:
        verb = TR(L"新建文件夹");
        break;
    //重命名
    case sf_rename:
        verb = TR(L"重命名");
        break;
    //删除
    case sf_delete:
        verb = TR(L"删除");
        break;
    //移动，以及不认识的取值
    default:
        break;
    }

    size_t failed = 0;
    for (size_t i = 0; i < items.size(); ++i)
    {
        if (items[i].m_code != error_code_ok)
            ++failed;
    }
    const size_t total = items.empty() ? 1 : items.size();
    const size_t succeeded = items.empty() ? (code == error_code_ok ? 1 : 0) : total - failed;
    wchar_t buf[kResultTextBufChars] = { 0 };
    if (code == error_code_ok)
    {
        //新建与重命名只有一项，不带数量
        if (opType == sf_mkdir || opType == sf_rename)
            _snwprintf_s(buf, kResultTextBufChars, _TRUNCATE, TR(L"已%s"), verb);
        else
            _snwprintf_s(buf, kResultTextBufChars, _TRUNCATE, TR(L"已%s %u 项"), verb, static_cast<unsigned>(succeeded));
        return buf;
    }
    const std::wstring reason = ErrorText(code);
    if (succeeded > 0)
        _snwprintf_s(buf, kResultTextBufChars, _TRUNCATE, TR(L"%s了 %u 项，%u 项失败：%s"), verb, static_cast<unsigned>(succeeded),
                     static_cast<unsigned>(total - succeeded), reason.c_str());
    else
        _snwprintf_s(buf, kResultTextBufChars, _TRUNCATE, TR(L"%s失败：%s"), verb, reason.c_str());
    return buf;
}

int ShareBrowseModel::GridColumns(int width, const ShareGridMetrics& m)
{
    const int stride = m.m_tileWidth + m.m_gap;
    if (stride <= 0)
        return 1;
    //n 格占 n * 宽 + (n - 1) * 间距，再加两侧留白
    const int usable = width - m.m_padding * 2 + m.m_gap;
    const int columns = usable / stride;
    return (columns > 0) ? columns : 1;
}

int ShareBrowseModel::GridRows(size_t count, int columns)
{
    if (columns <= 0 || count == 0)
        return 0;
    return static_cast<int>((count + static_cast<size_t>(columns) - 1) / static_cast<size_t>(columns));
}

RECT ShareBrowseModel::GridTileRect(const RECT& rowRect, int column, const ShareGridMetrics& m)
{
    RECT rc;
    rc.left = rowRect.left + m.m_padding + column * (m.m_tileWidth + m.m_gap);
    rc.top = rowRect.top;
    rc.right = rc.left + m.m_tileWidth;
    rc.bottom = rc.top + m.m_tileHeight;
    return rc;
}

int ShareBrowseModel::GridIndexAt(int rowIndex, int x, int rowLeft, int columns, size_t count, const ShareGridMetrics& m)
{
    const int stride = m.m_tileWidth + m.m_gap;
    if (rowIndex < 0 || columns <= 0 || stride <= 0)
        return -1;
    const int offset = x - rowLeft - m.m_padding;
    if (offset < 0)
        return -1;
    const int column = offset / stride;
    //落在格子右侧的间距里
    if (column >= columns || offset - column * stride >= m.m_tileWidth)
        return -1;
    const size_t index = static_cast<size_t>(rowIndex) * static_cast<size_t>(columns) + static_cast<size_t>(column);
    return (index < count) ? static_cast<int>(index) : -1;
}

int ShareBrowseModel::MenuItemsForSelection(int items, size_t selectedCount)
{
    if (selectedCount <= 1)
        return items;
    return items & ~(share_menu_open | share_menu_preview | share_menu_rename);
}

std::wstring ShareBrowseModel::DeleteConfirmText(const std::vector<std::wstring>& names)
{
    wchar_t buf[kConfirmTextBufChars] = { 0 };
    const std::wstring first = names.empty() ? std::wstring() : names[0];
    if (names.size() <= 1)
        _snwprintf_s(buf, kConfirmTextBufChars, _TRUNCATE, TR(L"确定删除「%s」吗？\n删除的内容会放进分享者电脑的回收站。"), first.c_str());
    else
        _snwprintf_s(buf, kConfirmTextBufChars, _TRUNCATE, TR(L"确定删除「%s」等 %u 项吗？\n删除的内容会放进分享者电脑的回收站。"),
                     first.c_str(), static_cast<unsigned>(names.size()));
    return buf;
}

std::vector<std::vector<size_t> > ShareBrowseModel::GroupByParent(const std::vector<std::string>& paths)
{
    std::vector<std::vector<size_t> > groups;
    std::vector<std::string> parents;
    for (size_t i = 0; i < paths.size(); ++i)
    {
        const std::string parent = ParentPath(paths[i]);
        size_t g = 0;
        while (g < parents.size() && parents[g] != parent)
            ++g;
        if (g == parents.size())
        {
            parents.push_back(parent);
            groups.push_back(std::vector<size_t>());
        }
        groups[g].push_back(i);
    }
    return groups;
}

std::vector<std::vector<std::wstring> > ShareBrowseModel::GroupLocalByParent(const std::vector<std::wstring>& paths)
{
    std::vector<std::vector<std::wstring> > groups;
    std::vector<std::wstring> parents;
    for (size_t i = 0; i < paths.size(); ++i)
    {
        const size_t slash = paths[i].find_last_of(kLocalSep);
        std::wstring parent = (slash == std::wstring::npos) ? std::wstring() : paths[i].substr(0, slash);
        //本机文件系统不区分大小写：转成小写比较
        if (!parent.empty())
            ::CharLowerBuffW(&parent[0], static_cast<DWORD>(parent.size()));
        size_t g = 0;
        while (g < parents.size() && parents[g] != parent)
            ++g;
        if (g == parents.size())
        {
            parents.push_back(parent);
            groups.push_back(std::vector<std::wstring>());
        }
        groups[g].push_back(paths[i]);
    }
    return groups;
}

} // namespace fileshare
