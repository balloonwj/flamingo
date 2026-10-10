/**
 *  屏幕共享房间中转数据帧（cmd_screenshare_relay_data=1860）的帧头解析与改写，RoomRelayFrame.h
 *
 *  1860 的包体（解密之后）以 11 字节帧头开头：[轨道类型 1B][发送者座位号 1B][标志 1B][时间戳 8B 大端]，
 *  其后是载荷。imgserver 只读帧头决定转发给谁、要不要丢，载荷原样转发；唯一的例外是服务端自己生成的控制帧
 *  （关键帧请求），由本模块构造。各字段的取值见 Protocol.h 的 screenshare_track / screenshare_frame_flag /
 *  screenshare_control_type / screenshare_seat，设计见 docs/屏幕共享与录制方案.md 5.4。
 *
 *  本模块为纯函数，不触及网络，便于单元测试，见 tests/test_roomrelayframe.cpp（Suite RRF）。
 *
 *  典型用法（RelaySession 收到一帧 1860 时）：
 *      RoomRelayFrameHeader header;
 *      if (!RoomRelayFrame::parseHeader(body.data(), body.size(), header))
 *          return;                                      //帧头不完整或轨道类型非法，丢弃
 *      RoomRelayFrame::rewriteSenderSeat(body, mySeat); //发送者座位号以本连接登记的为准
 *
 *  balloonwj@qq.com   2026-10-02
 */
#pragma once

#include <stddef.h>
#include <stdint.h>
#include <string>
#include <vector>

//帧头的字节数：轨道类型 1、发送者座位号 1、标志 1、时间戳 8
const size_t kRoomRelayHeaderSize = 11;

//帧头各字段
struct RoomRelayFrameHeader
{
    uint8_t  m_track;        //轨道类型，取值见 enum screenshare_track
    uint8_t  m_senderSeat;   //发送者座位号：成员为 0~254，服务端生成的帧为 255
    uint8_t  m_flags;        //标志位，取值见 enum screenshare_frame_flag，可组合
    uint64_t m_timestamp;    //时间戳：单位由发送端定义，服务端不解读，只原样转发

    RoomRelayFrameHeader();
};

//房间中转帧头的工具类；全部为静态方法，不可实例化
class RoomRelayFrame
{
public:
    /**
     *  解析帧头。
     *  @param data 1860 解密后的包体起始地址，不接管所有权
     *  @param len  包体长度，单位字节
     *  @param out  [出参] 解析成功时回填的帧头
     *  @return 长度不小于 kRoomRelayHeaderSize 且轨道类型在 screenshare_track 的取值范围内时返回 true
     */
    static bool parseHeader(const char* data, size_t len, RoomRelayFrameHeader& out);

    /**
     *  把包体帧头里的发送者座位号改写为本连接登记时的座位号，防止成员冒充别人发帧。
     *  @param body 1860 解密后的包体，就地修改
     *  @param seat 本连接登记的座位号（0~254）
     *  @return 包体长度足够容纳帧头时返回 true；否则不修改并返回 false
     */
    static bool rewriteSenderSeat(std::string& body, uint8_t seat);

    /**
     *  是否视频轨（屏幕画面、摄像头画面）。只有视频帧会因为观看者网络慢而被丢弃。
     *  @param track 轨道类型
     */
    static bool isVideoTrack(int track);

    /**
     *  构造一帧由服务端发出的关键帧请求（1860 的包体，未加密）：轨道类型为控制帧、发送者座位号为 255、
     *  首尾分片位都置上、时间戳为 0；载荷两字节，依次为 screenshare_control_keyframe_request 与要求的视频轨。
     *  @param videoTrack 要求发关键帧的视频轨：screenshare_track_screen 或 screenshare_track_camera
     *  @return 包体字节串
     */
    static std::string buildKeyFrameRequest(int videoTrack);

    /**
     *  某种角色的成员能否在房间里发这条轨道（docs/协议设计-v2.md 21.8）。群通话成员（RelayToken::role_groupcall_member）
     *  只能发摄像头画面、麦克风声音与控制帧；屏幕共享的发起人与观看者维持原来的规则，六种轨道都可以发。
     *  @param role  发送连接报到时令牌里的角色，取值见 RelayToken::role_*
     *  @param track 轨道类型，取值见 enum screenshare_track
     *  @return 可以发返回 true；返回 false 时调用方丢弃这一帧
     */
    static bool isTrackAllowedForRole(int role, int track);

    /**
     *  某个角色的连接是否接收房间里的转发（第 22 期）：「只推流」的连接（RelayToken::role_screenshare_publisher）只接收发给它这个
     *  座位的控制帧（关键帧请求、键鼠输入），房间里的画面、声音、标注一律不转给它；其余角色照常接收。
     */
    static bool receivesRoomFrames(int role);

    /**
     *  房间里的一帧要不要转给某个接收者（第 22 期）：接收者的角色须接收房间转发（见 receivesRoomFrames），且不是发送者本人的
     *  另一条连接 —— 共享者另连一条中转连接推自己的屏幕，他的画面不必再经中转回送到他自己的观看连接。
     *  @param receiverRole   接收者登记时的角色
     *  @param receiverUserId 接收者登记时的 userid；0 表示不知道
     *  @param senderUserId   发送者登记时的 userid；0 表示不知道（不知道时不按 userid 跳过）
     *  @return 要转给他返回 true
     */
    static bool shouldForwardTo(int receiverRole, int64_t receiverUserId, int64_t senderUserId);

    /**
     *  构造一帧订阅控制帧（群通话第 5 期）：轨道类型为控制帧、首尾分片位都置上、时间戳为 0，载荷为
     *  [screenshare_control_subscribe][座位数 N][N 个座位号]。发送者座位号填 0（服务端会改写为发送连接的座位）。
     *  @param seats 要看的摄像头座位号（1~254）；超过 254 个时只取前 254 个
     *  @return 包体字节串
     */
    static std::string buildSubscribe(const std::vector<int>& seats);

    /**
     *  解析订阅控制帧。
     *  @param body  1860 解密后的完整包体（帧头 + 载荷）
     *  @param seats [出参] 要看的摄像头座位号，按出现顺序；N 为 0 时为空
     *  @return 帧头合法、轨道为控制帧、载荷第 1 字节为 screenshare_control_subscribe 且长度与座位数相符时返回 true
     */
    static bool parseSubscribe(const std::string& body, std::vector<int>& seats);

    /**
     *  解析键鼠输入控制帧的目标座位（第 18 期控制键鼠）。载荷为 [screenshare_control_input][目标座位号][一条输入命令]，
     *  服务端只把它转发给目标座位、不广播；输入命令本身不解读。
     *  @param body       1860 解密后的完整包体（帧头 + 载荷）
     *  @param targetSeat [出参] 目标座位号（0~254）
     *  @return 帧头合法、轨道为控制帧、载荷第 1 字节为 screenshare_control_input、目标座位号合法且其后至少还有一个字节时
     *          返回 true；返回 false 时调用方按普通控制帧处理
     */
    static bool parseInputTarget(const std::string& body, int& targetSeat);

private:
    RoomRelayFrame();
};
