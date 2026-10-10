/**
 *  云端录制的端到端联调用例（Suite CRLIVE，标签 [.live][cloudreclive]），CloudRecordLiveTests.cpp
 *
 *  第 27 期 27h（docs/屏幕共享与录制方案.md 12.6）：用产品代码走一遍云端录制的完整链路，不经过界面：
 *    - CRLIVE1 共享中的云端录制：A 发起单聊共享、推合成画面（ShareHostStreamer），B 加入并收流；A 开始云端录制（1754），
 *      推几秒后停止；A 收到「等待合成」推送（1757），列表（1755）里有这条录像；A 取原始帧的下载令牌（1756 op 3），经 imgserver
 *      下载 .frec，用产品代码离线合成（CCloudRecordComposeTask）出 MP4，取上传令牌（op 4）上传；收到「可观看」推送；
 *      发到会话（op 5），B 收到云端录像卡片（1504，msgtype 24）；B 取最终文件的下载令牌下载，与 A 上传的逐字节相同；
 *      A 重命名（op 1）后列表里是新标题；A 删除（op 2）后 B 再取令牌回 739。
 *    - CRLIVE2 保存到云端与续传：A 新建一条上传录像（op 4，recordid 为 0），传到一半取消；列表里是「上传中」；
 *      凭录像 id 再取令牌续传，服务端从已收的位置接着收（第一次进度就不小于已传部分）；收到「可观看」推送；下载回来逐字节相同；删除。
 *    - CRLIVE3（另一个标签 [cloudrecpending]）：只做 CRLIVE1 的前半段，录像停在「等待合成」，然后让 A 的真实客户端登录，
 *      核对客户端登录后自动下载原始帧、合成、上传，直到「可观看」。
 *
 *  默认不参与常规测试。跑之前要有一套开通了云端录制的服务端（chatserver、imgserver 都是第 26 期之后的版本，测试账号所在企业的
 *  cloudrecord.enabled 为 1），默认连本机 20000 端口（WSL 开发服务端），可用环境变量 FLAMINGO_LIVE_CHATPORT 改端口；
 *  文件经 1756 应答里的 imgserver 地址传输。然后：FlamingoUnit.exe "[cloudreclive]"
 *  测试账号：HC0291（5291）与 HC0292（5292），密码按开发库的测试数据（flamingoserver/testdata.sql）为 123456，可用环境变量
 *  FLAMINGO_LIVE_PASSWORD 改。会把这两个账号在别处的 PC 端顶下线。
 *
 *  balloonwj@qq.com   2026-10-07
 */
#include "stdafx.h"
#include "catch_amalgamated.hpp"

#include <atomic>
#include <map>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <vector>

#include "LiveChatClient.h"
#include "../net/FileStreamTransfer.h"
#include "../net/FrameCodec.h"
#include "../net/FrameCrypto.h"
#include "../net/FramePayload.h"
#include "../net/ImageDownloadDriver.h"
#include "../net/ImageUploadDriver.h"
#include "../net/Protocol.h"
#include "../../FlamingoMedia/Record/CloudRecordComposer.h"
#include "../../FlamingoMedia/Record/CloudRecordProtocolClient.h"
#include "../../FlamingoMedia/Share/ShareHostStreamer.h"
#include "../../FlamingoMedia/Share/ShareProtocolClient.h"
#include "../../FlamingoMedia/Share/ShareViewerReceiver.h"
#include "../third-party/jsoncpp-1.9.0/json.h"

using namespace livetest;
using namespace screenshare;
using namespace screenrecord;

