/**
 *  v2 协议帧编解码：把消息打包成「24 字节帧头 + 包体」的完整帧，
 *  以及从字节流中拆解出完整帧。
 *
 *  本模块只负责「框架层」——帧头序列化、魔数/版本/长度校验、CRC-32
 *  校验、粘包/拆包；不负责包体压缩（压缩由会话层在调用前/收到后另行
 *  处理，对应 frame_flag_compressed 标志位）。本模块无外部依赖，可独立
 *  单元测试，见 flamingoclient/tests/test_framecodec.cpp。
 *
 *  本文件由服务端 flamingoserver/net/FrameCodec.h 移植而来，除 include
 *  路径外与服务端保持一致——改动须两端同步。
 *
 *  典型用法：
 *      std::string out;
 *      net::encodeFrame(cmd_login, seq, jsonbody, out);   // 编码后发送
 *
 *      net::frame_header h; std::string body; size_t consumed = 0;
 *      if (net::decodeFrame(buf, buflen, h, body, consumed) == net::frame_decode_ok)
 *          ... // 按 h.cmd 分发，body 为包体
 *
 *  balloonwj@qq.com   2026-05-22
 */
#pragma once

#include <stdint.h>
#include <stddef.h>
#include <string>
#include "Protocol.h"

namespace net {

//帧解码结果
enum frame_decode_result
{
    frame_decode_ok        = 0,   //成功解出一个完整帧
    frame_decode_need_more = 1,   //缓冲区数据不足一个完整帧，应等待更多字节后重试
    frame_decode_error     = 2,   //协议错误（魔数/版本/长度/CRC 非法），调用方应关闭连接
};

/**
 *  把一条消息编码成完整的 v2 帧（24 字节帧头 + 包体），追加写入 out 尾部。
 *  @param cmd       消息类型，取值见 enum msg_type
 *  @param seq       序列号；请求填正数，服务器主动推送填 0
 *  @param flags     帧标志位，按位或组合，取值见 enum frame_flag；本函数不
 *                   做压缩，若包体已由调用方压缩，需在此带上 frame_flag_compressed
 *  @param body      包体字节（UTF-8 JSON 文本或二进制附件），可为 nullptr
 *  @param bodylen   包体实际字节数；为 0 表示空包体（如心跳）
 *  @param originlen 包体未压缩前的原始字节数；未压缩时应等于 bodylen
 *  @param out       输出缓冲；编码结果**追加**到其尾部，不清空（便于批量粘包）
 *  @return 成功返回 true；bodylen 超过协议上限返回 false
 */
bool encodeFrame(uint16_t cmd, uint32_t seq, uint8_t flags,
                 const char* body, uint32_t bodylen, uint32_t originlen,
                 std::string& out);

/**
 *  编码一个包体未压缩的帧（flags=0，originlen=bodylen），常用于普通 JSON 报文。
 *  @param cmd  消息类型
 *  @param seq  序列号
 *  @param body 包体（UTF-8 JSON 文本）
 *  @param out  输出缓冲，编码结果追加到尾部
 *  @return 成功返回 true
 */
bool encodeFrame(uint16_t cmd, uint32_t seq, const std::string& body, std::string& out);

/**
 *  编码一个空包体帧，用于心跳、无参请求等。
 *  @param cmd 消息类型
 *  @param seq 序列号
 *  @param out 输出缓冲，编码结果追加到尾部
 *  @return 成功返回 true
 */
bool encodeFrame(uint16_t cmd, uint32_t seq, std::string& out);

/**
 *  从 buffer 起始处尝试解出一个完整帧。
 *  @param buffer   待解析的字节缓冲（可能含 0~N 个完整帧，或不足一个帧）
 *  @param buflen   buffer 中可读字节数
 *  @param header   [出参] 解析出的帧头，各字段为主机字节序；解码结果非
 *                  frame_decode_need_more 时其 magic 等字段已被填充
 *  @param body     [出参] 解析出的包体（按线上字节原样拷贝，未解压）；
 *                  仅当返回 frame_decode_ok 时有效
 *  @param consumed [出参] 本帧在 buffer 中占用的字节数；仅当返回
 *                  frame_decode_ok 时有效，调用方据此前移读指针
 *  @param maxbody  允许的最大包体长度，默认取 chatserver 上限；用于防御恶意大包
 *  @return 见 enum frame_decode_result
 */
frame_decode_result decodeFrame(const char* buffer, size_t buflen,
                                frame_header& header, std::string& body,
                                size_t& consumed,
                                uint32_t maxbody = MAX_BODY_CHATSERVER);

/**
 *  计算 CRC-32（IEEE 多项式，初值 0xFFFFFFFF，结果取反）。
 *  供帧头校验内部使用，并单独导出以便单元测试。
 *  @param data 待校验数据起始地址
 *  @param len  待校验字节数
 *  @return 32 位 CRC 校验值
 */
uint32_t crc32(const void* data, size_t len);

/**
 *  把「描述符 + 二进制附件」编码成 HAS_ATTACHMENT 帧的包体。
 *
 *  包体布局（见 docs/协议设计-v2.md §11.1）：
 *      4 字节大端 desclen + 描述符字节（desclen 字节）+ 附件二进制（其余字节）
 *  本函数只产出「包体」，外层仍需调用 encodeFrame 套上 24 字节帧头，并在
 *  帧头 flags 置 frame_flag_has_attachment。
 *
 *  @param descriptor 附件描述符（UTF-8 JSON 文本，承载 fileid/offset 等元信息）
 *  @param binary     附件二进制数据起始地址；binlen 为 0 时可为 nullptr
 *  @param binlen     附件二进制字节数
 *  @param out        输出缓冲；编码结果**追加**到其尾部，不清空
 *  @return 成功返回 true；4 + 描述符 + 附件总长超过 fileserver 包体上限返回 false
 */
bool encodeAttachmentBody(const std::string& descriptor,
                          const char* binary, uint32_t binlen,
                          std::string& out);

/**
 *  从 HAS_ATTACHMENT 帧的包体中拆出描述符与附件二进制。
 *
 *  为避免大块附件被复制，本函数不拷贝二进制数据，而是回传其在 body 中的
 *  起始偏移与长度，由调用方按 (body + binaryoff, binarylen) 直接取用。
 *
 *  @param body       帧包体起始地址（decodeFrame 解出的 body）
 *  @param bodylen    帧包体字节数
 *  @param descriptor [出参] 描述符 JSON 文本
 *  @param binaryoff  [出参] 附件二进制在 body 中的起始偏移
 *  @param binarylen  [出参] 附件二进制字节数（可能为 0）
 *  @return 成功返回 true；body 不足 4 字节、或 desclen 越出 body 范围返回 false
 */
bool decodeAttachmentBody(const char* body, size_t bodylen,
                          std::string& descriptor,
                          size_t& binaryoff, size_t& binarylen);

} // namespace net
