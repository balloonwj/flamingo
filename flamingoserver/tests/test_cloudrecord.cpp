/**
 *  云端录制的原始帧文件格式与录制器单元测试（Suite CREC），test_cloudrecord.cpp
 *
 *  覆盖第 26 期（docs/屏幕共享与录制方案.md 第 12.6.2 节）imgserver 一侧：
 *    - CREC1 文件头与记录的编码、解码往返；座位记录的负载；文件名 rec_<id>.frec；
 *    - CREC2 末尾残缺（进程中途退出）时读到最后一条完整的记录为止；标识、版本不对时文件头读不出；负载长度超过上限视为损坏；
 *    - CREC3 哪些轨道要写：录像要屏幕、摄像头、声音、标注、控制帧，只录声音时只要麦克风与电脑声音；
 *    - CREC4 录制器：开始时写文件头与座位记录；帧、座位、暂停、继续按顺序写入；暂停期间的帧不写；停止后回报大小、时长、帧数，
 *            文件能按格式完整读回；
 *    - CREC5 开始的各种失败：参数不对、同一房间或同一录像 id 重复、磁盘剩余空间不够；
 *    - CREC6 录制中磁盘空间低于下限：自动停止、原因为 disk_low，已写部分保留；
 *    - CREC7 删除文件只接受录像目录里的文件名，带目录、上级目录写法的一律拒绝；
 *    - CREC8 imgserver 退出：进行中的录制以 shutdown 原因收尾；
 *    - CRCM1~2 控制连接消息（utils/CloudRecordCtlMsg）：各种 op 的往返，格式不对的消息被拒，不认识的 op 解析为 unknown；
 *    - CRCT1~4 imgserver 一侧的控制（CloudRecordControl）：开始 / 暂停 / 继续 / 停止交给录制器，结束回报经控制连接发出；
 *            没有控制连接时事件暂存、报到后补发、超出上限丢最早的；旧连接断开不影响新连接；重复的停止按文件现状回报；删除文件。
 *    - CRHS1~2 服务端之间的加密握手（net/FrameCrypto 的 PeerClientHandshake，chatserver 连 imgserver 时用）：与 ServerHandshake
 *            对接后两个方向的帧都能解开；对端身份公钥不对、对端拒绝握手、公钥缺失时失败；
 *    - CRSC1 图片端口上的云端录像场景（fileserversrc/CloudRecordScene）：没设录像目录时不支持；令牌键解析成目录与文件名，
 *            不是录像文件名的键一律拒绝；同名同大小的文件算已传完；上传完成回调。
 *  录制器用例在当前目录写 rec_<id>.frec 临时文件，用完删除。每条用例构造自己的 CloudRecorder 实例。
 *
 *  典型用法：Tests.exe --test-case="CREC*"
 *
 *  balloonwj@qq.com   2026-10-06
 */
#include <stdio.h>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "doctest.h"
#include "../chatserversrc/Protocol.h"
#include "../net/FrameCrypto.h"
#include "../net/FramePayload.h"
#include "../fileserversrc/CloudRecordScene.h"
#include "../imgserversrc/CloudRecordFile.h"
#include "../imgserversrc/CloudRecordControl.h"
#include "../imgserversrc/CloudRecorder.h"
#include "../utils/CloudRecordCtlMsg.h"

using namespace cloudrecord;

namespace {

//测试用的开始时刻，Unix 毫秒
const int64_t kCrecT0 = 1790000000000LL;
//回报等待的上限（毫秒）
const int kCrecWaitMs = 5000;
//「磁盘剩余空间」的两档：够与不够（字节）
const uint64_t kCrecPlenty = 100ULL * 1024 * 1024 * 1024;
const uint64_t kCrecMinFree = 1024ULL * 1024 * 1024;
//CREC6 推帧的时长上限（毫秒）：写线程按磁盘速度写，写满一个检查间隔（64 MiB）才会检查剩余空间。2026-10-08 起改为推到
//录制停止为止、最多这么久，而不是推固定块数：WSL 下 build-linux 所在的 /mnt/e 实测写入只有约 5.6 MB/s，固定块数推完时
//写进文件的量还不够一个检查间隔（其余帧因积压被丢弃），录制不会停止，用例超时失败
const int kCrec6PushBudgetMs = 60000;
//CREC6 的录像目录：Linux 上写 /tmp（本机磁盘），Windows 上写当前目录
#ifdef WIN32
const char* const kCrec6Dir = "./";
#else
const char* const kCrec6Dir = "/tmp/";
#endif

//假的磁盘剩余空间；写线程也会读，所以用原子量
std::atomic<uint64_t> g_crecFree(kCrecPlenty);

uint64_t fakeFreeSpace(const std::string& /*dir*/)
{
    return g_crecFree.load();
}

//一帧房间中转帧的明文包体：11 字节帧头（轨道、座位、标志、8 字节时间戳）加数据
std::string makeFrame(int track, int seat, const std::string& data)
{
    std::string body;
    body.push_back(static_cast<char>(track));
    body.push_back(static_cast<char>(seat));
    body.push_back(0);
    body.append(8, '\0');
    body += data;
    return body;
}

std::string readWhole(const std::string& path)
{
    std::string out;
    FILE* f = fopen(path.c_str(), "rb");
    if (f == NULL)
        return out;
    char buf[4096];
    size_t n = 0;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0)
        out.append(buf, n);
    fclose(f);
    return out;
}