namespace {

//默认连的 chatserver 端口（WSL 开发服务端）
const unsigned short kCrlChatPort = 20000;
const char* kCrlAccountA = "HC0291";
const char* kCrlAccountB = "HC0292";
//开发库测试员工的统一密码（flamingoserver/testdata.sql 的 t_user 注释）
const char* kCrlPassword = "123456";
const int64_t kCrlUserB = 5292;
//等信令应答、等推送的时间，单位毫秒
const int kCrlWaitMs = 5000;
const int kCrlNotifyWaitMs = 30000;
//合成画面的尺寸、每帧间隔（约 15 fps）、颜色切换的周期（毫秒）
const int kCrlW = 640;
const int kCrlH = 360;
const UINT kCrlFrameIntervalMs = 66;
const DWORD kCrlColorPeriodMs = 500;
const int kCrlBpp = 4;
//云端录制期间推流的时长（毫秒）
const DWORD kCrlRecordMs = 6000;
//轮询等待的间隔与上限（毫秒）
const DWORD kCrlPollMs = 50;
const DWORD kCrlConnectWaitMs = 8000;
//离线合成的最长等待（毫秒）
const DWORD kCrlComposeWaitMs = 120000;
//传输的超时（秒）
const int kCrlIoTimeoutSec = 10;
//合成任务回报用的窗口私有消息（只在本测试的消息窗口里用）
const UINT kCrlMsgNames = WM_APP + 1;
const UINT kCrlMsgDone = WM_APP + 2;
//CRLIVE2 的测试文件大小（字节）：大于两片（512 KiB 一片），上传一片多之后取消
const int64_t kCrlUploadBytes = 3 * 512 * 1024 + 12345;
const int64_t kCrlCancelAfterBytes = 512 * 1024;
//MP4 文件头里「ftyp」盒子名的偏移
const size_t kMp4FtypOffset = 4;
//列表每页的条数（服务端上限）
const int kCrlListPage = 50;
//本组用例建出的录像标题都以它开头，开头清理遗留录像时按它识别
const char* kCrlTitlePrefix = "CRLIVE";

//每 kCrlColorPeriodMs 换一种颜色的合成画面，录下来的帧之间有变化
class CycleCapturer : public IShareCapturer
{
public:
    CycleCapturer()
        : m_pixels(static_cast<size_t>(kCrlW) * kCrlH * kCrlBpp)
        , m_start(::GetTickCount())
    {
    }

    virtual bool Start()
    {
        return true;
    }

    virtual int Capture(UINT timeoutMs, ShareCaptureFrame& out)
    {
        ::Sleep(timeoutMs < kCrlFrameIntervalMs ? timeoutMs : kCrlFrameIntervalMs);
        static const uint8_t kColors[3][3] = { { 0, 0, 255 }, { 0, 200, 0 }, { 220, 80, 0 } };
        const int idx = static_cast<int>(((::GetTickCount() - m_start) / kCrlColorPeriodMs) % 3);
        for (size_t i = 0; i < m_pixels.size(); i += kCrlBpp)
        {
            m_pixels[i] = kColors[idx][0];
            m_pixels[i + 1] = kColors[idx][1];
            m_pixels[i + 2] = kColors[idx][2];
            m_pixels[i + 3] = 255;
        }
        out.m_data = &m_pixels[0];
        out.m_stride = kCrlW * kCrlBpp;
        out.m_width = kCrlW;
        out.m_height = kCrlH;
        return share_capture_ok;
    }

    virtual void Stop()
    {
    }

    virtual bool IsMinimized() const
    {
        return false;
    }

    virtual RECT SourceRect() const
    {
        RECT rc = { 0, 0, kCrlW, kCrlH };
        return rc;
    }

private:
    std::vector<uint8_t> m_pixels;   //合成画面
    DWORD                m_start;    //开始的时刻
};

//推流管线的事件
class CrlHostEvents : public ShareHostStreamerListener
{
public:
    CrlHostEvents()
        : m_connected(false)
        , m_failed(false)
    {
    }

    virtual void OnHostStreamEvent(int event, int value)
    {
        (void)value;
        if (event == share_host_relay_connected)
            m_connected.store(true);
        if (event == share_host_relay_failed || event == share_host_capture_failed)
            m_failed.store(true);
    }

    std::atomic<bool> m_connected;   //已连上中转
    std::atomic<bool> m_failed;      //连接或采集失败
};

//收流管线的事件（只计帧数）
class CrlViewerEvents : public ShareViewerSink
{
public:
    CrlViewerEvents()
        : m_frames(0)
    {
    }

