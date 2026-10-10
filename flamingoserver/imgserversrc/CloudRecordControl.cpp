/**
 *  imgserver 一侧的云端录制控制，职责见 CloudRecordControl.h。
 *  balloonwj@qq.com   2026-10-06
 */
#include "CloudRecordControl.h"

#include <chrono>
#include <vector>
#include "../base/AsyncLog.h"
#include "../utils/CloudRecordCtlMsg.h"
#include "CloudRecordFile.h"

namespace {

int64_t systemNowMs()
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::system_clock::now().time_since_epoch()).count();
}

} // namespace

//类内初始化的静态常量的类外定义：被 ODR-use 时 g++ 要求有类外定义，MSVC 不要求
const size_t CloudRecordControl::kMaxPendingEvents;

CloudRecordControl::CloudRecordControl()
    : m_recorder(NULL)
    , m_clockFn(NULL)
{
}

CloudRecordControl::~CloudRecordControl()
{
}

void CloudRecordControl::setRecorder(CloudRecorder* recorder)
{
    m_recorder = recorder;
}

void CloudRecordControl::setStartFn(const StartFn& fn)
{
    m_startFn = fn;
}

void CloudRecordControl::setClockFn(ClockFn fn)
{
    m_clockFn = fn;
}

int64_t CloudRecordControl::now() const
{
    return (m_clockFn != NULL) ? m_clockFn() : systemNowMs();
}

void CloudRecordControl::attach(const std::shared_ptr<ICloudRecordCtlSink>& sink)
{
    std::deque<std::string> pending;
    {
        std::lock_guard<std::mutex> guard(m_mutex);
        m_sink = sink;
        pending.swap(m_pending);
    }
    LOGI("cloud record control: control connection attached, pending events: %d", (int)pending.size());
    for (size_t i = 0; i < pending.size(); ++i)
        sink->sendCloudRecordCtl(pending[i]);
}

void CloudRecordControl::detach(const ICloudRecordCtlSink* sink)
{
    std::lock_guard<std::mutex> guard(m_mutex);
    std::shared_ptr<ICloudRecordCtlSink> current = m_sink.lock();
    //只清除当前出口：旧连接被新连接替换后才断开时，不能把新连接也清掉
    if (!current || current.get() == sink)
    {
        m_sink.reset();
        LOGI("cloud record control: control connection detached");
    }
}

size_t CloudRecordControl::pendingCount() const
{
    std::lock_guard<std::mutex> guard(m_mutex);
    return m_pending.size();
}

void CloudRecordControl::emit(const std::string& body)
{
    std::shared_ptr<ICloudRecordCtlSink> sink;
    {
        std::lock_guard<std::mutex> guard(m_mutex);
        sink = m_sink.lock();
        if (!sink)
        {
            if (m_pending.size() >= kMaxPendingEvents)
            {
                LOGE("cloud record control: too many pending events, drop the oldest: %s", m_pending.front().c_str());
                m_pending.pop_front();
            }
            m_pending.push_back(body);
            return;
        }
    }
    sink->sendCloudRecordCtl(body);
}

