/**
 *  语音消息内容的辅助函数，说明见 VoiceContent.h。
 *  balloonwj@qq.com   2026-10-04
 */
#include "VoiceContent.h"

#include <stdio.h>    //snprintf
#include <string.h>   //strlen
#include <sodium.h>

namespace net {

namespace {

//解码后允许的最大字节数，与服务端的 VOICE_WAVE_MAX_BYTES 相同
const size_t kVoiceWaveMaxBytes = 64;
//每秒的毫秒数
const int64_t kMillisPerSecond = 1000;
//每分钟的秒数
const int kSecondsPerMinute = 60;
//时长文字的缓冲区字节数：最长如 "999′59″"（撇号各占 3 字节），32 字节绰绰有余
const int kDurationTextBytes = 32;
//秒的记号：双撇号 U+2033，UTF-8 为 E2 80 B3
const char* const kSecondMark = "\xE2\x80\xB3";
//分的记号：单撇号 U+2032，UTF-8 为 E2 80 B2
const char* const kMinuteMark = "\xE2\x80\xB2";

} // namespace

std::string EncodeVoiceWave(const std::vector<uint8_t>& wave)
{
    if (wave.empty())
        return std::string();
    const size_t bufLen = sodium_base64_ENCODED_LEN(wave.size(), sodium_base64_VARIANT_ORIGINAL);
    std::string buf(bufLen, '\0');
    sodium_bin2base64(&buf[0], bufLen, &wave[0], wave.size(), sodium_base64_VARIANT_ORIGINAL);
    //sodium_bin2base64 写的是以 '\0' 结尾的字符串，去掉结尾的 '\0'
    buf.resize(strlen(buf.c_str()));
    return buf;
}

bool DecodeVoiceWave(const std::string& text, std::vector<uint8_t>& out)
{
    out.clear();
    if (text.empty())
        return false;
    unsigned char decoded[kVoiceWaveMaxBytes];
    size_t decodedLen = 0;
    //非法字符、填充不对或解码结果放不进缓冲区时返回 -1
    if (sodium_base642bin(decoded, sizeof(decoded), text.c_str(), text.size(), NULL, &decodedLen, NULL,
                          sodium_base64_VARIANT_ORIGINAL) != 0 || decodedLen == 0)
        return false;
    out.assign(decoded, decoded + decodedLen);
    return true;
}

std::vector<uint8_t> VoiceWaveForDisplay(const std::string& text)
{
    std::vector<uint8_t> wave;
    if (DecodeVoiceWave(text, wave))
        return wave;
    return std::vector<uint8_t>(static_cast<size_t>(kVoiceWaveBars), kVoiceUniformWaveLevel);
}

std::string VoiceDurationText(int seconds)
{
    if (seconds < 1)
        seconds = 1;
    char buf[kDurationTextBytes] = { 0 };
    if (seconds < kSecondsPerMinute)
        snprintf(buf, sizeof(buf), "%d%s", seconds, kSecondMark);
    else
        snprintf(buf, sizeof(buf), "%d%s%02d%s", seconds / kSecondsPerMinute, kMinuteMark,
                 seconds % kSecondsPerMinute, kSecondMark);
    return std::string(buf);
}

int VoiceDurationSeconds(int64_t durationMs)
{
    if (durationMs <= 0)
        return 1;
    const int64_t seconds = (durationMs + kMillisPerSecond - 1) / kMillisPerSecond;
    if (seconds > kVoiceMaxDurationSeconds)
        return kVoiceMaxDurationSeconds;
    return static_cast<int>(seconds);
}

} // namespace net