    virtual void OnViewerFrame()
    {
        m_frames.fetch_add(1);
    }

    virtual void OnViewerEvent(int event, int value)
    {
        (void)event;
        (void)value;
    }

    std::atomic<int> m_frames;   //收到的画面通知次数
};

//到 imgserver 图片端口的文件帧通道：连接、三帧握手、按帧收发（与产品代码 net::FileServerConn 的收发相同，
//只是服务端公钥由测试给出，不经 CIUSocket）
class LiveFileChannel : public net::IFileFrameChannel
{
public:
    bool Connect(unsigned short port, std::string& reason)
    {
        if (!m_conn.Connect(port))
        {
            reason = "connect imgserver failed";
            return false;
        }
        unsigned char pubKey[net::IDENTITY_PUBLICKEY_BYTES] = { 0 };
        if (!net::parseServerPublicKey(kTestServerPubKey, pubKey))
        {
            reason = "bad server public key";
            return false;
        }
        return net::performClientHandshake(m_conn, pubKey, m_hs, reason);
    }

    virtual bool SendBytes(const char* data, int64_t size, int timeoutSec)
    {
        (void)timeoutSec;
        return m_conn.SendAll(data, static_cast<size_t>(size));
    }

    virtual bool RecvFrame(std::string& body, int timeoutSec)
    {
        (void)timeoutSec;
        frame_header header;
        std::string wire;
        if (!m_conn.RecvFrame(header, wire, MAX_BODY_FILESERVER))
            return false;
        std::string err;
        return net::decodeFramePayload(header.cmd, header.seq, header.flags, header.originlen, wire, &m_hs.cipher(), body,
                                       err);
    }

    virtual net::FrameCipher* GetCipher()
    {
        return &m_hs.cipher();
    }

private:
    TestConn            m_conn;   //到 imgserver 的连接
    net::ClientHandshake m_hs;    //本连接的握手器与会话密钥
};

//上传进度：记下第一次与最后一次回报的字节数；cancelAfter 大于 0 时超过它就置取消标志
class CrlUploadSink : public net::IUploadProgressSink
{
public:
    CrlUploadSink(std::atomic<bool>* cancel, int64_t cancelAfter)
        : m_cancel(cancel)
        , m_cancelAfter(cancelAfter)
        , m_first(-1)
        , m_last(0)
    {
    }

    virtual void onProgress(int64_t sent, int64_t total)
    {
        (void)total;
        if (m_first < 0)
            m_first = sent;
        m_last = sent;
        if (m_cancel != NULL && m_cancelAfter > 0 && sent >= m_cancelAfter)
            m_cancel->store(true);
    }

