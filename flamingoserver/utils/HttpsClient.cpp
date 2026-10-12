/**
 *  出站 HTTP / HTTPS 客户端实现，HttpsClient.cpp。设计与约定见 HttpsClient.h。
 *  balloonwj@qq.com   2026-10-08
 */
#include "HttpsClient.h"

#include <stdlib.h>
#include <string.h>
#include <mutex>

#include "../base/Platform.h"

#ifdef WIN32
#include <winhttp.h>
#pragma comment(lib, "winhttp.lib")
#else
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <poll.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/types.h>
#ifdef FLAMINGO_HAS_OPENSSL
#include <openssl/ssl.h>
#include <openssl/err.h>
#include <openssl/x509v3.h>
#endif
#ifdef FLAMINGO_BUNDLED_OPENSSL
#include "CaBundleLocator.h"
#include "../base/AsyncLog.h"
#endif
#endif

namespace httpclient {

namespace {

//默认端口
const int kHttpPort = 80;
const int kHttpsPort = 443;
//端口上限
const int kMaxPort = 65535;
//读应答时每次读多少字节
const size_t kReadChunk = 16384;
//应答头最多多少字节（防止对方发来无穷无尽的头）
const size_t kMaxHeaderBytes = 65536;
//报文头与正文的分隔
const char* const kHeaderEnd = "\r\n\r\n";
//请求里的 User-Agent
const char* const kUserAgent = "Flamingo-Bot/1.0";

std::mutex  g_proxyMutex;   //保护 g_proxy
std::string g_proxy;        //当前代理（host:port）；空表示不用

//字符串转小写（ASCII）
std::string Lower(const std::string& s)
{
    std::string out(s);
    for (size_t i = 0; i < out.size(); ++i)
    {
        if (out[i] >= 'A' && out[i] <= 'Z')
            out[i] = static_cast<char>(out[i] - 'A' + 'a');
    }
    return out;
}

//去掉首尾空白
std::string Trim(const std::string& s)
{
    size_t b = 0;
    size_t e = s.size();
    while (b < e && (s[b] == ' ' || s[b] == '\t'))
        ++b;
    while (e > b && (s[e - 1] == ' ' || s[e - 1] == '\t' || s[e - 1] == '\r'))
        --e;
    return s.substr(b, e - b);
}

//当前代理
std::string CurrentProxy()
{
    std::lock_guard<std::mutex> guard(g_proxyMutex);
    return g_proxy;
}

//解开 chunked 正文；完整时返回 true
bool DecodeChunked(const std::string& data, std::string& out)
{
    out.clear();
    size_t pos = 0;
    while (true)
    {
        const size_t lineEnd = data.find("\r\n", pos);
        if (lineEnd == std::string::npos)
            return false;
        std::string sizeLine = data.substr(pos, lineEnd - pos);
        const size_t semi = sizeLine.find(';');
        if (semi != std::string::npos)
            sizeLine = sizeLine.substr(0, semi);
        char* end = NULL;
        const unsigned long size = strtoul(Trim(sizeLine).c_str(), &end, 16);
        if (end == NULL)
            return false;
        pos = lineEnd + 2;
        if (size == 0)
            return true;
        if (pos + size + 2 > data.size())
            return false;
        out.append(data, pos, size);
        pos += size + 2;
    }
}

} // anonymous namespace

bool ParseUrl(const std::string& url, ParsedUrl& out)
{
    out = ParsedUrl();
    const std::string lower = Lower(url);
    size_t hostStart = 0;
    if (lower.compare(0, 8, "https://") == 0)
    {
        out.m_https = true;
        hostStart = 8;
    }
    else if (lower.compare(0, 7, "http://") == 0)
    {
        hostStart = 7;
    }
    else
    {
        return false;
    }
    size_t hostEnd = url.find_first_of("/?#", hostStart);
    if (hostEnd == std::string::npos)
        hostEnd = url.size();
    std::string hostPort = url.substr(hostStart, hostEnd - hostStart);
    if (hostPort.empty() || hostPort.find('@') != std::string::npos)
        return false;
    out.m_port = out.m_https ? kHttpsPort : kHttpPort;
    const size_t colon = hostPort.rfind(':');
    if (colon != std::string::npos)
    {
        const std::string portStr = hostPort.substr(colon + 1);
        if (portStr.empty())
            return false;
        for (size_t i = 0; i < portStr.size(); ++i)
        {
            if (portStr[i] < '0' || portStr[i] > '9')
                return false;
        }
        out.m_port = atoi(portStr.c_str());
        hostPort = hostPort.substr(0, colon);
    }
    if (hostPort.empty() || out.m_port <= 0 || out.m_port > kMaxPort)
        return false;
    out.m_host = hostPort;
    out.m_path = hostEnd < url.size() ? url.substr(hostEnd) : std::string("/");
    if (out.m_path.empty() || out.m_path[0] != '/')
        out.m_path = "/" + out.m_path;
    //片段（#…）不发给服务器
    const size_t hash = out.m_path.find('#');
    if (hash != std::string::npos)
        out.m_path = out.m_path.substr(0, hash);
    return true;
}

bool ParseResponse(const std::string& raw, int& status, std::string& body, bool& complete)
{
    status = 0;
    body.clear();
    complete = false;
    const size_t headEnd = raw.find(kHeaderEnd);
    if (headEnd == std::string::npos)
        return false;
    const std::string head = raw.substr(0, headEnd);
    const size_t firstLineEnd = head.find("\r\n");
    const std::string statusLine = head.substr(0, firstLineEnd);
    if (statusLine.compare(0, 5, "HTTP/") != 0)
        return false;
    const size_t sp = statusLine.find(' ');
    if (sp == std::string::npos)
        return false;
    status = atoi(statusLine.c_str() + sp + 1);
    if (status < 100 || status > 999)
        return false;

    long long contentLength = -1;
    bool chunked = false;
    size_t pos = firstLineEnd == std::string::npos ? head.size() : firstLineEnd + 2;
    while (pos < head.size())
    {
        size_t lineEnd = head.find("\r\n", pos);
        if (lineEnd == std::string::npos)
            lineEnd = head.size();
        const std::string line = head.substr(pos, lineEnd - pos);
        const size_t colon = line.find(':');
        if (colon != std::string::npos)
        {
            const std::string name = Lower(Trim(line.substr(0, colon)));
            const std::string value = Trim(line.substr(colon + 1));
            if (name == "content-length")
                contentLength = atoll(value.c_str());
            else if (name == "transfer-encoding" && Lower(value).find("chunked") != std::string::npos)
                chunked = true;
        }
        pos = lineEnd + 2;
    }
    const std::string rest = raw.substr(headEnd + 4);
    if (chunked)
    {
        complete = DecodeChunked(rest, body);
        if (!complete)
            body.clear();
        return true;
    }
    if (contentLength >= 0)
    {
        if (static_cast<long long>(rest.size()) >= contentLength)
        {
            body = rest.substr(0, static_cast<size_t>(contentLength));
            complete = true;
        }
        return true;
    }
    //既没有 Content-Length 也不是 chunked：正文到连接关闭为止，由调用方判断是否读完
    body = rest;
    //1xx、204、304 没有正文
    complete = (status == 204 || status == 304 || status < 200);
    return true;
}

void SetProxy(const std::string& proxy)
{
    std::string p = Trim(proxy);
    const std::string lower = Lower(p);
    if (lower.compare(0, 7, "http://") == 0)
        p = p.substr(7);
    while (!p.empty() && p[p.size() - 1] == '/')
        p.erase(p.size() - 1);
    std::lock_guard<std::mutex> guard(g_proxyMutex);
    g_proxy = p;
}

#ifdef WIN32

namespace {

//UTF-8 转宽字符
std::wstring Widen(const std::string& s)
{
    if (s.empty())
        return std::wstring();
    const int n = ::MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), NULL, 0);
    if (n <= 0)
        return std::wstring();
    std::wstring w(static_cast<size_t>(n), L'\0');
    ::MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), &w[0], n);
    return w;
}

