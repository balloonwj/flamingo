/**
 *  文件 / 图片传输会话实现，FileSession.cpp
 *  balloonwj@qq.com   2026-05-22
 */
#include "FileSession.h"
#include <string.h>
#include <time.h>
#include <stdio.h>
#include <fstream>
#include <functional>
#include <vector>
#include "../net/EventLoop.h"
#include "../net/TcpConnection.h"
#include "../net/FrameCodec.h"
#include "../net/FramePayload.h"
#include "../base/AsyncLog.h"
#include "../base/Singleton.h"
#include "../utils/FileToken.h"
#include "../utils/MD5.h"
#include "../jsoncpp1.9.0/json.h"
#include "FileMsg.h"
#include "UpdatePackageIndex.h"
#include "FileManager.h"

using namespace net;

//以 64 位偏移定位文件读写位置。标准 fseek 的偏移参数是 long——Windows 上 long 恒为
//32 位（x64 目标亦然），32 位 Linux 同样如此，故文件一旦超过 2 GiB，偏移就会溢出
//截断，造成上传续传位点错乱、下载读到错误位置。聊天文件与群文件都不限大小，因此
//所有文件定位一律走各平台的 64 位版本。
//参数 fp 为已打开的文件句柄（调用方持有所有权，本函数不关闭）；offset 为字节偏移，
//可为负（配合 SEEK_CUR / SEEK_END）；origin 取 SEEK_SET / SEEK_CUR / SEEK_END。
//返回 true 表示定位成功，false 表示失败（此时文件位置未定义，调用方须放弃本次读写）。
static bool seekFile64(FILE* fp, int64_t offset, int origin)
{
#ifdef _WIN32
    return _fseeki64(fp, offset, origin) == 0;
#else
    return fseeko(fp, static_cast<off_t>(offset), origin) == 0;
#endif
}

//取文件当前读写位置（64 位）。溢出理由同 seekFile64——标准 ftell 返回 long，大文件
//下会截断成错误值。参数 fp 为已打开的文件句柄（调用方持有所有权）。
//返回当前位置的字节偏移；失败返回 -1，调用方须自行兜底、不可把 -1 当作有效位点。
static int64_t tellFile64(FILE* fp)
{
#ifdef _WIN32
    return _ftelli64(fp);
#else
    return static_cast<int64_t>(ftello(fp));
#endif
}

//把 Json::Value 序列化为紧凑 JSON 文本
static std::string writeCompact(const Json::Value& v)
{
    Json::StreamWriterBuilder wb;
    wb["indentation"] = "";
    return Json::writeString(wb, v);
}

//构造 { code, msg } 错误应答 JSON 文本
static std::string buildErrorJson(int code, const std::string& msg)
{
    Json::Value root;
    root["code"] = code;
    root["msg"]  = msg;
    return writeCompact(root);
}

//判断某文件场景是否允许在本服务器收发：图片服务器只收图片类场景（头像 /
//图片消息），文件服务器只收文件类场景（聊天文件 / 群文件）。令牌的 scene
//由 chatserver 签发，本校验是防御性的对称兜底，防止令牌被拿到错误的服务器用。
static bool isSceneAllowedOnServer(int serverKind, int scene)
{
    if (serverKind == file_server_kind_image)
        return FileToken::isImageScene(scene);
    if (serverKind == file_server_kind_file)
        return FileToken::isFileScene(scene);
    //未知服务器种类：保守拒绝
    return false;
}

//把 UTF-8 JSON 文本解析为 Json::Value；解析失败或非对象返回 false
static bool parseJsonObject(const std::string& json, Json::Value& root)
{
    Json::CharReaderBuilder b;
    JSONCPP_STRING errs;
    Json::CharReader* reader = b.newCharReader();
    bool ok = reader->parse(json.c_str(), json.c_str() + json.size(), &root, &errs);
    delete reader;
    return ok && errs.empty() && root.isObject();
}

//取 JSON 字符串字段；缺失或非字符串返回空串
static std::string jsonString(const Json::Value& root, const char* key)
{
    if (!root[key].isString())
        return std::string();
    return root[key].asString();
}