//收录制结束回报
class CrecListener : public ICloudRecorderListener
{
public:
    CrecListener()
        : m_count(0)
    {
    }

    virtual void onCloudRecordFinished(const CloudRecordResult& result) override
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_results.push_back(result);
        ++m_count;
        m_cv.notify_all();
    }

    bool waitFor(int count)
    {
        std::unique_lock<std::mutex> lock(m_mutex);
        const std::chrono::steady_clock::time_point deadline =
            std::chrono::steady_clock::now() + std::chrono::milliseconds(kCrecWaitMs);
        while (m_count < count)
        {
            if (m_cv.wait_until(lock, deadline) == std::cv_status::timeout)
                break;
        }
        return m_count >= count;
    }

    std::mutex                     m_mutex;     //保护下面两项
    std::condition_variable        m_cv;        //回报到达的通知
    std::vector<CloudRecordResult> m_results;   //收到的回报
    int                            m_count;     //收到的回报数
};

} // namespace

TEST_CASE("CREC1 frec encode and decode")
{
    FrecHeader h;
    h.m_recordId = 42;
    h.m_startTimeMs = kCrecT0;
    h.m_roomId = "ss-5127-1000-1";
    std::string data = EncodeHeader(h);
    data += EncodeRecord(frec_record_seat, 0, EncodeSeatPayload(3, 7001));
    data += EncodeRecord(frec_record_frame, 1234, makeFrame(screenshare_track_screen, 0, "abc"));
    data += EncodeRecord(frec_record_pause, 2000, std::string());

    size_t pos = 0;
    FrecHeader back;
    REQUIRE(DecodeHeader(data, pos, back));
    CHECK(back.m_version == kFrecVersion);
    CHECK(back.m_flags == 0);
    CHECK(back.m_recordId == 42);
    CHECK(back.m_startTimeMs == kCrecT0);
    CHECK(back.m_roomId == "ss-5127-1000-1");
    uint8_t type = 0;
    uint32_t relMs = 0;
    std::string payload;
    REQUIRE(DecodeRecord(data, pos, type, relMs, payload));
    CHECK(type == frec_record_seat);
    int seat = 0;
    int64_t userId = 0;
    REQUIRE(DecodeSeatPayload(payload, seat, userId));
    CHECK(seat == 3);
    CHECK(userId == 7001);
    REQUIRE(DecodeRecord(data, pos, type, relMs, payload));
    CHECK(type == frec_record_frame);
    CHECK(relMs == 1234);
    CHECK(payload == makeFrame(screenshare_track_screen, 0, "abc"));
    REQUIRE(DecodeRecord(data, pos, type, relMs, payload));
    CHECK(type == frec_record_pause);
    CHECK(payload.empty());
    CHECK(pos == data.size());
    CHECK_FALSE(DecodeRecord(data, pos, type, relMs, payload));
    CHECK(FrecFileName(42) == "rec_42.frec");
    CHECK_FALSE(DecodeSeatPayload("abc", seat, userId));
}

TEST_CASE("CREC2 truncated and corrupt files")
{
    FrecHeader h;
    h.m_recordId = 7;
    h.m_roomId = "r";
    std::string data = EncodeHeader(h);
    const std::string full = EncodeRecord(frec_record_frame, 10, makeFrame(screenshare_track_mic, 1, "voice"));
    data += full;
    data += full.substr(0, full.size() - 3);   //末尾残缺
    size_t pos = 0;
    FrecHeader back;
    REQUIRE(DecodeHeader(data, pos, back));
    uint8_t type = 0;
    uint32_t relMs = 0;
    std::string payload;
    CHECK(DecodeRecord(data, pos, type, relMs, payload));
    const size_t before = pos;
    CHECK_FALSE(DecodeRecord(data, pos, type, relMs, payload));
    CHECK(pos == before);
    //标识不对、版本不对
    std::string badMagic = EncodeHeader(h);
    badMagic[0] = 'X';
    pos = 0;
    CHECK_FALSE(DecodeHeader(badMagic, pos, back));
    std::string badVersion = EncodeHeader(h);
    badVersion[4] = 9;
    pos = 0;
    CHECK_FALSE(DecodeHeader(badVersion, pos, back));
    //负载长度超过上限：视为损坏
    std::string huge = EncodeRecord(frec_record_frame, 0, std::string());
    huge[5] = static_cast<char>(0xFF);
    huge[6] = static_cast<char>(0xFF);
    huge[7] = static_cast<char>(0xFF);
    huge[8] = static_cast<char>(0x7F);
    pos = 0;
    CHECK_FALSE(DecodeRecord(huge, pos, type, relMs, payload));
}

TEST_CASE("CREC3 which tracks are recorded")
{
    CHECK(ShouldRecordTrack(screenshare_track_screen, false));
    CHECK(ShouldRecordTrack(screenshare_track_camera, false));
    CHECK(ShouldRecordTrack(screenshare_track_mic, false));
    CHECK(ShouldRecordTrack(screenshare_track_system_audio, false));
    CHECK(ShouldRecordTrack(screenshare_track_annotation, false));
    CHECK(ShouldRecordTrack(screenshare_track_control, false));
    CHECK_FALSE(ShouldRecordTrack(0, false));
    CHECK_FALSE(ShouldRecordTrack(7, false));
    CHECK_FALSE(ShouldRecordTrack(screenshare_track_screen, true));
    CHECK_FALSE(ShouldRecordTrack(screenshare_track_annotation, true));
    CHECK(ShouldRecordTrack(screenshare_track_mic, true));
    CHECK(ShouldRecordTrack(screenshare_track_system_audio, true));
}

