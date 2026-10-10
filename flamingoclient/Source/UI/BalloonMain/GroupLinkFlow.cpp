/**
 *  GroupLinkFlow.cpp
 *
 *  加群链接的纯逻辑，说明见 GroupLinkFlow.h 与 docs/邀请入群方案.md §12。
 *
 *  balloonwj@qq.com   2026-10-07
 */
#include "stdafx.h"
#include "GroupLinkFlow.h"

#include <time.h>

#include "../../net/Protocol.h"                  // error_code_*
#include "../../net/GroupLinkProtocolClient.h"   // kGroupLinkDays*、isJoinUrl、GroupLinksResult
#include "../../Common/Util/EncodeUtil.h"        // EncodeUtil::Utf8ToUnicode
#include "../../Common/I18n/I18n.h"              // TR / TRF：界面文字的多语言查找

namespace balloonmain {

namespace {

// 群的加入方式（与服务端 group_join_mode 一致）
const int kJoinModeFree = 0;
const int kJoinModeForbid = 2;

// 毫秒换算成秒
const int64_t kMillisPerSecond = 1000;

// 「月-日 时:分」格式化结果的缓冲区长度（字符数，含结尾的 0）
const size_t kMonthDayTimeBufLen = 32;

// 链接在命令行里的开头（小写）
const wchar_t* const kJoinUrlHead = L"flamingo://joingroup";

wchar_t LowerAscii(wchar_t c)
{
    if (c >= L'A' && c <= L'Z')
        return static_cast<wchar_t>(c - L'A' + L'a');
    return c;
}

// Unix 毫秒按本地时间写成「月-日 时:分」，与群公告时间的写法一致；换算失败时返回空串
std::wstring FormatMonthDayTime(int64_t timeMs)
{
    const time_t seconds = static_cast<time_t>(timeMs / kMillisPerSecond);
    struct tm local;
    if (localtime_s(&local, &seconds) != 0)
        return std::wstring();
    wchar_t buf[kMonthDayTimeBufLen] = { 0 };
    wcsftime(buf, kMonthDayTimeBufLen, L"%m-%d %H:%M", &local);
    return std::wstring(buf);
}

} // anonymous namespace

int DecideLinkInfoAction(int code, bool member, int joinMode)
{
    if (code != error_code_ok)
        return kLinkActionError;
    if (member)
        return kLinkActionOpenGroup;
    if (joinMode == kJoinModeForbid)
        return kLinkActionForbidden;
    return kLinkActionConfirmJoin;
}

std::wstring JoinLinkConfirmText(const std::wstring& groupName, int memberCount, int joinMode)
{
    if (joinMode == kJoinModeFree)
        return TRF(L"确定加入群聊「{0}」（{1} 人）吗？", groupName, memberCount);
    return TRF(L"加入群聊「{0}」（{1} 人）需要群主或管理员同意，确定提交加群申请吗？", groupName, memberCount);
}

std::wstring GroupLinkErrorText(int code)
{
    switch (code)
    {
    //链接不存在、已作废或已过期
    case error_code_group_link_invalid:
        return TR(L"这个加群链接已失效或已过期，请向群成员索取新的链接。");
    //链接只对生成者所在企业的同事有效
    case error_code_group_link_enterprise:
        return TR(L"这个加群链接只对生成者所在企业的同事有效。");
    //群设置为禁止加入（生成或加群时）
    case error_code_permission_denied:
        return TR(L"该群已设置为禁止加入，不能通过链接加群。");
    //生成或作废链接时本人已不在群里
    case error_code_not_group_member:
        return TR(L"你已不是该群成员。");
    //群已解散
    case error_code_group_not_exist:
        return TR(L"该群已解散。");
    //其余错误：带上错误码便于排查
    default:
        return TRF(L"操作没有成功（错误码 {0}），请稍后重试。", code);
    }
}

std::wstring JoinLinkPendingText()
{
    return TR(L"已提交加群申请，等待群主或管理员同意。");
}

int GroupLinkDaysByChoice(int index)
{
    switch (index)
    {
    //1 天
    case 0:
        return net::kGroupLinkDaysOne;
    //永久
    case 2:
        return net::kGroupLinkDaysForever;
    //7 天（默认），以及越界的下标
    default:
        return net::kGroupLinkDaysSeven;
    }
}

std::wstring GroupLinkChoiceText(int index)
{
    switch (index)
    {
    //第一项：1 天
    case 0:
        return TR(L"1 天");
    //第二项：7 天
    case 1:
        return TR(L"7 天");
    //第三项：永久有效
    case 2:
        return TR(L"永久有效");
    //越界：没有这一项
    default:
        return std::wstring();
    }
}

std::wstring GroupLinkExpireText(int64_t expireTimeMs)
{
    if (expireTimeMs <= 0)
        return TR(L"永久有效");
    const std::wstring when = FormatMonthDayTime(expireTimeMs);
    if (when.empty())
        return std::wstring();
    return TRF(L"有效期至 {0}", when);
}

bool ApplyMyGroupLinks(const net::GroupLinksResult& result, int64_t groupId, int64_t selfUserId, MyGroupLinkList& list)
{
    //失败的应答可能不带群号（只有 code 与 msg），这种情况只能认为是发给当前这个群的
    if (result.m_code == error_code_ok && result.m_groupid != groupId)
        return false;
    if (result.m_code != error_code_ok && result.m_groupid != 0 && result.m_groupid != groupId)
        return false;
    if (result.m_code != error_code_ok)
    {
        //已经加载成功过的保留：继续显示上一次的真实内容
        if (list.m_state != kMyLinksLoaded)
            list.m_state = kMyLinksFailed;
        return true;
    }
    list.m_rows.clear();
    for (size_t i = 0; i < result.m_links.size() && list.m_rows.size() < (size_t)kMyGroupLinkRowMax; ++i)
    {
        const net::GroupLinkItem& src = result.m_links[i];
        //群主、管理员拿到的是本群全部链接，别人生成的不列在「我生成的链接」里
        if (src.m_creator != selfUserId)
            continue;
        MyGroupLinkRow row;
        row.m_id = src.m_id;
        row.m_url = EncodeUtil::Utf8ToUnicode(src.m_url);
        row.m_metaText = TRF(L"{0} 生成 · {1}", FormatMonthDayTime(src.m_createtime), GroupLinkExpireText(src.m_expiretime));
        list.m_rows.push_back(row);
    }
    list.m_state = kMyLinksLoaded;
    return true;
}

std::wstring MyGroupLinksPlaceholderText(const MyGroupLinkList& list)
{
    switch (list.m_state)
    {
    //已发出请求、还没有成功的应答
    case kMyLinksLoading:
        return TR(L"正在加载…");
    //从没加载成功过
    case kMyLinksFailed:
        return TR(L"链接列表没有加载出来，请关闭本窗口后重新打开。");
    //已加载：有链接时不显示占位文字
    default:
        break;
    }
    if (!list.m_rows.empty())
        return std::wstring();
    return TR(L"你在本群还没有仍然有效的链接。");
}

std::wstring RevokeResultStatusText(int code)
{
    switch (code)
    {
    //已作废
    case error_code_ok:
        return TR(L"链接已作废，再点这个链接将无法加群。");
    //这条链接不是本人生成的：只有群主、管理员或生成者本人可以作废
    case error_code_permission_denied:
        return TR(L"只能作废自己生成的链接。");
    //链接已经失效：可能已被群主或管理员作废，或者刚好过期
    case error_code_group_link_invalid:
        return TR(L"这个链接已经失效，可能已被群主或管理员作废，或者已经过期。");
    //其余错误（不在群里、群已解散、服务端出错等）
    default:
        return TRF(L"作废没有成功：{0}", GroupLinkErrorText(code));
    }
}

GroupLinksDelivery DecideGroupLinksDelivery(int64_t resultGroupId, int64_t dialogGroupId, int64_t manageWindowGroupId)
{
    GroupLinksDelivery delivery;
    if (resultGroupId <= 0)
        return delivery;
    delivery.m_toDialog = (dialogGroupId == resultGroupId);
    delivery.m_toManageWindow = (manageWindowGroupId == resultGroupId);
    return delivery;
}

GroupLinkRevokeTracker::GroupLinkRevokeTracker()
    : m_linkId(0)
{
}

void GroupLinkRevokeTracker::Begin(int64_t linkId)
{
    m_linkId = (linkId > 0) ? linkId : 0;
}

void GroupLinkRevokeTracker::Cancel()
{
    m_linkId = 0;
}

bool GroupLinkRevokeTracker::IsWaiting() const
{
    return m_linkId > 0;
}

int64_t GroupLinkRevokeTracker::LinkId() const
{
    return m_linkId;
}

bool GroupLinkRevokeTracker::TakeResult()
{
    if (m_linkId <= 0)
        return false;
    m_linkId = 0;
    return true;
}

std::wstring FindJoinUrlInCommandLine(const std::wstring& commandLine)
{
    const std::wstring head(kJoinUrlHead);
    if (commandLine.size() < head.size())
        return std::wstring();
    for (size_t pos = 0; pos + head.size() <= commandLine.size(); ++pos)
    {
        bool same = true;
        for (size_t i = 0; i < head.size() && same; ++i)
        {
            if (LowerAscii(commandLine[pos + i]) != head[i])
                same = false;
        }
        if (!same)
            continue;
        //链接到空白或引号为止
        size_t end = pos + head.size();
        while (end < commandLine.size() && commandLine[end] != L' ' && commandLine[end] != L'\t'
               && commandLine[end] != L'"')
        {
            ++end;
        }
        const std::wstring url = commandLine.substr(pos, end - pos);
        if (net::GroupLinkProtocolClient::isJoinUrl(url))
            return url;
    }
    return std::wstring();
}

std::wstring BuildProtocolOpenCommand(const std::wstring& exePath)
{
    return L"\"" + exePath + L"\" \"%1\"";
}

} // namespace balloonmain
