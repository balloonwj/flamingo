/**
 *  屏幕共享的字幕控制帧（docs/语音字幕方案.md 第 9 节、协议 17.8 的控制帧类型 5），ShareCaptionFrame.h
 *
 *  每个发言人在自己电脑上识别自己的话，把结果以控制帧发给房间里的其他人。载荷为
 *  [5][标志 1B][句子编号 4B 大端][时长 2B 大端，毫秒][UTF-8 文字]：标志 0 为中间结果（同一句不断更新）、1 为最终结果；
 *  句子编号由发送者从 1 起递增，同一句的中间结果与最终结果编号相同；时长为这句话从开始到当前的毫秒数（超过 65535 按
 *  65535），接收端录制共享时据此推算这句的开始时刻；文字最多 kShareCaptionMaxTextBytes 字节，超长时在 UTF-8 字符边界截断。
 *  服务端改写发送者座位号后转发，不解析载荷。全部为纯函数，单测见 Source/Tests/ShareCaptionTests.cpp（标签 [sharecaption]）。
 *
 *  典型用法：
 *      screenshare::ShareCaptionText c;  c.m_final = true;  c.m_sentenceId = 3;  c.m_durationMs = 1800;  c.m_utf8 = "...";
 *      transport->SendRoomBody(screenshare::BuildShareCaptionBody(seat, c));
 *      if (screenshare::ParseShareCaptionFrame(frame, c)) ...
 *
 *  balloonwj@qq.com   2026-10-06
 */
#pragma once

#include <stdint.h>

#include <string>

#include "../Share/ShareFraming.h"

namespace screenshare {

//一条字幕帧里文字的最大字节数（UTF-8）
const size_t kShareCaptionMaxTextBytes = 600;
//时长字段的上限（毫秒，2 字节）
const uint32_t kShareCaptionMaxDurationMs = 0xFFFF;

//字幕帧的内容
struct ShareCaptionText
{
    bool        m_final;        //是否为最终结果（false 为中间结果）
    uint32_t    m_sentenceId;   //句子编号（发送者从 1 起递增）
    uint32_t    m_durationMs;   //这句话从开始到当前的毫秒数
    std::string m_utf8;         //文字（UTF-8）

    ShareCaptionText()
        : m_final(false)
        , m_sentenceId(0)
        , m_durationMs(0)
    {
    }
};

/**
 *  字幕帧的接收方：发起人一侧的推流管线、观看者一侧的收流管线在中转接收线程上收到字幕帧时调用，实现须线程安全且不能阻塞。
 */
class ShareCaptionSink
{
public:
    virtual ~ShareCaptionSink()
    {
    }

    /**
     *  收到一帧别人的字幕。
     *  @param senderSeat 帧头的发送者座位号（服务端已改写为登记的座位）
     *  @param caption    内容
     */
    virtual void OnCaptionFrame(int senderSeat, const ShareCaptionText& caption) = 0;
};

/**
 *  把 UTF-8 文字截到不超过 maxBytes 字节，不截断半个字符。
 *  @param text     原文
 *  @param maxBytes 字节上限
 *  @return 截断后的文字
 */
std::string TruncateUtf8(const std::string& text, size_t maxBytes);

/**
 *  构造一帧字幕控制帧的 1860 包体（未加密）：轨道为控制帧、首尾分片位都置上、时间戳 0。
 *  @param senderSeat 本人的座位号（服务端会改写为登记的座位）
 *  @param caption    内容；时长超过上限按上限，文字超长时截断
 *  @return 包体
 */
std::string BuildShareCaptionBody(int senderSeat, const ShareCaptionText& caption);

/**
 *  判断一帧是否为字幕控制帧并取出内容。
 *  @param frame [入参] 重组之后的帧
 *  @param out   [出参] 内容
 *  @return 是字幕控制帧、标志合法、长度足够时返回 true；文字可以为空（表示这一句被清空）
 */
bool ParseShareCaptionFrame(const ShareMediaFrame& frame, ShareCaptionText& out);

} // namespace screenshare
