/**
 *  远程桌面中转帧封装 / 拆解 / 分片重组（纯逻辑），RdRelayFraming.h
 *
 *  把 imgserver 中转通道上「握手帧 / 数据帧」的封帧、拆帧（含粘包 / 半包），以及
 *  引擎帧的分片与重组，抽成一组不依赖 socket、不打日志、不碰 MFC 的纯逻辑，便于
 *  单元测试；真正的网络收发（RdRelayClient）调用本模块完成字节与帧的转换。
 *
 *  中转通道沿用 v2 FrameCodec 的「24 字节帧头 + 包体」帧格式：
 *    - 握手帧 cmd_rd_relay_hello(1853) 包体是 {"token":"..."}（不分片）；
 *    - 数据帧 cmd_rd_relay_data(1854) 包体 = 1 字节分片标志(rd_relay_chunk_*) +
 *      分片数据。引擎帧（屏幕帧 / 控制命令）可能超过中转单帧上限(2MiB)，故按固定
 *      块大小切成多个数据帧发送；imgserver 不解析、按会话原样转发，接收端依 TCP
 *      有序性把同一引擎帧的若干分片按到达顺序重组还原。
 *
 *  balloonwj@qq.com   2026-06-09
 */
#pragma once

#include <stdint.h>
#include <string>
#include <vector>

namespace remotedesktop {

//rd_relay_data 帧包体首字节的分片标志：标识该帧承载完整引擎帧还是其一个分片。
enum rd_relay_chunk_flag
{
    rd_relay_chunk_whole = 0,   //完整引擎帧：标志字节后即整个引擎帧，无需重组
    rd_relay_chunk_more  = 1,   //分片且后续还有：累积进重组缓冲，等待后续分片
    rd_relay_chunk_last  = 2,   //分片且为末片：累积后交出重组好的完整引擎帧
};

//从中转通道拆出的一个完整 v2 帧的语义结果（供 DrainFrames 输出与单测）。
struct RdRelayInboundFrame
{
    uint16_t                   m_cmd;    //帧命令：cmd_rd_relay_hello(1853) 或 cmd_rd_relay_data(1854)
    std::vector<unsigned char> m_body;   //帧包体：hello 帧为握手应答 JSON；data 帧为「分片标志 + 分片数据」
                                         //接入协议加密后，这里是<u>线上</u>包体，调用方须自行解密

    //以下三个字段是接入协议加密之后补的：解密要按帧头字段重建「附加认证数据」，
    //少任何一个都拼不出与发送方一致的那 16 字节，解密必然失败。
    uint32_t                   m_seq;        //帧序列号
    uint8_t                    m_flags;      //帧标志位，决定要不要解密 / 解压
    uint32_t                   m_originlen;  //包体最原始的明文长度（压缩前、加密前）