//取 JSON 整型字段；同时兼容数字与字符串两种写法，缺失返回 0
static int64_t jsonInt64(const Json::Value& root, const char* key)
{
    const Json::Value& v = root[key];
    if (v.isNumeric())
        return v.asInt64();
    if (v.isString())
        return static_cast<int64_t>(atoll(v.asString().c_str()));
    return 0;
}

//把字符串转为小写，用于 MD5 大小写无关比较
static std::string toLower(const std::string& s)
{
    std::string r = s;
    for (size_t i = 0; i < r.size(); ++i)
    {
        if (r[i] >= 'A' && r[i] <= 'Z')
            r[i] = static_cast<char>(r[i] - 'A' + 'a');
    }
    return r;
}

//计算磁盘文件内容的 MD5（小写十六进制）；打开失败返回空串
static std::string computeFileMd5(const std::string& path)
{
    std::ifstream in(path.c_str(), std::ios::binary);
    if (!in)
        return std::string();
    MD5 md5(in);
    return md5.toString();
}

FileSession::FileSession(const std::shared_ptr<TcpConnection>& conn, const char* filebasedir,
                         const std::string& tokenkey, int serverkind,
                         const std::string& relaytokenkey, FolderRelayHub* relayhub)
    : TcpSession(conn),
      m_strFileBaseDir(filebasedir),
      m_tokenKey(tokenkey),
      m_serverKind(serverkind),
      m_mode(mode_idle),
      m_fp(NULL),
      m_expectSize(0),
      m_uploadOffset(0),
      m_relayEndpoint(relayhub, relaytokenkey)
{
}

FileSession::~FileSession()
{
    //会话销毁时，未完成的上传留下 .part 临时文件（可由后续续传复用或清理）
    if (m_fp != NULL)
        fclose(m_fp);
}

void FileSession::onRead(const std::shared_ptr<TcpConnection>& conn, ByteBuffer* pBuffer, Timestamp receivTime)
{
    (void)receivTime;
    while (true)
    {
        frame_header header;
        std::string framebody;
        size_t consumed = 0;
        frame_decode_result r = decodeFrame(pBuffer->peek(), pBuffer->readableBytes(),
                                            header, framebody, consumed, MAX_BODY_FILESERVER);
        //数据不足一个完整帧，等待后续字节
        if (r == frame_decode_need_more)
            return;
        //协议错误（魔数 / 版本 / 长度 / CRC 非法），关闭连接
        if (r == frame_decode_error)
        {
            LOGE("decodeFrame error, close connection, client: %s",
                 conn->peerAddress().toIpPort().c_str());
            conn->forceClose();
            return;
        }

        //成功解出一帧，前移读指针
        pBuffer->retrieve(consumed);

        //协议加密：握手、加密状态校验与解密，必须排在业务分发之前
        bool securityHandled = false;
        if (!processFrameSecurity(conn, header, framebody, securityHandled))
            return;

        //本帧是握手帧，已在上面处理并应答完毕，不进业务分发
        if (securityHandled)
            continue;

        if (!processFrame(conn, header.cmd, header.seq, framebody))
        {
            LOGE("process frame error, close connection, client: %s",
                 conn->peerAddress().toIpPort().c_str());
            conn->forceClose();
            return;
        }
    }
}

bool FileSession::processFrameSecurity(const std::shared_ptr<TcpConnection>& conn,
                                       const frame_header& header,
                                       std::string& body, bool& handled)
{
    handled = false;

    const std::string peer = conn->peerAddress().toIpPort();

    std::string response;
    std::string err;
    net::frame_security_result r =
        net::processInboundFrame(m_handshake, header.cmd, header.seq, header.flags,
                                 header.originlen, body, response, err);

    if (r == net::frame_security_error)
    {
        LOGE("frame security check failed: %s, cmd: %u, close connection, client: %s",
             err.c_str(), static_cast<unsigned>(header.cmd), peer.c_str());
        conn->forceClose();
        return false;
    }

    if (r == net::frame_security_handled)
    {
        handled = true;

        //三帧握手里只有第 2 帧有应答。此刻 m_handshake.ready() 仍为 false，
        //sendFrame 走的正是明文分支——客户端那时也还没初始化接收流。
        if (!response.empty())
            sendFrame(cmd_handshake, header.seq, 0, response);

        if (m_handshake.ready())
        {
            LOGI("handshake done, frame encryption is now active, client: %s, key fingerprint: %s",
                 peer.c_str(), net::cryptoServerIdentity().fingerprint().c_str());
        }
    }

    return true;
}

