/**
 *  云端录制控制连接上的消息格式，CloudRecordCtlMsg.h
 *
 *  第 26 期（docs/屏幕共享与录制方案.md 第 12.6.2 节）：chatserver 主动连 imgserver 的中转端口，用 cmd_cloudrec_ctl_hello（1863）
 *  报到后，两边经 cmd_cloudrec_ctl_data（1864）互发 JSON 消息，字段 op 区分种类：
 *
 *    chatserver → imgserver
 *      {"op":"start","recordid":42,"roomid":"ss-..."}     开始录某个屏幕共享房间；imgserver 回 started
 *      {"op":"pause","recordid":42} / resume / stop       暂停、继续、停止；停止后 imgserver 在文件关闭时发 finished
 *      {"op":"delete","files":["rec_42.frec","rec_42.mp4"]}   删除录像目录里的文件（只接受不带路径的文件名）
 *      {"op":"ping"}                                       探活；imgserver 回 pong
 *
 *    imgserver → chatserver
 *      {"op":"started","recordid":42,"code":0}             开始录制的结果，code 取值见 CloudRecorder.h 的 cloudrec_start_result
 *      {"op":"finished","recordid":42,"file":"rec_42.frec","bytes":..,"durationms":..,"frames":..,"dropped":..,"reason":..}
 *                                                          一次录制结束、文件已关闭；reason 取值见 cloudrec_stop_reason
 *      {"op":"uploaded","recordid":42,"file":"rec_42.mp4","bytes":..}
 *                                                          开启者的客户端把合成好的 MP4 上传完了（scene 6 上传完成时发）
 *      {"op":"pong"}
 *
 *  64 位整数一律按 JSON 整数收发（jsoncpp 的 UInt64 / Int64）。本文件只有纯函数，两边共用，单元测试见
 *  tests/test_cloudrecord.cpp（CRCM 系列）。
 *
 *  典型用法：
 *      CloudRecordCtlMsg msg;
 *      msg.m_op = cloudrecctl_op_start;
 *      msg.m_recordId = 42;
 *      msg.m_roomId = roomId;
 *      std::string body = BuildCloudRecordCtlMsg(msg);
 *      ...
 *      CloudRecordCtlMsg in;
 *      if (ParseCloudRecordCtlMsg(body, in) && in.m_op == cloudrecctl_op_finished) ...
 *
 *  balloonwj@qq.com   2026-10-06
 */
#pragma once

#include <stdint.h>
#include <string>
#include <vector>

//控制消息的种类（JSON 的 op 字段）
enum cloudrecctl_op
{
    cloudrecctl_op_unknown  = 0,    //解析时遇到不认识的 op（对端版本更新）：调用方忽略这条消息
    cloudrecctl_op_start    = 1,    //chatserver 要求开始录制，op 为 "start"
    cloudrecctl_op_pause    = 2,    //chatserver 要求暂停，op 为 "pause"
    cloudrecctl_op_resume   = 3,    //chatserver 要求继续，op 为 "resume"
    cloudrecctl_op_stop     = 4,    //chatserver 要求停止，op 为 "stop"
    cloudrecctl_op_delete   = 5,    //chatserver 要求删除文件，op 为 "delete"
    cloudrecctl_op_ping     = 6,    //chatserver 探活，op 为 "ping"
    cloudrecctl_op_pong     = 7,    //imgserver 回应探活，op 为 "pong"
    cloudrecctl_op_started  = 8,    //imgserver 回报开始录制的结果，op 为 "started"
    cloudrecctl_op_finished = 9,    //imgserver 回报一次录制结束，op 为 "finished"
    cloudrecctl_op_uploaded = 10,   //imgserver 回报合成后的 MP4 已上传完，op 为 "uploaded"
};

