/**
 *  FlamingoFileShare.dll 与宿主 Flamingo.exe 之间的接口约定。
 *
 *  FlamingoFileShare.dll 承载客户端的「文件共享」功能：在单聊或群聊里把本机的磁盘、文件夹或
 *  文件共享给指定的人，对方按需浏览、下载与写入（方案见 docs/文件共享方案.md）。exe 在登录成功
 *  之后用 LoadLibrary 动态加载它，经唯一的导出函数 FlamingoFileShare_Create 取得 IFileShareModule；
 *  exe 自己实现 IFileShareHost 交给 DLL，DLL 经它写日志、经主连接收发 2200 段信令。DLL 不存在或
 *  加载失败时 exe 照常运行，只是没有文件共享入口。
 *
 *  接口形态与 FlamingoMedia.dll（FlamingoMedia/Api/FlamingoMediaApi.h）保持一致：工厂函数只接收
 *  接口版本号、返回归 DLL 所有的模块对象，模块再经 Init 接收宿主接口。宿主侧用通用加载器
 *  Source/App/PluginModuleLoader.h 加载。
 *
 *  两边都静态链接 CRT、各有独立的堆，因此本文件中的接口只使用纯虚类与基本类型：
 *    - 字符串一律由调用方持有，只在本次调用期间有效，被调方需要保留时自行拷贝；
 *    - 不传递 STL 对象，不在一侧分配、另一侧释放内存；
 *    - 异常不越过模块边界，每个接口实现的最外层自行处理；
 *    - IFileShareModule 的实现对象由 DLL 自己持有（进程内唯一），exe 不负责释放。
 *
 *  修改规则：任何一侧增删接口方法、改动参数或结构体布局，都必须把 kApiVersion 加 1。
 *  加载时双方版本号必须完全相同（见 IsApiVersionCompatible），不做兼容旧版本的处理。
 *
 *  典型用法（exe 侧）：
 *
 *      balloonapp::CPluginModuleLoader<fileshare::IFileShareModule> loader;
 *      if (loader.Load(path, FLAMINGO_FILESHARE_FACTORY_NAME, fileshare::kApiVersion) == balloonapp::plugin_load_ok)
 *          loader.GetModule()->Init(&host, &params);
 *
 *  balloonwj@qq.com   2026-10-02
 */
#pragma once

#include <windows.h>
#include <stdint.h>

//P2P 服务接口（FlamingoP2P/FlamingoP2PApi.h）；本头文件只用到指针，前置声明即可
namespace p2p {
class IP2PService;
}

