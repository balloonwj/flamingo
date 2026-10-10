/**
 *  FlamingoMedia 里的云端录像中心，CloudRecordCenter.h
 *
 *  第 27 期（docs/屏幕共享与录制方案.md 12.6）。负责云端录像在客户端一侧的全部后台工作，界面（共享控制条、录制管理「云端」页、
 *  聊天卡片）都经它发起：
 *    - 合成（27c）：本人开启、状态为「等待合成」的共享录像，依次「取原始帧的下载令牌 → 下载 .frec → 后台线程离线合成 MP4 →
 *      取上传令牌 → 上传 → 等服务端推 1757 可观看」。登录后先列一次本人的录像，补做没合成的；收到 1757「等待合成」时立即开始。
 *      开始合成时右下角提示「正在生成云端录像，完成上传前请不要退出 Flamingo」，上传完成后提示「云端录像已可观看」（第 56 条）；
 *    - 保存到云端（27e）：独立录制录好的文件新建一条上传录像并上传；拿到录像 id 后记进本机清单
 *      （%LOCALAPPDATA%\Flamingo\CloudRecords\pending_<userid>.json），上传途中退出、断网时下次登录读到列表后续传（27d-4）；
 *    - 打开（27f）：取最终文件的下载令牌，下载到本机缓存后用系统默认播放器打开，缓存里已有时直接打开；
 *    - 发到会话：请 exe 弹出会话选择窗，选好后逐个会话发 1756（发到会话）；
 *    - 录制管理「云端」页（27d）：分页列表、本机合成与上传的进度、重命名、删除、下载、重新合成；列表与进度的变化经
 *      ICloudRecordObserver 告诉窗口。窗口关闭不影响合成与上传，完成时照常在右下角提示。
 *  文件传输交给 exe（IMediaHost::StartCloudTransfer），合成在本类自己的工作线程上（CCloudRecordComposer）。
 *  全部公开方法只在 UI 线程上调用。
 *
 *  典型用法（CMediaModule 里）：
 *      m_cloud.reset(new screenrecord::CCloudRecordCenter());
 *      m_cloud->Init(host);
 *      if (screenrecord::CCloudRecordCenter::IsCloudRecordSignal(cmd)) m_cloud->OnSignal(cmd, json);
 *
 *  balloonwj@qq.com   2026-10-06
 */
#pragma once

#include <stdint.h>
#include <atomic>
#include <deque>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <thread>
#include <vector>
#include <windows.h>
#include "../Api/FlamingoMediaApi.h"
#include "CloudRecordManagerModel.h"   // CloudRowLocal：本机对一条录像正在做的事
#include "CloudRecordProtocolClient.h"

namespace screenshare {
class CShareCornerToast;
}

namespace screenrecord {

class CCloudRecordComposeTask;

//录制管理「云端」页（27d）向云端录像中心登记的观察者；回调都在 UI 线程上
class ICloudRecordObserver
{
public:
    virtual ~ICloudRecordObserver()
    {
    }

    //列表或某条录像变了（列表应答、推送、重命名、删除、本机合成与上传换了阶段）：窗口据此重建列表
    virtual void OnCloudRecordsChanged() = 0;

    //本机合成或上传的进度变了（合成与传输期间每秒一次）：窗口据此只刷新状态列
    virtual void OnCloudProgressChanged() = 0;
};

//云端录像中心
class CCloudRecordCenter
{
public:
    //消息窗口收到的消息（窗口私有消息，见开发约定文档的 WM_APP 段）
    static const UINT kMsgComposeDone  = WM_APP + 0xA8;   //合成线程结束：wParam 为 CCloudRecordComposeTask*，lParam 为结果（0 成功）
    static const UINT kMsgComposeNames = WM_APP + 0xA9;   //合成线程请 UI 线程查参与者的名字：lParam 为 CCloudRecordComposeTask*
    static const UINT kMsgToastCommand = WM_APP + 0xAA;   //右下角提示的按钮命令（CShareCornerToast 投来）
    //消息窗口上刷新进度的定时器（录制管理窗口开着、本机在合成或上传时才开）的 id 与间隔（毫秒）
    static const UINT_PTR kProgressTimerId = 1;
    static const UINT kProgressTimerMs = 1000;
    //列表每页的条数（取服务端上限）
    static const int kListPage = 50;

