/**
 *  v2 帧「包体」的编解码管线：压缩与加密按固定顺序串起来，服务端侧。
 *
 *  帧头由 FrameCodec 负责，本文件只管包体，两者合起来才是一个完整的帧。
 *
 *  <u>存在的意义是把处理顺序收敛到一处</u>。发送时必须先压缩、后加密，接收时
 *  必须先解密、后解压 —— 顺序不能反，密文是高熵数据压不动，先加密再压缩等于
 *  白压。这个顺序一旦在某个调用点写反，表现是对端解不出的乱码，排查代价很高。
 *  把它封在本模块里，全仓库就只写一次，别处想写反也没有机会。
 *
 *  压缩阈值、「压缩后确实更小才采用」这条策略、以及附加认证数据怎么拼，同样
 *  收在这里，调用方不必知道。日后要换压缩算法（zstd / lz4）或换加密算法，改
 *  本文件即可，各调用点不用动。
 *
 *  客户端有一份镜像实现（flamingoclient/Source/net/FramePayload.h），线上格式
 *  必须逐字节一致，改动本文件时<u>必须同步改那一份</u>。
 *
 *  典型用法（发送侧，见 chatserversrc/TcpSession.cpp）：
 *      std::string wire;
 *      uint8_t     flags = 0;
 *      uint32_t    originlen = 0;
 *      net::encodeFramePayload(cmd, seq, body, cipher, 0,
 *                              net::payload_compress_never, wire, flags, originlen);
 *      net::encodeFrame(cmd, seq, flags, wire.data(), (uint32_t)wire.size(),
 *                       originlen, frame);
 *
 *  balloonwj@qq.com   2026-08-13
 */
#pragma once

#include <stdint.h>
#include <string>

#include "FrameCrypto.h"   //FrameCipher

//本头文件<u>刻意不包含 chatserversrc/Protocol.h</u>：它与遗留的 v1 协议头 Msg.h
//定义了同名的 msg_type / error_code 等枚举，同时进一个翻译单元会重定义冲突。
//故接口只收发基本类型，帧标志位等常量在 .cpp 里用。

