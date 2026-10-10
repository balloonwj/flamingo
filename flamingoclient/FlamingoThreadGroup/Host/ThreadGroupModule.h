/**
 *  帖子群模块对象 CThreadGroupModule：IThreadGroupModule 的实现（docs/帖子群方案.md 第 7 节）。
 *
 *  职责：
 *    - 每个打开的帖子群一份会话控制器（CThreadSession）与一个窗口（IThreadWindow，经工厂创建）；
 *    - 收帧：OnServerFrame 在网络线程上把帧拷贝后投递到隐藏的消息窗口（WM_APP + 0x180），在界面线程上由
 *      ProcessFrame 处理：2720 按 groupid 交给对应的会话；应答按发请求时登记的 seq 找回所属的群；
 *    - 图片：窗口经本对象向宿主上传、下载，请求号登记到群名下，结果交回那个群的窗口；
 *    - 已读与提醒：窗口「正在看」状态变化时告诉宿主（OnWatchChanged）；正在看的群有新帖、新留言、新补充时，
 *      以及窗口刚变成正在看时，请宿主标为已读（MarkGroupRead）；
 *    - 断线重连（OnLoggedIn）：未完成的请求按失败通知窗口，清掉 seq 登记，重新登记「正在查看」并重新加载；
 *    - 用户关闭窗口：撤销「正在查看」，等窗口的消息处理返回之后（投递 WM_APP + 0x181）再释放窗口对象与会话。
 *
 *  进程内经 Instance() 取唯一对象；单元测试直接构造，设为无窗口模式后用 ProcessFrame 在测试线程上驱动。
 *
 *  balloonwj@qq.com   2026-10-07
 */
#pragma once

#include <windows.h>
#include <stdint.h>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "../ThreadGroupApi.h"
#include "../Core/ThreadSession.h"
#include "../Core/ThreadText.h"
#include "ThreadWindowApi.h"

namespace threadgroup {

//消息窗口收到「有新帧」的私有消息号（开发约定文档的 WM_APP 占用表登记为 0x180）
const UINT kMsgThreadFrameArrived = WM_APP + 0x180;

//消息窗口收到「释放已关闭的窗口」的私有消息号（0x181），wParam 为群号的低 32 位、lParam 为高 32 位
const UINT kMsgThreadReleaseWindow = WM_APP + 0x181;

/**
 *  帖子群模块对象。
 */
class CThreadGroupModule : public IThreadGroupModule, public IThreadSessionSender, public IThreadWindowOwner
{
public:
    /**
     *  取进程内唯一的模块对象（DLL 导出函数返回它）。
     */
    static CThreadGroupModule& Instance();

    CThreadGroupModule();
    ~CThreadGroupModule();

    /**
     *  登记创建窗口的工厂（DllMain 的导出函数在返回模块对象之前调用）。
     *  @param factory 工厂（借用）；为 NULL 时不创建窗口（单元测试）
     */
    void SetWindowFactory(IThreadWindowFactory* factory);

    /**
     *  单元测试用：不建隐藏的消息窗口，帧由测试直接调用 ProcessFrame 交入；关闭的窗口在下次打开同一个群时释放。
     *  帖子群窗口仍由登记的工厂创建（测试登记假的工厂，或不登记、只测会话）。须在 Init 之前调用。
     */
    void SetHeadlessForTest(bool headless);

    /**
     *  在界面线程上处理一帧（消息窗口取出帧后调用；单元测试直接调用）。
     */
    void ProcessFrame(uint16_t cmd, uint32_t seq, const std::string& body);

    //某个群的会话（测试用）；没有打开时返回 NULL
    CThreadSession* SessionOf(int64_t groupId);

    //登记着的 seq 个数（测试用）
    size_t PendingSeqCount() const;

