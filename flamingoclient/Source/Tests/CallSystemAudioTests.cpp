/**
 *  通话中共享电脑声音的单元测试（标签 [callsysaudio]，docs/音视频通话方案.md 第 10 节），CallSystemAudioTests.cpp
 *  覆盖：
 *    - CSA1 中转封装：电脑声音编成 [4][whole][ts][opus]，分流结果为 call_demux_system_audio，不打乱视频分片的重组；
 *    - CSA2 P2P 封装：电脑声音消息（类型 5）组包后解析出类型、序号、时间戳与负载；
 *    - CSA3 质量报告的标志字节（偏移 22）：「能接收电脑声音」「正在共享电脑声音」两位原样解回；全 0 的报告解出两位都没有；
 *    - CSA4 接收端混音：说话声第 0 路、电脑声音第 1 路各自排队；电脑声音中途停了，说话声照常播放、不计卡顿；
 *    - CSA5 发送部分：长度不定的采样攒成 20 毫秒一帧，1 秒得到 50 个包，时间戳 0、20、……、980；停止后不再发出，重新开始
 *           时时间戳从 0 起；打开前的检查（没接通、对方没报告或是旧版本时不能打开）；
 *    - CSA6 回声参考：播放混音与电脑声音逐采样相加并限幅，哪一路不够按 0 计；电脑声音积压超过上限时丢掉最旧的部分；
 *    - CSA7 闪避：排除本进程树时不闪避；退回普通回环且对方电平达到门限时压到 10%，编出来的声音明显变小；
 *    - CSA8 操作栏：接通态语音、视频都有「共享电脑声音」，紧跟共享屏幕、在挂断之前，正在共享时为激活态，呼叫中与来电态
 *           没有；按钮外观（显示器加音符的图标、激活态深灰底）与提示文字；打不开时与对方开关时的提示文字；
 *    - CSA10 互斥：同一时刻只有一方登记共享电脑声音，后登记的一方让先登记的一方停下；被抢占的一方再 Release 不影响新登记。
 *  CSA9（通话窗里按钮的位置与外观）在 CallDlgIconBarTests.cpp 的 CDB1、CDB2（隐藏标签 [calldlg]，需要建窗口）。
 *  框架 Catch2 v3。纯逻辑，不触及 socket 与声音设备。
 *
 *  典型用法：FlamingoUnit.exe "[callsysaudio]"
 *
 *  balloonwj@qq.com   2026-10-06
 */
#include "stdafx.h"
#include "catch_amalgamated.hpp"

#include <stdint.h>
#include <vector>

#include "../../FlamingoMedia/Call/CallMediaFraming.h"
#include "../../FlamingoMedia/Call/CallP2PFraming.h"
#include "../../FlamingoMedia/Call/CallNetStats.h"
#include "../../FlamingoMedia/Call/AudioStreamMixer.h"
#include "../../FlamingoMedia/Call/OpusAudioCodec.h"
#include "../../FlamingoMedia/Call/CallSystemAudioSender.h"
#include "../../FlamingoMedia/Call/CallEchoReference.h"
#include "../../FlamingoMedia/Call/CallActionBarModel.h"
#include "../../FlamingoMedia/Call/SystemAudioExclusive.h"
#include <objidl.h>    //gdiplus.h 依赖 IStream 等 COM 声明（ChatIcons.h 的函数声明用到 GDI+ 类型）
#include <gdiplus.h>
#include "../../FlamingoMedia/Call/CallButtonArt.h"
#include "../UI/BalloonChatWnd/ChatIcons.h"
#include <string>

#include <math.h>

using namespace call;