    CCloudRecordCenter();
    ~CCloudRecordCenter();

    CCloudRecordCenter(const CCloudRecordCenter&) = delete;
    CCloudRecordCenter& operator=(const CCloudRecordCenter&) = delete;

    /**
     *  初始化：记下宿主、建消息窗口。
     *  @param host 宿主接口（不拥有）
     */
    void Init(flamingomedia::IMediaHost* host);

    //关闭：取消全部传输与合成、关提示窗口（模块 Shutdown 时）
    void Shutdown();

    //退出登录：同 Shutdown 的取消，但保留消息窗口，下次登录接着用
    void OnLogout();

    /**
     *  主连接的在线状态变了。
     *  变为离线：撤掉还在等应答的「打开」与「保存到云端」（提示用户），合成保留，下次连上时重发它在等的令牌请求；
     *  变为在线：重发合成在等的令牌请求，再列一次本人的录像，补做没合成的。
     *  @param online 主连接现在是否可用
     */
    void OnConnectionStateChanged(bool online);

    //是否是本类处理的信令：1755～1757
    static bool IsCloudRecordSignal(uint16_t cmd);

    /**
     *  处理一条信令（1755 应答、1756 应答、1757 推送）。
     *  @return 处理了返回 true
     */
    bool OnSignal(uint16_t cmd, const std::string& json);

    //exe 回报的传输进度与结束（IMediaModule 转来）
    void OnTransferProgress(uint32_t id, int64_t doneBytes, int64_t totalBytes);
    void OnTransferFinished(uint32_t id, int result);

    //是否有尚未完成的合成或上传（退出前确认，第 56 条）
    bool HasWork() const;

    /**
     *  打开一条云端录像（聊天卡片、录制管理「云端」页的「播放」）。
     *  @param recordId 录像 id
     *  @param title    标题（缓存文件名与提示用）；可为空
     */
    void OpenRecord(int64_t recordId, const std::wstring& title);

    /**
     *  把一条可观看的云端录像发到会话：请 exe 弹出会话选择窗，选好后逐个会话发 1756。
     *  @param recordId 录像 id
     *  @param title    标题（选择窗预览区显示）
     */
    void SendRecord(int64_t recordId, const std::wstring& title);

    //exe 回报的会话选择结果（IMediaModule::OnConversationsPicked 转来）
    void OnConversationsPicked(uint32_t token, const flamingomedia::MediaConversationRef* refs, uint32_t count);

    /**
     *  独立录制保存到云端（27e）：新建一条上传录像并上传本机文件。
     *  @param path       本机文件（.mp4 或 .m4a）
     *  @param audioOnly  是否只有声音
     *  @param durationMs 时长（毫秒）
     *  @param startTime  开始录制的时间（Unix 毫秒）
     *  @param title      标题（默认取文件名）
     *  @return 已发出请求返回 true；文件不存在、没有开通云端录制或主连接不可用时返回 false
     */
    bool UploadLocalRecording(const std::wstring& path, bool audioOnly, int64_t durationMs, int64_t startTime,
                              const std::wstring& title);

    /**
     *  合成线程上调用（经消息投回 UI 线程处理）：取参与者的名字。本方法在 UI 线程上由消息处理调用，供测试也可直接调。
     *  @param task 合成任务；名字填进它之后唤醒合成线程
     */
    void ResolveNames(CCloudRecordComposeTask* task);

    //======== 录制管理「云端」页（27d） ========

    /**
     *  登记或解除观察者（录制管理窗口打开时登记、关闭时传 NULL）。
     *  @param observer 观察者，不拥有；须在解除之前一直有效
     */
    void SetObserver(ICloudRecordObserver* observer);

