/**
 *  FlamingoSecretChat.dll 与宿主 Flamingo.exe 之间的接口约定（单聊密聊，docs/密聊方案.md）。
 *
 *  本 DLL 负责密聊的协议（2600 段）、端到端加密（libsodium：X25519 协商 + XChaCha20-Poly1305）、图片分块收发与重组、
 *  会话状态机与防截图（SetWindowDisplayAffinity）。不带界面、不链接 balloonui：聊天窗的密聊状态、邀请卡片与消息气泡
 *  都由 exe 实现，exe 经 ISecretChatModule 操作密聊，经 ISecretChatHost 接收事件。exe 在登录成功后按企业开关
 *  （features.secretchat）用 LoadLibrary 动态加载它；DLL 不存在或加载失败时 exe 照常运行，只是没有「密聊」按钮。
 *
 *  线程约定：
 *    - OnServerFrame 在 exe 的网络接收线程上调用，DLL 拷贝包体后投递到自己的隐藏窗口，在界面线程上处理；
 *    - 其余 ISecretChatModule 方法只在界面线程上调用；
 *    - ISecretChatHost 的回调除 Log 与 SendSecretCommand 外都在界面线程上发生。
 *
 *  两边都静态链接 CRT、各有独立的堆，因此本文件中的接口只使用纯虚类与基本类型：
 *    - 字符串与字节块一律由调用方持有，只在本次调用期间有效，被调方需要保留时自行拷贝；
 *    - 不传递 STL 对象，不在一侧分配、另一侧释放内存；
 *    - 异常不越过模块边界；ISecretChatModule 的实现对象由 DLL 自己持有（进程内唯一），exe 不负责释放。
 *
 *  修改规则：任何一侧增删接口方法、改动参数或结构体布局，都必须把 kApiVersion 加 1；加载时双方版本号必须完全相同。
 *
 *  典型用法（exe 侧，见 Source/Feature/SecretChat/SecretChatHost.cpp）：
 *      balloonapp::CPluginModuleLoader<secretchat::ISecretChatModule> loader;
 *      if (loader.Load(path, FLAMINGO_SECRETCHAT_FACTORY_NAME, secretchat::kApiVersion) == balloonapp::plugin_load_ok)
 *          loader.GetModule()->Init(&host, &params);
 *
 *  balloonwj@qq.com   2026-10-07
 */
#pragma once

#include <windows.h>
#include <stdint.h>

namespace secretchat {

//接口版本号。exe 与 DLL 编译时都取这一个值；加载时不同即拒绝创建模块。
//  1：2026-10-07 第一版
const uint32_t kApiVersion = 1;

//本 DLL 报到的密聊协议版本（2601 的 version），与服务端 kSecretProtocolVersion 一致
const int kProtocolVersion = 1;

//密聊信令的号段（Protocol.h 的 cmd_secret_* 2601~2609 都落在其中）。exe 据此把收到的帧原样转交本模块，
//代发时也只放行这一段
const uint16_t kSecretCmdFirst = 2600;
const uint16_t kSecretCmdLast = 2699;

//单张图片的字节上限：原图发送、不压缩（用户 2026-10-07 确认），超过时 SendImage 返回 false
const uint32_t kMaxImageBytes = 10 * 1024 * 1024;

//一条文字消息的字符上限，与普通聊天文字消息一致
const uint32_t kMaxTextChars = 4000;

//DLL 文件名，与 exe 放在同一目录
#define FLAMINGO_SECRETCHAT_DLL_NAME       L"FlamingoSecretChat.dll"

//导出的工厂函数名（GetProcAddress 使用 ANSI 名称）
#define FLAMINGO_SECRETCHAT_FACTORY_NAME   "FlamingoSecretChat_Create"

//DLL 经 ISecretChatHost::Log 写日志时的级别
enum SecretLogLevel
{
    secret_log_info    = 1,   //普通信息：模块初始化、会话建立与结束等关键路径
    secret_log_warning = 2,   //警告：可以继续运行的异常情况，如收到无法解密的帧
    secret_log_error   = 3,   //错误：某项操作无法继续，如密钥协商失败
};

//本机对一个窗口能做到的防截图级别，取值与 Protocol.h 的 secret_protect_level 相同
enum CaptureProtectLevel
{
    kProtectNone    = 0,   //设置失败（如 Win7 关闭了桌面合成）：仍可密聊，但双方都会看到提示
    kProtectMonitor = 1,   //WDA_MONITOR：截图里这个窗口显示为一块黑
    kProtectExclude = 2,   //WDA_EXCLUDEFROMCAPTURE：截图里不出现这个窗口
};

//OnSessionState 的 state，前六个与 Protocol.h 的 secret_state 取值相同，另加本机才有的一种
enum SessionStateEvent
{
    session_state_active            = 1,   //已建立：可以收发
    session_state_not_accepted      = 2,   //未接受（被忽略或超时）
    session_state_canceled          = 3,   //发起方撤回了邀请
    session_state_ended             = 4,   //会话在服务端结束（reason 见 Protocol.h 的 secret_end_reason）：本机转为「只能查看」
    session_state_handled_elsewhere = 5,   //邀请已在本人的其他设备上处理
    session_state_peer_protect      = 6,   //对方的防截图级别变了，peerProtect 为新级别
    session_state_local_error       = 100, //本机出错（密钥协商失败、收到无法解密的帧）：本机已结束会话并通知服务端
};

//ISecretChatModule::Init 的参数
struct SecretInitParams
{
    uint32_t       m_cbSize;   //本结构体的字节数，取 sizeof(SecretInitParams)
    const wchar_t* m_appDir;   //exe 所在目录（不带结尾反斜杠）；不可为 NULL
};

/**
 *  判断一个命令号是否属于密聊的号段。
 *  @param cmd 命令号
 *  @return 落在 [kSecretCmdFirst, kSecretCmdLast] 内返回 true
 */
inline bool IsSecretCommand(uint32_t cmd)
{
    return cmd >= kSecretCmdFirst && cmd <= kSecretCmdLast;
}

/**
 *  判断宿主与 DLL 的接口版本是否兼容：必须完全相同。
 */
inline bool IsApiVersionCompatible(uint32_t hostVersion, uint32_t dllVersion)
{
    return hostVersion == dllVersion;
}

/**
 *  exe 实现、交给 DLL 的宿主接口。
 */
class ISecretChatHost
{
public:
    /**
     *  写一行日志（任意线程）。text 为 UTF-8，已含 "[secret] " 前缀；只记会话号与状态，不含聊天内容。
     */
    virtual void Log(int level, const char* text) = 0;

