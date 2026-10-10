/**
 *  一对一通话质量报告的实网用例（标签 [.live][callreport]，默认不运行；docs/音视频通话-网络状况标识方案.md 第 8.2 节），
 *  CallReportLiveTests.cpp
 *
 *  两个 CallMediaSession（主叫 / 被叫）在同一进程里经一台真实的 imgserver 中转互连，音频引擎走无设备模式
 *  （FLAMINGO_CALL_NO_AUDIO=1，不碰声卡），验证网络状况标识第 1、2 期在会话层的实际效果：
 *    - CRL1 双方每秒互发质量报告：各自收到至少 3 份对方报告、算出了往返时延（本机中转应远小于 1 秒）；
 *           日志里出现 5 秒一条的「call stats:」与「first quality report from peer」；第 3 期起报告里带档位，
 *           无设备模式下没有音频包、只按往返时延评级，本机中转为「良好」；
 *    - CRL2（2026-10-06，第 3 期 3d 弱网校准，标签 [.live][callcal]）：两端各开一个线程，每 20 毫秒经 FeedTestCapture
 *           喂一帧正弦波、经 PullTestRender 取一帧播放，让音频包真的在中转上流动；每秒记录双方的档位、往返时延、
 *           丢包、抖动与卡顿。FLAMINGO_TEST_RELAY 指向 tmp/callaudio/netem_proxy.py 这样的弱网代理时即可对比不同网络
 *           条件下的评级。可选环境变量：FLAMINGO_TEST_CAL_SECONDS（运行秒数，默认 20）、FLAMINGO_TEST_CAL_EXPECT
 *           （后半段最常出现的档位应落在的范围，如 "3-4"）、FLAMINGO_TEST_CAL_CSV（把每秒的记录写到这个文件）。
 *  中转令牌用 CallRelaySelfTest 的自签函数生成，不需要登录账号、不联 chatserver。须先设三个环境变量，否则跳过：
 *      FLAMINGO_TEST_RELAY              imgserver 中转地址，如 127.0.0.1:28003
 *      FLAMINGO_TEST_RELAY_TOKENKEY     与该 imgserver 的 relaytokenkey 相同
 *      FLAMINGO_TEST_SERVER_PUBKEY_FILE 该 imgserver 的 server_key.pub（base64 的 32 字节身份公钥）
 *  运行：FlamingoUnit.exe "[callreport]"
 *  框架 Catch2 v3。
 *  balloonwj@qq.com   2026-10-05
 */
#include "stdafx.h"
#include "catch_amalgamated.hpp"

#include "../../FlamingoMedia/Call/CallMediaSession.h"   //先引入：其内含 winsock2.h，须在 windows.h 之前
#include "../../FlamingoMedia/Call/CallRelaySelfTest.h"
#include "../../FlamingoMedia/Call/CallProtocolClient.h"
#include "../Common/Log/IULog.h"

#include <sodium.h>
#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <math.h>
#include <mmsystem.h>   //timeBeginPeriod：CRL2 让 Sleep 的精度到 1 毫秒，喂声音的节奏才均匀
#include <fstream>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

using namespace call;

