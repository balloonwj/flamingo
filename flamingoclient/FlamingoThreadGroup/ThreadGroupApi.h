/**
 *  FlamingoThreadGroup.dll 与宿主 Flamingo.exe 之间的接口约定（帖子群，docs/帖子群方案.md 第 7 节）。
 *
 *  本 DLL 提供帖子群窗口：帖子列表（最新动态 / 最新发布、置顶在前、分页）、帖子详情（正文、图片、楼主补充、点赞与
 *  表情回应）、留言与回复、发帖与追加补充的编辑窗口；收发 2700 段信令（cmd_thread_*）。界面用 DLL 自己静态链接的一份
 *  balloonui。exe 在登录成功后按企业开关（features.threadgroup）用 LoadLibrary 动态加载它；DLL 不存在或加载失败时
 *  exe 照常运行，只是帖子群全部隐藏（docs/帖子群方案.md 4.7）。
 *
 *  图片的上传与下载、头像、显示名、群资料、表情库、看图窗口都由 exe 经 IThreadGroupHost 提供，DLL 不重复实现。
 *
 *  线程约定：OnServerFrame 在 exe 的网络接收线程上调用，DLL 拷贝后投递到自己的消息窗口；其余方法与回调都在界面线程。
 *
 *  两边都静态链接 CRT、各有独立的堆，因此接口只使用纯虚类、基本类型与固定布局的结构体：字符串一律由调用方持有、
 *  只在本次调用期间有效；不传递 STL 对象；不在一侧分配、另一侧释放。
 *
 *  修改规则：任何一侧增删接口方法、改动参数或结构体布局，都必须把 kApiVersion 加 1；加载时双方版本号必须完全相同。
 *
 *  balloonwj@qq.com   2026-10-07
 */
#pragma once

#include <windows.h>
#include <stdint.h>

