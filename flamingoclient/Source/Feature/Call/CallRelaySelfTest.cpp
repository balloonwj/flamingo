/**
 *  音视频通话中转自环自测（调试入口）实现，CallRelaySelfTest.cpp
 *  balloonwj@qq.com   2026-06-11
 */
#include "CallRelaySelfTest.h"

#include <winsock2.h>          // 确保 winsock2 在任何 windows.h 之前
#include "CallRelayClient.h"
#include "CallRelayTestFrame.h"
#include "../../net/CallProtocolClient.h"   // net::call_role_caller / call_role_callee
#include "../../Common/Log/IULog.h"                    // LOG_INFO / LOG_ERROR（把自测结论也落客户端日志，便于无人值守核对）

#include <sstream>
#include <vector>
#include <mutex>
#include <condition_variable>
#include <chrono>
#include <string.h>
#include <time.h>

//==== MD5 原始算法 C 接口（全局命名空间，定义在 MD5Sum.cpp）====
//此处仅复制声明以复用其算法，避免 include 带 CString 的 MD5Sum.h 引入 MFC/ATL 依赖。
//必须置于全局命名空间（与 MD5Sum.h 一致）：若并入 call:: 则符号 mangling 变成
//call::MD5Init，与 MD5Sum.cpp 定义的全局 ::MD5Init 对不上、链接失败。
//MD5_CTX 的内存布局须与 MD5Sum.h 完全一致（UINT4=unsigned long int，Win32 下 4 字节）。
typedef unsigned long int CallSelfTestUint4;
typedef struct
{
    CallSelfTestUint4 state[4];
    CallSelfTestUint4 count[2];
    unsigned char     buffer[64];
} MD5_CTX;
void MD5Init(MD5_CTX*);
void MD5Update(MD5_CTX*, const unsigned char*, unsigned int);
void MD5Final(unsigned char[16], MD5_CTX*);

namespace call {

//HMAC 分组字节数（MD5 分组 64 字节）
static const size_t kHmacBlockSize = 64;
//HMAC 内 / 外层填充常量
static const unsigned char kHmacIpad = 0x36;
static const unsigned char kHmacOpad = 0x5c;
//MD5 原始摘要长度
static const size_t kMd5DigestLen = 16;
//令牌默认有效期：5 分钟（毫秒）
static const long long kTokenTtlMs = 5LL * 60 * 1000;
//单方向收帧等待超时（毫秒）
static const int kWaitFrameMs = 3000;
//自测发出的两帧测试帧序号：方向一(主叫→被叫)用 1，方向二(被叫→主叫)用 2
static const uint32_t kSeqCallerToCallee = 1u;
static const uint32_t kSeqCalleeToCaller = 2u;

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

//自环收帧 handler：把收到的第一帧存下并唤醒等待者。
class SelfTestHandler : public CallRelayClientHandler
{
public:
    SelfTestHandler()
        : m_closed(false)
    {
    }

    virtual void OnRelayData(const unsigned char* data, size_t len) override
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
    std::vector<unsigned char> m_received;   //最近收到的一帧包体
    bool                       m_closed;     //连接是否已断开
};

//校验收到的帧是否为期望序号的测试帧（长度 + 魔数 + 载荷逐字节一致）。
static bool isExpectedTestFrame(const std::vector<unsigned char>& got, uint32_t expectSeq)
{
    if (got.empty())
        return false;
    uint32_t seq = 0;
    if (!ParseTestFrame(got.data(), got.size(), seq))
        return false;
    return seq == expectSeq;
}

std::string RunCallRelaySelfTest(const std::string& ip, int port,
                                 const std::string& tokenKey, bool& outOk)
{
    outOk = false;
    std::ostringstream rep;

    //同一 sessionid、相反 role（主叫 3 / 被叫 4），imgserver 据此把两条连接配对。
    //sessionid 不含 "." / "|"（令牌分隔符）。
    std::string sessionId = "selftest-call-0";
    long long expireMs = static_cast<long long>(time(NULL)) * 1000 + kTokenTtlMs;
    std::string tokCaller = signToken(sessionId, net::call_role_caller, 2001, 2002, expireMs, tokenKey);
    std::string tokCallee = signToken(sessionId, net::call_role_callee, 2002, 2001, expireMs, tokenKey);

    SelfTestHandler hCaller;
    SelfTestHandler hCallee;
    CallRelayClient cCaller;
    CallRelayClient cCallee;

    //先连被叫端（单端先登记、等待对端），再连主叫端促成配对
    if (!cCallee.Connect(ip, port, tokCallee, &hCallee, 5))
    {
        rep << "被叫端连接 / 握手中转失败（imgserver 是否在跑、relaytokenkey 是否一致？）";
        return rep.str();
    }
    if (!cCaller.Connect(ip, port, tokCaller, &hCaller, 5))
    {
        rep << "主叫端连接 / 握手中转失败";
        cCallee.Close();
        return rep.str();
    }
    rep << "双方已连上中转并握手成功（" << ip << ":" << port << "）\n";

    //方向一：主叫 → 被叫，发一帧测试帧
    std::string frame1 = BuildTestFrame(kSeqCallerToCallee);
    cCaller.SendData(reinterpret_cast<const unsigned char*>(frame1.data()), frame1.size());
    std::vector<unsigned char> got1;
    bool ok1 = hCallee.WaitFrame(got1, kWaitFrameMs) && isExpectedTestFrame(got1, kSeqCallerToCallee);
    rep << "主叫 → 被叫透传：" << (ok1 ? "收到且测试帧校验一致" : "失败") << "\n";
    LOG_INFO("CallRelaySelfTest: caller->callee transport %s (seq=%u).",
             ok1 ? "ok" : "FAILED", kSeqCallerToCallee);

    //方向二：被叫 → 主叫，发一帧测试帧
    std::string frame2 = BuildTestFrame(kSeqCalleeToCaller);
    cCallee.SendData(reinterpret_cast<const unsigned char*>(frame2.data()), frame2.size());
    std::vector<unsigned char> got2;
    bool ok2 = hCaller.WaitFrame(got2, kWaitFrameMs) && isExpectedTestFrame(got2, kSeqCalleeToCaller);
    rep << "被叫 → 主叫透传：" << (ok2 ? "收到且测试帧校验一致" : "失败") << "\n";
    LOG_INFO("CallRelaySelfTest: callee->caller transport %s (seq=%u).",
             ok2 ? "ok" : "FAILED", kSeqCalleeToCaller);

    cCaller.Close();
    cCallee.Close();

    outOk = ok1 && ok2;
    LOG_INFO("CallRelaySelfTest: overall %s (caller->callee=%d, callee->caller=%d).",
             outOk ? "PASS" : "FAIL", ok1 ? 1 : 0, ok2 ? 1 : 0);
    rep << (outOk ? "自环通过：通话中转双向透传正常。" : "自环失败：见上方各方向结果。");
    return rep.str();
}

} // namespace call
