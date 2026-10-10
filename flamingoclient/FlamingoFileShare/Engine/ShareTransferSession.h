/**
 *  一个传输子会话在本端的全部状态：文件夹传输的发送端或接收端状态机加上对应的扩展点，ShareTransferSession.h
 *
 *  四种角色（docs/文件共享方案.md 5.4）：
 *    - 分享者下载：发送端 + COwnerDownloadHooks（收到 st_job 才生成清单）；
 *    - 分享者上传：接收端 + COwnerUploadHooks（校验上传权限与目标目录，顶层同名拒绝）；
 *    - 访问者下载：接收端 + CVisitorDownloadHooks（落到用户选的目录，续传）；
 *    - 访问者上传：发送端 + CVisitorUploadHooks（清单在本机生成，发清单前先交换 st_job）。
 *  状态机的回调都在其工作线程上，本类把进度、完成、失败作为 ShareSessionEvent（accessid 一栏填 transferid）交给
 *  IShareSessionEventSink（ShareService），由它排队后回到 UI 线程处理。
 *
 *  生命周期：UI 线程上创建、Start*，任意线程可 Cancel；收到完成或失败事件之后在 UI 线程上 Stop（回收线程，
 *  此时线程正在退出，不会久等）再销毁。
 *
 *  典型用法（ShareService 内部）：
 *      std::unique_ptr<CShareTransferSession> s(new CShareTransferSession(relay, pubKey, this));
 *      s->StartVisitorDownload(shareId, items, targetDir, &store);
 *      ...（UI 线程收到 share_transfer_event_completed）
 *      s->Stop();
 *
 *  balloonwj@qq.com   2026-10-02
 */
#pragma once

#include <stdint.h>
#include <memory>
#include <string>
#include <vector>

#include "ShareProtocolCodec.h"     //ShareRelayParams
#include "ShareProviderSession.h"   //ShareSessionEvent / IShareSessionEventSink
#include "ShareTransferHooks.h"
#include "../../Source/Feature/FolderTransfer/FolderRecvSession.h"
#include "../../Source/Feature/FolderTransfer/FolderSendSession.h"