bool FileSession::processFrame(const std::shared_ptr<TcpConnection>& conn,
                               uint16_t cmd, uint32_t seq, const std::string& body)
{
    switch (cmd)
    {
    //客户端上传一片文件字节。模式互斥的一个方向在此判：中转连接不得再调文件接口，
    //否则等于拿一枚中转令牌绕过了上传令牌里的场景校验（见方案第 6.2 节）
    case cmd_file_upload:
        if (m_mode == mode_folder_relay)
        {
            LOGE("file upload rejected on a folder relay connection, session: %s, lane: %d, "
                 "close connection, client: %s",
                 m_relayEndpoint.sessionId().c_str(), m_relayEndpoint.lane(),
                 conn->peerAddress().toIpPort().c_str());
            return false;
        }
        return onUploadChunk(conn, seq, body);

    //客户端请求下载一片文件字节。模式互斥的判定与上传同理
    case cmd_file_download:
        if (m_mode == mode_folder_relay)
        {
            LOGE("file download rejected on a folder relay connection, session: %s, lane: %d, "
                 "close connection, client: %s",
                 m_relayEndpoint.sessionId().c_str(), m_relayEndpoint.lane(),
                 conn->peerAddress().toIpPort().c_str());
            return false;
        }
        return onDownloadChunk(conn, seq, body);

    //文件夹传输的中转握手：本连接凭中转令牌报到，按 (sessionid, lane) 与对端配成一对。
    //连上并完成协议加密握手之后的第一帧就应当是它
    case cmd_folder_relay_hello:
        return onFolderRelayHello(conn, seq, body);

    //文件夹传输的中转数据帧：服务端不解析包体，只找到配对端、用它的会话密钥重新加密后发出
    case cmd_folder_relay_data:
        return onFolderRelayData(conn, seq, body);

    //本连接不该出现的 cmd：文件连接只认上述四个命令，其余一律按协议错误关连接
    default:
        LOGE("unsupported cmd: %u, client: %s",
             static_cast<unsigned>(cmd), conn->peerAddress().toIpPort().c_str());
        return false;
    }
}

bool FileSession::onFolderRelayHello(const std::shared_ptr<TcpConnection>& conn,
                                     uint32_t seq, const std::string& body)
{
    const std::string peer = conn->peerAddress().toIpPort();

    //模式互斥的另一个方向：已经在传文件的连接不再受理中转握手；已经做过中转握手的连接
    //也不许再报到一次（重复报到会在配对表里留下摘不掉的残留登记）。三种情形共用这一处
    //判定，日志里带上当前所处的模式即可分辨是哪一种
    if (m_mode != mode_idle)
    {
        LOGE("folder relay hello rejected: connection is already in transfer mode %d, "
             "close connection, client: %s", m_mode, peer.c_str());
        sendFolderRelayHelloAck(seq, error_code_permission_denied, "connection is busy");
        return false;
    }

    //令牌校验、角色校验、通道序号校验与配对表登记都在端点类里，本处只按结果决定
    //应答内容与去留。登记时交进去的必须是持有本会话的那个 shared_ptr——配对表保存弱引用
    int code = m_relayEndpoint.handleHello(body, shared_from_this(), peer);
    if (code != error_code_ok)
    {
        //失败原因已由 handleHello 逐条记过日志，这里只把错误码回给客户端并断开
        sendFolderRelayHelloAck(seq, code, "folder relay hello rejected");
        return false;
    }

    //本连接自此只做中转：后续 cmd_file_upload / cmd_file_download 一律拒绝
    m_mode = mode_folder_relay;
    sendFolderRelayHelloAck(seq, error_code_ok, "ok");
    return true;
}