namespace {

//环境变量名
const char* const kCrlEnvRelay = "FLAMINGO_TEST_RELAY";
const char* const kCrlEnvTokenKey = "FLAMINGO_TEST_RELAY_TOKENKEY";
const char* const kCrlEnvPubKeyFile = "FLAMINGO_TEST_SERVER_PUBKEY_FILE";
//读环境变量用的缓冲（字符数）
const DWORD kCrlEnvChars = 512;
//两端的 userid（只用于签令牌，与真实账号无关）
const long long kCrlCallerUid = 3001;
const long long kCrlCalleeUid = 3002;
//令牌有效期（毫秒）
const long long kCrlTokenTtlMs = 5LL * 60 * 1000;
//每秒的毫秒数
const long long kCrlMsPerSecond = 1000;
//等待双方都满足条件的最长时间（毫秒）与轮询间隔：报告每秒一份，3 份约 3~4 秒；统计日志第一条在启动后 5 秒
const ULONGLONG kCrlWaitMs = 10000;
const DWORD kCrlPollMs = 100;
//至少要等这么久（毫秒），保证第一条 5 秒一次的统计日志已经写出
const ULONGLONG kCrlMinRunMs = 6000;
//期望收到的对方报告份数下限
const uint64_t kCrlMinReports = 3;
//本机中转的往返时延上限（毫秒）
const uint32_t kCrlMaxLocalRttMs = 1000;

//读一个环境变量；没设时返回空串
std::string CrlEnv(const char* name)
{
    char buf[kCrlEnvChars] = { 0 };
    const DWORD n = ::GetEnvironmentVariableA(name, buf, kCrlEnvChars);
    if (n == 0 || n >= kCrlEnvChars)
        return std::string();
    return std::string(buf, n);
}

//读 server_key.pub（base64）解出 32 字节公钥
bool CrlLoadPubKey(const std::string& path, std::vector<unsigned char>& out)
{
    std::ifstream f(path.c_str(), std::ios::binary);
    if (!f)
        return false;
    std::stringstream ss;
    ss << f.rdbuf();
    std::string b64 = ss.str();
    while (!b64.empty() && (b64[b64.size() - 1] == '\n' || b64[b64.size() - 1] == '\r' || b64[b64.size() - 1] == ' '))
        b64.erase(b64.size() - 1);
    out.assign(net::IDENTITY_PUBLICKEY_BYTES, 0);
    size_t binLen = 0;
    if (sodium_base642bin(&out[0], out.size(), b64.c_str(), b64.size(), NULL, &binLen, NULL,
                          sodium_base64_VARIANT_ORIGINAL) != 0)
        return false;
    return binLen == net::IDENTITY_PUBLICKEY_BYTES;
}

//把某个目录下以 prefix 开头的文件内容全部读出来拼在一起（CIULog 按前缀加时间与进程号生成文件名）
std::string CrlReadLogs(const std::wstring& dir, const std::wstring& prefix)
{
    std::string all;
    WIN32_FIND_DATAW fd;
    HANDLE h = ::FindFirstFileW((dir + prefix + L"*").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE)
        return all;
    do
    {
        std::ifstream f((dir + fd.cFileName).c_str(), std::ios::binary);
        std::stringstream ss;
        ss << f.rdbuf();
        all += ss.str();
    } while (::FindNextFileW(h, &fd));
    ::FindClose(h);
    return all;
}

} // namespace

