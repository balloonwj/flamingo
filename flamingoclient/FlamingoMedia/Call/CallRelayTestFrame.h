/**
 *  音视频通话中转「测试帧」编解码，CallRelayTestFrame.h
 *
 *  第一期（信令 + 中转骨架）在中转通道上不发真实音频，只发一种可校验的「测试帧」，
 *  用来验证「令牌配对 + 字节透传」整条链路真的把字节原样送达对端。本模块定义这种
 *  测试帧的格式与编解码：
 *
 *    帧布局（即 cmd_av_relay_data 帧的包体，imgserver 不解析、原样转发）：
 *      [魔数 4 字节 "CTST"][序号 4 字节，大端][载荷 kTestFramePayloadLen 字节]
 *    载荷是由序号唯一确定的定长字节模式（见 .cpp 的 testFramePayloadByte）。接收端
 *    重算同一模式逐字节比对，任何透传过程中的字节损坏 / 错配都能被发现。
 *
 *  本模块为纯逻辑，不依赖网络与 UI，便于单元测试（Source/Tests/CallRelayTestFrameTests.cpp）。
 *  CallMediaSession（真实通话流程的中转 owner）与 CallRelaySelfTest（进程内自测）都复用
 *  这一份，避免各写一套。第二期接入真实 Opus 音频帧后，本测试帧及其使用处一并退场。
 *
 *  balloonwj@qq.com   2026-06-11
 */
#pragma once

#include <stdint.h>
#include <stddef.h>
#include <string>

namespace call {

//测试帧魔数（4 字节，不含结尾 '\0'）：标识这是通话中转测试帧，接收端据此拒绝非测试帧。
extern const char        kTestFrameMagic[];
//魔数字节数。
const size_t             kTestFrameMagicLen   = 4;
//序号字节数（大端 uint32）。
const size_t             kTestFrameSeqLen     = 4;
//载荷字节数：取一个偏小的定长，贴近第二期单个音频帧的量级，又足以暴露字节损坏。
const size_t             kTestFramePayloadLen = 64;
//测试帧总字节数 = 魔数 + 序号 + 载荷。帧为定长，接收端按此长度严格校验。
const size_t             kTestFrameTotalLen   = kTestFrameMagicLen + kTestFrameSeqLen + kTestFramePayloadLen;

/**
 *  构造一帧测试帧。
 *  @param seq 序号；写入帧头并据此生成确定性的载荷字节模式
 *  @return 长度恒为 kTestFrameTotalLen 的字节串（作 cmd_av_relay_data 帧包体发出）
 */
std::string BuildTestFrame(uint32_t seq);

/**
 *  校验并解析一帧测试帧。
 *  严格要求 len == kTestFrameTotalLen、魔数匹配、且载荷与序号对应的字节模式逐字节一致；
 *  任一不满足（含缓冲过短 / 截断）一律返回 false，且不越界读取。
 *  @param data   帧包体起始地址（不得为 NULL，除非 len 为 0）
 *  @param len    帧包体字节数
 *  @param seqOut [出参] 校验通过时回填帧内序号；失败时不修改
 *  @return 校验通过返回 true，否则 false
 */
bool ParseTestFrame(const unsigned char* data, size_t len, uint32_t& seqOut);

} // namespace call
