/**
 *  MemoCenter.h
 *
 *  「备忘与提醒」在客户端的中枢：持有备忘簿、提醒调度器、右下角提醒队列，以及备忘窗口、编辑对话框、
 *  右下角提醒弹窗、全屏提醒窗口这几个窗口；主面板把 2401～2404 的应答与推送转给它，备忘窗口、群聊窗
 *  「备忘」页签与系统设置的入口都经它读数据、发请求。进程内只有一个实例（Instance()），只在 UI 线程上使用。
 *  方案见 docs/备忘与提醒方案.md 第 4 节。
 *
 *  提醒的时机由内部一个只收消息的窗口上的一次性定时器驱动：每次备忘簿变化、提醒弹出或推迟之后，按调度器
 *  算出的下一次提醒时间重新设定，最长 60 秒检查一次，所以改系统时间、休眠唤醒之后最多一分钟内补上。
 *
 *  典型用法（主面板）：
 *      balloonmemo::CMemoCenter::Instance().OnLoginSucceeded(m_hWnd, selfId, personalFolder, this);
 *      ...收到 FMG_MSG_V2_MEMO_LIST... CMemoCenter::Instance().OnListResult(*pResult);
 *      ...注销... CMemoCenter::Instance().OnLogout();
 *
 *  本文件遵循 balloonui 头文件约定：包含它之前调用方须已引入 ATL/WTL（Flamingo 侧由 stdafx.h 保证）。
 *
 *  balloonwj@qq.com   2026-10-03
 */
#pragma once

#include <memory>
#include <string>
#include <vector>
#include <stdint.h>

#include "../../Feature/Memo/MemoBook.h"
#include "../../Feature/Memo/MemoScheduler.h"
#include "../../net/MemoProtocolClient.h"
#include "MemoViewModel.h"

namespace net { struct ContactItem; }

class CBalloonMemoWindow;
class CMemoEditDialog;
class CMemoReminderPopup;
class CMemoFullscreenWindow;

namespace balloonmemo {

class CMemoTimerWindow;

/**
 *  备忘中枢向主面板要的几样东西。由主面板实现，在 OnLoginSucceeded 时登记，注销后不再调用。
 */
class IMemoHost
{
public:
    virtual ~IMemoHost()
    {
    }

    /**
     *  群名（群备忘在备忘窗口、提醒弹窗里标明来自哪个群）。不知道这个群时返回空串。
     */
    virtual std::wstring MemoGroupName(int64_t groupid) = 0;

    /**
     *  本人在某群的角色，取值见 group_member_role；不是群成员、或群列表还没加载时返回 -1。
     */
    virtual int MemoGroupRole(int64_t groupid) = 0;

    /**
     *  主面板是否可见（没有收在托盘、没有最小化）。全屏提醒据此决定盖哪块显示器。
     */
    virtual bool MemoPanelVisible() = 0;

    /**
     *  某人在某群里的显示名（群备忘的创建人）：该群的聊天窗开着时按它的成员表（群名片优先），
     *  否则按通讯录；都查不到时返回「用户<id>」。返回值保证非空。
     *    groupid：群 id；userid：用户 id。
     */
    virtual std::wstring MemoUserName(int64_t groupid, int64_t userid) = 0;
};

//编辑对话框的打开方式
enum MemoEditMode
{
    kMemoEditNew = 1,        //新建
    kMemoEditExisting = 2,   //修改已有的一条
    kMemoEditReadOnly = 3,   //只能查看：被提醒的群备忘在本人的备忘窗口里、或普通成员在群「备忘」页签里打开
};

/**
 *  备忘中枢。
 */
class CMemoCenter
{
public:
    /**
     *  进程内唯一的实例。首次调用时构造，进程退出时析构。
     */
    static CMemoCenter& Instance();

    ~CMemoCenter();

    // ---------------- 会话生命周期（主面板调用）----------------

    /**
     *  登录（或重连）成功：登记主面板与本人，载入本机的已提醒记录，发 2401 拉全部备忘。
     *    panel：         主面板窗口句柄，各弹窗以它为 owner；
     *    selfId：        本人 userid；
     *    personalFolder：本人的个人文件夹（以反斜杠结尾），已提醒记录存在其中的 memo_fired.txt；
     *    host：          主面板提供群名、角色等信息的接口，借用不持有。
     */
    void OnLoginSucceeded(HWND panel, int64_t selfId, const std::wstring& personalFolder, IMemoHost* host);

    /**
     *  注销或切换账号：关掉全部备忘窗口与提醒、清空备忘簿与调度状态，停止定时器。
     */
    void OnLogout();

