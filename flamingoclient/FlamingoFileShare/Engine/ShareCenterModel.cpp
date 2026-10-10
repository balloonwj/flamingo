/**
 *  「文件共享」窗口的纯逻辑 ShareCenterModel 的实现，见 ShareCenterModel.h。
 *  balloonwj@qq.com   2026-10-03
 */
#include "stdafx.h"

#include "ShareCenterModel.h"

#include <stdio.h>   //_snwprintf_s

#include "ShareBrowseModel.h"
#include "ShareConversationModel.h"
#include "ShareCreateModel.h"   //kShareMemberPermColumns
#include "ShareWinPath.h"
#include "../../Source/net/Protocol.h"
#include "../../Source/Common/I18n/I18n.h"   // TR：界面文字的多语言查找

namespace fileshare {

namespace {

//各段之间的分隔
const wchar_t* const kSep = L" · ";

//五个写权限位的名字，与 kShareMemberPermColumns 的顺序一致
const wchar_t* const kPermNames[] = { TR_NOOP(L"上传"), TR_NOOP(L"新建文件夹"), TR_NOOP(L"重命名"), TR_NOOP(L"删除"), TR_NOOP(L"移动") };

//成员默认就有的权限：浏览与下载（不随写权限位变化）
const wchar_t* const kDownloadPerm = TR_NOOP(L"下载");
const wchar_t* const kBrowseAndDownload = TR_NOOP(L"浏览、下载");

//格式化文字的缓冲区字符数
const size_t kTextBufChars = 256;

//百分比的满值
const int64_t kPercentScale = 100;

//UTF-8 转宽字符串
std::wstring Wide(const std::string& utf8)
{
    std::wstring w;
    ShareUtf8ToWide(utf8, w);
    return w;
}

//转成小写（按系统的大小写规则，中文不受影响），用于不区分大小写的查找
std::wstring LowerText(const std::wstring& text)
{
    std::wstring out = text;
    if (!out.empty())
        ::CharLowerBuffW(&out[0], static_cast<DWORD>(out.size()));
    return out;
}

} // namespace

std::wstring ShareCenterModel::ActionVerb(int action)
{
    switch (action)
    {
    //分享者收完一个上传的文件
    case share_audit_upload_file:
        return TR(L"上传");
    //新建文件夹
    case share_audit_mkdir:
        return TR(L"新建文件夹");
    //重命名
    case share_audit_rename:
        return TR(L"重命名");
    //删除（放进回收站）
    case share_audit_delete:
        return TR(L"删除");
    //移动
    case share_audit_move:
        return TR(L"移动");
    //下载，以及不认识的取值
    default:
        return TR(L"下载");
    }
}

std::wstring ShareCenterModel::KindText(int kind)
{
    switch (kind)
    {
    //整个磁盘
    case share_kind_disk:
        return TR(L"磁盘");
    //单个文件
    case share_kind_file:
        return TR(L"文件");
    //文件夹，以及不认识的取值
    default:
        return TR(L"文件夹");
    }
}

std::wstring ShareCenterModel::PermsText(int perms, int kind)
{
    if (kind == share_kind_file)
        return TR(L"只能下载");
    //先写默认就有的下载，再按列的顺序接上写权限；没有写权限时写明还能浏览
    std::wstring out = TR(kDownloadPerm);
    bool anyWrite = false;
    for (size_t i = 0; i < kShareMemberPermColumnCount; ++i)
    {
        if ((perms & kShareMemberPermColumns[i]) == 0)
            continue;
        out += L"、";
        out += TR(kPermNames[i]);
        anyWrite = true;
    }
    return anyWrite ? out : std::wstring(TR(kBrowseAndDownload));
}

std::wstring ShareCenterModel::LocalStatusText(int localStatus)
{
    switch (localStatus)
    {
    //绑定别的设备
    case share_local_status_other_device:
        return TR(L"位于另一台电脑");
    //绑定本机但本机没有记录
    case share_local_status_missing_local:
        return TR(L"本机缺少记录");
    //已报告，或还不知道
    default:
        return std::wstring();
    }
}

std::wstring ShareCenterModel::MineSubtitle(const ShareListEntry& e, const std::wstring& conversation, int localStatus)
{
    std::wstring out = Wide(e.m_displayPath);
    if (!conversation.empty())
    {
        if (!out.empty())
            out += kSep;
        out += conversation;
    }
    wchar_t buf[kTextBufChars] = { 0 };
    _snwprintf_s(buf, kTextBufChars, _TRUNCATE, TR(L"%u 名成员"), static_cast<unsigned>(e.m_members.size()));
    if (!out.empty())
        out += kSep;
    out += buf;
    out += kSep;
    out += ShareConversationModel::StatusText(e.m_state, e.m_reason);
    const std::wstring local = LocalStatusText(localStatus);
    if (!local.empty())
        out += L"（" + local + L"）";
    return out;
}

std::wstring ShareCenterModel::SharedSubtitle(const ShareListEntry& e, const std::wstring& owner,
                                              const std::wstring& conversation)
{
    std::wstring out = TRF(L"{0}的共享", owner);   //{0} 为分享者名
    if (!conversation.empty())
        out += kSep + conversation;
    out += kSep;
    out += TR(L"我的权限：");
    out += PermsText(e.m_myPerms, e.m_kind);
    out += kSep;
    out += ShareConversationModel::StatusText(e.m_state, e.m_reason);
    return out;
}

std::vector<std::wstring> ShareCenterModel::PermChipTexts(int perms, int kind)
{
    std::vector<std::wstring> out;
    if (kind == share_kind_file)
    {
        out.push_back(TR(L"只能下载"));
        return out;
    }
    //第一个是默认就有的下载，后面按列的顺序接上写权限
    out.push_back(TR(kDownloadPerm));
    for (size_t i = 0; i < kShareMemberPermColumnCount; ++i)
    {
        if ((perms & kShareMemberPermColumns[i]) != 0)
            out.push_back(TR(kPermNames[i]));
    }
    return out;
}

std::wstring ShareCenterModel::StateChipText(int state)
{
    switch (state)
    {
    //分享者在线、共享可以访问
    case share_state_available:
        return TR(L"可用");
    //分享者离线、共享根暂时不在等原因，之后可能恢复
    case share_state_unavailable:
        return TR(L"暂不可用");
    //已取消、已被移出成员等，不会再恢复
    case share_state_invalid:
        return TR(L"已失效");
    //不认识的状态：不画胶囊
    default:
        return std::wstring();
    }
}

std::wstring ShareCenterModel::MineRowSubtitle(const ShareListEntry& e, const std::wstring& conversation)
{
    wchar_t buf[kTextBufChars] = { 0 };
    _snwprintf_s(buf, kTextBufChars, _TRUNCATE, TR(L"%u 名成员"), static_cast<unsigned>(e.m_members.size()));
    if (conversation.empty())
        return buf;
    return conversation + kSep + buf;
}

std::wstring ShareCenterModel::SharedRowSubtitle(const ShareListEntry& e, const std::wstring& owner)
{
    const std::wstring perms = PermsText(e.m_myPerms, e.m_kind);
    if (owner.empty())
        return perms;
    return owner + kSep + perms;
}

bool ShareCenterModel::MatchesFilter(const ShareListEntry& e, const std::wstring& conversation, const std::wstring& owner,
                                     const std::wstring& keyword)
{
    //去掉首尾空白后转小写；只有空白时视为没有关键字
    const size_t first = keyword.find_first_not_of(L" \t");
    if (first == std::wstring::npos)
        return true;
    const size_t last = keyword.find_last_not_of(L" \t");
    const std::wstring needle = LowerText(keyword.substr(first, last - first + 1));
    const std::wstring fields[] = { Wide(e.m_name), Wide(e.m_displayPath), conversation, owner };
    for (size_t i = 0; i < sizeof(fields) / sizeof(fields[0]); ++i)
    {
        if (LowerText(fields[i]).find(needle) != std::wstring::npos)
            return true;
    }
    return false;
}

int ShareCenterModel::MineActions(const ShareListEntry& e, int localStatus)
{
    if (e.m_state == share_state_invalid)
        return share_center_action_locate;
    int actions = share_center_action_edit | share_center_action_cancel | share_center_action_locate;
    if (localStatus == share_local_status_reported)
        actions |= share_center_action_open_local;
    return actions;
}

int ShareCenterModel::SharedActions(const ShareListEntry& e)
{
    if (e.m_state == share_state_invalid)
        return share_center_action_locate;
    return share_center_action_open | share_center_action_locate;
}

int64_t ShareCenterModel::ViewerCid(const ShareListEntry& e, int64_t selfId)
{
    //单聊里别人共享给本人的：会话是与分享者的单聊
    if (e.m_ctype == conversation_single && e.m_ownerId != selfId)
        return e.m_ownerId;
    return e.m_cid;
}

std::wstring ShareCenterModel::VisitorText(const ShareActiveVisitor& v, const std::wstring& name)
{
    const std::wstring who = name.empty() ? Wide(v.m_visitorName) : name;
    return who + L" " + VisitorStatusText(v);
}

std::wstring ShareCenterModel::VisitorStatusText(const ShareActiveVisitor& v)
{
    if (v.m_downloads == 0 && v.m_uploads == 0)
        return TR(L"正在浏览");
    wchar_t buf[kTextBufChars] = { 0 };
    if (v.m_downloads > 0 && v.m_uploads > 0)
        _snwprintf_s(buf, kTextBufChars, _TRUNCATE, TR(L"正在下载 %d 项、上传 %d 项"), v.m_downloads, v.m_uploads);
    else if (v.m_downloads > 0)
        _snwprintf_s(buf, kTextBufChars, _TRUNCATE, TR(L"正在下载 %d 项"), v.m_downloads);
    else
        _snwprintf_s(buf, kTextBufChars, _TRUNCATE, TR(L"正在上传 %d 项"), v.m_uploads);
    std::wstring out = buf;
    if (v.m_bytesTotal > 0)
    {
        const int64_t done = (v.m_bytesDone < v.m_bytesTotal) ? v.m_bytesDone : v.m_bytesTotal;
        _snwprintf_s(buf, kTextBufChars, _TRUNCATE, L"（%d%%）", static_cast<int>(done * kPercentScale / v.m_bytesTotal));
        out += buf;
    }
    return out;
}

std::wstring ShareCenterModel::ActivityText(const ShareActivityRecord& rec, const std::wstring& name,
                                            const std::wstring& timeText)
{
    std::wstring out = timeText;
    if (!out.empty())
        out += L"  ";
    out += name;
    out += L" ";
    out += ActionVerb(rec.m_action);
    out += L" ";
    out += Wide(rec.m_path);
    out += kSep;
    if (rec.m_result == error_code_ok)
        out += TR(L"成功");
    else
        out += TRF(L"失败：{0}", ShareBrowseModel::ErrorText(rec.m_result));
    return out;
}

} // namespace fileshare
