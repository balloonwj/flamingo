/**
 *  FlamingoMedia.dll 与宿主 Flamingo.exe 之间的接口约定。
 *
 *  FlamingoMedia.dll 承载客户端的音视频与屏幕类功能（屏幕共享、屏幕录制，以及后续迁入的
 *  一对一通话与远程桌面）。exe 在进程启动时用 LoadLibrary 动态加载它，经唯一的导出函数
 *  FlamingoMedia_Create 取得 IMediaModule；exe 自己实现 IMediaHost 交给 DLL，DLL 经它写日志、
 *  经主连接发送信令。加载失败时 exe 照常运行，只是音视频类功能不可用。
 *
 *  两边都静态链接 CRT、各有独立的堆，因此本文件中的接口只使用纯虚类与基本类型：
 *    - 字符串一律由调用方持有，只在本次调用期间有效，被调方需要保留时自行拷贝；
 *    - 不传递 STL 对象，不在一侧分配、另一侧释放内存；
 *    - IMediaModule 的实现对象由 DLL 自己持有（进程内唯一），exe 不负责释放。
 *
 *  修改规则：任何一侧增删接口方法、改动参数或结构体布局，都必须把 kApiVersion 加 1。
 *  加载时双方版本号必须完全相同（见 IsApiVersionCompatible），不做兼容旧版本的处理。
 *
 *  典型用法（exe 侧，详见 Source/App/MediaModuleLoader.h）：
 *
 *      CMediaModuleLoader loader;
 *      if (loader.Load(path, flamingomedia::kApiVersion) == media_load_ok)
 *          loader.GetModule()->Init(&host, &params);
 *
 *  balloonwj@qq.com   2026-10-02
 */
#pragma once

#include <windows.h>
#include <stdint.h>

//FlamingoP2P.dll 的服务接口（FlamingoP2P/FlamingoP2PApi.h）：IMediaHost::GetP2PService 只传它的指针，这里不必带进整个头文件
namespace p2p {
class IP2PService;
}

//FlamingoCaption.dll 的模块接口（FlamingoCaption/CaptionApi.h）：IMediaHost::GetCaptionService 只传它的指针，同样不带进整个头文件
namespace caption {
class ICaptionModule;
}

namespace flamingomedia {

//接口版本号。exe 与 DLL 编译时都取这一个值；加载时 exe 把它传给 FlamingoMedia_Create，
//DLL 发现与自己的值不同就拒绝创建模块。
//  1：第 1a 步，模块生命周期、信令入口、功能查询。
//  2：第 1c 步，迁入一对一通话：模块新增 OnLogout / StartCall / RunDebugEntry，
//     宿主新增 GetSelfUserId / IsSoundMuted / AppendCallTip / GetMainWindow。
//  3：第 1d 步，迁入远程桌面：模块新增 StartRemoteDesktop。
//  4：第 2 期，屏幕共享：模块新增 StartScreenShare / JoinScreenShare / QueryScreenShare，宿主新增 OnScreenShareStatus。
//  5：第 4 期，房间语音的说话人提示：宿主新增 GetUserDisplayName。
//  6：第 7 期 7b，独立录屏：模块新增 StartRecording / StopRecording / IsRecording / SetRecordSettings，
//     宿主新增 SendFileToConversation / PickConversationAndSendFile。
//  7：第 12 期，共享密码：宿主的 OnScreenShareStatus 新增 locked 参数。
//  8：第 13 期，强制模式：宿主的 OnScreenShareStatus 与模块的 JoinScreenShare 新增 forced 参数。
//  9：第 15 期，共享标题与描述：宿主的 OnScreenShareStatus 新增 title 参数。
// 10：多语言第 3a 步：宿主新增 Translate / GetUiLanguage，DLL 的界面文字经 exe 的语言模块翻译。
// 11：屏幕共享第 16 期，鼠标效果：模块新增 SetMouseEffectSettings，宿主新增 SaveMouseEffectSettings。
// 12：录制管理（docs/屏幕共享与录制方案.md 第 11.6 节 F9）：模块新增 ShowRecordManager，
//     宿主新增 AddRecording / ListRecordings / UpdateRecordingPath / RemoveRecording，新增结构体 MediaRecordingInfo。
// 13：锁屏（docs/锁屏方案.md 第 4.9 节）：模块新增 SetScreenLocked。
// 14：语音字幕（docs/语音字幕方案.md 第 4 节）：宿主新增 IsCaptionAvailable / GetCaptionService，
//     模块新增 OnCaptionAvailabilityChanged。
// 15：屏幕共享第 25 期，分享链接（docs/屏幕共享与录制方案.md 第 12.4 节）：模块新增 JoinScreenShareByLink，
//     宿主新增 PickConversationsAndSendText。
// 16：屏幕共享第 27 期，云端录制（docs/屏幕共享与录制方案.md 第 12.6 节）：宿主新增 IsCloudRecordEnabled、
//     StartCloudTransfer / CancelCloudTransfer、PickConversations，新增结构体 MediaCloudTransfer、MediaConversationRef；
//     模块新增 OnCloudTransferProgress / OnCloudTransferFinished、HasCloudRecordWork、OpenCloudRecord、OnConversationsPicked。
// 17：屏幕共享第 14 节（docs/屏幕共享与录制方案.md 第 14.1、14.4 节，2026-10-07）：宿主新增 SendImageToConversation
//     （标注「保存为图片」后以图片消息发到本会话）；模块新增 ConfirmCloudRecordExit（退出、登出、关机前询问未完成的
//     云端录像），新增枚举 MediaCloudExitKind、MediaCloudExitChoice。
// 18：屏幕共享清晰度（docs/远程桌面H264与共享画质提升方案.md 4.2，2026-10-15）：模块新增 SetShareQuality。
const uint32_t kApiVersion = 18;

//DLL 文件名，与 exe 放在同一目录。
#define FLAMINGO_MEDIA_DLL_NAME     L"FlamingoMedia.dll"

//导出的工厂函数名（GetProcAddress 使用 ANSI 名称）。
#define FLAMINGO_MEDIA_FACTORY_NAME "FlamingoMedia_Create"

//DLL 经 IMediaHost::Log 写日志时的级别。
enum MediaLogLevel
{
    media_log_info    = 1,   //普通信息：模块初始化、会话开始结束等关键路径
    media_log_warning = 2,   //警告：可以继续运行的异常情况，如收到无法处理的信令
    media_log_error   = 3,   //错误：功能无法继续，如设备打开失败、中转连接失败
};

//IMediaModule::IsFeatureSupported 可以查询的功能。
enum MediaFeature
{
    media_feature_screen_share  = 1,   //屏幕共享：要求 Windows 10 2004（build 19041）及以上
    media_feature_screen_record = 2,   //屏幕录制：系统要求与屏幕共享相同
};

//IMediaModule::StartCall 的通话类型，取值与协议的 call_media（CallProtocolClient.h）一致。
enum MediaCallType
{
    media_call_audio = 1,   //语音通话：只起麦克风与扬声器
    media_call_video = 2,   //视频通话：另起摄像头采集与 H.264 编解码
};

//云端录像文件传输的方向（MediaCloudTransfer::m_direction）
enum MediaCloudTransferDirection
{
    media_cloud_download = 1,   //从 imgserver 下载到本机（合成前取原始帧文件、播放与下载最终文件）
    media_cloud_upload   = 2,   //从本机上传到 imgserver（合成后的 MP4、独立录制保存到云端）
};

//IMediaHost::StartCloudTransfer 的请求（第 27 期）。指针成员只在本次调用期间有效，exe 自己复制
struct MediaCloudTransfer
{
    uint32_t       m_cbSize;      //结构体字节数，填 sizeof(MediaCloudTransfer)
    int            m_direction;   //方向，取值见 MediaCloudTransferDirection
    const char*    m_token;       //文件令牌（1756 应答里的 token，scene 为 file_scene_cloud_record）
    const char*    m_host;        //imgserver 地址（1756 应答里的 host）
    int            m_port;        //imgserver 图片端口（1756 应答里的 port）
    const wchar_t* m_localPath;   //本地文件：下载时为落地路径（目录须已存在，先写临时文件、完成后改名），上传时为要读的文件
    int64_t        m_bytes;       //文件字节数：上传时为本地文件大小，下载时为 1756 应答里的 bytes