    /**
     *  请求本人的云端录像列表（1755）。
     *  @param more false：从最新的一页重新开始，应答回来后替换整个列表；true：接着取更早的一页，应答回来后接在后面
     *  @return 已发出返回 true；主连接不可用或上一次「更早的一页」还没回来时返回 false
     */
    bool RequestList(bool more);

    //当前的列表（按 id 从大到小，即最新的在前）、是否读到过、是否还有更早的
    const std::vector<CloudRecordInfo>& Records() const;
    bool ListLoaded() const;
    bool ListHasMore() const;

    //列表应答里的企业设置：是否开通、已用字节、配额字节、保留天数（0 为永久）
    bool CloudEnabled() const;
    int64_t UsedBytes() const;
    int64_t QuotaBytes() const;
    int64_t RetainDays() const;

    /**
     *  本机对一条录像正在做的事（合成、上传、本次登录里合成失败过）与进度。
     *  @param recordId 录像 id
     */
    CloudRowLocal LocalState(int64_t recordId) const;

    /**
     *  重命名（1756 op 1）。标题应已经 NormalizeCloudTitle 校验；结果回来后更新列表并通知观察者，失败时右下角提示。
     *  @return 已发出返回 true
     */
    bool Rename(int64_t recordId, const std::wstring& title);

    /**
     *  删除（1756 op 2）。本机对这条录像的合成、上传先取消；成功后从列表去掉，会话里的卡片随之不能再看。
     *  @return 已发出返回 true
     */
    bool Delete(int64_t recordId);

    /**
     *  下载最终文件到 destPath（先取下载令牌，再交给 exe 传输）；完成时右下角提示，可以打开所在文件夹。
     *  @param recordId 录像 id
     *  @param destPath 目标完整路径（目录须已存在，已有同名文件时覆盖）
     *  @return 已发出返回 true；同一条录像正在下载时返回 false
     */
    bool Download(int64_t recordId, const std::wstring& destPath);

    /**
     *  重新合成本次登录里合成失败过的共享录制。
     *  @return 已加入合成队列返回 true；这条录像不在列表里、不是等待合成或没有失败过时返回 false
     */
    bool Recompose(int64_t recordId);

    //======== 暂停 / 继续与退出前的处理（2026-10-07，docs/屏幕共享与录制方案.md 第 14.4 节） ========

    /**
     *  暂停一条录像本机的合成或上传：取消本机正在做的部分，服务器上的数据保留。「保存到云端」的上传仍留在本机续传清单里，
     *  继续时按服务端的位点接着传；合成从头再做（重新下载原始帧）。暂停的录像记进本机的暂停清单，下次登录不自动继续。
     *  @param recordId 录像 id
     *  @return 这条录像本机正在合成或上传、已暂停返回 true；否则返回 false
     */
    bool Pause(int64_t recordId);

    /**
     *  继续一条暂停了的录像：等待合成的共享录制重新加入合成队列；「保存到云端」的上传按本机续传清单续传。
     *  @param recordId 录像 id
     *  @return 没有暂停时返回 false；否则从暂停清单去掉并返回 true
     */
    bool Resume(int64_t recordId);

    //这条录像是否暂停了
    bool IsPaused(int64_t recordId) const;

    /**
     *  未完成的云端录像（退出、登出、关机前的对话框用）：本机正在合成或上传的，以及暂停了的。
     *  @return 录像 id 与标题；「保存到云端」还没拿到录像 id 的，id 为 0
     */
    std::vector<std::pair<int64_t, std::wstring> > UnfinishedRecords() const;

    /**
     *  放弃全部未完成的录像（对话框里选了「放弃」）：取消本机的工作，请服务端删除有录像 id 的，从暂停清单与续传清单去掉。
     *  @return 发出删除请求的条数
     */
    int AbandonUnfinished();

private:
    //一次合成的阶段
    enum ComposeStage
    {
        kComposeWaitFrecToken = 1,   //已请求原始帧的下载令牌
        kComposeDownloading   = 2,   //正在下载 .frec
        kComposeRunning       = 3,   //合成线程在合成
        kComposeWaitUpToken   = 4,   //已请求上传令牌
        kComposeUploading     = 5,   //正在上传 MP4
        kComposeWaitReady     = 6,   //上传完了，等服务端推「可观看」
    };