TEST_CASE("CREC4 recorder writes frames, seats and pauses")
{
    g_crecFree = kCrecPlenty;
    CrecListener listener;
    CloudRecorder rec;
    rec.configure("./", kCrecMinFree, &fakeFreeSpace);
    rec.setListener(&listener);
    const uint64_t id = 900001;
    std::vector<std::pair<int, int64_t> > seats;
    seats.push_back(std::make_pair(0, 5127));
    seats.push_back(std::make_pair(1, 5308));
    REQUIRE(rec.start(id, "room-crec4", kCrecT0, false, seats) == cloudrec_start_ok);
    uint64_t recordId = 0;
    CHECK(rec.isRecording("room-crec4", &recordId));
    CHECK(recordId == id);
    //别的房间的帧不写
    rec.onFrame("room-other", screenshare_track_screen, makeFrame(screenshare_track_screen, 0, "x"), kCrecT0 + 5);
    rec.onFrame("room-crec4", screenshare_track_screen, makeFrame(screenshare_track_screen, 0, "frame-1"), kCrecT0 + 100);
    rec.onSeat("room-crec4", 2, 7001, kCrecT0 + 150);
    CHECK(rec.pause(id, kCrecT0 + 200));
    //暂停期间的帧不写
    rec.onFrame("room-crec4", screenshare_track_mic, makeFrame(screenshare_track_mic, 1, "paused"), kCrecT0 + 300);
    CHECK(rec.resume(id, kCrecT0 + 700));
    rec.onFrame("room-crec4", screenshare_track_mic, makeFrame(screenshare_track_mic, 1, "voice"), kCrecT0 + 900);
    REQUIRE(rec.stop(id, cloudrec_stop_requested));
    //停止之后的帧不写，重复停止返回 false
    rec.onFrame("room-crec4", screenshare_track_screen, makeFrame(screenshare_track_screen, 0, "late"), kCrecT0 + 950);
    CHECK_FALSE(rec.stop(id, cloudrec_stop_requested));
    REQUIRE(listener.waitFor(1));
    const CloudRecordResult r = listener.m_results[0];
    CHECK(r.m_recordId == id);
    CHECK(r.m_roomId == "room-crec4");
    CHECK(r.m_fileName == "rec_900001.frec");
    CHECK(r.m_frames == 2);
    CHECK(r.m_durationMs == 900 - 500);
    CHECK(r.m_reason == cloudrec_stop_requested);
    CHECK_FALSE(rec.isRecording("room-crec4"));
    CHECK(rec.activeCount() == 0);

    const std::string data = readWhole("./rec_900001.frec");
    CHECK(r.m_bytes == data.size());
    size_t pos = 0;
    FrecHeader h;
    REQUIRE(DecodeHeader(data, pos, h));
    CHECK(h.m_recordId == id);
    CHECK(h.m_roomId == "room-crec4");
    std::vector<uint8_t> types;
    std::vector<uint32_t> times;
    std::vector<std::string> payloads;
    uint8_t type = 0;
    uint32_t relMs = 0;
    std::string payload;
    while (DecodeRecord(data, pos, type, relMs, payload))
    {
        types.push_back(type);
        times.push_back(relMs);
        payloads.push_back(payload);
    }
    CHECK(pos == data.size());
    REQUIRE(types.size() == 7);
    CHECK(types[0] == frec_record_seat);
    CHECK(types[1] == frec_record_seat);
    CHECK(types[2] == frec_record_frame);
    CHECK(times[2] == 100);
    CHECK(payloads[2] == makeFrame(screenshare_track_screen, 0, "frame-1"));
    CHECK(types[3] == frec_record_seat);
    CHECK(times[3] == 150);
    CHECK(types[4] == frec_record_pause);
    CHECK(times[4] == 200);
    CHECK(types[5] == frec_record_resume);
    CHECK(times[5] == 700);
    CHECK(types[6] == frec_record_frame);
    CHECK(payloads[6] == makeFrame(screenshare_track_mic, 1, "voice"));
    CHECK(rec.deleteFile("rec_900001.frec"));
    CHECK_FALSE(rec.deleteFile("rec_900001.frec"));
}