    MediaCloudTransfer()
        : m_cbSize(sizeof(MediaCloudTransfer))
        , m_direction(0)
        , m_token(NULL)
        , m_host(NULL)
        , m_port(0)
        , m_localPath(NULL)
        , m_bytes(0)
    {
    }
};

//一个会话（IMediaHost::PickConversations 的结果）
struct MediaConversationRef
{
    int     m_ctype;   //会话类型：1 单聊、2 群聊
    int64_t m_cid;     //会话 id：单聊为对方 userid，群聊为群 id

    MediaConversationRef()
        : m_ctype(0)
        , m_cid(0)
    {
    }
};

//IMediaModule::RunDebugEntry 的结果，与 exe 的命令行调试入口结果一一对应。
enum MediaDebugEntryResult
{
    media_debug_not_handled  = 0,   //命令行里没有本模块的调试开关，exe 继续检查自己的入口或走正常登录
    media_debug_handled      = 1,   //已运行本模块的调试入口，窗口留着，进程进入消息循环
    media_debug_request_exit = 2,   //已运行本模块的调试入口且要求进程立即退出
};

//IMediaModule::Init 的参数。exe 填好后传入，DLL 只在 Init 期间读取。
struct MediaInitParams
{
    uint32_t       m_cbSize;   //本结构体的字节数，取 sizeof(MediaInitParams)；DLL 据此拒绝布局不符的调用方
    const wchar_t* m_appDir;   //exe 所在目录（不带结尾反斜杠），DLL 据此定位皮肤、配置等资源；不可为 NULL
};

//MediaAvatarInfo 里头像字形的容量（含结尾 L'\0'），单位：wchar_t。字形最多 2 个字符，一个 emoji 占 2 个 UTF-16 单元，取 8 足够
const uint32_t kMediaAvatarGlyphChars = 8;
//MediaAvatarInfo 里照片路径的容量（含结尾 L'\0'），单位：wchar_t
const uint32_t kMediaAvatarPathChars = 1024;

//IMediaHost::GetUserAvatar / GetGroupAvatar 的出参：一个用户或一个群的头像该怎么画（docs/音视频通话优化方案.md 2.1）。
//DLL 分配、填好 m_cbSize 后传入，exe 填写其余字段
struct MediaAvatarInfo
{
    uint32_t m_cbSize;                               //本结构体的字节数，DLL 取 sizeof(MediaAvatarInfo)；exe 据此拒绝布局不符的调用方
    wchar_t  m_glyph[kMediaAvatarGlyphChars];        //字形头像上画的字（1~2 个字符，以 L'\0' 结尾）。照片头像也填，照片不在本机时用它占位
    uint32_t m_color;                                //字形头像的底色（COLORREF，即 RGB 宏的值）
    wchar_t  m_imagePath[kMediaAvatarPathChars];     //照片头像在本机的完整路径（以 L'\0' 结尾）；空串表示画字形：没有上传照片，
                                                     //或照片还没下载到本机
};

//MediaRecordingInfo 里录像路径的容量（含结尾 L'\0'），单位：wchar_t
const uint32_t kMediaRecordingPathChars = 1024;

//一条录像记录（docs/屏幕共享与录制方案.md 第 11.6 节 F9）：IMediaHost::AddRecording 的入参、ListRecordings 的出参。
//DLL 分配、填好 m_cbSize 后传入
struct MediaRecordingInfo
{
    uint32_t m_cbSize;                               //本结构体的字节数，DLL 取 sizeof(MediaRecordingInfo)；exe 据此拒绝布局不符的调用方
    int64_t  m_id;                                   //记录 id，由 exe 分配；AddRecording 时忽略
    int64_t  m_durationMs;                           //录像时长（毫秒）
    int64_t  m_sizeBytes;                            //文件字节数（录制完成时的大小）
    int64_t  m_endTimeMs;                            //录制结束时间（Unix 毫秒，UTC）
    wchar_t  m_path[kMediaRecordingPathChars];       //录像文件完整路径（以 L'\0' 结尾）
};

//IMediaHost::OnCallPresence 的会话类型（与 Protocol.h 的 conversation_type 取值一致）
enum MediaPresenceConversation
{
    media_presence_single = 1,   //一对一通话：cid 为对方 userid
    media_presence_group  = 2,   //群通话：cid 为群 id
};

//IMediaHost::OnCallPresence 的媒体类型：主面板据此给「通话中」徽章画电话或摄像机
const int kMediaPresenceVoice = 1;   //语音通话（群语音）
const int kMediaPresenceVideo = 2;   //视频通话（群视频）

/**
 *  宿主接口：由 exe 实现，DLL 调用。
 *
 *  除方法注释里特别注明「只在 UI 线程调用」的之外，所有方法都可能在 DLL 的任意线程上被调用
 *  （日志尤其如此），实现方须保证线程安全。
 *  析构函数受保护：DLL 只使用该对象，不负责销毁它。
 */
class IMediaHost
{
public:
    /**
     *  写一行日志到 exe 的日志文件。
     *  @param level 日志级别，取值见 enum MediaLogLevel；不认识的值按普通信息处理
     *  @param text  日志正文（UTF-8，按代码规范为英文），以 '\0' 结尾；为 NULL 时忽略
     */
    virtual void Log(int level, const char* text) = 0;

    /**
     *  经主连接（chatserver）发送一条信令。
     *  @param cmd  命令号，取值见 Protocol.h 中音视频、屏幕共享段的 cmd_type
     *  @param json 包体（UTF-8 JSON），不要求以 '\0' 结尾
     *  @param len  包体字节数
     *  @return 已放入发送队列返回 true；未连接、命令号不在允许范围或参数非法返回 false
     */
    virtual bool SendSignal(uint16_t cmd, const char* json, uint32_t len) = 0;