//WinHTTP 句柄的 RAII 包装
class WinHttpHandle
{
public:
    explicit WinHttpHandle(HINTERNET h)
        : m_h(h)
    {
    }

    ~WinHttpHandle()
    {
        if (m_h != NULL)
            ::WinHttpCloseHandle(m_h);
    }

    HINTERNET get() const
    {
        return m_h;
    }

private:
    WinHttpHandle(const WinHttpHandle&);
    WinHttpHandle& operator=(const WinHttpHandle&);

    HINTERNET m_h;  //句柄；为 NULL 时析构什么都不做
};

//记下失败原因
bool Fail(HttpResponse& out, const char* what)
{
    out.m_error = std::string(what) + " failed, winhttp error " + std::to_string(static_cast<long long>(::GetLastError()));
    return false;
}

} // anonymous namespace

bool Post(const std::string& url, const std::vector<std::pair<std::string, std::string> >& headers,
          const std::string& body, int timeoutMs, size_t maxBodyBytes, HttpResponse& out)
{
    out = HttpResponse();
    ParsedUrl u;
    if (!ParseUrl(url, u))
    {
        out.m_error = "bad url";
        return false;
    }
    const std::string proxy = CurrentProxy();
    const std::wstring wProxy = Widen(proxy);
    WinHttpHandle session(::WinHttpOpen(Widen(kUserAgent).c_str(),
                                        proxy.empty() ? WINHTTP_ACCESS_TYPE_NO_PROXY : WINHTTP_ACCESS_TYPE_NAMED_PROXY,
                                        proxy.empty() ? WINHTTP_NO_PROXY_NAME : wProxy.c_str(), WINHTTP_NO_PROXY_BYPASS,
                                        0));
    if (session.get() == NULL)
        return Fail(out, "WinHttpOpen");
    ::WinHttpSetTimeouts(session.get(), timeoutMs, timeoutMs, timeoutMs, timeoutMs);
    WinHttpHandle connect(::WinHttpConnect(session.get(), Widen(u.m_host).c_str(), static_cast<INTERNET_PORT>(u.m_port), 0));
    if (connect.get() == NULL)
        return Fail(out, "WinHttpConnect");
    WinHttpHandle request(::WinHttpOpenRequest(connect.get(), L"POST", Widen(u.m_path).c_str(), NULL, WINHTTP_NO_REFERER,
                                               WINHTTP_DEFAULT_ACCEPT_TYPES, u.m_https ? WINHTTP_FLAG_SECURE : 0));
    if (request.get() == NULL)
        return Fail(out, "WinHttpOpenRequest");
    std::string extra;
    for (size_t i = 0; i < headers.size(); ++i)
        extra += headers[i].first + ": " + headers[i].second + "\r\n";
    const std::wstring wExtra = Widen(extra);
    if (!::WinHttpSendRequest(request.get(), wExtra.empty() ? WINHTTP_NO_ADDITIONAL_HEADERS : wExtra.c_str(),
                              wExtra.empty() ? 0 : static_cast<DWORD>(-1L),
                              body.empty() ? WINHTTP_NO_REQUEST_DATA : const_cast<char*>(body.data()),
                              static_cast<DWORD>(body.size()), static_cast<DWORD>(body.size()), 0))
        return Fail(out, "WinHttpSendRequest");
    if (!::WinHttpReceiveResponse(request.get(), NULL))
        return Fail(out, "WinHttpReceiveResponse");
    DWORD status = 0;
    DWORD statusSize = sizeof(status);
    if (!::WinHttpQueryHeaders(request.get(), WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                               WINHTTP_HEADER_NAME_BY_INDEX, &status, &statusSize, WINHTTP_NO_HEADER_INDEX))
        return Fail(out, "WinHttpQueryHeaders");
    out.m_status = static_cast<int>(status);
    std::vector<char> buf(kReadChunk);
    while (true)
    {
        DWORD read = 0;
        if (!::WinHttpReadData(request.get(), &buf[0], static_cast<DWORD>(buf.size()), &read))
            return Fail(out, "WinHttpReadData");
        if (read == 0)
            break;
        if (out.m_body.size() < maxBodyBytes)
        {
            const size_t room = maxBodyBytes - out.m_body.size();
            out.m_body.append(&buf[0], read < room ? read : room);
        }
    }
    return true;
}

