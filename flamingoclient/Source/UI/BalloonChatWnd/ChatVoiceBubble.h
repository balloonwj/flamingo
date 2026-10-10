/**
 *  ChatVoiceBubble.h
 *
 *  语音气泡的纯逻辑（语音消息第 3d 步，交互见 docs/语音消息方案.md 第 3.2 节）：时长文字、气泡宽度随时长变化、
 *  未听红点的显示条件。不碰绘制，可以直接做单元测试；气泡的绘制在 ChatBubble 里按这里的结果画。
 *
 *  典型用法：
 *      const int width = balloonchatwnd::VoiceBubbleWidth(msg.voiceDuration);
 *      const std::wstring text = balloonchatwnd::FormatVoiceDuration(msg.voiceDuration);   //如 8″、1′05″
 *      if (balloonchatwnd::ShowVoiceUnplayedDot(msg.fromMe, msg.voicePlayed)) { ...画红点... }
 *
 *  balloonwj@qq.com   2026-10-04
 */
#pragma once

#include <stdint.h>
#include <string>
#include <vector>

namespace balloonchatwnd {

struct ChatMessage;

//语音气泡的状态（ChatMessage::voiceState）
enum ChatVoiceState
{
    //就绪：文件在本机，可以播放
    kVoiceStateReady = 0,
    //发送中（本人发的，上传或等回执期间）：气泡左侧画静态圆环，回执成功后回到就绪，失败时画红色感叹号
    kVoiceStateSending = 1,
    //下载中（对方发来的，文件还没到本机）：点击时提示正在下载
    kVoiceStateDownloading = 2,
    //下载失败：点击时重新下载
    kVoiceStateDownloadFailed = 3,
};

//语音气泡请宿主聊天窗做的事（WM_CHAT_VOICE_ACTION 的 wParam）
enum ChatVoiceAction
{
    //点了气泡：没在播放就开始播放，正在播放就停止
    kVoiceActionToggle = 0,
    //点了发送失败的红色感叹号：用原 clientmsgid 重发（2026-10-04 用户确认）
    kVoiceActionResend = 1,
};

/**
 *  WM_CHAT_VOICE_ACTION 的 lParam：堆上分配，宿主处理后 delete。
 */
struct VoiceActionRequest
{
    long long   m_msgid;        //服务器消息 id；还没回执的本人消息为 0
    std::string m_clientMsgId;  //客户端消息 id（本人发的消息据它定位）