TEST_CASE("CRL1 两端经真实中转每秒互发质量报告：都收到报告、算出往返时延、日志里有统计行", "[.live][callreport]")
{
    const std::string relay = CrlEnv(kCrlEnvRelay);
    const std::string tokenKey = CrlEnv(kCrlEnvTokenKey);
    const std::string pubKeyFile = CrlEnv(kCrlEnvPubKeyFile);
    if (relay.empty() || tokenKey.empty() || pubKeyFile.empty())
        SKIP("set FLAMINGO_TEST_RELAY / FLAMINGO_TEST_RELAY_TOKENKEY / FLAMINGO_TEST_SERVER_PUBKEY_FILE to run");
    const size_t colon = relay.rfind(':');
    REQUIRE(colon != std::string::npos);
    const std::string ip = relay.substr(0, colon);
    const int port = atoi(relay.substr(colon + 1).c_str());
    std::vector<unsigned char> pubKey;
    REQUIRE(sodium_init() >= 0);
    REQUIRE(CrlLoadPubKey(pubKeyFile, pubKey));

    //音频引擎不碰声卡；日志写到临时目录，结束后检查
    REQUIRE(_putenv_s("FLAMINGO_CALL_NO_AUDIO", "1") == 0);
    WSADATA wsa;
    REQUIRE(::WSAStartup(MAKEWORD(2, 2), &wsa) == 0);
    wchar_t tempDir[MAX_PATH] = { 0 };
    ::GetTempPathW(MAX_PATH, tempDir);
    const std::wstring logPrefix = L"callreport_live";
    CIULog::Init(true, false, (std::wstring(tempDir) + logPrefix).c_str());

    //同一会话号、相反角色的两枚令牌；会话号每次不同，避免与上一次运行残留的中转配对冲突
    std::ostringstream sid;
    sid << "callreport-" << ::GetTickCount64();
    const long long expireMs = static_cast<long long>(time(NULL)) * kCrlMsPerSecond + kCrlTokenTtlMs;
    const std::string tokCaller = SignCallRelayToken(sid.str(), net::call_role_caller, kCrlCallerUid, kCrlCalleeUid,
                                                     expireMs, tokenKey);
    const std::string tokCallee = SignCallRelayToken(sid.str(), net::call_role_callee, kCrlCalleeUid, kCrlCallerUid,
                                                     expireMs, tokenKey);

    CallNetSnapshot sa;
    CallNetSnapshot sb;
    {
        CallMediaSession a;
        CallMediaSession b;
        a.Start(ip, port, tokCaller, &pubKey[0], net::call_role_caller, sid.str(), NULL, 0, false);
        b.Start(ip, port, tokCallee, &pubKey[0], net::call_role_callee, sid.str(), NULL, 0, false);
        const ULONGLONG start = ::GetTickCount64();
        while (::GetTickCount64() - start < kCrlWaitMs)
        {
            ::Sleep(kCrlPollMs);
            sa = a.GetNetSnapshot();
            sb = b.GetNetSnapshot();
            const bool done = sa.m_peerReports >= kCrlMinReports && sb.m_peerReports >= kCrlMinReports && sa.m_hasRtt
                              && sb.m_hasRtt;
            if (done && ::GetTickCount64() - start >= kCrlMinRunMs)
                break;
        }
        INFO("caller: path=" << a.Path() << " reports=" << sa.m_peerReports << " rtt=" << sa.m_rttMs
             << "; callee: path=" << b.Path() << " reports=" << sb.m_peerReports << " rtt=" << sb.m_rttMs);
        CHECK(a.Path() == call_media_path_relay);
        CHECK(b.Path() == call_media_path_relay);
        a.Stop();
        b.Stop();
    }
    CIULog::Uninit();
    ::WSACleanup();

    INFO("caller reports=" << sa.m_peerReports << " rtt=" << sa.m_rttMs << "; callee reports=" << sb.m_peerReports
         << " rtt=" << sb.m_rttMs);
    CHECK(sa.m_peerReports >= kCrlMinReports);
    CHECK(sb.m_peerReports >= kCrlMinReports);
    REQUIRE(sa.m_hasRtt);
    REQUIRE(sb.m_hasRtt);
    CHECK(sa.m_rttMs < kCrlMaxLocalRttMs);
    CHECK(sb.m_rttMs < kCrlMaxLocalRttMs);
    //第 3 期起报告里带档位：无设备模式下没有音频包，只按往返时延评级，本机中转为「良好」
    CHECK(sa.m_peerReport.m_grade == call_grade_good);
    CHECK(sb.m_peerReport.m_grade == call_grade_good);

    const std::string logs = CrlReadLogs(tempDir, logPrefix);
    CHECK(logs.find("call stats:") != std::string::npos);
    CHECK(logs.find("first quality report from peer") != std::string::npos);
    CHECK(logs.find("audio=no-devices") != std::string::npos);
}

#pragma comment(lib, "winmm.lib")

