/**
 *  单聊文件夹传输走 P2P 的端到端联调用例（Suite FP2PL，标签 [.live][folderp2plive]），FolderP2PLiveTests.cpp
 *
 *  docs/P2P通信方案.md 第 5 期 5d：两个账号用 LiveChatClient 登录隔离的测试服务端，各挂一个真实的 P2PEngine 与一个
 *  文件夹传输的业务方（FolderP2PLinkBroker，单测里每端各建一个）。与 FP2P4 不同，这里的 P2P 会话不是测试代码模拟的，
 *  而是走真实的信令：A 发 1810 邀请 B，B 收到 1811 后发 1812 接受，chatserver 给两端各推一帧 1816，两端都报到过
 *  P2P 能力时 1816 带同一个 p2psessionid，随后推 2302 建 P2P 会话（用途 folder），2303 / 2304 经 chatserver 真实转发。
 *  两端按 1816 建通道工厂（做法与 FolderTransferManager 相同），用真实的 FolderSendSession / FolderRecvSession 传一个
 *  含子目录、空文件与大文件的文件夹，落地文件逐个与源文件比对；最后两端各发 1814 结束。
 *    - FP2PL1 局域网直连：1816 带同一个非空的 p2psessionid、角色相反；两端都决定走 P2P，链路路径为局域网直连；
 *    - FP2PL2 强制中转：两端引擎都不打洞，链路经 imgserver 的 P2P 中转建成，路径为中转；文件夹照样传完；
 *    - FP2PL3 失败后重试续传：第一次传到一部分时两端停掉会话并以 616 失败结束；A 发 1817 重试，服务端重新推 1816，
 *             带一个新的 p2psessionid；两端用同一个功能会话号重新建通道工厂（替换上一轮的链路槽），接收端沿用第一次的
 *             接收状态；第二次的续传起点大于 0（不是从头再传），两端都走 P2P，文件夹传完；
 *    - FP2PL4 对方是旧客户端：B 不报到 P2P 能力（不启动引擎），1816 不带 p2psessionid，两端都不建通道工厂，
 *             文件夹经 fileserver 中转传完；
 *    - FP2PL5 对方关掉了文件夹 P2P（FLAMINGO_FOLDER_P2P=0 时的样子）：B 的引擎照常报到，但 2301 里没有用途 folder，
 *             服务端因此不建 P2P 会话（908），1816 不带 p2psessionid，两端都走 fileserver 中转传完。这条用例最初按
 *             「1816 带 p2psessionid、B 关掉链路、A 退回中转」编写，实测 A 先看到链路就绪选了 P2P、B 走了中转，两端
 *             对不上而失败，于是改为由 2301 报到用途、服务端两端都支持才建会话；
 *    - FP2PL6 文件共享（P2P 方案第 6 期）：A 在与 B 的单聊里发起文件夹共享（2201）并报告可提供（2210），B 打开（2220）、
 *             申请下载（2224）；两帧 2222 都带同一个非空的 p2psessionid，两端收到用途为 fileshare、功能会话号为 transferid
 *             的 2302；两端按 transferid 从各自的 fileshare 业务方建通道工厂，A 发、B 收，经局域网直连的 P2P 链路传完，
 *             落地文件与源文件一致；最后 B 发 2225 结束传输、A 发 2202 取消共享。
 *    - FP2PL7 接收端晚报到、清单被中转丢弃的问题在真实 fileserver 上的复现（2026-10-05）：同 FP2PL4 走中转，但发送端先启动、接收端晚 1.5 秒
 *             才启动。发送端第一次发出的清单被 fileserver 丢弃（日志里有 folder relay data dropped），靠回执到达前
 *             的重发送达，文件夹照常传完、落地文件与源文件一致；
 *  远程桌面（P2P 方案第 6 期，标签 [.live][rdp2plive]）：A 作控制端发 1710 邀请 B，B 收到 1711 后发 1712 接受，两端各收到
 *  一帧 1716，带同一个非空的 p2psessionid、角色相反；两端按 1716 认领用途 rd 的链路槽，各在自己的线程上用 RdTransport
 *  连接（与推流线程、观看窗的连接线程相同）：
 *    - RDL1 局域网直连：两端同时连接，都走 P2P，链路路径为局域网直连；被控端 B 发一帧 4 MiB 的关键帧与一帧小的增量帧，
 *           控制端 A 按序收到；A 发请求关键帧与一条输入命令，B 按序收到；两端关闭传输后 A 发 1714 结束会话；
 *    - RDL2 强制中转、被控端先发：两端引擎都不打洞，链路经 imgserver 的 P2P 中转建成；B 先连上并立即发出关键帧与增量帧，
 *           A 过 500 毫秒才打开通道；中转路径上的可靠通道没有重传，这两帧要靠 DLL 复用层的暂存与补交（P2PCH8）才能
 *           完整、按序到达 A；
 *    - RDL3 按真实节奏连续推流（标签 [.live][rdp2pstream]）：局域网直连，B 在自己的线程上按推流器的节奏推流 12 秒
 *           （每 200 毫秒一帧，每 10 帧一次「BITMAPINFO + 4 MiB 关键帧」，其余是 200 KB 的增量帧），A 按序收齐每一帧。
 *           2026-10-04 界面联调中远程桌面经 P2P 连上后控制端一直拼不出首帧，RDL1 只发一帧关键帧测不到，补这一条复现。
 *  一对一通话（P2P 方案第 6 期，标签 [.live][callp2plive]）：A 发 1701 呼叫 B，邀请应答与来电推送带同一个非空的
 *  p2psessionid（服务端在响铃阶段就建 P2P 会话，用途 call）；两端按通话会话号认领链路槽：
 *    - CL1 局域网直连：B 接听，两端在通道 1 上打开数据报通道，A 发 50 个通话格式的音频消息与一帧 300 KB 的视频关键帧，
 *          B 全部按序收到、解析出的序号连续；B 发关键帧请求，A 收到；两端关闭后 A 发 1705 挂断；
 *    - CL2 强制中转：同 CL1，但两端引擎都不打洞，数据报经 imgserver 的 P2P 中转；
 *    - CL3 被叫拒绝：B 收到来电后放弃这个会话号（链路还没建好也一样）并发 1703 拒绝，A 收到「对方拒绝」；A 认领的
 *          链路槽随后变为断开或失败，不会一直挂着。
 *  单聊屏幕共享（P2P 方案第 6 期，标签 [.live][sharep2plive]）：A 发起与 B 的单聊共享（1720），用合成画面（640×360 纯红）
 *  经产品代码 ShareHostStreamer 推流；B 加入（1722），1726 与 A 收到的 1725 快照里 B 一项带同一个 p2psessionid；A 按快照
 *  把链路槽交给推流管线，B 带着链路槽启动 ShareViewerReceiver：
 *    - SSPL1 局域网直连：B 经 P2P 帧通道收到画面（尺寸与颜色正确），双方都走 P2P；B 发一帧标注（帧头座位号故意写错），
 *            A 收到时座位号已改写为 B 的座位；A 暂停与继续，B 收到状态变化；
 *    - SSPL2 强制中转：同 SSPL1，链路经 imgserver 的 P2P 中转；
 *    - SSPL3 P2P 中途断开：A 撤下 B 的 P2P 通道（关闭链路），B 自动改连房间中转，继续收到画面，A 回到经中转发送。
 *
 *  单聊单文件 P2P 直传（2026-10-04，docs/单聊文件P2P直传与P2P徽章方案.md，标签 [.live][directfilelive]）：A 发 1810（kind=1），
 *  B 收到的 1811 带 kind=1；双方都报到了用途 file 时服务端建用途 file 的 P2P 会话，数据只走 P2P 链路：
 *    - DFL1 局域网直连：B 接收，文件落在 direct-<会话号> 目录、内容一致；两端扩展点报出的 MD5 与文件内容一致，两端 1814
 *           带链路路径与各自的 MD5，双方各收到一条 msgtype 3、direct=1 的文件消息，结局 8、链路路径 1、MD5 与文件名一致；
 *    - DFL2 强制中转：同 DFL1，链路经 imgserver 中转（路径 3），文件消息里的链路路径也是 3；
 *    - DFL3 B 是不支持直传的旧版客户端（不报到用途 file）：1810 回 622，不建会话，B 收不到邀请；
 *    - DFL4 A 改为离线发送（1813 带 tooffline）：B 收到状态 9，双方都不留文件消息；
 *    - DFL5 B 拒绝：A 收到状态 3，双方各收到一条结局 3 的文件消息。
 *
 *  默认不参与常规测试：用例带 [.live] 标签。跑之前用 tmp/p2ptest/setup_p2ptest.py 建环境、start_server.ps1 起测试服务端
 *  （服务端须带第 5 期 5a 的改动，即 1816 会带 p2psessionid）。
 *
 *      FlamingoUnit.exe "[folderp2plive]"
 *
 *  账号：MY0127（作 A，发送端）、MY0308（作 B，接收端），密码都是 perf1234，同属企业 1。
 *
 *  框架 Catch2 v3。本文件不使用预编译头：会话头文件带进 <winsock2.h>，必须先于 <windows.h>。
 *
 *  balloonwj@qq.com   2026-10-04
 */
#include "catch_amalgamated.hpp"

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "LiveChatClient.h"
#include "../Feature/FolderTransfer/FolderSendSession.h"
#include "../Feature/FolderTransfer/FolderRecvSession.h"
#include "../Feature/FolderTransfer/FolderManifest.h"
#include "../Feature/P2P/FolderP2PTransport.h"
#include "../Feature/FolderTransfer/DirectFileTransfer.h"   //单文件直传（DFL1～DFL5）
#include "../net/FolderProtocolClient.h"
#include "../third-party/jsoncpp-1.9.0/json.h"   //解析 1504 推送（DFL）
#include "../Common/Util/MD5Core.h"              //核对直传报出的 MD5（DFL）
#include "../../FlamingoFileShare/Engine/ShareProtocolCodec.h"
#include "../net/Protocol.h"
#include "../Common/Log/IULog.h"
#include "../../FlamingoP2P/Io/P2PEngine.h"
#include "../../FlamingoMedia/RemoteDesktop/RdTransport.h"
#include "../../FlamingoMedia/RemoteDesktop/RemoteDesktopProtocolClient.h"
#include "../../FlamingoMedia/Call/CallProtocolClient.h"
#include "../../FlamingoMedia/Call/CallP2PFraming.h"
#include "../../FlamingoMedia/Share/ShareHostStreamer.h"
#include "../../FlamingoMedia/Share/ShareViewerReceiver.h"
#include "../../FlamingoMedia/Share/ShareProtocolClient.h"
#include "../../FlamingoMedia/Share/ShareAnnotation.h"

using namespace livetest;
using namespace balloonp2p;
using namespace foldertransfer;

namespace {

//tmp/p2ptest 测试服务端的 chatserver 端口
const unsigned short kP2PTestServerPort = 29900;

//两个人的账号与密码
const char* const kAccountA = "MY0127";
const char* const kAccountB = "MY0308";
const char* const kPassword = "perf1234";

//用途 folder：与服务端 ChatSession::pushFolderRelayInfo 建会话时的 purpose 一致
const char* const kPurposeFolder = "folder";

//用途 call：与服务端 ChatSession::onCallInvite 建会话时的 purpose 一致（通话控制器登记的也是它）
const char* const kPurposeCall = "call";

//等待各类事件的最长时间（毫秒）：报到应答；信令应答与推送；整个文件夹传完
const int kWaitEnabledMs = 5000;
const int kWaitSignalMs = 5000;
const int kWaitTransferMs = 120000;
//FP2PL7：接收端晚启动的时长（毫秒），长于发送端第一次重发清单前的等待（1 秒），第一次重发同样会被丢弃
const int kLateReceiverDelayMs = 1500;

//收帧线程一次等待的时长（毫秒）
const int kRecvSliceMs = 200;

//轮询间隔（毫秒）
const int kPollMs = 10;

//可辨认数据的步长与模数
const size_t kPatternStep = 7;
const size_t kPatternModulo = 251;

//源文件夹里的文件大小（字节）：大文件跨 3 个分片（分片 512 KiB）；续传用例的大文件要足够大，传到一部分时才来得及停下
const size_t kBigFileBytes = 1300 * 1024;
const size_t kResumeBigFileBytes = 64 * 1024 * 1024;
const size_t kSmallFileBytes = 1000;

//续传用例：发送端报告已确认落盘这么多字节之后停掉第一次传输
const int64_t kResumeStopAfterBytes = 4 * 1024 * 1024;

//读文件的缓冲大小（字节）
const size_t kReadChunkBytes = 65536;

/**
 *  一端：已登录的连接 + 引擎 + 文件夹业务方 + 收帧线程。引擎发的命令经连接发给 chatserver（发送加锁：IO 线程与测试
 *  线程都会发）；收帧线程把 2300 段的帧交给引擎，其余帧放进待取队列，由测试线程按 cmd 取。
 */
class LiveFolderSide : public p2p::IP2PEngineHost
{
public:
    LiveFolderSide()
        : m_shareBroker(kFileShareP2PPurpose)
        , m_rdBroker(remotedesktop::kRdP2PPurpose)
        , m_callBroker(kPurposeCall)
        , m_shareBroker2(screenshare::kShareP2PPurpose)
        , m_fileBroker(foldertransfer::kDirectFileP2PPurpose)
        , m_engine(*this)
        , m_folderEnabled(false)
        , m_fileEnabled(true)
        , m_engineStarted(false)
        , m_stop(false)
    {
        memset(m_serverPubKey, 0, sizeof(m_serverPubKey));
    }

    ~LiveFolderSide()
    {
        Shutdown();
    }

    /**
     *  登录并启动收帧线程。
     *  @param account       账号
     *  @param withEngine    true 时启动引擎并通知已登录（发 2301，报到 P2P 能力）；false 模拟不支持 P2P 的旧客户端
     *  @param folderHandler 是否登记用途 folder 的业务方（2301 里是否报到 folder）；false 模拟 FLAMINGO_FOLDER_P2P=0
     */
    void Start(const char* account, bool withEngine, bool folderHandler)
    {
        m_folderEnabled = folderHandler;
        std::string reason;
        const bool ok = m_client.ConnectAndLogin(kP2PTestServerPort, kTestServerPubKey, account, kPassword, reason);
        INFO("login " << account << " failed: " << reason);
        REQUIRE(ok);
        REQUIRE(net::parseServerPublicKey(kTestServerPubKey, m_serverPubKey));
        if (withEngine)
        {
            REQUIRE(m_engine.Start());
            m_engineStarted = true;
        }
        m_recvThread = std::thread(&LiveFolderSide::RecvProc, this);
        if (withEngine)
        {
            m_engine.OnLoggedIn();
            REQUIRE(WaitEnabled(kWaitEnabledMs));
        }
    }