    /**
     *  取 chatserver 的公钥，DLL 与 imgserver 中转通道握手时用它做协议加密。
     *  @return 指向 32 字节公钥的指针，在进程生命周期内有效；尚未取得公钥时返回 NULL
     */
    virtual const unsigned char* GetServerPublicKey() = 0;

    /**
     *  取当前登录用户的用户号。可在任意线程调用。
     *  @return 用户号；未登录时返回 0
     */
    virtual int64_t GetSelfUserId() = 0;

    /**
     *  用户是否在设置里关闭了提示音（来电铃声、回铃、忙音等都据此决定是否播放）。可在任意线程调用。
     *  @return 已静音返回 true
     */
    virtual bool IsSoundMuted() = 0;

    /**
     *  在与某个联系人的单聊窗里追加一条通话提示（如「对方已拒绝」「通话时长 01:23」）。
     *  只在 UI 线程调用。该单聊窗没有打开时由 exe 决定是否记入会话。
     *  @param peerUserId 对方用户号
     *  @param tip        提示文字（UTF-16），只在本次调用期间有效；为 NULL 时忽略
     */
    virtual void AppendCallTip(int64_t peerUserId, const wchar_t* tip) = 0;

    /**
     *  取 exe 的主窗口（主面板）句柄。DLL 弹出的窗口以它为居中参照、消息框以它为属主。
     *  只在 UI 线程调用。
     *  @return 主窗口句柄；尚未创建时返回 NULL（调用方按无属主处理）
     */
    virtual HWND GetMainWindow() = 0;

    /**
     *  报告某会话里屏幕共享的状态（有人开始共享、共享结束、查询结果），exe 据此刷新共享卡片的「加入」按钮与群聊横幅。
     *  只在 UI 线程调用。
     *  @param ctype    会话类型：conversation_single / conversation_group
     *  @param cid      从本人角度看的会话 id：单聊为对方 userid，群聊为群 id
     *  @param roomId   房间号（UTF-8，以 '\0' 结尾）；共享结束或查询结果为「没有共享」时为空串
     *  @param hostId   发起人 userid；没有共享时为 0
     *  @param hostName 发起人显示名（UTF-16）；不知道时为空串
     *  @param active   该会话现在是否有进行中的共享
     *  @param locked   加入是否需要密码（第 12 期）；没有共享时为 false
     *  @param forced   是否强制模式（第 13 期）；没有共享时为 false
     *  @param title    共享标题（第 15 期，UTF-16）；没有共享或老服务端不带时为空串，exe 据此在群聊横幅写「正在共享：标题」
     */
    virtual void OnScreenShareStatus(int ctype, int64_t cid, const char* roomId, int64_t hostId,
                                     const wchar_t* hostName, bool active, bool locked, bool forced,
                                     const wchar_t* title) = 0;

    /**
     *  取一个用户的显示名（与会话列表同一口径：通讯录里有备注时带备注；通讯录里没有时为「用户<id>」）。
     *  屏幕共享房间里显示「某某 正在说话」时用。只在 UI 线程调用。
     *  @param userId    用户号
     *  @param buf       [出参] 显示名（UTF-16），写不下时截断，结果始终以 '\0' 结尾
     *  @param bufChars  buf 的容量，单位字符，至少为 1
     *  @return 写入了显示名返回 true；参数非法返回 false
     */
    virtual bool GetUserDisplayName(int64_t userId, wchar_t* buf, uint32_t bufChars) = 0;

    /**
     *  把一个本地文件发到指定会话（录屏完成提示的「发送给 某某」）：exe 打开该会话的聊天窗，按普通文件消息发送。
     *  只在 UI 线程调用。
     *  @param ctype 会话类型：conversation_single / conversation_group
     *  @param cid   会话 id：单聊为对方 userid，群聊为群 id
     *  @param path  文件的完整路径（UTF-16），只在本次调用期间有效
     *  @return 已交给聊天窗发送返回 true；没有登录、会话不存在、文件不存在返回 false
     */
    virtual bool SendFileToConversation(int ctype, int64_t cid, const wchar_t* path) = 0;

    /**
     *  请用户选一个会话，再把本地文件发过去（从主面板、托盘或快捷键发起的录屏，完成提示的「发送到会话…」）。
     *  exe 弹出它的会话选择窗口；用户取消时什么也不做。只在 UI 线程调用。
     *  @param path 文件的完整路径（UTF-16），只在本次调用期间有效
     *  @return 已弹出选择窗口返回 true；没有登录或文件不存在返回 false
     */
    virtual bool PickConversationAndSendFile(const wchar_t* path) = 0;

    /**
     *  查界面文字的译文（多语言第 3a 步）。DLL 里的 TR / TRC / TRF 经它转交给 exe 的语言模块，整个进程只有
     *  一份语言状态。可在任意线程调用。
     *  @param context 上下文（与 TRC 的第一个参数相同），可为 NULL
     *  @param source  简体中文原文，以 '\0' 结尾；为 NULL 时返回 NULL
     *  @return 当前语言的译文。与本文件「字符串只在本次调用期间有效」的约定不同，返回的指针在进程生命周期内
     *          都有效（指向 exe 加载后不再卸载的语言包，或就是 source 本身）；没有译文时返回 source
     */
    virtual const wchar_t* Translate(const wchar_t* context, const wchar_t* source) = 0;

    /**
     *  取 exe 当前生效的界面语言与界面字体（多语言第 3a 步）。DLL 在 Init 里调用一次。可在任意线程调用。
     *  @param langBuf   [出参] 语言代码，如 L"zh-CN"、L"en-US"；写不下时截断，结果始终以 '\0' 结尾
     *  @param langChars langBuf 的容量，单位字符，至少为 1
     *  @param fontBuf   [出参] 界面字体名，如 L"Microsoft YaHei"；写不下时截断，结果始终以 '\0' 结尾
     *  @param fontChars fontBuf 的容量，单位字符，至少为 1
     *  @return 写入成功返回 true；参数非法返回 false
     */
    virtual bool GetUiLanguage(wchar_t* langBuf, uint32_t langChars, wchar_t* fontBuf, uint32_t fontChars) = 0;

    /**
     *  保存鼠标效果的设置（屏幕共享第 16 期）：用户在共享控制条（16d 起还有录屏条）的菜单里改了设置后调用，
     *  exe 把它写进当前账号的配置，并经 IMediaModule::SetMouseEffectSettings 推回模块，共享与录屏用同一份设置。
     *  @param settingsText 设置文本（ASCII，格式见 SerializeMouseEffectSettings），只在本次调用期间有效；为 NULL 时按空串处理
     */
    virtual void SaveMouseEffectSettings(const char* settingsText) = 0;

    /**
     *  语音消息：录音中的音量，约每 100 毫秒一次（录音条上的波形）。在 UI 线程上调用。
     *  @param level 0~255
     */
    virtual void OnVoiceRecordLevel(int level) = 0;