namespace fileshare {

//接口版本号。exe 与 DLL 编译时都取这一个值；加载时 exe 把它传给 FlamingoFileShare_Create，
//DLL 发现与自己的值不同就拒绝创建模块。
//第 11 版（多语言第 3b 步）：宿主新增 Translate / GetUiLanguage，DLL 的界面文字经 exe 的语言模块翻译。
//第 12 版（第 4 期，操作通知与后台传输）：宿主新增 QueryNoticePolicy / PlayNoticeSound，模块新增 GetActiveTransferCount。
const uint32_t kApiVersion = 12;

//文件共享信令的号段（Protocol.h 的 cmd_share_* 2201~2230 都落在其中）。主程序据此把收到的帧
//原样转交本模块，本模块也只接受这一段的命令。
const uint16_t kShareCmdFirst = 2200;
const uint16_t kShareCmdLast  = 2299;

//DLL 文件名，与 exe 放在同一目录。
#define FLAMINGO_FILESHARE_DLL_NAME     L"FlamingoFileShare.dll"

//导出的工厂函数名（GetProcAddress 使用 ANSI 名称）。
#define FLAMINGO_FILESHARE_FACTORY_NAME "FlamingoFileShare_Create"

//DLL 经 IFileShareHost::Log 写日志时的级别。
enum FileShareLogLevel
{
    fileshare_log_info    = 1,   //普通信息：模块初始化、共享发起与取消、访问会话开始结束等关键路径
    fileshare_log_warning = 2,   //警告：可以继续运行的异常情况，如收到无法解析的信令
    fileshare_log_error   = 3,   //错误：某项操作无法继续，如中转连接失败、文件读写失败
};

//IFileShareHost::QueryNoticePolicy 的返回值：文件共享的弹窗通知此刻怎么处理（第 12 版起）
enum FileShareNoticePolicy
{
    fileshare_notice_show  = 1,   //照常弹出
    fileshare_notice_defer = 2,   //先记下，过一会儿再问（锁屏期间，解锁后补弹）
    fileshare_notice_drop  = 3,   //不弹，丢弃（勿扰时段内；记录照常留在「最近操作」与传输列表里）
};

//FileShareSessionInfo 里账号、昵称、设备标识三个文本字段的容量（含结尾 '\0'），单位：字节（UTF-8）
const uint32_t kFileShareTextBytes = 256;

//FileShareSessionInfo 里路径字段的容量（含结尾 '\0'），单位：wchar_t。个人目录位于程序目录之下，取 1024 足够
const uint32_t kFileSharePathChars = 1024;

//IFileShareHost::GetSessionInfo 的出参：当前登录的账号与本机的信息。DLL 分配、填好 m_cbSize 后传入，exe 填写其余字段；
//文本一律以 '\0' 结尾，超出容量时 exe 判失败而不截断
struct FileShareSessionInfo
{
    uint32_t m_cbSize;                               //本结构体的字节数，DLL 取 sizeof(FileShareSessionInfo)；exe 据此拒绝布局不符的调用方
    int64_t  m_userId;                               //本人 userid
    char     m_account[kFileShareTextBytes];         //账号（UTF-8）
    char     m_nickName[kFileShareTextBytes];        //昵称（UTF-8）
    char     m_deviceId[kFileShareTextBytes];        //本机设备标识（ASCII），与登录请求里上报的 deviceid 是同一个值
    wchar_t  m_personalDir[kFileSharePathChars];     //本人的个人目录（带结尾反斜杠），DLL 的 fileshare.db 放在其下的 ChatLog 目录
};

//会话的一名成员（IFileShareModule::OnConversationMembers 的数组元素）。exe 填写，DLL 只在回调期间读取
struct FileShareMemberInfo
{
    int64_t m_userId;                      //成员 userid
    int     m_role;                        //在群里的角色：0 成员 1 管理员 2 群主（与 Protocol.h 的 group_member_role 一致）；单聊为 0
    char    m_name[kFileShareTextBytes];   //显示名（UTF-8，以 '\0' 结尾）：群名片、昵称、通讯录显示名依次取第一个非空的
};

//FileShareAvatarInfo 里头像字形的容量（含结尾 L'\0'），单位：wchar_t。字形最多 2 个字符，一个 emoji 占 2 个 UTF-16 单元，取 8 足够
const uint32_t kFileShareGlyphChars = 8;

//IFileShareHost::GetUserAvatar 的出参：一个用户的头像该怎么画（第 10 版起）。DLL 分配、填好 m_cbSize 后传入，exe 填写其余字段
struct FileShareAvatarInfo
{
    uint32_t m_cbSize;                            //本结构体的字节数，DLL 取 sizeof(FileShareAvatarInfo)；exe 据此拒绝布局不符的调用方
    wchar_t  m_glyph[kFileShareGlyphChars];       //字形头像上画的字（1~2 个字符，以 L'\0' 结尾）。照片头像也填，照片不在本机时用它占位
    uint32_t m_color;                             //字形头像的底色（COLORREF，即 RGB 宏的值）
    wchar_t  m_imagePath[kFileSharePathChars];    //照片头像在本机的完整路径（以 L'\0' 结尾）；空串表示画字形：用户没有上传照片，
                                                  //或照片还没下载到本机
};

//IFileShareModule::GetShareState 的出参：共享卡片的最新状态。exe 分配、填好 m_cbSize 后传入，DLL 填写其余字段
struct FileShareCardState
{
    uint32_t m_cbSize;    //本结构体的字节数，exe 取 sizeof(FileShareCardState)；DLL 据此拒绝布局不符的调用方
    int      m_state;     //对本人的状态：1 可用 2 暂不可用 3 已失效（与 Protocol.h 的 share_state 一致）
    int      m_reason;    //暂不可用或已失效的原因（share_unavailable_reason / share_invalid_reason）
    int      m_myPerms;   //本人的写权限位；不知道（分享者本人、尚未收到）时为 -1
};

//IFileShareModule::Init 的参数。exe 填好后传入，DLL 只在 Init 期间读取。
struct FileShareInitParams
{
    uint32_t       m_cbSize;   //本结构体的字节数，取 sizeof(FileShareInitParams)；DLL 据此拒绝布局不符的调用方
    const wchar_t* m_appDir;   //exe 所在目录（不带结尾反斜杠），DLL 据此定位皮肤等资源；不可为 NULL
};

/**
 *  判断一个命令号是否属于文件共享信令的号段。
 *  @param cmd 命令号
 *  @return 落在 [kShareCmdFirst, kShareCmdLast] 内返回 true
 */
inline bool IsShareCommand(uint32_t cmd)
{
    return cmd >= kShareCmdFirst && cmd <= kShareCmdLast;
}

/**
 *  宿主接口：由 exe 实现，DLL 调用。
 *
 *  所有方法都可能在 DLL 的任意线程上被调用（日志尤其如此），实现方须保证线程安全。
 *  析构函数受保护：DLL 只使用该对象，不负责销毁它。
 */
class IFileShareHost
{
public:
    /**
     *  写一行日志到 exe 的日志文件。
     *  @param level 日志级别，取值见 enum FileShareLogLevel；不认识的值按普通信息处理
     *  @param text  日志正文（UTF-8，按代码规范为英文），以 '\0' 结尾；为 NULL 时忽略
     */
    virtual void Log(int level, const char* text) = 0;