    //停止引擎（发 2305）、断开连接、等收帧线程结束。可重复调用
    void Shutdown()
    {
        m_engine.Stop();
        m_stop = true;
        m_client.Close();
        if (m_recvThread.joinable())
            m_recvThread.join();
    }

    //最多等 timeoutMs 毫秒，直到引擎报告已开启
    bool WaitEnabled(int timeoutMs) const
    {
        for (int waited = 0; waited <= timeoutMs; waited += kPollMs)
        {
            if (m_engine.IsEnabled())
                return true;
            std::this_thread::sleep_for(std::chrono::milliseconds(kPollMs));
        }
        return false;
    }

    //经本端的连接发一条信令
    void SendSignal(uint16_t cmd, const std::string& body)
    {
        std::lock_guard<std::mutex> guard(m_sendMutex);
        m_client.Send(cmd, body);
    }

    /**
     *  最多等 timeoutMs 毫秒，取出待取队列里第一帧 cmd 相符的帧（应答与推送都在这里）。
     *  @param cmd       要等的命令号
     *  @param body      [out] 取到的帧的 JSON 包体
     *  @param timeoutMs 最长等待时间（毫秒）
     *  @return 取到返回 true
     */
    bool WaitFrame(uint16_t cmd, std::string& body, int timeoutMs)
    {
        std::unique_lock<std::mutex> lock(m_frameMutex);
        const std::chrono::steady_clock::time_point deadline =
            std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
        for (;;)
        {
            for (std::deque<std::pair<uint16_t, std::string> >::iterator it = m_frames.begin(); it != m_frames.end(); ++it)
            {
                if (it->first == cmd)
                {
                    body = it->second;
                    m_frames.erase(it);
                    return true;
                }
            }
            if (m_frameCv.wait_until(lock, deadline) == std::cv_status::timeout)
                return false;
        }
    }

    //测试服务端的身份公钥（退回中转时用）
    const unsigned char* ServerPubKey() const
    {
        return m_serverPubKey;
    }

    //以下为 IP2PEngineHost 的实现

    //经本端的连接发一个 2300 段命令
    virtual void SendCommand(uint16_t cmd, const std::string& body) override
    {
        SendSignal(cmd, body);
    }

    //用途 folder 交给本端的文件夹业务方（业务方被关掉时返回 NULL，引擎随即关掉这条链路）；用途 fileshare 交给文件共享的业务方
    virtual p2p::IP2PLinkHandler* FindHandler(const std::string& purpose) override
    {
        if (purpose == kPurposeFolder)
            return m_folderEnabled ? &m_broker : NULL;
        if (purpose == kFileShareP2PPurpose)
            return &m_shareBroker;
        if (purpose == remotedesktop::kRdP2PPurpose)
            return &m_rdBroker;
        if (purpose == kPurposeCall)
            return &m_callBroker;
        if (purpose == screenshare::kShareP2PPurpose)
            return &m_shareBroker2;
        if (purpose == foldertransfer::kDirectFileP2PPurpose)
            return m_fileEnabled ? &m_fileBroker : NULL;
        return NULL;
    }

    //测试服务端的身份公钥：中转连接据此做协议加密握手
    virtual const unsigned char* GetServerPublicKey() override
    {
        return m_serverPubKey;
    }

    //报到的用途：fileshare 与 rd 总是报；登记了文件夹业务方时另报 folder
    virtual void ListPurposes(std::vector<std::string>& purposes) override
    {
        purposes.push_back(kFileShareP2PPurpose);
        purposes.push_back(remotedesktop::kRdP2PPurpose);
        purposes.push_back(kPurposeCall);
        purposes.push_back(screenshare::kShareP2PPurpose);
        if (m_folderEnabled)
            purposes.push_back(kPurposeFolder);
        if (m_fileEnabled)
            purposes.push_back(foldertransfer::kDirectFileP2PPurpose);
    }

    //是否登记用途 file（单文件直传）的业务方；在 Start 之前设置，false 模拟不支持直传的旧版客户端（DFL3）
    void SetFileEnabled(bool enabled)
    {
        m_fileEnabled = enabled;
    }

    LiveChatClient       m_client;    //已登录的连接
    FolderP2PLinkBroker  m_broker;    //用途 folder 的业务方（先于引擎构造，后于引擎析构）
    FolderP2PLinkBroker  m_shareBroker;   //用途 fileshare 的业务方（FP2PL6；同样先于引擎构造）
    P2PLinkBroker        m_rdBroker;      //用途 rd 的业务方（RDL1 / RDL2；同样先于引擎构造）
    P2PLinkBroker        m_callBroker;    //用途 call 的业务方（CL1～CL3；同样先于引擎构造）
    P2PLinkBroker        m_shareBroker2;  //用途 screenshare 的业务方（SSPL1～SSPL3；同样先于引擎构造）
    FolderP2PLinkBroker  m_fileBroker;    //用途 file 的业务方（单文件直传 DFL1～DFL5；同样先于引擎构造）
    p2p::P2PEngine       m_engine;    //被测引擎

private:
    //收帧线程：2300 段的帧交给引擎（引擎没启动时丢弃），其余帧放进待取队列
    void RecvProc()
    {
        while (!m_stop)
        {
            uint16_t cmd = 0;
            uint32_t seq = 0;
            std::string body;
            if (!m_client.WaitAny(cmd, seq, body, kRecvSliceMs))
                continue;
            if (cmd >= p2p::kP2PCmdFirst && cmd <= p2p::kP2PCmdLast)
            {
                if (m_engineStarted)
                    m_engine.OnServerFrame(cmd, seq, body);
                continue;
            }
            {
                std::lock_guard<std::mutex> guard(m_frameMutex);
                m_frames.push_back(std::make_pair(cmd, body));
            }
            m_frameCv.notify_all();
        }
    }

    unsigned char     m_serverPubKey[net::IDENTITY_PUBLICKEY_BYTES];   //测试服务端的身份公钥
    std::atomic<bool> m_folderEnabled;      //是否登记了用途 folder 的业务方（报到用途、交出链路都按它）
    std::atomic<bool> m_fileEnabled;        //是否登记了用途 file 的业务方（单文件直传；报到用途、交出链路都按它）
    std::atomic<bool> m_engineStarted;      //引擎是否已启动（模拟旧客户端时不启动）
    std::mutex        m_sendMutex;          //保护 m_client.Send：加密与写 socket 须在同一把锁内
    std::thread       m_recvThread;         //收帧线程
    std::atomic<bool> m_stop;               //是否要求收帧线程退出
    std::mutex                                      m_frameMutex;   //保护 m_frames
    std::condition_variable                         m_frameCv;      //有新帧放进 m_frames 时唤醒 WaitFrame
    std::deque<std::pair<uint16_t, std::string> >   m_frames;       //待取的非 2300 段帧：命令号与 JSON 包体
};

//发送端回调：记录完成、失败、第一次与最近一次进度
class LiveSendRecorder : public IFolderSendSessionHandler
{
public:
    LiveSendRecorder()
        : m_completed(0)
        , m_failed(0)
        , m_errcode(0)
        , m_progressCount(0)
        , m_firstSentBytes(-1)
        , m_lastSentBytes(0)
    {
    }

    //第一次进度回调发生在应用续传位点之后（FolderSendSession::WaitManifestAck），此时的已发送字节数即续传起点
    virtual void OnFolderSendProgress(const FolderSendProgress& progress) override
    {
        if (m_progressCount++ == 0)
            m_firstSentBytes = progress.m_sentBytes;
        m_lastSentBytes = progress.m_sentBytes;
    }

    virtual void OnFolderSendCompleted(int64_t totalFiles, int64_t totalBytes) override
    {
        (void)totalFiles;
        (void)totalBytes;
        ++m_completed;
    }

    virtual void OnFolderSendFailed(int errcode, const std::string& errmsg) override
    {
        (void)errmsg;
        m_errcode = errcode;
        ++m_failed;
    }

    std::atomic<int>     m_completed;        //完成次数
    std::atomic<int>     m_failed;           //失败次数
    std::atomic<int>     m_errcode;          //最近一次失败的错误码
    std::atomic<int>     m_progressCount;    //进度回调次数
    std::atomic<int64_t> m_firstSentBytes;   //第一次进度回调的已发送字节数；还没有回调时为 -1
    std::atomic<int64_t> m_lastSentBytes;    //最近一次进度回调的已发送字节数
};

//接收端回调：记录完成、失败与落地根目录
class LiveRecvRecorder : public IFolderRecvSessionHandler
{
public:
    LiveRecvRecorder()
        : m_completed(0)
        , m_failed(0)
        , m_errcode(0)
    {
    }

    virtual void OnFolderRecvProgress(const FolderRecvProgress& progress) override
    {
        (void)progress;
    }

    virtual void OnFolderRecvCompleted(const std::wstring& rootPath, int64_t totalFiles, int64_t totalBytes) override
    {
        (void)totalFiles;
        (void)totalBytes;
        {
            std::lock_guard<std::mutex> guard(m_mutex);
            m_rootPath = rootPath;
        }
        ++m_completed;
    }

    virtual void OnFolderRecvFailed(int errcode, const std::string& errmsg) override
    {
        (void)errmsg;
        m_errcode = errcode;
        ++m_failed;
    }

    //落地根目录
    std::wstring RootPath()
    {
        std::lock_guard<std::mutex> guard(m_mutex);
        return m_rootPath;
    }