    //语音消息：已录满 2 分钟，录音停在这里等用户确认发送或取消；只通知一次。在 UI 线程上调用
    virtual void OnVoiceRecordLimitReached() = 0;

    /**
     *  语音消息：播放进度，约每 100 毫秒一次。在 UI 线程上调用。
     *  @param token    IMediaModule::PlayVoice 时传入的编号
     *  @param playedMs 已播放的毫秒数
     *  @param totalMs  总毫秒数
     */
    virtual void OnVoicePlayProgress(uint32_t token, int64_t playedMs, int64_t totalMs) = 0;

    /**
     *  语音消息：播放到结尾（扬声器已关闭）。exe 主动 StopVoicePlay、或改播别的文件时不通知。在 UI 线程上调用。
     *  @param token IMediaModule::PlayVoice 时传入的编号
     */
    virtual void OnVoicePlayFinished(uint32_t token) = 0;

    /**
     *  取 exe 加载的 P2P 服务（docs/P2P通信方案.md 第 6 期）：远程桌面在它上面登记用途 rd，服务端为远程桌面建了 P2P
     *  会话时画面与控制命令优先走 P2P 链路。在 UI 线程上调用。
     *  @return P2P 服务（借用，exe 保证在 IMediaModule::Shutdown 返回之前一直有效）；FlamingoP2P.dll 没有加载时为 NULL，
     *          此时远程桌面照旧只走 imgserver 中转
     */
    virtual p2p::IP2PService* GetP2PService() = 0;

    /**
     *  报告某群的群通话状态（docs/群音视频通话方案.md 6.2）：查询结果、群内状态推送（开始、人数变化、结束），exe 据此刷新
     *  群聊窗横幅与通话卡片。只在 UI 线程调用。
     *  @param groupId     群 id
     *  @param roomId      房间号（UTF-8，以 '\0' 结尾）；没有进行中的群通话时为空串
     *  @param active      该群现在是否有进行中的群通话
     *  @param media       媒体类型：1 群语音、2 群视频；没有通话时为 0
     *  @param count       已加入人数；没有通话时为 0
     *  @param initiatorId 发起人 userid；没有通话时为 0
     *  @param startTime   发起时间，Unix 毫秒；没有通话时为 0
     */
    virtual void OnGroupCallStatus(int64_t groupId, const char* roomId, bool active, int media, int count,
                                   int64_t initiatorId, int64_t startTime) = 0;

    /**
     *  群通话结束时服务端改写了群里那张通话卡片（message_type_rtc）：exe 把本地保存的这条消息的 content 整体换掉，
     *  并刷新已打开的群聊窗。只在 UI 线程调用。
     *  @param groupId     群 id
     *  @param msgId       卡片的消息 id
     *  @param contentJson 改写后的 content（UTF-8 JSON 文本，以 '\0' 结尾）
     */
    virtual void OnGroupCallCardChanged(int64_t groupId, int64_t msgId, const char* contentJson) = 0;

    /**
     *  请 exe 弹出选人框，从群成员里选人追加邀请到群通话（通话窗的「邀请成员」）。exe 选好后调
     *  IMediaModule::InviteToGroupCall；用户取消时什么也不做。只在 UI 线程调用。
     *  @param groupId      群 id
     *  @param excludeIds   已在通话里（已加入或振铃中）的人，选人框里不列出；只在本次调用期间有效，可为 NULL
     *  @param excludeCount excludeIds 的个数
     */
    virtual void RequestGroupCallInvitees(int64_t groupId, const int64_t* excludeIds, uint32_t excludeCount) = 0;

    /**
     *  取某人在某群里的角色（群通话窗据此决定显不显示「成员管理」）。只在 UI 线程调用。
     *  @return 0 普通成员、1 管理员、2 群主；exe 不知道时返回 -1
     */
    virtual int GetGroupMemberRole(int64_t groupId, int64_t userId) = 0;

    /**
     *  取某个用户的头像（来电窗、通话画面在对方没开摄像头时显示），与主面板联系人列表同一口径：字形与底色，照片已在本机时
     *  另给照片路径。照片还没下载到本机时 exe 同时安排下载，下载完成后经 IMediaModule::OnAvatarsUpdated 通知。
     *  只在 UI 线程调用。
     *  @param userId 用户 userid
     *  @param out    [出参] 由调用方分配，m_cbSize 须等于 sizeof(MediaAvatarInfo)
     *  @return 已填写返回 true；参数非法、结构体大小不符返回 false
     */
    virtual bool GetUserAvatar(int64_t userId, MediaAvatarInfo* out) = 0;

    /**
     *  取某个群的头像（群通话来电窗显示），与主面板群列表同一口径。其余同 GetUserAvatar。只在 UI 线程调用。
     *  @param groupId 群 id
     *  @param out     [出参] 由调用方分配，m_cbSize 须等于 sizeof(MediaAvatarInfo)
     *  @return 已填写返回 true；参数非法、结构体大小不符、exe 不认识这个群时返回 false
     */
    virtual bool GetGroupAvatar(int64_t groupId, MediaAvatarInfo* out) = 0;

    /**
     *  本人的通话状态变了（docs/音视频通话优化方案.md 2.2）：一对一通话接通或结束、本人加入或离开某群的群通话。exe 据此
     *  在主面板的会话、通讯录、收藏、群列表里给对应的那一行加上或去掉「通话中」徽章。只在 UI 线程调用。
     *  @param ctype  会话类型，取值见 enum MediaPresenceConversation
     *  @param cid    一对一为对方 userid，群通话为群 id
     *  @param media  1 语音、2 视频（群通话按发起时的媒体类型）；inCall 为 false 时忽略
     *  @param inCall true 表示本人进入了通话，false 表示本人已离开或通话已结束
     */
    virtual void OnCallPresence(int ctype, int64_t cid, int media, bool inCall) = 0;

    /**
     *  添加一条录像记录（录制完成且生成了文件时调用，docs/屏幕共享与录制方案.md 第 11.6 节 F9）。exe 写入本账号的
     *  本地库；同一路径已有记录时覆盖它的时长、大小与结束时间。只在 UI 线程调用。
     *  @param info 录像信息；m_cbSize 须等于 sizeof(MediaRecordingInfo)，m_id 被忽略
     *  @return 写入成功返回 true；参数非法、结构体大小不符、本地库没有打开（尚未登录）时返回 false
     */
    virtual bool AddRecording(const MediaRecordingInfo* info) = 0;

    /**
     *  列出本账号的录像记录，按录制结束时间从新到旧排序。只在 UI 线程调用。
     *  @param out      [出参] 由调用方分配的数组，每个元素的 m_cbSize 须等于 sizeof(MediaRecordingInfo)；
     *                  capacity 为 0 时可为 NULL（只取总数）
     *  @param capacity out 的元素个数
     *  @return 记录总数，可能大于 capacity，此时只填写前 capacity 条；本地库没有打开时返回 0。
     *          路径长度达到 kMediaRecordingPathChars 的记录放不进结构体，不计入
     */
    virtual uint32_t ListRecordings(MediaRecordingInfo* out, uint32_t capacity) = 0;

