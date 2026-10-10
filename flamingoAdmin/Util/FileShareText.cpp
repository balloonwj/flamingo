/**
 *  flamingoAdmin —— 文件共享相关显示文字的实现，说明见 FileShareText.h。
 *
 *  balloonwj@qq.com   2026-10-03
 */
#include "stdafx.h"
#include "Util/FileShareText.h"
#include "Util/TimeText.h"   // 时间显示格式（日期 / 分钟 / 秒）

namespace flamingoadmin { namespace fstext {

namespace {

// 审计来源，与服务端 share_audit_source 一致
const int kSourceServer = 1;   // 服务端亲眼所见（打开、申请传输、发起、取消、调整成员）
const int kSourceClient = 2;   // 分享者客户端上报（逐个文件的传输、新建、重命名、删除、移动）

// 会话类型，与服务端 ctype 一致
const int kCtypeSingle = 1;    // 单聊
const int kCtypeGroup  = 2;    // 群聊

// 共享类型，与服务端 share_kind 一致
const int kKindDisk   = 1;     // 整个磁盘
const int kKindFolder = 2;     // 文件夹
const int kKindFile   = 3;     // 单个文件

// 字节数换算
const long long kBytesPerKb = 1024;
const long long kBytesPerMb = 1024 * 1024;
const long long kBytesPerGb = 1024LL * 1024 * 1024;

// 文件共享错误码段的起点与终点（服务端 error_code_share_disabled ～ error_code_share_cross_enterprise）
const int kShareErrorFirst = 800;
const int kShareErrorLast  = 821;

// 800～821 各错误码的简短原因，下标为「错误码 - 800」，须与服务端 Protocol.h 的 error_code 保持一致
const wchar_t* const kShareErrorText[] =
{
    L"功能已被管理员关闭",         // 800 error_code_share_disabled
    L"共享不存在",                 // 801 error_code_share_not_exist
    L"共享已被取消",               // 802 error_code_share_canceled
    L"不是该共享的成员",           // 803 error_code_share_not_member
    L"分享者不在线",               // 804 error_code_share_unavailable
    L"共享的文件夹或磁盘已不存在", // 805 error_code_share_root_missing
    L"没有这项操作的权限",         // 806 error_code_share_permission_denied
    L"目标位置已有同名条目",       // 807 error_code_share_name_conflict
    L"路径非法或超出共享范围",     // 808 error_code_share_path_invalid
    L"文件或目录不存在",           // 809 error_code_share_path_not_found
    L"无法放入回收站",             // 810 error_code_share_recycle_unavailable
    L"分享者电脑上读写失败",       // 811 error_code_share_io_fail
    L"磁盘空间不足",               // 812 error_code_share_disk_full
    L"同时传输的人数已满",         // 813 error_code_share_transfer_busy
    L"服务器繁忙",                 // 814 error_code_share_server_busy
    L"共享数已达上限",             // 815 error_code_share_too_many
    L"分享者没有响应",             // 816 error_code_share_owner_timeout
    L"中转连接已断开",             // 817 error_code_share_relay_broken
    L"传输被取消",                 // 818 error_code_share_transfer_canceled
    L"成员不属于该会话",           // 819 error_code_share_invalid_member
    L"操作与共享类型不符",         // 820 error_code_share_kind_mismatch
    L"成员不属于同一个企业",       // 821 error_code_share_cross_enterprise
};

// 时间范围对应的天数，下标为 AuditRange（kRangeAll 不用天数）
const int kRangeDays[] = { 7, 30, 180 };

// 一天的毫秒数
const long long kDayMs = 24LL * 60 * 60 * 1000;

// Unix 毫秒与 FILETIME（自 1601-01-01 起的 100 纳秒数）的换算常量
const long long kEpochDiff100ns = 116444736000000000LL;  // 1601-01-01 到 1970-01-01 的 100 纳秒数
const long long k100nsPerMs     = 10000;                 // 一毫秒的 100 纳秒数

} // namespace

CString ActionName(int action)
{
    switch (action)
    {
    //访问者打开共享
    case kActionOpen:
        return CString(_T("打开"));
    //访问者申请一次下载
    case kActionRequestDownload:
        return CString(_T("申请下载"));
    //访问者申请一次上传
    case kActionRequestUpload:
        return CString(_T("申请上传"));
    //分享者一端发完一个文件
    case kActionDownloadFile:
        return CString(_T("下载文件"));
    //分享者一端收完一个文件
    case kActionUploadFile:
        return CString(_T("上传文件"));
    //新建文件夹
    case kActionMkdir:
        return CString(_T("新建文件夹"));
    //重命名
    case kActionRename:
        return CString(_T("重命名"));
    //删除（放入回收站）
    case kActionDelete:
        return CString(_T("删除"));
    //移动
    case kActionMove:
        return CString(_T("移动"));
    //发起共享
    case kActionCreateShare:
        return CString(_T("发起共享"));
    //取消共享（各个入口）
    case kActionCancelShare:
        return CString(_T("取消共享"));
    //追加或移除成员、调整成员权限
    case kActionUpdateMembers:
        return CString(_T("调整成员"));
    //新版本服务端增加的动作：显示编号，不至于整行空着
    default:
        {
            CString text;
            text.Format(_T("动作 %d"), action);
            return text;
        }
    }
}

CString SourceName(int source)
{
    if (source == kSourceServer)
        return CString(_T("服务端记录"));
    if (source == kSourceClient)
        return CString(_T("分享者上报"));
    return CString(_T("-"));
}

CString SourceShort(int source)
{
    if (source == kSourceServer)
        return CString(_T("服务端"));
    if (source == kSourceClient)
        return CString(_T("分享者"));
    return CString(_T("-"));
}

CString StateText(int state)
{
    if (state == kStateActive)
        return CString(_T("有效"));
    if (state == kStateCanceled)
        return CString(_T("已取消"));
    return CString(_T("未知"));
}

CString CancelReasonText(int reason)
{
    switch (reason)
    {
    //没有取消原因（共享仍然有效）
    case kReasonNone:
        return CString(_T("-"));
    //分享者本人取消
    case kReasonOwnerCanceled:
        return CString(_T("分享者取消"));
    //群主或群管理员取消
    case kReasonGroupAdminCanceled:
        return CString(_T("群主或群管理员取消"));
    //管理员在后台强制取消
    case kReasonAdminCanceled:
        return CString(_T("管理员在后台强制取消"));
    //共享所在的群已解散
    case kReasonGroupDismissed:
        return CString(_T("群已解散"));
    //分享者退出了共享所在的群
    case kReasonOwnerLeftGroup:
        return CString(_T("分享者已退群"));
    //不认识的原因：显示编号
    default:
        {
            CString text;
            text.Format(_T("其他原因（%d）"), reason);
            return text;
        }
    }
}

CString KindName(int kind)
{
    if (kind == kKindDisk)
        return CString(_T("整个磁盘"));
    if (kind == kKindFolder)
        return CString(_T("文件夹"));
    if (kind == kKindFile)
        return CString(_T("单个文件"));
    return CString(_T("-"));
}

CString PermsText(int perms)
{
    struct PermName
    {
        int     m_bit;    // 权限位
        LPCTSTR m_name;   // 显示名
    };
    const PermName names[] =
    {
        { kPermUpload, _T("上传") },
        { kPermMkdir,  _T("新建文件夹") },
        { kPermRename, _T("重命名") },
        { kPermDelete, _T("删除") },
        { kPermMove,   _T("移动") },
    };
    CString text;
    for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); ++i)
    {
        if ((perms & names[i].m_bit) == 0)
            continue;
        if (!text.IsEmpty())
            text += _T("、");
        text += names[i].m_name;
    }
    if (text.IsEmpty())
        text = _T("只读");
    return text;
}