    std::atomic<int> m_completed;   //完成次数
    std::atomic<int> m_failed;      //失败次数
    std::atomic<int> m_errcode;     //最近一次失败的错误码

private:
    std::mutex       m_mutex;       //保护 m_rootPath
    std::wstring     m_rootPath;    //落地根目录
};

//源文件夹：根目录下一个大文件、一个空文件，子目录里一个小文件；另附扫描结果与保存位置
struct LiveSourceFolder
{
    std::wstring     m_parent;      //源目录的上一级（日志也写在这里）
    std::wstring     m_root;        //源文件夹根目录
    std::wstring     m_dstParent;   //接收端的保存位置
    FolderScanResult m_scan;        //扫描结果（清单与规模统计）
};

//写一个文件：内容按偏移生成
void WriteLiveFile(const std::wstring& path, size_t bytes, int salt)
{
    FILE* fp = NULL;
    REQUIRE(_wfopen_s(&fp, path.c_str(), L"wb") == 0);
    std::string data(bytes, '\0');
    for (size_t i = 0; i < bytes; ++i)
        data[i] = (char)((i * kPatternStep + (size_t)salt) % kPatternModulo);
    if (bytes > 0)
        REQUIRE(fwrite(data.data(), 1, bytes, fp) == bytes);
    fclose(fp);
}

//读整个文件；打不开时返回一个不可能与文件内容相同的标记串
std::string ReadLiveFile(const std::wstring& path)
{
    FILE* fp = NULL;
    if (_wfopen_s(&fp, path.c_str(), L"rb") != 0 || fp == NULL)
        return std::string("<missing>");
    std::string out;
    std::vector<char> buf(kReadChunkBytes);
    size_t n = 0;
    while ((n = fread(&buf[0], 1, buf.size(), fp)) > 0)
        out.append(&buf[0], n);
    fclose(fp);
    return out;
}

//本次运行的临时目录（%TEMP% 下按进程号与时间区分）
std::wstring MakeLiveTempDir(const wchar_t* tag)
{
    wchar_t tmp[MAX_PATH] = { 0 };
    ::GetTempPathW(MAX_PATH, tmp);
    wchar_t name[MAX_PATH] = { 0 };
    swprintf_s(name, L"%sfp2pl_%s_%lu_%llu", tmp, tag, ::GetCurrentProcessId(), (unsigned long long)::GetTickCount64());
    ::CreateDirectoryW(name, NULL);
    return name;
}

/**
 *  建源文件夹并扫描，日志写到源目录旁（用例结束时须调 CIULog::Uninit）。
 *  @param tag      日志目录名，区分各用例
 *  @param bigBytes 大文件的字节数
 *  @param out      [out] 源文件夹的各项路径与扫描结果
 */
void MakeSourceFolder(const wchar_t* tag, size_t bigBytes, LiveSourceFolder& out)
{
    out.m_parent = MakeLiveTempDir(L"src");
    out.m_root = out.m_parent + L"\\p2pfolder";
    REQUIRE(::CreateDirectoryW(out.m_root.c_str(), NULL));
    REQUIRE(::CreateDirectoryW((out.m_root + L"\\sub").c_str(), NULL));
    WriteLiveFile(out.m_root + L"\\big.bin", bigBytes, 1);
    WriteLiveFile(out.m_root + L"\\empty.txt", 0, 2);
    WriteLiveFile(out.m_root + L"\\sub\\small.txt", kSmallFileBytes, 3);
    REQUIRE(ScanFolderTree(out.m_root, out.m_scan));
    out.m_dstParent = MakeLiveTempDir(L"dst");
    CIULog::Init(true, false, (out.m_parent + L"\\" + tag).c_str());
}

//两端各取一帧 1816 并解析，核对会话号与角色
void WaitRelayInfo(LiveFolderSide& a, LiveFolderSide& b, const std::string& sessionId,
                   net::FolderRelayInfo& infoA, net::FolderRelayInfo& infoB)
{
    std::string bodyA;
    std::string bodyB;
    REQUIRE(a.WaitFrame(cmd_folder_relay_info, bodyA, kWaitSignalMs));
    REQUIRE(b.WaitFrame(cmd_folder_relay_info, bodyB, kWaitSignalMs));
    REQUIRE(net::FolderProtocolClient::parseRelayInfo(bodyA, infoA));
    REQUIRE(net::FolderProtocolClient::parseRelayInfo(bodyB, infoB));
    INFO("1816 to A: " << bodyA << ", 1816 to B: " << bodyB);
    CHECK(infoA.m_sessionId == sessionId);
    CHECK(infoB.m_sessionId == sessionId);
    CHECK(infoA.m_role == net::folder_role_sender);
    CHECK(infoB.m_role == net::folder_role_receiver);
    CHECK(infoA.m_p2pSessionId == infoB.m_p2pSessionId);
}

//A 发 1810 邀请 B，B 收到 1811 后发 1812 接受，两端各取一帧 1816；返回会话号
std::string InviteAndAccept(LiveFolderSide& a, LiveFolderSide& b, const LiveSourceFolder& src,
                            net::FolderRelayInfo& infoA, net::FolderRelayInfo& infoB)
{
    const std::string invite = net::FolderProtocolClient::buildInviteRequest(
        b.m_client.UserId(), src.m_scan.m_manifest.m_folderName, src.m_scan.m_stats.m_dirCount,
        src.m_scan.m_stats.m_fileCount, src.m_scan.m_stats.m_totalBytes);
    REQUIRE(!invite.empty());
    a.SendSignal(cmd_folder_invite, invite);
    std::string body;
    REQUIRE(a.WaitFrame(cmd_folder_invite, body, kWaitSignalMs));
    net::FolderInviteResponse inviteResp;
    REQUIRE(net::FolderProtocolClient::parseInviteResponse(body, inviteResp));
    INFO("invite response: " << body);
    REQUIRE(inviteResp.m_code == 0);
    const std::string sessionId = inviteResp.m_sessionId;

    REQUIRE(b.WaitFrame(cmd_folder_invite_notify, body, kWaitSignalMs));
    net::FolderInviteNotify notify;
    REQUIRE(net::FolderProtocolClient::parseInviteNotify(body, notify));
    CHECK(notify.m_sessionId == sessionId);
    CHECK(notify.m_fromId == a.m_client.UserId());
    b.SendSignal(cmd_folder_response, net::FolderProtocolClient::buildResponse(sessionId, true));
    REQUIRE(b.WaitFrame(cmd_folder_response, body, kWaitSignalMs));

    WaitRelayInfo(a, b, sessionId, infoA, infoB);
    return sessionId;
}

/**
 *  按 1816 建通道工厂，做法与 FolderTransferManager 相同：1816 带 p2psessionid 且本端有业务方时才建，否则返回空，
 *  各条通道走中转。
 */
std::shared_ptr<FolderP2PTransportFactory> MakeFactory(LiveFolderSide& side, const net::FolderRelayInfo& info,
                                                        bool hasHandler)
{
    if (info.m_p2pSessionId.empty() || !hasHandler)
        return std::shared_ptr<FolderP2PTransportFactory>();
    return side.m_broker.CreateFactory(info.m_sessionId, info.m_relayIp, info.m_relayPort, info.m_token,
                                       side.ServerPubKey());
}

//由 1816 填发送会话的参数
void FillSendParams(const net::FolderRelayInfo& info, const LiveSourceFolder& src, LiveFolderSide& side,
                    FolderP2PTransportFactory* factory, FolderSendSessionParams& sp)
{
    sp.m_relayIp = info.m_relayIp;
    sp.m_relayPort = info.m_relayPort;
    sp.m_relayToken = info.m_token;
    sp.m_dataLanes = info.m_lanes;
    sp.m_rootPath = src.m_root;
    sp.m_manifest = src.m_scan.m_manifest;
    sp.m_serverPubKey = side.ServerPubKey();
    sp.m_transportFactory = factory;
}

//由 1816 填接收会话的参数
void FillRecvParams(const net::FolderRelayInfo& info, const LiveSourceFolder& src, LiveFolderSide& side,
                    FolderP2PTransportFactory* factory, FolderRecvSessionParams& rp)
{
    rp.m_sessionId = info.m_sessionId;
    rp.m_relayIp = info.m_relayIp;
    rp.m_relayPort = info.m_relayPort;
    rp.m_relayToken = info.m_token;
    rp.m_dataLanes = info.m_lanes;
    rp.m_saveParentDir = src.m_dstParent;
    rp.m_serverPubKey = side.ServerPubKey();
    rp.m_transportFactory = factory;
}

/**
 *  等两端都报告完成，并核对落地文件与源文件逐个一致。发送端一旦报告失败就不再等接收端：两端走的路不一致时，
 *  接收端可能一直等不到对端。
 */
void WaitDoneAndCompare(LiveSendRecorder& sendRec, LiveRecvRecorder& recvRec, const LiveSourceFolder& src)
{
    for (int waited = 0; waited <= kWaitTransferMs; waited += kPollMs)
    {
        const bool sendEnded = sendRec.m_completed > 0 || sendRec.m_failed > 0;
        const bool recvEnded = recvRec.m_completed > 0 || recvRec.m_failed > 0;
        if ((sendEnded && recvEnded) || sendRec.m_failed > 0)
            break;
        std::this_thread::sleep_for(std::chrono::milliseconds(kPollMs));
    }
    const int sendCompleted = sendRec.m_completed;
    const int recvCompleted = recvRec.m_completed;
    INFO("send failed " << (int)sendRec.m_failed << " code " << (int)sendRec.m_errcode << ", recv failed "
         << (int)recvRec.m_failed << " code " << (int)recvRec.m_errcode);
    REQUIRE(sendCompleted == 1);
    REQUIRE(recvCompleted == 1);
    const std::wstring landed = recvRec.RootPath();
    CHECK(ReadLiveFile(landed + L"\\big.bin") == ReadLiveFile(src.m_root + L"\\big.bin"));
    CHECK(ReadLiveFile(landed + L"\\empty.txt") == ReadLiveFile(src.m_root + L"\\empty.txt"));
    CHECK(ReadLiveFile(landed + L"\\sub\\small.txt") == ReadLiveFile(src.m_root + L"\\sub\\small.txt"));
}

/**
 *  两端各发一帧 1814 结束本次尝试，并核对应答 code 为 0（服务端以先到的为准，后到的按幂等处理）。
 *  @param success true 成功结束；false 以 616（通道断开）失败结束，会话保留供 1817 重试
 */
void CloseBothSides(LiveFolderSide& a, LiveFolderSide& b, const std::string& sessionId, bool success)
{
    const std::string close = net::FolderProtocolClient::buildClose(sessionId, success, error_code_folder_relay_broken,
                                                                    "live test stop");
    REQUIRE(!close.empty());
    a.SendSignal(cmd_folder_close, close);
    b.SendSignal(cmd_folder_close, close);
    std::string body;
    net::FolderSessionResponse resp;
    REQUIRE(a.WaitFrame(cmd_folder_close, body, kWaitSignalMs));
    REQUIRE(net::FolderProtocolClient::parseSessionResponse(body, resp));
    CHECK(resp.m_code == 0);
    REQUIRE(b.WaitFrame(cmd_folder_close, body, kWaitSignalMs));
    REQUIRE(net::FolderProtocolClient::parseSessionResponse(body, resp));
    CHECK(resp.m_code == 0);
}

//链路的路径（取值见 p2p_link_path）；没有链路时返回 -1
int LinkPathOf(const std::shared_ptr<FolderP2PTransportFactory>& factory)
{
    p2p::IP2PLink* link = factory->Slot()->Link();
    if (link == NULL)
        return -1;
    p2p::P2PLinkStats stats;
    memset(&stats, 0, sizeof(stats));
    stats.m_cbSize = sizeof(stats);
    link->GetStats(&stats);
    return stats.m_path;
}

/**
 *  一次完整的传输：两端都有业务方，按 1816 建通道工厂与会话，传完后核对路径并成功结束。
 *  @param forceRelay   两端引擎是否强制中转（不打洞）
 *  @param expectedPath 期望的链路路径，取值见 p2p_link_path
 *  @param logTag       日志目录名
 */
void RunP2PTransfer(bool forceRelay, int expectedPath, const wchar_t* logTag)
{
    LiveSourceFolder src;
    MakeSourceFolder(logTag, kBigFileBytes, src);
    {
        LiveFolderSide a;
        LiveFolderSide b;
        a.Start(kAccountA, true, true);
        b.Start(kAccountB, true, true);
        a.m_engine.SetForceRelay(forceRelay);
        b.m_engine.SetForceRelay(forceRelay);

        net::FolderRelayInfo infoA;
        net::FolderRelayInfo infoB;
        const std::string sessionId = InviteAndAccept(a, b, src, infoA, infoB);
        REQUIRE(!infoA.m_p2pSessionId.empty());

        std::shared_ptr<FolderP2PTransportFactory> fa = MakeFactory(a, infoA, true);
        std::shared_ptr<FolderP2PTransportFactory> fb = MakeFactory(b, infoB, true);
        FolderSendSessionParams sp;
        FolderRecvSessionParams rp;
        FillSendParams(infoA, src, a, fa.get(), sp);
        FillRecvParams(infoB, src, b, fb.get(), rp);
        LiveSendRecorder sendRec;
        LiveRecvRecorder recvRec;
        std::shared_ptr<FolderReceiveState> state(new FolderReceiveState());
        FolderSendSession sender;
        FolderRecvSession receiver;
        REQUIRE(receiver.Start(rp, state, &recvRec));
        REQUIRE(sender.Start(sp, &sendRec));
        WaitDoneAndCompare(sendRec, recvRec, src);

        CHECK(fa->DecideP2P());
        CHECK(fb->DecideP2P());
        CHECK(LinkPathOf(fa) == expectedPath);
        CHECK(LinkPathOf(fb) == expectedPath);
        CloseBothSides(a, b, sessionId, true);

        sender.Stop();
        receiver.Stop();
        fa.reset();
        fb.reset();
        a.Shutdown();
        b.Shutdown();
    }
    CIULog::Uninit();
}

/**
 *  B 不能走 P2P 时的传输：服务端不建 P2P 会话，1816 不带 p2psessionid，两端都不建通道工厂，文件夹经 fileserver 中转。
 *  @param bWithEngine  B 是否启动引擎（false 模拟不支持 P2P 的旧客户端；true 时引擎照常报到，但不报到用途 folder）
 *  @param logTag       日志目录名
 */
//receiverDelayMs：大于 0 时发送端先启动，接收端晚这么多毫秒才启动（FP2PL7 用它在真实中转上复现接收端晚报到），否则接收端先启动
void RunRelayFallbackTransfer(bool bWithEngine, const wchar_t* logTag, int receiverDelayMs = 0)
{
    LiveSourceFolder src;
    MakeSourceFolder(logTag, kBigFileBytes, src);
    {
        LiveFolderSide a;
        LiveFolderSide b;
        a.Start(kAccountA, true, true);
        b.Start(kAccountB, bWithEngine, false);

        net::FolderRelayInfo infoA;
        net::FolderRelayInfo infoB;
        const std::string sessionId = InviteAndAccept(a, b, src, infoA, infoB);
        CHECK(infoA.m_p2pSessionId.empty());

        //与 FolderTransferManager 相同：1816 不带 p2psessionid 时不建工厂，各条通道走中转
        std::shared_ptr<FolderP2PTransportFactory> fa = MakeFactory(a, infoA, true);
        FolderSendSessionParams sp;
        FolderRecvSessionParams rp;
        FillSendParams(infoA, src, a, fa.get(), sp);
        FillRecvParams(infoB, src, b, NULL, rp);
        LiveSendRecorder sendRec;
        LiveRecvRecorder recvRec;
        std::shared_ptr<FolderReceiveState> state(new FolderReceiveState());
        FolderSendSession sender;
        FolderRecvSession receiver;
        if (receiverDelayMs > 0)
        {
            //发送端先连好通道、发出清单；接收端晚报到，第一份清单被 fileserver 丢弃，要靠重发送达
            REQUIRE(sender.Start(sp, &sendRec));
            std::this_thread::sleep_for(std::chrono::milliseconds(receiverDelayMs));
            REQUIRE(receiver.Start(rp, state, &recvRec));
        }
        else
        {
            REQUIRE(receiver.Start(rp, state, &recvRec));
            REQUIRE(sender.Start(sp, &sendRec));
        }
        WaitDoneAndCompare(sendRec, recvRec, src);
        CHECK(fa.get() == NULL);
        CloseBothSides(a, b, sessionId, true);

        sender.Stop();
        receiver.Stop();
        fa.reset();
        a.Shutdown();
        b.Shutdown();
    }
    CIULog::Uninit();
}

} // namespace

TEST_CASE("FP2PL1 真实信令：1816 带 p2psessionid，文件夹经局域网直连的 P2P 链路传完", "[.live][folderp2plive]")
{
    RunP2PTransfer(false, p2p::p2p_link_path_lan, L"fp2pl1");
}

TEST_CASE("FP2PL2 真实信令：两端强制中转，文件夹经 imgserver 的 P2P 中转传完", "[.live][folderp2plive]")
{
    RunP2PTransfer(true, p2p::p2p_link_path_relay, L"fp2pl2");
}

TEST_CASE("FP2PL3 真实信令：第一次传到一半失败，1817 重试换新的 P2P 会话并从断点续传", "[.live][folderp2plive]")
{
    LiveSourceFolder src;
    MakeSourceFolder(L"fp2pl3", kResumeBigFileBytes, src);
    {
        LiveFolderSide a;
        LiveFolderSide b;
        a.Start(kAccountA, true, true);
        b.Start(kAccountB, true, true);

        //第一次：传到一部分时两端停掉会话，以 616 失败结束
        net::FolderRelayInfo infoA;
        net::FolderRelayInfo infoB;
        const std::string sessionId = InviteAndAccept(a, b, src, infoA, infoB);
        const std::string firstP2PId = infoA.m_p2pSessionId;
        REQUIRE(!firstP2PId.empty());
        std::shared_ptr<FolderReceiveState> state(new FolderReceiveState());
        {
            std::shared_ptr<FolderP2PTransportFactory> fa = MakeFactory(a, infoA, true);
            std::shared_ptr<FolderP2PTransportFactory> fb = MakeFactory(b, infoB, true);
            FolderSendSessionParams sp;
            FolderRecvSessionParams rp;
            FillSendParams(infoA, src, a, fa.get(), sp);
            FillRecvParams(infoB, src, b, fb.get(), rp);
            LiveSendRecorder sendRec;
            LiveRecvRecorder recvRec;
            FolderSendSession sender;
            FolderRecvSession receiver;
            REQUIRE(receiver.Start(rp, state, &recvRec));
            REQUIRE(sender.Start(sp, &sendRec));
            bool reached = false;
            for (int waited = 0; waited <= kWaitTransferMs; waited += kPollMs)
            {
                if (sendRec.m_lastSentBytes >= kResumeStopAfterBytes || sendRec.m_completed > 0
                    || sendRec.m_failed > 0)
                {
                    reached = (sendRec.m_completed == 0 && sendRec.m_failed == 0);
                    break;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(kPollMs));
            }
            INFO("first attempt: sent " << (int64_t)sendRec.m_lastSentBytes << ", completed "
                 << (int)sendRec.m_completed << ", failed " << (int)sendRec.m_failed);
            REQUIRE(reached);
            CHECK(fa->DecideP2P());
            sender.Stop();
            receiver.Stop();
        }
        CloseBothSides(a, b, sessionId, false);
        INFO("bytes received in the first attempt: " << state->GetReceivedBytes());
        CHECK(state->GetReceivedBytes() > 0);

        //第二次：A 发 1817，服务端重新推 1816，带新的 p2psessionid
        a.SendSignal(cmd_folder_retry, net::FolderProtocolClient::buildRetry(sessionId));
        std::string body;
        REQUIRE(a.WaitFrame(cmd_folder_retry, body, kWaitSignalMs));
        net::FolderSessionResponse retryResp;
        REQUIRE(net::FolderProtocolClient::parseSessionResponse(body, retryResp));
        INFO("retry response: " << body);
        REQUIRE(retryResp.m_code == 0);
        WaitRelayInfo(a, b, sessionId, infoA, infoB);
        REQUIRE(!infoA.m_p2pSessionId.empty());
        CHECK(infoA.m_p2pSessionId != firstP2PId);

        std::shared_ptr<FolderP2PTransportFactory> fa = MakeFactory(a, infoA, true);
        std::shared_ptr<FolderP2PTransportFactory> fb = MakeFactory(b, infoB, true);
        FolderSendSessionParams sp;
        FolderRecvSessionParams rp;
        FillSendParams(infoA, src, a, fa.get(), sp);
        FillRecvParams(infoB, src, b, fb.get(), rp);
        LiveSendRecorder sendRec;
        LiveRecvRecorder recvRec;
        FolderSendSession sender;
        FolderRecvSession receiver;
        REQUIRE(receiver.Start(rp, state, &recvRec));
        REQUIRE(sender.Start(sp, &sendRec));
        WaitDoneAndCompare(sendRec, recvRec, src);
        //续传起点大于 0：第二次没有从头再传
        const int64_t resumedFrom = sendRec.m_firstSentBytes;
        CHECK(resumedFrom > 0);
        CHECK(fa->DecideP2P());
        CHECK(fb->DecideP2P());
        CloseBothSides(a, b, sessionId, true);

        sender.Stop();
        receiver.Stop();
        fa.reset();
        fb.reset();
        a.Shutdown();
        b.Shutdown();
    }
    CIULog::Uninit();
}

