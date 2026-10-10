/**
 *  文件共享弹窗通知的纯逻辑，见 ShareNoticeModel.h。
 *  balloonwj@qq.com   2026-10-07
 */
#include "stdafx.h"

#include "ShareNoticeModel.h"

#include <time.h>     //localtime_s
#include <stdio.h>    //_snwprintf_s

#include "ShareBrowseModel.h"
#include "ShareWinPath.h"
#include "../../Source/net/Protocol.h"
#include "../../Source/Common/I18n/I18n.h"   // TR / TRF：界面文字的多语言查找

namespace fileshare {

namespace {

//Unix 毫秒换算成秒
const int64_t kMsPerSecond = 1000;

//「时:分」的缓冲区字符数
const size_t kClockBufChars = 16;

//共享内位置里各级目录之间的分隔（与浏览窗口路径栏一致）
const wchar_t* const kLocationSeparator = L" / ";

//UTF-8 转成界面用的宽字符串
std::wstring Wide(const std::string& utf8)
{
    std::wstring w;
    ShareUtf8ToWide(utf8, w);
    return w;
}

} // namespace

bool ShareNoticeModel::ShouldNotifyVisitorTransfer(const ShareVisitorTransferResult& result, bool browserForeground)
{
    //自己点了「取消」：用户知道结果
    if (result.m_userCanceled)
        return false;
    //拖出到资源管理器：资源管理器的复制进度框会显示结果，下载到的是临时目录，弹窗里也打不开它
    if (result.m_dragOut)
        return false;
    //正看着浏览窗口：传输列表里已经显示了结果
    if (browserForeground)
        return false;
    return true;
}

bool ShareNoticeModel::IsOwnerActionNotified(int action)
{
    switch (action)
    {
    //下载：取走了分享者的文件
    case share_audit_download_file:
    //上传：往分享者电脑上写了文件
    case share_audit_upload_file:
    //重命名：改了分享者文件的名字
    case share_audit_rename:
    //删除：把分享者的文件放进了回收站
    case share_audit_delete:
    //移动：挪动了分享者的文件
    case share_audit_move:
        return true;

    //新建文件夹（需求方确认不通知）与服务端记录的打开、申请、发起等动作
    default:
        return false;
    }
}

std::wstring ShareNoticeModel::OwnerTitle(const std::wstring& who, int action, const std::wstring& first, size_t count,
                                          const std::wstring& target)
{
    const bool many = count > 1;
    switch (action)
    {
    //下载：「张三 下载了 报告.docx」「张三 下载了 报告.docx 等 3 项」
    case share_audit_download_file:
        return many ? TRF(L"{0} 下载了 {1} 等 {2} 项", who, first, static_cast<unsigned long long>(count))
                    : TRF(L"{0} 下载了 {1}", who, first);

    //上传
    case share_audit_upload_file:
        return many ? TRF(L"{0} 上传了 {1} 等 {2} 项", who, first, static_cast<unsigned long long>(count))
                    : TRF(L"{0} 上传了 {1}", who, first);

    //重命名：一次只改一项
    case share_audit_rename:
        return TRF(L"{0} 把 {1} 重命名为 {2}", who, first, target);

    //删除（放进了分享者电脑的回收站）
    case share_audit_delete:
        return many ? TRF(L"{0} 删除了 {1} 等 {2} 项", who, first, static_cast<unsigned long long>(count))
                    : TRF(L"{0} 删除了 {1}", who, first);

    //移动：目标为共享根时说「共享的根目录」
    case share_audit_move:
        if (target.empty())
            return many ? TRF(L"{0} 把 {1} 等 {2} 项移动到共享的根目录", who, first, static_cast<unsigned long long>(count))
                        : TRF(L"{0} 把 {1} 移动到共享的根目录", who, first);
        return many ? TRF(L"{0} 把 {1} 等 {2} 项移动到「{3}」", who, first, static_cast<unsigned long long>(count), target)
                    : TRF(L"{0} 把 {1} 移动到「{2}」", who, first, target);

    //其余动作不通知
    default:
        return std::wstring();
    }
}

std::wstring ShareNoticeModel::FailureReason(int code)
{
    switch (code)
    {
    //上传遇到同名：一律不覆盖，由上传者改名后再传
    case error_code_share_name_conflict:
        return TR(L"目标位置已有同名的文件或文件夹");
    //接收方磁盘空间不足
    case error_code_share_disk_full:
        return TR(L"磁盘空间不足");
    //该共享同时传输的人数已满（方案 2.4 定下的提示）
    case error_code_share_transfer_busy:
        return TR(L"对方同时传输的人数已满，请稍后再试");
    //不是本人点的取消：服务端结束了传输、退出登录等
    case error_code_share_transfer_canceled:
        return TR(L"传输被中止");
    //其余与浏览窗口用同一张文字表
    default:
        return ShareBrowseModel::ErrorText(code);
    }
}

std::wstring ShareNoticeModel::FormatClock(int64_t unixMs)
{
    if (unixMs <= 0)
        return std::wstring();
    const time_t seconds = static_cast<time_t>(unixMs / kMsPerSecond);
    struct tm local;
    if (localtime_s(&local, &seconds) != 0)
        return std::wstring();
    wchar_t buf[kClockBufChars] = { 0 };
    _snwprintf_s(buf, kClockBufChars, _TRUNCATE, L"%02d:%02d", local.tm_hour, local.tm_min);
    return buf;
}

std::wstring ShareNoticeModel::LocationText(const std::string& shareName, const std::string& dir)
{
    std::wstring text = Wide(shareName);
    const std::vector<std::string> parts = ShareBrowseModel::SplitPath(dir);
    for (size_t i = 0; i < parts.size(); ++i)
    {
        if (!text.empty())
            text += kLocationSeparator;
        text += Wide(parts[i]);
    }
    if (text.empty())
        return TR(L"共享的根目录");
    return TRF(L"「{0}」", text);
}

std::wstring ShareNoticeModel::OverflowText(size_t count)
{
    return TRF(L"还有 {0} 条", static_cast<unsigned long long>(count));
}

std::string ShareNoticeModel::LastSegment(const std::string& path)
{
    const std::vector<std::string> parts = ShareBrowseModel::SplitPath(path);
    return parts.empty() ? std::string() : parts.back();
}

ShareNoticeEntry ShareNoticeModel::MakeVisitorEntry(const ShareVisitorTransferResult& result)
{
    ShareNoticeEntry entry;
    const bool ok = (result.m_code == error_code_ok);
    const bool download = (result.m_direction == share_transfer_download);
    if (download)
        entry.m_title = ok ? TRF(L"下载完成：{0}", result.m_label) : TRF(L"下载失败：{0}", result.m_label);
    else
        entry.m_title = ok ? TRF(L"上传完成：{0}", result.m_label) : TRF(L"上传失败：{0}", result.m_label);
    entry.m_time = FormatClock(result.m_timeMs);
    entry.m_failed = !ok;

    if (!ok)
    {
        entry.m_detail = FailureReason(result.m_code);
        //有重试所需的条目时才给「重试」
        const bool canRetry = download ? !result.m_downloadItems.empty() : !result.m_uploadItems.empty();
        if (canRetry)
        {
            ShareNoticeLink retry;
            retry.m_action = share_notice_retry;
            retry.m_text = TR(L"重试");
            entry.m_links.push_back(retry);
        }
        return entry;
    }

    if (download)
    {
        entry.m_detail = TRF(L"已保存到 {0}", result.m_targetDir);
        entry.m_detailIsPath = true;
        //只有一项且知道它落地后的路径时才能「打开」；多项时打开哪一个说不清，只给「打开所在文件夹」
        if (result.m_topCount == 1 && !result.m_localTopPath.empty())
        {
            ShareNoticeLink open;
            open.m_action = share_notice_open_file;
            open.m_text = TR(L"打开");
            entry.m_links.push_back(open);
        }
        if (!result.m_localTopPath.empty() || !result.m_targetDir.empty())
        {
            ShareNoticeLink folder;
            folder.m_action = share_notice_open_folder;
            folder.m_text = TR(L"打开所在文件夹");
            entry.m_links.push_back(folder);
        }
        return entry;
    }

    //位置文字以全角括号「」开头（或是「共享的根目录」），前面不留空格
    entry.m_detail = TRF(L"已上传到{0}", LocationText(result.m_shareName, result.m_toDir));
    ShareNoticeLink browse;
    browse.m_action = share_notice_browse;
    browse.m_text = TR(L"查看");
    entry.m_links.push_back(browse);
    return entry;
}

ShareNoticeEntry ShareNoticeModel::MakeOwnerEntry(const ShareOwnerActivity& activity, const std::wstring& visitorName)
{
    ShareNoticeEntry entry;
    //移动的目标是多级目录时，各级之间与浏览窗口路径栏一样用「 / 」分隔；重命名的目标就是新名字
    std::wstring target;
    if (activity.m_action == share_audit_move)
    {
        const std::vector<std::string> parts = ShareBrowseModel::SplitPath(activity.m_targetName);
        for (size_t i = 0; i < parts.size(); ++i)
        {
            if (!target.empty())
                target += kLocationSeparator;
            target += Wide(parts[i]);
        }
    }
    else
    {
        target = Wide(activity.m_targetName);
    }
    entry.m_title = OwnerTitle(visitorName, activity.m_action, Wide(activity.m_firstName), activity.m_count, target);
    entry.m_time = FormatClock(activity.m_timeMs);
    if (!activity.m_shareName.empty())
        entry.m_detail = TRF(L"共享「{0}」", Wide(activity.m_shareName));

    //上传、重命名、移动：在资源管理器里选中那一项；拼不出本机路径时（本机记录已被清理）退回「查看」
    ShareNoticeLink link;
    const bool revealable = (activity.m_action == share_audit_upload_file || activity.m_action == share_audit_rename
                             || activity.m_action == share_audit_move);
    if (revealable && !activity.m_revealPath.empty())
    {
        link.m_action = share_notice_reveal;
        link.m_text = TR(L"在文件夹中显示");
    }
    else
    {
        link.m_action = share_notice_view_share;
        link.m_text = TR(L"查看");
    }
    entry.m_links.push_back(link);
    return entry;
}

bool ShareNoticeModel::MakeWriteActivity(const std::vector<ShareAuditEntry>& audits, ShareOwnerActivity& out)
{
    out = ShareOwnerActivity();
    std::vector<const ShareAuditEntry*> done;
    for (size_t i = 0; i < audits.size(); ++i)
    {
        if (audits[i].m_result == error_code_ok)
            done.push_back(&audits[i]);
    }
    if (done.empty())
        return false;
    const ShareAuditEntry& first = *done[0];
    //写操作里只有重命名、删除、移动通知；新建文件夹不通知
    if (first.m_action != share_audit_rename && first.m_action != share_audit_delete && first.m_action != share_audit_move)
        return false;

    out.m_shareId = first.m_shareId;
    out.m_visitorId = first.m_visitorId;
    out.m_action = first.m_action;
    out.m_count = done.size();
    out.m_firstName = LastSegment(first.m_path);
    out.m_timeMs = first.m_clientTime;
    switch (first.m_action)
    {
    //重命名：m_toPath 是新名字，新路径在原来的目录下
    case share_audit_rename:
        out.m_targetName = first.m_toPath;
        out.m_revealRelPath = ShareBrowseModel::JoinPath(ShareBrowseModel::ParentPath(first.m_path), first.m_toPath);
        break;

    //移动：m_toPath 是目标目录，第一项移过去之后在它下面
    case share_audit_move:
        out.m_targetName = first.m_toPath;
        out.m_revealRelPath = ShareBrowseModel::JoinPath(first.m_toPath, out.m_firstName);
        break;

    //删除：放进了回收站，没有可选中的位置
    default:
        break;
    }
    return true;
}

bool ShareNoticeModel::MakeTransferActivity(int direction, const std::vector<std::string>& paths, const std::string& toDir,
                                            ShareOwnerActivity& out)
{
    out = ShareOwnerActivity();
    if (paths.empty())
        return false;
    out.m_count = paths.size();
    if (direction == share_transfer_download)
    {
        //单文件共享的条目是空串：名字由 CShareService 按共享根补上
        out.m_action = share_audit_download_file;
        out.m_firstName = LastSegment(paths[0]);
        return true;
    }
    if (direction == share_transfer_upload)
    {
        out.m_action = share_audit_upload_file;
        out.m_firstName = paths[0];
        out.m_revealRelPath = ShareBrowseModel::JoinPath(toDir, paths[0]);
        return true;
    }
    return false;
}

CShareNoticeList::CShareNoticeList()
    : m_nextId(1)
{
}

uint32_t CShareNoticeList::Add(const ShareNoticeEntry& entry)
{
    ShareNoticeEntry copy = entry;
    copy.m_id = m_nextId++;
    m_entries.push_front(copy);
    while (m_entries.size() > kShareNoticeKeep)
        m_entries.pop_back();
    return copy.m_id;
}

bool CShareNoticeList::Remove(uint32_t id)
{
    for (std::deque<ShareNoticeEntry>::iterator it = m_entries.begin(); it != m_entries.end(); ++it)
    {
        if (it->m_id == id)
        {
            m_entries.erase(it);
            return true;
        }
    }
    return false;
}

void CShareNoticeList::Clear()
{
    m_entries.clear();
}

size_t CShareNoticeList::Size() const
{
    return m_entries.size();
}

std::vector<ShareNoticeEntry> CShareNoticeList::Visible() const
{
    std::vector<ShareNoticeEntry> out;
    for (size_t i = 0; i < m_entries.size() && i < kShareNoticeVisibleRows; ++i)
        out.push_back(m_entries[i]);
    return out;
}

size_t CShareNoticeList::OverflowCount() const
{
    return (m_entries.size() > kShareNoticeVisibleRows) ? m_entries.size() - kShareNoticeVisibleRows : 0;
}

const ShareNoticeEntry* CShareNoticeList::Find(uint32_t id) const
{
    for (size_t i = 0; i < m_entries.size(); ++i)
    {
        if (m_entries[i].m_id == id)
            return &m_entries[i];
    }
    return NULL;
}

} // namespace fileshare