//一条控制消息；按 op 只用到其中一部分字段，其余保持默认值
struct CloudRecordCtlMsg
{
    int                      m_op;           //种类，取值见 cloudrecctl_op
    uint64_t                 m_recordId;     //录像 id（start / pause / resume / stop / started / finished / uploaded）
    std::string              m_roomId;       //屏幕共享房间号（start）
    int                      m_code;         //开始录制的结果（started），取值见 cloudrec_start_result
    std::vector<std::string> m_files;        //要删除的文件名（delete），不带目录
    std::string              m_file;         //文件名（finished / uploaded），不带目录
    uint64_t                 m_bytes;        //文件字节数（finished / uploaded）
    int64_t                  m_durationMs;   //录到的时长，毫秒（finished）
    uint64_t                 m_frames;       //写进文件的帧数（finished）
    uint64_t                 m_dropped;      //写盘积压时丢弃的帧数（finished）
    int                      m_reason;       //结束原因（finished），取值见 cloudrec_stop_reason

    CloudRecordCtlMsg();
};

/**
 *  把一条控制消息写成紧凑 JSON。
 *  @param msg 消息；m_op 为 cloudrecctl_op_unknown 时返回空串
 *  @return JSON 文本
 */
std::string BuildCloudRecordCtlMsg(const CloudRecordCtlMsg& msg);

/**
 *  解析一条控制消息。
 *  @param body JSON 文本
 *  @param out  [出参] 解析结果；op 不认识时 m_op 为 cloudrecctl_op_unknown 且返回 true
 *  @return 不是 JSON 对象、没有 op 字段，或 op 要求的字段缺失、类型不对时返回 false
 */
bool ParseCloudRecordCtlMsg(const std::string& body, CloudRecordCtlMsg& out);

/**
 *  op 的文字名称（日志用）。
 *  @param op 取值见 cloudrecctl_op
 *  @return 如 "start"；不认识时为 "unknown"
 */
const char* CloudRecordCtlOpName(int op);

//云端录像目录里的文件种类（由文件名后缀区分）
enum cloudrec_file_type
{
    cloudrec_file_type_frec = 1,   //录制中由 imgserver 写的原始帧文件：rec_<id>.frec
    cloudrec_file_type_mp4  = 2,   //合成后或独立录制上传的视频：rec_<id>.mp4
    cloudrec_file_type_m4a  = 3,   //独立录制「只录声音」上传的录音：rec_<id>.m4a
};

/**
 *  云端录像目录里的文件名：「rec_<录像 id>.<后缀>」。chatserver 记库、签令牌，imgserver 写盘、删文件都用它。
 *  @param recordId 录像 id（>0）
 *  @param type     文件种类，取值见 cloudrec_file_type
 *  @return 文件名；参数不合法时为空串
 */
std::string CloudRecordFileName(uint64_t recordId, int type);

/**
 *  从文件名解析出录像 id 与种类（imgserver 收到 scene 6 上传完成时据此回报是哪条录像）。
 *  @param name     文件名，不带目录
 *  @param recordId [出参] 录像 id
 *  @param type     [出参] 文件种类
 *  @return 形如 rec_<正整数>.<frec|mp4|m4a> 时返回 true
 */
bool ParseCloudRecordFileName(const std::string& name, uint64_t& recordId, int& type);

/**
 *  云端录像文件令牌（scene 为 file_scene_cloud_record）的 md5 字段：文件名里的「.」换成「_」，如 rec_42.mp4 → rec_42_mp4。
 *  令牌的字段里不能出现「.」与「|」，所以不能直接放文件名。
 *  @param fileName 合法的录像文件名（见 ParseCloudRecordFileName）
 *  @return 键；文件名不合法时为空串
 */
std::string CloudRecordTokenKey(const std::string& fileName);

/**
 *  从令牌的 md5 字段还原文件名（imgserver 收到 scene 6 的上传、下载分片时用）。
 *  @param key      令牌的 md5 字段
 *  @param fileName [出参] 文件名
 *  @return 还原出的是合法的录像文件名时返回 true
 */
bool ParseCloudRecordTokenKey(const std::string& key, std::string& fileName);
