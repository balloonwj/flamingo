/**
 *  IFileShareModule 的实现：FlamingoFileShare.dll 的模块总控（进程内唯一）。
 *
 *  本类只负责与宿主之间的接口与线程切换，业务交给 CShareService（第 1 期第 12 步起）：
 *    - Init / Shutdown：登记与解除宿主、建与销毁收帧用的消息窗口，并把宿主与消息窗口交给 CShareService；
 *    - OnServerFrame：在网络接收线程上拷贝帧、排进队列，再通知 UI 线程取出，逐帧交给 CShareService；
 *    - 消息窗口还接收 CShareService 的提供会话事件通知（kMsgServiceEvents），在 UI 线程上调用其 DrainEvents；
 *    - OnLoggedIn / OnLoggingOut：转给 CShareService（拉取并比对「我的共享」、结束全部会话），退出登录时关闭窗口；
 *    - OpenShareCenter：打开「文件共享」窗口；
 *    - OpenShareFromCard：按共享 id 打开浏览窗口（每个共享一个，第 13 步起）；
 *    - OpenCreateShare：按会话打开发起共享对话框（每个会话一个，第 15 步起），宿主回送的会话成员（OnConversationMembers）
 *      转给会话相同的对话框；
 *    - OpenConversationShares：弹出本会话共享列表浮层（第 16 步起）；GetShareState：给主程序画共享卡片取最新状态；
 *    - 实现 IShareUiHost：浮层与「文件共享」窗口经它打开浏览窗口、发起共享对话框与调整成员窗口（每个共享一个）；
 *    - WatchConversationShares / UnwatchConversationShares（第 8 版起）：按会话统计可用共享的个数（CShareConversationCounter），
 *      个数变化时经宿主 NotifyConversationShareCount 通知，聊天窗头部文件共享按钮的角标由此更新；
 *    - OpenOwnShareLocally（第 8 版起）：点本人发出的共享卡片时在本机打开，打不开时弹出提示说明原因；
 *    - 消息窗口上有一个每秒一次的定时器，驱动 CShareService 里的访问状态机检查超时与空闲回收；
 *    - 第 4 期（第 12 版起，docs/文件共享方案.md 第 14 节）：持有弹窗总控 CShareNoticeCenter，登记为业务总控的两种通知的
 *      监听者，并实现它需要的动作（IShareNoticeActions：浏览窗口是否在前台、打开「文件共享」窗口并选中共享、打开浏览窗口
 *      进入目录、重试、在资源管理器里打开或选中）；GetActiveTransferCount 供宿主退出前确认。
 *
 *  线程模型：除 OnServerFrame 外的方法都只在 exe 的 UI 线程上调用。OnServerFrame 在网络接收线程
 *  上调用，与 UI 线程共享的状态（是否已初始化、待处理帧队列、消息窗口句柄）都由 m_mutex 保护。
 *
 *  典型用法（由 DllMain.cpp 的工厂函数返回）：
 *      fileshare::IFileShareModule* module = &fileshare::CFileShareModule::Instance();
 *      module->Init(host, &params);
 *      ...
 *      module->Shutdown();
 *
 *  balloonwj@qq.com   2026-10-02
 */
#pragma once

#include <stdint.h>
#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "FileShareApi.h"
#include "Engine/ShareService.h"
#include "Engine/ShareConversationCounter.h"
#include "UI/ShareBrowserWindow.h"
#include "UI/ShareCreateDialog.h"
#include "UI/ShareConversationPopup.h"
#include "UI/ShareUiHost.h"
#include "UI/ShareCenterWindow.h"
#include "UI/ShareNoticeCenter.h"

namespace fileshare {

/**
 *  CFileShareModule —— 模块总控单例。对象在 DLL 卸载时随静态存储一起销毁，宿主不得删除。
 */
class CFileShareModule : public IFileShareModule, public IShareUiHost, public IShareCountSink, public IShareNoticeActions
{
public:
    /**
     *  取进程内唯一的模块对象。
     *  @return 模块对象（静态存储，归 DLL 所有）
     */
    static CFileShareModule& Instance();

