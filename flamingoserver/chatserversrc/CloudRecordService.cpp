/**
 *  云端录制的服务端处理流程，职责见 CloudRecordService.h。
 *  balloonwj@qq.com   2026-10-06
 */
#include "CloudRecordService.h"

#include "../base/AsyncLog.h"
#include "../jsoncpp1.9.0/json.h"
#include "../utils/CloudRecordCtlMsg.h"
#include "../utils/FileToken.h"
#include "Protocol.h"

namespace {

//线上实例
CloudRecordService* g_cloudRecordService = NULL;

//错误码对应的应答 msg（给排查者看，客户端只认 code）
const char* errorText(int code)
{
    switch (code)
    {
    //成功
    case error_code_ok:
        return "ok";
    //请求包体缺字段或取值非法
    case error_code_bad_request:
        return "bad request";
    //企业没有开通云端录制
    case error_code_cloudrecord_disabled:
        return "cloud record disabled";
    //正在录制，或要操作的录制不在进行中
    case error_code_cloudrecord_busy:
        return "cloud record busy";
    //控制连接不可用，或 imgserver 拒绝
    case error_code_cloudrecord_unavailable:
        return "cloud storage unavailable";
    //录像不存在或无权访问
    case error_code_cloudrecord_not_found:
        return "record not found";
    //配额已满
    case error_code_storage_quota:
        return "quota exceeded";
    //写库失败等
    case error_code_server_internal:
        return "server internal error";
    //标题命中敏感词
    case error_code_sensitive_word:
        return "sensitive word";
    //其余错误码：本模块不会产生，兜底
    default:
        return "failed";
    }
}

std::string opReply(int code, int op, int64_t recordId, const CloudRecordRow* row = NULL,
                    const CloudRecordTokenInfo* token = NULL)
{
    return CloudRecordProtocol::buildOpResponse(code, errorText(code), op, recordId, row, token);
}

//imgserver 回报的结束原因（cloudrec_stop_reason 的取值）换算成下发给房间的原因：正常停止为 0，其余都是云端存储不可用
const int kStopReasonRequested = 1;

} // namespace

//类内初始化的静态常量的类外定义：被 ODR-use 时 g++ 要求有类外定义，MSVC 不要求
const int CloudRecordService::kMaintenanceBatch;

CloudRecordRoomInfo::CloudRecordRoomInfo()
    : m_ctype(0)
    , m_cid(0)
{
}

CloudRecordService::CloudRecordService(CloudRecordStore& store, ICloudRecordEnv& env, ICloudRecordCtlSender& sender,
                                       const std::string& fileTokenKey)
    : m_store(store)
    , m_env(env)
    , m_sender(sender)
    , m_fileTokenKey(fileTokenKey)
{
}

void CloudRecordService::setInstance(CloudRecordService* service)
{
    g_cloudRecordService = service;
}

CloudRecordService* CloudRecordService::instance()
{
    return g_cloudRecordService;
}

int CloudRecordService::checkStart(int64_t userId)
{
    std::lock_guard<std::mutex> guard(m_mutex);
    const int64_t enterpriseId = m_env.enterpriseOf(userId);
    if (enterpriseId <= 0 || !m_env.isEnabled(enterpriseId))
        return error_code_cloudrecord_disabled;
    if (!m_sender.isUp())
        return error_code_cloudrecord_unavailable;
    int64_t used = 0;
    if (!m_store.usedBytes(enterpriseId, used))
        return error_code_server_internal;
    if (!CloudRecordRules::quotaAllows(used, m_env.quotaGb(enterpriseId), 0))
        return error_code_storage_quota;
    return error_code_ok;
}

int CloudRecordService::createShareRecord(int64_t userId, const CloudRecordRoomInfo& room, int64_t nowMs,
                                          int64_t& recordId)
{
    std::lock_guard<std::mutex> guard(m_mutex);
    recordId = 0;
    CloudRecordRow row;
    row.m_enterpriseId = m_env.enterpriseOf(userId);
    row.m_owner = userId;
    row.m_source = cloudrecord_source_share;
    row.m_roomId = room.m_roomId;
    row.m_ctype = room.m_ctype;
    row.m_cid = room.m_cid;
    row.m_title = CloudRecordRules::defaultTitle(room.m_title);
    row.m_startTime = nowMs;
    row.m_status = cloudrecord_status_recording;
    row.m_createTime = nowMs;
    if (!m_store.insert(row))
        return error_code_server_internal;
    //原始帧文件名要等有了录像 id 才能定：写入后补上（它由 imgserver 按同一规则命名）
    row.m_frecFile = CloudRecordFileName(static_cast<uint64_t>(row.m_id), cloudrec_file_type_frec);
    if (!m_store.setFiles(row.m_id, row.m_frecFile, std::string(), 0))
    {
        m_store.remove(row.m_id);
        return error_code_server_internal;
    }
    recordId = row.m_id;
    return error_code_ok;
}