    /**
     *  修改一条录像记录的路径（录制管理窗口里重命名文件之后调用）。只在 UI 线程调用。
     *  @param id      记录 id（ListRecordings 给出的 m_id）
     *  @param newPath 新的完整路径（UTF-16）；只在本次调用期间有效
     *  @return 修改成功返回 true；id 不存在、路径为空或已被另一条记录占用、本地库没有打开时返回 false
     */
    virtual bool UpdateRecordingPath(int64_t id, const wchar_t* newPath) = 0;

    /**
     *  删除一条录像记录，只删记录，不删文件。只在 UI 线程调用。
     *  @param id 记录 id
     *  @return 删掉了一条返回 true；id 不存在或本地库没有打开时返回 false
     */
    virtual bool RemoveRecording(int64_t id) = 0;

    /**
     *  语音字幕当前能否使用（docs/语音字幕方案.md 第 4.1 节），不触发加载：已登录、本企业的字幕开关打开、
     *  且 FlamingoCaption.dll 没有加载失败过时返回 true。模块据此决定是否显示字幕入口（录制选项的「生成字幕」、
     *  录制管理窗口的「编辑字幕」、共享里的「为我的发言生成字幕」）。只在 UI 线程调用。
     *  @return 可以使用返回 true
     */
    virtual bool IsCaptionAvailable() = 0;

    /**
     *  取字幕模块（docs/语音字幕方案.md 第 4.1 节）。第一次调用时由 exe 加载并初始化 FlamingoCaption.dll。
     *  只在 UI 线程调用；返回的指针可以交给任意线程使用（字幕模块的识别接口是线程安全的）。
     *  @return 字幕模块，归 exe 所有，在 exe 卸载 FlamingoMedia 之后才释放；IsCaptionAvailable 为 false、或加载失败时返回 NULL
     */
    virtual caption::ICaptionModule* GetCaptionService() = 0;

    /**
     *  请用户选若干会话，把一段文字发过去（屏幕共享第 25 期：主持人「发送邀请到…」发分享链接）。exe 弹出转发目标选择窗
     *  （多选，上限与转发相同），选好后依次打开对应聊天窗并以普通文字消息发出；用户取消时什么也不做。只在 UI 线程调用。
     *  @param text 要发的文字（UTF-16），只在本次调用期间有效
     *  @return 已弹出选择窗返回 true；没有登录或文字为空返回 false
     */
    virtual bool PickConversationsAndSendText(const wchar_t* text) = 0;

    /**
     *  本企业是否开通了云端录制（第 27 期，功能开关 cloudrecord，登录应答与 1013 里的 features）。决定共享控制条「更多」菜单里有没有
     *  「开始云端录制」、录制选择窗里能不能选「保存到云端」、录制管理里有没有「云端」页的新建入口。没有登录时为 false。只在 UI 线程调用。
     */
    virtual bool IsCloudRecordEnabled() = 0;

    /**
     *  开始一次云端录像文件的上传或下载（第 27 期）：exe 在工作线程上另开一条到 imgserver 的连接，按 512 KiB 分片收发（上传按服务端
     *  的位点续传）。进度与结束经 IMediaModule::OnCloudTransferProgress / OnCloudTransferFinished 在 UI 线程回报。只在 UI 线程调用。
     *  @param req 请求；指针成员只在本次调用期间有效
     *  @return 传输 id（>0）；参数不合法或没有登录时返回 0（不会再有回报）
     */
    virtual uint32_t StartCloudTransfer(const MediaCloudTransfer* req) = 0;

    /**
     *  取消一次传输（在分片边界生效）。取消后仍会收到一次 OnCloudTransferFinished（结果为取消）。
     *  @param id StartCloudTransfer 的返回值；不认识时什么也不做
     */
    virtual void CancelCloudTransfer(uint32_t id) = 0;

    /**
     *  请用户选若干会话（第 27 期：把云端录像发到会话）。exe 弹出转发目标选择窗（多选，上限与转发相同，不是模态的），
     *  用户确定后在 UI 线程调用 IMediaModule::OnConversationsPicked；用户取消时不回调。只在 UI 线程调用。
     *  @param title 选择窗标题（UTF-16），只在本次调用期间有效；为 NULL 或空串时用默认标题
     *  @param token 调用方自定的标识，原样带回 OnConversationsPicked，用来区分是哪一次选择
     *  @return 已弹出选择窗返回 true；没有登录或断线时返回 false
     */
    virtual bool PickConversations(const wchar_t* title, uint32_t token) = 0;

    /**
     *  把一张本机图片作为图片消息发到某个会话（第 17 版，方案第 14.1 节 ⑤：标注「保存为图片」后的「发送到本会话」）。
     *  exe 打开对应聊天窗（已开着时不抢焦点），按截图发送的同一条链路上传并发出图片消息。只在 UI 线程调用。
     *  @param ctype 会话类型（conversation_single / conversation_group）
     *  @param cid   会话 id：单聊为对方 userid，群聊为群 id
     *  @param path  图片文件全路径（UTF-16），只在本次调用期间有效
     *  @return 已交给聊天窗发送返回 true；图片超过图片消息的大小上限、文件不存在、会话不对或没有登录时返回 false，
     *          调用方可改用 SendFileToConversation 以文件消息发送
     */
    virtual bool SendImageToConversation(int ctype, int64_t cid, const wchar_t* path) = 0;

protected:
    ~IMediaHost()
    {
    }
};

//IMediaModule::ConfirmCloudRecordExit 的场合（第 17 版）
enum MediaCloudExitKind
{
    //用户退出程序
    media_cloud_exit_app = 1,
    //用户登出账号
    media_cloud_exit_logout = 2,
    //电脑正在关机或重启（WM_QUERYENDSESSION，区分不出是关机还是重启）：对话框让用户选「关机」或「重启」
    media_cloud_exit_shutdown = 3,
    //Windows 正在注销（WM_QUERYENDSESSION 带 ENDSESSION_LOGOFF）
    media_cloud_exit_logoff = 4,
};

//IMediaModule::ConfirmCloudRecordExit 的结果（第 17 版）
enum MediaCloudExitChoice
{
    //不退出（或取消关机）：什么也不做
    media_cloud_exit_cancel = 0,
    //继续退出、登出或注销，未完成的云端录像下次登录后接着合成或续传
    media_cloud_exit_keep = 1,
    //继续退出、登出或注销，并放弃这些录像（模块已在返回前请服务端删除它们）
    media_cloud_exit_abandon = 2,
    //关机，录像下次登录后接着做（关机场合）
    media_cloud_exit_shutdown_keep = 3,
    //关机，并放弃这些录像（关机场合）
    media_cloud_exit_shutdown_abandon = 4,
    //重启，录像下次登录后接着做（关机场合）
    media_cloud_exit_restart_keep = 5,
    //重启，并放弃这些录像（关机场合）
    media_cloud_exit_restart_abandon = 6,
};

//语音消息的波形值个数（与消息 content 的 wave 字段一致，见 docs/语音消息方案.md V4）
const int kMediaVoiceWaveBars = 40;

/**
 *  语音消息的录音结果（IMediaModule::StopVoiceRecord 的出参）。文件路径即 StartVoiceRecord 时给的路径。
 */
struct MediaVoiceRecordInfo
{
    int64_t m_durationMs;                     //有效时长（毫秒）
    int64_t m_fileBytes;                      //文件字节数
    int32_t m_waveCount;                      //m_wave 中有效的个数（正常为 kMediaVoiceWaveBars）
    uint8_t m_wave[kMediaVoiceWaveBars];      //波形值，每个 0~255
};

//IMediaModule::StartVoiceRecord 的结果
enum MediaVoiceRecordStart
{
    //已开始录音
    media_voice_record_ok = 0,
    //麦克风正被一对一通话或屏幕共享使用，不录音；exe 提示「通话中不能录制语音消息」
    media_voice_record_mic_busy = 1,
    //麦克风打不开（没有设备、被独占、系统隐私设置禁止）
    media_voice_record_device_error = 2,
    //已经在录音，本次请求被忽略
    media_voice_record_already = 3,
    //模块还没有初始化或已经关闭
    media_voice_record_not_ready = 4,
    //继续录音（ResumeVoiceRecord）时没有已暂停的录音
    media_voice_record_not_paused = 5,
};

/**
 *  模块接口：由 DLL 实现，exe 调用。
 *
 *  除特别说明外，所有方法都只能在 exe 的 UI 线程上调用（DLL 的窗口也建在这个线程上）。
 *  析构函数受保护：对象归 DLL 所有，exe 不能删除它。
 */
class IMediaModule
{
public:
    /**
     *  取模块实现的接口版本号。
     *  @return 与 kApiVersion 相同的值（编译 DLL 时的值）
     */
    virtual uint32_t GetApiVersion() const = 0;