    //以下为 IFileShareModule 的实现，语义见 FileShareApi.h
    virtual uint32_t GetApiVersion() const override;
    virtual bool Init(IFileShareHost* host, const FileShareInitParams* params) override;
    virtual void Shutdown() override;
    virtual void OnLoggedIn() override;
    virtual void OnLoggingOut() override;
    virtual bool OnServerFrame(uint16_t cmd, uint32_t seq, const char* body, uint32_t len) override;
    virtual bool OpenShareCenter(HWND owner) override;
    virtual bool OpenShareFromCard(HWND owner, int64_t shareId) override;
    virtual bool OpenCreateShare(HWND owner, int ctype, int64_t cid) override;
    virtual void OnConversationMembers(int ctype, int64_t cid, int code, const FileShareMemberInfo* members,
                                       uint32_t count) override;
    virtual bool OpenConversationShares(HWND owner, int ctype, int64_t cid, const RECT* anchor) override;
    virtual bool GetShareState(int64_t shareId, FileShareCardState* out) override;
    virtual void NavigateImagePreview(int delta) override;

    /**
     *  登记一个会话，统计其中可用共享的个数（聊天窗头部文件共享按钮的角标）：交给 CShareConversationCounter 按会话
     *  拉两份 2205，数出个数后经宿主的 NotifyConversationShareCount 通知；已登记的会话再调一次即重新拉取。
     *  @param ctype 会话类型：1 单聊 2 群聊
     *  @param cid   会话 id：单聊为对方 userid，群聊为群 id
     *  @return 已发出列表请求返回 true；模块未初始化、尚未登录或参数非法返回 false
     */
    virtual bool WatchConversationShares(int ctype, int64_t cid) override;

    /**
     *  注销一个会话（聊天窗关闭时）：之后不再为它通知可用共享的个数。没有登记时调用无效果。
     *  @param ctype 会话类型
     *  @param cid   会话 id
     */
    virtual void UnwatchConversationShares(int ctype, int64_t cid) override;

    /**
     *  在本机打开本人发起的共享（点本人发出的共享卡片）：经 UI/ShareLocalOpen 用资源管理器打开共享的磁盘或文件夹，
     *  单个文件则打开所在文件夹并选中它；打不开时以 owner 为所有者弹出提示，说明原因。
     *  @param owner   提示框的所有者窗口（聊天窗），可为 NULL
     *  @param shareId 共享 id
     *  @return 已交给资源管理器返回 true；打不开、模块未初始化或参数非法返回 false
     */
    virtual bool OpenOwnShareLocally(HWND owner, int64_t shareId) override;

    /**
     *  宿主回送选好的共享对象（「文件共享」窗口点「发起共享」后，宿主选人窗口的结果）：打开发起共享对话框，
     *  成员就是这些人；勾选 1 个人时在与他的单聊里发起，多个人时先请宿主建群、再在群里发起。
     *  @param targets 已选的人（userid 与显示名），只在本次调用期间有效；count 为 0 时可为 NULL
     *  @param count   人数
     */
    virtual void OnShareTargetsPicked(const FileShareMemberInfo* targets, uint32_t count) override;

    /**
     *  宿主回送代为建群的结果（多人共享时先建群）：转给正在等待建群的发起共享对话框，成功时它接着在新群里发起共享。
     *  @param code    0 成功，否则为服务端的错误码
     *  @param groupId 新群的群 id；失败时为 0
     */
    virtual void OnShareGroupCreated(int code, int64_t groupId) override;

    /**
     *  宿主通知有用户的头像变了（照片下载到了本机，或联系人修改了头像）：已打开的发起共享对话框与「文件共享」窗口
     *  重新向宿主取成员行的头像并重画。
     */
    virtual void OnAvatarsUpdated() override;

