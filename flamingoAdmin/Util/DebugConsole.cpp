/**
 *  flamingoAdmin —— 调试控制台数据层实现（请求/响应往返记录）。
 *
 *  实现 DebugConsole.h 声明的进程级单例：把每一次「请求—响应」往返记录成
 *  一条 DebugEntry，存入有上限的环形缓冲。所有公有方法加锁，可在任意线程
 *  安全调用。请求体 / 响应体里的 password、oldpassword、newpassword、token
 *  等敏感字段在入缓冲前会被掩码，缓冲区内不留存明文密码或完整令牌。
 *
 *  balloonwj@qq.com   2026-05-22
 */
#include "stdafx.h"
#include "Util/DebugConsole.h"

#include "json.h"      // 第三方 jsoncpp 1.9.0（third-party/jsoncpp）

namespace flamingoadmin {

namespace {

// ---- 敏感字段键名。命中这些键名的对象成员，其值会被掩码后再存储 ----

// 明文密码字段：值整体替换为 "***"。
const char* const kSensitiveKeyPassword    = "password";
// 修改密码接口里的旧密码字段：值整体替换为 "***"。
const char* const kSensitiveKeyOldPassword = "oldpassword";
// 修改密码接口里的新密码字段：值整体替换为 "***"。
const char* const kSensitiveKeyNewPassword = "newpassword";
// 登录令牌字段：保留前若干字符后接省略号，既能比对令牌前缀又不泄露全文。
const char* const kSensitiveKeyToken       = "token";

// 掩码后用于替换敏感值的占位文本。
const char* const kMaskPlaceholder = "***";

// token 掩码时保留的前缀字符数。令牌长度大于此值才做「前缀 + 省略号」掩码，
// 否则（短令牌）整体替换为占位文本，避免短令牌前缀几乎等于全文。
const Json::ArrayIndex kTokenKeepPrefixLen = 6;

// token 掩码时拼在前缀之后的省略号（UTF-8 三字节字符 U+2026）。
const char* const kTokenEllipsisUtf8 = "\xE2\x80\xA6";

// 请求时刻 / 导出文本里用到的固定文案与格式串。
// 时刻格式 "时:分:秒.毫秒"，毫秒补足三位。
const wchar_t* const kTimeFormat = L"%02d:%02d:%02d.%03d";

// 导出文本里一条记录的分隔横线。
const wchar_t* const kExportSeparator =
    L"------------------------------------------------------------\r\n";

// 递归遍历 JSON 值，对命中敏感键名的对象成员就地掩码其值。
//   value：待遍历的 JSON 值（按引用就地修改）。
void MaskJsonValue(Json::Value& value)
{
    if (value.isArray())
    {
        // 数组：逐元素递归。
        for (Json::ArrayIndex i = 0; i < value.size(); ++i)
        {
            MaskJsonValue(value[i]);
        }
        return;
    }

    if (!value.isObject())
    {
        // 非数组非对象（字符串 / 数字 / 布尔 / 空）无需处理。
        return;
    }

    // 对象：逐成员检查键名，命中敏感键名的就地掩码，其余递归下钻。
    const Json::Value::Members members = value.getMemberNames();
    for (size_t i = 0; i < members.size(); ++i)
    {
        const std::string& key = members[i];

        if (key == kSensitiveKeyPassword
            || key == kSensitiveKeyOldPassword
            || key == kSensitiveKeyNewPassword)
        {
            // 密码类字段：值整体替换为占位文本。
            value[key] = Json::Value(kMaskPlaceholder);
            continue;
        }

        if (key == kSensitiveKeyToken)
        {
            // 令牌字段：长令牌保留前缀 + 省略号，短令牌整体替换。
            const Json::Value& tokenVal = value[key];
            if (tokenVal.isString())
            {
                const std::string tokenStr = tokenVal.asString();
                if (tokenStr.size() > kTokenKeepPrefixLen)
                {
                    std::string masked = tokenStr.substr(0, kTokenKeepPrefixLen);
                    masked += kTokenEllipsisUtf8;
                    value[key] = Json::Value(masked);
                }
                else
                {
                    value[key] = Json::Value(kMaskPlaceholder);
                }
            }
            else
            {
                // 令牌字段值不是字符串：仍按占位文本处理，避免泄露。
                value[key] = Json::Value(kMaskPlaceholder);
            }
            continue;
        }

        // 普通成员：递归下钻，以处理嵌套对象 / 数组里的敏感字段。
        MaskJsonValue(value[key]);
    }
}

// 把一段 UTF-8 JSON 文本掩码敏感字段后美化（带缩进）重新序列化。
//   bodyUtf8：原始 JSON 文本（UTF-8 字节）。
//   返回：掩码并美化后的 UTF-8 JSON 文本；若 bodyUtf8 不是合法 JSON
//         （如 CSV 导出响应），原样返回 —— 非 JSON 内容不含密钥。
std::string MaskAndPrettyPrint(const std::string& bodyUtf8)
{
    Json::Reader reader;
    Json::Value root;
    if (!reader.parse(bodyUtf8, root))
    {
        // 解析失败：内容不是 JSON，原样返回。
        return bodyUtf8;
    }

    MaskJsonValue(root);

    // StyledWriter 输出带缩进的多行 JSON，便于调试控制台逐行查看。
    Json::StyledWriter writer;
    return writer.write(root);
}

// 从响应体里解析顶层整数字段 "code"。
//   respBodyUtf8：响应体 JSON 文本（UTF-8 字节）。
//   返回：顶层整数 "code" 的值；响应体非合法 JSON、非对象、无 "code"
//         成员或 "code" 非整数时，返回 kDebugNoBusinessCode。
int ParseBusinessCode(const std::string& respBodyUtf8)
{
    Json::Reader reader;
    Json::Value root;
    if (!reader.parse(respBodyUtf8, root))
    {
        // 响应体不是合法 JSON：无业务 code 可取。
        return kDebugNoBusinessCode;
    }

    // 业务 code 字段名固定为 "code"（见服务端协议）。
    const char* const kCodeKey = "code";
    if (!root.isObject() || !root.isMember(kCodeKey))
    {
        return kDebugNoBusinessCode;
    }

    const Json::Value& codeVal = root[kCodeKey];
    if (!codeVal.isIntegral())
    {
        return kDebugNoBusinessCode;
    }

    return codeVal.asInt();
}

// 把一段 UTF-8 字节串转换为 UTF-16 的 CString。
//   utf8：UTF-8 字节串。
//   返回：对应的 UTF-16 文本；空串或转换失败返回空 CString。
CString Utf8ToCString(const std::string& utf8)
{
    if (utf8.empty())
    {
        return CString();
    }

    const int wideLen = ::MultiByteToWideChar(
        CP_UTF8, 0, utf8.c_str(), static_cast<int>(utf8.size()), nullptr, 0);
    if (wideLen <= 0)
    {
        return CString();
    }

    CString result;
    wchar_t* buffer = result.GetBuffer(wideLen);
    ::MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(),
                          static_cast<int>(utf8.size()), buffer, wideLen);
    result.ReleaseBuffer(wideLen);
    return result;
}

} // anonymous namespace