CString ConversationText(int ctype, const CString& name, long long cid)
{
    CString target = name;
    if (target.IsEmpty())
        target.Format(_T("%lld"), cid);
    if (ctype == kCtypeSingle)
        return _T("单聊：") + target;
    if (ctype == kCtypeGroup)
        return _T("群聊：") + target;
    return target;
}

CString FormatSize(long long bytes)
{
    CString text;
    if (bytes <= 0)
        text = _T("-");
    else if (bytes < kBytesPerKb)
        text.Format(_T("%lld B"), bytes);
    else if (bytes < kBytesPerMb)
        text.Format(_T("%.1f KB"), static_cast<double>(bytes) / kBytesPerKb);
    else if (bytes < kBytesPerGb)
        text.Format(_T("%.1f MB"), static_cast<double>(bytes) / kBytesPerMb);
    else
        text.Format(_T("%.1f GB"), static_cast<double>(bytes) / kBytesPerGb);
    return text;
}

CString ResultShort(int result)
{
    return CString(result == 0 ? _T("成功") : _T("失败"));
}

CString ResultText(int result)
{
    if (result == 0)
        return CString(_T("成功"));
    CString text;
    if (result >= kShareErrorFirst && result <= kShareErrorLast)
        text.Format(_T("失败：%s（%d）"), kShareErrorText[result - kShareErrorFirst], result);
    else
        text.Format(_T("失败（错误码 %d）"), result);
    return text;
}

CString ActorText(const CString& name, bool isAdmin)
{
    if (name.IsEmpty())
        return CString(_T("-"));
    if (isAdmin)
        return name + _T("（管理员）");
    return name;
}

CString FormatTime(long long ms)
{
    if (ms <= 0)
        return CString(_T("-"));
    ULARGE_INTEGER u;
    u.QuadPart = static_cast<ULONGLONG>(ms) * k100nsPerMs + kEpochDiff100ns;
    FILETIME utc;
    utc.dwLowDateTime = u.LowPart;
    utc.dwHighDateTime = u.HighPart;
    FILETIME local;
    SYSTEMTIME st;
    if (!::FileTimeToLocalFileTime(&utc, &local) || !::FileTimeToSystemTime(&local, &st))
        return CString(_T("-"));
    // 文件共享的时间（含共享审计记录）显示到秒。
    return timetext::FormatSystemTime(st, timetext::TimeStyle::Second);
}

long long NowMs()
{
    FILETIME ft;
    ::GetSystemTimeAsFileTime(&ft);
    ULARGE_INTEGER u;
    u.LowPart = ft.dwLowDateTime;
    u.HighPart = ft.dwHighDateTime;
    return static_cast<long long>((u.QuadPart - kEpochDiff100ns) / k100nsPerMs);
}

CString RangeName(int range)
{
    if (range == kRangeAll)
        return CString(_T("全部时间"));
    const int index = (range >= kRange7Days && range <= kRange180Days) ? range : kRange7Days;
    CString text;
    text.Format(_T("近 %d 天"), kRangeDays[index]);
    return text;
}

long long RangeStartMs(int range, long long nowMs)
{
    if (range == kRangeAll)
        return 0;
    const int index = (range >= kRange7Days && range <= kRange180Days) ? range : kRange7Days;
    return nowMs - kRangeDays[index] * kDayMs;
}

}} // namespace flamingoadmin::fstext
