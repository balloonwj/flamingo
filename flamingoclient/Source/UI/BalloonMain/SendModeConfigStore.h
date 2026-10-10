/**
 *  SendModeConfigStore.h
 *
 *  发送键模式存进本人账号配置（BUG-119，docs/快捷回复方案.md §3.4）：实现 balloonchatwnd::IChatSendModeStore，
 *  读写 CUserConfig 的 [ChatDlg] EnableEnterToSend 并立即存盘。系统设置「聊天」页与聊天窗 ▾ 菜单都经
 *  ChatSendMode 读写这一份，两边改了都能马上看到对方的结果。
 *
 *  典型用法（会话控制器登录成功、加载完账号配置之后）：
 *      m_sendModeStore.Attach(m_lpFMGClient);
 *      balloonchatwnd::BindChatSendModeStore(&m_sendModeStore);
 *  登出时：
 *      balloonchatwnd::BindChatSendModeStore(NULL);
 *      m_sendModeStore.Attach(NULL);
 *
 *  balloonwj@qq.com   2026-10-06
 */
#pragma once

#include "../BalloonChatWnd/ChatSendMode.h"   // IChatSendModeStore / SendKeyMode

class CFlamingoClient;

/**
 *  把发送键模式存进本人账号配置的存取接口实现。不持有客户端对象，只保存指针。
 */
class CSendModeConfigStore : public balloonchatwnd::IChatSendModeStore
{
public:
    CSendModeConfigStore();

    /**
     *  指定读写哪个客户端对象的账号配置。
     *    client：客户端对象（其 m_UserConfig 已按当前账号加载），所有权归调用方；NULL 表示不再读写任何配置。
     */
    void Attach(CFlamingoClient* client);

    /**
     *  账号配置里有没有写过发送键（CUserConfig::HasEnterToSendSetting）；没有绑定客户端时返回 true，不触发迁移。
     */
    virtual bool HasSendMode() const;

    /**
     *  读出账号配置里的发送键；没有绑定客户端时返回 kSendOnEnter。
     */
    virtual balloonchatwnd::SendKeyMode LoadSendMode() const;

    /**
     *  写入账号配置并立即存盘（CFlamingoClient::SaveUserConfig）；没有绑定客户端时什么都不做。
     *    mode：发送键模式。
     */
    virtual void SaveSendMode(balloonchatwnd::SendKeyMode mode);

private:
    CFlamingoClient* m_client;   //读写其 m_UserConfig 的客户端对象；不持有，NULL 表示未绑定
};
