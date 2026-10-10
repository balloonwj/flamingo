/**
 *  flamingoAdmin —— 「文件共享」页与「共享审计」页用到的显示文字：动作名、来源名、共享状态与取消原因、
 *  共享类型、成员权限、所在会话、字节数、操作结果与时间格式。全部为纯函数，不依赖界面与网络，
 *  单元测试见 Tests/FileShareAdminApiTests.cpp 的 FT 系列；取值约定见 docs/Admin控制台接口文档.md 第十二章。
 *
 *  典型用法：
 *      CString action = fstext::ActionName(item.m_action);          //「下载文件」
 *      CString perms  = fstext::PermsText(member.m_perms);          //「上传、删除」或「只读」
 *      CString size   = fstext::FormatSize(item.m_size);            //「1.5 MB」
 *      long long from = fstext::RangeStartMs(fstext::kRange7Days, fstext::NowMs());
 *
 *  balloonwj@qq.com   2026-10-03
 */
#pragma once

namespace flamingoadmin { namespace fstext {

// 审计动作，与服务端 share_audit_action 一致
enum AuditAction
{
    kActionOpen            = 1,    // 访问者打开共享，服务端记录
    kActionRequestDownload = 2,    // 访问者申请一次下载，服务端记录，带条目摘要与总字节数
    kActionRequestUpload   = 3,    // 访问者申请一次上传，服务端记录，带条目摘要与总字节数
    kActionDownloadFile    = 4,    // 分享者一端发完一个文件，分享者客户端上报
    kActionUploadFile      = 5,    // 分享者一端收完一个文件，分享者客户端上报
    kActionMkdir           = 6,    // 新建文件夹，分享者客户端上报
    kActionRename          = 7,    // 重命名，分享者客户端上报，带新名字
    kActionDelete          = 8,    // 删除（放入回收站），分享者客户端上报
    kActionMove            = 9,    // 移动，分享者客户端上报，带目标目录
    kActionCreateShare     = 10,   // 发起共享，服务端记录
    kActionCancelShare     = 11,   // 取消共享（分享者、群主或群管理员、后台、群解散、退群各入口），服务端记录
    kActionUpdateMembers   = 12,   // 追加或移除成员、调整成员权限，服务端记录
};

// 审计动作的取值个数：取值为 1～kActionCount
const int kActionCount = 12;

// 共享被取消的原因，与服务端 share_invalid_reason 的 1～5 一致（6、7 只用于单个成员，不会出现在共享记录上）
enum CancelReason
{
    kReasonNone              = 0,   // 没有取消原因：共享仍然有效
    kReasonOwnerCanceled     = 1,   // 分享者本人取消
    kReasonGroupAdminCanceled = 2,  // 群主或群管理员取消了群里的共享
    kReasonAdminCanceled     = 3,   // 管理员在后台强制取消
    kReasonGroupDismissed    = 4,   // 共享所在的群已解散
    kReasonOwnerLeftGroup    = 5,   // 分享者退出了共享所在的群
};

// 共享状态，与服务端 t_file_share.f_state 一致
enum ShareState
{
    kStateActive   = 1,   // 有效：成员可以访问
    kStateCanceled = 2,   // 已取消：被分享者、群主或群管理员、后台管理员取消，或因退群、解散而失效
};

// 成员的写权限位，与服务端 share_perm 一致；浏览与下载不需要权限位，所有成员都有
enum PermBit
{
    kPermUpload = 1,    // 上传
    kPermMkdir  = 2,    // 新建文件夹
    kPermRename = 4,    // 重命名
    kPermDelete = 8,    // 删除（放入分享者电脑的回收站）
    kPermMove   = 16,   // 移动
};

// 审计页的时间范围（时间范围下拉框的下标）
enum AuditRange
{
    kRange7Days   = 0,   // 近 7 天（默认）
    kRange30Days  = 1,   // 近 30 天
    kRange180Days = 2,   // 近 180 天
    kRangeAll     = 3,   // 全部时间，不限起始时间
    kRangeCount   = 4,   // 取值个数
};

// 审计动作的名称。action：1～kActionCount；越界时返回「动作 N」。
CString ActionName(int action);

// 审计记录来源的名称。source：1 为服务端记录，2 为分享者客户端上报；其余返回「-」。
CString SourceName(int source);

// 审计记录来源的简称（表格列用）：1 为「服务端」，2 为「分享者」；其余返回「-」。
CString SourceShort(int source);

// 共享状态的名称。state：取值见 ShareState；其余返回「未知」。
CString StateText(int state);

// 取消原因。reason：取值见服务端 share_invalid_reason（1～5）；0 返回「-」，其余返回「其他原因（N）」。
CString CancelReasonText(int reason);

// 共享类型。kind：1 整个磁盘、2 文件夹、3 单个文件；其余返回「-」。
CString KindName(int kind);

// 成员权限：没有任何写权限时为「只读」，否则按位列出，如「上传、新建文件夹、删除」。
//   perms：写权限位的组合，见 PermBit；不认识的位忽略。
CString PermsText(int perms);

// 所在会话：单聊为「单聊：对方昵称」，群聊为「群聊：群名」；名称为空时用 cid 代替。
//   ctype：1 单聊，2 群聊。name：服务端给出的会话名称。cid：单聊为对方 userid，群聊为群 ID。
CString ConversationText(int ctype, const CString& name, long long cid);

// 字节数的显示：不大于 0 时为「-」，否则按 B / KB / MB / GB 换算，KB 起保留一位小数。
CString FormatSize(long long bytes);

// 操作结果的简短文字（表格列用）：0 为「成功」，否则为「失败」。
CString ResultShort(int result);

// 操作结果的完整文字（详情用）：0 为「成功」；已知的 800 段错误码为「失败：原因」，其余为「失败（错误码 N）」。
CString ResultText(int result);

// 操作人：管理员在后台的操作在名字后加「（管理员）」；名字为空时为「-」。
CString ActorText(const CString& name, bool isAdmin);

// 把 Unix 毫秒格式化为本地时间「YYYY-MM-DD HH:MM:SS」；不大于 0 时返回「-」。
CString FormatTime(long long ms);

// 当前时间，Unix 毫秒。
long long NowMs();

// 时间范围下拉框的文字。range：取值见 AuditRange；越界时按近 7 天。
CString RangeName(int range);

// 时间范围对应的起始时间（Unix 毫秒）：全部时间为 0（不限），其余为 nowMs 往前推相应天数；越界时按近 7 天。
//   range：取值见 AuditRange。nowMs：当前时间，Unix 毫秒。
long long RangeStartMs(int range, long long nowMs);

}} // namespace flamingoadmin::fstext