TEST_CASE("CREC5 start failures")
{
    g_crecFree = kCrecPlenty;
    CloudRecorder rec;
    const std::vector<std::pair<int, int64_t> > none;
    //没有配置目录
    CHECK(rec.start(900010, "room-a", kCrecT0, false, none) == cloudrec_start_bad_arg);
    rec.configure("./", kCrecMinFree, &fakeFreeSpace);
    CHECK(rec.start(0, "room-a", kCrecT0, false, none) == cloudrec_start_bad_arg);
    CHECK(rec.start(900010, "", kCrecT0, false, none) == cloudrec_start_bad_arg);
    REQUIRE(rec.start(900010, "room-a", kCrecT0, false, none) == cloudrec_start_ok);
    //同一房间、同一录像 id 重复
    CHECK(rec.start(900011, "room-a", kCrecT0, false, none) == cloudrec_start_busy);
    CHECK(rec.start(900010, "room-b", kCrecT0, false, none) == cloudrec_start_busy);
    //磁盘剩余空间不够
    g_crecFree = kCrecMinFree - 1;
    CHECK(rec.start(900012, "room-c", kCrecT0, false, none) == cloudrec_start_disk_low);
    g_crecFree = kCrecPlenty;
    CHECK(rec.stop(900010, cloudrec_stop_requested));
    rec.shutdown();
    CHECK(rec.deleteFile("rec_900010.frec"));
}

TEST_CASE("CREC6 low disk space stops the recording")
{
    g_crecFree = kCrecPlenty;
    CrecListener listener;
    CloudRecorder rec;
    rec.configure(kCrec6Dir, kCrecMinFree, &fakeFreeSpace);
    rec.setListener(&listener);
    const uint64_t id = 900020;
    REQUIRE(rec.start(id, "room-disk", kCrecT0, false, std::vector<std::pair<int, int64_t> >()) == cloudrec_start_ok);
    rec.onFrame("room-disk", screenshare_track_screen, makeFrame(screenshare_track_screen, 0, "before"), kCrecT0 + 10);
    //之后磁盘变满：写满一次检查间隔后自动停止
    g_crecFree = 0;
    const std::string big(static_cast<size_t>(1024 * 1024), 'v');
    //队列积压时帧会被丢弃、不计入写入量，写进文件的量取决于磁盘速度：一直推到录制停止，最多 kCrec6PushBudgetMs
    const std::chrono::steady_clock::time_point deadline =
        std::chrono::steady_clock::now() + std::chrono::milliseconds(kCrec6PushBudgetMs);
    for (int i = 0; rec.isRecording("room-disk") && std::chrono::steady_clock::now() < deadline; ++i)
    {
        rec.onFrame("room-disk", screenshare_track_screen, makeFrame(screenshare_track_screen, 0, big), kCrecT0 + 20 + i);
        //让写线程跟上，减少积压丢帧
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    REQUIRE(listener.waitFor(1));
    CHECK(listener.m_results[0].m_reason == cloudrec_stop_disk_low);
    CHECK(listener.m_results[0].m_bytes > 0);
    CHECK_FALSE(rec.isRecording("room-disk"));
    g_crecFree = kCrecPlenty;
    CHECK(rec.deleteFile("rec_900020.frec"));
}

TEST_CASE("CREC7 delete only plain file names")
{
    CloudRecorder rec;
    rec.configure("./", 0, &fakeFreeSpace);
    CHECK_FALSE(rec.deleteFile(""));
    CHECK_FALSE(rec.deleteFile(".."));
    CHECK_FALSE(rec.deleteFile("../etc/imgserver.conf"));
    CHECK_FALSE(rec.deleteFile("a/b.frec"));
    CHECK_FALSE(rec.deleteFile("a\\b.frec"));
    CHECK_FALSE(rec.deleteFile("C:x.frec"));
    CHECK_FALSE(rec.deleteFile("rec_not_there.frec"));
}

TEST_CASE("CREC8 shutdown finishes active recordings")
{
    g_crecFree = kCrecPlenty;
    CrecListener listener;
    CloudRecorder rec;
    rec.configure("./", kCrecMinFree, &fakeFreeSpace);
    rec.setListener(&listener);
    REQUIRE(rec.start(900030, "room-x", kCrecT0, false, std::vector<std::pair<int, int64_t> >()) == cloudrec_start_ok);
    REQUIRE(rec.start(900031, "room-y", kCrecT0, false, std::vector<std::pair<int, int64_t> >()) == cloudrec_start_ok);
    rec.shutdown();
    REQUIRE(listener.waitFor(2));
    CHECK(listener.m_results[0].m_reason == cloudrec_stop_shutdown);
    CHECK(listener.m_results[1].m_reason == cloudrec_stop_shutdown);
    CHECK(rec.activeCount() == 0);
    CHECK(rec.deleteFile("rec_900030.frec"));
    CHECK(rec.deleteFile("rec_900031.frec"));
}

namespace {

//假的控制连接：记下发出的消息
class CrecSink : public ICloudRecordCtlSink
{
public:
    virtual void sendCloudRecordCtl(const std::string& body) override
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_sent.push_back(body);
        m_cv.notify_all();
    }

    bool waitFor(size_t count)
    {
        std::unique_lock<std::mutex> lock(m_mutex);
        const std::chrono::steady_clock::time_point deadline =
            std::chrono::steady_clock::now() + std::chrono::milliseconds(kCrecWaitMs);
        while (m_sent.size() < count)
        {
            if (m_cv.wait_until(lock, deadline) == std::cv_status::timeout)
                break;
        }
        return m_sent.size() >= count;
    }

    std::vector<std::string> sent()
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_sent;
    }

private:
    std::mutex               m_mutex;   //保护 m_sent
    std::condition_variable  m_cv;      //有新消息时通知
    std::vector<std::string> m_sent;    //发出的消息
};

//测试用的时钟（Unix 毫秒）
int64_t g_crecClock = kCrecT0;