// ======================= DebugEntry =======================

DebugEntry::DebugEntry()
    : m_seq(0)
    , m_state(DebugEntryState::Pending)
    , m_elapsedMs(0)
    , m_transportOk(false)
    , m_httpStatus(0)
    , m_businessCode(kDebugNoBusinessCode)
{
    // CString 成员（m_reqTime / m_url / m_apiName / m_errorText）与
    // std::string 成员（m_reqBodyUtf8 / m_respBodyUtf8）由各自默认构造为空。
}

DebugEntryKind DebugEntry::Kind() const
{
    if (m_state == DebugEntryState::Pending)
    {
        // 响应尚未回来：恒为进行中。
        return DebugEntryKind::Pending;
    }

    if (!m_transportOk)
    {
        // 已完成但传输失败：连接 / 收发出错，未拿到应答。
        return DebugEntryKind::TransportFail;
    }

    if (m_businessCode == 0)
    {
        // 传输成功且业务 code 为 0：业务成功。
        return DebugEntryKind::Success;
    }

    // 传输成功但业务 code 非 0 或缺失（kDebugNoBusinessCode）：业务失败。
    return DebugEntryKind::BusinessFail;
}

// ======================= DebugConsole =======================

DebugConsole::DebugConsole()
    : m_nextSeq(1)          // 序号从 1 起，0 留作「暂停时 BeginEntry」的返回值
    , m_paused(false)
    , m_sinkHwnd(nullptr)
    , m_sinkMsg(0)
{
}

