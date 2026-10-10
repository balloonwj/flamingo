/**
 *  传输子会话 CShareTransferSession 的实现，见 ShareTransferSession.h。
 *  balloonwj@qq.com   2026-10-02
 */
#include "stdafx.h"

#include "ShareTransferSession.h"

#include <string.h>   //memcpy / memset

#include "ShareBrowseModel.h"
#include "../../Source/Common/Log/IULog.h"

namespace fileshare {

CShareTransferSession::CShareTransferSession(const ShareRelayParams& relay, const unsigned char* serverPubKey,
                                             IShareSessionEventSink* sink,
                                             const std::shared_ptr<foldertransfer::IFolderLaneTransportFactory>& transportFactory)
    : m_relay(relay)
    , m_hasServerPubKey(serverPubKey != NULL)
    , m_sink(sink)
    , m_role(0)
    , m_transportFactory(transportFactory)
{
    memset(m_serverPubKey, 0, sizeof(m_serverPubKey));
    if (serverPubKey != NULL)
        memcpy(m_serverPubKey, serverPubKey, sizeof(m_serverPubKey));
}

CShareTransferSession::~CShareTransferSession()
{
    Stop();
    //先销毁状态机、再销毁扩展点：状态机的析构函数会再调一次 Stop，其中会回调扩展点的 OnStopping；
    //成员按声明的逆序析构，扩展点声明在状态机之后、会先被析构，所以这里显式提前销毁状态机
    m_send.reset();
    m_recv.reset();
}

bool CShareTransferSession::StartOwnerDownload(const ShareLocalRecord& share)
{
    m_role = share_transfer_role_owner_download;
    m_ownerDownload.reset(new COwnerDownloadHooks(share));
    foldertransfer::FolderSendSessionParams params;
    FillSendParams(params);
    //清单与读文件的根目录由扩展点在收到 st_job 之后给出；这里先填共享根，Start 要求非空
    params.m_rootPath = share.m_rootPath;
    params.m_hooks = m_ownerDownload.get();
    m_send.reset(new foldertransfer::FolderSendSession());
    return m_send->Start(params, this);
}

bool CShareTransferSession::StartOwnerUpload(const ShareLocalRecord& share, int perms)
{
    m_role = share_transfer_role_owner_upload;
    m_ownerUpload.reset(new COwnerUploadHooks(share, perms));
    m_recvState.reset(new foldertransfer::FolderReceiveState());
    foldertransfer::FolderRecvSessionParams params;
    FillRecvParams(params);
    params.m_saveParentDir = share.m_rootPath;
    params.m_hooks = m_ownerUpload.get();
    m_recv.reset(new foldertransfer::FolderRecvSession());
    return m_recv->Start(params, m_recvState, this);
}

bool CShareTransferSession::StartVisitorDownload(int64_t shareId, const std::vector<std::string>& items,
                                                 const std::wstring& targetDir, CShareLocalStore* store)
{
    m_role = share_transfer_role_visitor_download;
    m_recvState.reset(new foldertransfer::FolderReceiveState());
    m_visitorDownload.reset(new CVisitorDownloadHooks(shareId, items, targetDir, store, m_recvState));
    foldertransfer::FolderRecvSessionParams params;
    FillRecvParams(params);
    params.m_saveParentDir = targetDir;
    params.m_hooks = m_visitorDownload.get();
    m_recv.reset(new foldertransfer::FolderRecvSession());
    return m_recv->Start(params, m_recvState, this);
}

bool CShareTransferSession::StartVisitorUpload(const foldertransfer::FolderManifest& manifest,
                                               const std::wstring& sourceRoot, const std::string& toDir)
{
    m_role = share_transfer_role_visitor_upload;
    m_uploadManifest = manifest;
    std::vector<std::string> topNames;
    for (size_t i = 0; i < manifest.m_dirs.size() + manifest.m_files.size(); ++i)
    {
        const std::string& path = (i < manifest.m_dirs.size()) ? manifest.m_dirs[i]
                                                               : manifest.m_files[i - manifest.m_dirs.size()].m_relativePath;
        const std::string top = path.substr(0, path.find('/'));
        bool seen = false;
        for (size_t k = 0; k < topNames.size() && !seen; ++k)
            seen = (topNames[k] == top);
        if (!seen)
            topNames.push_back(top);
    }
    m_visitorUpload.reset(new CVisitorUploadHooks(topNames, toDir));
    foldertransfer::FolderSendSessionParams params;
    FillSendParams(params);
    params.m_rootPath = sourceRoot;
    params.m_manifest = manifest;
    params.m_hooks = m_visitorUpload.get();
    m_send.reset(new foldertransfer::FolderSendSession());
    return m_send->Start(params, this);
}

void CShareTransferSession::Cancel()
{
    if (m_send)
        m_send->Cancel();
    if (m_recv)
        m_recv->Cancel();
}

void CShareTransferSession::Stop()
{
    if (m_send)
        m_send->Stop();
    if (m_recv)
        m_recv->Stop();
}

const std::string& CShareTransferSession::GetTransferId() const
{
    return m_relay.m_transferId;
}

const std::string& CShareTransferSession::GetAccessId() const
{
    return m_relay.m_accessId;
}

int CShareTransferSession::GetRole() const
{
    return m_role;
}

CVisitorDownloadHooks* CShareTransferSession::GetVisitorDownloadHooks()
{
    return m_visitorDownload.get();
}

COwnerUploadHooks* CShareTransferSession::GetOwnerUploadHooks()
{
    return m_ownerUpload.get();
}

COwnerDownloadHooks* CShareTransferSession::GetOwnerDownloadHooks()
{
    return m_ownerDownload.get();
}

std::shared_ptr<foldertransfer::FolderReceiveState> CShareTransferSession::GetReceiveState() const
{
    return m_recvState;
}

const foldertransfer::FolderManifest& CShareTransferSession::GetUploadManifest() const
{
    return m_uploadManifest;
}

void CShareTransferSession::OnFolderSendProgress(const foldertransfer::FolderSendProgress& progress)
{
    Post(share_transfer_event_progress, 0, progress.m_sentBytes, progress.m_totalBytes, progress.m_finishedFiles,
         progress.m_totalFiles);
}

void CShareTransferSession::OnFolderSendCompleted(int64_t totalFiles, int64_t totalBytes)
{
    Post(share_transfer_event_completed, 0, totalBytes, totalBytes, totalFiles, totalFiles);
}

void CShareTransferSession::OnFolderSendFailed(int errcode, const std::string& errmsg)
{
    LOG_WARNING("share transfer send failed, transferid: %s, code: %d, msg: %s.", m_relay.m_transferId.c_str(), errcode,
                errmsg.c_str());
    Post(share_transfer_event_failed, errcode, 0, 0, 0, 0);
}

void CShareTransferSession::OnFolderRecvProgress(const foldertransfer::FolderRecvProgress& progress)
{
    Post(share_transfer_event_progress, 0, progress.m_receivedBytes, progress.m_totalBytes, progress.m_doneFiles,
         progress.m_totalFiles);
}

void CShareTransferSession::OnFolderRecvCompleted(const std::wstring& rootPath, int64_t totalFiles, int64_t totalBytes)
{
    (void)rootPath;
    Post(share_transfer_event_completed, 0, totalBytes, totalBytes, totalFiles, totalFiles);
}

void CShareTransferSession::OnFolderRecvFailed(int errcode, const std::string& errmsg)
{
    LOG_WARNING("share transfer recv failed, transferid: %s, code: %d, msg: %s.", m_relay.m_transferId.c_str(), errcode,
                errmsg.c_str());
    Post(share_transfer_event_failed, errcode, 0, 0, 0, 0);
}

void CShareTransferSession::FillSendParams(foldertransfer::FolderSendSessionParams& params) const
{
    params.m_relayIp = m_relay.m_relayIp;
    params.m_relayPort = m_relay.m_relayPort;
    params.m_relayToken = m_relay.m_token;
    params.m_dataLanes = m_relay.m_lanes;
    params.m_serverPubKey = m_hasServerPubKey ? m_serverPubKey : NULL;
    //有 P2P 通道工厂时各条通道在连接时按链路结果走 P2P 或退回中转；没有时直接走中转
    params.m_transportFactory = m_transportFactory.get();
}

void CShareTransferSession::FillRecvParams(foldertransfer::FolderRecvSessionParams& params) const
{
    params.m_sessionId = m_relay.m_transferId;
    params.m_relayIp = m_relay.m_relayIp;
    params.m_relayPort = m_relay.m_relayPort;
    params.m_relayToken = m_relay.m_token;
    params.m_dataLanes = m_relay.m_lanes;
    params.m_serverPubKey = m_hasServerPubKey ? m_serverPubKey : NULL;
    params.m_transportFactory = m_transportFactory.get();
}

void CShareTransferSession::Post(int type, int errCode, int64_t bytesDone, int64_t bytesTotal, int64_t filesDone,
                                 int64_t filesTotal)
{
    ShareSessionEvent ev;
    ev.m_type = type;
    ev.m_accessId = m_relay.m_transferId;
    ev.m_errCode = errCode;
    ev.m_bytesDone = bytesDone;
    ev.m_bytesTotal = bytesTotal;
    ev.m_filesDone = filesDone;
    ev.m_filesTotal = filesTotal;
    m_sink->PostSessionEvent(ev);
}

} // namespace fileshare