void CloudRecordService::discardRecord(int64_t recordId)
{
    std::lock_guard<std::mutex> guard(m_mutex);
    m_store.remove(recordId);
    LOGI("cloud record discarded, id: %lld", (long long)recordId);
}

bool CloudRecordService::sendStart(int64_t recordId, const std::string& roomId)
{
    CloudRecordCtlMsg msg;
    msg.m_op = cloudrecctl_op_start;
    msg.m_recordId = static_cast<uint64_t>(recordId);
    msg.m_roomId = roomId;
    return m_sender.send(BuildCloudRecordCtlMsg(msg));
}

bool CloudRecordService::sendPause(int64_t recordId)
{
    CloudRecordCtlMsg msg;
    msg.m_op = cloudrecctl_op_pause;
    msg.m_recordId = static_cast<uint64_t>(recordId);
    return m_sender.send(BuildCloudRecordCtlMsg(msg));
}

bool CloudRecordService::sendResume(int64_t recordId)
{
    CloudRecordCtlMsg msg;
    msg.m_op = cloudrecctl_op_resume;
    msg.m_recordId = static_cast<uint64_t>(recordId);
    return m_sender.send(BuildCloudRecordCtlMsg(msg));
}

bool CloudRecordService::sendStop(int64_t recordId)
{
    CloudRecordCtlMsg msg;
    msg.m_op = cloudrecctl_op_stop;
    msg.m_recordId = static_cast<uint64_t>(recordId);
    const bool ok = m_sender.send(BuildCloudRecordCtlMsg(msg));
    if (!ok)
        LOGW("cloud record: stop not sent (control link down), will retry on reconnect, id: %lld", (long long)recordId);
    return ok;
}

void CloudRecordService::onCtlMessage(const std::string& body, int64_t nowMs)
{
    CloudRecordCtlMsg msg;
    if (!ParseCloudRecordCtlMsg(body, msg))
    {
        LOGE("cloud record: bad control message from imgserver: %s", body.c_str());
        return;
    }
    std::lock_guard<std::mutex> guard(m_mutex);
    switch (msg.m_op)
    {
    //开始录制的结果
    case cloudrecctl_op_started:
        onStartedLocked(static_cast<int64_t>(msg.m_recordId), msg.m_code);
        break;

    //一次录制结束、文件已关闭
    case cloudrecctl_op_finished:
        onFinishedLocked(static_cast<int64_t>(msg.m_recordId), static_cast<int64_t>(msg.m_bytes), msg.m_durationMs,
                         msg.m_frames, msg.m_reason, nowMs);
        break;

    //最终文件上传完
    case cloudrecctl_op_uploaded:
        onUploadedLocked(static_cast<int64_t>(msg.m_recordId), msg.m_file, static_cast<int64_t>(msg.m_bytes), nowMs);
        break;

    //探活的回应：连接线程已据此刷新了活跃时间，这里不用处理
    case cloudrecctl_op_pong:
        break;

    //chatserver 发出的命令种类与不认识的 op：忽略
    default:
        LOGW("cloud record: unexpected op from imgserver: %s", CloudRecordCtlOpName(msg.m_op));
        break;
    }
}

void CloudRecordService::onStartedLocked(int64_t recordId, int code)
{
    //0 为 cloudrec_start_ok：imgserver 已开始，房间状态在 1754 处理时已经设好
    if (code == 0)
    {
        LOGI("cloud record started on imgserver, id: %lld", (long long)recordId);
        return;
    }
    CloudRecordRow row;
    bool found = false;
    if (!m_store.get(recordId, row, found) || !found)
        return;
    LOGE("cloud record start failed on imgserver, id: %lld, room: %s, result: %d", (long long)recordId,
         row.m_roomId.c_str(), code);
    //没有录到任何东西：删掉记录，通知房间「云端存储不可用」
    m_store.remove(recordId);
    m_env.onRoomRecordEnded(row.m_roomId, recordId, error_code_cloudrecord_unavailable);
}