    /**
     *  初始化模块。重复调用时直接返回 true，不会重复初始化。
     *  @param host   宿主接口（不转移所有权；须在 Shutdown 返回之前一直有效），不可为 NULL
     *  @param params 初始化参数，只在本次调用期间读取，不可为 NULL
     *  @return 成功返回 true；参数非法返回 false
     */
    virtual bool Init(IMediaHost* host, const MediaInitParams* params) = 0;

    /**
     *  关闭模块：结束进行中的全部会话、关闭全部窗口，之后不再调用宿主接口。
     *  未初始化时调用、重复调用都是安全的。卸载 DLL 之前必须先调用本方法。
     */
    virtual void Shutdown() = 0;

    /**
     *  把主连接收到的一条信令交给模块处理。
     *  @param cmd  命令号
     *  @param json 包体（UTF-8 JSON），不要求以 '\0' 结尾，只在本次调用期间有效
     *  @param len  包体字节数
     *  @return 模块处理了这条信令返回 true；命令号不归本模块处理、模块未初始化或参数非法返回 false
     */
    virtual bool OnSignal(uint16_t cmd, const char* json, uint32_t len) = 0;

    /**
     *  通知模块主连接的在线状态发生了变化。掉线时模块负责收尾进行中的会话。
     *  @param online true 表示已连上并登录，false 表示已断开
     */
    virtual void OnConnectionStateChanged(bool online) = 0;

    /**
     *  查询某项功能在当前系统上是否可用。可在 Init 之前调用。
     *  @param feature 功能，取值见 enum MediaFeature；不认识的值返回 false
     *  @return 可用返回 true
     */
    virtual bool IsFeatureSupported(int feature) = 0;

    /**
     *  用户登出：结束进行中的全部会话（停铃声、停媒体、关窗口），模块保持初始化，重新登录后照常可用。
     */
    virtual void OnLogout() = 0;

    /**
     *  向某个联系人发起一对一通话（单聊窗的「语音通话」「视频通话」按钮）。
     *  @param peerUserId 对方用户号
     *  @param peerName   对方显示名（UTF-16，呼叫窗标题用），只在本次调用期间有效；为 NULL 时按空串处理
     *  @param callType   通话类型，取值见 enum MediaCallType
     *  @return 已发出呼叫并打开呼叫窗返回 true；模块未初始化、参数非法或已在通话中返回 false
     */
    virtual bool StartCall(int64_t peerUserId, const wchar_t* peerName, int callType) = 0;

    /**
     *  向某个联系人发起远程桌面（单聊窗的「远程桌面」按钮），本端为控制端。
     *  @param peerUserId 对方用户号
     *  @param peerName   对方显示名（UTF-16，呼叫中对话框提示文字用），只在本次调用期间有效；为 NULL 时按空串处理
     *  @return 已发出邀请并打开呼叫中对话框返回 true；模块未初始化、参数非法或信令发送失败返回 false
     */
    virtual bool StartRemoteDesktop(int64_t peerUserId, const wchar_t* peerName) = 0;

    /**
     *  在单聊或群聊里发起屏幕共享（聊天窗的「共享屏幕」按钮）：先弹出「选择要共享的内容」窗口，选好后才真正发起。
     *  @param ctype            会话类型：conversation_single / conversation_group
     *  @param cid              会话 id：单聊为对方 userid，群聊为群 id
     *  @param conversationName 会话名（对方显示名或群名，UTF-16），只在本次调用期间有效；为 NULL 时按空串处理
     *  @return 已弹出选择窗返回 true；系统不支持、已在共享或观看中返回 false（模块已向用户提示原因）
     */
    virtual bool StartScreenShare(int ctype, int64_t cid, const wchar_t* conversationName) = 0;

    /**
     *  加入某会话里进行中的屏幕共享（共享卡片、群聊横幅的「加入」按钮）。加入成功后模块打开观看窗。
     *  @param ctype    会话类型
     *  @param cid      会话 id
     *  @param roomId   房间号（UTF-8，以 '\0' 结尾），取自共享卡片或 OnScreenShareStatus
     *  @param hostName 发起人显示名（UTF-16），用于观看窗标题；为 NULL 时按空串处理
     *  @return 已发出加入请求返回 true；参数非法、已在共享或观看中返回 false
     */
    virtual bool JoinScreenShare(int ctype, int64_t cid, const char* roomId, const wchar_t* hostName, bool forced) = 0;

    /**
     *  查询某会话当前有无进行中的屏幕共享（打开聊天窗时调用），结果经 IMediaHost::OnScreenShareStatus 回报。
     */
    virtual void QueryScreenShare(int ctype, int64_t cid) = 0;

    /**
     *  发起独立录屏（聊天窗工具栏、主面板菜单、托盘菜单、全局快捷键）：弹出「选择要录制的内容」窗口，选好后开始录制。
     *  @param ctype            从聊天窗发起时为会话类型（录完可一键发到该会话）；其它入口传 0
     *  @param cid              会话 id；ctype 为 0 时忽略
     *  @param conversationName 会话名（UTF-16），只在本次调用期间有效；可为 NULL
     *  @return 已弹出选择窗返回 true；系统不支持或已在录制时返回 false（模块已向用户提示）
     */
    virtual bool StartRecording(int ctype, int64_t cid, const wchar_t* conversationName) = 0;

