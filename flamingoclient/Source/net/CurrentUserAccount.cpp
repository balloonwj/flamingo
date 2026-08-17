/**
 *  当前登录账号的统一取值口实现，CurrentUserAccount.cpp
 *
 *  依赖全局登录态（CFlamingoClient 单例），故不进 FlamingoUnit 测试工程 ——
 *  可单测的那部分（路径拼接与账号净化）在 net/UserCachePath.cpp 里。
 *
 *  balloonwj@qq.com   2026-08-11
 */

#include "stdafx.h"
#include "CurrentUserAccount.h"

#include "../FlamingoClient.h"   // CFlamingoClient::GetInstance / m_UserMgr.m_UserInfo

namespace net {

std::wstring CurrentUserAccount()
{
    //取进程内正在使用的那个 CFlamingoClient（即 CMainDlg 的成员对象 m_FMGClient，
    //由 CMainDlg 构造时登记）。尚未登记（进程刚起、或 CMainDlg 已析构）时返回 NULL，
    //此处按"取不到登录态"降级为空串。不在这里补记日志：GetInstance 内部已记过一条
    //LOG_ERROR，而本函数在联系人 / 会话列表渲染时逐行调用，重复记会把日志刷满。
    CFlamingoClient* lpClient = CFlamingoClient::GetInstance();
    if (lpClient == NULL)
    {
        return std::wstring();
    }

    //已登记但尚未登录时 m_strAccount 本就是空串，直接返回即可，无须额外判断登录标志：
    //各缓存路径函数收到空账号会返回空串，调用方按"没有缓存"处理。
    return lpClient->m_UserMgr.m_UserInfo.m_strAccount;
}

} // namespace net