    std::atomic<bool>* m_cancel;       //取消标志（不拥有）
    int64_t            m_cancelAfter;  //传到这么多字节后取消；0 表示不取消
    int64_t            m_first;        //第一次回报的字节数；没有回报时为 -1
    int64_t            m_last;         //最后一次回报的字节数
};

unsigned short crlChatPort()
{
    char buf[16] = { 0 };
    if (::GetEnvironmentVariableA("FLAMINGO_LIVE_CHATPORT", buf, sizeof(buf)) > 0)
        return static_cast<unsigned short>(atoi(buf));
    return kCrlChatPort;
}

std::string crlPassword()
{
    char buf[64] = { 0 };
    if (::GetEnvironmentVariableA("FLAMINGO_LIVE_PASSWORD", buf, sizeof(buf)) > 0)
        return std::string(buf);
    return kCrlPassword;
}

void crlLogin(LiveChatClient& c, const char* account)
{
    std::string reason;
    const bool ok = c.ConnectAndLogin(crlChatPort(), kTestServerPubKey, account, crlPassword(), reason);
    INFO("login " << account << " failed: " << reason);
    REQUIRE(ok);
}

std::string crlRequest(LiveChatClient& c, uint16_t cmd, const std::string& body)
{
    REQUIRE(c.Send(cmd, body));
    std::string resp;
    REQUIRE(c.WaitFor(cmd, resp, kCrlWaitMs));
    return resp;
}

CloudRecordOpResult crlOp(LiveChatClient& c, const std::string& body)
{
    CloudRecordOpResult r;
    const std::string resp = crlRequest(c, cmd_cloudrecord_op, body);
    INFO("op response: " << resp);
    REQUIRE(ParseCloudRecordOpResult(resp, r));
    return r;
}

//等 1757 推送里某条录像到达某个状态
bool crlWaitStatus(LiveChatClient& c, int64_t recordId, int status, CloudRecordInfo* out)
{
    const DWORD start = ::GetTickCount();
    while (::GetTickCount() - start < static_cast<DWORD>(kCrlNotifyWaitMs))
    {
        std::string body;
        if (!c.WaitFor(cmd_cloudrecord_notify, body, kCrlWaitMs))
            continue;
        CloudRecordNotify n;
        if (!ParseCloudRecordNotify(body, n) || !n.m_hasRecord)
            continue;
        if ((recordId == 0 || n.m_recordId == recordId) && n.m_record.m_status == status)
        {
            if (out != NULL)
                *out = n.m_record;
            return true;
        }
    }
    return false;
}

//在列表第一页里找一条录像
bool crlFindInList(LiveChatClient& c, int64_t recordId, CloudRecordInfo& out)
{
    CloudRecordListResult list;
    REQUIRE(ParseCloudRecordList(crlRequest(c, cmd_cloudrecord_list, BuildCloudRecordList(0, kCrlListPage)),
                                 list));
    REQUIRE(list.m_code == 0);
    for (size_t i = 0; i < list.m_records.size(); ++i)
    {
        if (list.m_records[i].m_recordId == recordId)
        {
            out = list.m_records[i];
            return true;
        }
    }
    return false;
}

//删掉以前运行留下的录像（标题以 kCrlTitlePrefix 开头）：用例中途失败时，录像会停在「上传中」或「等待合成」
void crlDeleteLeftovers(LiveChatClient& c)
{
    CloudRecordListResult list;
    REQUIRE(ParseCloudRecordList(crlRequest(c, cmd_cloudrecord_list, BuildCloudRecordList(0, kCrlListPage)), list));
    REQUIRE(list.m_code == 0);
    const size_t prefixLen = strlen(kCrlTitlePrefix);
    for (size_t i = 0; i < list.m_records.size(); ++i)
    {
        const CloudRecordInfo& r = list.m_records[i];
        if (r.m_title.compare(0, prefixLen, kCrlTitlePrefix) != 0)
            continue;
        const CloudRecordOpResult del = crlOp(c, BuildCloudRecordDelete(r.m_recordId));
        INFO("delete leftover record " << r.m_recordId);
        CHECK(del.m_code == 0);
    }
}

std::wstring crlTempDir()
{
    wchar_t base[MAX_PATH] = { 0 };
    ::GetTempPathW(MAX_PATH, base);
    const std::wstring dir = std::wstring(base) + L"flamingo_cloudrec_live";
    ::CreateDirectoryW(dir.c_str(), NULL);
    return dir;
}

bool crlReadFile(const std::wstring& path, std::string& out)
{
    out.clear();
    FILE* fp = NULL;
    if (_wfopen_s(&fp, path.c_str(), L"rb") != 0 || fp == NULL)
        return false;
    char buf[65536];
    size_t n = 0;
    while ((n = fread(buf, 1, sizeof(buf), fp)) > 0)
        out.append(buf, n);
    fclose(fp);
    return true;
}

//经 imgserver 下载令牌指向的文件
bool crlDownload(const CloudRecordOpResult& r, const std::wstring& dest, std::string& reason)
{
    LiveFileChannel ch;
    if (!ch.Connect(static_cast<unsigned short>(r.m_port), reason))
        return false;
    std::wstring local;
    int errcode = 0;
    return net::FileStreamTransfer::downloadFile(ch, r.m_token, std::string(), r.m_bytes, dest, NULL, NULL, local, errcode,
                                                 reason);
}

//经 imgserver 上传文件
bool crlUpload(const CloudRecordOpResult& r, const std::wstring& path, int64_t bytes, CrlUploadSink* sink,
               std::atomic<bool>* cancel, std::string& reason)
{
    LiveFileChannel ch;
    if (!ch.Connect(static_cast<unsigned short>(r.m_port), reason))
        return false;
    int errcode = 0;
    return net::FileStreamTransfer::uploadFile(ch, path, bytes, 0, r.m_token, sink, cancel, errcode, reason);
}

//离线合成：在本线程建消息窗口收合成任务的回报，查名字时给出 A 的名字
int crlCompose(int64_t recordId, const std::wstring& frecPath, const std::wstring& mp4Path)
{
    HWND wnd = ::CreateWindowExW(0, L"STATIC", L"", 0, 0, 0, 0, 0, HWND_MESSAGE, NULL, ::GetModuleHandleW(NULL), NULL);
    REQUIRE(wnd != NULL);
    CCloudRecordComposeTask task(recordId, frecPath, mp4Path, wnd, kCrlMsgNames, kCrlMsgDone, L"Microsoft YaHei UI");
    task.Start();
    int result = 1;
    bool done = false;
    const DWORD start = ::GetTickCount();
    while (!done && ::GetTickCount() - start < kCrlComposeWaitMs)
    {
        MSG msg;
        while (::PeekMessageW(&msg, wnd, 0, 0, PM_REMOVE))
        {
            if (msg.message == kCrlMsgNames)
            {
                std::map<int64_t, std::wstring> names;
                const std::vector<int64_t>& ids = task.UserIds();
                for (size_t i = 0; i < ids.size(); ++i)
                    names[ids[i]] = L"HC0291";
                task.SetNames(names);
            }
            else if (msg.message == kCrlMsgDone)
            {
                result = static_cast<int>(msg.lParam);
                done = true;
            }
        }
        ::Sleep(kCrlPollMs);
    }
    if (!done)
        task.Cancel();
    task.Join();
    ::DestroyWindow(wnd);
    return done ? result : -2;
}

//A 发起与 B 的单聊共享并推流，B 加入并收流；A 开始云端录制，推 kCrlRecordMs 后停止并结束共享；
//等到「等待合成」推送，并确认列表里也是这个状态。返回推送里的录像信息
CloudRecordInfo crlRecordUntilPending(LiveChatClient& a, LiveChatClient& b, const std::string& shareTitle)
{
    unsigned char serverPubKey[net::IDENTITY_PUBLICKEY_BYTES] = { 0 };
    REQUIRE(net::parseServerPublicKey(kTestServerPubKey, serverPubKey));

    //上一次没收尾的共享先结束
    ShareQueryResult q;
    REQUIRE(ShareProtocolClient::parseQueryResult(
        crlRequest(a, cmd_screenshare_query, ShareProtocolClient::buildQuery(conversation_single, kCrlUserB)), q));
    if (q.m_exists)
        crlRequest(a, cmd_screenshare_stop, ShareProtocolClient::buildRoomRequest(q.m_roomId));
    a.ClearBacklog();
    b.ClearBacklog();

    //A 发起单聊共享并推流，B 加入并收流（第 15 期起共享标题必填）
    ShareResult started;
    REQUIRE(ShareProtocolClient::parseResult(
        crlRequest(a, cmd_screenshare_start,
                   ShareProtocolClient::buildStart(conversation_single, kCrlUserB, false, false, false, std::string(), false,
                                                   true, shareTitle)),
        started));
    REQUIRE(started.m_code == 0);
    std::string relayBody;
    REQUIRE(a.WaitFor(cmd_screenshare_relay_info, relayBody, kCrlWaitMs));
    ShareRelayInfo hostRelay;
    REQUIRE(ShareProtocolClient::parseRelayInfo(relayBody, hostRelay));
    CrlHostEvents hostEvents;
    ShareHostStreamer streamer;
    ShareSourceSpec spec;
    REQUIRE(streamer.Start(hostRelay, serverPubKey, spec, new CycleCapturer(), &hostEvents));
    const DWORD connStart = ::GetTickCount();
    while (!hostEvents.m_connected.load() && !hostEvents.m_failed.load() && ::GetTickCount() - connStart < kCrlConnectWaitMs)
        ::Sleep(kCrlPollMs);
    REQUIRE(hostEvents.m_connected.load());
    ShareResult joined;
    REQUIRE(ShareProtocolClient::parseResult(
        crlRequest(b, cmd_screenshare_join, ShareProtocolClient::buildRoomRequest(started.m_roomId)), joined));
    REQUIRE(joined.m_code == 0);
    REQUIRE(b.WaitFor(cmd_screenshare_relay_info, relayBody, kCrlWaitMs));
    ShareRelayInfo viewerRelay;
    REQUIRE(ShareProtocolClient::parseRelayInfo(relayBody, viewerRelay));
    CrlViewerEvents viewerEvents;
    ShareViewerReceiver receiver;
    REQUIRE(receiver.Start(viewerRelay, serverPubKey, &viewerEvents));

    //开始云端录制，推几秒后停止
    ShareCloudRecordResult rec;
    const std::string startResp = crlRequest(
        a, cmd_cloudrecord_control, ShareProtocolClient::buildCloudRecordControl(started.m_roomId, cloudrecord_control_start));
    INFO("cloud record start: " << startResp);
    REQUIRE(ShareProtocolClient::parseCloudRecordResult(startResp, rec));
    REQUIRE(rec.m_code == 0);
    REQUIRE(rec.m_recordId > 0);
    //成功应答的 msg 为 "ok"（2026-10-07 之前服务端在这里回的是 "failed"）
    CHECK(startResp.find("\"msg\":\"ok\"") != std::string::npos);
    const int64_t recordId = rec.m_recordId;
    ::Sleep(kCrlRecordMs);
    CHECK(viewerEvents.m_frames.load() > 0);
    ShareCloudRecordResult stopped;
    REQUIRE(ShareProtocolClient::parseCloudRecordResult(
        crlRequest(a, cmd_cloudrecord_control,
                   ShareProtocolClient::buildCloudRecordControl(started.m_roomId, cloudrecord_control_stop)),
        stopped));
    CHECK(stopped.m_code == 0);
    receiver.Stop();
    streamer.Stop();
    crlRequest(a, cmd_screenshare_stop, ShareProtocolClient::buildRoomRequest(started.m_roomId));

    //等待合成：推送与列表
    CloudRecordInfo pending;
    REQUIRE(crlWaitStatus(a, recordId, cloudrecord_status_pending, &pending));
    CHECK(pending.m_frecBytes > 0);
    CHECK(pending.m_durationMs > 0);
    CloudRecordInfo listed;
    REQUIRE(crlFindInList(a, recordId, listed));
    CHECK(listed.m_status == cloudrecord_status_pending);
    //推送里的录像 id 另放在 CloudRecordNotify::m_recordId，这里统一填回
    pending.m_recordId = recordId;
    return pending;
}

} // namespace

