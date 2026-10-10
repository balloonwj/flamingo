/**
 *  SendModeConfigStore.cpp
 *
 *  发送键模式存进本人账号配置，说明见 SendModeConfigStore.h 与 docs/快捷回复方案.md §3.4（BUG-119）。
 *
 *  balloonwj@qq.com   2026-10-06
 */
#include "stdafx.h"
#include "SendModeConfigStore.h"

#include "../../App/FlamingoClient.h"   // CFlamingoClient::m_UserConfig / SaveUserConfig
#include "../../Common/Log/IULog.h"     // LOG_INFO

CSendModeConfigStore::CSendModeConfigStore()
    : m_client(NULL)
{
}

void CSendModeConfigStore::Attach(CFlamingoClient* client)
{
    m_client = client;
}

bool CSendModeConfigStore::HasSendMode() const
{
    if (m_client == NULL)
    {
        return true;
    }
    return m_client->m_UserConfig.HasEnterToSendSetting() != FALSE;
}

balloonchatwnd::SendKeyMode CSendModeConfigStore::LoadSendMode() const
{
    if (m_client == NULL)
    {
        return balloonchatwnd::kSendOnEnter;
    }
    return (m_client->m_UserConfig.IsEnablePressEnterToSend() != FALSE) ? balloonchatwnd::kSendOnEnter
                                                                         : balloonchatwnd::kSendOnCtrlEnter;
}

void CSendModeConfigStore::SaveSendMode(balloonchatwnd::SendKeyMode mode)
{
    if (m_client == NULL)
    {
        return;
    }
    m_client->m_UserConfig.EnablePressEnterToSend((mode == balloonchatwnd::kSendOnEnter) ? TRUE : FALSE);
    m_client->SaveUserConfig();
    LOG_INFO("send mode saved to account config: %s.", (mode == balloonchatwnd::kSendOnEnter) ? "enter" : "ctrl+enter");
}
