/**
 *  云端录制的原始帧文件（.frec）格式，CloudRecordFile.h
 *
 *  第 26 期（docs/屏幕共享与录制方案.md 第 12.6.2 节）：主持人开启云端录制后，imgserver 在转发屏幕共享房间的帧时另写一份到
 *  cloudrecorddir 下的「rec_<录像 id>.frec」。服务器只保存原始帧、不解码不转码；录制结束后由开启者的客户端下载这个文件，
 *  离线合成为带标注、摄像头、多人画面的 MP4，再上传回云端（第 49 条）。本文件只管字节格式，是纯函数，便于两端各自实现、
 *  用同一组样本互相核对（客户端的读取见 FlamingoMedia/Record/CloudRecordReader）。
 *
 *  文件布局（全部整数为小端）：
 *    文件头：「FREC」4 字节 | 版本 u16 | 标志 u16（frec_flag_*）| 录像 id u64 | 开始时刻 i64（Unix 毫秒）| 房间号长度 u16 | 房间号
 *    之后是一条条记录：类型 u8 | 相对开始时刻的毫秒 u32 | 负载长度 u32 | 负载
 *      - 帧（frec_record_frame）：负载为房间中转帧的明文包体，即 11 字节帧头（轨道、发送者座位、标志、时间戳）加帧数据，
 *        与 imgserver 转发给观看者的内容完全相同（发送者座位已由服务端改写为真实值）；
 *      - 座位（frec_record_seat）：负载为座位号 u8 加 userid u64，表示该座位在这一刻属于这个人（开始录制时与有人进入房间时写）；
 *      - 暂停 / 继续（frec_record_pause / frec_record_resume）：没有负载；暂停期间的帧不写，合成时据此去掉暂停的时段。
 *  文件只追加、不回写；进程中途退出时，读取方读到最后一条完整的记录为止，残缺的末尾忽略。
 *
 *  典型用法（写入线程）：
 *      std::string bytes = cloudrecord::EncodeHeader(header);
 *      bytes += cloudrecord::EncodeRecord(cloudrecord::frec_record_frame, relMs, plainBody);
 *
 *  balloonwj@qq.com   2026-10-06
 */
#pragma once

#include <stddef.h>
#include <stdint.h>
#include <string>

namespace cloudrecord {

//文件开头的标识
const char kFrecMagic[] = "FREC";
//标识的字节数
const size_t kFrecMagicBytes = 4;
//当前格式版本
const uint16_t kFrecVersion = 1;
//单条记录负载的上限（字节）：中转帧本身不超过 2 MiB，这里留出余量，读取方据此识别损坏的文件
const uint32_t kFrecMaxPayloadBytes = 8U * 1024 * 1024;
//房间号长度上限（字节）
const size_t kFrecMaxRoomIdBytes = 255;

//文件头的标志位，可组合
enum frec_flag
{
    frec_flag_audio_only = 0x0001,   //只录声音（只有麦克风与电脑声音两条轨道）；第 53 条定为只做独立录制，本位保留，暂不置上
};

//记录的类型
enum frec_record_type
{
    frec_record_frame  = 1,   //一帧房间中转帧（明文包体）
    frec_record_seat   = 2,   //座位归属：负载为座位号 u8 + userid u64
    frec_record_pause  = 3,   //开始暂停：之后的帧不写，直到继续
    frec_record_resume = 4,   //继续录制
};

//文件头
struct FrecHeader
{
    uint16_t    m_version;     //格式版本，写入时为 kFrecVersion
    uint16_t    m_flags;       //标志位，取值见 frec_flag
    uint64_t    m_recordId;    //录像 id（chatserver 的 t_cloud_record.f_id）
    int64_t     m_startTimeMs; //开始录制的时刻，Unix 毫秒
    std::string m_roomId;      //屏幕共享房间号（不超过 kFrecMaxRoomIdBytes 字节）

    FrecHeader();
};

/**
 *  文件头的字节。
 *  @param header 文件头；房间号超长时截断到 kFrecMaxRoomIdBytes
 */
std::string EncodeHeader(const FrecHeader& header);

/**
 *  一条记录的字节。
 *  @param type    记录类型，取值见 frec_record_type
 *  @param relMs   相对开始时刻的毫秒数
 *  @param payload 负载（长度不超过 kFrecMaxPayloadBytes，超过时调用方不要写）
 */
std::string EncodeRecord(uint8_t type, uint32_t relMs, const std::string& payload);

//座位记录的负载：座位号 u8 + userid u64
std::string EncodeSeatPayload(int seat, int64_t userId);

/**
 *  从 data 的 pos 处读文件头；成功时 pos 移到文件头之后。
 *  @return 标识、版本正确且文件头完整时返回 true
 */
bool DecodeHeader(const std::string& data, size_t& pos, FrecHeader& out);

/**
 *  从 data 的 pos 处读一条记录；成功时 pos 移到这条记录之后。
 *  @return 记录完整且负载长度不超过上限时返回 true；读到末尾或末尾残缺（进程中途退出留下的）时返回 false，pos 不变
 */
bool DecodeRecord(const std::string& data, size_t& pos, uint8_t& type, uint32_t& relMs, std::string& payload);

//读座位记录的负载；长度不对时返回 false
bool DecodeSeatPayload(const std::string& payload, int& seat, int64_t& userId);

/**
 *  某条轨道要不要写进文件。
 *  @param track     中转帧头的轨道类型（screenshare_track_*）
 *  @param audioOnly 是否只录声音
 *  @return 只录声音时只要麦克风与电脑声音；否则屏幕、摄像头、声音、标注、控制帧都要（合成时用控制帧里的画面状态）
 */
bool ShouldRecordTrack(int track, bool audioOnly);

/**
 *  录像文件名：「rec_<录像 id>.frec」。
 *  @param recordId 录像 id
 */
std::string FrecFileName(uint64_t recordId);

} // namespace cloudrecord