namespace {

//中转 body 的头部字节数（[kind][chunk][ts(8)]，与 CallMediaFraming.cpp 一致）
const size_t kCsaRelayHeader = 10;
//一个电脑声音 Opus 包的长度（字节，任取；64 kbps、20 毫秒一帧约 160 字节）
const size_t kCsaPacketBytes = 160;
//电脑声音包的时间戳：这一路自己的第 3 帧（毫秒）
const uint64_t kCsaTsMs = 60;
//CSA1：夹在中间的视频大帧长度与分片大小（字节），切成两片
const size_t kCsaVideoBytes = 3000;
const size_t kCsaChunkBytes = 2000;
const uint64_t kCsaVideoTsMs = 1234;
//CSA2：P2P 消息的序号与时间戳（每个字节都不同，便于核对字节序）
const uint32_t kCsaSeq = 0x01020304u;
const uint64_t kCsaP2PTs = 0x0A0B0C0D0E0F1011ULL;
//CSA3：质量报告标志字节的偏移
const size_t kCsaReportOffFlags = 22;

//CSA4~CSA7：采样率与每帧采样数（48 kHz、20 毫秒）
const int kCsaRate = 48000;
const int kCsaFrame = 960;
const double kCsaPi = 3.14159265358979323846;
//CSA4：说话声与电脑声音各用一个频率的正弦（Hz）与幅度，便于在混音结果里分别测出两路
const double kCsaVoiceHz = 440.0;
const double kCsaSystemHz = 1320.0;
const double kCsaToneAmp = 8000.0;
//CSA4：说话声的编码码率（bps，与通话的语音码率同一量级）
const int kCsaVoiceBitrate = 32000;
//CSA4：总帧数、电脑声音只发前多少帧，以及两段测量区间（跳过编解码器起始的过渡段与停发后的丢包补偿）
const int kCsaMixFrames = 40;
const int kCsaSystemFrames = 15;
const int kCsaBothFrom = 5;
const int kCsaBothTo = 14;
const int kCsaVoiceOnlyFrom = 22;
//CSA4：某个频率「有声音」与「没有声音」的能量比门限（相对说话声那个频率的能量）
const double kCsaPresentRatio = 0.25;
const double kCsaAbsentRatio = 0.02;
//CSA5：一次喂入的采样数（故意不是帧长的整数倍）、喂满 1 秒应得的包数
const int kCsaFeedChunk = 441;
const int kCsaOneSecondPackets = 50;
//CSA5 / CSA7：编码前正弦的幅度
const double kCsaSenderAmp = 10000.0;
//CSA6：两路参考的取值、限幅场景的取值与积压上限场景一次押入的采样数
const int16_t kCsaFarValue = 1000;
const int16_t kCsaSysValue = 500;
const int16_t kCsaLoudFar = 30000;
const int16_t kCsaLoudSys = 10000;
const size_t kCsaOverflowSamples = 15000;
//CSA7：闪避后的声音相对原样的电平比例上限（增益 10%，编解码后留些余量）
const double kCsaDuckedRatioMax = 0.2;
//CSA7：用于判定电平的帧区间（跳过解码器起始的过渡段）
const int kCsaLevelFrom = 5;
//CSA5 / CSA7：原样的增益（百分比）
const int kCsaGainFull = 100;
//CSA5：解码后电平相对编码前正弦幅度的下限比例（音乐模式编解码有少量损耗）
const double kCsaDecodedMinRatio = 0.3;
//CSA6：16 位采样的上下限（限幅后的值）
const int16_t kCsaSampleMax = 32767;
const int16_t kCsaSampleMin = -32768;

//造一段确定性字节序列（值随下标变化，便于逐字节比对）
std::vector<uint8_t> CsaBytes(size_t n, uint8_t seed)
{
    std::vector<uint8_t> v(n);
    for (size_t i = 0; i < n; ++i)
        v[i] = static_cast<uint8_t>((i * 7 + seed) & 0xFF);
    return v;
}

//一段正弦（从第 offset 个采样起算相位），幅度 amp
std::vector<int16_t> CsaTone(int samples, double hz, double amp, int offset)
{
    std::vector<int16_t> v(static_cast<size_t>(samples));
    for (int i = 0; i < samples; ++i)
        v[i] = static_cast<int16_t>(amp * sin(2.0 * kCsaPi * hz * (i + offset) / kCsaRate));
    return v;
}

//一段采样的均方根
double CsaRms(const int16_t* p, int n)
{
    double sum = 0;
    for (int i = 0; i < n; ++i)
        sum += static_cast<double>(p[i]) * p[i];
    return (n > 0) ? sqrt(sum / n) : 0;
}

//Goertzel：一段采样在某个频率上的能量（相对值）
double CsaToneEnergy(const int16_t* p, int n, double hz)
{
    const double k = 2.0 * cos(2.0 * kCsaPi * hz / kCsaRate);
    double s1 = 0;
    double s2 = 0;
    for (int i = 0; i < n; ++i)
    {
        const double s0 = p[i] + k * s1 - s2;
        s2 = s1;
        s1 = s0;
    }
    return s1 * s1 + s2 * s2 - k * s1 * s2;
}

//把一段正弦按帧编码成 Opus 包
std::vector<std::vector<unsigned char> > CsaEncodeTone(int frames, double hz, int bitrate, bool music)
{
    OpusAudioCodec enc;
    std::vector<std::vector<unsigned char> > packets;
    if (!enc.Init(kCsaRate, kCsaFrame, bitrate, music))
        return packets;
    for (int f = 0; f < frames; ++f)
    {
        const std::vector<int16_t> pcm = CsaTone(kCsaFrame, hz, kCsaToneAmp, f * kCsaFrame);
        std::vector<unsigned char> packet;
        if (!enc.Encode(&pcm[0], kCsaFrame, packet))
            break;
        packets.push_back(packet);
    }
    return packets;
}

//记下发送部分交来的每个包
class CsaPacketRecorder : public CallSystemAudioPacketSink
{
public:
    virtual void OnSystemAudioPacket(const uint8_t* data, size_t len, uint64_t tsMs) override
    {
        m_packets.push_back(std::vector<uint8_t>(data, data + len));
        m_ts.push_back(tsMs);
    }