    //======== IThreadGroupModule ========
    virtual bool Init(IThreadGroupHost* host, const ThreadInitParams* params) override;
    virtual void Shutdown() override;
    virtual uint32_t GetApiVersion() const override;
    virtual void OnLoggedIn() override;
    virtual void OnLoggingOut() override;
    virtual void OnServerFrame(uint16_t cmd, uint32_t seq, const char* body, uint32_t len) override;
    virtual bool OpenThreadWindow(int64_t groupId, int64_t postId) override;
    virtual void CloseThreadWindow(int64_t groupId) override;
    virtual bool IsWatching(int64_t groupId) const override;
    virtual void OnGroupChanged(int64_t groupId) override;
    virtual void OnGroupMembers(int64_t groupId, const ThreadMemberInfo* members, uint32_t count) override;
    virtual void OnMemberPanelClosed(int64_t groupId) override;
    virtual void OnImageUploaded(uint32_t requestId, bool ok, const char* fileId, const char* md5, uint32_t width,
                                 uint32_t height, int64_t size, const wchar_t* localPath) override;
    virtual void OnImageReady(uint32_t requestId, bool ok, const wchar_t* localPath) override;
    virtual void OnAvatarsUpdated() override;

    //======== IThreadSessionSender ========
    virtual uint32_t SendThreadCommand(int64_t groupId, uint16_t cmd, const std::string& body) override;
    virtual int64_t MyUserId() override;

    //======== IThreadWindowOwner ========
    virtual IThreadGroupHost* Host() override;
    virtual const FaceTable& Faces() override;
    virtual const std::vector<ThreadFaceEntry>& FaceEntries() override;
    virtual uint32_t BeginImageRequest(int64_t groupId) override;
    virtual bool StartUpload(uint32_t requestId, const std::wstring& localPath) override;
    virtual bool StartDownload(uint32_t requestId, const std::string& fileId, const std::string& md5) override;
    virtual void OnWindowWatchChanged(int64_t groupId, bool watching) override;
    virtual void OnWindowClosed(int64_t groupId) override;

private:
    //一个打开的帖子群
    struct GroupEntry
    {
        std::unique_ptr<CThreadSession> m_session;  //会话控制器
        std::unique_ptr<IThreadWindow>  m_window;   //窗口；没有登记工厂时为 NULL
        bool                            m_closed;   //窗口已被用户关闭，等待释放
        bool                            m_watching; //最近一次告诉宿主的「正在看」状态

        GroupEntry()
            : m_closed(false)
            , m_watching(false)
        {
        }
    };

    //网络线程投递到界面线程的一帧
    struct FramePacket
    {
        uint16_t    m_cmd;
        uint32_t    m_seq;
        std::string m_body;
    };

    typedef std::map<int64_t, std::shared_ptr<GroupEntry> > GroupMap;

    //释放一个已关闭的群（窗口对象与会话）
    void ReleaseGroup(int64_t groupId);

    //关闭并释放全部群（退出登录、关闭模块）
    void CloseAll();

    //首次需要时从宿主取表情库
    void EnsureFaces();

    //消息窗口
    bool CreateMessageWindow();
    void DestroyMessageWindow();
    static LRESULT CALLBACK MessageWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

private:
    IThreadGroupHost*             m_host;           //宿主接口（借用）；Init 前与 Shutdown 后为 NULL
    IThreadWindowFactory*         m_factory;        //窗口工厂（借用）；为 NULL 时不创建窗口
    bool                          m_initialized;    //是否已初始化
    bool                          m_headless;       //测试模式：不建消息窗口
    bool                          m_loggedIn;       //是否已登录
    HWND                          m_hwnd;           //隐藏的消息窗口；测试模式下为 NULL
    mutable std::mutex            m_wndMutex;       //保护 m_postTarget：网络线程读，界面线程写
    HWND                          m_postTarget;     //网络线程投递帧的目标；未初始化或已关闭时为 NULL
    GroupMap                      m_groups;         //打开的帖子群：群号 → 会话与窗口
    std::map<uint32_t, int64_t>   m_seqGroup;       //未收到应答的请求：seq → 群号
    std::map<uint32_t, int64_t>   m_imageGroup;     //未完成的图片请求：请求号 → 群号
    uint32_t                      m_nextImageId;    //下一个图片请求号
    bool                          m_facesLoaded;    //是否已从宿主取过表情库
    FaceTable                     m_faces;          //表情名表
    std::vector<ThreadFaceEntry>  m_faceEntries;    //表情库
};

} // namespace threadgroup
