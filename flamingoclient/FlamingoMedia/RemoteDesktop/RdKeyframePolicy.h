/**
 *  远程桌面被控端何时发关键帧（纯逻辑），RdKeyframePolicy.h
 *
 *  关键帧是 BITMAPINFO 加整屏首帧，控制端靠它建立画面尺寸。2026-10-12 之前推流循环只在三种时候发：开始推流、控制端请求、
 *  经中转时每隔固定帧数。被控端改了分辨率后，编码器把下一帧当成没有基准的帧、只发帧头，控制端一直停在旧画面；
 *  经 P2P 时没有定时重发，再也恢复不了（docs/远程桌面控制权限与观看工具条方案.md 第 3.2 节）。现在采集尺寸与上一次关键帧的
 *  尺寸不同时立即补发一次，经 P2P 时也发。2026-10-14 起尺寸是否变化改由编码器报告（CScreenFrameEncoder::NeedsKeyframe），
 *  推流循环先抓帧再判断，分辨率变化的当轮就发出关键帧（同一文档 8.2 第 7 条）。
 *  单元测试见 Source/Tests/RdViewFitTests.cpp（RES3）。
 *
 *  balloonwj@qq.com   2026-10-12
 */
#pragma once

namespace remotedesktop {

/**
 *  这一轮要不要发关键帧。
 *  @param requested            控制端请求了关键帧，或推流刚开始
 *  @param periodic             是否定时重发（经中转时是，经 P2P 时否）
 *  @param framesSinceKeyframe  上一次关键帧之后已发的增量帧数
 *  @param framesPerKeyframe    定时重发的间隔（增量帧数）
 *  @param encoderNeedsKeyframe 编码器报告需要关键帧（这一轮抓到的是第一帧，或画面尺寸变了）
 *  @return 要发关键帧返回 true
 */
bool RdShouldSendKeyframe(bool requested, bool periodic, int framesSinceKeyframe, int framesPerKeyframe,
                          bool encoderNeedsKeyframe);

} // namespace remotedesktop