int64_t fakeClock()
{
    return g_crecClock;
}

//供 CloudRecordControl 开始录制的入口：直接交给录制器，座位为空
class CrecStarter
{
public:
    explicit CrecStarter(CloudRecorder* recorder)
        : m_recorder(recorder)
        , m_calls(0)
    {
    }

    int start(uint64_t recordId, const std::string& roomId)
    {
        ++m_calls;
        return m_recorder->start(recordId, roomId, g_crecClock, false, std::vector<std::pair<int, int64_t> >());
    }

    CloudRecorder* m_recorder;   //录制器（不拥有）
    int            m_calls;      //被调用的次数
};

CloudRecordCtlMsg crecCmd(int op, uint64_t recordId)
{
    CloudRecordCtlMsg m;
    m.m_op = op;
    m.m_recordId = recordId;
    return m;
}

CloudRecordCtlMsg crecParse(const std::string& body)
{
    CloudRecordCtlMsg m;
    REQUIRE(ParseCloudRecordCtlMsg(body, m));
    return m;
}

} // namespace

TEST_CASE("CRCM1 control messages round trip")
{
    CloudRecordCtlMsg start = crecCmd(cloudrecctl_op_start, 18446744073709551615ULL);
    start.m_roomId = "ss-5127-1000-1";
    CloudRecordCtlMsg back = crecParse(BuildCloudRecordCtlMsg(start));
    CHECK(back.m_op == cloudrecctl_op_start);
    CHECK(back.m_recordId == 18446744073709551615ULL);
    CHECK(back.m_roomId == "ss-5127-1000-1");

    const int simpleOps[] = { cloudrecctl_op_pause, cloudrecctl_op_resume, cloudrecctl_op_stop };
    for (size_t i = 0; i < sizeof(simpleOps) / sizeof(simpleOps[0]); ++i)
    {
        back = crecParse(BuildCloudRecordCtlMsg(crecCmd(simpleOps[i], 77)));
        CHECK(back.m_op == simpleOps[i]);
        CHECK(back.m_recordId == 77);
    }

    CloudRecordCtlMsg del;
    del.m_op = cloudrecctl_op_delete;
    del.m_files.push_back("rec_1.frec");
    del.m_files.push_back("rec_1.mp4");
    back = crecParse(BuildCloudRecordCtlMsg(del));
    CHECK(back.m_op == cloudrecctl_op_delete);
    REQUIRE(back.m_files.size() == 2);
    CHECK(back.m_files[1] == "rec_1.mp4");

    CloudRecordCtlMsg started = crecCmd(cloudrecctl_op_started, 9);
    started.m_code = cloudrec_start_disk_low;
    back = crecParse(BuildCloudRecordCtlMsg(started));
    CHECK(back.m_op == cloudrecctl_op_started);
    CHECK(back.m_code == cloudrec_start_disk_low);

    CloudRecordCtlMsg fin = crecCmd(cloudrecctl_op_finished, 9);
    fin.m_file = "rec_9.frec";
    fin.m_bytes = 5000000000ULL;
    fin.m_durationMs = 3600000;
    fin.m_frames = 123456;
    fin.m_dropped = 3;
    fin.m_reason = cloudrec_stop_disk_low;
    back = crecParse(BuildCloudRecordCtlMsg(fin));
    CHECK(back.m_op == cloudrecctl_op_finished);
    CHECK(back.m_file == "rec_9.frec");
    CHECK(back.m_bytes == 5000000000ULL);
    CHECK(back.m_durationMs == 3600000);
    CHECK(back.m_frames == 123456);
    CHECK(back.m_dropped == 3);
    CHECK(back.m_reason == cloudrec_stop_disk_low);

    CloudRecordCtlMsg up = crecCmd(cloudrecctl_op_uploaded, 9);
    up.m_file = "rec_9.mp4";
    up.m_bytes = 42;
    back = crecParse(BuildCloudRecordCtlMsg(up));
    CHECK(back.m_op == cloudrecctl_op_uploaded);
    CHECK(back.m_file == "rec_9.mp4");
    CHECK(back.m_bytes == 42);

    CHECK(crecParse(BuildCloudRecordCtlMsg(crecCmd(cloudrecctl_op_ping, 0))).m_op == cloudrecctl_op_ping);
    CHECK(crecParse(BuildCloudRecordCtlMsg(crecCmd(cloudrecctl_op_pong, 0))).m_op == cloudrecctl_op_pong);
    CHECK(BuildCloudRecordCtlMsg(CloudRecordCtlMsg()).empty());
    CHECK(std::string(CloudRecordCtlOpName(cloudrecctl_op_finished)) == "finished");
}