    /**
     *  经主连接发送一条 2600 段命令（任意线程）。
     *  @return 已放入发送队列时返回帧 seq（大于 0）；不放行（号段不对、未连接）时返回 0
     */
    virtual uint32_t SendSecretCommand(uint16_t cmd, const char* json, uint32_t len) = 0;

    /**
     *  本人 userid；未登录时为 0。
     */
    virtual int64_t GetMyUserId() = 0;

    /**
     *  报到（2601）的应答到达。
     *  @param enabled 本企业是否开启密聊
     */
    virtual void OnRegistered(bool enabled) = 0;

    /**
     *  发起邀请（2602）的应答到达。
     *  @param sessionId 成功时为会话号，失败时为 0
     *  @param peerId    被邀请的人
     *  @param remainMs  邀请剩余毫秒数（成功时）
     *  @param code      错误码：0 成功，其余见 Protocol.h 的 error_code_secret_*
     */
    virtual void OnInviteSent(uint64_t sessionId, int64_t peerId, uint32_t remainMs, int code) = 0;

    /**
     *  收到别人的密聊邀请（2603）。
     *  @param peerProtect 发起方报到的防截图级别，取值见 CaptureProtectLevel
     */
    virtual void OnInviteArrived(uint64_t sessionId, int64_t fromId, uint32_t remainMs, int peerProtect) = 0;

    /**
     *  会话状态变化。
     *  @param state       取值见 SessionStateEvent
     *  @param reason      state 为 session_state_ended 时为结束原因（secret_end_reason），其余为 0
     *  @param peerProtect 对方当前的防截图级别（state 为已建立或级别变化时有意义）
     */
    virtual void OnSessionState(uint64_t sessionId, int64_t peerId, int state, int reason, int peerProtect) = 0;

    /**
     *  收到一条密聊文字（已解密）。
     *  @param msgId        对方本次会话内的消息编号
     *  @param text         正文（UTF-16，表情以 /f["id"] 编码）；只在本次调用期间有效
     *  @param serverTimeMs 服务端转发时刻（Unix 毫秒）
     */
    virtual void OnSecretText(uint64_t sessionId, int64_t peerId, uint32_t msgId, const wchar_t* text,
                              int64_t serverTimeMs) = 0;

    /**
     *  对方开始发来一张图片（收到首块时调用一次），宿主据此先插入占位气泡。
     *  @param totalBytes 原图字节数
     *  @param width      宽（像素）
     *  @param height     高（像素）
     */
    virtual void OnSecretImageBegin(uint64_t sessionId, int64_t peerId, uint32_t imageId, uint32_t totalBytes,
                                    uint32_t width, uint32_t height, int64_t serverTimeMs) = 0;

    /**
     *  图片接收进度。
     */
    virtual void OnSecretImageProgress(uint64_t sessionId, uint32_t imageId, uint32_t receivedBytes) = 0;

    /**
     *  一张图片收齐（ok 为 true，bytes 为原图字节，只在本次调用期间有效，宿主自行复制）或接收失败（ok 为 false）。
     */
    virtual void OnSecretImage(uint64_t sessionId, uint32_t imageId, const void* bytes, uint32_t len, bool ok) = 0;

    /**
     *  本人发出的一条文字的结果。
     *  @param msgId 本人本次会话内的消息编号（SendText 返回的那个）
     *  @param code  0 成功送达服务端；其余为错误码
     */
    virtual void OnSendResult(uint64_t sessionId, uint32_t msgId, int code) = 0;