void CloudRecordService::onFinishedLocked(int64_t recordId, int64_t bytes, int64_t durationMs, uint64_t frames,
                                          int reason, int64_t nowMs)
{
    CloudRecordRow row;
    bool found = false;
    if (!m_store.get(recordId, row, found))
        return;
    if (!found)
    {
        //录像已被删除（例如开始失败后已删记录）：文件也删掉
        std::vector<std::string> files(1, CloudRecordFileName(static_cast<uint64_t>(recordId), cloudrec_file_type_frec));
        sendDeleteLocked(files);
        return;
    }
    const int status = CloudRecordRules::statusAfterFinished(frames);
    const int64_t expire = CloudRecordRules::expireTimeFor(nowMs, m_env.retainDays(row.m_enterpriseId));
    bool changed = false;
    if (!m_store.finishRecording(recordId, status, nowMs, durationMs, bytes, expire, changed) || !changed)
    {
        //重复的回报（控制连接恢复后补发的 stop 得到的第二次回报）：库里已是结束状态，不再处理
        return;
    }
    LOGI("cloud record finished, id: %lld, status: %d, bytes: %lld, duration: %lld ms, frames: %llu, reason: %d",
         (long long)recordId, status, (long long)bytes, (long long)durationMs, (unsigned long long)frames, reason);
    //imgserver 自己停的（磁盘不足、出错、退出）：房间还以为在录，要清掉并广播
    if (reason != kStopReasonRequested)
        m_env.onRoomRecordEnded(row.m_roomId, recordId, error_code_cloudrecord_unavailable);
    //一帧都没有：原始帧文件没有用处，删掉
    if (status == cloudrecord_status_failed)
    {
        std::vector<std::string> files(1, row.m_frecFile);
        sendDeleteLocked(files);
        m_store.clearFrec(recordId);
    }
    bool reread = false;
    if (m_store.get(recordId, row, reread) && reread)
        pushRecordLocked(cloudrecord_notify_status, row);
}

void CloudRecordService::onUploadedLocked(int64_t recordId, const std::string& fileName, int64_t bytes, int64_t nowMs)
{
    uint64_t parsedId = 0;
    int type = 0;
    if (!ParseCloudRecordFileName(fileName, parsedId, type) || static_cast<int64_t>(parsedId) != recordId ||
        type == cloudrec_file_type_frec)
    {
        LOGE("cloud record: uploaded event with bad file name, id: %lld, file: %s", (long long)recordId,
             fileName.c_str());
        return;
    }
    CloudRecordRow row;
    bool found = false;
    if (!m_store.get(recordId, row, found))
        return;
    if (!found)
    {
        //录像在上传期间被删除：传上来的文件没有归属，删掉
        std::vector<std::string> files(1, fileName);
        sendDeleteLocked(files);
        return;
    }
    if (row.m_source == cloudrecord_source_share && row.m_status == cloudrecord_status_pending)
    {
        //合成后的 MP4：可观看；原始帧文件不再需要
        if (!m_store.markReady(recordId, fileName, bytes, 0, 0))
            return;
        if (!row.m_frecFile.empty())
        {
            std::vector<std::string> files(1, row.m_frecFile);
            sendDeleteLocked(files);
            m_store.clearFrec(recordId);
        }
    }
    else if (row.m_source == cloudrecord_source_upload && row.m_status == cloudrecord_status_uploading &&
             fileName == row.m_file)
    {
        //独立录制上传完：到期时间从上传完成时算起
        const int64_t expire = CloudRecordRules::expireTimeFor(nowMs, m_env.retainDays(row.m_enterpriseId));
        if (!m_store.markReady(recordId, fileName, bytes, 0, expire))
            return;
    }
    else
    {
        //状态不符（重复的回报、已经可观看的录像又传了一遍）：只记日志
        LOGW("cloud record: uploaded event ignored, id: %lld, source: %d, status: %d, file: %s", (long long)recordId,
             row.m_source, row.m_status, fileName.c_str());
        return;
    }
    LOGI("cloud record ready, id: %lld, file: %s, bytes: %lld", (long long)recordId, fileName.c_str(), (long long)bytes);
    bool reread = false;
    if (m_store.get(recordId, row, reread) && reread)
        pushRecordLocked(cloudrecord_notify_status, row);
}

