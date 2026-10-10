/**
 *  云端录制的录制器，职责见 CloudRecorder.h。
 *  balloonwj@qq.com   2026-10-06
 */
#include "CloudRecorder.h"

#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#if defined(_WIN32)
#include <windows.h>
#else
#include <sys/statvfs.h>
#endif
#include "../base/AsyncLog.h"
#include "CloudRecordFile.h"

namespace {

//系统的磁盘剩余空间查询：目录所在的卷
uint64_t systemFreeSpace(const std::string& dir)
{
#if defined(_WIN32)
    ULARGE_INTEGER avail;
    avail.QuadPart = 0;
    if (!::GetDiskFreeSpaceExA(dir.c_str(), &avail, NULL, NULL))
        return 0;
    return avail.QuadPart;
#else
    struct statvfs st;
    if (statvfs(dir.c_str(), &st) != 0)
        return 0;
    return static_cast<uint64_t>(st.f_bavail) * static_cast<uint64_t>(st.f_frsize);
#endif
}

//文件名是否只是一个文件名（不带目录、不含上级目录写法），防止删到录像目录以外的文件
bool isPlainFileName(const std::string& name)
{
    if (name.empty() || name == "." || name == "..")
        return false;
    return name.find('/') == std::string::npos && name.find('\\') == std::string::npos &&
           name.find(':') == std::string::npos && name.find("..") == std::string::npos;
}

} // namespace

//类内初始化的静态常量的类外定义：被 ODR-use 时 g++ 要求有类外定义，MSVC 不要求
const size_t CloudRecorder::kMaxQueuedBytes;
const uint64_t CloudRecorder::kDiskCheckBytes;

CloudRecordResult::CloudRecordResult()
    : m_recordId(0)
    , m_bytes(0)
    , m_durationMs(0)
    , m_frames(0)
    , m_dropped(0)
    , m_reason(cloudrec_stop_requested)
{
}

CloudRecorder::Recording::Recording()
    : m_recordId(0)
    , m_startTimeMs(0)
    , m_audioOnly(false)
    , m_paused(false)
    , m_pausedAtMs(0)
    , m_pausedTotalMs(0)
    , m_lastRelMs(0)
    , m_frames(0)
    , m_dropped(0)
    , m_stopping(false)
    , m_file(NULL)
    , m_bytesWritten(0)
    , m_sinceDiskCheck(0)
    , m_failedReason(0)
{
}

CloudRecorder::Job::Job()
    : m_recordId(0)
    , m_close(false)
    , m_reason(cloudrec_stop_requested)
{
}

CloudRecorder::CloudRecorder()
    : m_minFreeBytes(0)
    , m_freeSpace(&systemFreeSpace)
    , m_listener(NULL)
    , m_queuedBytes(0)
    , m_quit(false)
{
}

CloudRecorder::~CloudRecorder()
{
    shutdown();
}

void CloudRecorder::configure(const std::string& dir, uint64_t minFreeBytes, FreeSpaceFn freeSpace)
{
    std::lock_guard<std::mutex> guard(m_mutex);
    m_dir = dir;
    m_minFreeBytes = minFreeBytes;
    m_freeSpace = (freeSpace != NULL) ? freeSpace : &systemFreeSpace;
}

void CloudRecorder::setListener(ICloudRecorderListener* listener)
{
    std::lock_guard<std::mutex> guard(m_mutex);
    m_listener = listener;
}

uint32_t CloudRecorder::relMs(const Recording& rec, int64_t nowMs)
{
    const int64_t d = nowMs - rec.m_startTimeMs;
    return d > 0 ? static_cast<uint32_t>(d) : 0;
}