    std::vector<std::vector<uint8_t> > m_packets;   //收到的包（按顺序）
    std::vector<uint64_t>              m_ts;        //对应的时间戳（毫秒）
};

//把 samples 个采样的正弦按 kCsaFeedChunk 一段喂给发送部分
void CsaFeedTone(CallSystemAudioSender& sender, int samples, int gainPercent)
{
    const std::vector<int16_t> pcm = CsaTone(samples, kCsaVoiceHz, kCsaSenderAmp, 0);
    int off = 0;
    while (off < samples)
    {
        const int n = (samples - off < kCsaFeedChunk) ? (samples - off) : kCsaFeedChunk;
        sender.Feed(&pcm[off], n, gainPercent);
        off += n;
    }
}

//把包解码后从第 fromFrame 帧起算均方根
double CsaDecodedRms(const std::vector<std::vector<uint8_t> >& packets, int fromFrame)
{
    OpusAudioCodec dec;
    if (!dec.Init(kCsaRate, kCsaFrame, 0))
        return 0;
    std::vector<int16_t> all;
    for (size_t i = 0; i < packets.size(); ++i)
    {
        std::vector<int16_t> pcm;
        if (dec.Decode(&packets[i][0], packets[i].size(), pcm) && static_cast<int>(i) >= fromFrame)
            all.insert(all.end(), pcm.begin(), pcm.end());
    }
    return all.empty() ? 0 : CsaRms(&all[0], static_cast<int>(all.size()));
}

} // namespace

TEST_CASE("CSA1 中转封装：电脑声音编成 [4][whole][ts][opus]，分流出 system_audio，不打乱视频重组", "[callsysaudio]")
{
    const std::vector<uint8_t> opus = CsaBytes(kCsaPacketBytes, 3);
    std::vector<uint8_t> body;
    BuildSystemAudioBody(opus.data(), opus.size(), kCsaTsMs, body);
    REQUIRE(body.size() == kCsaRelayHeader + opus.size());
    CHECK(body[0] == static_cast<uint8_t>(call_media_kind_system_audio));
    CHECK(body[1] == static_cast<uint8_t>(call_media_chunk_whole));

    CallMediaDemux demux;
    std::vector<uint8_t> out;
    uint64_t ts = 0;
    CHECK(demux.Feed(body.data(), body.size(), out, ts) == CallMediaDemux::call_demux_system_audio);
    CHECK(out == opus);
    CHECK(ts == kCsaTsMs);

    //夹在视频大帧的两片之间：视频照常重组成整帧
    const std::vector<uint8_t> video = CsaBytes(kCsaVideoBytes, 9);
    std::vector<std::vector<uint8_t> > vbodies;
    BuildVideoBodies(video.data(), video.size(), kCsaChunkBytes, kCsaVideoTsMs, vbodies);
    REQUIRE(vbodies.size() == 2);
    CHECK(demux.Feed(vbodies[0].data(), vbodies[0].size(), out, ts) == CallMediaDemux::call_demux_none);
    CHECK(demux.Feed(body.data(), body.size(), out, ts) == CallMediaDemux::call_demux_system_audio);
    CHECK(out == opus);
    CHECK(demux.Feed(vbodies[1].data(), vbodies[1].size(), out, ts) == CallMediaDemux::call_demux_video);
    CHECK(out == video);
    CHECK(ts == kCsaVideoTsMs);
}