    /**
     *  IShareCountSink 的实现：CShareConversationCounter 数出的可用共享个数，转给宿主的 NotifyConversationShareCount。
     *  @param ctype 会话类型
     *  @param cid   会话 id（本人视角，与登记时相同）
     *  @param count 可用共享的个数
     */
    virtual void OnConversationShareCount(int ctype, int64_t cid, int count) override;

    //以下为 IShareUiHost 的实现，语义见 UI/ShareUiHost.h
    virtual void UiOpenCreateShare(HWND owner, int ctype, int64_t cid, bool openConversationAfter) override;

    /**
     *  打开「文件共享」窗口并记住发起它的会话（聊天窗浮层的「管理全部共享」）：之后窗口里的「发起共享」直接在这个会话里发起。
     *  @param owner 所有者窗口（聊天窗），可为 NULL
     *  @param ctype 会话类型：1 单聊 2 群聊
     *  @param cid   会话 id（本人视角）
     *  @return 窗口已显示返回 true
     */
    virtual bool UiOpenShareCenter(HWND owner, int ctype, int64_t cid) override;
    virtual void UiOpenShare(HWND owner, int64_t shareId) override;
    virtual void UiEditMembers(HWND owner, const ShareListEntry& share) override;
    virtual bool UiOpenImage(HWND owner, const std::wstring& path, IShareImagePreviewSource* source) override;
    virtual void UiPreviewSourceGone(IShareImagePreviewSource* source) override;

    /**
     *  IShareUiHost：主动发起共享（「文件共享」窗口的「发起共享」）。发起共享对话框已打开时前置它；否则记下发起的窗口，
     *  请宿主弹出选人窗口，选好的人经 OnShareTargetsPicked 回来后打开发起共享对话框。
     *  @param owner 发起的窗口（「文件共享」窗口），可为 NULL
     *  @return 选人窗口已弹出（或对话框已前置）返回 true；宿主弹不出选人窗口返回 false
     */
    virtual bool UiStartDirectShare(HWND owner) override;

    /**
     *  本人作为访问者、尚未结束的传输数（第 12 版起，含排队的与浏览窗口关闭后在后台进行的），宿主退出前据此确认。
     *  @return 传输数；模块未初始化时为 0
     */
    virtual uint32_t GetActiveTransferCount() override;

    //以下为 IShareNoticeActions 的实现（弹窗里的链接经它执行），语义见 UI/ShareNoticeCenter.h
    virtual bool IsShareBrowserForeground(int64_t shareId) override;
    virtual void NoticeOpenShareCenter(int64_t shareId) override;
    virtual void NoticeBrowse(int64_t shareId, const std::string& dir) override;
    virtual bool NoticeRetry(const ShareVisitorTransferResult& result) override;
    virtual bool NoticeOpenPath(const std::wstring& path) override;
    virtual bool NoticeRevealPath(const std::wstring& path) override;

private:
    /**
     *  按会话打开发起共享对话框（每个会话一个，已打开时前置），OpenCreateShare 与 UiOpenCreateShare 共用。
     *  @param owner                 所有者窗口，可为 NULL
     *  @param ctype                 会话类型
     *  @param cid                   会话 id（本人视角）
     *  @param openConversationAfter 发起成功后是否经宿主打开这个会话的聊天窗
     *  @return 对话框已显示返回 true
     */
    bool OpenCreateShareDialog(HWND owner, int ctype, int64_t cid, bool openConversationAfter);

    //一帧待 UI 线程处理的服务端信令（OnServerFrame 拷贝进来的副本）
    struct PendingFrame
    {
        uint16_t    m_cmd;    //命令号，属于 2200 段
        uint32_t    m_seq;    //帧序号；推送帧为 0
        std::string m_body;   //包体（UTF-8 JSON）的副本
    };

    CFileShareModule();
    ~CFileShareModule();

    CFileShareModule(const CFileShareModule&) = delete;
    CFileShareModule& operator=(const CFileShareModule&) = delete;

    /**
     *  建收帧用的消息窗口（HWND_MESSAGE 子窗口，不可见），窗口类首次使用时注册。
     *  @return 成功返回窗口句柄，失败返回 NULL
     */
    HWND CreateMessageWindow();