DebugConsole::~DebugConsole()
{
}

DebugConsole& DebugConsole::Instance()
{
    // Meyers 单例：C++11 保证局部静态变量的初始化线程安全。
    static DebugConsole s_instance;
    return s_instance;
}

unsigned long long DebugConsole::BeginEntry(const CString& host, int port,
                                            bool useTls, const CString& path,
                                            const std::string& bodyUtf8)
{
    std::lock_guard<std::mutex> lock(m_mutex);

    if (m_paused)
    {
        // 暂停态：不入缓冲，返回 0。EndEntry 收到 0 时同样为空操作。
        return 0;
    }

    DebugEntry entry;
    entry.m_seq   = m_nextSeq++;
    entry.m_state = DebugEntryState::Pending;

    // 记录请求发起的本地时刻，格式 "HH:MM:SS.mmm"。
    SYSTEMTIME now;
    ::GetLocalTime(&now);
    entry.m_reqTime.Format(kTimeFormat, now.wHour, now.wMinute,
                           now.wSecond, now.wMilliseconds);

    // 拼完整 URL：scheme://host:port/path。
    const wchar_t* const scheme = useTls ? L"https" : L"http";
    entry.m_url.Format(L"%s://%s:%d%s", scheme, host.GetString(),
                       port, path.GetString());

    // 从 path 推导接口名：以 "/admin/" 开头取其后剩余段，否则取首个 '/' 之后。
    const wchar_t* const kAdminPrefix = L"/admin/";
    const int adminPrefixLen = lstrlenW(kAdminPrefix);
    if (path.Left(adminPrefixLen) == kAdminPrefix)
    {
        entry.m_apiName = path.Mid(adminPrefixLen);
    }
    else if (!path.IsEmpty() && path[0] == L'/')
    {
        entry.m_apiName = path.Mid(1);
    }
    else
    {
        // path 不以 '/' 开头（非常规输入）：整段作为接口名。
        entry.m_apiName = path;
    }

    // 请求体掩码 + 美化后存储，不留存原始明文。
    entry.m_reqBodyUtf8 = MaskAndPrettyPrint(bodyUtf8);

    // 环形缓冲：超出上限时先丢弃最旧一条，再追加新记录。
    if (m_entries.size() >= kDebugMaxEntries)
    {
        m_entries.pop_front();
    }
    m_entries.push_back(entry);

    NotifySink();
    return entry.m_seq;
}