    /**
     *  经主连接（chatserver）发送一条文件共享信令。
     *  @param cmd  命令号，必须属于 [kShareCmdFirst, kShareCmdLast]
     *  @param json 包体（UTF-8 JSON），不要求以 '\0' 结尾
     *  @param len  包体字节数
     *  @return 已放入发送队列时返回本帧的 seq（大于 0），应答帧带回同一个 seq 供配对；
     *          未连接、命令号不在号段内或参数非法时返回 0
     */
    virtual uint32_t SendShareCommand(uint16_t cmd, const char* json, uint32_t len) = 0;

    /**
     *  取 chatserver 的公钥，DLL 与 fileserver 中转通道握手时用它做协议加密。
     *  @return 指向 32 字节公钥的指针，在进程生命周期内有效；尚未取得公钥时返回 NULL
     */
    virtual const unsigned char* GetServerPublicKey() = 0;

    /**
     *  取当前登录的账号与本机的信息。DLL 在登录成功的通知（OnLoggedIn）里调用，用来打开本账号的本机库、
     *  在日志里标明账号。<u>只在 exe 的 UI 线程上调用</u>（OnLoggedIn 就在这个线程上）。
     *  @param info [出参] 由调用方分配，m_cbSize 须等于 sizeof(FileShareSessionInfo)；其余字段由 exe 填写
     *  @return 已登录且全部字段写得下时返回 true；未登录、参数非法或某个字段超出容量时返回 false
     */
    virtual bool GetSessionInfo(FileShareSessionInfo* info) = 0;

    /**
     *  请求一个会话的成员（发起共享、调整成员时作候选，第 4 版起）。结果经 IFileShareModule::OnConversationMembers 回送：
     *  群聊在群成员列表（cmd 1304）的应答到达后回送；单聊没有可等的，可能在本次调用返回之前就回送。
     *  <u>只在 exe 的 UI 线程上调用</u>。
     *  @param ctype 会话类型：1 单聊 2 群聊
     *  @param cid   会话 id：单聊为对方 userid，群聊为群 id
     *  @return 已发出请求（或已回送）返回 true；未登录或参数非法返回 false，此时不会回送
     */
    virtual bool RequestConversationMembers(int ctype, int64_t cid) = 0;