    VoiceActionRequest()
        : m_msgid(0)
    {
    }
};

//语音气泡的最小、最大宽度（像素）：1 秒及以下取最小，kVoiceBubbleFullSeconds 秒及以上取最大，中间按秒线性增长。
//最小宽度原为 88，扣掉喇叭图标与时长文字区后一根波形柱也放不下，不到 6 秒的语音没有波形；第 6b 步改为 120，
//1 秒的语音也有 6 根（见 ChatVoicePaint.h 的 VoiceWaveBarCount）
const int kVoiceBubbleMinWidth = 120;
const int kVoiceBubbleMaxWidth = 240;
//气泡宽度达到最大值时的时长（秒）
const int kVoiceBubbleFullSeconds = 60;

/**
 *  语音的时长文字。
 *  @param seconds 时长（整秒）；小于 1 按 1
 *  @return 不到 1 分钟写秒数加「″」，如 L"8″"；1 分钟及以上写「分′秒″」，秒补足两位，如 L"1′05″"、L"2′00″"
 */
std::wstring FormatVoiceDuration(int seconds);

/**
 *  语音气泡的宽度（不含头像与左右边距）。
 *  @param seconds 时长（整秒）
 *  @return kVoiceBubbleMinWidth ~ kVoiceBubbleMaxWidth 之间，随时长单调不减
 */
int VoiceBubbleWidth(int seconds);

/**
 *  是否在语音气泡旁画未听红点（方案 V7：只记在本机）。
 *  @param fromMe 是否是本人发的
 *  @param played 本机是否已经播放过
 *  @return 对方发来、还没听过时返回 true；本人发的一律不画
 */
bool ShowVoiceUnplayedDot(bool fromMe, bool played);

/**
 *  把一条消息填成语音气泡（kind、文字、voice* 字段）。调用前 fromMe、msgid、发送者等字段已由调用方填好。
 *  @param msg             [入参 / 出参] 消息
 *  @param fileId          语音文件 ID（字符串）
 *  @param md5             文件内容 MD5
 *  @param durationSeconds 时长（整秒）；小于 1 按 1
 *  @param sizeBytes       文件字节数
 *  @param waveBase64      content.wave（Base64）；为空或不合法时画均匀波形
 *  @param played          本机是否已听过（对方发来的取本地库 played 列）；本人发的忽略此参数、一律为已听
 */
void ApplyVoiceMessage(ChatMessage& msg, const std::string& fileId, const std::string& md5, int durationSeconds,
                       long long sizeBytes, const std::string& waveBase64, bool played);

/**
 *  构造本人刚录完、正在发送的语音气泡（语音消息第 3c 步）：插进消息列表后由上传结果与回执改状态。
 *  @param clientMsgId 客户端消息ID（开始录音时生成），回执、失败、重发都按它定位
 *  @param durationMs  有效时长（毫秒），按 net::VoiceDurationSeconds 向上取整到秒
 *  @param wave        录音器算出的波形值；个数不是 net::kVoiceWaveBars 时画均匀波形
 *  @param localPath   录音文件路径（ChatVoice\<md5>.ogg），播放与重发都用它
 *  @param md5         文件内容 MD5（小写十六进制）
 *  @param sizeBytes   文件字节数
 *  @return 本人发的、发送中、已听的语音气泡，msgid 为 0
 */
ChatMessage MakeSendingVoiceMessage(const std::string& clientMsgId, int64_t durationMs,
                                    const std::vector<unsigned char>& wave, const std::wstring& localPath,
                                    const std::string& md5, long long sizeBytes);

/**
 *  一条语音能否重发（点红色感叹号或右键「重新发送」时判断，2026-10-04 用户确认用原 clientmsgid 重发）。
 *  @param msg 气泡上的消息
 *  @return 本人发的、发送失败、带 clientMsgId、本机录音文件还在时返回 true
 */
bool CanResendVoice(const ChatMessage& msg);

/**
 *  录音文件已不在本机（被清理缓存删掉）、无法重发时的提示文字。
 */
std::wstring VoiceFileMissingHint();

/**
 *  语音上传或发出失败时，气泡下方的原因文字（语音消息第 6c 步；原先借用文件的失败文字「文件发送失败」）。
 *  @param code 失败错误码（见 enum error_code）；0 表示没有错误码（连不上文件服务器、本地读文件失败等）
 *  @return 不带句号的短句：没有错误码时「语音发送失败」，否则「语音发送失败（错误码 N）」
 */
std::wstring VoiceSendFailureText(int code);

//点语音气泡时要做的事（DecideVoiceClick 的结果，语音消息第 3e 步）
enum ChatVoiceClick
{
    //开始播放本机的语音文件（就绪的、本人发送中或发送失败的）
    kVoiceClickPlay = 0,
    //停止正在播放的这一条
    kVoiceClickStop = 1,
    //文件还在下载：提示「语音正在下载，请稍候」
    kVoiceClickWaitDownload = 2,
    //文件不在本机（上次下载失败等）：重新下载
    kVoiceClickRetryDownload = 3,
};

//播放中喇叭图标每一帧的时长（毫秒）：每过这么久换一帧
const int kVoicePlayFrameMs = 300;
//喇叭图标的帧数：1～3 道声波循环
const int kVoicePlayFrameCount = 3;

/**
 *  点了一条语音气泡要做什么。
 *  @param msg 气泡上的消息
 *  @return 取值见 ChatVoiceClick
 */
int DecideVoiceClick(const ChatMessage& msg);

/**
 *  播放进度对应的喇叭图标帧号。
 *  @param playedMs 已播放的毫秒数
 *  @return 0 ~ kVoicePlayFrameCount-1，每 kVoicePlayFrameMs 毫秒加一、循环
 */
int VoicePlayFrame(int64_t playedMs);

/**
 *  一条语音播完后，挑紧挨着的下一条自动接着播（方案第 3.2 节）。
 *  @param rows    消息列表里按顺序排列的各行（时间分隔行也在其中）
 *  @param current 刚播完的那一行的下标
 *  @return 下一条要播的下标：紧挨着的下一条（跳过时间分隔行）是对方发来、未听、文件已在本机的语音；否则 -1
 */
int PickNextAutoPlayVoice(const std::vector<ChatMessage>& rows, size_t current);

//点了还在下载的语音时的提示文字
std::wstring VoiceDownloadingHint();

//语音文件打不开、扬声器打不开等播放失败时的提示文字
std::wstring VoicePlayFailedHint();

} // namespace balloonchatwnd