bool FileSession::onFolderRelayData(const std::shared_ptr<TcpConnection>& conn,
                                    uint32_t seq, const std::string& body)
{
    //只有完成中转握手的连接才允许转发。空闲连接直接发数据帧属协议违规（握手必须是首帧），
    //上传 / 下载中的连接发它则是想跳过令牌校验直接占用一条中转通道，两者一律断开
    if (m_mode != mode_folder_relay)
    {
        LOGE("folder relay data rejected: connection is in transfer mode %d, not relay mode, "
             "close connection, client: %s", m_mode, conn->peerAddress().toIpPort().c_str());
        return false;
    }

    std::shared_ptr<FileSession> peerSession = m_relayEndpoint.getPeer(conn);
    if (!peerSession)
    {
        //对端那条同序号通道还没连上、或已经断开：丢掉本帧但不关本连接。已断开的情形由
        //FileServer::onDisconnected 连带关闭本次传输的全部通道，两端据此走失败路径
        LOGW("folder relay data dropped: peer lane is not connected, session: %s, lane: %d, "
             "bodylen: %u, client: %s",
             m_relayEndpoint.sessionId().c_str(), m_relayEndpoint.lane(),
             static_cast<unsigned>(body.size()), conn->peerAddress().toIpPort().c_str());
        return true;
    }

    //转发成功的帧不打日志：一次文件夹传输的数据帧数以万计，逐帧记录会淹没日志文件，
    //也拖慢转发本身。异常路径（丢帧、配对失败、通道断开）都有日志，排查够用
    peerSession->sendRelayFrame(cmd_folder_relay_data, seq, body);
    return true;
}