TEST_CASE("FP2PL4 真实信令：对方是不支持 P2P 的旧客户端，1816 不带 p2psessionid，文件夹经中转传完", "[.live][folderp2plive]")
{
    RunRelayFallbackTransfer(false, L"fp2pl4");
}

TEST_CASE("FP2PL5 真实信令：对方关掉了文件夹 P2P，2301 不报到 folder，1816 不带 p2psessionid，文件夹经中转传完", "[.live][folderp2plive]")
{
    RunRelayFallbackTransfer(true, L"fp2pl5");
}

TEST_CASE("FP2PL7 真实 fileserver 中转、接收端晚 1.5 秒报到：第一份清单被中转丢弃，发送端重发后传完", "[.live][folderp2plive]")
{
    RunRelayFallbackTransfer(false, L"fp2pl7", kLateReceiverDelayMs);
}

namespace {

//把只含 ASCII 的宽字符路径转成 UTF-8（测试用的临时目录都在 %TEMP% 下，是 ASCII）
std::string AsciiPath(const std::wstring& path)
{
    std::string out;
    for (size_t i = 0; i < path.size(); ++i)
        out.push_back((char)path[i]);
    return out;
}

//取一帧应答或推送并解析 code；取不到或解析失败返回 -1
int WaitSimpleCode(LiveFolderSide& side, uint16_t cmd)
{
    std::string body;
    if (!side.WaitFrame(cmd, body, kWaitSignalMs))
        return -1;
    int code = -1;
    std::string msg;
    return fileshare::ShareProtocolCodec::ParseSimpleResponse(body, code, msg) ? code : -1;
}

} // namespace

TEST_CASE("FP2PL6 真实信令：文件共享申请下载时 2222 带 p2psessionid，经用途 fileshare 的 P2P 链路传完", "[.live][folderp2plive]")
{
    LiveSourceFolder src;
    MakeSourceFolder(L"fp2pl6", kBigFileBytes, src);
    {
        LiveFolderSide a;
        LiveFolderSide b;
        a.Start(kAccountA, true, true);
        b.Start(kAccountB, true, true);

        //A 在与 B 的单聊里发起文件夹共享，B 只有下载权限
        fileshare::ShareCreateParams create;
        create.m_clientShareId = "fp2pl6-" + std::to_string((unsigned long long)::GetTickCount64());
        create.m_ctype = conversation_single;
        create.m_cid = b.m_client.UserId();
        create.m_kind = share_kind_folder;
        create.m_name = "p2pfolder";
        create.m_displayPath = AsciiPath(src.m_root);
        fileshare::ShareMemberPerm member;
        member.m_userId = b.m_client.UserId();
        member.m_perms = 0;
        create.m_members.push_back(member);
        a.SendSignal(cmd_share_create, fileshare::ShareProtocolCodec::BuildCreateRequest(create));
        std::string body;
        REQUIRE(a.WaitFrame(cmd_share_create, body, kWaitSignalMs));
        fileshare::ShareCreateResponse created;
        REQUIRE(fileshare::ShareProtocolCodec::ParseCreateResponse(body, created));
        INFO("2201 response: " << body);
        REQUIRE(created.m_code == 0);
        const int64_t shareId = created.m_shareId;

        //A 报告这个共享可提供：A 的这条连接成为提供连接
        std::vector<fileshare::ShareServeItem> serve(1);
        serve[0].m_shareId = shareId;
        serve[0].m_available = true;
        a.SendSignal(cmd_share_serve_report, fileshare::ShareProtocolCodec::BuildServeReport(serve));
        REQUIRE(WaitSimpleCode(a, cmd_share_serve_report) == 0);

        //B 打开共享：两端各收到一帧访问会话的 2222（不带 p2psessionid）
        b.SendSignal(cmd_share_open, fileshare::ShareProtocolCodec::BuildShareIdRequest(shareId));
        REQUIRE(b.WaitFrame(cmd_share_open, body, kWaitSignalMs));
        int code = -1;
        std::string msg;
        std::string accessId;
        REQUIRE(fileshare::ShareProtocolCodec::ParseOpenResponse(body, code, msg, accessId));
        INFO("2220 response: " << body);
        REQUIRE(code == 0);
        fileshare::ShareRelayParams accessA;
        fileshare::ShareRelayParams accessB;
        REQUIRE(a.WaitFrame(cmd_share_relay_info, body, kWaitSignalMs));
        REQUIRE(fileshare::ShareProtocolCodec::ParseRelayInfo(body, accessA));
        REQUIRE(b.WaitFrame(cmd_share_relay_info, body, kWaitSignalMs));
        REQUIRE(fileshare::ShareProtocolCodec::ParseRelayInfo(body, accessB));
        CHECK(accessA.m_transferId.empty());
        CHECK(accessA.m_p2pSessionId.empty());
        CHECK(accessB.m_p2pSessionId.empty());

        //B 申请下载整个共享
        std::vector<fileshare::ShareTransferEntry> items(1);
        items[0].m_path = "";
        items[0].m_isDir = true;
        items[0].m_size = src.m_scan.m_stats.m_totalBytes;
        b.SendSignal(cmd_share_request_transfer, fileshare::ShareProtocolCodec::BuildTransferRequest(
            accessId, fileshare::share_transfer_download, items, src.m_scan.m_stats.m_totalBytes,
            src.m_scan.m_stats.m_fileCount));
        REQUIRE(b.WaitFrame(cmd_share_request_transfer, body, kWaitSignalMs));
        std::string transferId;
        REQUIRE(fileshare::ShareProtocolCodec::ParseTransferResponse(body, code, msg, transferId));
        INFO("2224 response: " << body);
        REQUIRE(code == 0);

        //两帧传输子会话的 2222 带同一个非空的 p2psessionid
        fileshare::ShareRelayParams relayA;
        fileshare::ShareRelayParams relayB;
        REQUIRE(a.WaitFrame(cmd_share_relay_info, body, kWaitSignalMs));
        REQUIRE(fileshare::ShareProtocolCodec::ParseRelayInfo(body, relayA));
        REQUIRE(b.WaitFrame(cmd_share_relay_info, body, kWaitSignalMs));
        REQUIRE(fileshare::ShareProtocolCodec::ParseRelayInfo(body, relayB));
        CHECK(relayA.m_forOwner);
        CHECK_FALSE(relayB.m_forOwner);
        CHECK(relayA.m_transferId == transferId);
        CHECK(relayB.m_transferId == transferId);
        REQUIRE(!relayA.m_p2pSessionId.empty());
        CHECK(relayA.m_p2pSessionId == relayB.m_p2pSessionId);

        //两端按 transferid 从 fileshare 业务方建通道工厂；A（分享者，下载时发送）发、B 收
        std::shared_ptr<FolderP2PTransportFactory> fa = a.m_shareBroker.CreateFactory(
            relayA.m_transferId, relayA.m_relayIp, relayA.m_relayPort, relayA.m_token, a.ServerPubKey());
        std::shared_ptr<FolderP2PTransportFactory> fb = b.m_shareBroker.CreateFactory(
            relayB.m_transferId, relayB.m_relayIp, relayB.m_relayPort, relayB.m_token, b.ServerPubKey());
        FolderSendSessionParams sp;
        sp.m_relayIp = relayA.m_relayIp;
        sp.m_relayPort = relayA.m_relayPort;
        sp.m_relayToken = relayA.m_token;
        sp.m_dataLanes = relayA.m_lanes;
        sp.m_rootPath = src.m_root;
        sp.m_manifest = src.m_scan.m_manifest;
        sp.m_serverPubKey = a.ServerPubKey();
        sp.m_transportFactory = fa.get();
        FolderRecvSessionParams rp;
        rp.m_sessionId = relayB.m_transferId;
        rp.m_relayIp = relayB.m_relayIp;
        rp.m_relayPort = relayB.m_relayPort;
        rp.m_relayToken = relayB.m_token;
        rp.m_dataLanes = relayB.m_lanes;
        rp.m_saveParentDir = src.m_dstParent;
        rp.m_serverPubKey = b.ServerPubKey();
        rp.m_transportFactory = fb.get();
        LiveSendRecorder sendRec;
        LiveRecvRecorder recvRec;
        std::shared_ptr<FolderReceiveState> state(new FolderReceiveState());
        FolderSendSession sender;
        FolderRecvSession receiver;
        REQUIRE(receiver.Start(rp, state, &recvRec));
        REQUIRE(sender.Start(sp, &sendRec));
        WaitDoneAndCompare(sendRec, recvRec, src);
        CHECK(fa->DecideP2P());
        CHECK(fb->DecideP2P());
        CHECK(LinkPathOf(fa) == p2p::p2p_link_path_lan);
        CHECK(LinkPathOf(fb) == p2p::p2p_link_path_lan);

        //收尾：B 结束传输，A 取消共享（测试库里不留下这个共享）
        b.SendSignal(cmd_share_transfer_close, fileshare::ShareProtocolCodec::BuildTransferClose(transferId, 0, 0));
        CHECK(WaitSimpleCode(b, cmd_share_transfer_close) == 0);
        a.SendSignal(cmd_share_cancel, fileshare::ShareProtocolCodec::BuildShareIdRequest(shareId));
        CHECK(WaitSimpleCode(a, cmd_share_cancel) == 0);

        sender.Stop();
        receiver.Stop();
        fa.reset();
        fb.reset();
        a.Shutdown();
        b.Shutdown();
    }
    CIULog::Uninit();
}

namespace {

//RDL1 / RDL2：被控端发的关键帧大小（字节），与 1080p 屏幕的 16 位色关键帧同一量级
const size_t kRdKeyframeBytes = 4 * 1024 * 1024;

//RDL1 / RDL2：被控端发的增量帧大小（字节）
const size_t kRdDiffBytes = 2000;

//RDL2：控制端比被控端晚多久打开通道（毫秒）
const int kRdControllerLateMs = 500;

//RDL1 / RDL2：等引擎帧到达的最长时间（毫秒）：强制中转时 4 MiB 经 imgserver 转发
const int kRdWaitFramesMs = 30000;

//一段内容可辨认的数据
std::string RdPattern(size_t n, int salt)
{
    std::string s(n, ' ');
    for (size_t i = 0; i < n; ++i)
        s[i] = (char)((i * kPatternStep + (size_t)salt) % kPatternModulo);
    return s;
}

/**
 *  远程桌面一端的上层回调：记录收到的引擎帧与断开（推流器、观看窗在这里做解码或注入）。
 */
class LiveRdRecorder : public remotedesktop::RdRelayClientHandler
{
public:
    LiveRdRecorder()
        : m_closed(0)
    {
    }

    virtual void OnRelayEngineFrame(const unsigned char* data, size_t len) override
    {
        std::lock_guard<std::mutex> guard(m_mutex);
        m_frames.push_back(std::string(reinterpret_cast<const char*>(data), len));
    }

    virtual void OnRelayClosed() override
    {
        ++m_closed;
    }

    //最多等 timeoutMs 毫秒，直到收到 count 帧
    bool WaitFrames(size_t count, int timeoutMs)
    {
        for (int waited = 0; waited <= timeoutMs; waited += kPollMs)
        {
            {
                std::lock_guard<std::mutex> guard(m_mutex);
                if (m_frames.size() >= count)
                    return true;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(kPollMs));
        }
        return false;
    }

    std::string FrameAt(size_t i)
    {
        std::lock_guard<std::mutex> guard(m_mutex);
        return m_frames[i];
    }