std::string CloudRecordControl::onCommand(const std::string& body)
{
    CloudRecordCtlMsg msg;
    if (!ParseCloudRecordCtlMsg(body, msg))
    {
        LOGE("cloud record control: bad command: %s", body.c_str());
        return std::string();
    }
    if (m_recorder == NULL)
    {
        LOGE("cloud record control: recorder not configured, command ignored: %s", CloudRecordCtlOpName(msg.m_op));
        return std::string();
    }

    CloudRecordCtlMsg reply;
    switch (msg.m_op)
    {
    //开始录制：交给房间中转（它取座位、开文件、请求关键帧），回 started
    case cloudrecctl_op_start:
    {
        const int code = m_startFn ? m_startFn(msg.m_recordId, msg.m_roomId) : static_cast<int>(cloudrec_start_bad_arg);
        LOGI("cloud record control: start, record: %llu, room: %s, result: %d",
             static_cast<unsigned long long>(msg.m_recordId), msg.m_roomId.c_str(), code);
        reply.m_op = cloudrecctl_op_started;
        reply.m_recordId = msg.m_recordId;
        reply.m_code = code;
        return BuildCloudRecordCtlMsg(reply);
    }

    //暂停：录像 id 不在录时只记日志
    case cloudrecctl_op_pause:
        if (!m_recorder->pause(msg.m_recordId, now()))
            LOGW("cloud record control: pause ignored, record %llu is not recording",
                 static_cast<unsigned long long>(msg.m_recordId));
        return std::string();

    //继续：录像 id 不在录时只记日志
    case cloudrecctl_op_resume:
        if (!m_recorder->resume(msg.m_recordId, now()))
            LOGW("cloud record control: resume ignored, record %llu is not recording",
                 static_cast<unsigned long long>(msg.m_recordId));
        return std::string();

    //停止：在录时由写线程收尾后回报；已经结束的按文件现状立即回一条 finished
    case cloudrecctl_op_stop:
    {
        if (m_recorder->stop(msg.m_recordId, cloudrec_stop_requested) || m_recorder->hasRecord(msg.m_recordId))
            return std::string();
        reply.m_op = cloudrecctl_op_finished;
        reply.m_recordId = msg.m_recordId;
        reply.m_file = cloudrecord::FrecFileName(msg.m_recordId);
        if (m_recorder->statFile(reply.m_file, reply.m_bytes))
            reply.m_reason = cloudrec_stop_requested;
        else
            reply.m_reason = cloudrec_stop_write_error;
        LOGI("cloud record control: stop for finished record %llu, file bytes: %llu, reason: %d",
             static_cast<unsigned long long>(msg.m_recordId), static_cast<unsigned long long>(reply.m_bytes),
             reply.m_reason);
        return BuildCloudRecordCtlMsg(reply);
    }

    //删除文件：逐个删除，不在的文件与带路径的文件名由录制器拒绝并记日志
    case cloudrecctl_op_delete:
        for (size_t i = 0; i < msg.m_files.size(); ++i)
            m_recorder->deleteFile(msg.m_files[i]);
        return std::string();

    //探活
    case cloudrecctl_op_ping:
        reply.m_op = cloudrecctl_op_pong;
        return BuildCloudRecordCtlMsg(reply);

    //imgserver 发出的事件种类与不认识的 op（chatserver 版本更新）：忽略
    default:
        LOGW("cloud record control: unexpected op ignored: %s", CloudRecordCtlOpName(msg.m_op));
        return std::string();
    }
}

void CloudRecordControl::onCloudRecordFinished(const CloudRecordResult& result)
{
    CloudRecordCtlMsg msg;
    msg.m_op = cloudrecctl_op_finished;
    msg.m_recordId = result.m_recordId;
    msg.m_file = result.m_fileName;
    msg.m_bytes = result.m_bytes;
    msg.m_durationMs = result.m_durationMs;
    msg.m_frames = result.m_frames;
    msg.m_dropped = result.m_dropped;
    msg.m_reason = result.m_reason;
    emit(BuildCloudRecordCtlMsg(msg));
}

void CloudRecordControl::notifyUploaded(uint64_t recordId, const std::string& fileName, uint64_t bytes)
{
    CloudRecordCtlMsg msg;
    msg.m_op = cloudrecctl_op_uploaded;
    msg.m_recordId = recordId;
    msg.m_file = fileName;
    msg.m_bytes = bytes;
    LOGI("cloud record control: uploaded, record: %llu, file: %s, bytes: %llu",
         static_cast<unsigned long long>(recordId), fileName.c_str(), static_cast<unsigned long long>(bytes));
    emit(BuildCloudRecordCtlMsg(msg));
}