#else // !WIN32

namespace {

//套接字的 RAII 包装
class SocketGuard
{
public:
    explicit SocketGuard(int fd)
        : m_fd(fd)
    {
    }

    ~SocketGuard()
    {
        if (m_fd >= 0)
            ::close(m_fd);
    }

    int get() const
    {
        return m_fd;
    }

private:
    SocketGuard(const SocketGuard&);
    SocketGuard& operator=(const SocketGuard&);

    int m_fd;  //套接字；小于 0 时析构什么都不做
};

//带超时地连接 host:port，返回套接字（失败返回 -1）
int ConnectWithTimeout(const std::string& host, int port, int timeoutMs, std::string& err)
{
    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    struct addrinfo* res = NULL;
    const std::string portStr = std::to_string(static_cast<long long>(port));
    if (::getaddrinfo(host.c_str(), portStr.c_str(), &hints, &res) != 0 || res == NULL)
    {
        err = "resolve failed: " + host;
        return -1;
    }
    int result = -1;
    for (struct addrinfo* ai = res; ai != NULL && result < 0; ai = ai->ai_next)
    {
        const int fd = ::socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
        if (fd < 0)
            continue;
        const int flags = ::fcntl(fd, F_GETFL, 0);
        ::fcntl(fd, F_SETFL, flags | O_NONBLOCK);
        int rc = ::connect(fd, ai->ai_addr, ai->ai_addrlen);
        if (rc != 0 && errno == EINPROGRESS)
        {
            struct pollfd pfd;
            pfd.fd = fd;
            pfd.events = POLLOUT;
            pfd.revents = 0;
            rc = ::poll(&pfd, 1, timeoutMs) == 1 ? 0 : -1;
            if (rc == 0)
            {
                int soErr = 0;
                socklen_t len = sizeof(soErr);
                ::getsockopt(fd, SOL_SOCKET, SO_ERROR, &soErr, &len);
                rc = soErr == 0 ? 0 : -1;
            }
        }
        if (rc == 0)
        {
            ::fcntl(fd, F_SETFL, flags);
            struct timeval tv;
            tv.tv_sec = timeoutMs / 1000;
            tv.tv_usec = (timeoutMs % 1000) * 1000;
            ::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
            ::setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
            result = fd;
        }
        else
        {
            ::close(fd);
        }
    }
    ::freeaddrinfo(res);
    if (result < 0)
        err = "connect failed: " + host + ":" + portStr;
    return result;
}

#if defined(FLAMINGO_HAS_OPENSSL) && OPENSSL_VERSION_NUMBER < 0x10100000L
//OpenSSL 1.0.x（如 CentOS 7 自带的 1.0.2k）与 1.1.0 及以上版本有三处差别，本段只在 1.0.x 下编译：
//一是没有 TLS_client_method 与 SSL_set1_host，startTls 里分别改用 SSLv23_client_method 与
//X509_VERIFY_PARAM_set1_host（1.1.0 起这两个函数就是这样实现的，行为相同）；
//二是不会自动初始化，使用前必须调用 SSL_library_init；
//三是内部没有自带加锁，多个线程同时使用之前必须注册加锁回调，否则内部数据结构会被并发访问破坏。
//MySQL 后端启用时，MariaDB 连接器在 mysql_server_init 里会无条件注册它自己的加锁回调（见
//mariadb-connector-c-3.4.9/libmariadb/secure/openssl.c 的 ma_tls_start）。chatserver 先初始化数据库、后启动
//机器人线程，到这里时回调已经存在，就不再注册；dbtype=sqlite 或 WITH_MYSQL=OFF 时由这里注册。
std::once_flag g_sslInitOnce;      //保证 InitOpenSsl 只执行一次
std::mutex*    g_sslLocks = NULL;  //加锁回调使用的锁数组，共 CRYPTO_num_locks() 把；进程退出前不释放

//OpenSSL 1.0.x 的加锁回调：mode 含 CRYPTO_LOCK 时给第 n 把锁加锁，否则解锁
void SslLockingCallback(int mode, int n, const char* file, int line)
{
    (void)file;
    (void)line;
    if (mode & CRYPTO_LOCK)
        g_sslLocks[n].lock();
    else
        g_sslLocks[n].unlock();
}

//初始化 OpenSSL 1.0.x 并在需要时注册加锁回调，见上文；只经 std::call_once 调用
void InitOpenSsl()
{
    ::SSL_library_init();
    ::SSL_load_error_strings();
    if (::CRYPTO_get_locking_callback() == NULL)
    {
        g_sslLocks = new std::mutex[::CRYPTO_num_locks()];
        ::CRYPTO_set_locking_callback(SslLockingCallback);
    }
}
#endif

#ifdef FLAMINGO_BUNDLED_OPENSSL
//随仓库源码编出的 OpenSSL（cmake/FlamingoDeps.cmake）只会到编译时设定的 OPENSSLDIR 找根证书，而那个目录有意
//不存在，所以要另行探测系统的证书包（utils/CaBundleLocator.h）。只探测一次，结果供之后的每个 TLS 连接使用
std::once_flag g_caBundleOnce;  //保证 ProbeCaBundle 只执行一次
std::string    g_caBundlePath;  //探测到的系统证书包；为空表示不额外加载（设置了环境变量，或一个都没找到）

//探测系统证书包并写一条日志；只经 std::call_once 调用
void ProbeCaBundle()
{
    if (!cabundle::ShouldProbe(::getenv("SSL_CERT_FILE"), ::getenv("SSL_CERT_DIR")))
    {
        LOGI("https: SSL_CERT_FILE or SSL_CERT_DIR is set, root certificates are taken from it");
        return;
    }
    g_caBundlePath = cabundle::FindFirstExisting(cabundle::DefaultCandidates(), cabundle::FileExists);
    if (g_caBundlePath.empty())
        LOGW("https: no system CA bundle found, certificate verification will fail; set SSL_CERT_FILE to the CA bundle");
    else
        LOGI("https: using system CA bundle %s", g_caBundlePath.c_str());
}
#endif

//一条连接上的读写：明文或 TLS
class Channel
{
public:
    explicit Channel(int fd)
        : m_fd(fd)
#ifdef FLAMINGO_HAS_OPENSSL
        , m_ctx(NULL), m_ssl(NULL)
#endif
    {
    }