int CloudRecorder::start(uint64_t recordId, const std::string& roomId, int64_t startTimeMs, bool audioOnly,
                         const std::vector<std::pair<int, int64_t> >& seats)
{
    std::lock_guard<std::mutex> guard(m_mutex);
    if (recordId == 0 || roomId.empty() || m_dir.empty())
        return cloudrec_start_bad_arg;
    if (m_byRoom.find(roomId) != m_byRoom.end() || m_byId.find(recordId) != m_byId.end())
        return cloudrec_start_busy;
    if (m_freeSpace(m_dir) < m_minFreeBytes)
    {
        LOGW("cloud record: not enough disk space to start, record: %llu, room: %s",
             static_cast<unsigned long long>(recordId), roomId.c_str());
        return cloudrec_start_disk_low;
    }
    std::shared_ptr<Recording> rec(new Recording());
    rec->m_recordId = recordId;
    rec->m_roomId = roomId;
    rec->m_fileName = cloudrecord::FrecFileName(recordId);
    rec->m_startTimeMs = startTimeMs;
    rec->m_audioOnly = audioOnly;
    //文件在这里建好并写入文件头：建不起来时当场返回，之后由写线程追加
    const std::string path = m_dir + rec->m_fileName;
    rec->m_file = fopen(path.c_str(), "wb");
    if (rec->m_file == NULL)
    {
        LOGE("cloud record: create file failed, record: %llu, file: %s", static_cast<unsigned long long>(recordId),
             path.c_str());
        return cloudrec_start_open_failed;
    }
    cloudrecord::FrecHeader header;
    header.m_flags = audioOnly ? cloudrecord::frec_flag_audio_only : 0;
    header.m_recordId = recordId;
    header.m_startTimeMs = startTimeMs;
    header.m_roomId = roomId;
    std::string head = cloudrecord::EncodeHeader(header);
    for (size_t i = 0; i < seats.size(); ++i)
        head += cloudrecord::EncodeRecord(cloudrecord::frec_record_seat, 0,
                                          cloudrecord::EncodeSeatPayload(seats[i].first, seats[i].second));
    if (fwrite(head.data(), 1, head.size(), rec->m_file) != head.size())
    {
        fclose(rec->m_file);
        rec->m_file = NULL;
        remove(path.c_str());
        return cloudrec_start_open_failed;
    }
    rec->m_bytesWritten = head.size();
    m_byRoom[roomId] = rec;
    m_byId[recordId] = rec;
    if (!m_thread)
    {
        m_quit = false;
        m_thread.reset(new std::thread(&CloudRecorder::threadProc, this));
    }
    LOGI("cloud record: started, record: %llu, room: %s, seats: %d, audio only: %d",
         static_cast<unsigned long long>(recordId), roomId.c_str(), (int)seats.size(), audioOnly ? 1 : 0);
    return cloudrec_start_ok;
}

bool CloudRecorder::enqueueLocked(const Job& job)
{
    //关文件的工作不受积压上限限制，否则录制永远收不了尾
    if (!job.m_close && m_queuedBytes + job.m_bytes.size() > kMaxQueuedBytes)
        return false;
    m_queuedBytes += job.m_bytes.size();
    m_queue.push_back(job);
    m_cv.notify_one();
    return true;
}

bool CloudRecorder::pause(uint64_t recordId, int64_t nowMs)
{
    std::lock_guard<std::mutex> guard(m_mutex);
    std::map<uint64_t, std::shared_ptr<Recording> >::iterator it = m_byId.find(recordId);
    if (it == m_byId.end() || it->second->m_stopping)
        return false;
    Recording& rec = *it->second;
    if (rec.m_paused)
        return true;
    rec.m_paused = true;
    rec.m_pausedAtMs = nowMs;
    Job job;
    job.m_recordId = recordId;
    job.m_bytes = cloudrecord::EncodeRecord(cloudrecord::frec_record_pause, relMs(rec, nowMs), std::string());
    enqueueLocked(job);
    LOGI("cloud record: paused, record: %llu", static_cast<unsigned long long>(recordId));
    return true;
}

bool CloudRecorder::resume(uint64_t recordId, int64_t nowMs)
{
    std::lock_guard<std::mutex> guard(m_mutex);
    std::map<uint64_t, std::shared_ptr<Recording> >::iterator it = m_byId.find(recordId);
    if (it == m_byId.end() || it->second->m_stopping)
        return false;
    Recording& rec = *it->second;
    if (!rec.m_paused)
        return true;
    rec.m_paused = false;
    if (nowMs > rec.m_pausedAtMs)
        rec.m_pausedTotalMs += nowMs - rec.m_pausedAtMs;
    Job job;
    job.m_recordId = recordId;
    job.m_bytes = cloudrecord::EncodeRecord(cloudrecord::frec_record_resume, relMs(rec, nowMs), std::string());
    enqueueLocked(job);
    LOGI("cloud record: resumed, record: %llu", static_cast<unsigned long long>(recordId));
    return true;
}

bool CloudRecorder::stop(uint64_t recordId, int reason)
{
    std::lock_guard<std::mutex> guard(m_mutex);
    std::map<uint64_t, std::shared_ptr<Recording> >::iterator it = m_byId.find(recordId);
    if (it == m_byId.end() || it->second->m_stopping)
        return false;
    Recording& rec = *it->second;
    rec.m_stopping = true;
    //从房间表里摘掉：之后这个房间的帧不再录
    std::map<std::string, std::shared_ptr<Recording> >::iterator room = m_byRoom.find(rec.m_roomId);
    if (room != m_byRoom.end() && room->second.get() == &rec)
        m_byRoom.erase(room);
    Job job;
    job.m_recordId = recordId;
    job.m_close = true;
    job.m_reason = reason;
    enqueueLocked(job);
    LOGI("cloud record: stop requested, record: %llu, reason: %d", static_cast<unsigned long long>(recordId), reason);
    return true;
}