TEST_CASE("CRCM2 bad control messages")
{
    CloudRecordCtlMsg m;
    CHECK_FALSE(ParseCloudRecordCtlMsg("", m));
    CHECK_FALSE(ParseCloudRecordCtlMsg("[1,2]", m));
    CHECK_FALSE(ParseCloudRecordCtlMsg("{\"recordid\":1}", m));
    CHECK_FALSE(ParseCloudRecordCtlMsg("{\"op\":\"start\",\"recordid\":1}", m));
    CHECK_FALSE(ParseCloudRecordCtlMsg("{\"op\":\"start\",\"recordid\":0,\"roomid\":\"r\"}", m));
    CHECK_FALSE(ParseCloudRecordCtlMsg("{\"op\":\"start\",\"recordid\":-1,\"roomid\":\"r\"}", m));
    CHECK_FALSE(ParseCloudRecordCtlMsg("{\"op\":\"start\",\"recordid\":\"1\",\"roomid\":\"r\"}", m));
    CHECK_FALSE(ParseCloudRecordCtlMsg("{\"op\":\"stop\"}", m));
    CHECK_FALSE(ParseCloudRecordCtlMsg("{\"op\":\"delete\",\"files\":[\"a\",2]}", m));
    CHECK_FALSE(ParseCloudRecordCtlMsg("{\"op\":\"finished\",\"recordid\":1,\"file\":\"f\"}", m));
    REQUIRE(ParseCloudRecordCtlMsg("{\"op\":\"rewind\",\"recordid\":1}", m));
    CHECK(m.m_op == cloudrecctl_op_unknown);
}

TEST_CASE("CRCT1 commands drive the recorder and finished goes back")
{
    g_crecFree = kCrecPlenty;
    g_crecClock = kCrecT0;
    std::shared_ptr<CrecSink> sink(new CrecSink());
    CloudRecordControl control;
    CloudRecorder rec;
    rec.configure("./", kCrecMinFree, &fakeFreeSpace);
    CrecStarter starter(&rec);
    control.setRecorder(&rec);
    control.setStartFn(std::bind(&CrecStarter::start, &starter, std::placeholders::_1, std::placeholders::_2));
    control.setClockFn(&fakeClock);
    rec.setListener(&control);
    control.attach(sink);

    CloudRecordCtlMsg start = crecCmd(cloudrecctl_op_start, 900040);
    start.m_roomId = "room-ctl";
    CloudRecordCtlMsg reply = crecParse(control.onCommand(BuildCloudRecordCtlMsg(start)));
    CHECK(starter.m_calls == 1);
    CHECK(reply.m_op == cloudrecctl_op_started);
    CHECK(reply.m_recordId == 900040);
    CHECK(reply.m_code == cloudrec_start_ok);
    //同一房间再开始：busy
    start.m_recordId = 900041;
    CHECK(crecParse(control.onCommand(BuildCloudRecordCtlMsg(start))).m_code == cloudrec_start_busy);

    rec.onFrame("room-ctl", screenshare_track_screen, makeFrame(screenshare_track_screen, 0, "a"), kCrecT0 + 100);
    g_crecClock = kCrecT0 + 200;
    CHECK(control.onCommand(BuildCloudRecordCtlMsg(crecCmd(cloudrecctl_op_pause, 900040))).empty());
    g_crecClock = kCrecT0 + 500;
    CHECK(control.onCommand(BuildCloudRecordCtlMsg(crecCmd(cloudrecctl_op_resume, 900040))).empty());
    rec.onFrame("room-ctl", screenshare_track_screen, makeFrame(screenshare_track_screen, 0, "b"), kCrecT0 + 600);
    CHECK(control.onCommand(BuildCloudRecordCtlMsg(crecCmd(cloudrecctl_op_stop, 900040))).empty());

    REQUIRE(sink->waitFor(1));
    CloudRecordCtlMsg fin = crecParse(sink->sent()[0]);
    CHECK(fin.m_op == cloudrecctl_op_finished);
    CHECK(fin.m_recordId == 900040);
    CHECK(fin.m_file == "rec_900040.frec");
    CHECK(fin.m_frames == 2);
    CHECK(fin.m_durationMs == 600 - 300);
    CHECK(fin.m_reason == cloudrec_stop_requested);
    CHECK(fin.m_bytes > 0);
    //探活
    CHECK(crecParse(control.onCommand("{\"op\":\"ping\"}")).m_op == cloudrecctl_op_pong);
    //格式不对、imgserver 自己发出的事件种类：不回
    CHECK(control.onCommand("not json").empty());
    CHECK(control.onCommand("{\"op\":\"pong\"}").empty());
    rec.shutdown();
    CHECK(rec.deleteFile("rec_900040.frec"));
}

TEST_CASE("CRCT2 events are kept while no control connection")
{
    CloudRecordControl control;
    control.notifyUploaded(1, "rec_1.mp4", 10);
    control.notifyUploaded(2, "rec_2.mp4", 20);
    CHECK(control.pendingCount() == 2);
    std::shared_ptr<CrecSink> first(new CrecSink());
    control.attach(first);
    CHECK(control.pendingCount() == 0);
    std::vector<std::string> sent = first->sent();
    REQUIRE(sent.size() == 2);
    CHECK(crecParse(sent[0]).m_recordId == 1);
    CHECK(crecParse(sent[1]).m_recordId == 2);
    //新连接替换旧连接后，旧连接断开不影响新连接
    std::shared_ptr<CrecSink> second(new CrecSink());
    control.attach(second);
    control.detach(first.get());
    control.notifyUploaded(3, "rec_3.mp4", 30);
    CHECK(second->sent().size() == 1);
    CHECK(first->sent().size() == 2);
    //当前连接断开：暂存
    control.detach(second.get());
    control.notifyUploaded(4, "rec_4.mp4", 40);
    CHECK(control.pendingCount() == 1);
    //连接对象已销毁（弱引用失效）也按没有连接处理
    std::shared_ptr<CrecSink> third(new CrecSink());
    control.attach(third);
    third.reset();
    control.notifyUploaded(5, "rec_5.mp4", 50);
    CHECK(control.pendingCount() == 1);
}