    ~Channel()
    {
#ifdef FLAMINGO_HAS_OPENSSL
        if (m_ssl != NULL)
        {
            ::SSL_shutdown(m_ssl);
            ::SSL_free(m_ssl);
        }
        if (m_ctx != NULL)
            ::SSL_CTX_free(m_ctx);
#endif
    }

    //在已连接的套接字上做 TLS 握手，校验证书链与主机名
    bool startTls(const std::string& host, std::string& err)
    {
#ifdef FLAMINGO_HAS_OPENSSL
#if OPENSSL_VERSION_NUMBER < 0x10100000L
        std::call_once(g_sslInitOnce, InitOpenSsl);
        //SSLv23_client_method 与 1.1.0 起的 TLS_client_method 一样，协商双方都支持的最高协议版本
        m_ctx = ::SSL_CTX_new(::SSLv23_client_method());
#else
        m_ctx = ::SSL_CTX_new(::TLS_client_method());
#endif
        if (m_ctx == NULL)
        {
            err = "SSL_CTX_new failed";
            return false;
        }
#if OPENSSL_VERSION_NUMBER < 0x10100000L
        //不接受已不安全的 SSLv2 / SSLv3，与 1.1.0 起编译时默认去掉 SSLv3 的行为一致
        ::SSL_CTX_set_options(m_ctx, SSL_OP_NO_SSLv2 | SSL_OP_NO_SSLv3);
#endif
        ::SSL_CTX_set_default_verify_paths(m_ctx);
#ifdef FLAMINGO_BUNDLED_OPENSSL
        std::call_once(g_caBundleOnce, ProbeCaBundle);
        if (!g_caBundlePath.empty())
            ::SSL_CTX_load_verify_locations(m_ctx, g_caBundlePath.c_str(), NULL);
#endif
        ::SSL_CTX_set_verify(m_ctx, SSL_VERIFY_PEER, NULL);
        m_ssl = ::SSL_new(m_ctx);
        if (m_ssl == NULL)
        {
            err = "SSL_new failed";
            return false;
        }
        ::SSL_set_fd(m_ssl, m_fd);
        ::SSL_set_tlsext_host_name(m_ssl, host.c_str());
#if OPENSSL_VERSION_NUMBER < 0x10100000L
        ::X509_VERIFY_PARAM_set1_host(::SSL_get0_param(m_ssl), host.c_str(), 0);
#else
        ::SSL_set1_host(m_ssl, host.c_str());
#endif
        if (::SSL_connect(m_ssl) != 1 || ::SSL_get_verify_result(m_ssl) != X509_V_OK)
        {
            err = "tls handshake or certificate verification failed";
            return false;
        }
        return true;
#else
        (void)host;
        err = "https not supported: built without openssl";
        return false;
#endif
    }

