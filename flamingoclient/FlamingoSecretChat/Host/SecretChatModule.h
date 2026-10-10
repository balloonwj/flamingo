/**
 *  密聊模块对象 CSecretChatModule：ISecretChatModule 的实现（docs/密聊方案.md 第 4 节）。
 *
 *  职责：
 *    - 会话表：发起中、待应答（收到的邀请）、同意中、进行中四种阶段，每个会话一份 CSecretCipher；
 *    - 收帧：OnServerFrame 在网络线程上把帧拷贝后投递到隐藏的消息窗口（WM_APP + 0x170），在界面线程上由
 *      ProcessFrame 处理，再回调宿主；
 *    - 发送：文字立即加密发出；图片切块后排队，由 20 毫秒的定时器按「最多 4 帧在途、每秒约 50 帧」的节奏发出，
 *      服务端回 1206（转发过快）时换一个新序号重新加密后重发（旧序号可能已被后面的帧越过，接收方会当成重放丢弃）；
 *    - 防截图：ProtectWindow 打开后每 2 秒核对一次，被改掉就重设。
 *
 *  进程内经 Instance() 取唯一对象；单元测试直接构造，用 ProcessFrame / PumpSend 在测试线程上驱动。
 *
 *  balloonwj@qq.com   2026-10-07
 */
#pragma once

#include <windows.h>
#include <stdint.h>
#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "../SecretChatApi.h"
#include "../Core/SecretCrypto.h"
#include "../Core/SecretEnvelope.h"

namespace secretchat {

//消息窗口收到「有新帧」的私有消息号（开发约定文档的 WM_APP 占用表登记为 0x170）
const UINT kMsgSecretFrameArrived = WM_APP + 0x170;

//图片帧最多同时在途的帧数
const size_t kMaxInflightImageFrames = 4;

//相邻两帧图片之间的最小间隔（毫秒）：每秒约 50 帧，低于服务端每会话每秒 64 帧的上限
const int64_t kImageFrameIntervalMs = 20;

//服务端回 1206 后等多久重发（毫秒）
const int64_t kRetryDelayMs = 300;

//同一帧最多重发的次数，超过即判失败
const int kMaxFrameRetries = 20;

//防截图核对周期（毫秒）
const UINT kProtectCheckIntervalMs = 2000;

//发送节奏定时器的周期（毫秒）
const UINT kPumpIntervalMs = 20;

/**
 *  密聊模块对象。
 */
class CSecretChatModule : public ISecretChatModule
{
public:
    /**
     *  取进程内唯一的模块对象（DLL 导出函数返回它）。
     */
    static CSecretChatModule& Instance();

    CSecretChatModule();
    ~CSecretChatModule();

    //======== ISecretChatModule ========
    virtual bool Init(ISecretChatHost* host, const SecretInitParams* params) override;
    virtual void Shutdown() override;
    virtual uint32_t GetApiVersion() const override;
    virtual void OnLoggedIn() override;
    virtual void OnLoggingOut() override;
    virtual void OnServerFrame(uint16_t cmd, uint32_t seq, const char* body, uint32_t len) override;
    virtual int ProbeProtect() override;
    virtual bool Invite(int64_t peerId) override;
    virtual bool Respond(uint64_t sessionId, bool accept) override;
    virtual bool Cancel(uint64_t sessionId) override;
    virtual bool Close(uint64_t sessionId) override;
    virtual bool SendText(uint64_t sessionId, const wchar_t* text, uint32_t* outMsgId) override;
    virtual bool SendImage(uint64_t sessionId, const void* bytes, uint32_t len, uint32_t width, uint32_t height,
                           uint32_t* outImageId) override;
    virtual int ProtectWindow(HWND hwnd, bool on) override;
    virtual bool GetSafetyCode(uint64_t sessionId, wchar_t* buf, uint32_t chars) override;

    //======== 消息窗口与单元测试共用 ========

    /**
     *  在当前线程上处理一帧服务端帧（消息窗口收到 kMsgSecretFrameArrived 时调用；测试直接调用）。
     */
    void ProcessFrame(uint16_t cmd, uint32_t seq, const std::string& body);

    /**
     *  推进发送：按节奏发出排队的图片帧，重发到期的帧。
     *  @param nowMs 当前时刻（毫秒，单调时钟）
     *  @return 还有没发完或在等重发的帧时返回 true
     */
    bool PumpSend(int64_t nowMs);

    /**
     *  核对全部受保护窗口的防截图设置，被改掉的重设。
     */
    void CheckProtectedWindows();

    /**
     *  当前会话数（含发起中与待应答），供测试使用。
     */
    size_t SessionCount() const;

    /**
     *  测试用：不建消息窗口（测试线程没有消息循环，帧由测试直接经 ProcessFrame 交入）。须在 Init 之前调用。
     */
    void SetHeadlessForTest(bool headless);

private:
    //会话所处的阶段
    enum Phase
    {
        phase_outgoing  = 1,   //本人发起，已拿到会话号，等对方应答
        phase_incoming  = 2,   //收到邀请，等本人决定
        phase_accepting = 3,   //本人已同意，等服务端确认建立
        phase_active    = 4,   //进行中
    };

