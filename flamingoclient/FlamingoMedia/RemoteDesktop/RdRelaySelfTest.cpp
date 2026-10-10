/**
 *  远程桌面中转自环自测（调试入口）实现，RdRelaySelfTest.cpp
 *  balloonwj@qq.com   2026-06-09
 */
#include "RdRelaySelfTest.h"

#include <winsock2.h>          // 确保 winsock2 在任何 windows.h 之前
#include "RdRelayClient.h"

#include <sstream>
#include <vector>
#include <mutex>
#include <condition_variable>
#include <chrono>
#include <string.h>
#include <time.h>

//==== MD5 原始算法 C 接口（全局命名空间，定义在 MD5Sum.cpp）====
//此处仅复制声明以复用其算法，避免 include 带 CString 的 MD5Sum.h 引入 MFC/ATL 依赖。
//必须置于全局命名空间（与 MD5Sum.h 一致）：若并入 remotedesktop:: 则符号 mangling
//变成 remotedesktop::MD5Init，与 MD5Sum.cpp 定义的全局 ::MD5Init 对不上、链接失败。
//MD5_CTX 的内存布局须与 MD5Sum.h 完全一致（UINT4=unsigned long int，Win32 下 4 字节）。
typedef unsigned long int RdSelfTestUint4;
typedef struct
{
    RdSelfTestUint4 state[4];
    RdSelfTestUint4 count[2];
    unsigned char   buffer[64];
} MD5_CTX;
void MD5Init(MD5_CTX*);
void MD5Update(MD5_CTX*, const unsigned char*, unsigned int);
void MD5Final(unsigned char[16], MD5_CTX*);

namespace remotedesktop {

//HMAC 分组字节数（MD5 分组 64 字节）
static const size_t kHmacBlockSize = 64;
//HMAC 内 / 外层填充常量
static const unsigned char kHmacIpad = 0x36;
static const unsigned char kHmacOpad = 0x5c;
//MD5 原始摘要长度
static const size_t kMd5DigestLen = 16;
//角色：与服务端 RelayToken::role_* 一致
static const int kRoleController = 1;
static const int kRoleControlled = 2;
//令牌默认有效期：5 分钟（毫秒）
static const long long kTokenTtlMs = 5LL * 60 * 1000;
//单方向收帧等待超时（毫秒）
static const int kWaitFrameMs = 3000;

//计算一段数据的 MD5 原始摘要（16 字节）
static std::string rawMd5(const std::string& data)
{
    MD5_CTX ctx;
    MD5Init(&ctx);
    MD5Update(&ctx, reinterpret_cast<const unsigned char*>(data.data()),
              static_cast<unsigned int>(data.size()));
    unsigned char digest[16];
    MD5Final(digest, &ctx);
    return std::string(reinterpret_cast<const char*>(digest), kMd5DigestLen);
}

//把原始字节串转为小写十六进制（与服务端 MD5::bytesToHexString 一致）
static std::string toHexLower(const std::string& raw)
{
    static const char HEX[] = "0123456789abcdef";
    std::string out;
    out.reserve(raw.size() * 2);
    for (size_t i = 0; i < raw.size(); ++i)
    {
        unsigned char b = static_cast<unsigned char>(raw[i]);
        out.push_back(HEX[b >> 4]);
        out.push_back(HEX[b & 0x0F]);
    }
    return out;
}

//计算 HMAC-MD5(key, msg)，返回小写十六进制（复刻服务端 RelayToken::hmacMd5Hex）
static std::string hmacMd5Hex(const std::string& key, const std::string& msg)
{
    std::string k = key;
    if (k.size() > kHmacBlockSize)
        k = rawMd5(k);
    k.resize(kHmacBlockSize, '\0');

    std::string ipad(kHmacBlockSize, '\0');
    std::string opad(kHmacBlockSize, '\0');
    for (size_t i = 0; i < kHmacBlockSize; ++i)
    {
        unsigned char kb = static_cast<unsigned char>(k[i]);
        ipad[i] = static_cast<char>(kb ^ kHmacIpad);
        opad[i] = static_cast<char>(kb ^ kHmacOpad);
    }

    std::string inner = rawMd5(ipad + msg);
    return toHexLower(rawMd5(opad + inner));
}

//按 payload 与密钥签发一枚令牌（复刻服务端 RelayToken::sign）
static std::string signToken(const std::string& sessionId, int role,
                             long long uid, long long peerUid, long long expireMs,
                             const std::string& key)
{
    std::ostringstream os;
    os << sessionId << "|" << role << "|" << uid << "|" << peerUid << "|" << expireMs;
    std::string payload = os.str();
    return payload + "." + hmacMd5Hex(key, payload);
}

//自环收帧 handler：把收到的第一帧引擎字节存下并唤醒等待者。
class SelfTestHandler : public RdRelayClientHandler
{
public:
    SelfTestHandler()
        : m_closed(false)
    {
    }

