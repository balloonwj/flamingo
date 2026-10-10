/**
 *  「清理聊天记录」对话框流程状态的实现，ChatCleanFlow.cpp
 *  各阶段的走向见 ChatCleanFlow.h。
 *
 *  本 .cpp 不 include Flamingo 的 stdafx.h —— 它要被 Flamingo.vcxproj 与
 *  FlamingoUnit.vcxproj 共用。在 Flamingo.vcxproj 里需标记 PrecompiledHeader=NotUsing。
 *
 *  balloonwj@qq.com   2026-10-01
 */

#include "ChatCleanFlow.h"
#include "ChatCleanRange.h"
#include "../../net/Protocol.h"   // error_code：失败原因按错误码区分

#include <stdio.h>   // swprintf_s（带错误码的失败原因）
#include "../../Common/I18n/I18n.h"   // TR：界面文字的多语言查找

namespace balloonsettings {

namespace {

//各种情况下按钮下方的提示文字
const wchar_t* const kHintOffline      = TR_NOOP(L"未连接服务器，连接后才能清理");
const wchar_t* const kHintDisconnected = TR_NOOP(L"与服务器的连接已断开，请重连后重试");
const wchar_t* const kHintTimeout      = TR_NOOP(L"服务器没有响应，请稍后重试");
const wchar_t* const kHintBadRequest   = TR_NOOP(L"清理范围无效，请重新选择");
const wchar_t* const kHintServerError  = TR_NOOP(L"服务器处理失败，请稍后重试");

//按钮文字
const wchar_t* const kButtonIdle    = TR_NOOP(L"清理");
const wchar_t* const kButtonRunning = TR_NOOP(L"清理中…");

//带错误码的失败原因的缓冲区长度（文字加上一个十进制整数，留足余量）
const int kHintBufLen = 64;

} // anonymous namespace

ChatCleanFlow::ChatCleanFlow()
    : m_stage(kCleanStageIdle)
    , m_preset(kCleanAll)
    , m_connected(false)
    , m_awaitingResult(false)
    , m_fillDepth(0)
{
}

void ChatCleanFlow::Reset(int initialPreset)
{
    m_stage = kCleanStageIdle;
    m_preset = (initialPreset >= 0 && initialPreset < kCleanPresetCount) ? initialPreset : kCleanAll;
    m_awaitingResult = false;
    m_fillDepth = 0;
    m_hint.clear();
    RefreshOfflineHint();
}

void ChatCleanFlow::SetConnected(bool connected)
{
    const bool wasConnected = m_connected;
    m_connected = connected;

    //清理中断开连接：这次请求的应答不会再到达，回到等待操作，并说明原因
    if (!connected && wasConnected && m_stage == kCleanStageRunning)
    {
        m_stage = kCleanStageIdle;
        m_awaitingResult = false;
        m_hint = TR(kHintDisconnected);
        return;
    }
    //等待操作期间断线：后续应答同样不会到达
    if (!connected)
    {
        m_awaitingResult = false;
    }
    RefreshOfflineHint();
}

bool ChatCleanFlow::IsConnected() const
{
    return m_connected;
}

void ChatCleanFlow::BeginProgramFill()
{
    ++m_fillDepth;
}

void ChatCleanFlow::EndProgramFill()
{
    if (m_fillDepth > 0)
    {
        --m_fillDepth;
    }
}

bool ChatCleanFlow::OnPresetSelected(int preset)
{
    //程序给「时间范围」下拉框赋值时发出的通知不算用户选择
    if (m_fillDepth > 0)
        return false;
    if (preset < 0 || preset >= kCleanPresetCount)
        return false;

    m_preset = preset;
    //时间范围变了，之前针对旧范围的提示不再适用；未连接的原因仍要保留
    m_hint.clear();
    RefreshOfflineHint();
    return true;
}

bool ChatCleanFlow::OnTimeFieldChanged()
{
    if (m_fillDepth > 0)
        return false;

    m_hint.clear();
    RefreshOfflineHint();
    if (m_preset == kCleanCustom)
        return false;

    m_preset = kCleanCustom;
    return true;
}

int ChatCleanFlow::Preset() const
{
    return m_preset;
}

bool ChatCleanFlow::CanStartClean(int rangeError)
{
    if (!m_connected || m_stage != kCleanStageIdle)
        return false;

    if (rangeError != kCleanRangeOk)
    {
        m_hint = ChatCleanRangeErrorText(rangeError);
        return false;
    }
    return true;
}

void ChatCleanFlow::OnRequestSent()
{
    m_stage = kCleanStageRunning;
    m_awaitingResult = true;
    m_hint.clear();
}

bool ChatCleanFlow::OnResult(int code)
{
    //没有在途请求（对话框重新打开过、或清理中断过线）时的应答与当前界面无关
    if (!m_awaitingResult)
        return false;
    m_awaitingResult = false;

    if (code == 0)
    {
        m_stage = kCleanStageDone;
        m_hint.clear();
        return true;
    }

    m_stage = kCleanStageIdle;
    switch (code)
    {
    //报文或时间范围不合法：正常操作不会出现，出现时请用户重新选择
    case error_code_bad_request:
        m_hint = TR(kHintBadRequest);
        break;

    //服务端写库失败等内部错误
    case error_code_server_internal:
        m_hint = TR(kHintServerError);
        break;

    //其他错误（如登录态失效）：带上错误码，便于用户反馈问题
    default:
        {
            wchar_t buf[kHintBufLen] = { 0 };
            swprintf_s(buf, kHintBufLen, TR(L"清理失败（错误码 %d），请稍后重试"), code);
            m_hint = buf;
        }
        break;
    }
    return true;
}

void ChatCleanFlow::OnTimeout()
{
    if (m_stage != kCleanStageRunning)
        return;

    //这次请求仍算在途：服务端可能只是处理得慢，之后应答到了照常处理
    m_stage = kCleanStageIdle;
    m_hint = TR(kHintTimeout);
}

int ChatCleanFlow::Stage() const
{
    return m_stage;
}

bool ChatCleanFlow::IsCleanEnabled() const
{
    return m_connected && m_stage == kCleanStageIdle;
}

const wchar_t* ChatCleanFlow::CleanButtonText() const
{
    return (m_stage == kCleanStageRunning) ? TR(kButtonRunning) : TR(kButtonIdle);
}

const std::wstring& ChatCleanFlow::Hint() const
{
    return m_hint;
}

void ChatCleanFlow::RefreshOfflineHint()
{
    if (!m_connected)
    {
        //断线原因比笼统的"未连接"更具体，已经在显示时保留
        if (m_hint != TR(kHintDisconnected))
        {
            m_hint = TR(kHintOffline);
        }
        return;
    }

    //已连接：清掉与连接有关的两种提示，其余提示（区间不合法、失败原因等）保留
    if (m_hint == TR(kHintOffline) || m_hint == TR(kHintDisconnected))
    {
        m_hint.clear();
    }
}

} // namespace balloonsettings