    /**
     *  本人发出的一张图片的进度与结果。
     *  @param sentBytes  已被服务端受理的字节数
     *  @param totalBytes 原图字节数
     *  @param code       0 表示进行中或已完成（sentBytes == totalBytes）；其余为失败的错误码
     */
    virtual void OnImageSendProgress(uint64_t sessionId, uint32_t imageId, uint32_t sentBytes, uint32_t totalBytes,
                                     int code) = 0;

protected:
    ~ISecretChatHost()
    {
    }
};

/**
 *  DLL 实现、交给 exe 的模块接口。除 OnServerFrame 外只在界面线程上调用。
 */
class ISecretChatModule
{
public:
    /**
     *  初始化：登记宿主、初始化 libsodium、建立隐藏的消息窗口。
     *  @return 成功返回 true；失败时宿主应卸载 DLL
     */
    virtual bool Init(ISecretChatHost* host, const SecretInitParams* params) = 0;

    /**
     *  进程退出前调用：结束全部会话、清零密钥、销毁消息窗口。之后不再回调宿主。
     */
    virtual void Shutdown() = 0;

    //返回 DLL 编译时的接口版本号
    virtual uint32_t GetApiVersion() const = 0;

    /**
     *  登录成功（含断线重连）：发 2601 报到协议版本与本机的防截图级别。
     */
    virtual void OnLoggedIn() = 0;

    /**
     *  即将退出登录：结束全部会话（尽量通知服务端）并清零密钥。
     */
    virtual void OnLoggingOut() = 0;

    /**
     *  收到一帧 2600 段的服务端帧（网络接收线程）。DLL 拷贝后投递到界面线程处理。
     */
    virtual void OnServerFrame(uint16_t cmd, uint32_t seq, const char* body, uint32_t len) = 0;

    /**
     *  本机能做到的防截图级别（不改动任何窗口）。宿主在发起前据此决定要不要弹确认框。
     */
    virtual int ProbeProtect() = 0;

    /**
     *  发起密聊：生成临时密钥对并发 2602。结果经 OnInviteSent 回来。
     *  @return 已发出返回 true；未登录、未报到或已有与此人的密聊返回 false
     */
    virtual bool Invite(int64_t peerId) = 0;

    /**
     *  应答别人的邀请：同意时生成密钥对、派生会话密钥并发 2604，建立结果经 OnSessionState 回来；忽略时发 2604 后即结束。
     */
    virtual bool Respond(uint64_t sessionId, bool accept) = 0;

    /**
     *  发起方撤回待应答的邀请（2606）。
     */
    virtual bool Cancel(uint64_t sessionId) = 0;

    /**
     *  本人退出密聊（2607）：通知服务端并清零本机密钥。不回调 OnSessionState。
     */
    virtual bool Close(uint64_t sessionId) = 0;

    /**
     *  发送一条文字。
     *  @param text   正文（UTF-16），不超过 kMaxTextChars 个字符
     *  @param outMsgId [出参] 本条的消息编号，结果经 OnSendResult 回来
     *  @return 已加密发出返回 true；会话不在进行中或正文非法返回 false
     */
    virtual bool SendText(uint64_t sessionId, const wchar_t* text, uint32_t* outMsgId) = 0;

    /**
     *  发送一张图片（原图字节，DLL 内部复制一份），按 48 KB 分块加密发送，进度经 OnImageSendProgress 回来。
     *  @param width / height 图片尺寸（像素），由宿主解码后给出
     *  @param outImageId [出参] 本张图片的编号
     *  @return 已开始发送返回 true；超过 kMaxImageBytes、会话不在进行中返回 false
     */
    virtual bool SendImage(uint64_t sessionId, const void* bytes, uint32_t len, uint32_t width, uint32_t height,
                           uint32_t* outImageId) = 0;

    /**
     *  对一个顶层窗口打开或关闭防截图，并开始或停止每 2 秒一次的核对（被改掉时重设）。
     *  打开时若实际达到的级别与报到时不同，重新发 2601 让服务端转告对方。
     *  @return 打开时为实际达到的级别（CaptureProtectLevel）；关闭时返回 kProtectNone
     */
    virtual int ProtectWindow(HWND hwnd, bool on) = 0;

    /**
     *  取一个进行中会话的安全码：4 组 5 位数字，组间一个空格，例如 "12345 67890 13579 24680"。
     *  @param buf   输出缓冲
     *  @param chars 缓冲字符数（含结尾 0），至少 24
     *  @return 会话已建立且缓冲足够时返回 true
     */
    virtual bool GetSafetyCode(uint64_t sessionId, wchar_t* buf, uint32_t chars) = 0;

protected:
    ~ISecretChatModule()
    {
    }
};

} // namespace secretchat

//导出的工厂函数：版本兼容时返回模块对象（归 DLL 所有），否则返回 NULL
extern "C" secretchat::ISecretChatModule* __cdecl FlamingoSecretChat_Create(uint32_t apiVersion);
