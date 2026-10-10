/**
 *  出站 HTTP / HTTPS 客户端，HttpsClient.h（docs/机器人方案.md 第 10 节 1k；仓库变动通知方案 §5.8 共用）。
 *
 *  只提供同步的 POST 请求：机器人「呼叫外部服务」在专用的工作线程上调用，阻塞不影响 I/O 线程。
 *    - Windows：系统自带的 WinHTTP（#pragma comment 链接 winhttp.lib，工程不必改）；证书由系统证书库校验。
 *    - Linux：POSIX 套接字 + OpenSSL（CMake 找到 OpenSSL 时定义 FLAMINGO_HAS_OPENSSL）；校验证书链与主机名。
 *      没有 OpenSSL 时 https 地址直接失败，http 照常可用。
 *  两边都支持 HTTP 代理（SetProxy），不提供跳过证书校验的选项。
 *
 *  URL 与应答报文的解析是纯函数，单元测试直接调用（tests/test_httpsclient.cpp）。
 *
 *  典型用法：
 *      httpclient::HttpResponse resp;
 *      std::vector<std::pair<std::string, std::string> > headers;
 *      headers.push_back(std::make_pair("Content-Type", "application/json"));
 *      if (httpclient::Post("https://api.example.com/bot", headers, body, 5000, 65536, resp) && resp.m_status == 200)
 *          ...
 *
 *  balloonwj@qq.com   2026-10-08
 */
#pragma once

#include <stddef.h>
#include <string>
#include <utility>
#include <vector>

namespace httpclient {

//一次请求的结果
struct HttpResponse
{
    int         m_status;  //HTTP 状态码；网络错误时为 0
    std::string m_body;    //应答体（解开 chunked 之后；超过上限的部分丢弃）
    std::string m_error;   //失败原因（英文，写日志用）

    HttpResponse()
        : m_status(0)
    {
    }
};

//拆开之后的 URL
struct ParsedUrl
{
    bool        m_https;  //是否为 https
    std::string m_host;   //主机名或 IP（不含端口）
    int         m_port;   //端口；URL 里没写时取 80 / 443
    std::string m_path;   //路径与查询串，至少为 "/"

    ParsedUrl()
        : m_https(false), m_port(0)
    {
    }
};

/**
 *  拆开一个 http(s) URL。
 *  @param url 地址，如 "https://api.example.com:8443/bot?x=1"
 *  @param out [出参] 结果
 *  @return 是 http 或 https、主机非空、端口合法时返回 true
 */
bool ParseUrl(const std::string& url, ParsedUrl& out);

/**
 *  解析一份完整的 HTTP/1.x 应答报文（状态行、头、正文；正文支持 Content-Length、chunked 与读到连接关闭为止三种）。
 *  @param raw      收到的全部字节
 *  @param status   [出参] 状态码
 *  @param body     [出参] 正文
 *  @param complete [出参] 报文是否已经完整（按 Content-Length 或 chunked 的结束块判断；两者都没有时恒为 false，
 *                  调用方读到连接关闭即视为完整）
 *  @return 状态行与头部合法返回 true；头部还没收全或格式错误返回 false
 */
bool ParseResponse(const std::string& raw, int& status, std::string& body, bool& complete);

/**
 *  设置 HTTP 代理，对之后的全部请求生效。线程安全。
 *  @param proxy 形如 "http://10.0.0.1:3128" 或 "10.0.0.1:3128"；空串表示不用代理
 */
void SetProxy(const std::string& proxy);

/**
 *  同步发一个 POST 请求。
 *  @param url          地址（http 或 https）
 *  @param headers      额外的请求头；Host、Content-Length、Connection 由本函数填写
 *  @param body         请求体
 *  @param timeoutMs    连接、发送、接收各阶段的超时（毫秒）
 *  @param maxBodyBytes 应答体最多保留的字节数；超出的部分丢弃（m_body 截断）
 *  @param out          [出参] 结果
 *  @return 收到了完整的应答（不论状态码）返回 true；地址不合法、连接失败、证书校验失败、超时返回 false
 */
bool Post(const std::string& url, const std::vector<std::pair<std::string, std::string> >& headers,
          const std::string& body, int timeoutMs, size_t maxBodyBytes, HttpResponse& out);

} // namespace httpclient