namespace {

//CRL2 的环境变量：运行时长（秒）、期望档位范围（形如 "3-4"）、结果 CSV 文件路径
const char* const kCalEnvSeconds = "FLAMINGO_TEST_CAL_SECONDS";
const char* const kCalEnvExpect = "FLAMINGO_TEST_CAL_EXPECT";
const char* const kCalEnvCsv = "FLAMINGO_TEST_CAL_CSV";
//默认运行时长（秒）：防抖升档要 3 秒、降档要 2 秒，20 秒足够看清稳定后的档位
const int kCalDefaultSeconds = 20;
//一帧采集 / 播放的时长（毫秒）与采样个数（48 kHz）
const int kCalFrameMs = 20;
const int kCalFrameSamples = 960;
//正弦波的频率（赫兹）、幅度与采样率
const double kCalToneHz = 440.0;
const double kCalToneAmp = 8000.0;
const double kCalSampleRate = 48000.0;
//圆周率
const double kCalPi = 3.14159265358979323846;
//记录一行指标的间隔（毫秒）
const DWORD kCalSampleMs = 1000;
//档位个数（0~4）
const int kCalGradeCount = 5;
//运行期间的系统计时器精度（毫秒）
const UINT kCalTimerResolutionMs = 1;

//一个音频线程的参数：每 20 毫秒给会话喂一帧正弦波采集、取一帧播放，直到 m_stop 置位
struct CalPumpArgs
{
    CallMediaSession* m_session;   //会话（借用）
    volatile bool     m_stop;      //为 true 时线程退出
};

//高精度的当前时刻（毫秒）
double CalNowMs()
{
    LARGE_INTEGER freq;
    LARGE_INTEGER now;
    ::QueryPerformanceFrequency(&freq);
    ::QueryPerformanceCounter(&now);
    return static_cast<double>(now.QuadPart) * kCrlMsPerSecond / static_cast<double>(freq.QuadPart);
}

//音频线程体
void CalAudioPump(CalPumpArgs* args)
{
    std::vector<int16_t> tone(kCalFrameSamples);
    std::vector<int16_t> render(kCalFrameSamples);
    double phase = 0.0;
    const double step = 2.0 * kCalPi * kCalToneHz / kCalSampleRate;
    double next = CalNowMs();
    while (!args->m_stop)
    {
        for (int i = 0; i < kCalFrameSamples; ++i)
        {
            tone[i] = static_cast<int16_t>(kCalToneAmp * sin(phase));
            phase += step;
        }
        args->m_session->FeedTestCapture(&tone[0], kCalFrameSamples);
        args->m_session->PullTestRender(&render[0], kCalFrameSamples);
        next += kCalFrameMs;
        const double wait = next - CalNowMs();
        if (wait > 1.0)
            ::Sleep(static_cast<DWORD>(wait));
    }
}

//解析 "a-b" 形式的档位范围
bool CalParseRange(const std::string& s, int& lo, int& hi)
{
    const size_t dash = s.find('-');
    if (dash == std::string::npos)
        return false;
    lo = atoi(s.substr(0, dash).c_str());
    hi = atoi(s.substr(dash + 1).c_str());
    return lo <= hi;
}

} // namespace