    //消息窗口的窗口过程：收到 kMsgFramesQueued 时调用 DrainFrames
    static LRESULT CALLBACK MessageWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

    //在 UI 线程上取出队列里的全部帧，逐帧记一行日志后交给 CShareService
    void DrainFrames();

    //关闭全部浏览窗口（退出登录与模块关闭时，须在 CShareService 释放访问状态机之前）
    void CloseBrowsers();

    //关闭全部发起共享对话框、调整成员窗口与本会话共享浮层（退出登录与模块关闭时，须在 CShareService 结束管理请求之前，
    //免得回调已关闭的窗口）
    void CloseCreateDialogs();

private:
    //保护下面三项：OnServerFrame 在网络接收线程上读写它们，其余方法在 UI 线程上读写
    std::mutex                m_mutex;
    bool                      m_initialized;   //是否已 Init 且尚未 Shutdown
    HWND                      m_msgWnd;        //收帧用的消息窗口；未初始化时为 NULL
    std::deque<PendingFrame>  m_frames;        //待 UI 线程处理的帧，先进先出；Shutdown 时清空

    //以下只在 UI 线程上读写
    IFileShareHost*           m_host;          //宿主接口（不拥有）；未初始化时为 NULL
    std::wstring              m_appDir;        //exe 所在目录（不带结尾反斜杠），Init 时从参数拷贝
    //弹窗总控（第 4 期起）：业务总控持有它的指针（两种通知的监听者），须声明在业务总控之前，晚于业务总控析构；
    //它自己不引用业务总控
    CShareNoticeCenter        m_notices;
    //业务总控：Init 时登记宿主与消息窗口，Shutdown 时解除。下面的各个窗口都引用它，必须声明在它们之前：
    //成员按声明的逆序析构，窗口析构时还要向它注销观察者（DLL 卸载时静态对象析构曾因顺序颠倒而访问已释放的内存）
    CShareService             m_service;
    //按会话统计可用共享的个数：订阅业务总控的卡片状态变化，须声明在业务总控之后（先于它析构）
    CShareConversationCounter m_shareCounter;
    CShareCenterWindow        m_centerWindow;  //「文件共享」窗口；关闭后对象保留，下次打开时重建 HWND
    //浏览窗口：共享 id → 窗口。关闭后对象保留，再次打开时重建 HWND；退出登录与模块关闭时全部关闭
    std::map<int64_t, std::unique_ptr<CShareBrowserWindow> > m_browsers;
    //浏览窗口的打开先后（共享 id，每次打开或前置都追加；最多保留 kMaxBrowserOpenOrder 条）。新建浏览窗口时据此找出
    //最近打开、仍开着的另一个浏览窗口，相对它错开摆放（2026-10-07）
    std::vector<int64_t> m_browserOpenOrder;
    //发起共享对话框：(会话类型, 会话 id) → 对话框。关闭后对象保留，再次打开时重建 HWND
    std::map<std::pair<int, int64_t>, std::unique_ptr<CShareCreateDialog> > m_createDialogs;
    //调整成员窗口：共享 id → 窗口。关闭后对象保留，再次打开时重建 HWND
    std::map<int64_t, std::unique_ptr<CShareCreateDialog> > m_editDialogs;
    CShareConversationPopup  m_convPopup;     //本会话共享列表浮层（同一时刻至多一个）
    //「文件共享」窗口「发起共享」用的发起共享对话框（按选好的对象发起，同一时刻至多一个）。关闭后对象保留，再次打开时重建 HWND
    CShareCreateDialog       m_targetDialog;
    HWND                     m_targetOwner;   //最近一次「发起共享」的发起窗口（借用）；选好人后作为发起共享对话框的所有者
    IShareImagePreviewSource* m_previewSource; //最近一次打开预览图的来源（不拥有）；看图窗翻页转给它，来源销毁时置空
};

} // namespace fileshare
