/**
 *  文件共享弹窗通知的总控，ShareNoticeCenter.h
 *
 *  第 4 期（docs/文件共享方案.md 第 14 节）。CFileShareModule 持有一个，登记为 CShareService 的两种通知的监听者：
 *    - 访问者一端的传输结束（IShareTransferFinishListener）：本人点了「取消」的、拖出到资源管理器的、浏览窗口正在前台的
 *      不弹（ShareNoticeModel::ShouldNotifyVisitorTransfer），其余加一条到「文件共享传输」弹窗；
 *    - 分享者一端的操作（IShareOwnerActivityListener）：加一条到「文件共享动态」弹窗，访问者的名字向宿主取通讯录里的显示名。
 *  加之前先问宿主能不能弹（IFileShareHost::QueryNoticePolicy：勿扰时段丢弃、锁屏推迟）；推迟的先记下，每秒的定时检查
 *  在可以弹时补弹。弹出或追加时请宿主响一声提示音（宿主按「提示音」开关决定响不响），两次之间至少相隔
 *  kShareNoticeSoundGapMs，连续的批量操作不会响成一串。
 *  两个弹窗都在右下角：分享者的贴着工作区右下角，访问者的叠在它上方（只有一个时贴着右下角）。弹窗里点了链接后，
 *  需要打开窗口、在资源管理器里显示、重试的动作经 IShareNoticeActions 交给模块执行。
 *  退出登录、模块关闭之前 SetSuppressed(true)：期间被中断的传输等一律不弹；Reset 关闭弹窗并清空条目。
 *
 *  只在 UI 线程上使用。本文件遵循 balloonui 头文件约定：包含它之前调用方须已引入 ATL/WTL（本工程的 stdafx.h）。
 *
 *  典型用法（CFileShareModule 内部）：
 *      m_notices.Attach(host, this);
 *      m_service.SetNoticeListeners(&m_notices, &m_notices);
 *      m_notices.OnTimer();          //每秒
 *      m_notices.SetSuppressed(true); m_notices.Reset();   //退出登录
 *
 *  balloonwj@qq.com   2026-10-07
 */
#pragma once

#include <stdint.h>
#include <map>
#include <string>

#include "../FileShareApi.h"
#include "../Engine/ShareNoticeModel.h"
#include "../Engine/ShareService.h"   //IShareTransferFinishListener、IShareOwnerActivityListener
#include "ShareNoticePopup.h"

namespace fileshare {

//两次提示音之间至少相隔的时间，单位：毫秒。访问者一次删除几十项是一条通知，但连续操作会接连到达，不必每条都响
const int64_t kShareNoticeSoundGapMs = 2000;

//两个弹窗上下叠放时之间的间距，单位：逻辑像素
const int kShareNoticeStackGap = 8;

/**
 *  弹窗里的链接需要模块代为执行的动作（CFileShareModule 实现，单元测试用桩）。只在 UI 线程上调用。
 */
class IShareNoticeActions
{
public:
    virtual ~IShareNoticeActions()
    {
    }

    /**
     *  这个共享的浏览窗口此刻是否在前台：存在、可见、没有最小化，且前台窗口就是它或它拥有的对话框。
     *  @param shareId 共享 id
     *  @return 在前台返回 true
     */
    virtual bool IsShareBrowserForeground(int64_t shareId) = 0;

    /**
     *  打开「文件共享」窗口。
     *  @param shareId 大于 0 时切到「我的共享」并选中这个共享（列表到达后）；为 0 时只打开
     */
    virtual void NoticeOpenShareCenter(int64_t shareId) = 0;

    /**
     *  打开一个共享的浏览窗口并进入一个目录。
     *  @param shareId 共享 id
     *  @param dir     相对共享根的目录（UTF-8），根为空串
     */
    virtual void NoticeBrowse(int64_t shareId, const std::string& dir) = 0;

    /**
     *  重新发起一笔结束了的传输：浏览窗口开着时经它发起（传输列表里多一行），没开时由业务总控在后台发起。
     *  @param result 那笔传输的结果（带重试所需的条目）
     *  @return 已发起（或已排队）返回 true
     */
    virtual bool NoticeRetry(const ShareVisitorTransferResult& result) = 0;

    /**
     *  用系统默认程序打开本机的一个文件或文件夹。
     *  @param path 完整路径
     *  @return 已交给外壳返回 true；文件不存在或外壳失败返回 false
     */
    virtual bool NoticeOpenPath(const std::wstring& path) = 0;

