/**
 *  flamingoAdmin —— 极简 HTTP 客户端实现。
 *
 *  winhttp.h 依赖 windows.h（经 stdafx.h 间接引入），故先包含 stdafx.h 再包含
 *  winhttp.h。WinHTTP 与 winsock 头文件无先后冲突，无需像 NetProbe 那样把网络
 *  头提到 stdafx.h 之前。
 *
 *  balloonwj@qq.com   2026-05-21
 */
#include "stdafx.h"
#include "Util/HttpClient.h"

#include <winhttp.h>
#include <vector>

#pragma comment(lib, "winhttp.lib")

namespace flamingoadmin {

namespace {

// User-Agent 标识，便于服务端日志区分来源。
const wchar_t* const kUserAgent = L"flamingoAdmin/1.0";

// 单次应答体的上限（2 MB）。admin 接口应答最大的是审计日志，远小于此；
// 设上限是防御：服务器异常时不至于把超大响应全部读入内存。
const size_t kMaxBodyLength = 2 * 1024 * 1024;

// HINTERNET 句柄的 RAII 包装：析构时自动 WinHttpCloseHandle。
// HttpPostJson 有多个失败分支提前 return，靠它确保任何路径都不漏关句柄。
class WinHttpHandle
{
public:
    WinHttpHandle()
        : m_handle(nullptr)
    {
    }

    ~WinHttpHandle()
    {
        if (m_handle != nullptr)
            ::WinHttpCloseHandle(m_handle);
    }

    // 接管一个新句柄；若已持有旧句柄，先关闭旧句柄。
    //   handle：WinHTTP API 返回的句柄，可为 nullptr。
    void Reset(HINTERNET handle)
    {
        if (m_handle != nullptr)
            ::WinHttpCloseHandle(m_handle);
        m_handle = handle;
    }

    // 取出原始句柄供 WinHTTP API 使用。
    HINTERNET Get() const { return m_handle; }

    // 句柄是否有效（非空）。
    bool Valid() const { return m_handle != nullptr; }

private:
    WinHttpHandle(const WinHttpHandle&) = delete;
    WinHttpHandle& operator=(const WinHttpHandle&) = delete;

    HINTERNET m_handle;   // 持有的 WinHTTP 句柄，nullptr 表示未持有
};

// 把 WinHTTP 的失败 GetLastError() 翻译为给用户看的中文原因。
CString DescribeWinHttpError(DWORD err)
{
    switch (err)
    {
    case ERROR_WINHTTP_TIMEOUT:
        // 连接 / 收发任一阶段超时。
        return CString(_T("连接服务器超时，请检查服务器地址与网络"));
    case ERROR_WINHTTP_CANNOT_CONNECT:
        // TCP 连接被拒：通常是端口错误或服务未启动。
        return CString(_T("无法连接到服务器，请确认服务已启动且端口正确"));
    case ERROR_WINHTTP_NAME_NOT_RESOLVED:
        // 主机名 DNS 解析失败。
        return CString(_T("无法解析服务器主机名"));
    case ERROR_WINHTTP_SECURE_FAILURE:
        // TLS 握手失败（证书等问题）。
        return CString(_T("安全连接（TLS）建立失败"));
    default:
        {
            CString msg;
            msg.Format(_T("网络请求失败（WinHTTP 错误码 %lu）"), err);
            return msg;
        }
    }
}

} // anonymous namespace

HttpResult HttpPostJson(const CString& host, int port, bool useTls,
                        const CString& path, const std::string& jsonUtf8,
                        int timeoutMs)
{
    HttpResult result;

    // 1. 打开 WinHTTP 会话。WINHTTP_ACCESS_TYPE_NO_PROXY：直连指定 host:port，
    //    不经系统代理 —— admin 控制台连的是内网 / 本机服务，走代理只会添乱。
    WinHttpHandle session;
    session.Reset(::WinHttpOpen(kUserAgent, WINHTTP_ACCESS_TYPE_NO_PROXY,
                                WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0));
    if (!session.Valid())
    {
        result.m_error = _T("无法初始化 HTTP 会话");
        return result;
    }

    // 解析 / 连接 / 发送 / 接收 四个阶段统一用同一超时值。
    ::WinHttpSetTimeouts(session.Get(), timeoutMs, timeoutMs, timeoutMs, timeoutMs);

    // 2. 连接到服务器（此处只是登记目标地址，真正的 TCP 连接在发送时建立）。
    WinHttpHandle connect;
    connect.Reset(::WinHttpConnect(session.Get(), host,
                                   static_cast<INTERNET_PORT>(port), 0));
    if (!connect.Valid())
    {
        result.m_error = _T("无法连接到服务器");
        return result;
    }

    // 3. 创建 POST 请求。useTls 时加 WINHTTP_FLAG_SECURE 走 https。
    const DWORD requestFlags = useTls ? WINHTTP_FLAG_SECURE : 0;
    WinHttpHandle request;
    request.Reset(::WinHttpOpenRequest(connect.Get(), L"POST", path,
                                       nullptr, WINHTTP_NO_REFERER,
                                       WINHTTP_DEFAULT_ACCEPT_TYPES, requestFlags));
    if (!request.Valid())
    {
        result.m_error = _T("无法创建 HTTP 请求");
        return result;
    }

    // 4. 发送请求：带 Content-Type 头与 UTF-8 JSON 请求体。
    const wchar_t* const kContentTypeHeader =
        L"Content-Type: application/json; charset=utf-8";
    // 头部长度传 (DWORD)-1：WinHTTP 约定的「按 '\0' 结尾自行计算长度」哨兵值。
    const DWORD kHeadersAutoLength = static_cast<DWORD>(-1);
    const DWORD bodyLen = static_cast<DWORD>(jsonUtf8.size());
    BOOL sent = ::WinHttpSendRequest(request.Get(),
        kContentTypeHeader, kHeadersAutoLength,
        const_cast<char*>(jsonUtf8.data()), bodyLen, bodyLen, 0);
    BOOL received = FALSE;
    if (sent)
        received = ::WinHttpReceiveResponse(request.Get(), nullptr);

    if (!sent || !received)
    {
        result.m_error = DescribeWinHttpError(::GetLastError());
        return result;
    }

    // 5. 查询 HTTP 状态码。
    DWORD statusCode = 0;
    DWORD statusCodeSize = sizeof(statusCode);
    ::WinHttpQueryHeaders(request.Get(),
        WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
        WINHTTP_HEADER_NAME_BY_INDEX, &statusCode, &statusCodeSize,
        WINHTTP_NO_HEADER_INDEX);
    result.m_statusCode = static_cast<int>(statusCode);

    // 6. 循环读取应答体，直到无更多数据。
    std::string body;
    for (;;)
    {
        DWORD available = 0;
        if (!::WinHttpQueryDataAvailable(request.Get(), &available))
        {
            result.m_error = _T("读取服务器应答失败");
            return result;
        }
        if (available == 0)
            break;   // 应答体已读完

        std::vector<char> chunk(available);
        DWORD bytesRead = 0;
        if (!::WinHttpReadData(request.Get(), &chunk[0], available, &bytesRead))
        {
            result.m_error = _T("读取服务器应答失败");
            return result;
        }
        if (bytesRead == 0)
            break;

        body.append(&chunk[0], bytesRead);
        if (body.size() > kMaxBodyLength)
        {
            result.m_error = _T("服务器应答过大，已中止");
            return result;
        }
    }

    result.m_transportOk = true;
    result.m_body = body;
    return result;
}

} // namespace flamingoadmin
