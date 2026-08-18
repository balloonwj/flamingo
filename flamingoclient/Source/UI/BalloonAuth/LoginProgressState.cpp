/**
 *  登录窗「登录中」状态机的实现，LoginProgressState.cpp
 *  设计与轮次号的由来见 LoginProgressState.h。
 *  balloonwj@qq.com   2026-08-13
 */
#include "stdafx.h"

#include "LoginProgressState.h"

#include "../../App/UIText.h"   //UI_LOGIN_ERR_* 提示文案

namespace auth {

//第一个有效的轮次号。0 留给 kInvalidLoginGeneration 表示「没有正在进行的登录」，
//所以计数从 1 起。
static const uint32_t kFirstLoginGeneration = 1;

LoginProgressState::LoginProgressState()
    : m_phase(login_phase_idle),
      m_generation(kInvalidLoginGeneration),
      m_nextGeneration(kFirstLoginGeneration)
{
}

uint32_t LoginProgressState::startLogin()
{
    if (!canStartLogin())
    {
        //登录中重复点击、或已登录成功后又被触发。返回无效号让调用方别起线程，
        //且<u>不能动当前的轮次号</u>——那会让正在进行的那一轮的结果被误判成过期。
        return kInvalidLoginGeneration;
    }

    m_generation = m_nextGeneration;
    ++m_nextGeneration;
    m_phase = login_phase_logining;
    return m_generation;
}

bool LoginProgressState::cancelLogin()
{
    if (m_phase != login_phase_logining)
    {
        //不在登录中，无事可取消。这种情形出现在「结果刚到、界面已恢复，用户又点了
        //一下取消」这类竞态上，按无操作处理即可，不该把状态搅乱。
        return false;
    }

    //把本轮的号作废：后台线程稍后带着它回来时，shouldAcceptResult 会判为过期而丢弃。
    //不去中止那个线程——它阻塞在 socket 上，中止不了，只能等它自己回来。
    m_generation = kInvalidLoginGeneration;
    m_phase = login_phase_idle;
    return true;
}

void LoginProgressState::reset()
{
    //当前轮次号一并作废，复位之前那一轮的迟到结果因而对不上号；
    //m_nextGeneration 只增不减，不在此处动它。
    m_phase = login_phase_idle;
    m_generation = kInvalidLoginGeneration;
}

bool LoginProgressState::shouldAcceptResult(uint32_t generation) const
{
    if (m_phase != login_phase_logining)
    {
        //已取消（回到空闲）或已登录成功，此后到达的结果一律不再处理
        return false;
    }

    //号对不上说明这是更早一轮的结果，那一轮要么被取消、要么被新的一轮取代
    return generation != kInvalidLoginGeneration && generation == m_generation;
}

bool LoginProgressState::acceptSuccess(uint32_t generation)
{
    if (!shouldAcceptResult(generation))
        return false;

    m_phase = login_phase_done;
    //已成功，本轮结束；号一并作废，使之后任何迟到的结果都对不上
    m_generation = kInvalidLoginGeneration;
    return true;
}

bool LoginProgressState::acceptFailure(uint32_t generation)
{
    if (!shouldAcceptResult(generation))
        return false;

    m_phase = login_phase_idle;
    m_generation = kInvalidLoginGeneration;
    return true;
}

const wchar_t* LoginErrorText(long nLoginResultCode)
{
    switch (nLoginResultCode)
    {
    //网络层就没走通：连不上服务器、或收发超时
    case LOGIN_FAILED:
        return UI_LOGIN_ERR_NETWORK;

    //服务端明确答复该账号不存在
    case LOGIN_UNREGISTERED:
        return UI_LOGIN_ERR_UNREGISTERED;

    //服务端明确答复口令不对
    case LOGIN_PASSWORD_ERROR:
        return UI_LOGIN_ERR_PASSWORD;

    //服务端拒绝了本次登录（账号被停用等）
    case LOGIN_SERVER_REFUSED:
        return UI_LOGIN_ERR_REFUSED;

    //服务端在超时时间内没有应答
    case LOGIN_SERVER_NOT_RESPONSE:
        return UI_LOGIN_ERR_NO_RESPONSE;

    //客户端版本低于服务端要求，重试无用，须先升级
    case LOGIN_VERSION_TOO_OLD:
        return UI_LOGIN_ERR_VERSION_TOO_OLD;

    //兜底：出现了这里没列到的结果码。返回一句通用文案而不是空指针，
    //否则调用方拿去显示时会崩，而它本来只是想告诉用户"没登上"。
    default:
        return UI_LOGIN_ERR_UNKNOWN;
    }
}

} // namespace auth