    /**
     *  主面板销毁时调用：按注销收尾，并销毁承载定时器的窗口。必须在进程退出前调用 ——
     *  本类是函数内静态对象，等到它析构时窗口类、模块可能都已不可用，ATL 还会对未销毁的窗口报断言。
     */
    void Shutdown();

    // ---------------- 网络应答与推送（主面板转来）----------------

    void OnListResult(const net::MemoListResult& result);
    void OnSaveResult(const net::MemoSaveResult& result);
    void OnDeleteResult(const net::MemoDeleteResult& result);
    void OnNotify(const net::MemoNotify& notify);

    // ---------------- 入口 ----------------

    /**
     *  打开本人的备忘窗口（系统设置「通用」页「备忘与提醒」一行的「打开」按钮）。已打开时调到前台。
     *    owner：发起打开的窗口，备忘窗口居中摆在它上面；可为 NULL。
     */
    void OpenMemoWindow(HWND owner);

    /**
     *  打开编辑对话框。同一时刻只开一个，已开着时换成这一次的内容。
     *    owner：  发起打开的窗口（备忘窗口或群聊窗），对话框以它为 owner 并在打开期间禁用它；
     *    item：   要编辑的备忘；新建时由 DefaultMemoForDay 生成；
     *    mode：   打开方式，取值见 MemoEditMode；
     *    members：群备忘的候选提醒对象（本群成员，不含本人也可）；个人备忘传空。
     */
    void OpenEditor(HWND owner, const net::MemoItem& item, MemoEditMode mode,
                    const std::vector<net::ContactItem>& members);

    /**
     *  发 2402 保存（编辑对话框点「保存」时调用）。
     *  @return 已排进发送队列返回 true；未连接时返回 false，调用方提示用户
     */
    bool Save(const net::MemoItem& item);

    /**
     *  弹确认框后发 2403 删除。
     *    owner：确认框的 owner；memoid：备忘 ID。
     */
    void ConfirmAndDelete(HWND owner, int64_t memoid);

    // ---------------- 数据 ----------------

    const memo::CMemoBook& Book() const;

    /**
     *  点开某群的「备忘」页签：角标清零并通知登记的窗口。
     */
    void MarkGroupSeen(int64_t groupid);

    int64_t SelfId() const;

    /**
     *  登录后拉取备忘已经超过 10 秒没有应答（多半是服务端版本太旧、不认识备忘命令）。备忘窗口与群「备忘」
     *  页签据此把「正在加载」换成「没有加载出来」的提示，并置灰新建按钮。收到列表后恢复为 false。
     */
    bool ListTimedOut() const;

    /**
     *  群名，取不到时返回「群聊」。
     */
    std::wstring GroupNameOf(int64_t groupid) const;

    /**
     *  本人在某群的角色，取值见 group_member_role；不知道时返回 -1。
     */
    int GroupRoleOf(int64_t groupid) const;

    /**
     *  群备忘创建人的显示名，口径见 IMemoHost::MemoUserName。主面板接口还没登记时返回「用户<id>」。
     */
    std::wstring UserNameOf(int64_t groupid, int64_t userid) const;

    /**
     *  备忘的创建时间换算成本地分钟序号；服务端没有给创建时间（为 0）时返回 0，表示不知道。
     */
    static memo::LocalMinute CreatedAtOf(const net::MemoItem& item);

    /**
     *  提醒弹窗与全屏提醒里的创建人一行：群备忘为「张勇 · 10月3日 22:59 创建」，个人备忘返回空串。
     *    item：备忘；now：现在（本地分钟序号）。
     */
    std::wstring CreatorLineOf(const net::MemoItem& item, memo::LocalMinute now) const;

    /**
     *  登记 / 取消登记要收 FMG_MSG_MEMO_CHANGED 的窗口（备忘窗口、群聊窗）。重复登记只算一次。
     */
    void RegisterObserver(HWND hwnd);
    void UnregisterObserver(HWND hwnd);

    // ---------------- 提醒窗口回调 ----------------

    /**
     *  右下角提醒弹窗或全屏窗口上点了「知道了」。
     *    memoid：备忘 ID；传 0 表示「错过的提醒」那一行的「知道了」。
     */
    void OnReminderDismissed(int64_t memoid);

    /**
     *  点了「10 分钟后再提醒」。
     *    memoid：备忘 ID；
     *    event： 这次事件的时间（全屏窗口传它显示的那一条）；传 0 时到右下角提醒队列里找。
     */
    void OnReminderSnoozed(int64_t memoid, memo::LocalMinute event = 0);