TEST_CASE("CRCT3 pending events are capped, oldest dropped")
{
    CloudRecordControl control;
    const size_t total = CloudRecordControl::kMaxPendingEvents + 3;
    for (size_t i = 1; i <= total; ++i)
        control.notifyUploaded(i, "rec.mp4", i);
    CHECK(control.pendingCount() == CloudRecordControl::kMaxPendingEvents);
    std::shared_ptr<CrecSink> sink(new CrecSink());
    control.attach(sink);
    std::vector<std::string> sent = sink->sent();
    REQUIRE(sent.size() == CloudRecordControl::kMaxPendingEvents);
    CHECK(crecParse(sent.front()).m_recordId == 4);
    CHECK(crecParse(sent.back()).m_recordId == total);
}

TEST_CASE("CRCT4 repeated stop reports the file on disk, delete removes files")
{
    g_crecFree = kCrecPlenty;
    CloudRecordControl control;
    CloudRecorder rec;
    rec.configure("./", kCrecMinFree, &fakeFreeSpace);
    control.setRecorder(&rec);
    //没有设置开始入口：开始返回 bad_arg
    CloudRecordCtlMsg start = crecCmd(cloudrecctl_op_start, 900050);
    start.m_roomId = "room-x";
    CHECK(crecParse(control.onCommand(BuildCloudRecordCtlMsg(start))).m_code == cloudrec_start_bad_arg);
    //录完一个文件
    REQUIRE(rec.start(900050, "room-x", kCrecT0, false, std::vector<std::pair<int, int64_t> >()) == cloudrec_start_ok);
    rec.onFrame("room-x", screenshare_track_mic, makeFrame(screenshare_track_mic, 0, "voice"), kCrecT0 + 10);
    REQUIRE(rec.stop(900050, cloudrec_stop_requested));
    rec.shutdown();
    uint64_t size = 0;
    REQUIRE(rec.statFile("rec_900050.frec", size));
    CHECK_FALSE(rec.hasRecord(900050));
    //再发停止：按文件现状回报
    CloudRecordCtlMsg fin = crecParse(control.onCommand(BuildCloudRecordCtlMsg(crecCmd(cloudrecctl_op_stop, 900050))));
    CHECK(fin.m_op == cloudrecctl_op_finished);
    CHECK(fin.m_file == "rec_900050.frec");
    CHECK(fin.m_bytes == size);
    CHECK(fin.m_reason == cloudrec_stop_requested);
    //暂停不在录的录像：不回
    CHECK(control.onCommand(BuildCloudRecordCtlMsg(crecCmd(cloudrecctl_op_pause, 900050))).empty());
    //删除，带路径的名字被拒
    CloudRecordCtlMsg del;
    del.m_op = cloudrecctl_op_delete;
    del.m_files.push_back("rec_900050.frec");
    del.m_files.push_back("../rec_900050.frec");
    CHECK(control.onCommand(BuildCloudRecordCtlMsg(del)).empty());
    CHECK_FALSE(rec.statFile("rec_900050.frec", size));
    CHECK_FALSE(rec.statFile("../x", size));
    //文件已经不在：reason 为 write_error，bytes 为 0
    fin = crecParse(control.onCommand(BuildCloudRecordCtlMsg(crecCmd(cloudrecctl_op_stop, 900050))));
    CHECK(fin.m_reason == cloudrec_stop_write_error);
    CHECK(fin.m_bytes == 0);
}

namespace {

//握手用例专用的身份密钥种子文件（当前目录，用完删除）；第二份用来模拟「对端不是预期的服务器」
const char* const kCrecKeyFile = "flamingo_crypto_crhs_test.key";
const char* const kCrecOtherKeyFile = "flamingo_crypto_crhs_other.key";
//loadOrCreate 在种子文件旁写出的公钥文件后缀
const char* const kCrecPubSuffix = ".pub";

void crecRemoveKey(const char* keyFile)
{
    remove(keyFile);
    remove((std::string(keyFile) + kCrecPubSuffix).c_str());
}

} // namespace

