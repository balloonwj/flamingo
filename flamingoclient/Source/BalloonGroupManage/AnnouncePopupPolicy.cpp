/**
 *  群公告弹窗判定与记账的实现，见 AnnouncePopupPolicy.h。
 *  balloonwj@qq.com   2026-08-03
 */

#include "AnnouncePopupPolicy.h"

#include <stdio.h>     //_stprintf_s：拼小节名与键名

namespace balloongroupmanage {

// 记账落在用户配置文件的这个小节里，键名是群ID的十进制文本。
// 单开一个小节而不是并进现有的 App / MainDlg，是因为它的键是<u>按群动态生长</u>的，
// 混进那些固定字段里会让配置文件读起来一团乱。
static const TCHAR* const kAnnouncePopupSection = _T("GroupAnnouncePopped");

// changetype 取值，与服务端 group_announcement_change_type 一致。
// 本模块刻意不 include net/Protocol.h —— 它与 v1 的 net/Msg.h 有同名枚举冲突，
// 而本模块要能被单测工程直接编译（与 GroupManageMapping 里的处理同理）。
static const int kAnnounceChangePublish = 1;   // 新发布

bool ShouldPopupAnnouncement(const AnnouncePopupInput& input)
{
    //公告ID非正：这条推送本身就不合法，界面上也无从定位它
    if (input.m_announceId <= 0)
    {
        return false;
    }

    //只有"新发布"才弹。撤回是管理动作，不该反过来打扰全群一次。
    if (input.m_changetype != kAnnounceChangePublish)
    {
        return false;
    }

    //自己发的不弹给自己 —— 刚点完"发布"就被自己的公告拦一下，只会让人以为出错了。
    //selfUserId 为 0（未登录）时这一条不生效，此时按"不是自己发的"处理。
    if (input.m_selfUserId > 0 && input.m_publisherId == input.m_selfUserId)
    {
        return false;
    }

    //不大于上次弹过的一律不弹：既挡掉断线重连后重复推来的同一条，也挡掉推送
    //乱序时补到的更旧的一条。
    if (input.m_announceId <= input.m_lastPoppedId)
    {
        return false;
    }

    return true;
}

int64_t LoadLastPoppedAnnounceId(LPCTSTR configPath, int64_t groupId)
{
    if (configPath == NULL || configPath[0] == _T('\0') || groupId <= 0)
    {
        return 0;
    }

    TCHAR szKey[32] = { 0 };
    _stprintf_s(szKey, _countof(szKey), _T("%lld"), static_cast<long long>(groupId));

    //公告ID是 64 位，GetPrivateProfileInt 只回 UINT，故按字符串读再自行解析
    TCHAR szValue[32] = { 0 };
    ::GetPrivateProfileString(kAnnouncePopupSection, szKey, _T("0"),
                              szValue, _countof(szValue), configPath);

    const long long parsed = _ttoi64(szValue);
    return (parsed > 0) ? static_cast<int64_t>(parsed) : 0;
}

void SaveLastPoppedAnnounceId(LPCTSTR configPath, int64_t groupId, int64_t announceId)
{
    if (configPath == NULL || configPath[0] == _T('\0') || groupId <= 0 || announceId <= 0)
    {
        return;
    }

    TCHAR szKey[32] = { 0 };
    _stprintf_s(szKey, _countof(szKey), _T("%lld"), static_cast<long long>(groupId));

    TCHAR szValue[32] = { 0 };
    _stprintf_s(szValue, _countof(szValue), _T("%lld"), static_cast<long long>(announceId));

    ::WritePrivateProfileString(kAnnouncePopupSection, szKey, szValue, configPath);
}

} // namespace balloongroupmanage