TEST_CASE("CSA2 P2P 封装：电脑声音消息组包后解析出类型、序号、时间戳与负载", "[callsysaudio]")
{
    const std::vector<uint8_t> opus = CsaBytes(kCsaPacketBytes, 5);
    std::vector<uint8_t> msg;
    BuildCallP2PMessage(call_p2p_kind_system_audio, 0, kCsaSeq, kCsaP2PTs, opus.data(), opus.size(), msg);
    CallP2PMessage m;
    REQUIRE(ParseCallP2PMessage(msg.data(), msg.size(), m));
    CHECK(m.m_kind == call_p2p_kind_system_audio);
    CHECK(m.m_seq == kCsaSeq);
    CHECK(m.m_tsMs == kCsaP2PTs);
    REQUIRE(m.m_payloadLen == opus.size());
    CHECK(std::vector<uint8_t>(m.m_payload, m.m_payload + m.m_payloadLen) == opus);
}

TEST_CASE("CSA3 质量报告的标志字节：能接收、正在共享电脑声音两位原样解回，全 0 时两位都没有", "[callsysaudio]")
{
    CallQualityReport r;
    //新建的报告不带任何标志，由会话按自己的能力与状态填写
    CHECK(r.m_flags == 0);
    r.m_flags = kCallReportFlagRecvSystemAudio | kCallReportFlagSharingSystemAudio;
    std::vector<uint8_t> bytes;
    BuildCallQualityReport(r, bytes);
    REQUIRE(bytes.size() == kCallReportBytes);
    CHECK(bytes[kCsaReportOffFlags] == (kCallReportFlagRecvSystemAudio | kCallReportFlagSharingSystemAudio));
    //最后 1 字节仍是保留字段
    CHECK(bytes[kCsaReportOffFlags + 1] == 0);

    CallQualityReport back;
    REQUIRE(ParseCallQualityReport(bytes.data(), bytes.size(), back));
    CHECK((back.m_flags & kCallReportFlagRecvSystemAudio) != 0);
    CHECK((back.m_flags & kCallReportFlagSharingSystemAudio) != 0);

    //只能接收、没在共享
    r.m_flags = kCallReportFlagRecvSystemAudio;
    BuildCallQualityReport(r, bytes);
    REQUIRE(ParseCallQualityReport(bytes.data(), bytes.size(), back));
    CHECK(back.m_flags == kCallReportFlagRecvSystemAudio);

    //旧版本发来的报告这个字节是 0：两位都没有
    bytes[kCsaReportOffFlags] = 0;
    REQUIRE(ParseCallQualityReport(bytes.data(), bytes.size(), back));
    CHECK((back.m_flags & kCallReportFlagRecvSystemAudio) == 0);
    CHECK((back.m_flags & kCallReportFlagSharingSystemAudio) == 0);
}