    /**
     *  取一个用户的显示名（第 4 版起）：与会话列表、消息记录同一口径（通讯录里有备注时带备注），通讯录里没有时
     *  为「用户<id>」。<u>只在 exe 的 UI 线程上调用</u>。
     *  @param userId   用户 userid
     *  @param buf      [出参] 由调用方分配，写入以 '\0' 结尾的 UTF-8 文本
     *  @param bufBytes buf 的字节数
     *  @return 写入成功返回 true；参数非法或写不下返回 false
     */
    virtual bool GetUserDisplayName(int64_t userId, char* buf, uint32_t bufBytes) = 0;

    /**
     *  共享卡片的状态变了（第 5 版起）：DLL 收到 2203、推给本人的 2206 后调用，exe 据此刷新已打开窗口里该共享的卡片。
     *  <u>只在 exe 的 UI 线程上调用</u>。
     *  @param shareId 共享 id
     *  @param state   对本人的状态（share_state）
     *  @param reason  原因
     *  @param myPerms 本人的写权限位；不知道时为 -1
     */
    virtual void NotifyShareState(int64_t shareId, int state, int reason, int myPerms) = 0;

    /**
     *  取一个会话的显示名（第 6 版起，「文件共享」窗口显示共享所在的会话）：群聊为群名，单聊为对方的显示名。
     *  <u>只在 exe 的 UI 线程上调用</u>。
     *  @param ctype    会话类型：1 单聊 2 群聊
     *  @param cid      会话 id（本人视角）
     *  @param buf      [出参] 由调用方分配，写入以 '\0' 结尾的 UTF-8 文本
     *  @param bufBytes buf 的字节数
     *  @return 写入成功返回 true；群名还不知道、参数非法或写不下返回 false
     */
    virtual bool GetConversationName(int ctype, int64_t cid, char* buf, uint32_t bufBytes) = 0;

    /**
     *  打开（或前置）一个会话的聊天窗（第 6 版起，「文件共享」窗口的「定位到会话」）。<u>只在 exe 的 UI 线程上调用</u>。
     *  @param ctype 会话类型
     *  @param cid   会话 id（本人视角）
     *  @return 已交给主面板返回 true；未登录或参数非法返回 false
     */
    virtual bool OpenConversation(int ctype, int64_t cid) = 0;

    /**
     *  用主程序的看图窗显示一张本机图片（第 7 版起，浏览窗口的图片预览）。看图窗是进程内共用的一个，已打开时换图并前置。
     *  <u>只在 exe 的 UI 线程上调用</u>。
     *  @param owner     看图窗的拥有者窗口，可为 NULL
     *  @param imagePath 图片的本机完整路径（UTF-16，以 L'\0' 结尾）
     *  @param navigable 为真时看图窗里的 ← / → 键经模块的 NavigateImagePreview 翻到上一张 / 下一张
     *  @return 已交给看图窗返回 true；参数非法返回 false
     */
    virtual bool OpenImageViewer(HWND owner, const wchar_t* imagePath, bool navigable) = 0;

    /**
     *  一个会话里可用的共享个数变了（第 8 版起）：对经 WatchConversationShares 登记过的会话，DLL 在拉到该会话的共享列表、
     *  或其中某个共享的状态变化之后数一遍，个数与上次通知的不同时调用。exe 据此刷新聊天窗头部文件共享按钮上的角标。
     *  <u>只在 exe 的 UI 线程上调用</u>。
     *  @param ctype 会话类型：1 单聊 2 群聊
     *  @param cid   会话 id（本人视角，与登记时相同）
     *  @param count 本会话里状态为「可用」的共享个数（本人发起的与共享给本人的都算），不小于 0
     */
    virtual void NotifyConversationShareCount(int ctype, int64_t cid, int count) = 0;