    //写全部字节
    bool writeAll(const std::string& data)
    {
        size_t sent = 0;
        while (sent < data.size())
        {
            int n = 0;
#ifdef FLAMINGO_HAS_OPENSSL
            if (m_ssl != NULL)
                n = ::SSL_write(m_ssl, data.data() + sent, static_cast<int>(data.size() - sent));
            else
#endif
                n = static_cast<int>(::send(m_fd, data.data() + sent, data.size() - sent, 0));
            if (n <= 0)
                return false;
            sent += static_cast<size_t>(n);
        }
        return true;
    }

    //读一段；返回读到的字节数，0 表示对方关闭，小于 0 表示出错或超时
    int readSome(char* buf, size_t len)
    {
#ifdef FLAMINGO_HAS_OPENSSL
        if (m_ssl != NULL)
            return ::SSL_read(m_ssl, buf, static_cast<int>(len));
#endif
        return static_cast<int>(::recv(m_fd, buf, len, 0));
    }

private:
    int      m_fd;   //已连接的套接字；不拥有
#ifdef FLAMINGO_HAS_OPENSSL
    SSL_CTX* m_ctx;  //TLS 上下文
    SSL*     m_ssl;  //TLS 连接；为 NULL 表示明文
#endif
};

} // anonymous namespace