    //一次合成
    struct ComposeJob
    {
        int64_t      m_recordId;    //录像 id
        std::wstring m_title;       //标题
        int          m_stage;       //阶段，取值见 ComposeStage
        uint32_t     m_transferId;  //进行中的传输 id；没有为 0
        std::wstring m_frecPath;    //本机的原始帧文件
        std::wstring m_mp4Path;     //本机合成出的 MP4
        int64_t      m_mp4Bytes;    //MP4 字节数
        std::shared_ptr<CCloudRecordComposeTask> m_task;   //合成任务（合成阶段有效）

        ComposeJob();
    };

    //一次「保存到云端」的上传
    struct UploadJob
    {
        int64_t      m_recordId;    //服务端分配的录像 id；请求新建之前为 0
        std::wstring m_path;        //本机文件
        bool         m_audioOnly;   //是否只有声音
        int64_t      m_durationMs;  //时长
        int64_t      m_startTime;   //开始录制的时间
        int64_t      m_bytes;       //文件字节数
        std::wstring m_title;       //标题
        uint32_t     m_transferId;  //进行中的传输 id；没有为 0
        bool         m_waitingToken;   //还在等上传令牌（新建与续传都是）；传输开始后为 false

        UploadJob();
    };

    //一次「打开」
    struct OpenJob
    {
        int64_t      m_recordId;    //录像 id
        std::wstring m_title;       //标题
        std::wstring m_cachePath;   //本机缓存路径
        uint32_t     m_transferId;  //进行中的传输 id；没有为 0

        OpenJob();
    };

    //一次「下载…」（录制管理「云端」页）
    struct DownloadJob
    {
        int64_t      m_recordId;    //录像 id
        std::wstring m_destPath;    //用户选的保存位置
        uint32_t     m_transferId;  //进行中的传输 id；还在等令牌时为 0

        DownloadJob();
    };

    //消息窗口
    static LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
    bool ensureWindow();

    //经宿主发一条信令；主连接不可用时返回 false
    bool send(uint16_t cmd, const std::string& json);
    void onList(const std::string& json);
    void onOp(const std::string& json);
    void onNotify(const std::string& json);
    void onDownloadToken(const CloudRecordOpResult& r);
    void onUploadToken(const CloudRecordOpResult& r);
    void onSendResult(const CloudRecordOpResult& r);
    void onRenameResult(const CloudRecordOpResult& r);
    void onDeleteResult(const CloudRecordOpResult& r);

    //列表：用一条录像的新内容替换（不在列表里时按 id 插到合适的位置）、按 id 去掉、按 id 找
    void applyRecord(const CloudRecordInfo& info);
    void removeRecord(int64_t recordId);
    const CloudRecordInfo* findRecord(int64_t recordId) const;
    //通知观察者列表变了，并按是否有进行中的合成、上传开关进度定时器
    void notifyChanged();
    void updateProgressTimer();
    void onProgressTimer();
    //取消本机对一条录像的全部工作（合成、合成的传输、保存到云端的上传、打开、下载），不提示
    void cancelLocalWork(int64_t recordId);

    //没传完的上传清单（27d-4）：文件路径（取不到本人 userid 时为空，不记清单）、读、写、加一条、去掉一条
    std::wstring pendingPath() const;
    bool loadPending(std::vector<PendingCloudUpload>& out) const;
    void savePending(const std::vector<PendingCloudUpload>& list) const;
    void addPending(const UploadJob& job);
    void removePending(int64_t recordId);
    //读到第一页列表后：对清单里的每一条续传、去掉或先留着
    void resumePendingUploads();