TEST_CASE("CSA4 接收端混音：说话声与电脑声音各一路，电脑声音中途停了说话声照常播放", "[callsysaudio][audiomix]")
{
    const int kVoiceStream = 0;
    const int kSystemStream = 1;
    const std::vector<std::vector<unsigned char> > voice = CsaEncodeTone(kCsaMixFrames, kCsaVoiceHz, kCsaVoiceBitrate,
                                                                         false);
    const std::vector<std::vector<unsigned char> > sys = CsaEncodeTone(kCsaSystemFrames, kCsaSystemHz,
                                                                       CallSystemAudioSender::kBitrateBps, true);
    REQUIRE(voice.size() == static_cast<size_t>(kCsaMixFrames));
    REQUIRE(sys.size() == static_cast<size_t>(kCsaSystemFrames));

    AudioStreamMixer mixer(kCsaRate, kCsaFrame);
    std::vector<int16_t> out(static_cast<size_t>(kCsaMixFrames) * kCsaFrame);
    for (int f = 0; f < kCsaMixFrames; ++f)
    {
        //两路的时间戳各自从 0 起，互不相干
        REQUIRE(mixer.PushEncoded(kVoiceStream, &voice[f][0], voice[f].size(), static_cast<uint32_t>(f * kCsaFrame)));
        if (f < kCsaSystemFrames)
            REQUIRE(mixer.PushEncoded(kSystemStream, &sys[f][0], sys[f].size(), static_cast<uint32_t>(f * kCsaFrame)));
        mixer.Mix(&out[static_cast<size_t>(f) * kCsaFrame], kCsaFrame);
    }

    //两路都在时，两个频率都听得到
    const int bothOff = kCsaBothFrom * kCsaFrame;
    const int bothLen = (kCsaBothTo - kCsaBothFrom) * kCsaFrame;
    const double bothVoice = CsaToneEnergy(&out[bothOff], bothLen, kCsaVoiceHz);
    const double bothSys = CsaToneEnergy(&out[bothOff], bothLen, kCsaSystemHz);
    CHECK(bothSys > bothVoice * kCsaPresentRatio);

    //电脑声音停了之后：说话声还在，电脑声音那个频率没有了
    const int onlyOff = kCsaVoiceOnlyFrom * kCsaFrame;
    const int onlyLen = (kCsaMixFrames - kCsaVoiceOnlyFrom) * kCsaFrame;
    const double onlyVoice = CsaToneEnergy(&out[onlyOff], onlyLen, kCsaVoiceHz);
    const double onlySys = CsaToneEnergy(&out[onlyOff], onlyLen, kCsaSystemHz);
    CHECK(onlyVoice > 0);
    CHECK(onlySys < onlyVoice * kCsaAbsentRatio);

    //说话声那一路的卡顿计数不受电脑声音停发的影响
    uint64_t requested = 0;
    uint64_t concealed = 0;
    REQUIRE(mixer.GetPlayoutCounters(kVoiceStream, requested, concealed));
    CHECK(concealed == 0);
    //电脑声音那一路停发后由抖动缓冲报缺失
    AudioStreamStats st;
    REQUIRE(mixer.GetStreamStats(kSystemStream, st));
    CHECK(st.m_jb.m_packets == static_cast<uint64_t>(kCsaSystemFrames));
    CHECK(st.m_jb.m_missing > 0);
}

TEST_CASE("CSA5 发送部分：攒成 20 毫秒一帧、1 秒 50 个包、时间戳连续；停止后不发，重新开始从 0 起；打开前的检查", "[callsysaudio]")
{
    CallSystemAudioSender sender;
    CsaPacketRecorder rec;
    CHECK_FALSE(sender.IsRunning());
    //没开始时喂的声音直接丢弃
    CsaFeedTone(sender, kCsaRate, kCsaGainFull);
    CHECK(rec.m_packets.empty());
    CHECK_FALSE(sender.Start(NULL));

    REQUIRE(sender.Start(&rec));
    CHECK(sender.IsRunning());
    CsaFeedTone(sender, kCsaRate, kCsaGainFull);
    REQUIRE(rec.m_packets.size() == static_cast<size_t>(kCsaOneSecondPackets));
    for (int i = 0; i < kCsaOneSecondPackets; ++i)
        CHECK(rec.m_ts[i] == static_cast<uint64_t>(i) * CallSystemAudioSender::kFrameMs);
    CHECK(sender.FramesSent() == static_cast<uint64_t>(kCsaOneSecondPackets));
    //编出来的是真实的声音
    CHECK(CsaDecodedRms(rec.m_packets, kCsaLevelFrom) > kCsaSenderAmp * kCsaDecodedMinRatio);

    //停止之后不再发出
    sender.Stop();
    CHECK_FALSE(sender.IsRunning());
    CsaFeedTone(sender, kCsaRate, kCsaGainFull);
    CHECK(rec.m_packets.size() == static_cast<size_t>(kCsaOneSecondPackets));

    //重新开始：时间戳从 0 起
    CsaPacketRecorder rec2;
    REQUIRE(sender.Start(&rec2));
    CsaFeedTone(sender, kCsaFrame, kCsaGainFull);
    REQUIRE(rec2.m_ts.size() == 1);
    CHECK(rec2.m_ts[0] == 0);
    sender.Stop();

    //打开前的检查
    CHECK(CallSystemAudioPrecheck(false, 3, kCallReportFlagRecvSystemAudio) == call_sysaudio_not_connected);
    CHECK(CallSystemAudioPrecheck(true, 0, 0) == call_sysaudio_peer_unsupported);
    CHECK(CallSystemAudioPrecheck(true, 3, 0) == call_sysaudio_peer_unsupported);
    CHECK(CallSystemAudioPrecheck(true, 3, kCallReportFlagSharingSystemAudio) == call_sysaudio_peer_unsupported);
    CHECK(CallSystemAudioPrecheck(true, 3, kCallReportFlagRecvSystemAudio) == call_sysaudio_ok);
}