void CloudRecordService::onLinkUp()
{
    std::lock_guard<std::mutex> guard(m_mutex);
    std::vector<CloudRecordRow> recording;
    if (!m_store.listByStatus(cloudrecord_status_recording, recording))
        return;
    int resent = 0;
    for (size_t i = 0; i < recording.size(); ++i)
    {
        //房间还在录：控制连接只是短暂断开，imgserver 那边照常在录，不打扰
        if (m_env.isRoomRecording(recording[i].m_roomId, recording[i].m_id))
            continue;
        CloudRecordCtlMsg msg;
        msg.m_op = cloudrecctl_op_stop;
        msg.m_recordId = static_cast<uint64_t>(recording[i].m_id);
        if (m_sender.send(BuildCloudRecordCtlMsg(msg)))
            ++resent;
    }
    LOGI("cloud record link up, recordings in db: %d, stop resent: %d", (int)recording.size(), resent);
}

void CloudRecordService::sendDeleteLocked(const std::vector<std::string>& files)
{
    CloudRecordCtlMsg msg;
    msg.m_op = cloudrecctl_op_delete;
    for (size_t i = 0; i < files.size(); ++i)
    {
        if (!files[i].empty())
            msg.m_files.push_back(files[i]);
    }
    if (msg.m_files.empty())
        return;
    if (!m_sender.send(BuildCloudRecordCtlMsg(msg)))
        LOGW("cloud record: delete files not sent (control link down), first file: %s", msg.m_files[0].c_str());
}

void CloudRecordService::deleteFilesLocked(const CloudRecordRow& row)
{
    std::vector<std::string> files;
    files.push_back(row.m_frecFile);
    files.push_back(row.m_file);
    sendDeleteLocked(files);
}

void CloudRecordService::pushRecordLocked(int type, const CloudRecordRow& row)
{
    m_env.push(row.m_owner, cmd_cloudrecord_notify, CloudRecordProtocol::buildRecordNotify(type, row));
}

std::string CloudRecordService::handleList(int64_t userId, const std::string& body)
{
    int64_t beforeId = 0;
    int limit = 0;
    std::vector<CloudRecordRow> rows;
    if (!CloudRecordProtocol::parseListRequest(body, beforeId, limit))
        return CloudRecordProtocol::buildListResponse(error_code_bad_request, errorText(error_code_bad_request), rows,
                                                      false, false, 0, 0, 0);
    std::lock_guard<std::mutex> guard(m_mutex);
    bool hasMore = false;
    const int64_t enterpriseId = m_env.enterpriseOf(userId);
    int64_t used = 0;
    if (!m_store.listByOwner(userId, beforeId, limit, rows, hasMore) || !m_store.usedBytes(enterpriseId, used))
        return CloudRecordProtocol::buildListResponse(error_code_server_internal, errorText(error_code_server_internal),
                                                      rows, false, false, 0, 0, 0);
    const bool enabled = enterpriseId > 0 && m_env.isEnabled(enterpriseId);
    return CloudRecordProtocol::buildListResponse(error_code_ok, errorText(error_code_ok), rows, hasMore, enabled, used,
                                                  CloudRecordRules::quotaBytes(m_env.quotaGb(enterpriseId)),
                                                  m_env.retainDays(enterpriseId));
}

std::string CloudRecordService::handleOp(int64_t userId, const std::string& body, int64_t nowMs,
                                         ICloudRecordTitleChecker* checker)
{
    CloudRecordOpRequest req;
    if (!CloudRecordProtocol::parseOpRequest(body, req))
        return opReply(error_code_bad_request, req.m_op, req.m_recordId);
    std::lock_guard<std::mutex> guard(m_mutex);
    switch (req.m_op)
    {
    //重命名
    case cloudrecord_op_rename:
        return opRenameLocked(userId, req, checker);

    //删除
    case cloudrecord_op_delete:
        return opDeleteLocked(userId, req);

    //取下载令牌
    case cloudrecord_op_download_token:
        return opDownloadLocked(userId, req, nowMs);

    //取上传令牌
    case cloudrecord_op_upload_token:
        return opUploadLocked(userId, req, nowMs, checker);

    //发到会话
    case cloudrecord_op_send:
        return opSendLocked(userId, req, nowMs);

    //parseOpRequest 已拒绝不认识的操作，这里不会走到
    default:
        return opReply(error_code_bad_request, req.m_op, req.m_recordId);
    }
}