    virtual void OnRelayEngineFrame(const unsigned char* data, size_t len) override
    {
        std::lock_guard<std::mutex> guard(m_mtx);
        m_received.assign(data, data + len);
        m_cv.notify_all();
    }

    virtual void OnRelayClosed() override
    {
        std::lock_guard<std::mutex> guard(m_mtx);
        m_closed = true;
        m_cv.notify_all();
    }

    //阻塞等待收到一帧；超时 / 连接断开返回 false。
    bool WaitFrame(std::vector<unsigned char>& out, int timeoutMs)
    {
        std::unique_lock<std::mutex> lk(m_mtx);
        std::chrono::steady_clock::time_point deadline =
            std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
        while (m_received.empty() && !m_closed)
        {
            if (m_cv.wait_until(lk, deadline) == std::cv_status::timeout)
                break;
        }
        if (m_received.empty())
            return false;
        out.swap(m_received);
        m_received.clear();
        return true;
    }

private:
    std::mutex                 m_mtx;        //保护 m_received / m_closed
    std::condition_variable    m_cv;         //收到帧 / 断开时唤醒等待者
    std::vector<unsigned char> m_received;   //最近收到的一帧引擎字节
    bool                       m_closed;     //连接是否已断开
};

std::string RunRdRelaySelfTest(const std::string& ip, int port,
                               const std::string& tokenKey, const unsigned char* serverPubKey, bool& outOk)
{
    outOk = false;
    std::ostringstream rep;

    //同一 sessionid、相反 role，imgserver 据此把两条连接配对。sessionid 不含 "." / "|"。
    std::string sessionId = "selftest-relay-0";
    long long expireMs = static_cast<long long>(time(NULL)) * 1000 + kTokenTtlMs;
    std::string tokController = signToken(sessionId, kRoleController, 1001, 1002, expireMs, tokenKey);
    std::string tokControlled = signToken(sessionId, kRoleControlled, 1002, 1001, expireMs, tokenKey);

    SelfTestHandler hController;
    SelfTestHandler hControlled;
    RdRelayClient   cController;
    RdRelayClient   cControlled;

    //先连被控端（单端先登记、等待对端），再连控制端促成配对
    if (!cControlled.Connect(ip, port, tokControlled, serverPubKey, &hControlled, 5))
    {
        rep << "被控端连接 / 握手中转失败（imgserver 是否在跑、relaytokenkey 是否一致？）";
        return rep.str();
    }
    if (!cController.Connect(ip, port, tokController, serverPubKey, &hController, 5))
    {
        rep << "控制端连接 / 握手中转失败";
        cControlled.Close();
        return rep.str();
    }
    rep << "双方已连上中转并握手成功（" << ip << ":" << port << "）\n";

    //方向一：控制端 → 被控端（模拟控制命令）
    unsigned char cmdFrame[] = { 0x02, 'P', 'I', 'N', 'G', 0x01 };
    cController.SendEngineFrame(cmdFrame, sizeof(cmdFrame));
    std::vector<unsigned char> got1;
    bool ok1 = hControlled.WaitFrame(got1, kWaitFrameMs)
               && got1.size() == sizeof(cmdFrame)
               && memcmp(got1.data(), cmdFrame, sizeof(cmdFrame)) == 0;
    rep << "控制端 → 被控端透传：" << (ok1 ? "收到且内容一致" : "失败") << "\n";

    //方向二：被控端 → 控制端，发一个超过分片大小(512KB)的大帧，验证「分片 → 重组」
    //端到端经真实 imgserver 中转后内容仍逐字节一致（模拟首屏全量帧需分片的场景）
    std::vector<unsigned char> bigFrame(700 * 1024);
    for (size_t i = 0; i < bigFrame.size(); ++i)
        bigFrame[i] = static_cast<unsigned char>((i * 131 + 17) & 0xFF);
    cControlled.SendEngineFrame(bigFrame.data(), bigFrame.size());
    std::vector<unsigned char> got2;
    bool ok2 = hController.WaitFrame(got2, kWaitFrameMs)
               && got2.size() == bigFrame.size()
               && memcmp(got2.data(), bigFrame.data(), bigFrame.size()) == 0;
    rep << "被控端 → 控制端透传（含分片重组，" << (bigFrame.size() / 1024)
        << "KB）：" << (ok2 ? "收到且内容一致" : "失败") << "\n";

    cController.Close();
    cControlled.Close();

    outOk = ok1 && ok2;
    rep << (outOk ? "自环通过：中转双向透传正常。" : "自环失败：见上方各方向结果。");
    return rep.str();
}

} // namespace remotedesktop