    //一帧在途或待重发的密文
    struct InFlight
    {
        int         kind;        //envelope_text 或 envelope_image_chunk
        uint32_t    itemId;      //文字的消息编号或图片编号
        std::string plain;       //明文信封（重发时换新序号重新加密）
        uint32_t    bytes;       //图片块的原始字节数（文字为 0）
        int         retries;     //已重发的次数
        int64_t     retryAtMs;   //0 表示在等服务端应答；大于 0 表示到这个时刻重发

        InFlight()
            : kind(0)
            , itemId(0)
            , bytes(0)
            , retries(0)
            , retryAtMs(0)
        {
        }
    };

    //本人发出的一张图片的进度
    struct OutImage
    {
        uint32_t totalBytes;   //原图字节数
        uint32_t sentBytes;    //已被服务端受理的字节数
        bool     failed;       //是否已判失败

        OutImage()
            : totalBytes(0)
            , sentBytes(0)
            , failed(false)
        {
        }
    };

    //一个会话
    struct Session
    {
        uint64_t                       id;              //会话号
        std::string                    idText;          //会话号的十进制文本（参与附加数据）
        int64_t                        peerId;          //对方
        int                            phase;           //阶段，取值见 Phase
        std::shared_ptr<CSecretCipher> cipher;          //加密状态：发起方沿用发邀请时生成的那一对密钥，不可为空
        std::string                    peerPubkey;      //收到邀请时对方的公钥（同意时派生用）
        int                            peerProtect;     //对方的防截图级别
        uint32_t                       txSeq;           //本方最近一帧的序号
        uint32_t                       rxSeq;           //已接收的对方最大序号
        uint32_t                       nextMsgId;       //下一条文字的编号
        uint32_t                       nextImageId;     //下一张图片的编号
        std::map<uint32_t, InFlight>   inflight;        //在途或待重发的帧，键为发出时的序号
        std::deque<SecretEnvelope>     queue;           //排队待发的图片块
        int64_t                        lastImageSendMs; //上一帧图片的发出时刻
        std::map<uint32_t, OutImage>   outImages;       //本人发出的图片进度
        CImageAssembler                assembler;       //对方发来的图片拼图

        Session()
            : id(0)
            , peerId(0)
            , phase(0)
            , cipher(new CSecretCipher())
            , peerProtect(0)
            , txSeq(0)
            , rxSeq(0)
            , nextMsgId(1)
            , nextImageId(1)
            , lastImageSendMs(0)
        {
        }
    };

    //发出邀请、还没拿到会话号时暂存的密钥对，键为 2602 请求的帧 seq
    struct PendingInvite
    {
        int64_t                        peerId;   //被邀请的人
        std::shared_ptr<CSecretCipher> cipher;   //已生成的密钥对

        PendingInvite()
            : peerId(0)
        {
        }
    };

    typedef std::map<uint64_t, std::shared_ptr<Session> > SessionMap;

private:
    //各命令的处理（界面线程）
    void OnRegisterResponse(const std::string& body);
    void OnInviteResponse(uint32_t seq, const std::string& body);
    void OnInviteNotify(const std::string& body);
    void OnRespondResponse(const std::string& body);
    void OnStateNotify(const std::string& body);
    void OnMessageResponse(const std::string& body);
    void OnMessageNotify(const std::string& body);

    //把一个信封加密成一帧发出并记入在途表；返回是否已交给宿主发出
    bool SendEnvelope(Session& s, const std::string& plain, int kind, uint32_t itemId, uint32_t bytes, int retries);

    //结束一个会话：把在收与在发的图片报失败（notify 为 true 时），清零密钥并从表里删除
    void EndSession(uint64_t sessionId, bool notify);

    //本人发出的一帧失败（非 1206 的错误码、重发次数用尽、发送失败）
    void FailFrame(Session& s, const InFlight& f, int code);

    //报到：按当前已知的防截图级别发 2601
    void SendRegister();

    //发一条 2600 段命令；返回帧 seq，0 表示没发出
    uint32_t Send(uint16_t cmd, const std::string& body);

    //确保发送节奏定时器在跑（有排队或待重发的帧时）
    void EnsurePumpTimer();

    //消息窗口
    bool CreateMessageWindow();
    void DestroyMessageWindow();
    static LRESULT CALLBACK MessageWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

    //UTF-8 与 UTF-16 互转
    static std::string ToUtf8(const wchar_t* text);
    static std::wstring FromUtf8(const std::string& text);

    //当前的单调时钟（毫秒）
    static int64_t NowMs();

private:
    ISecretChatHost*                       m_host;            //宿主（借用）；Init 前与 Shutdown 后为 NULL
    bool                                   m_headless;        //测试模式：不建消息窗口
    HWND                                   m_hwnd;            //隐藏的消息窗口；只在界面线程读写
    std::mutex                             m_wndMutex;        //保护 m_postTarget（网络线程读取）
    HWND                                   m_postTarget;      //网络线程投递帧的目标窗口；Shutdown 时置空
    bool                                   m_loggedIn;        //是否已登录
    bool                                   m_registered;      //2601 是否已成功
    bool                                   m_enabled;         //本企业是否开启密聊（2601 应答）
    int                                    m_reportedProtect; //最近一次报到的防截图级别
    bool                                   m_pumpRunning;     //发送节奏定时器是否在跑
    SessionMap                             m_sessions;        //全部会话
    std::map<uint32_t, PendingInvite>      m_pendingInvites;  //发出邀请、还没拿到会话号的
    std::map<HWND, int>                    m_protected;       //受保护的窗口及其级别
};

} // namespace secretchat
