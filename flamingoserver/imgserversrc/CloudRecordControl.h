/**
 *  imgserver 一侧的云端录制控制，CloudRecordControl.h
 *
 *  第 26 期（docs/屏幕共享与录制方案.md 第 12.6.2 节）：chatserver 经控制连接（cmd_cloudrec_ctl_hello / cmd_cloudrec_ctl_data，
 *  令牌角色 role_server_control）发来开始、暂停、继续、停止录制与删除文件的命令，本类解析后交给房间中转与录制器执行，
 *  并把录制器的结束回报、scene 6 上传完成的通知写成事件发回 chatserver。消息格式见 utils/CloudRecordCtlMsg.h。
 *  规则：
 *    - 同一时刻只有一条控制连接：新连接报到成功后替换旧的；
 *    - 没有控制连接时产生的事件暂存（最多 kMaxPendingEvents 条，超出时丢掉最早的并记日志），下次有控制连接报到时依次补发；
 *    - 停止命令可以重复发：录像 id 已不在录、也不在收尾时，按录像目录里的文件现状回一条 finished（文件不存在时 reason 为
 *      write_error、bytes 为 0），chatserver 据此补齐因连接中断漏掉的结束回报。chatserver 须按录像 id 去重。
 *  本类不依赖网络层：发送出口是 ICloudRecordCtlSink（imgserver 里由控制连接的 RelaySession 实现），开始录制经 StartFn
 *  交给 ScreenShareRoomRelay::startCloudRecord，时钟经 ClockFn 取，测试可全部替换。全部公开方法线程安全。
 *
 *  典型用法（RelayServer 里）：
 *      m_cloudControl.setRecorder(&m_recorder);
 *      m_cloudControl.setStartFn(std::bind(&ScreenShareRoomRelay::startCloudRecord, &m_roomRelay, _1, _2));
 *      m_cloudControl.setClockFn(&ScreenShareRoomRelay::recordNowMs);
 *      m_recorder.setListener(&m_cloudControl);
 *      ...控制连接报到成功：m_cloudControl.attach(session);
 *      ...收到 1864：std::string reply = m_cloudControl.onCommand(body); 非空时发回
 *
 *  balloonwj@qq.com   2026-10-06
 */
#pragma once

#include <stddef.h>
#include <stdint.h>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include "CloudRecorder.h"

//控制消息的发送出口
class ICloudRecordCtlSink
{
public:
    virtual ~ICloudRecordCtlSink() {}

    /**
     *  把一条控制消息（JSON 文本）发给 chatserver。可从任意线程调用；连接已断开时由实现方丢弃。
     *  @param body 消息，格式见 utils/CloudRecordCtlMsg.h
     */
    virtual void sendCloudRecordCtl(const std::string& body) = 0;
};

//imgserver 一侧的云端录制控制；由 RelayServer 持有一份
class CloudRecordControl final : public ICloudRecorderListener
{
public:
    //没有控制连接时最多暂存的事件数
    static const size_t kMaxPendingEvents = 1024;

    /**
     *  开始录某个房间。
     *  @param recordId 录像 id
     *  @param roomId   房间号
     *  @return 取值见 cloudrec_start_result
     */
    typedef std::function<int(uint64_t recordId, const std::string& roomId)> StartFn;

    //取当前时刻（Unix 毫秒，单调推算），暂停与继续记录用
    typedef int64_t (*ClockFn)();

    CloudRecordControl();
    ~CloudRecordControl();

    CloudRecordControl(const CloudRecordControl&) = delete;
    CloudRecordControl& operator=(const CloudRecordControl&) = delete;

    //设置录制器（不拥有，生命周期须长于本对象）；须在接受连接之前调用
    void setRecorder(CloudRecorder* recorder);

    //设置开始录制的入口；须在接受连接之前调用
    void setStartFn(const StartFn& fn);

    //设置时钟；传 NULL 时用系统时间。须在接受连接之前调用
    void setClockFn(ClockFn fn);

    /**
     *  控制连接报到成功：登记为当前出口（之前的出口不再使用），并依次补发暂存的事件。
     *  @param sink 控制连接；本类只保存弱引用
     */
    void attach(const std::shared_ptr<ICloudRecordCtlSink>& sink);

    /**
     *  控制连接断开：若它是当前出口则清除，之后的事件暂存。
     *  @param sink 断开的控制连接（只比较地址）
     */
    void detach(const ICloudRecordCtlSink* sink);

    /**
     *  处理 chatserver 发来的一条命令。
     *  @param body 消息 JSON
     *  @return 要回给 chatserver 的消息（start 回 started、ping 回 pong、对已结束录像的 stop 回 finished）；不需要回时为空串。
     *          消息格式不对时记日志并返回空串
     */
    std::string onCommand(const std::string& body);

    //录制器回报一次录制结束（在录制器的写线程上调用）：写成 finished 事件发出或暂存
    virtual void onCloudRecordFinished(const CloudRecordResult& result) override;

    /**
     *  合成后的录像已上传完（scene 6 上传完成时调用，任意线程）：写成 uploaded 事件发出或暂存。
     *  @param recordId 录像 id
     *  @param fileName 录像目录里的文件名
     *  @param bytes    文件字节数
     */
    void notifyUploaded(uint64_t recordId, const std::string& fileName, uint64_t bytes);

    //当前暂存的事件数（测试与日志用）
    size_t pendingCount() const;

private:
    //发出一条事件；没有控制连接时暂存
    void emit(const std::string& body);
    int64_t now() const;

    mutable std::mutex                 m_mutex;      //保护以下两项
    std::weak_ptr<ICloudRecordCtlSink> m_sink;       //当前的控制连接（弱引用）；为空表示没有
    std::deque<std::string>            m_pending;    //没有控制连接时暂存的事件，先进先出
    CloudRecorder*                     m_recorder;   //录制器（不拥有）；只在接受连接前设置
    StartFn                            m_startFn;    //开始录制的入口；只在接受连接前设置
    ClockFn                            m_clockFn;    //时钟；只在接受连接前设置
};