void CloudRecorder::onFrame(const std::string& roomId, int track, const std::string& plainBody, int64_t nowMs)
{
    std::lock_guard<std::mutex> guard(m_mutex);
    std::map<std::string, std::shared_ptr<Recording> >::iterator it = m_byRoom.find(roomId);
    if (it == m_byRoom.end())
        return;
    Recording& rec = *it->second;
    if (rec.m_paused || rec.m_stopping || !cloudrecord::ShouldRecordTrack(track, rec.m_audioOnly))
        return;
    if (plainBody.size() > cloudrecord::kFrecMaxPayloadBytes)
        return;
    const uint32_t rel = relMs(rec, nowMs);
    Job job;
    job.m_recordId = rec.m_recordId;
    job.m_bytes = cloudrecord::EncodeRecord(cloudrecord::frec_record_frame, rel, plainBody);
    if (!enqueueLocked(job))
    {
        //积压：丢掉这一帧，每 100 帧记一次日志，免得日志刷屏
        const uint64_t kDropLogEvery = 100;
        if (rec.m_dropped % kDropLogEvery == 0)
            LOGW("cloud record: write queue full, dropping frames, record: %llu, dropped: %llu",
                 static_cast<unsigned long long>(rec.m_recordId), static_cast<unsigned long long>(rec.m_dropped + 1));
        ++rec.m_dropped;
        return;
    }
    ++rec.m_frames;
    rec.m_lastRelMs = rel;
}

void CloudRecorder::onSeat(const std::string& roomId, int seat, int64_t userId, int64_t nowMs)
{
    std::lock_guard<std::mutex> guard(m_mutex);
    std::map<std::string, std::shared_ptr<Recording> >::iterator it = m_byRoom.find(roomId);
    if (it == m_byRoom.end() || it->second->m_stopping)
        return;
    Job job;
    job.m_recordId = it->second->m_recordId;
    job.m_bytes = cloudrecord::EncodeRecord(cloudrecord::frec_record_seat, relMs(*it->second, nowMs),
                                            cloudrecord::EncodeSeatPayload(seat, userId));
    enqueueLocked(job);
}

bool CloudRecorder::isRecording(const std::string& roomId, uint64_t* roomRecordId) const
{
    std::lock_guard<std::mutex> guard(m_mutex);
    std::map<std::string, std::shared_ptr<Recording> >::const_iterator it = m_byRoom.find(roomId);
    if (it == m_byRoom.end())
        return false;
    if (roomRecordId != NULL)
        *roomRecordId = it->second->m_recordId;
    return true;
}

bool CloudRecorder::deleteFile(const std::string& fileName)
{
    std::string dir;
    {
        std::lock_guard<std::mutex> guard(m_mutex);
        dir = m_dir;
    }
    if (dir.empty() || !isPlainFileName(fileName))
    {
        LOGW("cloud record: refuse to delete file, name: %s", fileName.c_str());
        return false;
    }
    const bool ok = (remove((dir + fileName).c_str()) == 0);
    LOGI("cloud record: delete file %s, ok: %d", fileName.c_str(), ok ? 1 : 0);
    return ok;
}

bool CloudRecorder::hasRecord(uint64_t recordId) const
{
    std::lock_guard<std::mutex> guard(m_mutex);
    return m_byId.find(recordId) != m_byId.end();
}

bool CloudRecorder::statFile(const std::string& fileName, uint64_t& bytes) const
{
    bytes = 0;
    std::string dir;
    {
        std::lock_guard<std::mutex> guard(m_mutex);
        dir = m_dir;
    }
    if (dir.empty() || !isPlainFileName(fileName))
        return false;
    const std::string path = dir + fileName;
#if defined(_WIN32)
    struct _stat64 st;
    if (_stat64(path.c_str(), &st) != 0)
        return false;
#else
    struct stat st;
    if (stat(path.c_str(), &st) != 0)
        return false;
#endif
    bytes = static_cast<uint64_t>(st.st_size);
    return true;
}

size_t CloudRecorder::activeCount() const
{
    std::lock_guard<std::mutex> guard(m_mutex);
    return m_byId.size();
}

uint64_t CloudRecorder::freeSpace() const
{
    return m_freeSpace(m_dir);
}