bool Post(const std::string& url, const std::vector<std::pair<std::string, std::string> >& headers,
          const std::string& body, int timeoutMs, size_t maxBodyBytes, HttpResponse& out)
{
    out = HttpResponse();
    ParsedUrl u;
    if (!ParseUrl(url, u))
    {
        out.m_error = "bad url";
        return false;
    }
    const std::string proxy = CurrentProxy();
    std::string connectHost = u.m_host;
    int connectPort = u.m_port;
    if (!proxy.empty())
    {
        ParsedUrl p;
        if (!ParseUrl("http://" + proxy, p))
        {
            out.m_error = "bad proxy";
            return false;
        }
        connectHost = p.m_host;
        connectPort = p.m_port;
    }
    SocketGuard sock(ConnectWithTimeout(connectHost, connectPort, timeoutMs, out.m_error));
    if (sock.get() < 0)
        return false;
    Channel ch(sock.get());
    const std::string hostHeader = u.m_host + ":" + std::to_string(static_cast<long long>(u.m_port));

    //经代理访问 https：先用 CONNECT 打通隧道
    if (!proxy.empty() && u.m_https)
    {
        const std::string req = "CONNECT " + hostHeader + " HTTP/1.1\r\nHost: " + hostHeader + "\r\n\r\n";
        if (!ch.writeAll(req))
        {
            out.m_error = "proxy connect write failed";
            return false;
        }
        std::string resp;
        char buf[1024];
        while (resp.find(kHeaderEnd) == std::string::npos && resp.size() < kMaxHeaderBytes)
        {
            const int n = ch.readSome(buf, sizeof(buf));
            if (n <= 0)
                break;
            resp.append(buf, static_cast<size_t>(n));
        }
        int status = 0;
        std::string ignored;
        bool complete = false;
        ParseResponse(resp, status, ignored, complete);
        if (status != 200)
        {
            out.m_error = "proxy connect refused";
            return false;
        }
    }
    if (u.m_https && !ch.startTls(u.m_host, out.m_error))
        return false;

    const std::string target = (!proxy.empty() && !u.m_https) ? url : u.m_path;
    std::string req = "POST " + target + " HTTP/1.1\r\nHost: " + hostHeader + "\r\nUser-Agent: " + kUserAgent +
                      "\r\nConnection: close\r\nContent-Length: " + std::to_string(static_cast<long long>(body.size())) +
                      "\r\n";
    for (size_t i = 0; i < headers.size(); ++i)
        req += headers[i].first + ": " + headers[i].second + "\r\n";
    req += "\r\n";
    req += body;
    if (!ch.writeAll(req))
    {
        out.m_error = "write failed";
        return false;
    }

    //读到报文完整或连接关闭为止；正文超过上限时停止保留但仍判断完整性
    std::string raw;
    std::vector<char> buf(kReadChunk);
    bool closed = false;
    while (true)
    {
        int status = 0;
        std::string parsedBody;
        bool complete = false;
        if (ParseResponse(raw, status, parsedBody, complete) && complete)
            break;
        const int n = ch.readSome(&buf[0], buf.size());
        if (n == 0)
        {
            closed = true;
            break;
        }
        if (n < 0)
        {
            out.m_error = "read failed or timed out";
            return false;
        }
        raw.append(&buf[0], static_cast<size_t>(n));
        if (raw.size() > maxBodyBytes + kMaxHeaderBytes)
            break;
    }
    int status = 0;
    bool complete = false;
    if (!ParseResponse(raw, status, out.m_body, complete))
    {
        out.m_error = "bad response";
        return false;
    }
    if (!complete && !closed)
    {
        //超过上限提前停止：保留已收到的部分
        complete = true;
    }
    out.m_status = status;
    if (out.m_body.size() > maxBodyBytes)
        out.m_body.resize(maxBodyBytes);
    return true;
}

#endif // WIN32

} // namespace httpclient