std::string CloudRecordService::opRenameLocked(int64_t userId, const CloudRecordOpRequest& req,
                                               ICloudRecordTitleChecker* checker)
{
    CloudRecordRow row;
    bool found = false;
    if (!m_store.get(req.m_recordId, row, found))
        return opReply(error_code_server_internal, req.m_op, req.m_recordId);
    if (!found || row.m_owner != userId)
        return opReply(error_code_cloudrecord_not_found, req.m_op, req.m_recordId);
    std::string title;
    if (!CloudRecordRules::normalizeTitle(req.m_title, title))
        return opReply(error_code_bad_request, req.m_op, req.m_recordId);
    if (checker != NULL && checker->hitTitle(title))
        return opReply(error_code_sensitive_word, req.m_op, req.m_recordId);
    if (!m_store.rename(req.m_recordId, title))
        return opReply(error_code_server_internal, req.m_op, req.m_recordId);
    row.m_title = title;
    LOGI("cloud record renamed, id: %lld, owner: %lld", (long long)row.m_id, (long long)userId);
    return opReply(error_code_ok, req.m_op, req.m_recordId, &row);
}

std::string CloudRecordService::opDeleteLocked(int64_t userId, const CloudRecordOpRequest& req)
{
    CloudRecordRow row;
    bool found = false;
    if (!m_store.get(req.m_recordId, row, found))
        return opReply(error_code_server_internal, req.m_op, req.m_recordId);
    if (!found || row.m_owner != userId)
        return opReply(error_code_cloudrecord_not_found, req.m_op, req.m_recordId);
    //录制中的录像要先停止
    if (row.m_status == cloudrecord_status_recording)
        return opReply(error_code_cloudrecord_busy, req.m_op, req.m_recordId);
    if (!m_store.remove(req.m_recordId))
        return opReply(error_code_server_internal, req.m_op, req.m_recordId);
    deleteFilesLocked(row);
    //本人的其它设备也要从列表里拿掉
    m_env.push(userId, cmd_cloudrecord_notify, CloudRecordProtocol::buildDeleteNotify(row.m_id));
    LOGI("cloud record deleted by owner, id: %lld, owner: %lld", (long long)row.m_id, (long long)userId);
    return opReply(error_code_ok, req.m_op, req.m_recordId);
}

bool CloudRecordService::canViewLocked(int64_t userId, const CloudRecordRow& row)
{
    if (userId == row.m_owner)
        return true;
    std::vector<CloudRecordShareRow> shares;
    if (!m_store.listShares(row.m_id, shares))
        return false;
    std::set<int64_t> memberGroups;
    for (size_t i = 0; i < shares.size(); ++i)
    {
        if (shares[i].m_ctype == conversation_group && m_env.isGroupMember(shares[i].m_cid, userId))
            memberGroups.insert(shares[i].m_cid);
    }
    return CloudRecordRules::canView(userId, row.m_owner, shares, memberGroups);
}

void CloudRecordService::fillTokenLocked(int64_t userId, int tokenType, const std::string& fileName, int64_t bytes,
                                         int64_t nowMs, CloudRecordTokenInfo& out)
{
    FileTokenPayload payload;
    payload.type = tokenType;
    payload.scene = file_scene_cloud_record;
    payload.md5 = CloudRecordTokenKey(fileName);
    payload.size = bytes;
    payload.uid = userId;
    payload.expire = nowMs + CloudRecordRules::kTokenTtlMs;
    out.m_token = FileToken::sign(payload, m_fileTokenKey);
    m_env.imgServerAddr(out.m_host, out.m_port);
    out.m_fileName = fileName;
    out.m_bytes = bytes;
}

std::string CloudRecordService::opDownloadLocked(int64_t userId, const CloudRecordOpRequest& req, int64_t nowMs)
{
    CloudRecordRow row;
    bool found = false;
    if (!m_store.get(req.m_recordId, row, found))
        return opReply(error_code_server_internal, req.m_op, req.m_recordId);
    const bool isOwner = found && row.m_owner == userId;
    if (!found || !CloudRecordRules::canDownload(req.m_kind, isOwner, isOwner || canViewLocked(userId, row), row))
        return opReply(error_code_cloudrecord_not_found, req.m_op, req.m_recordId);
    const bool frec = (req.m_kind == cloudrecord_file_frec);
    CloudRecordTokenInfo token;
    fillTokenLocked(userId, FileToken::token_type_download, frec ? row.m_frecFile : row.m_file,
                    frec ? row.m_frecBytes : row.m_bytes, nowMs, token);
    LOGI("cloud record download token, id: %lld, user: %lld, kind: %d", (long long)row.m_id, (long long)userId,
         req.m_kind);
    return opReply(error_code_ok, req.m_op, req.m_recordId, NULL, &token);
}