TEST_CASE("CRL2 弱网校准：两端经（可夹弱网代理的）真实中转收发音频，每秒记录档位与各项指标", "[.live][callcal]")
{
    const std::string relay = CrlEnv(kCrlEnvRelay);
    const std::string tokenKey = CrlEnv(kCrlEnvTokenKey);
    const std::string pubKeyFile = CrlEnv(kCrlEnvPubKeyFile);
    if (relay.empty() || tokenKey.empty() || pubKeyFile.empty())
        SKIP("set FLAMINGO_TEST_RELAY / FLAMINGO_TEST_RELAY_TOKENKEY / FLAMINGO_TEST_SERVER_PUBKEY_FILE to run");
    const size_t colon = relay.rfind(':');
    REQUIRE(colon != std::string::npos);
    const std::string ip = relay.substr(0, colon);
    const int port = atoi(relay.substr(colon + 1).c_str());
    std::vector<unsigned char> pubKey;
    REQUIRE(sodium_init() >= 0);
    REQUIRE(CrlLoadPubKey(pubKeyFile, pubKey));
    int seconds = atoi(CrlEnv(kCalEnvSeconds).c_str());
    if (seconds <= 0)
        seconds = kCalDefaultSeconds;
    std::string csvPath = CrlEnv(kCalEnvCsv);

    REQUIRE(_putenv_s("FLAMINGO_CALL_NO_AUDIO", "1") == 0);
    WSADATA wsa;
    REQUIRE(::WSAStartup(MAKEWORD(2, 2), &wsa) == 0);
    wchar_t tempDir[MAX_PATH] = { 0 };
    ::GetTempPathW(MAX_PATH, tempDir);
    CIULog::Init(true, false, (std::wstring(tempDir) + L"callcal_live").c_str());

    std::ostringstream sid;
    sid << "callcal-" << ::GetTickCount64();
    const long long expireMs = static_cast<long long>(time(NULL)) * kCrlMsPerSecond + kCrlTokenTtlMs;
    const std::string tokCaller = SignCallRelayToken(sid.str(), net::call_role_caller, kCrlCallerUid, kCrlCalleeUid,
                                                     expireMs, tokenKey);
    const std::string tokCallee = SignCallRelayToken(sid.str(), net::call_role_callee, kCrlCalleeUid, kCrlCallerUid,
                                                     expireMs, tokenKey);

    std::ostringstream csv;
    csv << "second,a_grade,a_rtt,a_loss,a_jitter,a_stall,b_grade,b_rtt,b_loss,b_jitter,b_stall,a_buffer,b_buffer\n";
    //后半段每个档位出现的次数（两端合计），用来取稳定后的档位
    int histogram[kCalGradeCount] = { 0 };
    CallNetSnapshot sa;
    CallNetSnapshot sb;
    {
        CallMediaSession a;
        CallMediaSession b;
        a.Start(ip, port, tokCaller, &pubKey[0], net::call_role_caller, sid.str(), NULL, 0, false);
        b.Start(ip, port, tokCallee, &pubKey[0], net::call_role_callee, sid.str(), NULL, 0, false);
        CalPumpArgs pa;
        pa.m_session = &a;
        pa.m_stop = false;
        CalPumpArgs pb;
        pb.m_session = &b;
        pb.m_stop = false;
        //系统计时器精度调到 1 毫秒：默认约 15.6 毫秒时 Sleep 忽长忽短，喂声音的节奏本身就会带来抖动
        ::timeBeginPeriod(kCalTimerResolutionMs);
        std::thread ta(&CalAudioPump, &pa);
        std::thread tb(&CalAudioPump, &pb);
        for (int s = 1; s <= seconds; ++s)
        {
            ::Sleep(kCalSampleMs);
            sa = a.GetNetSnapshot();
            sb = b.GetNetSnapshot();
            csv << s << ',' << sa.m_myGrade << ',' << (sa.m_hasRtt ? static_cast<int>(sa.m_rttMs) : -1) << ','
                << sa.m_mineLossPermille << ',' << sa.m_mineJitterMs << ',' << sa.m_mineStallPermille << ','
                << sb.m_myGrade << ',' << (sb.m_hasRtt ? static_cast<int>(sb.m_rttMs) : -1) << ','
                << sb.m_mineLossPermille << ',' << sb.m_mineJitterMs << ',' << sb.m_mineStallPermille << ','
                << sa.m_mineBufferMs << ',' << sb.m_mineBufferMs << '\n';
            if (s > seconds / 2)
            {
                if (sa.m_myGrade >= 0 && sa.m_myGrade < kCalGradeCount)
                    ++histogram[sa.m_myGrade];
                if (sb.m_myGrade >= 0 && sb.m_myGrade < kCalGradeCount)
                    ++histogram[sb.m_myGrade];
            }
        }
        pa.m_stop = true;
        pb.m_stop = true;
        ta.join();
        tb.join();
        ::timeEndPeriod(kCalTimerResolutionMs);
        CHECK(a.Path() == call_media_path_relay);
        CHECK(b.Path() == call_media_path_relay);
        a.Stop();
        b.Stop();
    }
    CIULog::Uninit();
    ::WSACleanup();

    int mode = -1;
    int modeCount = 0;
    for (int g = 0; g < kCalGradeCount; ++g)
    {
        if (histogram[g] > modeCount)
        {
            mode = g;
            modeCount = histogram[g];
        }
    }
    WARN("relay=" << relay << " stable grade=" << mode << "\n" << csv.str());
    if (!csvPath.empty())
    {
        std::ofstream out(csvPath.c_str(), std::ios::binary);
        out << csv.str();
    }
    CHECK(sa.m_peerReports > 0);
    CHECK(sb.m_peerReports > 0);
    int lo = 0;
    int hi = 0;
    if (CalParseRange(CrlEnv(kCalEnvExpect), lo, hi))
    {
        CHECK(mode >= lo);
        CHECK(mode <= hi);
    }
}