bool FileSession::onUploadChunk(const std::shared_ptr<TcpConnection>& conn,
                                uint32_t seq, const std::string& body)
{
    //拆出附件包体：描述符 JSON + 分片二进制
    std::string descriptor;
    size_t binoff = 0;
    size_t binlen = 0;
    if (!decodeAttachmentBody(body.data(), body.size(), descriptor, binoff, binlen))
    {
        LOGE("upload: decodeAttachmentBody failed, client: %s",
             conn->peerAddress().toIpPort().c_str());
        return false;
    }
    const char* binptr = body.data() + binoff;

    //解析描述符
    Json::Value desc;
    if (!parseJsonObject(descriptor, desc))
    {
        LOGE("upload: bad descriptor, client: %s", conn->peerAddress().toIpPort().c_str());
        return false;
    }
    std::string token  = jsonString(desc, "uploadtoken");
    int64_t     offset = jsonInt64(desc, "offset");

    //校验上传令牌
    FileTokenPayload payload;
    int64_t now = static_cast<int64_t>(time(NULL)) * 1000;
    if (!FileToken::verify(token, m_tokenKey, now, payload)
        || payload.type != FileToken::token_type_upload)
    {
        LOGE("upload: invalid token, client: %s", conn->peerAddress().toIpPort().c_str());
        sendJsonResponse(cmd_file_upload, seq,
            buildErrorJson(error_code_invalid_file_token, "invalid upload token"));
        return true;
    }
    //场景与服务器种类须匹配：图片服务器只收图片类场景，文件服务器只收文件类场景
    if (!isSceneAllowedOnServer(m_serverKind, payload.scene))
    {
        //这一拒绝只会在令牌被拿到错误的服务器上用时发生，属于上游逻辑错误，必须
        //留下记录；否则日志里只看得到连接建立又断开，无从判断原因（2026-09-30 排查
        //图片消息下载失败时即是如此）。
        LOGE("upload: scene not allowed on this server, scene: %d, serverkind: %d, md5: %s, client: %s",
             payload.scene, m_serverKind, payload.md5.c_str(),
             conn->peerAddress().toIpPort().c_str());
        sendJsonResponse(cmd_file_upload, seq,
            buildErrorJson(error_code_permission_denied, "scene not allowed on this server"));
        return true;
    }

    const std::string& md5 = payload.md5;
    int64_t expectSize = payload.size;

    //首次处理该文件：判断秒传、打开 .part 临时文件
    if (m_fp == NULL)
    {
        //最终文件已在磁盘——秒传命中，无需写入，直接回完成
        if (Singleton<FileManager>::Instance().isFileExsit(md5.c_str()))
        {
            Json::Value done;
            done["code"]       = error_code_ok;
            done["done"]       = true;
            done["nextoffset"] = static_cast<Json::Int64>(expectSize);
            sendJsonResponse(cmd_file_upload, seq, writeCompact(done));
            LOGI("upload hit instant-transfer, md5: %s, client: %s",
                 md5.c_str(), conn->peerAddress().toIpPort().c_str());
            return true;
        }

        m_curMd5     = md5;
        m_expectSize = expectSize;
        m_partPath   = m_strFileBaseDir + md5 + ".part";

        //.part 已存在则续传（rb+），否则新建（wb）
        m_fp = fopen(m_partPath.c_str(), "rb+");
        if (m_fp != NULL)
        {
            //以 .part 的现有长度作续传位点；取长度失败则从头重传，
            //绝不能把 -1 当位点用，否则后续偏移计算全错
            seekFile64(m_fp, 0, SEEK_END);
            m_uploadOffset = tellFile64(m_fp);
            if (m_uploadOffset < 0)
                m_uploadOffset = 0;
        }
        else
        {
            m_fp = fopen(m_partPath.c_str(), "wb");
            m_uploadOffset = 0;
        }
        if (m_fp == NULL)
        {
            LOGE("upload: open part file failed: %s", m_partPath.c_str());
            sendJsonResponse(cmd_file_upload, seq,
                buildErrorJson(error_code_server_internal, "open file failed"));
            return true;
        }
        m_mode = mode_uploading;
    }
    //已在上传中：必须与首片同一文件
    else if (md5 != m_curMd5)
    {
        LOGE("upload: md5 mismatch within one session, client: %s",
             conn->peerAddress().toIpPort().c_str());
        return false;
    }

    //分片落盘：仅当 offset 与权威续传位点对齐时才写
    if (offset == m_uploadOffset && binlen > 0)
    {
        //防御：写入后总量不得超过期望大小
        if (m_uploadOffset + static_cast<int64_t>(binlen) > m_expectSize)
        {
            LOGE("upload: size overrun, md5: %s", m_curMd5.c_str());
            abortUpload();
            sendJsonResponse(cmd_file_upload, seq,
                buildErrorJson(error_code_upload_fail, "upload size overrun"));
            return true;
        }
        //定位失败就此终止本次上传：继续 fwrite 会写到错误位置、污染 .part 文件
        if (!seekFile64(m_fp, m_uploadOffset, SEEK_SET))
        {
            LOGE("upload: fseek failed, offset: %lld, md5: %s",
                 static_cast<long long>(m_uploadOffset), m_curMd5.c_str());
            abortUpload();
            sendJsonResponse(cmd_file_upload, seq,
                buildErrorJson(error_code_server_internal, "seek file failed"));
            return true;
        }
        if (fwrite(binptr, 1, binlen, m_fp) != binlen)
        {
            LOGE("upload: fwrite failed, md5: %s", m_curMd5.c_str());
            abortUpload();
            sendJsonResponse(cmd_file_upload, seq,
                buildErrorJson(error_code_server_internal, "write file failed"));
            return true;
        }
        fflush(m_fp);
        m_uploadOffset += static_cast<int64_t>(binlen);
    }
    //出现空洞：不写入，回当前权威位点让客户端从此续传
    else if (offset > m_uploadOffset)
    {
        Json::Value prog;
        prog["code"]       = error_code_ok;
        prog["done"]       = false;
        prog["nextoffset"] = static_cast<Json::Int64>(m_uploadOffset);
        sendJsonResponse(cmd_file_upload, seq, writeCompact(prog));
        return true;
    }
    //offset < m_uploadOffset：客户端重发了已有数据，忽略写入，照常走收尾判断

    //是否已收齐：以累计写入量是否达到期望大小为准
    if (m_uploadOffset >= m_expectSize)
    {
        fclose(m_fp);
        m_fp = NULL;

        //校验文件内容 MD5（大小写无关）
        std::string actualMd5 = computeFileMd5(m_partPath);
        if (toLower(actualMd5) != toLower(m_curMd5))
        {
            LOGE("upload: md5 mismatch, expect: %s, actual: %s",
                 m_curMd5.c_str(), actualMd5.c_str());
            remove(m_partPath.c_str());
            resetFile();
            sendJsonResponse(cmd_file_upload, seq,
                buildErrorJson(error_code_md5_mismatch, "md5 mismatch"));
            return true;
        }

        //把 .part 转正为最终文件
        std::string finalPath = m_strFileBaseDir + m_curMd5;
        int64_t total = m_uploadOffset;
        if (Singleton<FileManager>::Instance().isFileExsit(m_curMd5.c_str()))
        {
            //并发情形下他方已先行完成同一文件：丢弃本端 .part
            remove(m_partPath.c_str());
        }
        else if (rename(m_partPath.c_str(), finalPath.c_str()) != 0)
        {
            LOGE("upload: rename part to final failed: %s", finalPath.c_str());
            remove(m_partPath.c_str());
            resetFile();
            sendJsonResponse(cmd_file_upload, seq,
                buildErrorJson(error_code_server_internal, "finalize file failed"));
            return true;
        }
        else
        {
            Singleton<FileManager>::Instance().addFile(m_curMd5.c_str());
        }

        LOGI("upload complete, md5: %s, size: %lld, client: %s",
             m_curMd5.c_str(), static_cast<long long>(total),
             conn->peerAddress().toIpPort().c_str());
        resetFile();

        Json::Value done;
        done["code"]       = error_code_ok;
        done["done"]       = true;
        done["nextoffset"] = static_cast<Json::Int64>(total);
        sendJsonResponse(cmd_file_upload, seq, writeCompact(done));
        return true;
    }

    //尚未收齐：回当前进度
    Json::Value prog;
    prog["code"]       = error_code_ok;
    prog["done"]       = false;
    prog["nextoffset"] = static_cast<Json::Int64>(m_uploadOffset);
    sendJsonResponse(cmd_file_upload, seq, writeCompact(prog));
    return true;
}