    RdRelayInboundFrame()
        : m_cmd(0), m_seq(0), m_flags(0), m_originlen(0)
    {
    }
};

namespace relayframing {

/**
 *  把 relay 令牌封成一个完整的 rd_relay_hello 握手帧（24 字节帧头 + 包体）。
 *  @param token relay 令牌（非空）
 *  @param out   [出参] 封帧结果**追加**写入其尾部（不清空）
 *  @return 成功返回 true
 */
bool BuildHelloFrame(const std::string& token, std::string& out);

/**
 *  把一个分片（带分片标志）封成一个 rd_relay_data 帧：包体 = [1字节 chunkFlag] + 分片数据。
 *  @param chunkFlag 分片标志，取值见 rd_relay_chunk_*
 *  @param data      分片数据起始地址；len 为 0 时可为 nullptr
 *  @param len       分片数据字节数
 *  @param out       [出参] 封帧结果**追加**写入其尾部（不清空）
 *  @return 成功返回 true；包体超出中转上限返回 false（out 不变）
 */
bool BuildChunkFrame(unsigned char chunkFlag, const unsigned char* data, size_t len, std::string& out);

/**
 *  把一个完整引擎帧按 chunkSize 切成若干 rd_relay_data 帧（顺序即发送顺序）。
 *  len<=chunkSize 时产出单个 whole 帧；否则产出 more...more,last 多帧。
 *  @param data      引擎帧字节起始地址；len 为 0 时可为 nullptr（产出一个空的 whole 帧）
 *  @param len       引擎帧字节数
 *  @param chunkSize 单个分片的数据字节上限（须 > 0；一期取 512 KiB）
 *  @param outFrames [出参] 每项是一个封好的完整帧字节，调用方须按序逐个发出
 *  @return 成功返回 true；chunkSize 为 0 或任一分片封帧失败返回 false
 */
bool BuildEngineFrames(const unsigned char* data, size_t len, size_t chunkSize,
                       std::vector<std::string>& outFrames);

/**
 *  把一个完整引擎帧按 chunkSize 切成若干中转数据帧的<u>包体</u>（不含帧头）。
 *  切分规则与 BuildEngineFrames 完全相同，区别只在于产出的是包体。
 *
 *  接入协议加密之后发送侧必须用这个版本：包体的加密要与写 socket 一起放在发送锁内
 *  完成。流加密按调用顺序推进内部计数器，若在锁外加密、锁内发送，多条业务线程
 *  （远程桌面的推流线程与控制命令线程）产出的密文上线顺序会与加密顺序错开，
 *  对端解密必然失败。所以「切包体」与「加密并套帧头」这两步必须拆开，
 *  前者可以在锁外做，后者只能在锁内做。
 *
 *  @param data      引擎帧字节起始地址；len 为 0 时可为 nullptr（产出一个空的 whole 包体）
 *  @param len       引擎帧字节数
 *  @param chunkSize 单个分片的数据字节上限（须 > 0）
 *  @param outBodies [出参] 每项是一个包体（[1字节 chunkFlag] + 分片数据），
 *                   调用方须按序逐个加密并发出。函数内部<u>不清空</u>该容器，追加写入
 *  @return 成功返回 true；chunkSize 为 0 或任一包体超出中转上限返回 false
 */
bool BuildEngineBodies(const unsigned char* data, size_t len, size_t chunkSize,
                       std::vector<std::string>& outBodies);

/**
 *  从累积缓冲拆出尽可能多的完整中转帧。仅识别 cmd_rd_relay_hello /
 *  cmd_rd_relay_data；遇其它 cmd 或协议错误（魔数 / 版本 / 长度 / CRC 非法）
 *  置 outError=true。缓冲尾部不足一个完整帧的部分不消费，留待补齐后重试。
 *  @param buf       待解析字节缓冲起始地址
 *  @param len       buf 中可读字节数
 *  @param outFrames [出参] 每拆出一帧追加一项（m_cmd + m_body）
 *  @param outError  [出参] 遇协议错误 / 非中转 cmd 置 true（调用方应关连接）
 *  @return 已消费字节数；调用方据此前移读指针，剩余为半帧
 */
size_t DrainFrames(const unsigned char* buf, size_t len,
                   std::vector<RdRelayInboundFrame>& outFrames, bool& outError);

} // namespace relayframing

//接收端分片重组器：按到达顺序喂入 rd_relay_data 帧包体（含分片标志），重组出完整
//引擎帧。依赖 TCP 有序——同一引擎帧的若干分片连续到达、中间不插入其它引擎帧的
//分片。每条中转连接的接收侧持有一个本对象。
class RdRelayReassembler
{
public:
    RdRelayReassembler();

    /**
     *  喂入一个 rd_relay_data 帧的包体（首字节为 rd_relay_chunk_*）。
     *  @param body     帧包体起始地址（首字节为分片标志）
     *  @param bodylen  包体字节数（至少 1）
     *  @param outFrame [出参] 构成完整引擎帧时填入重组好的引擎帧字节
     *  @return 构成完整引擎帧返回 true（outFrame 有效）；分片累积中返回 false；
     *          包体非法（空 / 标志非法）时重置内部状态并返回 false
     */
    bool Feed(const unsigned char* body, size_t bodylen, std::vector<unsigned char>& outFrame);

    //清空重组状态（连接建立 / 重置 / 异常时调用）。
    void Reset();

private:
    std::vector<unsigned char> m_buf;         //分片重组缓冲（累积 more / last 分片数据）
    bool                       m_inProgress;  //是否正在重组一个被分片的引擎帧
};

} // namespace remotedesktop