void CloudRecorder::shutdown()
{
    {
        std::lock_guard<std::mutex> guard(m_mutex);
        std::vector<uint64_t> ids;
        for (std::map<uint64_t, std::shared_ptr<Recording> >::iterator it = m_byId.begin(); it != m_byId.end(); ++it)
        {
            if (!it->second->m_stopping)
                ids.push_back(it->first);
        }
        for (size_t i = 0; i < ids.size(); ++i)
        {
            Recording& rec = *m_byId[ids[i]];
            rec.m_stopping = true;
            m_byRoom.erase(rec.m_roomId);
            Job job;
            job.m_recordId = ids[i];
            job.m_close = true;
            job.m_reason = cloudrec_stop_shutdown;
            enqueueLocked(job);
        }
        m_quit = true;
        m_cv.notify_one();
    }
    if (m_thread)
    {
        if (m_thread->joinable())
            m_thread->join();
        m_thread.reset();
    }
}

void CloudRecorder::threadProc()
{
    for (;;)
    {
        Job job;
        std::shared_ptr<Recording> rec;
        ICloudRecorderListener* listener = NULL;
        {
            std::unique_lock<std::mutex> lock(m_mutex);
            while (m_queue.empty() && !m_quit)
                m_cv.wait(lock);
            if (m_queue.empty() && m_quit)
                return;
            job = m_queue.front();
            m_queue.pop_front();
            m_queuedBytes -= job.m_bytes.size();
            std::map<uint64_t, std::shared_ptr<Recording> >::iterator it = m_byId.find(job.m_recordId);
            if (it == m_byId.end())
                continue;
            rec = it->second;
            listener = m_listener;
        }

        //写盘在锁外进行：m_file、m_bytesWritten、m_sinceDiskCheck、m_failedReason 只在本线程读写
        if (!job.m_bytes.empty() && rec->m_file != NULL && rec->m_failedReason == 0)
        {
            if (fwrite(job.m_bytes.data(), 1, job.m_bytes.size(), rec->m_file) != job.m_bytes.size())
            {
                LOGE("cloud record: write failed, record: %llu", static_cast<unsigned long long>(rec->m_recordId));
                rec->m_failedReason = cloudrec_stop_write_error;
            }
            else
            {
                rec->m_bytesWritten += job.m_bytes.size();
                rec->m_sinceDiskCheck += job.m_bytes.size();
                if (rec->m_sinceDiskCheck >= kDiskCheckBytes)
                {
                    rec->m_sinceDiskCheck = 0;
                    uint64_t minFree = 0;
                    {
                        std::lock_guard<std::mutex> guard(m_mutex);
                        minFree = m_minFreeBytes;
                    }
                    if (freeSpace() < minFree)
                    {
                        LOGW("cloud record: disk space low, stopping, record: %llu",
                             static_cast<unsigned long long>(rec->m_recordId));
                        rec->m_failedReason = cloudrec_stop_disk_low;
                    }
                }
            }
            //出错或磁盘不够：自动停止（排一个关文件的工作，之后这一次录制的帧不再写）
            if (rec->m_failedReason != 0)
                stop(rec->m_recordId, rec->m_failedReason);
        }

        if (!job.m_close)
            continue;
        if (rec->m_file != NULL)
        {
            fflush(rec->m_file);
            fclose(rec->m_file);
            rec->m_file = NULL;
        }
        CloudRecordResult result;
        {
            std::lock_guard<std::mutex> guard(m_mutex);
            result.m_recordId = rec->m_recordId;
            result.m_roomId = rec->m_roomId;
            result.m_fileName = rec->m_fileName;
            result.m_bytes = rec->m_bytesWritten;
            const int64_t duration = static_cast<int64_t>(rec->m_lastRelMs) - rec->m_pausedTotalMs;
            result.m_durationMs = duration > 0 ? duration : 0;
            result.m_frames = rec->m_frames;
            result.m_dropped = rec->m_dropped;
            result.m_reason = (rec->m_failedReason != 0) ? rec->m_failedReason : job.m_reason;
            m_byId.erase(rec->m_recordId);
        }
        LOGI("cloud record: finished, record: %llu, bytes: %llu, duration: %lld ms, frames: %llu, dropped: %llu, "
             "reason: %d", static_cast<unsigned long long>(result.m_recordId),
             static_cast<unsigned long long>(result.m_bytes), static_cast<long long>(result.m_durationMs),
             static_cast<unsigned long long>(result.m_frames), static_cast<unsigned long long>(result.m_dropped),
             result.m_reason);
        if (listener != NULL)
            listener->onCloudRecordFinished(result);
    }
}