    /**
     *  弹出选择联系人窗口，选择共享对象（第 9 版起，「文件共享」窗口的「发起共享」）：按组织架构列出同企业的人，可以多选。
     *  窗口不是模态的：用户确认后经 IFileShareModule::OnShareTargetsPicked 回送已选的人，取消或关闭窗口时不回送。
     *  <u>只在 exe 的 UI 线程上调用</u>。
     *  @param owner 发起的窗口（「文件共享」窗口），可为 NULL
     *  @return 窗口已弹出返回 true；未登录、通讯录还没加载时返回 false
     */
    virtual bool PickShareTargets(HWND owner) = 0;

    /**
     *  新建一个由本人与这些人组成的普通群（第 9 版起，多人共享时先建群、再在群里发起共享），本人为群主。群名由 DLL 拼好
     *  （「共享 <共享名> 给 <成员名>」，见 ShareCreateModel::DirectShareGroupName），exe 原样放进建群请求，因而是用户设定的群名，
     *  不随成员变化自动改名。结果经 IFileShareModule::OnShareGroupCreated 回送；建群成功后群出现在主面板的群列表里。
     *  exe 按请求的 seq 区分它与用户自己点的建群。<u>只在 exe 的 UI 线程上调用</u>。
     *  @param groupName 群名（UTF-8，以 '\0' 结尾，不超过 64 个字符），只在本次调用期间有效
     *  @param memberIds 成员 userid 数组（不含本人），只在本次调用期间有效
     *  @param count     成员数，至少 2
     *  @return 请求已发出返回 true；未连接或参数非法时返回 false，此时不会回送
     */
    virtual bool CreateShareGroup(const char* groupName, const int64_t* memberIds, uint32_t count) = 0;

    /**
     *  取一个用户的头像（第 10 版起，发起共享对话框的成员行、「文件共享」窗口详情里的成员与访问者）：与主面板联系人列表
     *  同一口径——用户选过字形头像的给字形与底色，没设置过的取显示名首字与按 userid 取的底色；上传了照片且照片已在本机的
     *  另给照片路径。照片还没下载到本机时 exe 同时安排下载，下载完成后经 IFileShareModule::OnAvatarsUpdated 通知。
     *  <u>只在 exe 的 UI 线程上调用</u>。
     *  @param userId 用户 userid
     *  @param out    [出参] 由调用方分配，m_cbSize 须等于 sizeof(FileShareAvatarInfo)
     *  @return 已填写返回 true；参数非法返回 false
     */
    virtual bool GetUserAvatar(int64_t userId, FileShareAvatarInfo* out) = 0;

    /**
     *  查界面文字的译文（第 11 版起，多语言第 3b 步）。DLL 里的 TR / TRC / TRF 经它转交给 exe 的语言模块，整个进程
     *  只有一份语言状态。可在任意线程调用。
     *  @param context 上下文（与 TRC 的第一个参数相同），可为 NULL
     *  @param source  简体中文原文，以 '\0' 结尾；为 NULL 时返回 NULL
     *  @return 当前语言的译文。返回的指针在进程生命周期内都有效（指向 exe 加载后不再卸载的语言包，或就是 source 本身）；
     *          没有译文时返回 source
     */
    virtual const wchar_t* Translate(const wchar_t* context, const wchar_t* source) = 0;

    /**
     *  取 exe 当前生效的界面语言与界面字体（第 11 版起，多语言第 3b 步）。DLL 在 Init 里调用一次。可在任意线程调用。
     *  @param langBuf   [出参] 语言代码，如 L"zh-CN"、L"en-US"；写不下时截断，结果始终以 '\0' 结尾
     *  @param langChars langBuf 的容量，单位字符，至少为 1
     *  @param fontBuf   [出参] 界面字体名，如 L"Microsoft YaHei"；写不下时截断，结果始终以 '\0' 结尾
     *  @param fontChars fontBuf 的容量，单位字符，至少为 1
     *  @return 写入成功返回 true；参数非法返回 false
     */
    virtual bool GetUiLanguage(wchar_t* langBuf, uint32_t langChars, wchar_t* fontBuf, uint32_t fontChars) = 0;