namespace net {

//包体压缩策略，见 encodeFramePayload 的 policy 参数
enum payload_compress_policy
{
    //按阈值自动决定：包体长度达到 COMPRESS_THRESHOLD、且压缩后确实更小时才压缩。
    //客户端发出的业务帧走这条
    payload_compress_auto  = 0,
    //一律不压缩。服务端当前发出的帧走这条，与收敛之前的行为保持一致；
    //中转通道转发时也走这条（压缩标志是两端客户端之间的语义，服务端只透传）
    payload_compress_never = 1,
};

/**
 *  把明文包体处理成线上包体：<u>先压缩、后加密</u>。
 *
 *  @param cmd          本帧的 cmd。加密时纳入附加认证数据，使它无法被中途改写
 *  @param seq          本帧的 seq，同上
 *  @param plain        明文包体，可以为空串（心跳这类空包体帧）
 *  @param cipher       本连接的帧体加解密器。传 NULL、或其 ready() 为 false 时
 *                      不加密 —— 对应「本进程未启用加密」与「握手尚未完成」两种
 *                      情形，握手帧本身正是靠后者走明文的。<u>本函数不接管其所有权</u>
 *  @param extraFlags   调用方要求附加的帧标志位（典型是 frame_flag_has_attachment）。
 *                      压缩位与加密位由本函数按实际处理结果自行置上，不必传入
 *  @param policy       压缩策略，见 enum payload_compress_policy
 *  @param wireOut      [出参] 线上包体，即最终应写进帧的那段字节。函数内部先清空它
 *  @param flagsOut     [出参] 最终应写进帧头 flags 字段的值
 *  @param originLenOut [出参] 最终应写进帧头 originlen 字段的值，即最原始的明文长度
 *  @return 成功返回 true。加密失败时返回 false，此时 wireOut 已被清空，
 *          调用方应放弃发送本帧
 *  副作用：无。
 */
bool encodeFramePayload(uint16_t cmd, uint32_t seq, const std::string& plain,
                        FrameCipher* cipher, uint8_t extraFlags,
                        payload_compress_policy policy,
                        std::string& wireOut, uint8_t& flagsOut, uint32_t& originLenOut);

/**
 *  把收到的线上包体还原成明文：<u>先解密、后解压</u>。
 *
 *  参数取的是帧头里的四个字段而不是 frame_header 本身，与 encodeFramePayload 对称。
 *  这样调用方手里没有完整帧头时（例如中转转发只关心其中几个字段）不必去拼一个假的。
 *
 *  @param cmd       本帧的 cmd，用于重建附加认证数据，必须与发送方一致
 *  @param seq       本帧的 seq，同上
 *  @param flags     本帧的 flags，决定要不要解密 / 解压；它本身也参与认证
 *  @param originlen 明文的原始长度，即解压的目标长度。已由 decodeFrame 校验过
 *                   不超过包体上限，据此分配内存是安全的
 *  @param wire      线上包体字节
 *  @param cipher    本连接的帧体加解密器，语义同 encodeFramePayload。
 *                   帧上置了加密位而这里为 NULL 或未就绪时，本函数判失败
 *  @param plainOut  [出参] 还原出的明文包体。函数内部先清空它
 *  @param errOut    [出参] 失败原因（英文，可直接进日志），成功时清空
 *  @return 成功返回 true。返回 false 时调用方<u>必须关闭连接</u>，
 *          不得跳过本帧继续解下一帧 —— 流加密的内部状态已经对不上了
 *  副作用：无。
 */
bool decodeFramePayload(uint16_t cmd, uint32_t seq, uint8_t flags, uint32_t originlen,
                        const std::string& wire, FrameCipher* cipher,
                        std::string& plainOut, std::string& errOut);

/**
 *  只加密、不压缩：把一段<u>已经处于最终形态</u>的包体加密成线上包体。
 *
 *  这是 encodeFramePayload 的下半段，单独暴露出来是给<u>中转通道</u>用的。
 *  imgserver 转发屏幕帧 / 音视频帧时有两点与普通发送不同：一是包体可能仍是
 *  发送端压缩过的 zlib 数据，服务端既不该解压也不该重压，只需换一层加密；
 *  二是此时的「明文原始长度」是发送端那一帧的 originlen，不能按本次传入的
 *  字节数重算，否则接收端解压的目标长度就错了。这两点都不符合 encodeFramePayload
 *  的语义，故把这一层单独导出，而不是给它加特例分支。
 *
 *  @param cmd       本帧的 cmd，纳入附加认证数据
 *  @param seq       本帧的 seq，同上
 *  @param srcFlags  调用方要求带上的帧标志位，压缩位 / 附件位等原样保留；
 *                   其中的加密位会被忽略，并按本次是否真的加密重新置位
 *  @param originlen 最原始的明文长度（压缩前、加密前），由调用方给定；
 *                   它既要写进帧头，也参与认证
 *  @param plain     待加密的包体，可以是压缩后的字节，也可以为空串
 *  @param cipher    本连接的帧体加解密器，语义同 encodeFramePayload：
 *                   传 NULL 或其 ready() 为 false 时不加密
 *  @param wireOut   [出参] 线上包体。函数内部先清空它，故不可与 plain 是同一对象
 *  @param flagsOut  [出参] 最终应写进帧头 flags 字段的值
 *  @return 成功返回 true。加密失败时返回 false，此时 wireOut 已被清空，
 *          调用方应放弃发送本帧
 *  副作用：无。
 */
bool encryptFramePayload(uint16_t cmd, uint32_t seq, uint8_t srcFlags, uint32_t originlen,
                         const std::string& plain, FrameCipher* cipher,
                         std::string& wireOut, uint8_t& flagsOut);

/**
 *  只解密、不解压：把线上包体还原到「发送端压缩之后」的那一层。
 *
 *  这是 decodeFramePayload 的上半段，与 encryptFramePayload 对称，同样是给
 *  中转通道用的 —— 服务端只负责把包体从本端密钥换成对端密钥，压缩标志是两端
 *  客户端之间的语义，中途解压再压既没有意义也会白费 CPU。
 *
 *  参数含义与 decodeFramePayload 完全一致，区别只是不看 frame_flag_compressed。
 *
 *  @return 成功返回 true。返回 false 时调用方<u>必须关闭连接</u>，
 *          不得跳过本帧继续解下一帧 —— 流加密的内部状态已经对不上了
 *  副作用：无。
 */
bool decryptFramePayload(uint16_t cmd, uint32_t seq, uint8_t flags, uint32_t originlen,
                         const std::string& wire, FrameCipher* cipher,
                         std::string& plainOut, std::string& errOut);

} // namespace net
