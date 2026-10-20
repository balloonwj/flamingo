/**
 *  远程桌面引擎共享定义，RdEngineDef.h
 *
 *  抓屏端（RdScreenCapturer）与解码端（RdScreenDecoder）共用的「引擎帧」格式
 *  约定。引擎帧是承载在 v2 中转数据帧（cmd_rd_relay_data=1854）包体里的不透明
 *  二进制——imgserver 只按字节转发、不解析，真正解析它的是收发两端的引擎。
 *
 *  引擎帧 = 1 字节类型（rd_frame_type）+ 后续负载：
 *    rd_frame_bitmapinfo：屏幕 BITMAPINFO 字节（会话开始 / 分辨率变化时发一次）
 *    rd_frame_first     ：首屏全量 DIB 字节
 *    rd_frame_next      ：增量帧——头部（算法 1 + 光标位置 POINT 8 + 光标类型 1）
 *                         + 数据块（与 CScreenSpy::getNextScreen 产出一致）
 *
 *  balloonwj@qq.com   2026-06-08
 */
#pragma once

namespace remotedesktop {

//引擎帧类型，占引擎帧首字节
enum rd_frame_type
{
    rd_frame_bitmapinfo = 0,   //屏幕 BITMAPINFO（首字节后跟 BITMAPINFO 字节）；被控端→控制端
    rd_frame_first      = 1,   //首屏全量 DIB（首字节后跟整屏 DIB 字节）；被控端→控制端
    rd_frame_next       = 2,   //增量帧（首字节后跟「头部 + 变化数据块」）；被控端→控制端
    rd_frame_request_keyframe = 3,   //请求重发关键帧；控制端→被控端：
                                     //控制端连上中转后 / 缺关键帧时发出，促被控端立即补发 BITMAPINFO+首屏。
                                     //2026-10-15 起新控制端在首字节后附能力位 1B 与能解的最大长边 2B（RdVideoFrames），
                                     //旧被控端只看首字节，多出的负载被忽略
    //以下三种为 2026-10-15 远程桌面改用 H.264 后新增（docs/远程桌面H264与共享画质提升方案.md 4.3），旧控制端的解码器忽略它们
    rd_frame_video      = 4,   //H.264 视频帧：源宽高、编码宽高、关键帧标志、发送时刻 + H.264 访问单元；被控端→控制端
    rd_frame_cursor     = 5,   //指针位置与类型（指针变化时、每个关键帧之后）；被控端→控制端
    rd_frame_caps       = 6,   //被控端的能力位（每个关键帧之前，不论新旧格式）；被控端→控制端
};

//控制端→被控端方向的帧类型（与上面的 rd_frame_type 各自编号：1、2 已被鼠标、键盘命令占用，3 为关键帧请求）
enum rd_ctl_frame_type
{
    rd_ctl_video_ack = 4,   //视频帧回执（2026-10-15）：原样回送最近收到的 4 号帧的发送时刻，每 500 毫秒一次，只在收 H.264 期间发
};

//增量帧头部长度：算法标志(1) + 光标位置 POINT(8) + 光标类型(1)，与
//CScreenSpy::getNextScreen 写入的头部逐字节一致。
const int rd_next_header_len = 10;

//增量帧的编码算法标志（镜像 ScreenSpy.h 的 ALGORITHM_*，避免反向 include 抓屏头）。
const unsigned char rd_algo_scan = 1;   //隔行扫描：变化区按 [RECT + 矩形DIB] 块
const unsigned char rd_algo_diff = 2;   //差异比较：变化区按 [偏移 + 长度 + 数据] 块

} // namespace remotedesktop