    //停止进行中的录制并保存（全局快捷键在录制中再按一次）；没有在录制时什么也不做
    virtual void StopRecording() = 0;

    //是否正在录制（独立录屏或共享录制，含暂停）
    virtual bool IsRecording() = 0;

    /**
     *  设置录制参数（exe 启动后与用户改了设置时调用）。
     *  @param quality 画质：0 标准，1 高清
     *  @param folder  保存目录（UTF-16），只在本次调用期间有效；为 NULL 或空串时用「视频」文件夹下的 FlamingoRecords
     */
    virtual void SetRecordSettings(int quality, const wchar_t* folder) = 0;

    /**
     *  设置鼠标效果（屏幕共享第 16 期）：exe 登录后、以及保存了新设置之后调用。
     *  @param settingsText 设置文本（ASCII，格式见 SerializeMouseEffectSettings），只在本次调用期间有效；
     *                      为 NULL 或空串时按默认设置（只开光圈）
     */
    virtual void SetMouseEffectSettings(const char* settingsText) = 0;

    /**
     *  运行本模块的命令行调试入口（只在 Debug 构建中真正做事，Release 构建恒返回未处理）。
     *  exe 在检查自己的调试入口之前先调用本方法。
     *  @param commandLine 进程命令行（UTF-16），只在本次调用期间有效
     *  @param hwndHost    宿主窗口（调试入口弹出的窗口与消息框以它为属主）
     *  @return 结果，取值见 enum MediaDebugEntryResult
     */
    virtual int RunDebugEntry(const wchar_t* commandLine, HWND hwndHost) = 0;

    /**
     *  语音消息：开始录音（Ogg Opus，16 kHz 单声道，最长 2 分钟，见 docs/语音消息方案.md）。
     *  @param path 录音文件的完整路径（UTF-16），只在本次调用期间有效；停止录音时才写盘
     *  @return 取值见 enum MediaVoiceRecordStart
     */
    virtual int StartVoiceRecord(const wchar_t* path) = 0;

    /**
     *  语音消息：停止录音并写文件。
     *  @param info [出参] 录音结果，返回 true 时有效；不能为 NULL
     *  @return 写文件成功返回 true；没有在录音、没有录到音频或写文件失败返回 false（不留文件）
     */
    virtual bool StopVoiceRecord(MediaVoiceRecordInfo* info) = 0;

    //语音消息：取消录音，不写文件。没有在录音时什么也不做
    virtual void CancelVoiceRecord() = 0;

    /**
     *  语音消息：播放一个语音文件；正在播放别的文件时先停止它（不通知 OnVoicePlayFinished）。
     *  @param path  语音文件的完整路径（UTF-16），只在本次调用期间有效
     *  @param token exe 用来对应进度与结束通知的编号，原样带回；不能为 0
     *  @return 文件格式正确、扬声器打开成功时返回 true
     */
    virtual bool PlayVoice(const wchar_t* path, uint32_t token) = 0;

    //语音消息：停止播放（不通知 OnVoicePlayFinished）。没有在播放时什么也不做
    virtual void StopVoicePlay() = 0;

    /**
     *  语音消息：暂停录音（录音条的「暂停」）。关闭麦克风采集，已录的内容保留；暂停期间没有 OnVoiceRecordLevel。
     *  暂停中照常可以 StopVoiceRecord（写出已录的部分）或 CancelVoiceRecord。
     *  @return 正在录音并已暂停时返回 true；没有在录音或已经暂停时返回 false
     */
    virtual bool PauseVoiceRecord() = 0;

    /**
     *  语音消息：继续已暂停的录音，接着往同一段录音里录。
     *  @return 取值见 enum MediaVoiceRecordStart：继续成功为 media_voice_record_ok；麦克风正被通话或屏幕共享使用为
     *          media_voice_record_mic_busy；麦克风打不开为 media_voice_record_device_error；没有已暂停的录音为
     *          media_voice_record_not_paused；模块没有初始化为 media_voice_record_not_ready。失败时录音保持暂停
     */
    virtual int ResumeVoiceRecord() = 0;

    /**
     *  在群里发起群通话（群聊窗的「语音通话」「视频通话」按钮，选好人之后）。
     *  @param groupId   群 id
     *  @param groupName 群名（UTF-16），用于通话窗标题；只在本次调用期间有效，为 NULL 时按空串处理
     *  @param media     1 群语音、2 群视频
     *  @param invitees  要振铃的人（不含自己）；只在本次调用期间有效，count 为 0 时可为 NULL
     *  @param count     invitees 的个数
     *  @return 已发出请求返回 true；已在通话或屏幕共享里、参数非法返回 false（模块已向用户提示原因）
     */
    virtual bool StartGroupCall(int64_t groupId, const wchar_t* groupName, int media, const int64_t* invitees,
                                uint32_t count) = 0;

    /**
     *  加入某群进行中的群通话（群聊窗横幅、通话卡片的「加入」）。
     *  @param groupId   群 id
     *  @param groupName 群名（UTF-16），用于通话窗标题；为 NULL 时按空串处理
     *  @param roomId    房间号（UTF-8，以 '\0' 结尾）
     *  @return 已发出请求（或已在这场通话里、把通话窗提到前面）返回 true；已在别处通话、参数非法返回 false
     */
    virtual bool JoinGroupCall(int64_t groupId, const wchar_t* groupName, const char* roomId) = 0;

    /**
     *  查询某群有无进行中的群通话（打开群聊窗时调用），结果经 IMediaHost::OnGroupCallStatus 回报。
     */
    virtual void QueryGroupCall(int64_t groupId) = 0;

    /**
     *  追加邀请（IMediaHost::RequestGroupCallInvitees 弹出的选人框确认后调用）。
     *  @param groupId 群 id，须是当前群通话所在的群
     *  @param userIds 要振铃的人；只在本次调用期间有效
     *  @param count   userIds 的个数，须大于 0
     *  @return 已发出请求返回 true
     */
    virtual bool InviteToGroupCall(int64_t groupId, const int64_t* userIds, uint32_t count) = 0;

    /**
     *  有用户或群的头像变了：照片头像下载到了本机，或联系人修改了头像。模块据此经 IMediaHost::GetUserAvatar /
     *  GetGroupAvatar 重新取已打开窗口（来电窗、通话窗）里的头像。不说明是谁，模块整体刷新。
     */
    virtual void OnAvatarsUpdated() = 0;

    /**
     *  语音消息：把已暂停的录音里已录的部分另存成文件，供录音条在暂停时试听（2026-10-05）。录音保持暂停，之后照常可以
     *  ResumeVoiceRecord、StopVoiceRecord 或 CancelVoiceRecord；试听用 PlayVoice 播放这份文件。
     *  @param path          另存文件的完整路径（UTF-16）；只在本次调用期间有效，已存在时覆盖
     *  @param outDurationMs [出参] 另存文件的时长（毫秒），返回 true 时有效；不能为 NULL
     *  @return 录音已暂停、至少录到一帧且写文件成功时返回 true
     */
    virtual bool SnapshotVoiceRecord(const wchar_t* path, int64_t* outDurationMs) = 0;