bool FileSession::onDownloadChunk(const std::shared_ptr<TcpConnection>& conn,
                                  uint32_t seq, const std::string& body)
{
    //下载请求帧为普通 JSON 包体（无附件）
    Json::Value desc;
    if (!parseJsonObject(body, desc))
    {
        LOGE("download: bad request body, client: %s", conn->peerAddress().toIpPort().c_str());
        return false;
    }
    std::string token  = jsonString(desc, "downloadtoken");
    int64_t     offset = jsonInt64(desc, "offset");

    //校验下载令牌
    FileTokenPayload payload;
    int64_t now = static_cast<int64_t>(time(NULL)) * 1000;
    if (!FileToken::verify(token, m_tokenKey, now, payload)
        || payload.type != FileToken::token_type_download)
    {
        LOGE("download: invalid token, client: %s", conn->peerAddress().toIpPort().c_str());
        sendDownloadError(seq, error_code_invalid_file_token, "invalid download token");
        return true;
    }
    //场景与服务器种类须匹配：图片服务器只收图片类场景，文件服务器只收文件类场景
    if (!isSceneAllowedOnServer(m_serverKind, payload.scene))
    {
        //理由同上传分支：令牌被拿到了错误的服务器上，属于上游逻辑错误，必须留下记录。
        LOGE("download: scene not allowed on this server, scene: %d, serverkind: %d, md5: %s, client: %s",
             payload.scene, m_serverKind, payload.md5.c_str(),
             conn->peerAddress().toIpPort().c_str());
        sendDownloadError(seq, error_code_permission_denied, "scene not allowed on this server");
        return true;
    }

    const std::string& md5 = payload.md5;

    //打开目标文件（首次下载、或切换到另一文件时重新打开）
    if (m_fp == NULL || m_curMd5 != md5)
    {
        if (m_fp != NULL)
            resetFile();

        //两类文件的寻址方式不同：聊天文件 / 群文件落盘时即以 MD5 命名可直接拼接，
        //客户端升级包由运维手工放入、文件名是人起的，须经索引反查。差异收敛在
        //ResolveDownloadPath 里，本处不做场景判断。
        std::string path;
        if (!ResolveDownloadPath(payload.scene, md5, now, m_strFileBaseDir,
                                 Singleton<UpdatePackageIndex>::Instance(), path))
        {
            LOGE("download: resolve path failed, scene: %d, md5: %s", payload.scene, md5.c_str());
            sendDownloadError(seq, error_code_file_not_exist, "file not exist");
            return true;
        }

        m_fp = fopen(path.c_str(), "rb");
        if (m_fp == NULL)
        {
            LOGE("download: file not exist, path: %s, md5: %s", path.c_str(), md5.c_str());
            sendDownloadError(seq, error_code_file_not_exist, "file not exist");
            return true;
        }
        //以文件实际长度作下载总长；取长度失败视同文件不可用，避免用 -1 参与偏移计算
        seekFile64(m_fp, 0, SEEK_END);
        m_expectSize = tellFile64(m_fp);
        if (m_expectSize < 0)
        {
            LOGE("download: get file size failed, md5: %s", md5.c_str());
            resetFile();
            sendDownloadError(seq, error_code_server_internal, "get file size failed");
            return true;
        }
        m_curMd5     = md5;
        m_mode       = mode_downloading;
    }

    int64_t totalsize = m_expectSize;
    //offset 越界判定
    if (offset < 0 || offset > totalsize)
    {
        LOGE("download: bad offset %lld, totalsize: %lld",
             static_cast<long long>(offset), static_cast<long long>(totalsize));
        sendDownloadError(seq, error_code_bad_request, "bad offset");
        return true;
    }

    //本片字节数：不超过分片上限，且不越过文件尾
    int64_t remain   = totalsize - offset;
    int64_t chunklen = (remain < static_cast<int64_t>(FILE_CHUNK_SIZE))
                           ? remain : static_cast<int64_t>(FILE_CHUNK_SIZE);
    std::string chunk;
    if (chunklen > 0)
    {
        std::vector<char> buf(static_cast<size_t>(chunklen));
        //定位失败就不要读了：否则会把文件里另一处的内容当成本片发出去
        if (!seekFile64(m_fp, offset, SEEK_SET))
        {
            LOGE("download: fseek failed, offset: %lld, md5: %s",
                 static_cast<long long>(offset), md5.c_str());
            sendDownloadError(seq, error_code_server_internal, "seek file failed");
            return true;
        }
        size_t rd = fread(&buf[0], 1, static_cast<size_t>(chunklen), m_fp);
        if (rd != static_cast<size_t>(chunklen))
        {
            LOGE("download: fread failed, md5: %s", md5.c_str());
            sendDownloadError(seq, error_code_server_internal, "read file failed");
            return true;
        }
        chunk.assign(&buf[0], static_cast<size_t>(chunklen));
    }
    bool done = (offset + chunklen >= totalsize);

    //描述符：本片偏移、文件总大小、是否末片
    Json::Value d;
    d["code"]      = error_code_ok;
    d["offset"]    = static_cast<Json::Int64>(offset);
    d["totalsize"] = static_cast<Json::Int64>(totalsize);
    d["done"]      = done;

    std::string attBody;
    if (!encodeAttachmentBody(writeCompact(d), chunk.data(),
                              static_cast<uint32_t>(chunk.size()), attBody))
    {
        LOGE("download: encodeAttachmentBody failed, md5: %s", md5.c_str());
        return false;
    }
    sendFrame(cmd_file_download, seq, frame_flag_has_attachment, attBody);

    LOGI("download chunk, md5: %s, offset: %lld, chunklen: %lld, done: %d, client: %s",
         md5.c_str(), static_cast<long long>(offset), static_cast<long long>(chunklen),
         done ? 1 : 0, conn->peerAddress().toIpPort().c_str());

    //末片发送完毕，关闭文件
    if (done)
        resetFile();
    return true;
}