TEST_CASE("CSA6 回声参考：两路逐采样相加并限幅，不够按 0 计；电脑声音积压超限时丢掉最旧的部分", "[callsysaudio]")
{
    const size_t kFarCapacity = static_cast<size_t>(kCsaRate);
    CallEchoReference ref(kFarCapacity);
    std::vector<int16_t> farPcm(static_cast<size_t>(kCsaFrame), kCsaFarValue);
    std::vector<int16_t> sys(static_cast<size_t>(kCsaFrame), kCsaSysValue);
    std::vector<int16_t> out(static_cast<size_t>(kCsaFrame));

    //两路都有：相加
    ref.PushFar(&farPcm[0], farPcm.size());
    ref.PushSystem(&sys[0], sys.size());
    ref.Pop(&out[0], out.size());
    CHECK(out.front() == kCsaFarValue + kCsaSysValue);
    CHECK(out.back() == kCsaFarValue + kCsaSysValue);

    //只有电脑声音：参考就是电脑声音；什么都没有：全 0
    ref.PushSystem(&sys[0], sys.size());
    ref.Pop(&out[0], out.size());
    CHECK(out[0] == kCsaSysValue);
    ref.Pop(&out[0], out.size());
    CHECK(out[0] == 0);
    CHECK(out[kCsaFrame - 1] == 0);

    //只有播放混音：与原先相同
    ref.PushFar(&farPcm[0], farPcm.size());
    ref.Pop(&out[0], out.size());
    CHECK(out[0] == kCsaFarValue);

    //限幅：正负两个方向
    std::vector<int16_t> loudFar(static_cast<size_t>(kCsaFrame), kCsaLoudFar);
    std::vector<int16_t> loudSys(static_cast<size_t>(kCsaFrame), kCsaLoudSys);
    ref.PushFar(&loudFar[0], loudFar.size());
    ref.PushSystem(&loudSys[0], loudSys.size());
    ref.Pop(&out[0], out.size());
    CHECK(out[0] == kCsaSampleMax);
    std::vector<int16_t> negFar(static_cast<size_t>(kCsaFrame), static_cast<int16_t>(-kCsaLoudFar));
    std::vector<int16_t> negSys(static_cast<size_t>(kCsaFrame), static_cast<int16_t>(-kCsaLoudSys));
    ref.PushFar(&negFar[0], negFar.size());
    ref.PushSystem(&negSys[0], negSys.size());
    ref.Pop(&out[0], out.size());
    CHECK(out[0] == kCsaSampleMin);

    //积压超过上限：只留最新的 kMaxSystemSamples 个，取出来的第一个是倒数第 kMaxSystemSamples 个
    std::vector<int16_t> ramp(kCsaOverflowSamples);
    for (size_t i = 0; i < ramp.size(); ++i)
        ramp[i] = static_cast<int16_t>(i);
    ref.PushSystem(&ramp[0], ramp.size());
    CHECK(ref.SystemAvailable() == CallEchoReference::kMaxSystemSamples);
    ref.Pop(&out[0], out.size());
    CHECK(out[0] == static_cast<int16_t>(kCsaOverflowSamples - CallEchoReference::kMaxSystemSamples));

    //停止共享：电脑声音那一路清空，参考里只剩播放混音
    ref.ClearSystem();
    CHECK(ref.SystemAvailable() == 0);
    ref.PushFar(&farPcm[0], farPcm.size());
    ref.Pop(&out[0], out.size());
    CHECK(out[0] == kCsaFarValue);
}