    /**
     *  打开「录制管理」窗口（系统菜单「录制管理」，docs/屏幕共享与录制方案.md 第 11.6 节 F9），列出本账号录过的录像；
     *  窗口已经打开时恢复并激活它。记录经 IMediaHost::ListRecordings 读取。
     */
    virtual void ShowRecordManager() = 0;

    /**
     *  通知锁屏状态变化（docs/锁屏方案.md 第 4.9 节），exe 在界面线程上调用，锁屏与解锁各调用一次。
     *  锁屏时：结束本机作为受控端的远程桌面、结束本人主持的屏幕共享、停止录屏（照常保存）、停止语音播放与循环铃声；
     *          此后来电、群通话邀请静默（不弹窗、不响铃、不拒绝），远程桌面请求自动拒绝，强制共享不自动加入。
     *  解锁时：仍在响铃时限内的来电补弹来电窗，锁屏期间发起的强制共享补加入。
     *  通话中与观看中的共享不受影响。
     *  @param locked true 表示刚进入锁屏，false 表示刚解锁
     */
    virtual void SetScreenLocked(bool locked) = 0;

    /**
     *  语音字幕的可用状态变了（docs/语音字幕方案.md 第 2 节 D5）：登录成功、退出登录、管理员改了本企业的字幕开关（1013）、
     *  或字幕 DLL 加载失败时，exe 在 UI 线程上调用，取值与此刻 IMediaHost::IsCaptionAvailable 相同。
     *  变为不可用时模块停止正在进行的字幕识别（录像本身照常录制），隐藏字幕入口，丢弃此后收到的字幕控制帧；
     *  已经打开的字幕编辑器与正在进行的导出不受影响（它们在字幕 DLL 里）。
     *  @param available 是否可用
     */
    virtual void OnCaptionAvailabilityChanged(bool available) = 0;

    /**
     *  凭分享链接加入屏幕共享（第 25 期，docs/屏幕共享与录制方案.md 第 12.4 节）：exe 在聊天里认出「flamingo://screenshare/链接码」
     *  并被点击后调用。模块先向服务端查共享概要（1753），弹确认框说明标题、发起人、是否要密码、是否强制模式，确认后带链接码加入；
     *  链接无效、共享已结束、不同企业等情况由模块向用户提示。只在 UI 线程调用。
     *  @param url 聊天里点到的整条链接（UTF-16），只在本次调用期间有效；格式不对时直接提示「链接无效」
     *  @return 已发出查询返回 true；格式不对、没有登录、已在共享或观看别的共享时返回 false（已向用户提示）
     */
    virtual bool JoinScreenShareByLink(const wchar_t* url) = 0;

    /**
     *  云端录像文件传输的进度（第 27 期，IMediaHost::StartCloudTransfer 发起的）。exe 在 UI 线程上调用，每个分片一次。
     *  @param id         传输 id
     *  @param doneBytes  已传完的字节数
     *  @param totalBytes 总字节数
     */
    virtual void OnCloudTransferProgress(uint32_t id, int64_t doneBytes, int64_t totalBytes) = 0;

    /**
     *  云端录像文件传输结束（成功、失败或被取消）。exe 在 UI 线程上调用，每次传输恰好一次。
     *  @param id     传输 id
     *  @param result 0 成功；-1 被取消；其余为错误码（error_code，网络断开等为 error_code_server_internal）
     */
    virtual void OnCloudTransferFinished(uint32_t id, int result) = 0;

    /**
     *  是否有尚未完成的云端录像合成或上传（第 27 期第 56 条）：退出程序、退出登录前 exe 调用，为 true 时先弹确认框，
     *  说明「现在退出，下次登录后将重新合成」。只在 UI 线程调用。
     */
    virtual bool HasCloudRecordWork() = 0;

    /**
     *  打开一条云端录像（第 27 期：聊天里点了云端录像卡片的「播放」）：模块取下载令牌、把最终文件下载到本机缓存后用系统默认播放器打开，
     *  缓存里已有时直接打开；没有权限、录像已删除等情况由模块向用户提示。只在 UI 线程调用。
     *  @param recordId 录像 id
     *  @param title    录像标题（UTF-16，卡片上的），用于提示与缓存文件名；可为 NULL
     */
    virtual void OpenCloudRecord(int64_t recordId, const wchar_t* title) = 0;

    /**
     *  IMediaHost::PickConversations 的结果：用户在选择窗里选好会话并确定。exe 在 UI 线程上调用。
     *  @param token 发起选择时传入的标识
     *  @param refs  选中的会话，只在本次调用期间有效
     *  @param count 会话数（>0）
     */
    virtual void OnConversationsPicked(uint32_t token, const MediaConversationRef* refs, uint32_t count) = 0;

    /**
     *  退出、登出、电脑关机或注销前，询问用户怎样处理未完成的云端录像（第 17 版，方案第 14.4 节）。模块弹出模态的「云端录像」
     *  对话框，列出还在合成、上传或已暂停的录像；用户选择放弃时，模块在返回前取消本机的合成与上传、请服务端删除这些录像。
     *  没有未完成的录像时不弹框，直接返回 media_cloud_exit_keep（关机场合返回 media_cloud_exit_shutdown_keep）。只在 UI 线程调用。
     *  @param kind 场合，取值见 MediaCloudExitKind
     *  @return 用户的选择，取值见 MediaCloudExitChoice
     */
    virtual int ConfirmCloudRecordExit(int kind) = 0;

    /**
     *  屏幕共享清晰度（第 18 版）：发起人在系统设置「音视频」页选的档位。exe 登录后与改了设置后在 UI 线程上调用；
     *  共享进行中修改时，下一帧按新的长边上限重建编码器。
     *  @param quality 0 为 1080p、1 为 2K（默认）、2 为 4K（即 screenshare::ShareQualityLevel）；未知取值按 2K
     */
    virtual void SetShareQuality(int quality) = 0;

protected:
    ~IMediaModule()
    {
    }
};

//导出函数 FlamingoMedia_Create 的类型。apiVersion 传 exe 编译时的 kApiVersion；
//与 DLL 的版本不同时返回 NULL。返回的对象归 DLL 所有。
typedef IMediaModule* (__cdecl* FlamingoMediaCreateFn)(uint32_t apiVersion);

/**
 *  判断宿主与模块的接口版本是否兼容。当前规则是必须完全相同。
 *  @param hostVersion   exe 编译时的 kApiVersion
 *  @param moduleVersion DLL 编译时的 kApiVersion
 *  @return 兼容返回 true
 */
inline bool IsApiVersionCompatible(uint32_t hostVersion, uint32_t moduleVersion)
{
    return hostVersion == moduleVersion;
}

} // namespace flamingomedia