    std::mutex               m_mutex;    //保护 m_frames
    std::vector<std::string> m_frames;   //收到的引擎帧
    std::atomic<int>         m_closed;   //报告断开的次数
};

//A 发 1710 邀请 B，B 收到 1711 后发 1712 接受，两端各取一帧 1716；返回会话号
std::string RdInviteAndAccept(LiveFolderSide& a, LiveFolderSide& b, net::RDRelayInfo& infoA, net::RDRelayInfo& infoB)
{
    a.SendSignal(cmd_rd_invite, net::RemoteDesktopProtocolClient::buildInviteRequest(b.m_client.UserId()));
    std::string body;
    REQUIRE(a.WaitFrame(cmd_rd_invite, body, kWaitSignalMs));
    net::RDInviteResponse inviteResp;
    REQUIRE(net::RemoteDesktopProtocolClient::parseInviteResponse(body, inviteResp));
    INFO("rd invite response: " << body);
    REQUIRE(inviteResp.m_code == 0);
    const std::string sessionId = inviteResp.m_sessionId;

    REQUIRE(b.WaitFrame(cmd_rd_invite_notify, body, kWaitSignalMs));
    net::RDInviteNotify notify;
    REQUIRE(net::RemoteDesktopProtocolClient::parseInviteNotify(body, notify));
    CHECK(notify.m_sessionId == sessionId);
    CHECK(notify.m_fromId == a.m_client.UserId());
    b.SendSignal(cmd_rd_response, net::RemoteDesktopProtocolClient::buildResponse(sessionId, true));

    std::string bodyA;
    std::string bodyB;
    REQUIRE(a.WaitFrame(cmd_rd_relay_info, bodyA, kWaitSignalMs));
    REQUIRE(b.WaitFrame(cmd_rd_relay_info, bodyB, kWaitSignalMs));
    REQUIRE(net::RemoteDesktopProtocolClient::parseRelayInfo(bodyA, infoA));
    REQUIRE(net::RemoteDesktopProtocolClient::parseRelayInfo(bodyB, infoB));
    INFO("1716 to A: " << bodyA << ", 1716 to B: " << bodyB);
    CHECK(infoA.m_sessionId == sessionId);
    CHECK(infoB.m_sessionId == sessionId);
    CHECK(infoA.m_role == net::rd_role_controller);
    CHECK(infoB.m_role == net::rd_role_controlled);
    CHECK(!infoA.m_p2pSessionId.empty());
    CHECK(infoA.m_p2pSessionId == infoB.m_p2pSessionId);
    return sessionId;
}

//按 1716 填传输参数，做法与 CRdController::BuildTransportParams 相同
void FillRdParams(LiveFolderSide& side, const net::RDRelayInfo& info, remotedesktop::RdTransportParams& out)
{
    out.SetRelay(info.m_sessionId, info.m_relayIp, info.m_relayPort, info.m_token, side.ServerPubKey());
    if (!info.m_p2pSessionId.empty())
        out.m_slot = side.m_rdBroker.ClaimSlot(info.m_sessionId);
}

//在工作线程上连接（推流线程、观看窗的连接线程做的事）
void RdConnectProc(remotedesktop::RdTransport* transport, const remotedesktop::RdTransportParams* params,
                   remotedesktop::RdRelayClientHandler* handler, bool* ok)
{
    *ok = transport->Connect(*params, handler, NULL);
}

//链路槽上链路的路径（取值见 p2p_link_path）；没有链路时返回 -1
int SlotLinkPath(const std::shared_ptr<P2PLinkSlot>& slot)
{
    p2p::IP2PLink* link = slot ? slot->Link() : NULL;
    if (link == NULL)
        return -1;
    p2p::P2PLinkStats stats;
    memset(&stats, 0, sizeof(stats));
    stats.m_cbSize = sizeof(stats);
    link->GetStats(&stats);
    return stats.m_path;
}

//发一帧并要求成功
void RdSend(remotedesktop::RdTransport& transport, const std::string& frame)
{
    REQUIRE(transport.SendEngineFrame(reinterpret_cast<const unsigned char*>(frame.data()), frame.size()));
}

/**
 *  一次远程桌面：邀请、接受、两端连接、互发引擎帧、结束。
 *  @param forceRelay        两端引擎是否强制中转（不打洞）
 *  @param expectedPath      期望的链路路径，取值见 p2p_link_path
 *  @param controllerLateMs  控制端比被控端晚多久连接（毫秒）；0 表示两端同时连接
 *  @param logTag            日志目录名
 */
void RunRdSession(bool forceRelay, int expectedPath, int controllerLateMs, const wchar_t* logTag)
{
    const std::wstring logParent = MakeLiveTempDir(L"rd");
    CIULog::Init(true, false, (logParent + L"\\" + logTag).c_str());
    {
        LiveFolderSide a;
        LiveFolderSide b;
        a.Start(kAccountA, true, true);
        b.Start(kAccountB, true, true);
        a.m_engine.SetForceRelay(forceRelay);
        b.m_engine.SetForceRelay(forceRelay);

        net::RDRelayInfo infoA;
        net::RDRelayInfo infoB;
        const std::string sessionId = RdInviteAndAccept(a, b, infoA, infoB);
        remotedesktop::RdTransportParams pa;
        remotedesktop::RdTransportParams pb;
        FillRdParams(a, infoA, pa);
        FillRdParams(b, infoB, pb);
        REQUIRE(pa.m_slot);
        REQUIRE(pb.m_slot);

        LiveRdRecorder recA;
        LiveRdRecorder recB;
        remotedesktop::RdTransport ta;
        remotedesktop::RdTransport tb;
        const std::string key = RdPattern(kRdKeyframeBytes, 1);
        const std::string diff = RdPattern(kRdDiffBytes, 2);
        bool okA = false;
        bool okB = false;
        if (controllerLateMs > 0)
        {
            //被控端先连上并立即推出关键帧与增量帧，控制端过一会儿才打开通道
            RdConnectProc(&tb, &pb, &recB, &okB);
            REQUIRE(okB);
            RdSend(tb, key);
            RdSend(tb, diff);
            std::this_thread::sleep_for(std::chrono::milliseconds(controllerLateMs));
            RdConnectProc(&ta, &pa, &recA, &okA);
            REQUIRE(okA);
        }
        else
        {
            std::thread ca(&RdConnectProc, &ta, &pa, &recA, &okA);
            std::thread cb(&RdConnectProc, &tb, &pb, &recB, &okB);
            ca.join();
            cb.join();
            REQUIRE(okA);
            REQUIRE(okB);
            RdSend(tb, key);
            RdSend(tb, diff);
        }
        CHECK(ta.Path() == remotedesktop::RdTransport::rd_path_p2p);
        CHECK(tb.Path() == remotedesktop::RdTransport::rd_path_p2p);
        CHECK(SlotLinkPath(pa.m_slot) == expectedPath);
        CHECK(SlotLinkPath(pb.m_slot) == expectedPath);
        //观看窗工具条与受控提示条的「P2P」徽章按它判断（2026-10-04 起）
        CHECK(ta.LinkPath() == expectedPath);
        CHECK(tb.LinkPath() == expectedPath);

        //控制端按序收到关键帧与增量帧
        REQUIRE(recA.WaitFrames(2, kRdWaitFramesMs));
        CHECK(recA.FrameAt(0) == key);
        CHECK(recA.FrameAt(1) == diff);

        //控制端发请求关键帧与一条输入命令，被控端按序收到
        const std::string request(1, '\x05');
        const std::string input = "rd-live-input";
        RdSend(ta, request);
        RdSend(ta, input);
        REQUIRE(recB.WaitFrames(2, kRdWaitFramesMs));
        CHECK(recB.FrameAt(0) == request);
        CHECK(recB.FrameAt(1) == input);
        CHECK(recA.m_closed == 0);
        CHECK(recB.m_closed == 0);

        //结束：两端关闭传输（链路以业务结束关闭），A 发 1714 结束会话，B 收到状态推送
        ta.Close();
        tb.Close();
        a.SendSignal(cmd_rd_close, net::RemoteDesktopProtocolClient::buildClose(sessionId));
        std::string body;
        REQUIRE(b.WaitFrame(cmd_rd_state_notify, body, kWaitSignalMs));
        net::RDStateNotify state;
        REQUIRE(net::RemoteDesktopProtocolClient::parseStateNotify(body, state));
        CHECK(state.m_sessionId == sessionId);
        a.Shutdown();
        b.Shutdown();
    }
    CIULog::Uninit();
}

} // namespace

TEST_CASE("RDL1 远程桌面真实信令：1716 带 p2psessionid，两端经局域网直连的 P2P 链路互发引擎帧", "[.live][rdp2plive]")
{
    RunRdSession(false, p2p::p2p_link_path_lan, 0, L"rdl1");
}

TEST_CASE("RDL2 远程桌面强制中转、被控端先发：控制端晚打开通道也完整收到关键帧", "[.live][rdp2plive]")
{
    RunRdSession(true, p2p::p2p_link_path_relay, kRdControllerLateMs, L"rdl2");
}

namespace {

//RDL3：按真实节奏连续推流的时长（毫秒）
const int kRdStreamDurationMs = 12000;

//RDL3：两帧之间的间隔（毫秒），与 RdControlledStreamer 的抓屏间隔相同
const int kRdStreamIntervalMs = 200;

//RDL3：每隔多少帧发一次关键帧（BITMAPINFO + 首屏全量），与 RdControlledStreamer 的关键帧间隔相同
const int kRdStreamFramesPerKeyframe = 10;

//RDL3：BITMAPINFO 帧大小（字节）：类型字节 + BITMAPINFOHEADER + 16 位色的三个颜色掩码
const size_t kRdStreamBitmapInfoBytes = 1 + 40 + 12;

//RDL3：增量帧大小（字节）：用户正在操作电脑、整屏有较多变化时的量级
const size_t kRdStreamDiffBytes = 200 * 1024;

//RDL3：推流结束后等控制端收齐全部帧的最长时间（毫秒）
const int kRdStreamDrainMs = 30000;

//RDL3：记录链路统计的间隔（毫秒）
const int kRdStatsIntervalMs = 1000;

/**
 *  RDL3 被控端推流线程的输入与结果：按推流器的节奏连续发帧，记下每一帧的大小与发出时刻。
 */
struct RdStreamJob
{
    remotedesktop::RdTransport* m_transport;   //被控端的传输（不拥有）
    const std::string*          m_bitmapInfo;  //BITMAPINFO 帧内容（不拥有）
    const std::string*          m_keyframe;    //关键帧内容（不拥有）
    const std::string*          m_diff;        //增量帧内容（不拥有）
    std::vector<size_t>         m_sentSizes;   //按发送顺序记录的每帧大小（字节）
    std::vector<int64_t>        m_sentAtMs;    //每帧发出（SendEngineFrame 返回）的时刻，P2PSteadyNowMs 计
    bool                        m_failed;      //是否有 SendEngineFrame 返回失败

    RdStreamJob()
        : m_transport(NULL)
        , m_bitmapInfo(NULL)
        , m_keyframe(NULL)
        , m_diff(NULL)
        , m_failed(false)
    {
    }
};

//发一帧并记下大小与时刻；失败时置 m_failed 并返回 false
bool RdStreamSend(RdStreamJob* job, const std::string& frame)
{
    if (!job->m_transport->SendEngineFrame(reinterpret_cast<const unsigned char*>(frame.data()), frame.size()))
    {
        job->m_failed = true;
        return false;
    }
    job->m_sentSizes.push_back(frame.size());
    job->m_sentAtMs.push_back(P2PSteadyNowMs());
    return true;
}

//把一端链路的统计写进日志（RDL3 复现失败时据此判断数据停在了哪一段）
void LogRdLinkStats(const char* side, const std::shared_ptr<P2PLinkSlot>& slot)
{
    p2p::IP2PLink* link = slot ? slot->Link() : NULL;
    if (link == NULL)
    {
        LOG_INFO("RDL3 stats %s: no link.", side);
        return;
    }
    p2p::P2PLinkStats stats;
    memset(&stats, 0, sizeof(stats));
    stats.m_cbSize = sizeof(stats);
    link->GetStats(&stats);
    LOG_INFO("RDL3 stats %s: path=%d rtt=%u ms loss=%u permille queued=%llu sent=%llu received=%llu.", side,
             stats.m_path, stats.m_rttMs, stats.m_lossPermille, (unsigned long long)stats.m_queuedBytes,
             (unsigned long long)stats.m_bytesSent, (unsigned long long)stats.m_bytesReceived);
}

//被控端推流线程：做法与 RdControlledStreamer::PushLoop 相同，首轮与每隔 kRdStreamFramesPerKeyframe 帧发关键帧
void RdStreamProc(RdStreamJob* job)
{
    const int64_t start = P2PSteadyNowMs();
    int framesSinceKeyframe = 0;
    bool needKeyframe = true;
    while (P2PSteadyNowMs() - start < kRdStreamDurationMs)
    {
        if (needKeyframe || framesSinceKeyframe >= kRdStreamFramesPerKeyframe)
        {
            if (!RdStreamSend(job, *job->m_bitmapInfo) || !RdStreamSend(job, *job->m_keyframe))
                return;
            framesSinceKeyframe = 0;
            needKeyframe = false;
        }
        else
        {
            if (!RdStreamSend(job, *job->m_diff))
                return;
            ++framesSinceKeyframe;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(kRdStreamIntervalMs));
    }
}

} // namespace

TEST_CASE("RDL3 远程桌面按真实节奏连续推流：局域网直连的 P2P 链路上控制端按序收齐每一帧", "[.live][rdp2pstream]")
{
    const std::wstring logParent = MakeLiveTempDir(L"rd");
    CIULog::Init(true, false, (logParent + L"\\rdl3").c_str());
    {
        LiveFolderSide a;
        LiveFolderSide b;
        a.Start(kAccountA, true, true);
        b.Start(kAccountB, true, true);

        net::RDRelayInfo infoA;
        net::RDRelayInfo infoB;
        const std::string sessionId = RdInviteAndAccept(a, b, infoA, infoB);
        remotedesktop::RdTransportParams pa;
        remotedesktop::RdTransportParams pb;
        FillRdParams(a, infoA, pa);
        FillRdParams(b, infoB, pb);
        REQUIRE(pa.m_slot);
        REQUIRE(pb.m_slot);

        LiveRdRecorder recA;
        LiveRdRecorder recB;
        remotedesktop::RdTransport ta;
        remotedesktop::RdTransport tb;
        bool okA = false;
        bool okB = false;
        std::thread ca(&RdConnectProc, &ta, &pa, &recA, &okA);
        std::thread cb(&RdConnectProc, &tb, &pb, &recB, &okB);
        ca.join();
        cb.join();
        REQUIRE(okA);
        REQUIRE(okB);
        CHECK(ta.Path() == remotedesktop::RdTransport::rd_path_p2p);
        CHECK(tb.Path() == remotedesktop::RdTransport::rd_path_p2p);
        CHECK(SlotLinkPath(pa.m_slot) == p2p::p2p_link_path_lan);

        //被控端在自己的线程上按推流器的节奏连续推流
        const std::string bitmapInfo = RdPattern(kRdStreamBitmapInfoBytes, 3);
        const std::string keyframe = RdPattern(kRdKeyframeBytes, 1);
        const std::string diff = RdPattern(kRdStreamDiffBytes, 2);
        RdStreamJob job;
        job.m_transport = &tb;
        job.m_bitmapInfo = &bitmapInfo;
        job.m_keyframe = &keyframe;
        job.m_diff = &diff;
        std::thread pusher(&RdStreamProc, &job);
        //推流期间每秒记一次两端链路的统计
        for (int t = 0; t < kRdStreamDurationMs; t += kRdStatsIntervalMs)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(kRdStatsIntervalMs));
            LogRdLinkStats("A", pa.m_slot);
            LogRdLinkStats("B", pb.m_slot);
        }
        pusher.join();
        CHECK_FALSE(job.m_failed);
        const size_t sent = job.m_sentSizes.size();
        LOG_INFO("RDL3: pusher sent %u frames in %d ms.", (unsigned)sent, kRdStreamDurationMs);
        REQUIRE(sent > (size_t)kRdStreamFramesPerKeyframe);

        //控制端按序收齐每一帧，大小与发送顺序一致；等待期间每秒记一次两端链路的统计
        bool all = false;
        for (int waited = 0; waited < kRdStreamDrainMs && !all; waited += kRdStatsIntervalMs)
        {
            all = recA.WaitFrames(sent, kRdStatsIntervalMs);
            LogRdLinkStats("A", pa.m_slot);
            LogRdLinkStats("B", pb.m_slot);
        }
        size_t received = 0;
        {
            std::lock_guard<std::mutex> guard(recA.m_mutex);
            received = recA.m_frames.size();
        }
        LOG_INFO("RDL3: controller received %u of %u frames, drained=%d.", (unsigned)received, (unsigned)sent,
                 all ? 1 : 0);
        INFO("received " << received << " of " << sent << " frames");
        CHECK(all);
        const size_t check = received < sent ? received : sent;
        for (size_t i = 0; i < check; ++i)
        {
            INFO("frame " << i);
            CHECK(recA.FrameAt(i).size() == job.m_sentSizes[i]);
        }
        CHECK(recA.m_closed == 0);

        ta.Close();
        tb.Close();
        a.SendSignal(cmd_rd_close, net::RemoteDesktopProtocolClient::buildClose(sessionId));
        a.Shutdown();
        b.Shutdown();
    }
    CIULog::Uninit();
}

