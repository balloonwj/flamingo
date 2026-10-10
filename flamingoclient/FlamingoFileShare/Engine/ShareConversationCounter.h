/**
 *  按会话统计可用共享的个数，ShareConversationCounter.h
 *
 *  聊天窗头部的文件共享按钮上有一个角标，显示本会话里状态为「可用」的共享个数（本人发起的与共享给本人的都算，
 *  2026-10-03 用户确定）。个数只随可用共享的增减变化：有人发起或取消共享、分享者上线或下线时随之变化，
 *  变成 0 时角标消失；打开共享浮层不会让角标消失。
 *
 *  本类为每个登记过的会话（聊天窗打开时登记、关闭时注销）记下本会话里有哪些共享：
 *    - Watch：登记一个会话，并按会话拉两份列表（2205 我的共享、共享给我的）；已登记的会话再调一次即重新拉取
 *      （聊天窗收到新的共享卡片时调用，新发起的共享由此计入）；
 *    - 两份列表都有结果之后，按 CShareService 记录的各共享对本人的最新状态数出「可用」的个数，与上次通知的不同时
 *      经 IShareCountSink 通知；
 *    - OnCardStateChanged：某个共享的状态变了（2203、2206、2205 应答），重新数一遍包含它的会话；
 *    - RefreshAll：断线重连后重新拉取全部登记的会话；Reset：退出登录时丢弃全部登记与尚未有结果的请求。
 *
 *  所有方法只在 UI 线程上调用（与 CShareService 相同）。单元测试见 FlamingoFileShare/Tests/ShareManageTests.cpp
 *  的 SCC 系列（标签 [sharecount]）。
 *
 *  典型用法（模块里）：
 *      m_counter.Attach(&m_service, this);          //this 实现 IShareCountSink
 *      m_service.SetCardStateListener(&m_counter);
 *      m_counter.Watch(conversation_group, groupId); //聊天窗打开
 *      m_counter.Unwatch(conversation_group, groupId);
 *
 *  balloonwj@qq.com   2026-10-03
 */
#pragma once

#include <stdint.h>
#include <map>
#include <set>
#include <utility>
#include <vector>

#include "ShareService.h"   //CShareService / IShareListObserver / IShareCardStateListener

namespace fileshare {

/**
 *  个数的接收方（模块实现，转给宿主的 NotifyConversationShareCount；单元测试用桩记录）。
 */
class IShareCountSink
{
public:
    virtual ~IShareCountSink()
    {
    }

    /**
     *  一个登记过的会话里可用共享的个数变了（或第一次数出来）。
     *  @param ctype 会话类型：1 单聊 2 群聊
     *  @param cid   会话 id（本人视角，与 Watch 时相同）
     *  @param count 可用共享的个数，不小于 0
     */
    virtual void OnConversationShareCount(int ctype, int64_t cid, int count) = 0;
};

/**
 *  按会话统计可用共享的个数（见文件头）。
 */
class CShareConversationCounter : public IShareListObserver, public IShareCardStateListener
{
public:
    CShareConversationCounter();
    virtual ~CShareConversationCounter();

    /**
     *  关联业务总控与个数的接收方。其后才能 Watch。
     *  @param service 业务总控（不拥有），须比本对象晚销毁，或在本对象销毁前调 Detach
     *  @param sink    个数的接收方（不拥有），可为 NULL（只计数、不通知）
     */
    void Attach(CShareService* service, IShareCountSink* sink);

    //解除关联：丢弃全部登记，并让业务总控不再回调本对象的列表请求
    void Detach();

    /**
     *  登记一个会话并拉取它的两份列表；已登记的会话再调一次即重新拉取。
     *  @param ctype 会话类型：1 单聊 2 群聊
     *  @param cid   会话 id（本人视角：单聊为对方 userid，群聊为群 id），大于 0
     *  @return 两份列表请求都已发出返回 true；未关联、参数非法或发送失败返回 false（登记仍保留，RefreshAll 时再拉）
     */
    bool Watch(int ctype, int64_t cid);

    /**
     *  注销一个会话：之后不再为它通知，它尚未有结果的列表请求到达时直接丢弃。
     *  @param ctype 会话类型
     *  @param cid   会话 id
     */
    void Unwatch(int ctype, int64_t cid);

    //重新拉取全部登记的会话（断线重连、重新登录之后）
    void RefreshAll();

    //退出登录：丢弃全部登记与尚未有结果的请求，不再通知
    void Reset();

    /**
     *  一个登记过的会话最近一次数出的个数（单元测试与模块查询用）。
     *  @return 个数；没有登记或还没数出来时返回 -1
     */
    int GetCount(int ctype, int64_t cid) const;

    //登记的会话数（单元测试用）
    size_t GetWatchedCount() const;

    //IShareListObserver：一份列表有了结果
    virtual void OnShareListResult(uint32_t requestId, int scope, int code, const std::vector<ShareListEntry>& shares) override;

    //IShareCardStateListener：某个共享对本人的状态变了
    virtual void OnCardStateChanged(int64_t shareId) override;

private:
    //会话的键：(会话类型, 会话 id)
    typedef std::pair<int, int64_t> ConvKey;

    //一个登记的会话
    struct Watched
    {
        std::set<int64_t> m_mine;        //本会话里本人发起的共享（最近一次「我的共享」列表）
        std::set<int64_t> m_sharedMe;    //本会话里共享给本人的共享（最近一次「共享给我的」列表）
        int               m_pending;     //尚未有结果的列表请求数；为 0 时才数个数、才通知
        int               m_lastCount;   //最近一次通知的个数；还没通知过时为 -1

        Watched()
            : m_pending(0)
            , m_lastCount(-1)
        {
        }
    };

    //一个尚未有结果的列表请求属于哪个会话、哪份列表
    struct PendingRequest
    {
        ConvKey m_conv;    //会话
        int     m_scope;   //kShareListScopeMine 或 kShareListScopeSharedMe
    };

    //为一个登记的会话发出两份列表请求
    bool RequestBoth(const ConvKey& key, Watched& watched);

    //数出一个会话里可用共享的个数，与上次通知的不同时通知
    void Recount(const ConvKey& key, Watched& watched);

    CShareService*                    m_service;    //业务总控（不拥有）；Attach 之后有效
    IShareCountSink*                  m_sink;       //个数的接收方（不拥有），可为 NULL
    std::map<ConvKey, Watched>        m_watched;    //登记的会话
    std::map<uint32_t, PendingRequest> m_requests;  //尚未有结果的列表请求：请求编号 → 所属会话与列表
};

} // namespace fileshare