TEST_CASE("CRLIVE1 共享中的云端录制：录制、等待合成、下载原始帧、离线合成、上传、可观看、发到会话、对方下载、重命名、删除",
          "[.live][cloudreclive]")
{
    LiveChatClient a;
    LiveChatClient b;
    crlLogin(a, kCrlAccountA);
    crlDeleteLeftovers(a);
    crlLogin(b, kCrlAccountB);

    //共享、云端录制，等到「等待合成」
    const CloudRecordInfo pending = crlRecordUntilPending(a, b, "CRLIVE1 share");
    const int64_t recordId = pending.m_recordId;
    CloudRecordInfo listed;

    //下载原始帧并离线合成
    const std::wstring dir = crlTempDir();
    const std::wstring frecPath = dir + L"\\crlive1.frec";
    const std::wstring mp4Path = dir + L"\\crlive1.mp4";
    ::DeleteFileW(frecPath.c_str());
    ::DeleteFileW(mp4Path.c_str());
    const CloudRecordOpResult frecTok = crlOp(a, BuildCloudRecordDownloadToken(recordId, kCloudFileFrec));
    REQUIRE(frecTok.m_code == 0);
    std::string reason;
    {
        INFO("download frec: " << reason);
        REQUIRE(crlDownload(frecTok, frecPath, reason));
    }
    std::string frec;
    REQUIRE(crlReadFile(frecPath, frec));
    CHECK(static_cast<int64_t>(frec.size()) == pending.m_frecBytes);
    REQUIRE(crlCompose(recordId, frecPath, mp4Path) == kCloudComposeOk);
    std::string mp4;
    REQUIRE(crlReadFile(mp4Path, mp4));
    REQUIRE(mp4.size() > kMp4FtypOffset + 4);
    CHECK(mp4.compare(kMp4FtypOffset, 4, "ftyp") == 0);

    //上传合成结果，等可观看
    const CloudRecordOpResult upTok =
        crlOp(a, BuildCloudRecordUploadToken(recordId, static_cast<int64_t>(mp4.size()), false, std::string(), 0, 0));
    REQUIRE(upTok.m_code == 0);
    {
        INFO("upload mp4: " << reason);
        REQUIRE(crlUpload(upTok, mp4Path, static_cast<int64_t>(mp4.size()), NULL, NULL, reason));
    }
    CloudRecordInfo ready;
    REQUIRE(crlWaitStatus(a, recordId, cloudrecord_status_ready, &ready));
    CHECK(ready.m_bytes == static_cast<int64_t>(mp4.size()));

    //发到会话：B 收到云端录像卡片，下载最终文件与 A 上传的逐字节相同
    b.ClearBacklog();
    const CloudRecordOpResult sent = crlOp(a, BuildCloudRecordSend(recordId, conversation_single, kCrlUserB));
    REQUIRE(sent.m_code == 0);
    bool gotCard = false;
    for (int i = 0; i < 5 && !gotCard; ++i)
    {
        std::string body;
        if (!b.WaitFor(cmd_message_notify, body, kCrlWaitMs))
            break;
        gotCard = body.find("\"msgtype\":24") != std::string::npos &&
                  body.find(std::to_string(static_cast<long long>(recordId))) != std::string::npos;
    }
    CHECK(gotCard);
    const CloudRecordOpResult finalTok = crlOp(b, BuildCloudRecordDownloadToken(recordId, kCloudFileFinal));
    REQUIRE(finalTok.m_code == 0);
    const std::wstring finalPath = dir + L"\\crlive1_final.mp4";
    ::DeleteFileW(finalPath.c_str());
    {
        INFO("download final: " << reason);
        REQUIRE(crlDownload(finalTok, finalPath, reason));
    }
    std::string finalBytes;
    REQUIRE(crlReadFile(finalPath, finalBytes));
    CHECK(finalBytes == mp4);

    //重命名、删除
    const CloudRecordOpResult renamed = crlOp(a, BuildCloudRecordRename(recordId, "CRLIVE1 renamed"));
    CHECK(renamed.m_code == 0);
    REQUIRE(crlFindInList(a, recordId, listed));
    CHECK(listed.m_title == "CRLIVE1 renamed");
    const CloudRecordOpResult deleted = crlOp(a, BuildCloudRecordDelete(recordId));
    CHECK(deleted.m_code == 0);
    const CloudRecordOpResult gone = crlOp(b, BuildCloudRecordDownloadToken(recordId, kCloudFileFinal));
    CHECK(gone.m_code == error_code_cloudrecord_not_found);
}