    /**
     *  取 exe 已加载的 P2P 服务（docs/P2P通信方案.md 第 6 期：文件共享的传输优先走两端之间的 P2P 链路）。DLL 在 Init 里
     *  调用一次，以用途 fileshare 登记业务方，Shutdown 时注销。<u>只在 exe 的 UI 线程上调用</u>。
     *  @return P2P 服务（归 exe 加载的 FlamingoP2P.dll 所有，在 IFileShareModule::Shutdown 返回之前一直有效）；P2P 模块
     *          没有加载成功时返回 NULL，此时文件共享的传输全部走 fileserver 中转
     */
    virtual p2p::IP2PService* GetP2PService() = 0;

    /**
     *  文件共享的弹窗通知此刻怎么处理（第 12 版起，docs/文件共享方案.md 第 14 节）：系统设置打开了「勿扰时段」且当前在
     *  时段内时丢弃；锁屏期间推迟（balloonlock::DecideDuringLock 的来源「文件共享通知」）；其余照常。DLL 每次要弹窗之前
     *  调用一次，推迟时每秒再问一次。<u>只在 exe 的 UI 线程上调用</u>。
     *  @return FileShareNoticePolicy 的取值
     */
    virtual int QueryNoticePolicy() = 0;

    /**
     *  文件共享的弹窗弹出或追加一条时响一声提示音（第 12 版起）：用新消息的提示音，系统设置的「提示音」关着（即「关闭所有
     *  声音」）或锁屏期间不响。<u>只在 exe 的 UI 线程上调用</u>。
     */
    virtual void PlayNoticeSound() = 0;

protected:
    ~IFileShareHost()
    {
    }
};

/**
 *  模块接口：由 DLL 实现，exe 调用。
 *
 *  除特别说明外，所有方法都只能在 exe 的 UI 线程上调用（DLL 的窗口也建在这个线程上）。
 *  析构函数受保护：对象归 DLL 所有，exe 不能删除它。
 */
class IFileShareModule
{
public:
    /**
     *  取模块实现的接口版本号。
     *  @return 编译 DLL 时的 kApiVersion
     */
    virtual uint32_t GetApiVersion() const = 0;

    /**
     *  初始化模块。重复调用时直接返回 true，不会重复初始化。
     *  @param host   宿主接口（不转移所有权；须在 Shutdown 返回之前一直有效），不可为 NULL
     *  @param params 初始化参数，只在本次调用期间读取，不可为 NULL，m_cbSize 须等于 sizeof(FileShareInitParams)
     *  @return 成功返回 true；参数非法返回 false
     */
    virtual bool Init(IFileShareHost* host, const FileShareInitParams* params) = 0;

    /**
     *  关闭模块：关闭全部窗口、丢弃尚未处理的信令，之后不再调用宿主接口。
     *  未初始化时调用、重复调用都是安全的。卸载 DLL 之前必须先调用本方法。
     */
    virtual void Shutdown() = 0;

    /**
     *  通知模块：登录成功（首次登录与断线重连成功都会调用）。模块据此拉取「我的共享」并报告可提供状态。
     *  未初始化时调用无效果。
     */
    virtual void OnLoggedIn() = 0;

    /**
     *  通知模块：即将退出登录（含切换账号）。模块据此关闭全部窗口、结束全部访问会话。
     *  未初始化时调用无效果。
     */
    virtual void OnLoggingOut() = 0;

    /**
     *  把主连接收到的一帧文件共享信令交给模块。<u>在 exe 的网络接收线程上调用</u>：
     *  模块只拷贝包体并投递到自己的 UI 线程队列，立即返回，不在调用线程上做任何耗时工作。
     *  @param cmd  命令号
     *  @param seq  帧序号；应答帧与对应请求的 seq 相同，推送帧为 0
     *  @param body 包体（UTF-8 JSON），不要求以 '\0' 结尾，只在本次调用期间有效；len 为 0 时可为 NULL
     *  @param len  包体字节数
     *  @return 模块接收了这一帧返回 true；命令号不在号段内、模块未初始化或参数非法返回 false
     */
    virtual bool OnServerFrame(uint16_t cmd, uint32_t seq, const char* body, uint32_t len) = 0;

