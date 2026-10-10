/**
 *  屏幕共享房间中转帧的封装、分片与重组（纯逻辑），ShareFraming.h
 *
 *  imgserver 房间中转的数据帧 cmd_screenshare_relay_data（1860）的包体为
 *  [轨道类型 1B][发送者座位号 1B][标志 1B][时间戳 8B 大端] + 载荷，服务端只读帧头决定转发与丢帧
 *  （docs/协议设计-v2.md 17.8）。本模块负责客户端这一侧：
 *    - 封装 / 拆解帧头；
 *    - 把一个大的视频帧（H.264 访问单元）切成多个分片，每个分片各占一帧 1860；
 *    - 接收端按「发送者座位号 × 轨道」分别重组，还原出完整的媒体帧；
 *    - 识别服务端发来的关键帧请求。
 *
 *  分片约定（客户端之间的约定，服务端不解析载荷）：只有视频轨（屏幕画面、摄像头画面）会拆分。视频帧的
 *  首分片载荷以 4 字节大端的整帧长度开头，接收端在尾分片到达时核对累计长度，不符（中间缺片）则整帧丢弃；
 *  非视频轨（声音、标注、控制）一律不拆分、不带长度前缀。一个关键帧的每个分片都带关键帧位。
 *
 *  本模块不依赖 socket、不打日志，便于单元测试，见 flamingoclient/Source/Tests/ShareFramingTests.cpp。
 *
 *  典型用法：
 *      std::vector<std::string> bodies;
 *      screenshare::ShareFraming::fragmentFrame(screenshare_track_screen, mySeat, isKey, ts, data, len,
 *                                               screenshare::kShareFragmentPayloadBytes, bodies);
 *      for (...) relay.SendBody(bodies[i]);
 *      ...
 *      screenshare::ShareFrameReassembler reassembler;
 *      screenshare::ShareMediaFrame frame;
 *      if (reassembler.push(body.data(), body.size(), frame)) 交给解码器;
 *
 *  balloonwj@qq.com   2026-10-02
 */
#pragma once

#include <stddef.h>
#include <stdint.h>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace screenshare {

//帧头的字节数：轨道类型 1、发送者座位号 1、标志 1、时间戳 8（与服务端 imgserversrc/RoomRelayFrame.h 一致）
const size_t kShareFrameHeaderSize = 11;

//视频帧首分片载荷开头的整帧长度字段的字节数
const size_t kShareFrameLengthPrefixSize = 4;

//视频帧载荷开头的画面尺寸字段的字节数：[宽 2B 大端][高 2B 大端]，见 ShareFraming::buildVideoPayload
const size_t kShareVideoPayloadHeaderSize = 4;

//每个分片载荷的默认上限，单位字节。中转单帧上限是 2 MiB（MAX_BODY_FILESERVER），取 64 KiB 是为了让服务端
//能在较小的粒度上决定丢弃慢观看者的非关键帧，同时每帧的帧头开销可以忽略
const size_t kShareFragmentPayloadBytes = 64 * 1024;

//帧头各字段
struct ShareFrameHeader
{
    int      m_track;        //轨道类型，取值见 Protocol.h 的 screenshare_track
    int      m_senderSeat;   //发送者座位号：成员 0~254，服务端生成的帧为 255
    int      m_flags;        //标志位，取值见 screenshare_frame_flag，可组合
    uint64_t m_timestamp;    //时间戳，单位由发送端定义（本客户端用微秒）

    ShareFrameHeader();
};

//重组之后的一个完整媒体帧
struct ShareMediaFrame
{
    int         m_track;        //轨道类型
    int         m_senderSeat;   //发送者座位号
    bool        m_keyframe;     //是否视频关键帧
    uint64_t    m_timestamp;    //时间戳
    std::string m_data;         //整帧数据（视频帧已去掉长度前缀）

    ShareMediaFrame();
};

//房间中转帧的封装与拆解工具类；全部为静态方法，不可实例化
class ShareFraming
{
public:
    /**
     *  封装一帧 1860 的包体：帧头 + 载荷。
     *  @param header  帧头；轨道类型与标志位按原值写入，座位号与轨道类型按 1 字节截断
     *  @param payload 载荷，可为 NULL（此时 len 须为 0）
     *  @param len     载荷字节数
     *  @return 包体字节串
     */
    static std::string encodeBody(const ShareFrameHeader& header, const char* payload, size_t len);

    /**
     *  拆解一帧 1860 的包体。
     *  @param body       包体起始地址，不接管所有权
     *  @param len        包体字节数
     *  @param header     [出参] 帧头
     *  @param payload    [出参] 载荷起始地址（指向 body 内部）
     *  @param payloadLen [出参] 载荷字节数
     *  @return 长度不小于帧头且轨道类型在 screenshare_track 的取值范围内返回 true
     */
    static bool decodeBody(const char* body, size_t len, ShareFrameHeader& header, const char*& payload,
                           size_t& payloadLen);