void FileSession::sendFrame(uint16_t cmd, uint32_t seq, uint8_t flags, const std::string& body)
{
    net::EventLoop* loop = getOwnLoop();
    if (loop == NULL)
    {
        LOGE("TcpConnection is destroyed, drop frame, cmd: %u", static_cast<unsigned>(cmd));
        return;
    }

    //不在本连接所属的 EventLoop 线程时，把整段「组帧 + 加密 + 写 socket」投递回去执行。
    //
    //加密器是每条连接一份的流式对象，每帧推进一次内部状态，接收端严格按序解密；本连接
    //的帧却有两个来源——本会话自己回的应答帧，以及配对端 I/O 线程经 sendRelayFrame
    //转发来的数据帧。若在调用方线程里就地加密、只把写 socket 投递回来，「加密的先后」
    //与「写到线上的先后」就可能相反：配对端线程加密好的那一帧要排队等本 loop 处理，
    //而本 loop 线程自己发的帧是直接写出去的，后加密的反而先到线上，接收端必然解不开。
    //把加密也一并放到连接自己的线程里，两个顺序就恒等（这也是这里不再需要发送锁的原因）。
    //
    //任务里捕获 shared_from_this() 而不是裸 this：任务真正执行时本会话可能已被
    //会话表移除，捕获 shared_ptr 才能保证那时对象还在。
    if (!loop->isInLoopThread())
    {
        loop->runInLoop(std::bind(&FileSession::sendFrameInLoop, shared_from_this(),
                                  cmd, seq, flags, body));
        return;
    }

    sendFrameInLoop(cmd, seq, flags, body);
}