namespace {

//CL1 / CL2：A 发的音频消息数（20 毫秒一个，相当于 1 秒）与每个音频包的大小（字节）
const int kCallAudioPackets = 50;
const size_t kCallAudioBytes = 80;

//CL1 / CL2：A 发的视频关键帧大小（字节），720p 关键帧的量级
const size_t kCallKeyframeBytes = 300 * 1024;

//CL1 / CL2：通话用的数据报通道号与排队上限（与 CallMediaSession 的 kCallP2PChannelId / kCallDatagramQueueLimit 相同）
const uint16_t kCallChannelId = 1;
const size_t kCallQueueLimit = 256;

//等链路就绪、等消息到达的最长时间（毫秒）
const int kCallWaitLinkMs = 20000;
const int kCallWaitMessagesMs = 30000;

//CL3：等主叫一端的链路槽进入终态的最长时间（毫秒）：被叫放弃后，链路建好时由被叫一端关闭，主叫收到 2305
const int kCallWaitAbandonMs = 30000;

/**
 *  数据报通道一侧的回调：记录收到的消息与断开。
 */
class LiveDatagramRecorder : public IP2PDatagramHandler
{
public:
    LiveDatagramRecorder()
        : m_closed(0)
    {
    }

    virtual void OnDatagram(const unsigned char* data, size_t len) override
    {
        std::lock_guard<std::mutex> guard(m_mutex);
        m_messages.push_back(std::string(reinterpret_cast<const char*>(data), len));
    }

    virtual void OnDatagramClosed() override
    {
        ++m_closed;
    }

    bool WaitCount(size_t count, int timeoutMs)
    {
        for (int waited = 0; waited <= timeoutMs; waited += kPollMs)
        {
            {
                std::lock_guard<std::mutex> guard(m_mutex);
                if (m_messages.size() >= count)
                    return true;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(kPollMs));
        }
        return false;
    }

    std::string At(size_t i)
    {
        std::lock_guard<std::mutex> guard(m_mutex);
        return m_messages[i];
    }

    std::mutex               m_mutex;      //保护 m_messages
    std::vector<std::string> m_messages;   //收到的消息
    std::atomic<int>         m_closed;     //报告断开的次数
};

//A 发 1701 呼叫 B（语音），A 取应答、B 取来电推送；核对两边带同一个非空的 p2psessionid，返回通话会话号
std::string CallInvite(LiveFolderSide& a, LiveFolderSide& b, net::CallInviteResponse& resp, net::CallInviteNotify& notify)
{
    a.SendSignal(cmd_call_invite, net::CallProtocolClient::buildInviteRequest(b.m_client.UserId(), net::call_media_audio));
    std::string body;
    REQUIRE(a.WaitFrame(cmd_call_invite, body, kWaitSignalMs));
    INFO("call invite response: " << body);
    REQUIRE(net::CallProtocolClient::parseInviteResponse(body, resp));
    REQUIRE(resp.m_code == 0);
    REQUIRE(b.WaitFrame(cmd_call_invite_notify, body, kWaitSignalMs));
    INFO("call invite notify: " << body);
    REQUIRE(net::CallProtocolClient::parseInviteNotify(body, notify));
    CHECK(notify.m_sessionId == resp.m_sessionId);
    CHECK(!resp.m_p2pSessionId.empty());
    CHECK(resp.m_p2pSessionId == notify.m_p2pSessionId);
    return resp.m_sessionId;
}

//取 side 的状态推送，直到状态为 wanted；最多等 kWaitSignalMs 毫秒（每条推送各等一次）
bool WaitCallState(LiveFolderSide& side, int wanted)
{
    std::string body;
    while (side.WaitFrame(cmd_call_state_notify, body, kWaitSignalMs))
    {
        net::CallStateNotify state;
        if (net::CallProtocolClient::parseStateNotify(body, state) && state.m_state == wanted)
            return true;
    }
    return false;
}

//组一条通话格式的消息
std::string CallMessage(int kind, uint8_t flags, uint32_t seq, const std::string& payload)
{
    std::vector<uint8_t> msg;
    call::BuildCallP2PMessage(kind, flags, seq, (uint64_t)seq * kRecvSliceMs, reinterpret_cast<const uint8_t*>(payload.data()),
                              payload.size(), msg);
    return std::string(msg.begin(), msg.end());
}

//经数据报通道发一条消息并要求成功
void SendCallMessage(P2PDatagramChannel& ch, const std::string& msg)
{
    REQUIRE(ch.Send(reinterpret_cast<const unsigned char*>(msg.data()), msg.size()) == p2p::p2p_send_ok);
}

/**
 *  一通接通的电话：呼叫、接听、两端在数据报通道上互发通话格式的消息、挂断。
 *  @param forceRelay   两端引擎是否强制中转（不打洞）
 *  @param expectedPath 期望的链路路径，取值见 p2p_link_path
 *  @param logTag       日志目录名
 */
void RunCallOverP2P(bool forceRelay, int expectedPath, const wchar_t* logTag)
{
    const std::wstring logParent = MakeLiveTempDir(L"call");
    CIULog::Init(true, false, (logParent + L"\\" + logTag).c_str());
    {
        LiveFolderSide a;
        LiveFolderSide b;
        a.Start(kAccountA, true, true);
        b.Start(kAccountB, true, true);
        a.m_engine.SetForceRelay(forceRelay);
        b.m_engine.SetForceRelay(forceRelay);

        net::CallInviteResponse resp;
        net::CallInviteNotify notify;
        const std::string sessionId = CallInvite(a, b, resp, notify);
        //响铃阶段：两端各自认领链路槽（与通话控制器的做法相同）
        std::shared_ptr<P2PLinkSlot> slotA = a.m_callBroker.ClaimSlot(sessionId);
        std::shared_ptr<P2PLinkSlot> slotB = b.m_callBroker.ClaimSlot(sessionId);
        b.SendSignal(cmd_call_response, net::CallProtocolClient::buildResponse(sessionId, true));
        std::string body;
        REQUIRE(a.WaitFrame(cmd_call_signaling, body, kWaitSignalMs));
        REQUIRE(b.WaitFrame(cmd_call_signaling, body, kWaitSignalMs));

        REQUIRE(slotA->Wait(kCallWaitLinkMs) == P2PLinkSlot::slot_ready);
        REQUIRE(slotB->Wait(kCallWaitLinkMs) == P2PLinkSlot::slot_ready);
        CHECK(SlotLinkPath(slotA) == expectedPath);
        CHECK(SlotLinkPath(slotB) == expectedPath);

        LiveDatagramRecorder recA;
        LiveDatagramRecorder recB;
        P2PDatagramChannel chA(slotA, kCallQueueLimit);
        P2PDatagramChannel chB(slotB, kCallQueueLimit);
        REQUIRE(chA.Open(kCallChannelId, &recA));
        REQUIRE(chB.Open(kCallChannelId, &recB));

        //A 发音频与一帧关键帧；B 全部收到，序号连续（局域网与中转路径上都不该丢）
        for (int i = 0; i < kCallAudioPackets; ++i)
        {
            SendCallMessage(chA, CallMessage(call::call_p2p_kind_audio, 0, (uint32_t)i,
                                             std::string(kCallAudioBytes, (char)('a' + i % 26))));
            std::this_thread::sleep_for(std::chrono::milliseconds(kPollMs));
        }
        const std::string key = RdPattern(kCallKeyframeBytes, 3);
        SendCallMessage(chA, CallMessage(call::call_p2p_kind_video, call::kCallP2PFlagKeyframe, 0, key));
        REQUIRE(recB.WaitCount((size_t)kCallAudioPackets + 1, kCallWaitMessagesMs));
        call::CallAudioSeqTracker tracker;
        for (int i = 0; i < kCallAudioPackets; ++i)
        {
            const std::string m = recB.At((size_t)i);
            call::CallP2PMessage parsed;
            REQUIRE(call::ParseCallP2PMessage(reinterpret_cast<const uint8_t*>(m.data()), m.size(), parsed));
            CHECK(parsed.m_kind == call::call_p2p_kind_audio);
            int lost = -1;
            CHECK(tracker.OnPacket(parsed.m_seq, lost));
            CHECK(lost == 0);
        }
        const std::string v = recB.At((size_t)kCallAudioPackets);
        call::CallP2PMessage video;
        REQUIRE(call::ParseCallP2PMessage(reinterpret_cast<const uint8_t*>(v.data()), v.size(), video));
        CHECK(video.m_kind == call::call_p2p_kind_video);
        CHECK((video.m_flags & call::kCallP2PFlagKeyframe) != 0);
        CHECK(std::string(reinterpret_cast<const char*>(video.m_payload), video.m_payloadLen) == key);

        //B 请 A 补一个关键帧
        SendCallMessage(chB, CallMessage(call::call_p2p_kind_keyframe_request, 0, 0, std::string()));
        REQUIRE(recA.WaitCount(1, kCallWaitMessagesMs));
        call::CallP2PMessage request;
        const std::string r = recA.At(0);
        REQUIRE(call::ParseCallP2PMessage(reinterpret_cast<const uint8_t*>(r.data()), r.size(), request));
        CHECK(request.m_kind == call::call_p2p_kind_keyframe_request);
        CHECK(chA.DroppedCount() == 0);
        CHECK(chB.DroppedCount() == 0);
        CHECK(recA.m_closed == 0);
        CHECK(recB.m_closed == 0);

        //挂断：两端关闭通道与链路，A 发 1705
        chA.Close();
        chB.Close();
        slotA->CloseLink();
        slotB->CloseLink();
        a.SendSignal(cmd_call_hangup, net::CallProtocolClient::buildHangup(sessionId));
        //接听时已排进队列的「已接通」推送先取走，一直等到「已结束」：断开得太早，服务端还没处理挂断，通话按掉线
        //宽限期保留，后面的用例再呼叫就是「对方忙」
        REQUIRE(WaitCallState(b, net::call_state_ended));
        a.Shutdown();
        b.Shutdown();
    }
    CIULog::Uninit();
}

} // namespace

TEST_CASE("CL1 通话真实信令：邀请应答与来电推送带 p2psessionid，音视频消息经局域网直连的数据报通道互通", "[.live][callp2plive]")
{
    RunCallOverP2P(false, p2p::p2p_link_path_lan, L"cl1");
}

TEST_CASE("CL2 通话强制中转：音视频消息经 imgserver 的 P2P 中转互通", "[.live][callp2plive]")
{
    RunCallOverP2P(true, p2p::p2p_link_path_relay, L"cl2");
}

TEST_CASE("CL3 被叫拒绝：放弃会话号后主叫一端的链路不会一直挂着", "[.live][callp2plive]")
{
    const std::wstring logParent = MakeLiveTempDir(L"call");
    CIULog::Init(true, false, (logParent + L"\\cl3").c_str());
    {
        LiveFolderSide a;
        LiveFolderSide b;
        a.Start(kAccountA, true, true);
        b.Start(kAccountB, true, true);

        net::CallInviteResponse resp;
        net::CallInviteNotify notify;
        const std::string sessionId = CallInvite(a, b, resp, notify);
        std::shared_ptr<P2PLinkSlot> slotA = a.m_callBroker.ClaimSlot(sessionId);
        //被叫拒绝：通话控制器先放弃这个会话号（链路可能还没建好），再发拒绝
        b.m_callBroker.Abandon(sessionId);
        b.SendSignal(cmd_call_response, net::CallProtocolClient::buildResponse(sessionId, false));
        std::string body;
        REQUIRE(a.WaitFrame(cmd_call_state_notify, body, kWaitSignalMs));
        net::CallStateNotify state;
        REQUIRE(net::CallProtocolClient::parseStateNotify(body, state));
        CHECK(state.m_state == net::call_state_rejected);

        //主叫一端的链路槽进入终态（链路建好后被被叫一端关闭，或建链失败），不会一直等着
        int finalState = P2PLinkSlot::slot_waiting;
        for (int waited = 0; waited <= kCallWaitAbandonMs; waited += kRecvSliceMs)
        {
            finalState = slotA->State();
            if (finalState == P2PLinkSlot::slot_closed || finalState == P2PLinkSlot::slot_failed)
                break;
            std::this_thread::sleep_for(std::chrono::milliseconds(kRecvSliceMs));
        }
        INFO("caller slot state " << finalState);
        CHECK((finalState == P2PLinkSlot::slot_closed || finalState == P2PLinkSlot::slot_failed));
        a.m_callBroker.Abandon(sessionId);
        a.Shutdown();
        b.Shutdown();
    }
    CIULog::Uninit();
}

namespace {

//合成画面的尺寸：高 360 不是 16 的倍数，解码器会补齐，用来检查裁剪
const int kSharePW = 640;
const int kSharePH = 360;

//合成画面每帧之间的间隔（毫秒），约 30 帧每秒
const UINT kShareFrameIntervalMs = 33;

//每像素字节数与颜色比对的容差（每通道），H.264 有损压缩后的纯色会有小偏差
const int kShareBpp = 4;
const int kShareColorTol = 30;

//等画面、状态、事件的最长时间（毫秒）：P2P 链路的建立最多 20 秒，再留余量
const int kShareWaitMs = 30000;

//SSPL1：B 发标注时故意写错的座位号
const int kShareFakeSeat = 7;

//发起共享时的标题：第 15 期起服务端要求 1720 带 1～30 个字符的标题
const char* const kShareTitle = "p2p live share";

//合成画面采集器：每次都给一帧纯红画面
class P2PSolidCapturer : public screenshare::IShareCapturer
{
public:
    P2PSolidCapturer()
        : m_pixels(static_cast<size_t>(kSharePW) * kSharePH * kShareBpp)
    {
        for (size_t i = 0; i < m_pixels.size(); i += kShareBpp)
        {
            m_pixels[i] = 0;
            m_pixels[i + 1] = 0;
            m_pixels[i + 2] = 255;
            m_pixels[i + 3] = 255;
        }
    }