    /**
     *  把一个媒体帧切成若干个 1860 包体。视频轨按 maxPayload 拆分并在首分片载荷前加整帧长度；
     *  其余轨道整帧一个包体、不加长度前缀。
     *  @param track      轨道类型
     *  @param senderSeat 本人的座位号（服务端会改写为登记的座位，填对只是便于排查）
     *  @param keyframe   是否视频关键帧；为 true 时每个分片都带关键帧位
     *  @param timestamp  时间戳
     *  @param data       整帧数据，可为 NULL（此时 len 须为 0）
     *  @param len        整帧字节数
     *  @param maxPayload 每个分片载荷的上限，单位字节；为 0 时按 kShareFragmentPayloadBytes 处理
     *  @param outBodies  [出参] 依次要发送的包体，先清空再填入
     */
    static void fragmentFrame(int track, int senderSeat, bool keyframe, uint64_t timestamp, const char* data,
                              size_t len, size_t maxPayload, std::vector<std::string>& outBodies);

    /**
     *  判断一帧是否为服务端发来的关键帧请求（控制帧、发送者座位号 255、载荷为 [请求关键帧][视频轨]）。
     *  @param frame      重组之后的帧
     *  @param videoTrack [出参] 要求发关键帧的视频轨
     *  @return 是关键帧请求返回 true
     */
    static bool parseKeyFrameRequest(const ShareMediaFrame& frame, int& videoTrack);

    //是否视频轨（屏幕画面、摄像头画面）
    static bool isVideoTrack(int track);

    /**
     *  构造一帧「共享内容状态」控制帧的包体（1860 的包体，未加密）：轨道为控制帧、首尾分片位都置上、时间戳 0，
     *  载荷两字节 [screenshare_control_source_state][状态]。由发起人在状态变化时、以及每次发关键帧时发出。
     *  @param senderSeat 发起人的座位号
     *  @param state      状态，取值见 Protocol.h 的 screenshare_source_state
     */
    static std::string buildSourceStateBody(int senderSeat, int state);

    /**
     *  判断一帧是否为「共享内容状态」控制帧。
     *  @param frame [入参] 重组之后的帧
     *  @param state [出参] 状态
     *  @return 是该控制帧且状态取值合法返回 true
     */
    static bool parseSourceState(const ShareMediaFrame& frame, int& state);

    /**
     *  组一个视频帧的载荷：[画面宽 2B 大端][画面高 2B 大端] + H.264 访问单元。H.264 解码器输出的高会补齐到 16 的倍数
     *  （1080 解出来是 1088），观看端据这里的真实宽高裁掉补齐的部分（2026-10-02 编码自测查出）。
     *  @param width / height 编码画面的真实尺寸，单位像素（1~65535）
     *  @param h264 / len     H.264 访问单元
     *  @return 载荷字节串；尺寸越界时返回空串
     */
    static std::string buildVideoPayload(int width, int height, const char* h264, size_t len);

    /**
     *  拆一个视频帧的载荷。
     *  @param payload        载荷（ShareMediaFrame::m_data）
     *  @param width / height [出参] 画面真实尺寸
     *  @param h264 / len     [出参] H.264 访问单元（指向 payload 内部）
     *  @return 载荷长度足够且尺寸都大于 0 返回 true
     */
    static bool parseVideoPayload(const std::string& payload, int& width, int& height, const char*& h264,
                                  size_t& len);

private:
    ShareFraming();
};

/**
 *  接收端的分片重组器：按「发送者座位号 × 轨道」分别重组，不同发送者、不同轨道的分片交错到达互不影响。
 *  一个帧的首分片到达时开始累积；尾分片到达且累计长度与首分片声明的整帧长度一致时交出整帧。以下情况整帧丢弃：
 *  没有首分片就收到后续分片、累积中途又来了新的首分片、时间戳与首分片不一致、长度不符。
 *  不加锁，只在一条接收线程上使用。
 */
class ShareFrameReassembler
{
public:
    ShareFrameReassembler();

    /**
     *  喂入一帧 1860 包体。
     *  @param body 包体起始地址，不接管所有权
     *  @param len  包体字节数
     *  @param out  [出参] 返回 true 时为还原出的完整媒体帧
     *  @return 还原出一个完整媒体帧返回 true；分片尚未收齐、包体非法或被丢弃返回 false
     */
    bool push(const char* body, size_t len, ShareMediaFrame& out);

    //丢掉某个发送者的全部未完成分片（该成员离开房间时调用）
    void resetSender(int senderSeat);

    //因中间缺片、顺序错乱等原因被丢弃的帧数（供日志与测试统计）
    uint64_t droppedFrames() const;

private:
    //一个「发送者 × 轨道」正在累积的帧
    struct Partial
    {
        ShareFrameHeader m_header;        //首分片的帧头
        uint32_t         m_expectedLen;   //首分片声明的整帧长度（视频轨）
        std::string      m_data;          //已累积的数据（不含长度前缀）

        Partial();
    };

    typedef std::pair<int, int> StreamKey;   //(发送者座位号, 轨道类型)

    std::map<StreamKey, Partial> m_partials;        //正在累积的帧
    uint64_t                     m_droppedFrames;   //丢弃的帧数
};

} // namespace screenshare