TEST_CASE("CRLIVE2 保存到云端：新建上传录像、传到一半取消、凭录像 id 续传、可观看、下载比对、删除", "[.live][cloudreclive]")
{
    LiveChatClient a;
    crlLogin(a, kCrlAccountA);
    crlDeleteLeftovers(a);
    a.ClearBacklog();

    //本机文件：内容按位置写出，便于比对
    const std::wstring dir = crlTempDir();
    const std::wstring path = dir + L"\\crlive2.mp4";
    std::string content(static_cast<size_t>(kCrlUploadBytes), '\0');
    for (size_t i = 0; i < content.size(); ++i)
        content[i] = static_cast<char>((i * 131 + 7) & 0xFF);
    {
        FILE* fp = NULL;
        REQUIRE(_wfopen_s(&fp, path.c_str(), L"wb") == 0);
        REQUIRE(fp != NULL);
        fwrite(content.data(), 1, content.size(), fp);
        fclose(fp);
    }
    const int64_t kDurationMs = 10000;
    const int64_t kStartTime = 1791300000000LL;

    //新建上传录像，传一片多之后取消
    const CloudRecordOpResult created =
        crlOp(a, BuildCloudRecordUploadToken(0, kCrlUploadBytes, false, "CRLIVE2 upload", kDurationMs, kStartTime));
    REQUIRE(created.m_code == 0);
    REQUIRE(created.m_recordId > 0);
    const int64_t recordId = created.m_recordId;
    std::atomic<bool> cancel(false);
    CrlUploadSink firstSink(&cancel, kCrlCancelAfterBytes);
    std::string reason;
    CHECK_FALSE(crlUpload(created, path, kCrlUploadBytes, &firstSink, &cancel, reason));
    CHECK(cancel.load());
    CHECK(firstSink.m_last >= kCrlCancelAfterBytes);
    CHECK(firstSink.m_last < kCrlUploadBytes);
    CloudRecordInfo listed;
    REQUIRE(crlFindInList(a, recordId, listed));
    CHECK(listed.m_status == cloudrecord_status_uploading);

    //续传：凭录像 id 再取令牌，服务端从已收的位置接着收
    const CloudRecordOpResult resumed =
        crlOp(a, BuildCloudRecordUploadToken(recordId, kCrlUploadBytes, false, "CRLIVE2 upload", kDurationMs, kStartTime));
    REQUIRE(resumed.m_code == 0);
    CHECK(resumed.m_recordId == recordId);
    CrlUploadSink secondSink(NULL, 0);
    {
        INFO("resume upload: " << reason);
        REQUIRE(crlUpload(resumed, path, kCrlUploadBytes, &secondSink, NULL, reason));
    }
    CHECK(secondSink.m_first >= firstSink.m_last);
    CHECK(secondSink.m_last == kCrlUploadBytes);
    CloudRecordInfo ready;
    REQUIRE(crlWaitStatus(a, recordId, cloudrecord_status_ready, &ready));
    CHECK(ready.m_bytes == kCrlUploadBytes);

    //下载回来逐字节相同，然后删除
    const CloudRecordOpResult tok = crlOp(a, BuildCloudRecordDownloadToken(recordId, kCloudFileFinal));
    REQUIRE(tok.m_code == 0);
    const std::wstring back = dir + L"\\crlive2_back.mp4";
    ::DeleteFileW(back.c_str());
    {
        INFO("download: " << reason);
        REQUIRE(crlDownload(tok, back, reason));
    }
    std::string backBytes;
    REQUIRE(crlReadFile(back, backBytes));
    CHECK(backBytes == content);
    CHECK(crlOp(a, BuildCloudRecordDelete(recordId)).m_code == 0);
}

TEST_CASE("CRLIVE3 只录到「等待合成」为止，留给 A 的真实客户端登录后合成与上传", "[.live][cloudrecpending]")
{
    LiveChatClient a;
    LiveChatClient b;
    crlLogin(a, kCrlAccountA);
    crlDeleteLeftovers(a);
    crlLogin(b, kCrlAccountB);
    const CloudRecordInfo pending = crlRecordUntilPending(a, b, "CRLIVE3 share");
    REQUIRE(pending.m_recordId > 0);
    //录像 id 打到输出里，供后续按 id 查服务端数据库与客户端日志
    WARN("pending cloud record id: " << pending.m_recordId);
}