    virtual bool Start()
    {
        return true;
    }

    virtual int Capture(UINT timeoutMs, screenshare::ShareCaptureFrame& out)
    {
        ::Sleep(timeoutMs < kShareFrameIntervalMs ? timeoutMs : kShareFrameIntervalMs);
        out.m_data = &m_pixels[0];
        out.m_stride = kSharePW * kShareBpp;
        out.m_width = kSharePW;
        out.m_height = kSharePH;
        return screenshare::share_capture_ok;
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
        RECT rc = { 0, 0, kSharePW, kSharePH };
        return rc;
    }

private:
    std::vector<uint8_t> m_pixels;   //合成画面
};

//记录推流管线的事件
class P2PHostEvents : public screenshare::ShareHostStreamerListener
{
public:
    P2PHostEvents()
        : m_connected(false)
        , m_failed(false)
    {
    }

    virtual void OnHostStreamEvent(int event, int value)
    {
        (void)value;
        if (event == screenshare::share_host_relay_connected)
            m_connected.store(true);
        if (event == screenshare::share_host_relay_failed || event == screenshare::share_host_capture_failed)
            m_failed.store(true);
    }

    std::atomic<bool> m_connected;   //已连上房间中转
    std::atomic<bool> m_failed;      //连接或采集失败
};

//记录收流管线的事件
class P2PViewerEvents : public screenshare::ShareViewerSink
{
public:
    P2PViewerEvents()
        : m_frames(0)
        , m_lastState(-1)
        , m_relayEvents(0)
    {
    }

    virtual void OnViewerFrame()
    {
        m_frames.fetch_add(1);
    }

    virtual void OnViewerEvent(int event, int value)
    {
        if (event == screenshare::share_viewer_source_state)
            m_lastState.store(value);
        else
            m_relayEvents.fetch_add(1);
    }

    std::atomic<int> m_frames;        //收到的画面通知次数
    std::atomic<int> m_lastState;     //最近一次共享内容状态
    std::atomic<int> m_relayEvents;   //连接失败或断开的次数
};

//记录发起人收到的标注
class P2PAnnotRecorder : public screenshare::ShareAnnotationSink
{
public:
    P2PAnnotRecorder()
        : m_count(0)
        , m_lastSeat(-1)
    {
    }

    virtual void OnAnnotationFrame(int senderSeat, const std::string& payload)
    {
        (void)payload;
        m_lastSeat.store(senderSeat);
        m_count.fetch_add(1);
    }

    std::atomic<int> m_count;      //收到的标注帧数
    std::atomic<int> m_lastSeat;   //最近一帧的发送者座位号
};

//最多等 kShareWaitMs 毫秒，直到 *value 不小于 atLeast
bool ShareWaitAtLeast(const std::atomic<int>& value, int atLeast)
{
    for (int waited = 0; waited <= kShareWaitMs; waited += kPollMs * 5)
    {
        if (value.load() >= atLeast)
            return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(kPollMs * 5));
    }
    return false;
}

//最多等 kShareWaitMs 毫秒，直到 *value 等于 wanted
bool ShareWaitEquals(const std::atomic<int>& value, int wanted)
{
    for (int waited = 0; waited <= kShareWaitMs; waited += kPollMs * 5)
    {
        if (value.load() == wanted)
            return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(kPollMs * 5));
    }
    return false;
}

//发一条共享请求并取应答
screenshare::ShareResult ShareRequest(LiveFolderSide& side, uint16_t cmd, const std::string& body)
{
    side.SendSignal(cmd, body);
    std::string resp;
    REQUIRE(side.WaitFrame(cmd, resp, kWaitSignalMs));
    screenshare::ShareResult r;
    REQUIRE(screenshare::ShareProtocolClient::parseResult(resp, r));
    return r;
}

//取一帧 1726
screenshare::ShareRelayInfo ShareRelay(LiveFolderSide& side)
{
    std::string body;
    REQUIRE(side.WaitFrame(cmd_screenshare_relay_info, body, kWaitSignalMs));
    screenshare::ShareRelayInfo info;
    REQUIRE(screenshare::ShareProtocolClient::parseRelayInfo(body, info));
    return info;
}

//取 A 的 1725，直到快照里 B 一项带 p2psessionid（或等到超时）；返回那一项
screenshare::ShareMember ShareWaitP2PMember(LiveFolderSide& a, int64_t viewerId)
{
    std::string body;
    while (a.WaitFrame(cmd_screenshare_state_notify, body, kWaitSignalMs))
    {
        screenshare::ShareStateNotify n;
        if (!screenshare::ShareProtocolClient::parseStateNotify(body, n))
            continue;
        for (size_t i = 0; i < n.m_members.size(); ++i)
        {
            if (n.m_members[i].m_userId == viewerId && !n.m_members[i].m_p2pSessionId.empty())
                return n.m_members[i];
        }
    }
    return screenshare::ShareMember();
}

//核对 B 收到的最新一帧：尺寸正好、中心与最后一行都是红色
void CheckRedFrame(const screenshare::ShareViewerReceiver& receiver)
{
    std::vector<uint8_t> bgra;
    int w = 0;
    int h = 0;
    uint64_t serial = 0;
    REQUIRE(receiver.GetLatestFrame(bgra, w, h, serial));
    CHECK(w == kSharePW);
    CHECK(h == kSharePH);
    const uint8_t* center = &bgra[(static_cast<size_t>(h / 2) * w + w / 2) * kShareBpp];
    CHECK(abs(center[2] - 255) <= kShareColorTol);
    CHECK(center[1] <= kShareColorTol);
    const uint8_t* lastRow = &bgra[(static_cast<size_t>(h - 1) * w + w / 2) * kShareBpp];
    CHECK(abs(lastRow[2] - 255) <= kShareColorTol);
}

/**
 *  一场单聊共享：A 发起并推流，B 加入并经 P2P 收流。
 *  @param forceRelay   两端引擎是否强制中转（不打洞）
 *  @param expectedPath 期望的链路路径，取值见 p2p_link_path
 *  @param dropMidway   true 时在看到画面之后由 A 撤下 P2P 通道，核对 B 改连房间中转后继续收到画面
 *  @param logTag       日志目录名
 */
void RunShareOverP2P(bool forceRelay, int expectedPath, bool dropMidway, const wchar_t* logTag)
{
    const std::wstring logParent = MakeLiveTempDir(L"share");
    CIULog::Init(true, false, (logParent + L"\\" + logTag).c_str());
    {
        LiveFolderSide a;
        LiveFolderSide b;
        a.Start(kAccountA, true, true);
        b.Start(kAccountB, true, true);
        a.m_engine.SetForceRelay(forceRelay);
        b.m_engine.SetForceRelay(forceRelay);
        const int64_t userB = b.m_client.UserId();

        //上一轮没结束的共享先结束
        a.SendSignal(cmd_screenshare_query, screenshare::ShareProtocolClient::buildQuery(conversation_single, userB));
        std::string body;
        REQUIRE(a.WaitFrame(cmd_screenshare_query, body, kWaitSignalMs));
        screenshare::ShareQueryResult q;
        REQUIRE(screenshare::ShareProtocolClient::parseQueryResult(body, q));
        if (q.m_exists)
            ShareRequest(a, cmd_screenshare_stop, screenshare::ShareProtocolClient::buildRoomRequest(q.m_roomId));

        //A 发起并推流
        screenshare::ShareResult started = ShareRequest(
            a, cmd_screenshare_start, screenshare::ShareProtocolClient::buildStart(conversation_single, userB, false, false,
                                                                                   false, std::string(), false, true,
                                                                                   kShareTitle));
        REQUIRE(started.m_code == 0);
        P2PHostEvents hostEvents;
        P2PAnnotRecorder annot;
        screenshare::ShareHostStreamer streamer;
        streamer.SetAnnotationSink(&annot);
        screenshare::ShareSourceSpec spec;
        REQUIRE(streamer.Start(ShareRelay(a), a.ServerPubKey(), spec, new P2PSolidCapturer(), &hostEvents));

        //B 加入：1726 与 A 收到的 1725 里 B 一项带同一个 P2P 会话号
        screenshare::ShareResult joined = ShareRequest(
            b, cmd_screenshare_join, screenshare::ShareProtocolClient::buildRoomRequest(started.m_roomId));
        REQUIRE(joined.m_code == 0);
        const screenshare::ShareRelayInfo relayB = ShareRelay(b);
        INFO("1726 to B p2p session: " << relayB.m_p2pSessionId);
        REQUIRE(!relayB.m_p2pSessionId.empty());
        const screenshare::ShareMember memberB = ShareWaitP2PMember(a, userB);
        CHECK(memberB.m_p2pSessionId == relayB.m_p2pSessionId);
        CHECK(memberB.m_seat == relayB.m_seat);

        //两端按「房间号#座位号」认领链路槽：A 交给推流管线，B 交给收流管线
        const std::string feature = screenshare::ShareP2PFeatureId(started.m_roomId, relayB.m_seat);
        std::shared_ptr<P2PLinkSlot> slotA = a.m_shareBroker2.ClaimSlot(feature);
        streamer.SetP2PViewer(relayB.m_seat, slotA);
        P2PViewerEvents viewerEvents;
        screenshare::ShareViewerReceiver receiver;
        REQUIRE(receiver.Start(relayB, b.ServerPubKey(), &viewerEvents, b.m_shareBroker2.ClaimSlot(feature)));

        //B 经 P2P 收到画面
        REQUIRE(ShareWaitAtLeast(viewerEvents.m_frames, 1));
        CHECK(receiver.Path() == screenshare::share_viewer_path_p2p);
        CHECK(streamer.IsP2PViewerOpen());
        CHECK(SlotLinkPath(slotA) == expectedPath);
        //观看窗与发起人控制条的「P2P」徽章按它们判断（2026-10-04 起）
        CHECK(receiver.LinkPath() == expectedPath);
        CHECK(streamer.P2PViewerLinkPath() == expectedPath);
        CheckRedFrame(receiver);
        CHECK(viewerEvents.m_relayEvents.load() == 0);

        //B 发一帧标注，帧头座位号故意写错：A 收到时已改写为 B 的座位
        std::vector<std::string> bodies;
        const std::string annotPayload = "annot";
        screenshare::ShareFraming::fragmentFrame(screenshare_track_annotation, kShareFakeSeat, false, 0,
                                                 annotPayload.data(), annotPayload.size(),
                                                 screenshare::kShareFragmentPayloadBytes, bodies);
        REQUIRE(bodies.size() == 1);
        REQUIRE(receiver.SendRoomBody(bodies[0]));
        REQUIRE(ShareWaitAtLeast(annot.m_count, 1));
        CHECK(annot.m_lastSeat.load() == relayB.m_seat);

        //暂停与继续：B 经 P2P 收到状态
        streamer.SetPaused(true);
        CHECK(ShareWaitEquals(viewerEvents.m_lastState, screenshare_source_paused));
        streamer.SetPaused(false);
        CHECK(ShareWaitEquals(viewerEvents.m_lastState, screenshare_source_sharing));

        if (dropMidway)
        {
            //A 撤下 P2P 通道并关闭链路：B 改连房间中转，服务端在 B 报到时请求关键帧，B 继续收到画面
            streamer.ClearP2PViewer(relayB.m_seat);
            CHECK_FALSE(streamer.IsP2PViewerOpen());
            const int framesBefore = viewerEvents.m_frames.load();
            for (int waited = 0; waited <= kShareWaitMs && receiver.Path() != screenshare::share_viewer_path_relay;
                 waited += kPollMs * 5)
                std::this_thread::sleep_for(std::chrono::milliseconds(kPollMs * 5));
            CHECK(receiver.Path() == screenshare::share_viewer_path_relay);
            CHECK(ShareWaitAtLeast(viewerEvents.m_frames, framesBefore + 1));
            CheckRedFrame(receiver);
        }

        receiver.Stop();
        streamer.Stop();
        ShareRequest(a, cmd_screenshare_stop, screenshare::ShareProtocolClient::buildRoomRequest(started.m_roomId));
        a.Shutdown();
        b.Shutdown();
    }
    CIULog::Uninit();
}

} // namespace

TEST_CASE("SSPL1 单聊共享经局域网直连的 P2P：画面、标注、状态都经 P2P 送达，标注的座位号由发起人改写", "[.live][sharep2plive]")
{
    RunShareOverP2P(false, p2p::p2p_link_path_lan, false, L"sspl1");
}

TEST_CASE("SSPL2 单聊共享强制中转：画面、标注、状态经 imgserver 的 P2P 中转送达", "[.live][sharep2plive]")
{
    RunShareOverP2P(true, p2p::p2p_link_path_relay, false, L"sspl2");
}

TEST_CASE("SSPL3 单聊共享 P2P 中途断开：观看者改连房间中转后继续收到画面", "[.live][sharep2plive]")
{
    RunShareOverP2P(false, p2p::p2p_link_path_lan, true, L"sspl3");
}

// ======================= 2026-10-04 单聊单文件 P2P 直传（DFL1～DFL5） =======================