std::string CloudRecordService::opUploadLocked(int64_t userId, const CloudRecordOpRequest& req, int64_t nowMs,
                                               ICloudRecordTitleChecker* checker)
{
    CloudRecordRow row;
    CloudRecordTokenInfo token;
    if (req.m_recordId > 0)
    {
        bool found = false;
        if (!m_store.get(req.m_recordId, row, found))
            return opReply(error_code_server_internal, req.m_op, req.m_recordId);
        if (!found || row.m_owner != userId)
            return opReply(error_code_cloudrecord_not_found, req.m_op, req.m_recordId);
        //合成后的 MP4：不查配额（原始帧文件随后删除，占用不会持续增加）
        if (row.m_source == cloudrecord_source_share && row.m_status == cloudrecord_status_pending)
        {
            const std::string file = CloudRecordFileName(static_cast<uint64_t>(row.m_id), cloudrec_file_type_mp4);
            fillTokenLocked(userId, FileToken::token_type_upload, file, req.m_bytes, nowMs, token);
            LOGI("cloud record upload token (composed), id: %lld, bytes: %lld", (long long)row.m_id,
                 (long long)req.m_bytes);
            return opReply(error_code_ok, req.m_op, row.m_id, &row, &token);
        }
        //续传独立录制：大小须与第一次申请时一致
        if (row.m_source == cloudrecord_source_upload && row.m_status == cloudrecord_status_uploading &&
            row.m_bytes == req.m_bytes)
        {
            fillTokenLocked(userId, FileToken::token_type_upload, row.m_file, row.m_bytes, nowMs, token);
            return opReply(error_code_ok, req.m_op, row.m_id, &row, &token);
        }
        return opReply(error_code_cloudrecord_busy, req.m_op, req.m_recordId);
    }

    //新建一条来源为上传的录像：查开关与配额（按申请的大小预留）
    const int64_t enterpriseId = m_env.enterpriseOf(userId);
    if (enterpriseId <= 0 || !m_env.isEnabled(enterpriseId))
        return opReply(error_code_cloudrecord_disabled, req.m_op, 0);
    int64_t used = 0;
    if (!m_store.usedBytes(enterpriseId, used))
        return opReply(error_code_server_internal, req.m_op, 0);
    if (!CloudRecordRules::quotaAllows(used, m_env.quotaGb(enterpriseId), req.m_bytes))
        return opReply(error_code_storage_quota, req.m_op, 0);
    //标题不合法或命中敏感词时用默认标题，不让一个标题问题挡住整个上传；之后可以重命名
    std::string title;
    if (!CloudRecordRules::normalizeTitle(req.m_title, title) || (checker != NULL && checker->hitTitle(title)))
        title = CloudRecordRules::defaultTitle(std::string());
    row.m_enterpriseId = enterpriseId;
    row.m_owner = userId;
    row.m_source = cloudrecord_source_upload;
    row.m_title = title;
    row.m_startTime = req.m_startTime > 0 ? req.m_startTime : nowMs;
    row.m_endTime = row.m_startTime + req.m_durationMs;
    row.m_durationMs = req.m_durationMs;
    row.m_audioOnly = req.m_audioOnly;
    row.m_status = cloudrecord_status_uploading;
    //没传完就被放弃的上传也要能按保留期清理：先按申请时刻算一个到期时间，传完后重算
    row.m_expireTime = CloudRecordRules::expireTimeFor(nowMs, m_env.retainDays(enterpriseId));
    row.m_createTime = nowMs;
    if (!m_store.insert(row))
        return opReply(error_code_server_internal, req.m_op, 0);
    row.m_file = CloudRecordFileName(static_cast<uint64_t>(row.m_id),
                                     req.m_audioOnly ? cloudrec_file_type_m4a : cloudrec_file_type_mp4);
    row.m_bytes = req.m_bytes;
    if (!m_store.setFiles(row.m_id, std::string(), row.m_file, row.m_bytes))
    {
        m_store.remove(row.m_id);
        return opReply(error_code_server_internal, req.m_op, 0);
    }
    fillTokenLocked(userId, FileToken::token_type_upload, row.m_file, row.m_bytes, nowMs, token);
    LOGI("cloud record upload created, id: %lld, owner: %lld, bytes: %lld, audio only: %d", (long long)row.m_id,
         (long long)userId, (long long)row.m_bytes, row.m_audioOnly ? 1 : 0);
    return opReply(error_code_ok, req.m_op, row.m_id, &row, &token);
}

