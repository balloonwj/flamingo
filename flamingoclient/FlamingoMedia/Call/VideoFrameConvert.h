/**
 *  视频帧颜色空间转换，VideoFrameConvert.h
 *
 *  音视频通话视频管线的纯逻辑底座：在摄像头 / 编解码常用的 NV12（YUV 4:2:0 半平面）与
 *  Windows 显示用的 BGRA（32 位，DIB / StretchDIBits）之间转换。摄像头采集（MF Source
 *  Reader）与 H.264 解码（MF MFT）都产出 NV12，预览 / 远端画面要画成 BGRA 位图。
 *
 *  NV12 内存布局：先 Y 平面（height 行、每行 stride 字节、有效 width 个亮度样本），紧接
 *  UV 平面（height/2 行、每行 stride 字节，U、V 交错，每 2×2 个 Y 共用一组 U/V）。
 *  采用 BT.601 limited-range 系数（摄像头 / H.264 常用）。
 *
 *  纯逻辑，不碰设备 / GDI，便于单元测试（VideoFrameConvertTests.cpp）。
 *
 *  balloonwj@qq.com   2026-06-15
 */
#pragma once

#include <stdint.h>

namespace call {

/**
 *  把一帧 NV12 转成 BGRA（32 位，自上而下，每像素 B,G,R,A，A 恒为 255）。
 *  @param nv12       NV12 缓冲起始（Y 平面起点；UV 平面位于 nv12 + stride*height）
 *  @param width      画面宽（像素，应为偶数）
 *  @param height     画面高（像素，应为偶数）
 *  @param stride     NV12 行跨度（字节）：Y 平面与 UV 平面同跨度；须 ≥ width
 *  @param bgra       输出 BGRA 缓冲（非空，容量 ≥ bgraStride*height）
 *  @param bgraStride 输出行跨度（字节）：须 ≥ width*4，通常即 width*4
 */
void Nv12ToBgra(const uint8_t* nv12, int width, int height, int stride,
                uint8_t* bgra, int bgraStride);

/**
 *  Nv12ToBgra 的两种实现，参数与结果都与 Nv12ToBgra 相同（逐字节一致，单元测试 SSVP15，2026-10-15）。
 *  Nv12ToBgra 在 Release 版用 SSE2 实现（一次 8 个像素）、在 Debug 版用逐像素实现，与 ConvertBgraToNv12 相同的取舍：
 *  Debug 版不优化向量指令，SSE2 实现反而更慢。这两个函数供单元测试与性能对比直接调用，产品代码请用 Nv12ToBgra。
 */
void Nv12ToBgraSse2(const uint8_t* nv12, int width, int height, int stride,
                    uint8_t* bgra, int bgraStride);

void Nv12ToBgraReference(const uint8_t* nv12, int width, int height, int stride,
                         uint8_t* bgra, int bgraStride);

/**
 *  只转换 NV12 画面左上角 width × height 的区域（2026-10-15）：解码器的输出按 16 对齐补齐（如 1080 解成 1088）时，
 *  直接按载荷里的真实尺寸转换，不再先转整幅再逐行拷贝。结果与先 Nv12ToBgra 整幅再裁剪逐字节相同；
 *  Release 版用 SSE2 实现、Debug 版用逐像素实现。
 *  @param nv12        NV12 缓冲起始（Y 平面起点）
 *  @param planeHeight Y 平面的行数（UV 平面位于 nv12 + stride × planeHeight），须不小于 height
 *  @param stride      NV12 行跨度（字节）
 *  @param width       要转换的宽（像素，不大于行跨度）
 *  @param height      要转换的高（像素）
 *  @param bgra        输出 BGRA 缓冲（容量 ≥ bgraStride × height）
 *  @param bgraStride  输出行跨度（字节），须 ≥ width × 4
 */
void Nv12ToBgraCropped(const uint8_t* nv12, int planeHeight, int stride, int width, int height,
                       uint8_t* bgra, int bgraStride);

} // namespace call
