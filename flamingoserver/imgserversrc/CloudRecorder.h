/**
 *  云端录制的录制器（imgserver），CloudRecorder.h
 *
 *  第 26 期（docs/屏幕共享与录制方案.md 第 12.6.2 节）：chatserver 经控制连接通知 imgserver 开始录某个屏幕共享房间后，
 *  本类按房间登记这次录制，ScreenShareRoomRelay 每转发一帧就交一份给本类（onFrame），由独立的写线程按 CloudRecordFile 的格式
 *  追加到 cloudrecorddir 下的「rec_<录像 id>.frec」。转发线程上只做入队，写盘慢时不拖住实时转发；队列积压超过上限时丢弃新帧
 *  并记日志（录像出现卡顿，好过拖慢所有观看者）。
 *  规则：
 *    - 同一房间同时只有一份录制；同一录像 id 不能重复开始；
 *    - 暂停期间收到的帧不写，暂停与继续各写一条记录，合成时据此去掉暂停的时段；
 *    - 开始时与录制中每写满 kDiskCheckBytes 字节检查一次磁盘剩余空间，低于配置的下限时停止（原因 disk_low）；
 *    - 停止时写线程把队列里剩下的写完再关文件，然后经 ICloudRecorderListener 回报大小、时长、帧数（在写线程上回调）。
 *  全部公开方法线程安全。单元测试见 tests/test_cloudrecord.cpp（Suite CREC）。
 *
 *  典型用法（imgserver 的控制连接处理里）：
 *      CloudRecorder& rec = Singleton<CloudRecorder>::Instance();
 *      rec.configure(dir, minFreeBytes, NULL);
 *      rec.setListener(&control);
 *      rec.start(recordId, roomId, nowMs, false, seats);
 *      ...ScreenShareRoomRelay::forward 里：rec.onFrame(roomId, track, plainBody, nowMs);
 *      rec.stop(recordId, cloudrec_stop_requested);
 *
 *  balloonwj@qq.com   2026-10-06
 */
#pragma once

#include <stdint.h>
#include <stdio.h>
#include <condition_variable>
#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

//录制结束的原因
enum cloudrec_stop_reason
{
    cloudrec_stop_requested   = 1,   //chatserver 要求停止（主持人停止、房间解散）
    cloudrec_stop_disk_low    = 2,   //磁盘剩余空间低于下限，自动停止并保存已录部分
    cloudrec_stop_write_error = 3,   //写文件失败（磁盘满、文件被删等），保存已录部分
    cloudrec_stop_shutdown    = 4,   //imgserver 退出，保存已录部分
};

//start 的结果
enum cloudrec_start_result
{
    cloudrec_start_ok          = 0,   //已开始
    cloudrec_start_busy        = 1,   //这个房间已经在录，或这个录像 id 已经在用
    cloudrec_start_disk_low    = 2,   //磁盘剩余空间低于下限，没有开始
    cloudrec_start_open_failed = 3,   //文件建不起来（目录不存在或没有写权限）
    cloudrec_start_bad_arg     = 4,   //参数不对（录像 id 为 0、房间号为空、没有配置目录）
};

//一次录制的结果
struct CloudRecordResult
{
    uint64_t    m_recordId;     //录像 id
    std::string m_roomId;       //房间号
    std::string m_fileName;     //文件名（不含目录），如 rec_42.frec
    uint64_t    m_bytes;        //文件字节数
    int64_t     m_durationMs;   //录到的时长（毫秒，不含暂停）
    uint64_t    m_frames;       //写进文件的帧数
    uint64_t    m_dropped;      //队列积压时丢弃的帧数
    int         m_reason;       //结束原因，取值见 cloudrec_stop_reason

    CloudRecordResult();
};

//录制结束的回报（在写线程上调用，实现方自行转到需要的线程）
class ICloudRecorderListener
{
public:
    virtual ~ICloudRecorderListener() {}

    //一次录制已结束、文件已关闭
    virtual void onCloudRecordFinished(const CloudRecordResult& result) = 0;
};

//云端录制的录制器；全局单例经 Singleton<CloudRecorder> 访问，测试里可直接构造
class CloudRecorder final
{
public:
    //写线程队列里允许积压的字节上限：超过时丢弃新帧（约 30 秒的 2 Mbps 画面）
    static const size_t kMaxQueuedBytes = 8U * 1024 * 1024;
    //录制中每写这么多字节检查一次磁盘剩余空间
    static const uint64_t kDiskCheckBytes = 64ULL * 1024 * 1024;

    /**
     *  取磁盘剩余空间的函数（测试可替换）。
     *  @param dir 目录
     *  @return 剩余字节数；取不到时返回 0
     */
    typedef uint64_t (*FreeSpaceFn)(const std::string& dir);

    CloudRecorder();
    ~CloudRecorder();

    CloudRecorder(const CloudRecorder&) = delete;
    CloudRecorder& operator=(const CloudRecorder&) = delete;

    /**
     *  设置运行参数（imgserver 启动时调用一次，之后可再调用修改）。
     *  @param dir          录像目录（cloudrecorddir，已确保存在，末尾带分隔符）
     *  @param minFreeBytes 磁盘剩余空间下限（字节）：开始时低于它拒绝，录制中低于它停止
     *  @param freeSpace    取剩余空间的函数；为 NULL 时用系统接口
     */
    void configure(const std::string& dir, uint64_t minFreeBytes, FreeSpaceFn freeSpace);

    //登记结束回报（不拥有）；传 NULL 取消
    void setListener(ICloudRecorderListener* listener);

