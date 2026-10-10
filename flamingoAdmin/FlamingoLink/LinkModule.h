/**
 *  FlamingoLink.dll —— ILinkModule 的实现：把连接线程（LinkClient）、在线名单（OnlineTable）与
 *  页面窗口（COnlinePage）串起来。
 *
 *  - 连接线程的事件经本类的消息窗口（只收消息的窗口，HWND_MESSAGE）回到界面线程：状态更新、
 *    订阅结果、名单推送分别更新本类的状态与名单，然后刷新页面；会话过期时通知宿主；
 *  - 订阅由「页面是否活动」与「当前企业」决定（DesiredEnterprise），每次两者变化都交给连接线程；
 *  - 页面窗口是否可见由「页面是否活动」与「是否被叠层弹窗盖住」决定。
 *
 *  除 GetApiVersion 外全部方法只在界面线程上调用。单例，经 Instance() 取得。
 *
 *  balloonwj@qq.com   2026-10-04
 */
#pragma once

#include <memory>
#include <vector>

#include "FlamingoLink/FlamingoLinkApi.h"
#include "FlamingoLink/Core/OnlineTable.h"
#include "FlamingoLink/Net/LinkClient.h"
#include "FlamingoLink/Ui/OnlinePage.h"

namespace flamingolink {

/**
 *  接收连接线程事件通知的消息窗口（只收消息，不显示）。
 */
class CLinkMessageWnd : public CWindowImpl<CLinkMessageWnd>
{
public:
    DECLARE_WND_CLASS(_T("FlamingoLinkMessageWnd"))

    //连接线程放入新事件后投递的消息（窗口私有消息，WM_APP 段）
    enum { kMsgLinkEvent = WM_APP + 0x10 };

    /**
     *  @param owner 事件的处理者（LinkModule），所有权归调用方
     */
    explicit CLinkMessageWnd(class CLinkModule* owner);

    BEGIN_MSG_MAP(CLinkMessageWnd)
        MESSAGE_HANDLER(kMsgLinkEvent, OnLinkEvent)
    END_MSG_MAP()

private:
    //取走并处理连接线程放入的全部事件
    LRESULT OnLinkEvent(UINT, WPARAM, LPARAM, BOOL& bHandled);

    class CLinkModule* m_owner;   //事件的处理者，归调用方所有
};

/**
 *  模块对象（单例）。
 */
class CLinkModule : public ILinkModule, public IOnlineViewSource
{
public:
    //取唯一实例
    static CLinkModule& Instance();

    //以下为 ILinkModule 的实现，语义见 FlamingoLinkApi.h

    //返回编译时的 kApiVersion
    uint32_t GetApiVersion() const override;
    //记下宿主、初始化网络与密码学库、建消息窗口
    bool Init(ILinkHost* host) override;
    //停连接、销毁页面与消息窗口、清理网络库
    void Shutdown() override;
    //按参数建立长连接
    void Start(const LinkStartParams* params) override;
    //断开长连接并清空名单
    void Stop() override;
    //创建页面窗口
    HWND CreatePage(HWND parent) override;
    //销毁页面窗口
    void DestroyPage() override;
    //设置页面窗口的位置与大小
    void SetPageBounds(const RECT* rc) override;
    //设置当前企业
    void SetEnterprise(const wchar_t* enterpriseId, const wchar_t* enterpriseName) override;
    //页面是否处于活动状态
    void SetPageActive(bool active) override;
    //页面是否被叠层弹窗盖住
    void SetPageCovered(bool covered) override;
    //取连接与订阅的当前状态
    void GetStatus(LinkStatus* out) override;

    //以下为 IOnlineViewSource 的实现

    //取页面显示需要的数据
    void GetViewState(OnlineViewState& out) override;

    /**
     *  处理连接线程的事件（由消息窗口在界面线程上调用）。
     */
    void OnLinkEvents();

private:
    CLinkModule();

    //把「想订阅的企业」交给连接线程；企业变了时清空名单与订阅结果
    void UpdateSubscription();

    //按页面是否活动、是否被盖住决定页面窗口显示与否
    void UpdatePageVisibility();

    //刷新页面（页面不存在时什么都不做）
    void RefreshPage();

    //处理一个事件
    void HandleEvent(const LinkEvent& ev, bool& sessionExpired);

    ILinkHost*                        m_host;                //宿主接口，Init 时登记；所有权归宿主
    bool                              m_inited;              //是否已 Init
    std::unique_ptr<CLinkMessageWnd>  m_msgWnd;              //接收连接线程事件的消息窗口
    LinkClient                        m_client;              //连接线程
    std::unique_ptr<COnlinePage>      m_page;                //页面窗口；未创建时为空
    OnlineTable                       m_table;               //当前企业的在线名单
    int64_t                           m_enterpriseId;        //页面当前的企业ID，0 表示没有
    CString                           m_enterpriseName;      //页面当前的企业名称
    bool                              m_pageActive;          //页面是否处于活动状态
    bool                              m_pageCovered;         //页面是否被叠层弹窗盖住
    int64_t                           m_desired;             //上一次交给连接线程的想订阅的企业
    int                               m_state;               //连接状态，取值见 enum LinkState
    int                               m_attempt;             //等待重连时是第几次
    int                               m_delayMs;             //等待重连时这次要等的毫秒数
    ULONGLONG                         m_waitStartTick;       //进入等待时的 GetTickCount64
    int64_t                           m_subscribed;          //服务端已确认订阅的企业，0 表示没有
    int                               m_lastError;           //最近一次订阅失败的错误码，0 表示没有
    bool                              m_received;            //当前企业是否已收到过完整名单
};

} // namespace flamingolink