void DebugConsole::EndEntry(unsigned long long seq, bool transportOk,
                            int httpStatus, const std::string& respBodyUtf8,
                            const CString& errorText, unsigned elapsedMs)
{
    std::lock_guard<std::mutex> lock(m_mutex);

    if (seq == 0)
    {
        // seq 为 0：对应暂停期间的 BeginEntry，无记录可回填，空操作。
        return;
    }

    // 从后往前扫：刚 BeginEntry 的记录通常靠近队尾，逆序查找更快。
    DebugEntry* target = nullptr;
    for (std::deque<DebugEntry>::reverse_iterator it = m_entries.rbegin();
         it != m_entries.rend(); ++it)
    {
        if (it->m_seq == seq)
        {
            target = &(*it);
            break;
        }
    }

    if (target == nullptr)
    {
        // 找不到对应记录（可能已被环形缓冲淘汰）：空操作，不崩溃。
        return;
    }

    // 回填响应侧字段。
    target->m_state       = DebugEntryState::Done;
    target->m_transportOk = transportOk;
    target->m_httpStatus  = httpStatus;
    target->m_errorText   = errorText;
    target->m_elapsedMs   = elapsedMs;
    target->m_respBodyUtf8 = MaskAndPrettyPrint(respBodyUtf8);
    target->m_businessCode = ParseBusinessCode(respBodyUtf8);

    NotifySink();
}

void DebugConsole::SetPaused(bool paused)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_paused = paused;
}

bool DebugConsole::IsPaused() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_paused;
}

void DebugConsole::Clear()
{
    std::lock_guard<std::mutex> lock(m_mutex);

    // 只清空记录，m_nextSeq 不重置 —— 便于据序号区分清空前后的记录。
    m_entries.clear();

    NotifySink();
}

size_t DebugConsole::Count() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_entries.size();
}

std::vector<DebugEntry> DebugConsole::Snapshot() const
{
    std::lock_guard<std::mutex> lock(m_mutex);

    // 整体拷贝：拷出后即与内部缓冲解耦，UI 线程遍历期间不受工作线程影响。
    return std::vector<DebugEntry>(m_entries.begin(), m_entries.end());
}

void DebugConsole::SetSink(HWND hwnd, UINT msg)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_sinkHwnd = hwnd;
    m_sinkMsg  = msg;
}

CString DebugConsole::FormatAllForExport() const
{
    std::lock_guard<std::mutex> lock(m_mutex);

    CString result;
    for (std::deque<DebugEntry>::const_iterator it = m_entries.begin();
         it != m_entries.end(); ++it)
    {
        const DebugEntry& entry = *it;

        // 每条记录一个文本块：序号 + 时刻 + URL。
        CString header;
        header.Format(L"#%llu  [%s]  %s\r\n",
                      entry.m_seq, entry.m_reqTime.GetString(),
                      entry.m_url.GetString());
        result += header;

        // 请求体 JSON（已掩码、已美化）。
        result += L"请求:\r\n";
        result += Utf8ToCString(entry.m_reqBodyUtf8);
        result += L"\r\n";

        if (entry.m_state == DebugEntryState::Pending)
        {
            // 进行中：尚无响应。
            result += L"结果: 进行中\r\n";
        }
        else if (!entry.m_transportOk)
        {
            // 传输失败：写中文错误原因。
            CString line;
            line.Format(L"结果: 传输失败  耗时 %u ms  原因: %s\r\n",
                        entry.m_elapsedMs, entry.m_errorText.GetString());
            result += line;
        }
        else
        {
            // 传输成功：写 HTTP 状态、耗时、业务 code 与响应体。
            CString line;
            if (entry.m_businessCode == kDebugNoBusinessCode)
            {
                line.Format(L"结果: HTTP %d  耗时 %u ms  业务 code: 无\r\n",
                            entry.m_httpStatus, entry.m_elapsedMs);
            }
            else
            {
                line.Format(L"结果: HTTP %d  耗时 %u ms  业务 code: %d\r\n",
                            entry.m_httpStatus, entry.m_elapsedMs,
                            entry.m_businessCode);
            }
            result += line;
            result += L"响应:\r\n";
            result += Utf8ToCString(entry.m_respBodyUtf8);
            result += L"\r\n";
        }

        result += kExportSeparator;
    }

    return result;
}

void DebugConsole::NotifySink()
{
    // 调用方须已持有 m_mutex。仅做一次廉价的异步消息投递。
    if (m_sinkHwnd != nullptr)
    {
        ::PostMessage(m_sinkHwnd, m_sinkMsg, 0, 0);
    }
}

} // namespace flamingoadmin