TEST_CASE("CSA7 闪避：排除本进程树时不闪避；退回普通回环且对方在说话时压到 10%", "[callsysaudio]")
{
    const int kFull = kCsaGainFull;
    const int kDucked = 10;
    CHECK(CallSystemAudioGainPercent(true, 0) == kFull);
    CHECK(CallSystemAudioGainPercent(true, kCallPeerSpeakingLevel * 10) == kFull);
    CHECK(CallSystemAudioGainPercent(false, 0) == kFull);
    CHECK(CallSystemAudioGainPercent(false, kCallPeerSpeakingLevel - 1) == kFull);
    CHECK(CallSystemAudioGainPercent(false, kCallPeerSpeakingLevel) == kDucked);

    //同一段声音按 10% 编码，解出来明显比原样小
    CallSystemAudioSender sender;
    CsaPacketRecorder full;
    REQUIRE(sender.Start(&full));
    CsaFeedTone(sender, kCsaRate, kFull);
    sender.Stop();
    CsaPacketRecorder ducked;
    REQUIRE(sender.Start(&ducked));
    CsaFeedTone(sender, kCsaRate, kDucked);
    sender.Stop();
    const double fullRms = CsaDecodedRms(full.m_packets, kCsaLevelFrom);
    const double duckedRms = CsaDecodedRms(ducked.m_packets, kCsaLevelFrom);
    REQUIRE(fullRms > 0);
    CHECK(duckedRms < fullRms * kCsaDuckedRatioMax);
}

TEST_CASE("CSA8 操作栏：接通态都有「共享电脑声音」，紧跟共享屏幕、在挂断之前；外观与提示文字", "[callsysaudio][callbar]")
{
    for (int v = 0; v < 2; ++v)
    {
        INFO("video=" << v);
        const std::vector<CallActionItem> idle = BuildActionBar(kBarConnected, v != 0, true, false, false, false);
        int share = -1;
        int sys = -1;
        int hangup = -1;
        for (size_t i = 0; i < idle.size(); ++i)
        {
            if (idle[i].m_id == kActionShare)
                share = static_cast<int>(i);
            else if (idle[i].m_id == kActionSystemAudio)
                sys = static_cast<int>(i);
            else if (idle[i].m_id == kActionHangup)
                hangup = static_cast<int>(i);
        }
        REQUIRE(sys >= 0);
        CHECK(sys == share + 1);
        CHECK(hangup == sys + 1);
        CHECK_FALSE(idle[sys].m_active);
        CHECK_FALSE(idle[sys].m_danger);
        //正在共享：按钮集合不变，只有这一项变为激活态
        const std::vector<CallActionItem> on = BuildActionBar(kBarConnected, v != 0, true, false, false, true);
        REQUIRE(on.size() == idle.size());
        CHECK(on[sys].m_id == kActionSystemAudio);
        CHECK(on[sys].m_active);
        CHECK_FALSE(on[share].m_active);
    }
    //呼叫中、来电态没有
    const std::vector<CallActionItem> calling = BuildActionBar(kBarCalling, true, true, false, false, true);
    const std::vector<CallActionItem> incoming = BuildActionBar(kBarIncoming, true, true, false, false, true);
    for (size_t i = 0; i < calling.size(); ++i)
        CHECK(calling[i].m_id != kActionSystemAudio);
    for (size_t i = 0; i < incoming.size(); ++i)
        CHECK(incoming[i].m_id != kActionSystemAudio);

    //外观：显示器加音符；常态浅灰底、激活态深灰底；与其它普通按钮一样大
    const CallButtonLook idleLook = LookForAction(kActionSystemAudio, false, true);
    const CallButtonLook onLook = LookForAction(kActionSystemAudio, true, true);
    CHECK(idleLook.m_icon == balloonchatwnd::kChatIconSystemAudio);
    CHECK(onLook.m_icon == balloonchatwnd::kChatIconSystemAudio);
    CHECK(idleLook.m_tone == call_tone_normal);
    CHECK(onLook.m_tone == call_tone_active);
    CHECK_FALSE(idleLook.m_slash);
    CHECK(idleLook.m_diameter == kCallRoundBtnPx);
    //提示文字：两态不同、都不为空；打开时的提示里建议戴耳机
    const std::wstring idleTip = TooltipForAction(kActionSystemAudio, false, true);
    const std::wstring onTip = TooltipForAction(kActionSystemAudio, true, true);
    CHECK_FALSE(idleTip.empty());
    CHECK_FALSE(onTip.empty());
    CHECK(idleTip != onTip);
    CHECK(idleTip.find(L"耳机") != std::wstring::npos);

    //打不开时的提示：成功不提示，三种失败各有一句且互不相同
    CHECK(SystemAudioFailureText(call_sysaudio_ok).empty());
    const std::wstring notConnected = SystemAudioFailureText(call_sysaudio_not_connected);
    const std::wstring unsupported = SystemAudioFailureText(call_sysaudio_peer_unsupported);
    const std::wstring captureFailed = SystemAudioFailureText(call_sysaudio_capture_failed);
    CHECK_FALSE(notConnected.empty());
    CHECK_FALSE(unsupported.empty());
    CHECK_FALSE(captureFailed.empty());
    CHECK(notConnected != unsupported);
    CHECK(unsupported != captureFailed);
    //对方开始、停止共享的提示
    CHECK_FALSE(PeerSystemAudioText(true).empty());
    CHECK(PeerSystemAudioText(true) != PeerSystemAudioText(false));
}

