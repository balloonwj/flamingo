/**
 *  语音消息内容的辅助函数（语音消息第 3a 步，方案见 docs/语音消息方案.md 第 4 节 V4、第 5 节）。
 *
 *  消息 content 里的 wave 是 kVoiceWaveBars 个音量值（0~255）的标准 Base64；时长 duration 是整秒（向上取整，
 *  1~kVoiceMaxDurationSeconds）。本文件负责两者与界面所用形式之间的换算，以及消息里没有波形或波形不合法时
 *  使用的均匀波形。只依赖 libsodium，可以直接做单元测试。
 *
 *  典型用法：
 *      const std::string wave = net::EncodeVoiceWave(levels);             //发送前
 *      const int seconds = net::VoiceDurationSeconds(result.m_durationMs);
 *      std::vector<uint8_t> bars = net::VoiceWaveForDisplay(notify.m_voiceWave);   //画气泡
 *
 *  balloonwj@qq.com   2026-10-04
 */
#pragma once

#include <stdint.h>
#include <string>
#include <vector>

namespace net {

//消息里的波形值个数（与 FlamingoMedia 的 kMediaVoiceWaveBars 相同）
const int kVoiceWaveBars = 40;
//语音的最长时长（秒），与服务端的校验上限相同
const int kVoiceMaxDurationSeconds = 120;
//没有波形或波形不合法时，均匀波形里每个值的大小（0~255，约四成高度）
const uint8_t kVoiceUniformWaveLevel = 96;

/**
 *  把波形值编成 content.wave 用的标准 Base64（带填充）。
 *  @param wave 波形值，每个 0~255；为空时返回空串（发送时不写 wave 字段）
 *  @return Base64 文本
 */
std::string EncodeVoiceWave(const std::vector<uint8_t>& wave);

/**
 *  解码 content.wave。
 *  @param text Base64 文本
 *  @param out  [出参] 波形值，返回 true 时有效
 *  @return 是合法的标准 Base64、且解码后有 1~64 个字节时返回 true；空串、非法字符、填充不对或过长返回 false
 */
bool DecodeVoiceWave(const std::string& text, std::vector<uint8_t>& out);

/**
 *  取画气泡用的波形：能解码时返回解码结果，否则（没有 wave 字段、不合法）返回 kVoiceWaveBars 个
 *  kVoiceUniformWaveLevel。
 *  @param text content.wave 的 Base64 文本，可以为空
 *  @return 至少 1 个波形值
 */
std::vector<uint8_t> VoiceWaveForDisplay(const std::string& text);

/**
 *  把录音时长换算成 content.duration：毫秒向上取整到秒，限制在 1~kVoiceMaxDurationSeconds。
 *  @param durationMs 录音时长（毫秒）
 *  @return 整秒
 */
int VoiceDurationSeconds(int64_t durationMs);

/**
 *  语音时长的文字（UTF-8），与气泡上的 balloonchatwnd::FormatVoiceDuration 写法相同（语音消息第 4a 步）：
 *  消息记录窗、合并记录查看窗的预览与导出按 UTF-8 处理文字，用这个版本。
 *  @param seconds 时长（整秒）；小于 1 按 1
 *  @return 不到 1 分钟写秒数加「″」，如 "8″"；1 分钟及以上写「分′秒″」，秒补足两位，如 "1′05″"
 */
std::string VoiceDurationText(int seconds);

} // namespace net
