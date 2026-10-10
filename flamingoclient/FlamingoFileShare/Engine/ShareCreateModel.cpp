/**
 *  发起共享与调整成员的纯逻辑 ShareCreateModel 的实现，见 ShareCreateModel.h。
 *  balloonwj@qq.com   2026-10-03
 */
#include "stdafx.h"

#include "ShareCreateModel.h"

#include <windows.h>
#include <shlobj.h>       //SHGetKnownFolderPath
#include <knownfolders.h>
#include <stdio.h>        //_snwprintf_s
#include <wctype.h>       //iswspace：去掉共享名称首尾的空白
#include <algorithm>
#include <map>

#include "SharePathGuard.h"
#include "../../Source/Common/I18n/I18n.h"   // TR：界面文字的多语言查找

namespace fileshare {

namespace {

//GUID 文本的缓冲区字符数（含连字符与结尾的 '\0'）
const size_t kGuidTextChars = 40;

//「本地磁盘 (D:)」一类文字的缓冲区字符数
const size_t kDriveTextChars = MAX_PATH;

//驱动器根目录（"X:\"）的字符数
const size_t kDriveRootChars = 3;

//逻辑驱动器位图的位数：A..Z
const int kDriveLetters = 26;

//去掉结尾的反斜杠；盘符根目录保留
std::wstring Trim(const std::wstring& path)
{
    std::wstring s = path;
    while (s.size() > kDriveRootChars && s[s.size() - 1] == L'\\')
        s.erase(s.size() - 1);
    return s;
}

//取一个已知目录的路径追加到敏感位置；取不到时不追加
void AddKnownFolder(std::vector<ShareSensitivePath>& out, REFKNOWNFOLDERID id, bool withSubtree)
{
    PWSTR path = NULL;
    if (SUCCEEDED(::SHGetKnownFolderPath(id, 0, NULL, &path)) && path != NULL && path[0] != L'\0')
    {
        ShareSensitivePath item;
        item.m_path = path;
        item.m_withSubtree = withSubtree;
        out.push_back(item);
    }
    if (path != NULL)
        ::CoTaskMemFree(path);
}

} // namespace

//==================== CShareMemberTable ====================

CShareMemberTable::CShareMemberTable()
    : m_fileShare(false)
{
}

void CShareMemberTable::Reset(const std::vector<ShareMemberRow>& candidates, bool fileShare)
{
    m_fileShare = fileShare;
    m_rows.clear();
    for (size_t i = 0; i < candidates.size(); ++i)
    {
        ShareMemberRow row;
        row.m_userId = candidates[i].m_userId;
        row.m_name = candidates[i].m_name;
        m_rows.push_back(row);
    }
}

void CShareMemberTable::SetFixed(int64_t userId)
{
    ShareMemberRow* row = Find(userId);
    if (row == NULL)
        return;
    row->m_fixed = true;
    row->m_checked = true;
}

void CShareMemberTable::ApplyExisting(const std::vector<ShareMemberPerm>& members, const std::vector<ShareMemberRow>& names)
{
    for (size_t i = 0; i < members.size(); ++i)
    {
        ShareMemberRow* row = Find(members[i].m_userId);
        if (row == NULL)
        {
            ShareMemberRow added;
            added.m_userId = members[i].m_userId;
            for (size_t n = 0; n < names.size(); ++n)
            {
                if (names[n].m_userId == members[i].m_userId)
                    added.m_name = names[n].m_name;
            }
            m_rows.push_back(added);
            row = &m_rows.back();
        }
        row->m_checked = true;
        row->m_perms = m_fileShare ? 0 : members[i].m_perms;
    }
}

void CShareMemberTable::SetChecked(int64_t userId, bool checked)
{
    ShareMemberRow* row = Find(userId);
    if (row == NULL || (row->m_fixed && !checked))
        return;
    row->m_checked = checked;
    if (!checked)
        row->m_perms = 0;
}

void CShareMemberTable::SetPerm(int64_t userId, int perm, bool on)
{
    ShareMemberRow* row = Find(userId);
    if (row == NULL || !row->m_checked || m_fileShare)
        return;
    row->m_perms = on ? (row->m_perms | perm) : (row->m_perms & ~perm);
}

void CShareMemberTable::SetColumn(int perm, bool on)
{
    if (m_fileShare)
        return;
    for (size_t i = 0; i < m_rows.size(); ++i)
    {
        if (m_rows[i].m_checked)
            m_rows[i].m_perms = on ? (m_rows[i].m_perms | perm) : (m_rows[i].m_perms & ~perm);
    }
}

int CShareMemberTable::ColumnState(int perm) const
{
    size_t checked = 0;
    size_t withPerm = 0;
    for (size_t i = 0; i < m_rows.size(); ++i)
    {
        if (!m_rows[i].m_checked)
            continue;
        ++checked;
        if ((m_rows[i].m_perms & perm) != 0)
            ++withPerm;
    }
    if (withPerm == 0)
        return share_column_none;
    return (withPerm == checked) ? share_column_all : share_column_some;
}

const std::vector<ShareMemberRow>& CShareMemberTable::Rows() const
{
    return m_rows;
}

std::vector<ShareMemberPerm> CShareMemberTable::CheckedMembers() const
{
    std::vector<ShareMemberPerm> out;
    for (size_t i = 0; i < m_rows.size(); ++i)
    {
        if (!m_rows[i].m_checked)
            continue;
        ShareMemberPerm m;
        m.m_userId = m_rows[i].m_userId;
        m.m_perms = m_fileShare ? 0 : m_rows[i].m_perms;
        out.push_back(m);
    }
    return out;
}

bool CShareMemberTable::Diff(const std::vector<ShareMemberPerm>& before, std::vector<ShareMemberPerm>& add,
                             std::vector<int64_t>& remove, std::vector<ShareMemberPerm>& update) const
{
    add.clear();
    remove.clear();
    update.clear();
    std::map<int64_t, int> old;
    for (size_t i = 0; i < before.size(); ++i)
        old[before[i].m_userId] = before[i].m_perms;

    const std::vector<ShareMemberPerm> now = CheckedMembers();
    std::map<int64_t, int> cur;
    for (size_t i = 0; i < now.size(); ++i)
    {
        cur[now[i].m_userId] = now[i].m_perms;
        std::map<int64_t, int>::const_iterator it = old.find(now[i].m_userId);
        if (it == old.end())
            add.push_back(now[i]);
        else if (it->second != now[i].m_perms)
            update.push_back(now[i]);
    }
    for (size_t i = 0; i < before.size(); ++i)
    {
        if (cur.find(before[i].m_userId) == cur.end())
            remove.push_back(before[i].m_userId);
    }
    return !add.empty() || !remove.empty() || !update.empty();
}

void CShareMemberTable::MarkInvalid(const std::vector<int64_t>& userIds)
{
    for (size_t i = 0; i < m_rows.size(); ++i)
        m_rows[i].m_invalid = std::find(userIds.begin(), userIds.end(), m_rows[i].m_userId) != userIds.end();
}

bool CShareMemberTable::IsFileShare() const
{
    return m_fileShare;
}

void CShareMemberTable::SetFileShare(bool fileShare)
{
    m_fileShare = fileShare;
    if (!fileShare)
        return;
    for (size_t i = 0; i < m_rows.size(); ++i)
        m_rows[i].m_perms = 0;
}

ShareMemberRow* CShareMemberTable::Find(int64_t userId)
{
    for (size_t i = 0; i < m_rows.size(); ++i)
    {
        if (m_rows[i].m_userId == userId)
            return &m_rows[i];
    }
    return NULL;
}

//==================== ShareCreateModel ====================

std::wstring ShareCreateModel::DisplayName(const std::wstring& path, int kind, const std::wstring& label)
{
    const std::wstring p = Trim(path);
    if (kind == share_kind_disk)
    {
        const std::wstring letter = p.substr(0, 2);
        wchar_t buf[kDriveTextChars] = { 0 };
        _snwprintf_s(buf, kDriveTextChars, _TRUNCATE, L"%s (%s)", label.empty() ? TR(L"本地磁盘") : label.c_str(), letter.c_str());
        return buf;
    }
    const size_t pos = p.find_last_of(L'\\');
    return (pos == std::wstring::npos) ? p : p.substr(pos + 1);
}

bool ShareCreateModel::IsSensitivePath(const std::wstring& path, const std::vector<ShareSensitivePath>& sensitive)
{
    const std::wstring p = Trim(path);
    if (p.empty())
        return false;
    for (size_t i = 0; i < sensitive.size(); ++i)
    {
        if (sensitive[i].m_path.empty())
            continue;
        const std::wstring s = Trim(sensitive[i].m_path);
        //共享的就是敏感目录本身，或是它的上级（共享包含了它）
        if (SharePathGuard::IsUnderRoot(p, s))
            return true;
        //位于整棵子树都敏感的目录之下
        if (sensitive[i].m_withSubtree && SharePathGuard::IsUnderRoot(s, p))
            return true;
    }
    return false;
}

std::vector<ShareSensitivePath> ShareCreateModel::CollectSensitivePaths()
{
    std::vector<ShareSensitivePath> out;
    wchar_t windows[MAX_PATH] = { 0 };
    if (::GetWindowsDirectoryW(windows, MAX_PATH) > kDriveRootChars)
    {
        ShareSensitivePath win;
        win.m_path = windows;
        win.m_withSubtree = true;
        out.push_back(win);
        //系统盘根目录：只有共享整个系统盘时提示，其下的普通目录不提示
        ShareSensitivePath root;
        root.m_path = std::wstring(windows).substr(0, kDriveRootChars);
        out.push_back(root);
    }
    AddKnownFolder(out, FOLDERID_ProgramFiles, true);
    AddKnownFolder(out, FOLDERID_ProgramFilesX86, true);
    AddKnownFolder(out, FOLDERID_Profile, false);
    return out;
}

std::vector<ShareDriveInfo> ShareCreateModel::ListLocalDrives()
{
    std::vector<ShareDriveInfo> out;
    wchar_t windows[MAX_PATH] = { 0 };
    ::GetWindowsDirectoryW(windows, MAX_PATH);
    const DWORD mask = ::GetLogicalDrives();
    for (int i = 0; i < kDriveLetters; ++i)
    {
        if ((mask & (1u << i)) == 0)
            continue;
        std::wstring root;
        root.push_back(static_cast<wchar_t>(L'A' + i));
        root += L":\\";
        const UINT type = ::GetDriveTypeW(root.c_str());
        if (type != DRIVE_FIXED && type != DRIVE_REMOVABLE)
            continue;
        ShareDriveInfo drive;
        drive.m_root = root;
        drive.m_removable = (type == DRIVE_REMOVABLE);
        drive.m_system = (::CompareStringOrdinal(root.c_str(), 2, windows, 2, TRUE) == CSTR_EQUAL);
        wchar_t label[MAX_PATH] = { 0 };
        if (::GetVolumeInformationW(root.c_str(), label, MAX_PATH, NULL, NULL, NULL, NULL, 0))
            drive.m_label = label;
        out.push_back(drive);
    }
    return out;
}

std::string ShareCreateModel::NewClientShareId()
{
    GUID guid;
    if (FAILED(::CoCreateGuid(&guid)))
        return std::string();
    char buf[kGuidTextChars] = { 0 };
    _snprintf_s(buf, kGuidTextChars, _TRUNCATE, "%08lx-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x",
                static_cast<unsigned long>(guid.Data1), guid.Data2, guid.Data3, guid.Data4[0], guid.Data4[1],
                guid.Data4[2], guid.Data4[3], guid.Data4[4], guid.Data4[5], guid.Data4[6], guid.Data4[7]);
    return buf;
}

std::wstring ShareCreateModel::CreateErrorText(int code)
{
    switch (code)
    {
    //成员跨企业
    case error_code_share_cross_enterprise:
        return TR(L"不能与其他企业的人共享文件，已标出这些成员");
    //成员不属于这个会话
    case error_code_share_invalid_member:
        return TR(L"有成员已不在这个会话里，已标出这些成员");
    //共享数达到上限
    case error_code_share_too_many:
        return TR(L"你的共享数量已达上限，请先取消不再需要的共享");
    //功能被关闭
    case error_code_share_disabled:
        return TR(L"文件共享功能已被管理员关闭");
    //共享名称命中敏感词（2026-10-04 起）
    case error_code_share_name_sensitive:
        return TR(L"共享名称包含敏感词，请修改后再发起");
    //调整成员时共享已不存在或已被取消
    case error_code_share_not_exist:
    case error_code_share_canceled:
        return TR(L"这个共享已被取消");
    //没有设备标识（登录太早或服务端版本不对）
    case error_code_bad_request:
        return TR(L"请求格式有误，请重新登录后再试");
    //其余错误码
    default:
    {
        wchar_t buf[kDriveTextChars] = { 0 };
        _snwprintf_s(buf, kDriveTextChars, _TRUNCATE, TR(L"操作失败（错误码 %d）"), code);
        return buf;
    }
    }
}

namespace {

//多人共享群名的整句模板（只做标记，生成时再取译文）：{0} 为共享名，{1} 为成员名列表（可能以「等 N 人」收尾）
const wchar_t* const kGroupNameShareOnly    = TR_NOOP(L"共享 {0}");
const wchar_t* const kGroupNameWithMembers  = TR_NOOP(L"共享 {0} 给 {1}");
const wchar_t* const kGroupNameSep      = L"、";
const wchar_t* const kGroupNameMoreFmt  = TR_NOOP(L"等%u人");
const wchar_t* const kGroupNameEllipsis = L"…";

//「等 N 人」文字缓冲区的字符数
const size_t kGroupNameMoreBufChars = 32;

//前 count 个成员名用「、」连起来
std::wstring JoinMemberNames(const std::vector<std::wstring>& names, size_t count)
{
    std::wstring out;
    for (size_t i = 0; i < count && i < names.size(); ++i)
    {
        if (i > 0)
            out += kGroupNameSep;
        out += names[i];
    }
    return out;
}

//「等 N 人」
std::wstring MoreMembersText(size_t total)
{
    wchar_t buf[kGroupNameMoreBufChars] = { 0 };
    _snwprintf_s(buf, kGroupNameMoreBufChars, _TRUNCATE, TR(kGroupNameMoreFmt), static_cast<unsigned>(total));
    return buf;
}

//截到至多 maxChars 个 UTF-16 码元；截断处落在代理对中间时少取一个，不留下孤立的高位代理
std::wstring CutUtf16(const std::wstring& text, size_t maxChars)
{
    if (text.size() <= maxChars)
        return text;
    size_t n = maxChars;
    const wchar_t kHighSurrogateFirst = 0xD800;
    const wchar_t kHighSurrogateLast = 0xDBFF;
    if (n > 0 && text[n - 1] >= kHighSurrogateFirst && text[n - 1] <= kHighSurrogateLast)
        --n;
    return text.substr(0, n);
}

//按整句模板生成群名：没有成员名列表时用「共享 {0}」，否则用「共享 {0} 给 {1}」
std::wstring FormatGroupName(const std::wstring& sharePart, const std::wstring& membersPart)
{
    if (membersPart.empty())
        return TRF(kGroupNameShareOnly, sharePart);
    return TRF(kGroupNameWithMembers, sharePart, membersPart);
}

} // namespace

std::wstring ShareCreateModel::DirectShareGroupName(const std::wstring& shareName,
                                                    const std::vector<std::wstring>& memberNames, size_t maxChars)
{
    if (memberNames.empty())
        return CutUtf16(FormatGroupName(shareName, std::wstring()), maxChars);

    //全部列出
    const std::wstring full = FormatGroupName(shareName, JoinMemberNames(memberNames, memberNames.size()));
    if (full.size() <= maxChars)
        return full;

    //依次少列几个人，以「等 N 人」收尾
    const std::wstring more = MoreMembersText(memberNames.size());
    for (size_t count = memberNames.size() - 1; count >= 1; --count)
    {
        const std::wstring candidate = FormatGroupName(shareName, JoinMemberNames(memberNames, count) + more);
        if (candidate.size() <= maxChars)
            return candidate;
    }

    //只列一个人仍放不下：截短共享名，以「…」收尾。共享名以外的部分（模板文字与成员名）占用的长度
    //由共享名为空时生成的群名得出
    const std::wstring membersPart = memberNames[0] + (memberNames.size() > 1 ? more : std::wstring());
    const std::wstring ellipsis = kGroupNameEllipsis;
    const size_t fixedChars = FormatGroupName(std::wstring(), membersPart).size() + ellipsis.size();
    if (fixedChars < maxChars)
    {
        const size_t budget = maxChars - fixedChars;
        return FormatGroupName(CutUtf16(shareName, budget) + ellipsis, membersPart);
    }

    //成员名本身就超长：整体截断
    return CutUtf16(FormatGroupName(shareName, membersPart), maxChars);
}

std::wstring ShareCreateModel::DefaultShareName(const std::wstring& selfName)
{
    //不知道本人名字时用「我的文件共享」整句，不拼「我」+「的文件共享」（与屏幕共享的默认标题一致）
    if (selfName.empty())
        return TR(L"我的文件共享");
    return TRF(L"{0}的文件共享", selfName);
}

std::wstring ShareCreateModel::ResolveShareName(const std::wstring& input, const std::wstring& selfName)
{
    //首尾空白（含全角空格）不算名字的一部分；只有空白时视同没有起名
    const wchar_t kFullWidthSpace = 0x3000;
    size_t begin = 0;
    size_t end = input.size();
    while (begin < end && (::iswspace(input[begin]) || input[begin] == kFullWidthSpace))
        ++begin;
    while (end > begin && (::iswspace(input[end - 1]) || input[end - 1] == kFullWidthSpace))
        --end;
    if (begin == end)
        return CutUtf16(DefaultShareName(selfName), kShareTitleNameMaxChars);
    return CutUtf16(input.substr(begin, end - begin), kShareTitleNameMaxChars);
}

} // namespace fileshare