namespace threadgroup {

//接口版本号；加载时宿主与 DLL 必须完全相同。1：2026-10-07 第一版；2：2026-10-08 OnGroupMembers 改为带显示名与群角色
//（docs/帖子群方案.md 14.2 第 5a 步）；3：2026-10-08 群成员面板（宿主的 ShowMemberPanel / MoveMemberPanel /
//HideMemberPanel，模块的 OnMemberPanelClosed，14.3）；4：2026-10-08 第 6 期（§15）：ThreadGroupBrief 加 m_atAllScope，
//宿主加 OpenGroupManage（群管理窗口）与 ShowMemberMenu（点正文里的 @ 弹成员菜单）
const uint32_t kApiVersion = 4;

//帖子群信令的号段（Protocol.h 的 cmd_thread_* 2701~2720 都落在其中）
const uint16_t kThreadCmdFirst = 2700;
const uint16_t kThreadCmdLast = 2799;

//结构体里文字字段的容量（UTF-16 字符数，含结尾 0）
const uint32_t kThreadNameChars = 128;
const uint32_t kThreadPathChars = 520;
const uint32_t kThreadGlyphChars = 8;

//DLL 文件名与导出的工厂函数名
#define FLAMINGO_THREADGROUP_DLL_NAME       L"FlamingoThreadGroup.dll"
#define FLAMINGO_THREADGROUP_FACTORY_NAME   "FlamingoThreadGroup_Create"

//IThreadGroupHost::ShowMemberMenu 的 targetRole 除群角色 0 / 1 / 2 之外的两个取值
const int kMemberRoleLeft    = -1;   //对方不在群成员名单里（已退群或被移出）
const int kMemberRoleUnknown = -2;   //成员名单还没取到：按仍在群里、角色未知处理

//DLL 经 IThreadGroupHost::Log 写日志时的级别
enum ThreadLogLevel
{
    thread_log_info    = 1,   //普通信息
    thread_log_warning = 2,   //警告：可以继续运行的异常情况
    thread_log_error   = 3,   //错误：某项操作无法继续
};

//IThreadGroupModule::Init 的参数
struct ThreadInitParams
{
    uint32_t       m_cbSize;   //sizeof(ThreadInitParams)
    const wchar_t* m_appDir;   //exe 所在目录（不带结尾反斜杠）
};

//宿主提供的群概况（IThreadGroupHost::GetGroupBrief 的出参），DLL 分配并填好 m_cbSize
struct ThreadGroupBrief
{
    uint32_t m_cbSize;                    //sizeof(ThreadGroupBrief)
    wchar_t  m_name[kThreadNameChars];    //群名
    uint32_t m_memberCount;               //成员数
    int      m_myRole;                    //本人角色：0 成员 1 管理员 2 群主（与 Protocol.h 的 group_member_role 一致）；不是成员为 -1
    bool     m_mutedForMe;                //本人此刻不能发言：被禁言，或仅通知模式下不是管理员，或入群必读还没确认
    int      m_atAllScope;                //群设置「谁可以 @ 全体」：0 仅群主和管理员 1 所有人（与 Protocol.h 的 group_at_all_scope
                                          //一致）；群资料还没到时为 0（v4，docs/帖子群方案.md §15.1 第 1 条）
};

//宿主提供的头像（IThreadGroupHost::GetUserAvatar 的出参），做法与文件共享相同：字形 + 底色，照片在本机时另给路径
struct ThreadAvatarInfo
{
    uint32_t m_cbSize;                       //sizeof(ThreadAvatarInfo)
    wchar_t  m_glyph[kThreadGlyphChars];     //字形头像上画的字
    uint32_t m_color;                        //字形头像的底色（COLORREF）
    wchar_t  m_imagePath[kThreadPathChars];  //照片头像在本机的完整路径；空串表示画字形
};

//群成员名单里的一个人（IThreadGroupModule::OnGroupMembers 的参数，宿主分配、只在本次调用期间有效）
struct ThreadMemberInfo
{
    uint32_t m_cbSize;                    //sizeof(ThreadMemberInfo)
    int64_t  m_userId;                    //用户 ID
    int      m_role;                      //群角色：0 成员 1 管理员 2 群主（与 Protocol.h 的 group_member_role 一致）
    wchar_t  m_name[kThreadNameChars];    //显示名：群名片，没有时为昵称，与普通群 @ 的口径相同；不含本人给对方起的备注
                                          //（@ 的文字会随正文发给全群，2026-10-07 实机联调发现的问题 P1）
};

//表情库里的一个表情（IThreadGroupHost::GetFaceInfo 的出参）
struct ThreadFaceInfo
{
    uint32_t m_cbSize;                       //sizeof(ThreadFaceInfo)
    int      m_faceId;                       //表情编号（正文里编码为 /f["编号"]，互动编号为 1000 + 编号）
    wchar_t  m_name[kThreadNameChars];       //表情名（如「微笑」），正文里显示为 [微笑]
    wchar_t  m_path[kThreadPathChars];       //表情图片在本机的完整路径
};

/**
 *  判断一个命令号是否属于帖子群的号段。
 */
inline bool IsThreadCommand(uint32_t cmd)
{
    return cmd >= kThreadCmdFirst && cmd <= kThreadCmdLast;
}

/**
 *  判断宿主与 DLL 的接口版本是否兼容：必须完全相同。
 */
inline bool IsApiVersionCompatible(uint32_t hostVersion, uint32_t dllVersion)
{
    return hostVersion == dllVersion;
}

/**
 *  exe 实现、交给 DLL 的宿主接口。除注明外只在界面线程上调用。
 */
class IThreadGroupHost
{
public:
    //写一行日志（任意线程），text 为 UTF-8
    virtual void Log(int level, const char* text) = 0;

    //界面文字翻译（与 FlamingoFileShare 相同：i18n::InitFromHost 经它取 exe 当前语言的译文）
    virtual const wchar_t* Translate(const wchar_t* context, const wchar_t* source) = 0;

    //exe 当前的界面语言与界面字体
    virtual bool GetUiLanguage(wchar_t* langBuf, uint32_t langChars, wchar_t* fontBuf, uint32_t fontChars) = 0;

    /**
     *  经主连接发送一条 2700 段命令（任意线程）。
     *  @return 已放入发送队列时返回帧 seq（大于 0）；不放行时返回 0
     */
    virtual uint32_t SendThreadCommand(uint16_t cmd, const char* json, uint32_t len) = 0;

    //本人 userid；未登录时为 0
    virtual int64_t GetMyUserId() = 0;

