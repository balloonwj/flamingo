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
    //未登录时 m_strAccount 本就是空串，直接返回即可，无须额外判断登录标志：
    //各缓存路径函数收到空账号会返回空串，调用方按"没有缓存"处理。
    return CFlamingoClient::GetInstance().m_UserMgr.m_UserInfo.m_strAccount;
}

} // namespace net
