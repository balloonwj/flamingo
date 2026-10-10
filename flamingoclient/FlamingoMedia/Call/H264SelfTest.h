/**
 *  H.264 编解码自测（调试入口），H264SelfTest.h
 *
 *  不依赖摄像头地验证 MF H.264 编解码 round-trip：造一帧合成 NV12（左暗右亮的灰度渐变），
 *  经 MfH264Encoder 编码、MfH264Decoder 解码，把首个解码帧转 BGRA 落成 BMP，供离线打开核对
 *  「编码→解码」确实还原出可辨认的画面。仅 --h264selftest 调试入口手动用。
 *
 *  balloonwj@qq.com   2026-06-15
 */
#pragma once

namespace call {

/**
 *  运行一次 H.264 编解码自测（--h264selftest 调试入口）：合成 NV12 → 编码 → 解码 →
 *  首帧落 BMP（E:\flamingo\h264_selftest_decoded.bmp）。弹框报告结果。
 *  @return 至少解出一帧返回 true；否则 false
 */
bool RunH264SelfTest();

} // namespace call