    /**
     *  一个用户在某个群里的显示名（群名片、通讯录显示名、昵称依次取第一个非空的）。
     *  @return 已填写返回 true
     */
    virtual bool GetUserDisplayName(int64_t userId, int64_t groupId, wchar_t* buf, uint32_t chars) = 0;

    //一个用户的头像；照片还没下载到本机时 exe 同时安排下载，完成后经 IThreadGroupModule::OnAvatarsUpdated 通知
    virtual bool GetUserAvatar(int64_t userId, ThreadAvatarInfo* out) = 0;

    //群概况；本人不在群里时 m_myRole 为 -1
    virtual bool GetGroupBrief(int64_t groupId, ThreadGroupBrief* out) = 0;

    //请求某群的成员名单（@ 成员时用），结果经 IThreadGroupModule::OnGroupMembers 回来
    virtual void RequestGroupMembers(int64_t groupId) = 0;

    //打开现有的群资料窗口（帖子群窗口的「群资料」按钮，所有成员都有）
    virtual void OpenGroupInfo(HWND owner, int64_t groupId) = 0;

    /**
     *  打开现有的群管理窗口（帖子群窗口的「群管理」按钮，只有群主与管理员有；v4，docs/帖子群方案.md §15.1 第 3 条）。
     *  帖子群的群管理窗口没有「群公告」页，其余页与普通群相同。本人不是群主或管理员时宿主提示后不打开。
     */
    virtual void OpenGroupManage(HWND owner, int64_t groupId) = 0;

    /**
     *  在屏幕坐标 (screenX, screenY) 弹出某个群成员的菜单（点帖子正文、补充、留言或回复里的 @某人，v4，§15.1 第 4 条）。
     *  菜单与群成员面板的右键菜单相同（没有「@TA」），选中后由宿主执行；弹出期间不返回（模态）。
     *  @param owner      菜单与确认框的所有者窗口（帖子群窗口）
     *  @param userId     被 @ 的人
     *  @param targetRole 对方的群角色：0 成员 1 管理员 2 群主；kMemberRoleLeft 表示已不在群里；kMemberRoleUnknown 表示
     *                    成员名单还没取到
     */
    virtual void ShowMemberMenu(HWND owner, int64_t groupId, int64_t userId, int targetRole, int screenX,
                                int screenY) = 0;

    //打开某人的资料卡
    virtual void OpenContactCard(HWND owner, int64_t userId) = 0;

    /**
     *  上传一张本地图片（不发消息），结果经 IThreadGroupModule::OnImageUploaded 回来。
     *  @param requestId DLL 自己分配的请求号，原样带回
     */
    virtual bool UploadImage(uint32_t requestId, const wchar_t* localPath) = 0;

    /**
     *  按 fileid 下载图片到本机缓存（md5 已知且已缓存时立即回报），结果经 IThreadGroupModule::OnImageReady 回来。
     */
    virtual bool DownloadImage(uint32_t requestId, const char* fileId, const char* md5) = 0;

    //用现有的看图窗口打开一组本机图片，从第 index 张开始
    virtual void OpenImageViewer(HWND owner, const wchar_t* const* paths, uint32_t count, uint32_t index) = 0;

    //表情库：表情个数与第 index 个表情的信息
    virtual uint32_t GetFaceCount() = 0;
    virtual bool GetFaceInfo(uint32_t index, ThreadFaceInfo* out) = 0;

    //把该群标为已读（帖子群窗口在前台、内容已显示时调用）：exe 按该群最新的 msgid 发 1506 并清掉未读
    virtual void MarkGroupRead(int64_t groupId) = 0;

    //帖子群窗口是否「正在看」（在前台且没有最小化）变了，exe 的提醒判定据此决定要不要弹窗与计未读
    virtual void OnWatchChanged(int64_t groupId, bool watching) = 0;

    /**
     *  显示帖子群窗口右侧的群成员面板（docs/帖子群方案.md 14.3）：面板由 exe 实现（列表与按部门两种视图、搜索、
     *  右键菜单与普通群相同），第一次时在 parent 下建成子窗口，从右边缘滑入到 rc。
     *  @param parent  帖子群窗口
     *  @param groupId 帖子群
     *  @param rc      面板在 parent 客户区里的矩形
     *  @return 面板已显示返回 true
     */
    virtual bool ShowMemberPanel(HWND parent, int64_t groupId, const RECT* rc) = 0;