    //暂停清单（2026-10-07）：文件路径（取不到本人 userid 时为空）、读入 m_paused、写出 m_paused
    std::wstring pausedPath() const;
    void loadPaused();
    void savePaused() const;
    //从暂停清单去掉一条（录像已删除、已可观看、录制出错）并写盘；不在清单里时什么也不做
    void forgetPaused(int64_t recordId);
    //右下角提示「正在生成云端录像」：主按钮为「暂停」，点了暂停这条录像的合成
    void showPauseToast(const std::wstring& text, int64_t recordId);

    //合成：加入队列（已在做的不重复加）、开始下一步、合成线程结束
    void enqueueCompose(const CloudRecordInfo& info);
    void startNextCompose();
    //给一个合成发它当前阶段在等的令牌请求（原始帧的下载令牌或 MP4 的上传令牌）；发不出去返回 false
    bool requestComposeToken(const ComposeJob& job);
    void onComposeDone(int64_t recordId, int result);
    void failCompose(int64_t recordId, const char* reason, int code);

    //开始一次传输（交给 exe）；失败返回 0
    uint32_t startTransfer(int direction, const CloudRecordOpResult& r, const std::wstring& localPath, int64_t bytes);

    //提示
    void showToast(const std::wstring& text, int64_t recordId, bool playButton);
    void showError(const std::wstring& text);

    //本机目录：合成用的工作目录与播放用的缓存目录（%LOCALAPPDATA%\Flamingo\CloudRecords\work|cache），不存在时建
    static std::wstring workDir();
    static std::wstring cacheDir();

    flamingomedia::IMediaHost*        m_host;          //宿主（不拥有）
    HWND                              m_hWnd;          //消息窗口（HWND_MESSAGE）
    bool                              m_listedOnce;    //本次登录是否已列过一次本人的录像
    std::map<int64_t, ComposeJob>     m_composeJobs;   //录像 id → 合成（含排队中的）
    std::deque<int64_t>               m_composeQueue;  //排队等合成的录像 id（同一时刻只做一个）
    int64_t                           m_composing;     //正在做的合成（录像 id）；没有为 0
    std::vector<UploadJob>            m_uploadJobs;    //保存到云端的上传（含等新建应答的）
    std::map<int64_t, OpenJob>        m_openJobs;      //录像 id → 打开
    std::map<uint32_t, int64_t>       m_sendPicks;     //会话选择的标识 → 要发的录像 id
    uint32_t                          m_nextPickToken; //下一个会话选择的标识
    std::unique_ptr<screenshare::CShareCornerToast> m_toast;   //右下角提示
    int64_t                           m_toastRecordId; //提示对应的录像（「播放」按钮用）；没有为 0
    std::wstring                      m_toastFolderPath; //提示对应的下载文件（「打开所在文件夹」按钮用）；没有为空
    //录制管理「云端」页（27d）
    ICloudRecordObserver*             m_observer;      //观察者（不拥有）；窗口没开时为 NULL
    std::vector<CloudRecordInfo>      m_list;          //本人的录像，按 id 从大到小
    std::deque<bool>                  m_listRequests;  //已发出、还没回来的列表请求：true 为「更早的一页」
    bool                              m_listLoaded;    //读到过列表
    bool                              m_listHasMore;   //还有更早的录像
    bool                              m_listEnabled;   //企业是否开通了云端录制
    int64_t                           m_listUsedBytes; //企业已用字节数
    int64_t                           m_listQuotaBytes;//企业配额字节数
    int64_t                           m_listRetainDays;//保留天数，0 为永久
    std::set<int64_t>                 m_composeFailed; //本次登录里合成失败过的录像
    std::map<uint32_t, int>           m_transferProgress; //传输 id → 进度（0～100）
    std::map<int64_t, DownloadJob>    m_downloadJobs;  //录像 id → 下载
    bool                              m_progressTimerOn; //进度定时器是否开着
    std::set<int64_t>                 m_paused;        //暂停了的录像（2026-10-07）；与本机的暂停清单文件一致，退出登录时清空
    bool                              m_pausedLoaded;  //本次登录是否已读入暂停清单
    int64_t                           m_toastPauseRecordId; //提示的主按钮为「暂停」时对应的录像；没有为 0
};

} // namespace screenrecord