    /**
     *  打开「文件共享」窗口（系统菜单入口）。窗口已打开时前置它。
     *  @param owner 所有者窗口，可为 NULL
     *  @return 窗口已显示返回 true；模块未初始化或窗口创建失败返回 false
     */
    virtual bool OpenShareCenter(HWND owner) = 0;

    /**
     *  打开一个共享（点共享卡片上的「打开」、或从「文件共享」窗口打开）：显示该共享的浏览窗口并列出共享根目录；
     *  该共享的浏览窗口已打开时只前置。第 1 期第 13 步起提供，单文件共享的下载在第 14 步接入。
     *  @param owner   用来居中对齐的参照窗口，可为 NULL
     *  @param shareId 共享 id
     *  @return 窗口已显示返回 true；模块未初始化、尚未登录或窗口创建失败返回 false
     */
    virtual bool OpenShareFromCard(HWND owner, int64_t shareId) = 0;

    /**
     *  打开发起共享对话框（第 4 版起）：在给定会话里共享本机的磁盘、文件夹或文件。同一会话的对话框已打开时只前置。
     *  @param owner 所有者窗口（通常是聊天窗），可为 NULL
     *  @param ctype 会话类型：1 单聊 2 群聊
     *  @param cid   会话 id：单聊为对方 userid，群聊为群 id
     *  @return 对话框已显示返回 true；模块未初始化、尚未登录、参数非法或窗口创建失败返回 false
     */
    virtual bool OpenCreateShare(HWND owner, int ctype, int64_t cid) = 0;

    /**
     *  exe 回送会话成员（IFileShareHost::RequestConversationMembers 的结果，第 4 版起）。
     *  @param ctype   会话类型
     *  @param cid     会话 id
     *  @param code    0 成功，否则为拉取失败的错误码（此时 count 为 0）
     *  @param members 成员数组，只在本次调用期间有效；count 为 0 时可为 NULL。群聊含本人
     *  @param count   成员数
     */
    virtual void OnConversationMembers(int ctype, int64_t cid, int code, const FileShareMemberInfo* members,
                                       uint32_t count) = 0;

    /**
     *  弹出本会话的共享列表浮层（聊天窗工具栏的「文件共享」按钮，第 5 版起）：列出本会话里本人能看到的共享，
     *  底部有「发起共享」。浮层失去焦点时自动关闭。
     *  @param owner  所有者窗口（聊天窗）；不可为 NULL
     *  @param ctype  会话类型：1 单聊 2 群聊
     *  @param cid    会话 id：单聊为对方 userid，群聊为群 id
     *  @param anchor 按钮在屏幕坐标下的矩形，浮层贴着它弹出（靠近屏幕边缘时自动翻转到工作区内）；可为 NULL，此时贴着光标
     *  @return 浮层已显示返回 true
     */
    virtual bool OpenConversationShares(HWND owner, int ctype, int64_t cid, const RECT* anchor) = 0;

    /**
     *  取共享卡片的最新状态（聊天窗画卡片时调用，第 5 版起）。DLL 有记录时以它为准，没有时 exe 用消息里服务端合并的字段。
     *  @param shareId 共享 id
     *  @param out     [出参] 由调用方分配，m_cbSize 须等于 sizeof(FileShareCardState)
     *  @return 有记录返回 true；没有记录或参数非法返回 false
     */
    virtual bool GetShareState(int64_t shareId, FileShareCardState* out) = 0;

    /**
     *  看图窗里按了 ← / →（第 7 版起；只在经 OpenImageViewer(..., navigable = true) 打开的图片上）：翻到同一目录的
     *  上一张或下一张图片。到头时什么也不做。
     *  @param delta -1 上一张，1 下一张
     */
    virtual void NavigateImagePreview(int delta) = 0;