    //帖子群窗口改变了大小：面板跟随到新的矩形（面板没显示时什么也不做）
    virtual void MoveMemberPanel(int64_t groupId, const RECT* rc) = 0;

    //收起群成员面板（再点一次「成员」，或帖子群窗口关闭）
    virtual void HideMemberPanel(int64_t groupId) = 0;

protected:
    ~IThreadGroupHost()
    {
    }
};

/**
 *  DLL 实现、交给 exe 的模块接口。除 OnServerFrame 外只在界面线程上调用。
 */
class IThreadGroupModule
{
public:
    //初始化：登记宿主、建消息窗口、初始化本 DLL 那份 balloonui 与多语言；失败时宿主应卸载 DLL
    virtual bool Init(IThreadGroupHost* host, const ThreadInitParams* params) = 0;

    //进程退出前调用：关闭全部帖子群窗口、销毁消息窗口；之后不再回调宿主
    virtual void Shutdown() = 0;

    //返回 DLL 编译时的接口版本号
    virtual uint32_t GetApiVersion() const = 0;

    //登录成功（含断线重连）：已打开的帖子群窗口重新登记「正在查看」并刷新
    virtual void OnLoggedIn() = 0;

    //即将退出登录：关闭全部帖子群窗口
    virtual void OnLoggingOut() = 0;

    //收到一帧 2700 段的服务端帧（网络接收线程），DLL 拷贝后投递到界面线程处理
    virtual void OnServerFrame(uint16_t cmd, uint32_t seq, const char* body, uint32_t len) = 0;

    /**
     *  打开（或激活）某个帖子群的窗口。
     *  @param postId 大于 0 时打开后定位到这条帖子（从卡片、提醒点进来时）
     *  @return 窗口已显示返回 true
     */
    virtual bool OpenThreadWindow(int64_t groupId, int64_t postId) = 0;

    //关闭某个帖子群的窗口（本人退群、被移出、群被解散时）
    virtual void CloseThreadWindow(int64_t groupId) = 0;

    //某个帖子群的窗口是否「正在看」（存在、在前台、没有最小化）
    virtual bool IsWatching(int64_t groupId) const = 0;

    //群资料变了（群名、成员数、本人角色、禁言状态）：已打开的窗口刷新标题与可用的操作
    virtual void OnGroupChanged(int64_t groupId) = 0;

    /**
     *  群成员名单到达（RequestGroupMembers 的结果）。
     *  @param members 成员数组，按宿主收到的顺序；拉取失败时为 NULL（count 为 0）
     *  @param count   成员个数
     */
    virtual void OnGroupMembers(int64_t groupId, const ThreadMemberInfo* members, uint32_t count) = 0;

    /**
     *  图片上传的结果。
     *  @param ok        是否成功
     *  @param fileId / md5 成功时为服务端的 fileid 与内容 MD5
     *  @param width / height / size 图片尺寸与字节数
     *  @param localPath 本机缓存路径（可为空）
     */
    virtual void OnImageUploaded(uint32_t requestId, bool ok, const char* fileId, const char* md5, uint32_t width,
                                 uint32_t height, int64_t size, const wchar_t* localPath) = 0;

    //图片下载的结果：成功时 localPath 为本机缓存路径
    virtual void OnImageReady(uint32_t requestId, bool ok, const wchar_t* localPath) = 0;

    //头像照片下载完成：已打开的窗口重画头像
    virtual void OnAvatarsUpdated() = 0;

    //群成员面板被用户关掉了（面板上的关闭按钮或 Esc）：帖子群窗口记下面板已收起，下次点「成员」再打开
    virtual void OnMemberPanelClosed(int64_t groupId) = 0;

protected:
    ~IThreadGroupModule()
    {
    }
};

} // namespace threadgroup

//导出的工厂函数：版本兼容时返回模块对象（归 DLL 所有），否则返回 NULL
extern "C" threadgroup::IThreadGroupModule* __cdecl FlamingoThreadGroup_Create(uint32_t apiVersion);