    /**
     *  开始录一个房间。
     *  @param recordId    录像 id（>0）
     *  @param roomId      屏幕共享房间号
     *  @param startTimeMs 开始时刻，Unix 毫秒（写进文件头，也是记录相对时刻的起点）
     *  @param audioOnly   是否只录声音（第 53 条定为不提供，保留参数）
     *  @param seats       房间里此刻的座位归属（座位号、userid），各写一条座位记录
     *  @return 取值见 cloudrec_start_result
     */
    int start(uint64_t recordId, const std::string& roomId, int64_t startTimeMs, bool audioOnly,
              const std::vector<std::pair<int, int64_t> >& seats);

    //暂停或继续某次录制；录像 id 不在录时返回 false
    bool pause(uint64_t recordId, int64_t nowMs);
    bool resume(uint64_t recordId, int64_t nowMs);

    /**
     *  停止某次录制：写线程写完队列里剩下的帧后关文件并回报。
     *  @param recordId 录像 id
     *  @param reason   结束原因，取值见 cloudrec_stop_reason
     *  @return 录像 id 在录时返回 true
     */
    bool stop(uint64_t recordId, int reason);

    /**
     *  转发线程交来一帧（ScreenShareRoomRelay::forward 里调用，任意线程）。房间没有在录、暂停中或轨道不要时什么也不做。
     *  @param roomId    房间号
     *  @param track     轨道类型（帧头第 1 字节）
     *  @param plainBody 明文包体（11 字节帧头 + 数据）
     *  @param nowMs     当前时刻，Unix 毫秒
     */
    void onFrame(const std::string& roomId, int track, const std::string& plainBody, int64_t nowMs);

    //有人进入正在录的房间：写一条座位记录（房间没有在录时什么也不做）
    void onSeat(const std::string& roomId, int seat, int64_t userId, int64_t nowMs);

    //房间是否正在录（含暂停）；roomRecordId 返回录像 id
    bool isRecording(const std::string& roomId, uint64_t* roomRecordId = NULL) const;

    //某个录像 id 是否还在录或正在收尾（已要求停止、写线程还没关文件）
    bool hasRecord(uint64_t recordId) const;

    /**
     *  取录像目录里某个文件的大小（重复的停止命令按文件现状回报时用）。只接受不带路径分隔符的文件名。
     *  @param fileName 文件名
     *  @param bytes    [出参] 字节数
     *  @return 文件存在时返回 true
     */
    bool statFile(const std::string& fileName, uint64_t& bytes) const;

    /**
     *  删除录像目录里的一个文件（chatserver 在合成上传完成、录像删除或过期时要求）。只接受不带路径分隔符的文件名。
     *  @return 删掉了返回 true
     */
    bool deleteFile(const std::string& fileName);

    //停止全部录制并结束写线程（imgserver 退出时调用）；可重复调用
    void shutdown();

    //当前进行中的录制数（测试与日志用）
    size_t activeCount() const;

private:
    //一次进行中的录制
    struct Recording
    {
        uint64_t    m_recordId;     //录像 id
        std::string m_roomId;       //房间号
        std::string m_fileName;     //文件名
        int64_t     m_startTimeMs;  //开始时刻
        bool        m_audioOnly;    //是否只录声音
        bool        m_paused;       //是否暂停中
        int64_t     m_pausedAtMs;   //这次暂停开始的时刻
        int64_t     m_pausedTotalMs;//已暂停的总时长（毫秒）
        uint32_t    m_lastRelMs;    //最后一帧相对开始时刻的毫秒数（结束时据此算时长）
        uint64_t    m_frames;       //入队的帧数
        uint64_t    m_dropped;      //丢弃的帧数
        bool        m_stopping;     //已要求停止，等写线程收尾
        //以下只在写线程上读写（start 里建好文件之后才交给写线程）
        FILE*       m_file;         //录像文件；关闭后为 NULL
        uint64_t    m_bytesWritten; //已写入的字节数（含文件头）
        uint64_t    m_sinceDiskCheck;   //上次检查磁盘空间以来写入的字节数
        int         m_failedReason; //写入出错或磁盘不够时的结束原因；0 表示没有出错

        Recording();
    };

    //写线程的一项工作
    struct Job
    {
        uint64_t    m_recordId;   //录像 id
        std::string m_bytes;      //要追加的字节；关文件的工作为空
        bool        m_close;      //写完后关文件并回报
        int         m_reason;     //关文件时的结束原因

        Job();
    };

    void threadProc();
    //入队（调用方已持 m_mutex）；积压超过上限时返回 false
    bool enqueueLocked(const Job& job);
    uint64_t freeSpace() const;
    static uint32_t relMs(const Recording& rec, int64_t nowMs);

    mutable std::mutex                    m_mutex;        //保护以下全部成员
    std::condition_variable               m_cv;           //通知写线程
    std::string                           m_dir;          //录像目录
    uint64_t                              m_minFreeBytes; //磁盘剩余空间下限
    FreeSpaceFn                           m_freeSpace;    //取剩余空间
    ICloudRecorderListener*               m_listener;     //结束回报（不拥有）
    std::map<std::string, std::shared_ptr<Recording> > m_byRoom;   //房间号 → 进行中的录制
    std::map<uint64_t, std::shared_ptr<Recording> >    m_byId;     //录像 id → 进行中的录制（含正在收尾的）
    std::deque<Job>                       m_queue;        //写线程的工作队列
    size_t                                m_queuedBytes;  //队列里待写的字节数
    std::unique_ptr<std::thread>          m_thread;       //写线程；第一次开始录制时创建
    bool                                  m_quit;         //通知写线程退出
};