std::string CloudRecordService::opSendLocked(int64_t userId, const CloudRecordOpRequest& req, int64_t nowMs)
{
    CloudRecordRow row;
    bool found = false;
    if (!m_store.get(req.m_recordId, row, found))
        return opReply(error_code_server_internal, req.m_op, req.m_recordId);
    if (!found || row.m_owner != userId)
        return opReply(error_code_cloudrecord_not_found, req.m_op, req.m_recordId);
    if (!CloudRecordRules::canSend(row))
        return opReply(error_code_cloudrecord_busy, req.m_op, req.m_recordId);
    //发到群：本人须是群成员
    if (req.m_ctype == conversation_group && !m_env.isGroupMember(req.m_cid, userId))
        return opReply(error_code_cloudrecord_not_found, req.m_op, req.m_recordId);
    CloudRecordShareRow share;
    share.m_recordId = row.m_id;
    share.m_ctype = req.m_ctype;
    share.m_cid = req.m_cid;
    share.m_sender = userId;
    share.m_sendTime = nowMs;
    if (!m_store.addShare(share))
        return opReply(error_code_server_internal, req.m_op, req.m_recordId);
    //卡片内容：录像 id、标题、时长、大小、是否只有声音、开启者（字段见 docs/协议设计-v2.md 17.10）
    Json::Value card(Json::objectValue);
    card["recordid"] = std::to_string(static_cast<long long>(row.m_id));
    card["owner"] = std::to_string(static_cast<long long>(row.m_owner));
    card["title"] = row.m_title;
    card["durationms"] = static_cast<Json::Int64>(row.m_durationMs);
    card["bytes"] = static_cast<Json::Int64>(row.m_bytes);
    card["audioonly"] = row.m_audioOnly ? 1 : 0;
    Json::StreamWriterBuilder wb;
    wb["indentation"] = "";
    if (m_env.sendCard(userId, req.m_ctype, req.m_cid, Json::writeString(wb, card), nowMs) == 0)
        return opReply(error_code_server_internal, req.m_op, req.m_recordId);
    LOGI("cloud record sent, id: %lld, ctype: %d, cid: %lld", (long long)row.m_id, req.m_ctype, (long long)req.m_cid);
    return opReply(error_code_ok, req.m_op, req.m_recordId, &row);
}

bool CloudRecordService::enterpriseUsedBytes(int64_t enterpriseId, int64_t& out)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_store.usedBytes(enterpriseId, out);
}

void CloudRecordService::runMaintenance(int64_t nowMs)
{
    std::lock_guard<std::mutex> guard(m_mutex);
    std::vector<CloudRecordRow> rows;
    //到期前 3 天提醒一次
    if (m_store.listToWarn(nowMs, CloudRecordRules::kExpireWarnBeforeMs, kMaintenanceBatch, rows))
    {
        for (size_t i = 0; i < rows.size(); ++i)
        {
            if (m_store.markWarned(rows[i].m_id))
                pushRecordLocked(cloudrecord_notify_expire, rows[i]);
        }
    }
    int removed = 0;
    if (m_store.listExpired(nowMs, kMaintenanceBatch, rows))
    {
        for (size_t i = 0; i < rows.size(); ++i)
        {
            //还在录制的不删（到期时间只在录制结束后才设置，正常不会出现）
            if (rows[i].m_status == cloudrecord_status_recording)
                continue;
            if (!m_store.remove(rows[i].m_id))
                continue;
            deleteFilesLocked(rows[i]);
            m_env.push(rows[i].m_owner, cmd_cloudrecord_notify, CloudRecordProtocol::buildDeleteNotify(rows[i].m_id));
            ++removed;
        }
    }
    if (removed > 0)
        LOGI("cloud record maintenance: %d expired records removed", removed);
}