    /**
     *  全屏提醒窗口 60 秒无人操作：把它还没处理的提醒转到右下角弹窗。
     *    entries：全屏窗口里还没处理的提醒。
     */
    void OnFullscreenTimedOut(const std::vector<ReminderEntry>& entries);

    /**
     *  从提醒弹窗打开某条备忘（点了标题）。
     */
    void OpenFromReminder(int64_t memoid);

    /**
     *  内部定时器到点（由只收消息的窗口调用）。
     */
    void OnScheduleTimer();

    /**
     *  拉取备忘 10 秒没有应答（由只收消息的窗口调用）：记下超时、通知登记的窗口更换提示。
     */
    void OnListTimeout();

    /**
     *  保存 10 秒没有应答（由只收消息的窗口调用）：编辑对话框写出原因、恢复「保存」。
     */
    void OnSaveTimeout();

    // ---------------- 锁屏（docs/锁屏方案.md 第 4.9 节）----------------

    /**
     *  刚解锁（主面板调用）：补弹锁屏期间到点的提醒（右下角弹窗与全屏提醒），以及锁屏期间压下的右下角弹窗刷新。
     *  锁屏期间被删除的备忘不再弹出。未登录时只清空记下的提醒。
     */
    void OnScreenUnlocked();

private:
    CMemoCenter();
    CMemoCenter(const CMemoCenter&);
    CMemoCenter& operator=(const CMemoCenter&);

    //备忘簿变化之后：通知登记的窗口、刷新提醒弹窗，重新检查与安排提醒
    void AfterBookChanged(int64_t groupid);

    //检查此刻该弹出的提醒并弹出，必要时存回已提醒记录
    void CheckNow();

    //按下一次提醒时间设定定时器
    void Reschedule();

    //确保承载定时器的只收消息的窗口已经建好；失败返回 false
    bool EnsureTimerWindow();

    //把调度器给出的提醒分给全屏窗口与右下角弹窗
    void DispatchFires(const std::vector<memo::MemoFire>& fires);

    //刷新右下角提醒弹窗：队列空了就关掉
    void RefreshPopup();

    //通知登记的窗口备忘簿有变化
    void NotifyObservers(int64_t groupid);

    //一条备忘被删除（应答或推送）：从备忘簿、提醒、编辑对话框里拿掉
    void HandleDeleted(int64_t memoid);

    //全屏提醒要盖住的显示器
    RECT FullscreenMonitorRect() const;

    //已提醒记录的文件路径
    std::wstring FiredFilePath() const;
    void LoadFiredRecords();
    void SaveFiredRecords();

private:
    HWND         m_panel;            //主面板窗口句柄；登录成功时登记，注销时清零
    int64_t      m_selfId;           //本人 userid；未登录时为 0
    std::wstring m_personalFolder;   //本人的个人文件夹（以反斜杠结尾），已提醒记录存在这里
    IMemoHost*   m_host;             //主面板提供群名、角色的接口，借用不持有；注销时清零

    memo::CMemoBook      m_book;       //备忘簿，随登录会话
    memo::CMemoScheduler m_scheduler;  //提醒调度器，已提醒记录随本人配置持久化
    CReminderQueue       m_queue;      //右下角提醒弹窗里的提醒

    std::vector<HWND> m_observers;     //要收 FMG_MSG_MEMO_CHANGED 的窗口

    bool m_saving;                     //是否有一次保存正在等应答（编辑对话框据此置灰「保存」）
    bool m_firedLoaded;                //本次登录是否已从文件载入过已提醒记录；重连时不再重复载入
    bool m_listTimedOut;               //拉取备忘是否已超过 10 秒没有应答；登录（含重连）时清零，收到列表后清零
    HWND m_deleteOwner;                //最近一次删除确认框的 owner，删除失败时提示框以它为 owner

    std::vector<memo::MemoFire> m_lockedFires;   //锁屏期间到点的提醒，按到点顺序排列；解锁时补弹后清空，注销时清空
    bool m_popupDeferredByLock;        //锁屏期间是否有一次右下角弹窗的显示被压下；解锁时据此刷新弹窗，注销时清零

    std::unique_ptr<CMemoTimerWindow>      m_timerWnd;     //只收消息的窗口，承载提醒定时器；首次使用时创建
    std::unique_ptr<CBalloonMemoWindow>    m_memoWindow;   //本人的备忘窗口
    std::unique_ptr<CMemoEditDialog>       m_editDialog;   //编辑对话框
    std::unique_ptr<CMemoReminderPopup>    m_popup;        //右下角提醒弹窗
    std::unique_ptr<CMemoFullscreenWindow> m_fullscreen;   //全屏提醒窗口
};

} // namespace balloonmemo