TEST_CASE("CRHS1 server-to-server handshake: PeerClientHandshake against ServerHandshake")
{
    REQUIRE(net::cryptoInit());
    std::string err;
    net::ServerIdentity identity;
    REQUIRE_MESSAGE(identity.loadOrCreate(kCrecKeyFile, err), err);

    net::PeerClientHandshake client;
    net::ServerHandshake server;
    CHECK(client.buildClientStreamHeader().empty());
    const std::string hello = client.buildClientHello();
    REQUIRE_FALSE(hello.empty());
    //第 1 帧只能产出一次
    CHECK(client.buildClientHello().empty());
    std::string response;
    REQUIRE_MESSAGE(server.onHandshakeFrame(hello, identity, response, err), err);
    REQUIRE_MESSAGE(client.onServerHello(response, identity.publicKey(), err), err);
    CHECK_FALSE(client.ready());
    const std::string streamHeader = client.buildClientStreamHeader();
    REQUIRE_FALSE(streamHeader.empty());
    CHECK(client.ready());
    std::string none;
    REQUIRE_MESSAGE(server.onHandshakeFrame(streamHeader, identity, none, err), err);
    CHECK(none.empty());
    CHECK(server.ready());

    //两个方向各发两帧，对端都能按序解开
    const char* const kBodies[] = { "{\"op\":\"ping\"}", "{\"op\":\"stop\",\"recordid\":1}" };
    for (uint32_t seq = 1; seq <= 2; ++seq)
    {
        std::string wire;
        uint8_t flags = 0;
        uint32_t originlen = 0;
        REQUIRE(net::encodeFramePayload(cmd_cloudrec_ctl_data, seq, kBodies[seq - 1], &client.cipher(), 0,
                                        net::payload_compress_never, wire, flags, originlen));
        std::string plain;
        REQUIRE_MESSAGE(net::decodeFramePayload(cmd_cloudrec_ctl_data, seq, flags, originlen, wire,
                                                &server.cipher(), plain, err), err);
        CHECK(plain == kBodies[seq - 1]);

        REQUIRE(net::encodeFramePayload(cmd_cloudrec_ctl_data, 0, "{\"op\":\"pong\"}", &server.cipher(), 0,
                                        net::payload_compress_never, wire, flags, originlen));
        REQUIRE_MESSAGE(net::decodeFramePayload(cmd_cloudrec_ctl_data, 0, flags, originlen, wire,
                                                &client.cipher(), plain, err), err);
        CHECK(plain == "{\"op\":\"pong\"}");
    }
    crecRemoveKey(kCrecKeyFile);
}

TEST_CASE("CRHS2 server-to-server handshake fails with a different server key")
{
    REQUIRE(net::cryptoInit());
    std::string err;
    net::ServerIdentity identity;
    net::ServerIdentity other;
    REQUIRE_MESSAGE(identity.loadOrCreate(kCrecKeyFile, err), err);
    REQUIRE_MESSAGE(other.loadOrCreate(kCrecOtherKeyFile, err), err);

    net::PeerClientHandshake client;
    net::ServerHandshake server;
    std::string response;
    REQUIRE(server.onHandshakeFrame(client.buildClientHello(), identity, response, err));
    //拿另一份公钥验签：失败，之后不能再产出第 3 帧
    CHECK_FALSE(client.onServerHello(response, other.publicKey(), err));
    CHECK(err.find("signature") != std::string::npos);
    CHECK(client.buildClientStreamHeader().empty());
    CHECK_FALSE(client.ready());
    //对端拒绝握手、公钥缺失
    net::PeerClientHandshake second;
    second.buildClientHello();
    CHECK_FALSE(second.onServerHello("{\"code\":1}", identity.publicKey(), err));
    net::PeerClientHandshake third;
    third.buildClientHello();
    CHECK_FALSE(third.onServerHello(response, NULL, err));
    crecRemoveKey(kCrecKeyFile);
    crecRemoveKey(kCrecOtherKeyFile);
}

namespace {

//CRSC1 的上传完成回调记录
std::string g_crscFile;
uint64_t g_crscBytes = 0;

void crscOnUploaded(const std::string& fileName, uint64_t bytes)
{
    g_crscFile = fileName;
    g_crscBytes = bytes;
}

} // namespace

TEST_CASE("CRSC1 cloud record scene for file sessions")
{
    //没有设置录像目录（fileserver 的情形）：一律不支持
    fileserver::SetCloudRecordScene("", NULL);
    std::string dir;
    std::string name;
    CHECK_FALSE(fileserver::CloudRecordSceneEnabled());
    CHECK_FALSE(fileserver::ResolveCloudRecordTarget("rec_42_mp4", dir, name));
    fileserver::NotifyCloudRecordUploaded("rec_42.mp4", 1);

    fileserver::SetCloudRecordScene("./", &crscOnUploaded);
    CHECK(fileserver::CloudRecordSceneEnabled());
    REQUIRE(fileserver::ResolveCloudRecordTarget("rec_42_mp4", dir, name));
    CHECK(dir == "./");
    CHECK(name == "rec_42.mp4");
    //不是录像文件名的键：拒绝（防止借令牌读写目录里的其它文件）
    CHECK_FALSE(fileserver::ResolveCloudRecordTarget("0123456789abcdef0123456789abcdef", dir, name));
    CHECK_FALSE(fileserver::ResolveCloudRecordTarget("rec_42.mp4", dir, name));
    CHECK_FALSE(fileserver::ResolveCloudRecordTarget("x_mp4", dir, name));

    //文件完整性：同名且大小相同
    FILE* f = fopen("./rec_77.mp4", "wb");
    REQUIRE(f != NULL);
    fwrite("hello", 1, 5, f);
    fclose(f);
    CHECK(fileserver::CloudRecordFileComplete("./rec_77.mp4", 5));
    CHECK_FALSE(fileserver::CloudRecordFileComplete("./rec_77.mp4", 6));
    CHECK_FALSE(fileserver::CloudRecordFileComplete("./rec_missing.mp4", 0));
    remove("./rec_77.mp4");

    //上传完成回调
    fileserver::NotifyCloudRecordUploaded("rec_77.mp4", 5);
    CHECK(g_crscFile == "rec_77.mp4");
    CHECK(g_crscBytes == 5);
    fileserver::SetCloudRecordScene("", NULL);
}