namespace fileshare {

//传输子会话在本端的角色
enum share_transfer_role
{
    share_transfer_role_owner_download   = 1,   //分享者一端、下载：读共享里的文件发给访问者
    share_transfer_role_owner_upload     = 2,   //分享者一端、上传：把访问者发来的文件写进共享
    share_transfer_role_visitor_download = 3,   //访问者一端、下载：把分享者发来的文件写到本机
    share_transfer_role_visitor_upload   = 4,   //访问者一端、上传：读本机文件发给分享者
};

/**
 *  CShareTransferSession —— 一个传输子会话在本端的状态。不可拷贝；只在 UI 线程上创建、启动、停止与销毁。
 */
class CShareTransferSession : public foldertransfer::IFolderSendSessionHandler,
                              public foldertransfer::IFolderRecvSessionHandler
{
public:
    /**
     *  @param relay        2222 推来的中转参数（拷贝保存；transferid 即中转 sessionid）
     *  @param serverPubKey 服务器公钥（32 字节，拷贝保存）；NULL 表示明文
     *  @param sink         事件接收方（不拥有，须在本对象销毁之前一直有效）
     */
    CShareTransferSession(const ShareRelayParams& relay, const unsigned char* serverPubKey, IShareSessionEventSink* sink,
                          const std::shared_ptr<foldertransfer::IFolderLaneTransportFactory>& transportFactory =
                              std::shared_ptr<foldertransfer::IFolderLaneTransportFactory>());

    //销毁前须已 Stop；析构时仍会兜底 Stop
    virtual ~CShareTransferSession();

    CShareTransferSession(const CShareTransferSession&) = delete;
    CShareTransferSession& operator=(const CShareTransferSession&) = delete;

    /**
     *  分享者一端开始一次下载（发送端）。
     *  @param share 本机的共享记录
     *  @return 已启动返回 true
     */
    bool StartOwnerDownload(const ShareLocalRecord& share);

    /**
     *  分享者一端开始一次上传（接收端）。
     *  @param share 本机的共享记录
     *  @param perms 访问者此刻的写权限位
     */
    bool StartOwnerUpload(const ShareLocalRecord& share, int perms);

    /**
     *  访问者一端开始一次下载（接收端）。
     *  @param shareId   共享 id
     *  @param items     要下载的条目（相对共享根，同一目录下）
     *  @param targetDir 本机目标目录（须已存在）
     *  @param store     本机库（不拥有，可为 NULL；只在会话线程的 PrepareRoot 里读写，本库有锁）
     */
    bool StartVisitorDownload(int64_t shareId, const std::vector<std::string>& items, const std::wstring& targetDir,
                              CShareLocalStore* store);

    /**
     *  访问者一端开始一次上传（发送端）。
     *  @param manifest   本机生成的清单（ShareTransferPlan::BuildUploadManifest）
     *  @param sourceRoot 清单条目所在的本机目录
     *  @param toDir      共享内的目标目录
     */
    bool StartVisitorUpload(const foldertransfer::FolderManifest& manifest, const std::wstring& sourceRoot,
                            const std::string& toDir);

    /**
     *  取消（任意线程，不阻塞）：之后状态机按失败收场，经事件回报。
     */
    void Cancel();

    /**
     *  回收全部线程（UI 线程，收到完成或失败事件之后调用）。幂等。
     */
    void Stop();

    //传输子会话 id
    const std::string& GetTransferId() const;

    //所属访问会话 id
    const std::string& GetAccessId() const;

    //本端角色，share_transfer_role
    int GetRole() const;

    //访问者下载的扩展点（落地路径、续传记录用）；其余角色为 NULL
    CVisitorDownloadHooks* GetVisitorDownloadHooks();

    //分享者上传的扩展点（半成品清理、审计用）；其余角色为 NULL
    COwnerUploadHooks* GetOwnerUploadHooks();

    //分享者下载的扩展点（审计用）；其余角色为 NULL
    COwnerDownloadHooks* GetOwnerDownloadHooks();

    //接收端的接收状态（访问者下载、分享者上传）；发送端为空
    std::shared_ptr<foldertransfer::FolderReceiveState> GetReceiveState() const;

    //发送端清单的文件条数与本机根目录（访问者上传时界面显示用）
    const foldertransfer::FolderManifest& GetUploadManifest() const;

    //IFolderSendSessionHandler（发送端工作线程上）
    virtual void OnFolderSendProgress(const foldertransfer::FolderSendProgress& progress) override;
    virtual void OnFolderSendCompleted(int64_t totalFiles, int64_t totalBytes) override;
    virtual void OnFolderSendFailed(int errcode, const std::string& errmsg) override;

    //IFolderRecvSessionHandler（接收端工作线程上）
    virtual void OnFolderRecvProgress(const foldertransfer::FolderRecvProgress& progress) override;
    virtual void OnFolderRecvCompleted(const std::wstring& rootPath, int64_t totalFiles, int64_t totalBytes) override;
    virtual void OnFolderRecvFailed(int errcode, const std::string& errmsg) override;

private:
    //填发送端与接收端共用的中转参数
    void FillSendParams(foldertransfer::FolderSendSessionParams& params) const;
    void FillRecvParams(foldertransfer::FolderRecvSessionParams& params) const;

    //投递一个本会话的事件
    void Post(int type, int errCode, int64_t bytesDone, int64_t bytesTotal, int64_t filesDone, int64_t filesTotal);

private:
    ShareRelayParams                                     m_relay;            //中转参数；构造后只读
    unsigned char                                        m_serverPubKey[net::IDENTITY_PUBLICKEY_BYTES];   //公钥副本
    bool                                                 m_hasServerPubKey;  //是否带了公钥
    IShareSessionEventSink*                              m_sink;             //事件接收方（不拥有）
    int                                                  m_role;             //本端角色；Start* 之前为 0
    //P2P 通道工厂（P2P 方案第 6 期，2222 带 p2psessionid 时由 CShareService 建）；为空时各条通道走中转。
    //声明在 m_send / m_recv 之前：发送端与接收端先析构，工厂后析构
    std::shared_ptr<foldertransfer::IFolderLaneTransportFactory> m_transportFactory;
    std::unique_ptr<foldertransfer::FolderSendSession>   m_send;             //发送端（分享者下载、访问者上传）
    std::unique_ptr<foldertransfer::FolderRecvSession>   m_recv;             //接收端（分享者上传、访问者下载）
    std::shared_ptr<foldertransfer::FolderReceiveState>  m_recvState;        //接收端的接收状态
    std::unique_ptr<CVisitorDownloadHooks>               m_visitorDownload;  //各角色的扩展点，只有一个非空
    std::unique_ptr<CVisitorUploadHooks>                 m_visitorUpload;
    std::unique_ptr<COwnerDownloadHooks>                 m_ownerDownload;
    std::unique_ptr<COwnerUploadHooks>                   m_ownerUpload;
    foldertransfer::FolderManifest                       m_uploadManifest;   //访问者上传的清单（界面显示用）
};

} // namespace fileshare