namespace {

//CSA10：记下自己被要求停下的次数；被要求停下时按控制器的做法调用 Release
class CsaHolder : public SystemAudioHolder
{
public:
    explicit CsaHolder(SystemAudioExclusive* reg)
        : m_reg(reg)
        , m_preempted(0)
    {
    }

    virtual void OnSystemAudioPreempted() override
    {
        ++m_preempted;
        m_reg->Release(this);
    }

    SystemAudioExclusive* m_reg;   //所用的互斥登记（不拥有）
    int m_preempted;               //被要求停下的次数
};

} // namespace

TEST_CASE("CSA10 互斥：同一时刻只有一方共享电脑声音，后登记的一方让先登记的一方停下", "[callsysaudio]")
{
    //用一个独立的实例，不碰进程内唯一的那一个
    SystemAudioExclusive reg;
    CsaHolder callSide(&reg);
    CsaHolder shareSide(&reg);
    CHECK(reg.Current() == NULL);
    CHECK(reg.Acquire(NULL) == NULL);

    //通话先打开：没有人被要求停下
    CHECK(reg.Acquire(&callSide) == NULL);
    CHECK(reg.Current() == &callSide);
    //同一方再登记一次：不会让自己停下
    CHECK(reg.Acquire(&callSide) == NULL);
    CHECK(callSide.m_preempted == 0);

    //屏幕共享再打开：通话那一方被要求停下，登记改为屏幕共享（被抢占的一方在回调里 Release 不影响新登记）
    CHECK(reg.Acquire(&shareSide) == &callSide);
    CHECK(callSide.m_preempted == 1);
    CHECK(reg.Current() == &shareSide);

    //已被抢占的一方再 Release：不影响当前登记
    reg.Release(&callSide);
    CHECK(reg.Current() == &shareSide);

    //屏幕共享关掉：登记清空；之后通话再打开不会打扰任何人
    reg.Release(&shareSide);
    CHECK(reg.Current() == NULL);
    CHECK(reg.Acquire(&callSide) == NULL);
    CHECK(shareSide.m_preempted == 0);
    reg.Release(&callSide);

    //进程内唯一的实例确实是同一个
    CHECK(&SystemAudioExclusive::Instance() == &SystemAudioExclusive::Instance());
}