void FileSession::sendFrameInLoop(uint16_t cmd, uint32_t seq, uint8_t flags, const std::string& body)
{
    //包体的压缩与加密都由 FramePayload 这一条管线负责，顺序也在那里定死。
    //压缩策略取 never：文件分片本身多半已是压缩格式，再走一遍 zlib 既费 CPU 又压不动，
    //而且这也与收敛到管线之前的行为一致。
    //加密器未就绪（未启用加密 / 握手未完成）时它自然产出明文，握手应答正是走这条。
    std::string wire;
    uint8_t     finalFlags = 0;
    uint32_t    originlen = 0;
    if (!net::encodeFramePayload(cmd, seq, body, &m_handshake.cipher(), flags,
                                 net::payload_compress_never, wire, finalFlags, originlen))
    {
        LOGE("encode v2 frame payload failed, cmd: %u, bodylen: %u",
             static_cast<unsigned>(cmd), static_cast<unsigned>(body.size()));
        return;
    }

    std::string out;
    if (!encodeFrame(cmd, seq, finalFlags, wire.data(),
                     static_cast<uint32_t>(wire.size()), originlen, out))
    {
        LOGE("encodeFrame failed, cmd: %u, bodylen: %u",
             static_cast<unsigned>(cmd), static_cast<unsigned>(body.size()));
        return;
    }

    sendData(out.data(), out.size());
}

void FileSession::sendJsonResponse(uint16_t cmd, uint32_t seq, const std::string& json)
{
    //上传应答为普通 JSON 帧，不带附件
    sendFrame(cmd, seq, 0, json);
}

void FileSession::sendDownloadError(uint32_t seq, int code, const std::string& msg)
{
    //下载应答统一为附件帧；错误时描述符带错误码、二进制段为空
    Json::Value d;
    d["code"] = code;
    d["msg"]  = msg;

    std::string attBody;
    encodeAttachmentBody(writeCompact(d), NULL, 0, attBody);
    sendFrame(cmd_file_download, seq, frame_flag_has_attachment, attBody);
}

void FileSession::sendFolderRelayHelloAck(uint32_t seq, int code, const std::string& msg)
{
    //中转握手应答为普通 JSON 帧（不带附件），cmd 与请求一致，seq 原样回带供客户端关联
    Json::Value root;
    root["code"] = code;
    root["msg"]  = msg;
    sendFrame(cmd_folder_relay_hello, seq, 0, writeCompact(root));
}

void FileSession::sendRelayFrame(uint16_t cmd, uint32_t seq, const std::string& plainBody)
{
    //转发的帧不带任何附加标志位：按协议约定（docs/协议设计-v2.md 第 12.3.2 节）
    //cmd_folder_relay_data 的包体是一段不透明的应用帧，既不压缩、也不是附件包体布局，
    //故这里只需交给 sendFrame 用本连接的会话密钥重新加密后发出。
    //注意本函数运行在<u>配对端</u>的 I/O 线程上，sendFrame 会把整段动作投递回本连接
    //所属的 EventLoop 线程，加密与写出的先后因此始终一致
    sendFrame(cmd, seq, 0, plainBody);
}

void FileSession::abortUpload()
{
    if (m_fp != NULL)
    {
        fclose(m_fp);
        m_fp = NULL;
    }
    if (!m_partPath.empty())
        remove(m_partPath.c_str());
    resetFile();
}

void FileSession::resetFile()
{
    if (m_fp != NULL)
    {
        fclose(m_fp);
        m_fp = NULL;
    }
    m_mode         = mode_idle;
    m_curMd5.clear();
    m_partPath.clear();
    m_expectSize   = 0;
    m_uploadOffset = 0;
}