    /**
     *  在资源管理器里打开所在文件夹并选中这一项。
     *  @param path 完整路径
     *  @return 已交给资源管理器返回 true；这一项已不存在时返回 false
     */
    virtual bool NoticeRevealPath(const std::wstring& path) = 0;
};

/**
 *  CShareNoticeCenter —— 文件共享弹窗通知的总控。不可拷贝。
 */
class CShareNoticeCenter : public IShareTransferFinishListener,
                           public IShareOwnerActivityListener,
                           public IShareNoticePopupListener
{
public:
    CShareNoticeCenter();
    ~CShareNoticeCenter();

    CShareNoticeCenter(const CShareNoticeCenter&) = delete;
    CShareNoticeCenter& operator=(const CShareNoticeCenter&) = delete;

    /**
     *  登记宿主与模块的动作接口（模块 Init 时）。
     *  @param host    宿主（不拥有，须在 Detach 之前一直有效），不可为 NULL
     *  @param actions 模块的动作接口（不拥有，须在 Detach 之前一直有效），不可为 NULL
     */
    void Attach(IFileShareHost* host, IShareNoticeActions* actions);

    //关闭弹窗、清空条目、解除宿主（模块 Shutdown 时）；重复调用是安全的
    void Detach();

    /**
     *  是否压下此后到达的通知（退出登录、模块关闭之前置为 true，登录成功后置为 false）。被压下的通知直接丢弃。
     *  @param suppressed 为真时压下
     */
    void SetSuppressed(bool suppressed);

    //关闭两个弹窗、清空条目与推迟的通知（退出登录时）
    void Reset();

    //每秒的定时检查：有推迟的通知且宿主此刻允许弹时补弹
    void OnTimer();

    //IShareTransferFinishListener：访问者一端的一笔传输结束
    virtual void OnVisitorTransferFinished(const ShareVisitorTransferResult& result) override;

    //IShareOwnerActivityListener：访问者在本人的共享上完成了一次操作
    virtual void OnOwnerActivity(const ShareOwnerActivity& activity) override;

    //IShareNoticePopupListener：点了某一条的链接、「查看全部」、关闭了弹窗
    virtual void OnNoticeLink(int kind, uint32_t entryId, int action) override;
    virtual void OnNoticeViewAll(int kind) override;
    virtual void OnNoticePopupClosed(int kind) override;

    //以下供单元测试使用
    CShareNoticePopup& GetPopup(int kind);
    size_t GetEntryCount(int kind) const;
    bool HasDeferred() const;

    /**
     *  改两次提示音之间的最小间隔（单元测试用，正式流程固定为 kShareNoticeSoundGapMs）。
     *  @param gapMs 间隔，单位：毫秒，不小于 0
     */
    void SetSoundGapMs(int64_t gapMs);

private:
    //向宿主问此刻能不能弹（fileshare_notice_policy）；宿主未登记时按「丢弃」
    int QueryPolicy() const;

    //加一条到 kind 指定的弹窗：按宿主的判定丢弃、推迟或立即显示
    void AddEntry(int kind, const ShareNoticeEntry& entry, const ShareVisitorTransferResult* visitor,
                  const ShareOwnerActivity* owner);

    //按条目表刷新两个弹窗（没有条目的关闭），再摆放并显示；playSound 为真时请宿主响一声（受最小间隔约束）
    void ShowPopups(bool playSound);

    //摆放两个弹窗：分享者的贴着右下角，访问者的叠在它上方
    void LayoutPopups();

    //删掉条目表里已被挤掉的条目对应的数据
    void PrunePayloads();

    //访问者的显示名：宿主取通讯录里的名字，取不到时用服务端给的后备名字
    std::wstring VisitorName(const ShareOwnerActivity& activity) const;

    IFileShareHost*      m_host;        //宿主（不拥有）；未 Attach 时为 NULL
    IShareNoticeActions* m_actions;     //模块的动作接口（不拥有）；未 Attach 时为 NULL
    bool                 m_suppressed;  //是否压下此后到达的通知（退出登录到下一次登录成功之间）
    bool                 m_deferred;    //是否有推迟（锁屏期间）还没显示的条目
    int64_t              m_lastSoundMs; //上一次请宿主响提示音的时间（GetTickCount64）；还没响过时为 0
    int64_t              m_soundGapMs;  //两次提示音之间的最小间隔，单位：毫秒

    CShareNoticeList     m_ownerList;   //「文件共享动态」的条目
    CShareNoticeList     m_visitorList; //「文件共享传输」的条目
    std::map<uint32_t, ShareOwnerActivity>         m_ownerPayloads;    //条目编号 → 分享者的操作（链接动作用）
    std::map<uint32_t, ShareVisitorTransferResult> m_visitorPayloads;  //条目编号 → 访问者的传输结果（链接动作用）
    CShareNoticePopup    m_ownerPopup;   //「文件共享动态」弹窗
    CShareNoticePopup    m_visitorPopup; //「文件共享传输」弹窗
};

} // namespace fileshare