namespace {

//DFL：直传的文件大小（字节）：比单个分片大，能走满多条数据通道
const size_t kDirectFileBytes = 3 * 1024 * 1024 + 123;
//DFL：直传的文件名
const wchar_t* const kDirectFileNameW = L"p2pfile.bin";
const char* const kDirectFileName = "p2pfile.bin";
//DFL：等「不该出现的推送」的时长（毫秒）
const int kNoRecordWaitMs = 1500;
//DFL：MD5 的字节数
const int kDflMd5Bytes = 16;

//一个文件内容的 MD5（32 位小写十六进制）
std::string Md5OfLiveFile(const std::wstring& path)
{
    const std::string data = ReadLiveFile(path);
    MD5_CTX ctx;
    MD5Init(&ctx);
    if (!data.empty())
        MD5Update(&ctx, reinterpret_cast<const unsigned char*>(data.data()), (unsigned int)data.size());
    unsigned char digest[kDflMd5Bytes];
    MD5Final(digest, &ctx);
    static const char kHex[] = "0123456789abcdef";
    std::string hex;
    for (int i = 0; i < kDflMd5Bytes; ++i)
    {
        hex += kHex[digest[i] >> 4];
        hex += kHex[digest[i] & 0x0F];
    }
    return hex;
}

//从 1504 包体里取出属于这次直传的文件消息内容；content 可能是 JSON 字符串，也可能已经是对象
bool DirectRecordOf(const std::string& body, const std::string& sessionId, net::DirectFileContent& out)
{
    Json::CharReaderBuilder builder;
    std::unique_ptr<Json::CharReader> reader(builder.newCharReader());
    Json::Value root;
    JSONCPP_STRING errs;
    if (!reader->parse(body.c_str(), body.c_str() + body.size(), &root, &errs) || !root.isObject())
        return false;
    if (root["msgtype"].asInt() != message_type_file)
        return false;
    std::string content;
    if (root["content"].isString())
    {
        content = root["content"].asString();
    }
    else
    {
        Json::StreamWriterBuilder w;
        w["indentation"] = "";
        content = Json::writeString(w, root["content"]);
    }
    if (!net::FolderProtocolClient::parseDirectFileContent(content, out))
        return false;
    return out.m_sessionId == sessionId;
}

//最多等 timeoutMs 毫秒，取一条属于这次直传的文件消息（跳过其它 1504）
bool WaitDirectRecord(LiveFolderSide& side, const std::string& sessionId, net::DirectFileContent& out, int timeoutMs)
{
    const ULONGLONG deadline = ::GetTickCount64() + (ULONGLONG)timeoutMs;
    while (::GetTickCount64() < deadline)
    {
        std::string body;
        const int left = (int)(deadline - ::GetTickCount64());
        if (!side.WaitFrame(cmd_message_notify, body, left > 0 ? left : 1))
            return false;
        if (DirectRecordOf(body, sessionId, out))
            return true;
    }
    return false;
}

//A 向 B 发起直传，返回 1810 的应答
net::FolderInviteResponse SendDirectInvite(LiveFolderSide& a, LiveFolderSide& b, int64_t fileSize)
{
    const std::string invite = net::FolderProtocolClient::buildFileInviteRequest(b.m_client.UserId(), kDirectFileName, fileSize);
    REQUIRE(!invite.empty());
    a.SendSignal(cmd_folder_invite, invite);
    std::string body;
    REQUIRE(a.WaitFrame(cmd_folder_invite, body, kWaitSignalMs));
    net::FolderInviteResponse resp;
    REQUIRE(net::FolderProtocolClient::parseInviteResponse(body, resp));
    INFO("direct invite response: " << body);
    return resp;
}

//B 取 1811，核对是单文件直传
void WaitDirectInviteNotify(LiveFolderSide& b, const std::string& sessionId, int64_t fileSize)
{
    std::string body;
    REQUIRE(b.WaitFrame(cmd_folder_invite_notify, body, kWaitSignalMs));
    net::FolderInviteNotify notify;
    REQUIRE(net::FolderProtocolClient::parseInviteNotify(body, notify));
    INFO("1811: " << body);
    CHECK(notify.m_sessionId == sessionId);
    CHECK(notify.m_kind == net::folder_kind_file);
    CHECK(notify.m_folderName == kDirectFileName);
    CHECK(notify.m_totalFiles == 1);
    CHECK(notify.m_totalDirs == 0);
    CHECK(notify.m_totalSize == fileSize);
}

//取一帧状态推送（1815），核对会话号并返回状态
int WaitFolderState(LiveFolderSide& side, const std::string& sessionId)
{
    std::string body;
    REQUIRE(side.WaitFrame(cmd_folder_state_notify, body, kWaitSignalMs));
    net::FolderStateNotify n;
    REQUIRE(net::FolderProtocolClient::parseStateNotify(body, n));
    INFO("1815: " << body);
    CHECK(n.m_sessionId == sessionId);
    return n.m_state;
}

//一次完整的直传：B 接收，A 用单文件清单发、B 用直传扩展点收，核对落地文件、链路路径与双方的文件消息
void RunDirectTransfer(bool forceRelay, int expectedPath, const wchar_t* logTag)
{
    const std::wstring srcDir = MakeLiveTempDir(L"dsrc");
    const std::wstring file = srcDir + L"\\" + kDirectFileNameW;
    WriteLiveFile(file, kDirectFileBytes, 7);
    const std::wstring dstBase = MakeLiveTempDir(L"ddst");
    CIULog::Init(true, false, (srcDir + L"\\" + logTag).c_str());
    {
        LiveFolderSide a;
        LiveFolderSide b;
        a.Start(kAccountA, true, true);
        b.Start(kAccountB, true, true);
        a.m_engine.SetForceRelay(forceRelay);
        b.m_engine.SetForceRelay(forceRelay);

        const net::FolderInviteResponse resp = SendDirectInvite(a, b, (int64_t)kDirectFileBytes);
        REQUIRE(resp.m_code == 0);
        const std::string sessionId = resp.m_sessionId;
        WaitDirectInviteNotify(b, sessionId, (int64_t)kDirectFileBytes);
        b.SendSignal(cmd_folder_response, net::FolderProtocolClient::buildResponse(sessionId, true));
        std::string body;
        REQUIRE(b.WaitFrame(cmd_folder_response, body, kWaitSignalMs));

        net::FolderRelayInfo infoA;
        net::FolderRelayInfo infoB;
        WaitRelayInfo(a, b, sessionId, infoA, infoB);
        //直传一定建了用途 file 的 P2P 会话
        REQUIRE(!infoA.m_p2pSessionId.empty());

        std::shared_ptr<FolderP2PTransportFactory> fa = a.m_fileBroker.CreateFactory(
            infoA.m_sessionId, infoA.m_relayIp, infoA.m_relayPort, infoA.m_token, a.ServerPubKey());
        std::shared_ptr<FolderP2PTransportFactory> fb = b.m_fileBroker.CreateFactory(
            infoB.m_sessionId, infoB.m_relayIp, infoB.m_relayPort, infoB.m_token, b.ServerPubKey());
        //直传与正式代码一样不允许退回 fileserver 中转
        fa->SetRelayFallback(false);
        fb->SetRelayFallback(false);

        foldertransfer::DirectFileSendHooks sendHooks;
        FolderSendSessionParams sp;
        sp.m_relayIp = infoA.m_relayIp;
        sp.m_relayPort = infoA.m_relayPort;
        sp.m_relayToken = infoA.m_token;
        sp.m_dataLanes = infoA.m_lanes;
        REQUIRE(foldertransfer::BuildSingleFileManifest(file, sp.m_rootPath, sp.m_manifest));
        sp.m_serverPubKey = a.ServerPubKey();
        sp.m_hooks = &sendHooks;
        sp.m_transportFactory = fa.get();

        const std::wstring saveDir = foldertransfer::DirectFileSaveDir(dstBase, sessionId);
        foldertransfer::DirectFileRecvHooks hooks(saveDir);
        FolderRecvSessionParams rp;
        rp.m_sessionId = infoB.m_sessionId;
        rp.m_relayIp = infoB.m_relayIp;
        rp.m_relayPort = infoB.m_relayPort;
        rp.m_relayToken = infoB.m_token;
        rp.m_dataLanes = infoB.m_lanes;
        rp.m_saveParentDir = saveDir;
        rp.m_serverPubKey = b.ServerPubKey();
        rp.m_hooks = &hooks;
        rp.m_transportFactory = fb.get();

        LiveSendRecorder sendRec;
        LiveRecvRecorder recvRec;
        std::shared_ptr<FolderReceiveState> state(new FolderReceiveState());
        FolderSendSession sender;
        FolderRecvSession receiver;
        REQUIRE(receiver.Start(rp, state, &recvRec));
        REQUIRE(sender.Start(sp, &sendRec));
        for (int waited = 0; waited <= kWaitTransferMs; waited += kPollMs)
        {
            if ((sendRec.m_completed > 0 || sendRec.m_failed > 0) && (recvRec.m_completed > 0 || recvRec.m_failed > 0))
                break;
            std::this_thread::sleep_for(std::chrono::milliseconds(kPollMs));
        }
        INFO("send failed " << (int)sendRec.m_failed << " code " << (int)sendRec.m_errcode << ", recv failed "
             << (int)recvRec.m_failed << " code " << (int)recvRec.m_errcode);
        REQUIRE(sendRec.m_completed == 1);
        REQUIRE(recvRec.m_completed == 1);
        //文件直接落在 direct-<会话号> 目录下，内容一致
        CHECK(ReadLiveFile(saveDir + kDirectFileNameW) == ReadLiveFile(file));
        CHECK(fa->LinkPath() == expectedPath);
        CHECK(fb->LinkPath() == expectedPath);
        //两端扩展点报出的 MD5 与文件内容一致
        const std::string md5 = Md5OfLiveFile(file);
        CHECK(sendHooks.Md5() == md5);
        CHECK(hooks.Md5() == md5);

        //两端各发一帧 1814，带链路路径与各自扩展点报出的 MD5
        a.SendSignal(cmd_folder_close,
                     net::FolderProtocolClient::buildClose(sessionId, true, 0, "", fa->LinkPath(), sendHooks.Md5()));
        b.SendSignal(cmd_folder_close,
                     net::FolderProtocolClient::buildClose(sessionId, true, 0, "", fb->LinkPath(), hooks.Md5()));

        //双方各收到一条直传文件消息：结局 8、链路路径、MD5、文件名与大小
        net::DirectFileContent recA;
        net::DirectFileContent recB;
        REQUIRE(WaitDirectRecord(a, sessionId, recA, kWaitSignalMs));
        REQUIRE(WaitDirectRecord(b, sessionId, recB, kWaitSignalMs));
        CHECK(recA.m_result == folder_state_completed);
        CHECK(recA.m_linkPath == expectedPath);
        CHECK(recA.m_md5 == md5);
        CHECK(recB.m_md5 == md5);
        CHECK(recA.m_fileName == kDirectFileName);
        CHECK(recA.m_fileSize == (int64_t)kDirectFileBytes);
        CHECK(recB.m_result == folder_state_completed);
        CHECK(recB.m_linkPath == expectedPath);

        sender.Stop();
        receiver.Stop();
        fa.reset();
        fb.reset();
        a.Shutdown();
        b.Shutdown();
    }
    CIULog::Uninit();
}

} // namespace

TEST_CASE("DFL1 单文件直传：局域网直连传完，双方各收到一条带链路路径与 MD5 的直传文件消息", "[.live][directfilelive]")
{
    RunDirectTransfer(false, p2p::p2p_link_path_lan, L"dfl1");
}

TEST_CASE("DFL2 单文件直传：强制中转时经 imgserver 中转传完，文件消息里的链路路径为 3", "[.live][directfilelive]")
{
    RunDirectTransfer(true, p2p::p2p_link_path_relay, L"dfl2");
}

TEST_CASE("DFL3 单文件直传：对方是不支持直传的旧版客户端，1810 回 622、不建会话", "[.live][directfilelive]")
{
    const std::wstring logDir = MakeLiveTempDir(L"dfl3");
    CIULog::Init(true, false, (logDir + L"\\dfl3").c_str());
    {
        LiveFolderSide a;
        LiveFolderSide b;
        b.SetFileEnabled(false);
        a.Start(kAccountA, true, true);
        b.Start(kAccountB, true, true);
        const net::FolderInviteResponse resp = SendDirectInvite(a, b, (int64_t)kDirectFileBytes);
        CHECK(resp.m_code == error_code_folder_direct_unavailable);
        CHECK(resp.m_sessionId.empty());
        //B 收不到邀请
        std::string body;
        CHECK_FALSE(b.WaitFrame(cmd_folder_invite_notify, body, kNoRecordWaitMs));
        a.Shutdown();
        b.Shutdown();
    }
    CIULog::Uninit();
}

TEST_CASE("DFL4 单文件直传：A 改为离线发送，B 收到状态 9，双方都不留文件消息", "[.live][directfilelive]")
{
    const std::wstring logDir = MakeLiveTempDir(L"dfl4");
    CIULog::Init(true, false, (logDir + L"\\dfl4").c_str());
    {
        LiveFolderSide a;
        LiveFolderSide b;
        a.Start(kAccountA, true, true);
        b.Start(kAccountB, true, true);
        const net::FolderInviteResponse resp = SendDirectInvite(a, b, (int64_t)kDirectFileBytes);
        REQUIRE(resp.m_code == 0);
        const std::string sessionId = resp.m_sessionId;
        WaitDirectInviteNotify(b, sessionId, (int64_t)kDirectFileBytes);

        a.SendSignal(cmd_folder_cancel, net::FolderProtocolClient::buildCancel(sessionId, true));
        std::string body;
        REQUIRE(a.WaitFrame(cmd_folder_cancel, body, kWaitSignalMs));
        net::FolderSessionResponse cancelResp;
        REQUIRE(net::FolderProtocolClient::parseSessionResponse(body, cancelResp));
        CHECK(cancelResp.m_code == 0);
        CHECK(WaitFolderState(b, sessionId) == folder_state_to_offline);

        net::DirectFileContent rec;
        CHECK_FALSE(WaitDirectRecord(a, sessionId, rec, kNoRecordWaitMs));
        CHECK_FALSE(WaitDirectRecord(b, sessionId, rec, kNoRecordWaitMs));
        a.Shutdown();
        b.Shutdown();
    }
    CIULog::Uninit();
}

TEST_CASE("DFL5 单文件直传：B 拒绝，A 收到状态 3，双方各收到一条结局 3 的直传文件消息", "[.live][directfilelive]")
{
    const std::wstring logDir = MakeLiveTempDir(L"dfl5");
    CIULog::Init(true, false, (logDir + L"\\dfl5").c_str());
    {
        LiveFolderSide a;
        LiveFolderSide b;
        a.Start(kAccountA, true, true);
        b.Start(kAccountB, true, true);
        const net::FolderInviteResponse resp = SendDirectInvite(a, b, (int64_t)kDirectFileBytes);
        REQUIRE(resp.m_code == 0);
        const std::string sessionId = resp.m_sessionId;
        WaitDirectInviteNotify(b, sessionId, (int64_t)kDirectFileBytes);

        b.SendSignal(cmd_folder_response, net::FolderProtocolClient::buildResponse(sessionId, false));
        CHECK(WaitFolderState(a, sessionId) == folder_state_rejected);

        net::DirectFileContent recA;
        net::DirectFileContent recB;
        REQUIRE(WaitDirectRecord(a, sessionId, recA, kWaitSignalMs));
        REQUIRE(WaitDirectRecord(b, sessionId, recB, kWaitSignalMs));
        CHECK(recA.m_result == folder_state_rejected);
        CHECK(recB.m_result == folder_state_rejected);
        CHECK(recA.m_fileName == kDirectFileName);
        CHECK(recA.m_linkPath == net::kFolderLinkPathNone);
        a.Shutdown();
        b.Shutdown();
    }
    CIULog::Uninit();
}
