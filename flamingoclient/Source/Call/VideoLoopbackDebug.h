/**
 *  本地视频回环（调试入口），VideoLoopbackDebug.h
 *
 *  用 CallVideoEngine 在本机跑「摄像头采集 → H.264 编码 → 回灌解码 → 取首个解码帧」整条
 *  视频管线，把首个解码帧落成 BMP，供离线核对「采集→编码→解码→显示」端到端通了。仅
 *  --videoloop 调试入口手动用。验证管线机制本身——画面内容取决于摄像头（被系统占位时为
 *  灰色占位图，但仍能证明整条链路工作）。
 *
 *  balloonwj@qq.com   2026-06-16
 */
#pragma once

namespace call {

/**
 *  运行一次本地视频回环（--videoloop 调试入口）：起 CallVideoEngine，编码帧回灌解码，
 *  把首个解码帧落 BMP（E:\flamingo\videoloop_decoded.bmp）；跑约 2.5 秒后停、弹框报告。
 *  @return 至少解出一帧返回 true；否则 false
 */
bool RunVideoLoopbackDebug();

} // namespace call