    /**
     *  登记一个会话，统计其中可用共享的个数（第 8 版起，聊天窗头部文件共享按钮的角标）：DLL 按会话拉取共享列表，
     *  数出个数后经 IFileShareHost::NotifyConversationShareCount 通知，之后个数一变再通知。已登记的会话再调一次即重新拉取：
     *  聊天窗打开时调用一次，收到新的共享卡片时再调用（新发起的共享由此计入）。断线重连后 DLL 自行重新拉取。
     *  @param ctype 会话类型：1 单聊 2 群聊
     *  @param cid   会话 id：单聊为对方 userid，群聊为群 id
     *  @return 已发出列表请求返回 true；模块未初始化、尚未登录或参数非法返回 false（登记仍保留，重连后再拉取）
     */
    virtual bool WatchConversationShares(int ctype, int64_t cid) = 0;

    /**
     *  注销一个会话（第 8 版起，聊天窗关闭时调用）：之后不再为它通知个数。没有登记时调用无效果。
     *  @param ctype 会话类型
     *  @param cid   会话 id
     */
    virtual void UnwatchConversationShares(int ctype, int64_t cid) = 0;

    /**
     *  在本机打开本人发起的共享（第 8 版起，点本人发出的共享卡片）：用资源管理器打开共享的磁盘或文件夹，单个文件则打开所在
     *  文件夹并选中它，与「文件共享」窗口的「在本机打开」相同。打不开时（共享不是在本机发起的、共享的内容已被移走）
     *  以 owner 为所有者弹出提示，说明原因。
     *  @param owner   提示框的所有者窗口（通常是聊天窗），可为 NULL
     *  @param shareId 共享 id
     *  @return 已交给资源管理器返回 true；打不开、模块未初始化或参数非法返回 false
     */
    virtual bool OpenOwnShareLocally(HWND owner, int64_t shareId) = 0;

    /**
     *  exe 回送选好的共享对象（IFileShareHost::PickShareTargets 的结果，第 9 版起）：模块据此打开发起共享对话框，
     *  成员就是这些人（逐人勾选写权限）。勾选的是 1 个人时在与他的单聊里发起；多个人时先经 CreateShareGroup 建群，
     *  再在群里发起。发起成功后经 IFileShareHost::OpenConversation 打开对应的聊天窗。
     *  @param targets 已选的人（userid 与显示名），只在本次调用期间有效；count 为 0 时可为 NULL
     *  @param count   人数
     */
    virtual void OnShareTargetsPicked(const FileShareMemberInfo* targets, uint32_t count) = 0;

    /**
     *  exe 回送建群结果（IFileShareHost::CreateShareGroup 的结果，第 9 版起）。
     *  @param code    0 成功，否则为服务端的错误码
     *  @param groupId 新群的群 id；失败时为 0
     */
    virtual void OnShareGroupCreated(int code, int64_t groupId) = 0;

    /**
     *  有用户的头像变了（第 10 版起）：照片头像下载到了本机，或联系人修改了头像。模块据此经 IFileShareHost::GetUserAvatar
     *  重新取已打开窗口里各人的头像，照片替换原先的字形占位。不说明是哪个用户，模块整体刷新。
     */
    virtual void OnAvatarsUpdated() = 0;

    /**
     *  本人作为访问者、尚未结束的文件共享传输数（第 12 版起，含排队的与浏览窗口关闭后在后台进行的）。exe 在退出程序、
     *  退出登录之前据此请用户确认：退出会中断这些传输，下载下次可以接着传，上传需要重新传。
     *  @return 传输数；模块未初始化时为 0
     */
    virtual uint32_t GetActiveTransferCount() = 0;

protected:
    ~IFileShareModule()
    {
    }
};

//导出函数 FlamingoFileShare_Create 的类型。apiVersion 传 exe 编译时的 kApiVersion；
//与 DLL 的版本不同时返回 NULL。返回的对象归 DLL 所有。
typedef IFileShareModule* (__cdecl* FlamingoFileShareCreateFn)(uint32_t apiVersion);

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

} // namespace fileshare
