/**
 *  客户端云端录制的单元测试（标签 [cloudrec]），CloudRecordClientTests.cpp
 *
 *  第 27 期（docs/屏幕共享与录制方案.md 12.6）。原始帧文件的读取（FlamingoMedia/Record/CloudRecordReader，27a）：测试数据按服务端
 *  flamingoserver/imgserversrc/CloudRecordFile.h 的格式逐字节拼出，两边的格式据此对齐：
 *    - CFR1 文件头与三种记录的解码；座位负载；标识、版本不对时读不出；
 *    - CFR2 末尾残缺时读到最后一条完整记录为止；负载长度超限视为损坏；
 *    - CFR3 文件读取器：跨块读取、读完后的状态、已读字节数、残缺标记；帧记录的负载能交给 ShareFraming 拆出帧头；
 *  共享中云端录制的协议（FlamingoMedia/Share/ShareProtocolClient，27b）：
 *    - CFP1 1754 的请求与应答，1725 快照里的 cloudrecord 与事件 15 的 cloudrecordcode；
 *  云端录像的协议（FlamingoMedia/Record/CloudRecordProtocolClient）：
 *    - CFP2 1755 / 1756 请求的组装，1755 / 1756 应答与 1757 推送的解析；
 *    - CFP3 没传完的上传清单（27d-4）的写出与读回，登录后续传、去掉还是先留着的判断；
 *  离线合成的纯逻辑（FlamingoMedia/Record/CloudRecordCompose，27a）：
 *    - CFC1 输出时间轴去掉暂停、只往前走；CFC2 声音按发送端时间戳排位与重新定锚；CFC3 混音缓冲的叠加、截幅与按序取走；
 *    - CFC4 画中画取谁、哪些画面还在共享、PCM 的均方根；CFC5 合成的视频码率取「标准」画质；
 *    - CFC6 白板靠白板视野判断还在（2026-10-07）；CFC7 白板图在一块画面里按 16:9 放下的尺寸；
 *    - 合成提速（2026-10-08）：CFC8 画面在格子里所占的矩形与是否原尺寸；CFC9 原尺寸画面逐行复制进画布
 *      （含越界裁剪）；CFC10 画布签名与能否沿用上一帧；CFC11 激光笔算作按时间变化的笔迹。
 *  录制管理窗口「云端」页的纯逻辑（FlamingoMedia/Record/CloudRecordManagerModel，27d）：
 *    - CRM1 一行的状态：服务端状态与本机正在做的事合起来；CRM2 状态列的文字、颜色、进度条与到期提醒；
 *    - CRM3 副文字与一行可做的操作；CRM4 右上角的用量文字、提醒色与用量条；CRM5 重命名标题的校验；
 *    - CRM6 每行右侧状态列的矩形；
 *    - CRM7 暂停的合成与上传（2026-10-07，方案第 14.4 节）：行状态、文字、颜色、可做的操作；CRM8 暂停清单的写出与读回。
 *  框架 Catch2 v3。
 *  balloonwj@qq.com   2026-10-06
 */
#include "stdafx.h"
#include "catch_amalgamated.hpp"

#include <stdio.h>
#include <map>
#include <set>
#include <string>
#include <vector>
#include <windows.h>

#include "../../FlamingoMedia/Record/CloudRecordCompose.h"
#include "../../FlamingoMedia/Record/CloudRecordManagerModel.h"
#include "../../FlamingoMedia/Record/CloudRecordProtocolClient.h"
#include "../../FlamingoMedia/Record/CloudRecordReader.h"
#include "../../FlamingoMedia/Record/RecordPolicy.h"
#include "../../FlamingoMedia/Share/ShareAnnotation.h"   // AnnotationStroke：CFC11
#include "../../FlamingoMedia/Share/ShareFraming.h"
#include "../../FlamingoMedia/Share/ShareProtocolClient.h"
#include "../net/Protocol.h"
#include "../Common/I18n/I18n.h"   // TR：CRM7 比较状态列的文字

using namespace screenrecord;

namespace {

//测试用的开始时刻，Unix 毫秒
const int64_t kCrrStart = 1790000000000LL;

void putLe(std::string& out, uint64_t v, size_t bytes)
{
    for (size_t i = 0; i < bytes; ++i)
    {
        out.push_back(static_cast<char>(v & 0xFF));
        v >>= 8;
    }
}

std::string makeHeader(uint64_t recordId, const std::string& roomId, uint16_t version = kFrecVersion)
{
    std::string h("FREC");
    putLe(h, version, 2);
    putLe(h, 0, 2);
    putLe(h, recordId, 8);
    putLe(h, static_cast<uint64_t>(kCrrStart), 8);
    putLe(h, roomId.size(), 2);
    h += roomId;
    return h;
}

std::string makeRecord(int type, uint32_t relMs, const std::string& payload)
{
    std::string r;
    r.push_back(static_cast<char>(type));
    putLe(r, relMs, 4);
    putLe(r, payload.size(), 4);
    r += payload;
    return r;
}

std::string makeSeat(int seat, int64_t userId)
{
    std::string p;
    p.push_back(static_cast<char>(seat));
    putLe(p, static_cast<uint64_t>(userId), 8);
    return p;
}

//一帧房间中转帧的明文包体
std::string makeFrame(int track, int seat, const std::string& data)
{
    screenshare::ShareFrameHeader h;
    h.m_track = track;
    h.m_senderSeat = seat;
    h.m_flags = screenshare_flag_first_frag | screenshare_flag_last_frag;
    h.m_timestamp = 77;
    return screenshare::ShareFraming::encodeBody(h, data.data(), data.size());
}

std::wstring tempPath(const wchar_t* name)
{
    wchar_t dir[MAX_PATH] = { 0 };
    ::GetTempPathW(MAX_PATH, dir);
    return std::wstring(dir) + name;
}

void writeFile(const std::wstring& path, const std::string& bytes)
{
    FILE* f = NULL;
    REQUIRE(_wfopen_s(&f, path.c_str(), L"wb") == 0);
    fwrite(bytes.data(), 1, bytes.size(), f);
    fclose(f);
}

} // namespace

TEST_CASE("CFR1 decode header and records", "[cloudrec]")
{
    std::string data = makeHeader(42, "ss-5127-1000-1");
    data += makeRecord(kFrecRecordSeat, 0, makeSeat(3, 7001));
    data += makeRecord(kFrecRecordFrame, 1234, "abc");
    data += makeRecord(kFrecRecordPause, 2000, std::string());
    size_t pos = 0;
    FrecFileHeader h;
    REQUIRE(DecodeFrecHeader(data, pos, h));
    CHECK(h.m_version == kFrecVersion);
    CHECK(h.m_recordId == 42);
    CHECK(h.m_startTimeMs == kCrrStart);
    CHECK(h.m_roomId == "ss-5127-1000-1");
    FrecRecord r;
    REQUIRE(DecodeFrecRecord(data, pos, r));
    CHECK(r.m_type == kFrecRecordSeat);
    int seat = 0;
    int64_t userId = 0;
    REQUIRE(DecodeFrecSeat(r.m_payload, seat, userId));
    CHECK(seat == 3);
    CHECK(userId == 7001);
    REQUIRE(DecodeFrecRecord(data, pos, r));
    CHECK(r.m_type == kFrecRecordFrame);
    CHECK(r.m_relMs == 1234);
    CHECK(r.m_payload == "abc");
    REQUIRE(DecodeFrecRecord(data, pos, r));
    CHECK(r.m_type == kFrecRecordPause);
    CHECK(r.m_payload.empty());
    CHECK(pos == data.size());
    CHECK_FALSE(DecodeFrecRecord(data, pos, r));
    CHECK_FALSE(DecodeFrecSeat("ab", seat, userId));
    //标识、版本不对
    std::string bad = makeHeader(1, "r");
    bad[0] = 'X';
    pos = 0;
    CHECK_FALSE(DecodeFrecHeader(bad, pos, h));
    pos = 0;
    CHECK_FALSE(DecodeFrecHeader(makeHeader(1, "r", 2), pos, h));
    //房间号不完整
    std::string shortRoom = makeHeader(1, "room");
    shortRoom.resize(shortRoom.size() - 1);
    pos = 0;
    CHECK_FALSE(DecodeFrecHeader(shortRoom, pos, h));
}

TEST_CASE("CFR2 truncated and corrupt", "[cloudrec]")
{
    const std::string full = makeRecord(kFrecRecordFrame, 10, "voice");
    std::string data = full + full.substr(0, full.size() - 2);
    size_t pos = 0;
    FrecRecord r;
    REQUIRE(DecodeFrecRecord(data, pos, r));
    const size_t before = pos;
    CHECK_FALSE(DecodeFrecRecord(data, pos, r));
    CHECK(pos == before);
    //负载长度超过上限
    std::string huge = makeRecord(kFrecRecordFrame, 0, std::string());
    huge[5] = static_cast<char>(0xFF);
    huge[6] = static_cast<char>(0xFF);
    huge[7] = static_cast<char>(0xFF);
    huge[8] = static_cast<char>(0x7F);
    pos = 0;
    CHECK_FALSE(DecodeFrecRecord(huge, pos, r));
}

TEST_CASE("CFR3 file reader across chunks", "[cloudrec]")
{
    //写一个超过一个读块的文件：几条大帧，跨块边界
    std::string data = makeHeader(9, "room-x");
    data += makeRecord(kFrecRecordSeat, 0, makeSeat(0, 5127));
    const std::string big(CFrecReader::kReadChunkBytes / 2 + 123, 'v');
    const int kFrames = 5;
    for (int i = 0; i < kFrames; ++i)
        data += makeRecord(kFrecRecordFrame, static_cast<uint32_t>(i * 40), makeFrame(screenshare_track_screen, 0, big));
    data += makeRecord(kFrecRecordResume, 999, std::string());
    const size_t completeBytes = data.size();
    data += std::string("\x01\x02\x03", 3);   //末尾残缺
    const std::wstring path = tempPath(L"flamingo_cfr3_test.frec");
    writeFile(path, data);

    CFrecReader reader;
    REQUIRE(reader.Open(path));
    CHECK(reader.Header().m_recordId == 9);
    CHECK(reader.Header().m_roomId == "room-x");
    CHECK(reader.FileBytes() == data.size());
    FrecRecord r;
    REQUIRE(reader.Next(r));
    CHECK(r.m_type == kFrecRecordSeat);
    int frames = 0;
    while (reader.Next(r))
    {
        if (r.m_type != kFrecRecordFrame)
        {
            CHECK(r.m_type == kFrecRecordResume);
            CHECK(r.m_relMs == 999);
            continue;
        }
        screenshare::ShareFrameHeader fh;
        const char* payload = NULL;
        size_t payloadLen = 0;
        REQUIRE(screenshare::ShareFraming::decodeBody(r.m_payload.data(), r.m_payload.size(), fh, payload, payloadLen));
        CHECK(fh.m_track == screenshare_track_screen);
        CHECK(payloadLen == big.size());
        CHECK(r.m_relMs == static_cast<uint32_t>(frames * 40));
        ++frames;
    }
    CHECK(frames == kFrames);
    CHECK(reader.IsTruncated());
    CHECK(reader.BytesConsumed() == completeBytes);
    reader.Close();
    ::DeleteFileW(path.c_str());

    //不是 .frec 的文件、不存在的文件
    writeFile(path, "hello");
    CHECK_FALSE(reader.Open(path));
    ::DeleteFileW(path.c_str());
    CHECK_FALSE(reader.Open(tempPath(L"flamingo_cfr3_missing.frec")));
}

TEST_CASE("CFP1 cloud record control and snapshot", "[cloudrec]")
{
    using screenshare::ShareProtocolClient;
    const std::string req = ShareProtocolClient::buildCloudRecordControl("ss-1", cloudrecord_control_pause);
    CHECK(req.find("\"roomid\":\"ss-1\"") != std::string::npos);
    CHECK(req.find("\"action\":2") != std::string::npos);

    screenshare::ShareCloudRecordResult res;
    REQUIRE(ShareProtocolClient::parseCloudRecordResult(
        "{\"code\":0,\"msg\":\"ok\",\"roomid\":\"ss-1\",\"action\":1,\"recordid\":42}", res));
    CHECK(res.m_code == 0);
    CHECK(res.m_roomId == "ss-1");
    CHECK(res.m_action == cloudrecord_control_start);
    CHECK(res.m_recordId == 42);
    REQUIRE(ShareProtocolClient::parseCloudRecordResult("{\"code\":736,\"msg\":\"\",\"roomid\":\"ss-1\",\"action\":1}", res));
    CHECK(res.m_code == error_code_cloudrecord_disabled);
    CHECK(res.m_recordId == 0);
    CHECK_FALSE(ShareProtocolClient::parseCloudRecordResult("x", res));

    //快照：正在录
    screenshare::ShareStateNotify st;
    REQUIRE(ShareProtocolClient::parseStateNotify(
        "{\"roomid\":\"ss-1\",\"event\":15,\"actor\":\"5127\",\"host\":\"5127\",\"members\":[],"
        "\"cloudrecord\":{\"recordid\":\"42\",\"owner\":\"5127\",\"paused\":1,\"since\":1790000000000}}", st));
    CHECK(st.m_event == screenshare::share_event_cloudrecord_changed);
    CHECK(st.m_cloudRecordId == 42);
    CHECK(st.m_cloudRecordOwner == 5127);
    CHECK(st.m_cloudRecordPaused);
    CHECK(st.m_cloudRecordSince == 1790000000000LL);
    CHECK(st.m_cloudRecordCode == 0);
    //被动结束：没有 cloudrecord，带原因
    REQUIRE(ShareProtocolClient::parseStateNotify(
        "{\"roomid\":\"ss-1\",\"event\":15,\"actor\":\"0\",\"host\":\"5127\",\"members\":[],\"cloudrecordcode\":738}",
        st));
    CHECK(st.m_cloudRecordId == 0);
    CHECK_FALSE(st.m_cloudRecordPaused);
    CHECK(st.m_cloudRecordCode == error_code_cloudrecord_unavailable);
}

TEST_CASE("CFC1 compose timeline removes pauses", "[cloudrec]")
{
    screenrecord::CloudComposeTimeline t;
    CHECK(t.OutMs(0) == 0);
    CHECK(t.OutMs(1000) == 1000);
    t.OnPause(2000);
    CHECK(t.Paused());
    //暂停中的时刻按暂停开始处算
    CHECK(t.OutMs(2500) == 2000);
    t.OnResume(5000);
    CHECK_FALSE(t.Paused());
    CHECK(t.OutMs(5000) == 2000);
    CHECK(t.OutMs(6000) == 3000);
    //时间轴只往前走：到达顺序颠倒的记录不会让它倒退
    CHECK(t.OutMs(5500) == 3000);
    //重复的继续、没有暂停时的继续：不影响
    t.OnResume(7000);
    CHECK(t.OutMs(7000) == 4000);
}

TEST_CASE("CFC2 audio placer anchors on sender timestamps", "[cloudrec]")
{
    screenrecord::CloudAudioPlacer p;
    const int64_t kSamplesPerMs = 48;
    //第一包按到达时刻定锚
    CHECK(p.Place(11, 1000000, 100) == 100 * kSamplesPerMs);
    //之后按时间戳排：到达有抖动也不影响
    CHECK(p.Place(11, 1020000, 130) == 120 * kSamplesPerMs);
    CHECK(p.Place(11, 1040000, 131) == 140 * kSamplesPerMs);
    //另一路独立定锚
    CHECK(p.Place(12, 5000, 200) == 200 * kSamplesPerMs);
    //偏离超过 1 秒：重新定锚
    CHECK(p.Place(11, 1060000, 3000) == 3000 * kSamplesPerMs);
    CHECK(p.Place(11, 1080000, 3010) == 3020 * kSamplesPerMs);
    //时间戳倒退（发送端重新开始采集）：重新定锚
    CHECK(p.Place(11, 0, 4000) == 4000 * kSamplesPerMs);
}

TEST_CASE("CFC3 mix buffer sums, clamps and takes in order", "[cloudrec]")
{
    screenrecord::CloudAudioMixBuffer mix;
    const int16_t a[4] = { 100, 200, 30000, -30000 };
    const int16_t b[4] = { 1, 2, 30000, -30000 };
    mix.Add(2, a, 4);
    mix.Add(2, b, 4);
    std::vector<int16_t> out;
    mix.Take(3, out);
    REQUIRE(out.size() == 3);
    CHECK(out[0] == 0);
    CHECK(out[1] == 0);
    CHECK(out[2] == 101);
    CHECK(mix.Position() == 3);
    mix.Take(4, out);
    CHECK(out[0] == 202);
    CHECK(out[1] == 32767);
    CHECK(out[2] == -32768);
    CHECK(out[3] == 0);
    //落在已经取走的部分之前的样点丢弃，后面的照常叠加
    const int16_t c[3] = { 7, 8, 9 };
    mix.Add(6, c, 3);
    mix.Take(2, out);
    CHECK(out[0] == 8);
    CHECK(out[1] == 9);
    //整段都在之前：什么也不加
    mix.Add(0, c, 3);
    mix.Take(1, out);
    CHECK(out[0] == 0);
}

TEST_CASE("CFC4 pip seat, visible screens and rms", "[cloudrec]")
{
    std::set<int> speaking;
    std::set<int> cameras;
    CHECK(screenrecord::ChooseCloudPipSeat(speaking, cameras, -1) == -1);
    cameras.insert(3);
    cameras.insert(5);
    //没人说话：沿用上一位（摄像头还开着），否则不显示
    CHECK(screenrecord::ChooseCloudPipSeat(speaking, cameras, 5) == 5);
    CHECK(screenrecord::ChooseCloudPipSeat(speaking, cameras, 7) == -1);
    //说话中、摄像头开着的人里座位最小的
    speaking.insert(7);
    speaking.insert(5);
    speaking.insert(3);
    CHECK(screenrecord::ChooseCloudPipSeat(speaking, cameras, 5) == 3);
    speaking.erase(3);
    CHECK(screenrecord::ChooseCloudPipSeat(speaking, cameras, -1) == 5);

    std::map<int, int64_t> last;
    last[4] = 3000;
    last[0] = 9000;
    last[2] = 11000;
    std::vector<int> seats = screenrecord::CloudVisibleScreenSeats(last, 12000);
    REQUIRE(seats.size() == 3);
    CHECK(seats[0] == 0);
    CHECK(seats[1] == 2);
    CHECK(seats[2] == 4);
    //超过 10 秒没有新帧的不显示
    seats = screenrecord::CloudVisibleScreenSeats(last, 19500);
    REQUIRE(seats.size() == 1);
    CHECK(seats[0] == 2);

    const int16_t quiet[4] = { 0, 0, 0, 0 };
    const int16_t loud[4] = { 1000, -1000, 1000, -1000 };
    CHECK(screenrecord::CloudPcmRms(quiet, 4) == 0);
    CHECK(screenrecord::CloudPcmRms(loud, 4) == 1000);
    CHECK(screenrecord::CloudPcmRms(NULL, 0) == 0);
}

TEST_CASE("CFC5 compose video bitrate uses the standard quality", "[cloudrec]")
{
    //2026-10-07 联调实测「高」画质每小时约 3 GB，用户定为「标准」：1080p 输出即 4 Mbps 的基准码率
    CHECK(screenrecord::CloudComposeVideoBitrate() ==
          screenrecord::RecordVideoBitrate(screenrecord::record_quality_standard, screenrecord::kCloudComposeWidth,
                                           screenrecord::kCloudComposeHeight));
    CHECK(screenrecord::CloudComposeVideoBitrate() == screenrecord::kRecordStandardBitrate1080p);
}

TEST_CASE("CFC6 whiteboards stay visible while their views keep arriving", "[cloudrec]")
{
    //2026-10-07（方案第 14.1 节 ④-2）：白板没有视频帧，靠建立者每 3 秒广播一次的白板视野判断它还在
    std::map<int, int64_t> last;
    std::set<int> boards;
    last[0] = 1000;
    screenrecord::CloudNoteBoardView(7, 2000, last, boards);
    CHECK(boards.count(7) == 1);
    std::vector<int> seats = screenrecord::CloudVisibleScreenSeats(last, 3000);
    REQUIRE(seats.size() == 2);
    CHECK(seats[0] == 0);
    CHECK(seats[1] == 7);
    //视野不断到来：一直在
    screenrecord::CloudNoteBoardView(7, 5000, last, boards);
    screenrecord::CloudNoteBoardView(7, 8000, last, boards);
    seats = screenrecord::CloudVisibleScreenSeats(last, 12000);
    REQUIRE(seats.size() == 1);
    CHECK(seats[0] == 7);
    //建立者关掉白板，视野不再到来：超过 kCloudScreenStaleMs 后不再显示
    seats = screenrecord::CloudVisibleScreenSeats(last, 8000 + screenrecord::kCloudScreenStaleMs + 1);
    CHECK(seats.empty());
}

TEST_CASE("CFC7 whiteboard image fits the tile at 16:9", "[cloudrec]")
{
    int w = 0;
    int h = 0;
    //一整块 1920×1080：正好 16:9
    screenrecord::CloudBoardImageSize(1920, 1080, w, h);
    CHECK(w == 1920);
    CHECK(h == 1080);
    //并列两块（每块 958×1080）：按宽放下
    screenrecord::CloudBoardImageSize(958, 1080, w, h);
    CHECK(w == 958);
    CHECK(h == 958 * 9 / 16);
    //很宽很矮：按高放下
    screenrecord::CloudBoardImageSize(1920, 400, w, h);
    CHECK(h == 400);
    CHECK(w == 400 * 16 / 9);
    //尺寸不合法：0
    screenrecord::CloudBoardImageSize(0, 400, w, h);
    CHECK(w == 0);
    CHECK(h == 0);
}

TEST_CASE("CFC8 frame fit rect and one-to-one detection", "[cloudrec]")
{
    const RECT full = { 0, 0, 1920, 1080 };
    //1920×1080 的画面放进同样大小的格子：原尺寸
    RECT dst = screenrecord::CloudFitRect(1920, 1080, full);
    CHECK(dst.left == 0);
    CHECK(dst.top == 0);
    CHECK(dst.right == 1920);
    CHECK(dst.bottom == 1080);
    CHECK(screenrecord::CloudFitIsOneToOne(1920, 1080, dst));
    //1280×720：放大到整格，不是原尺寸
    dst = screenrecord::CloudFitRect(1280, 720, full);
    CHECK(dst.right - dst.left == 1920);
    CHECK(dst.bottom - dst.top == 1080);
    CHECK_FALSE(screenrecord::CloudFitIsOneToOne(1280, 720, dst));
    //1920×1200（16:10）：高撑满、左右留黑，居中
    dst = screenrecord::CloudFitRect(1920, 1200, full);
    CHECK(dst.bottom - dst.top == 1080);
    CHECK(dst.right - dst.left == 1728);
    CHECK(dst.left == 96);
    CHECK_FALSE(screenrecord::CloudFitIsOneToOne(1920, 1200, dst));
    //2560×1080（超宽）：宽撑满、上下留黑
    dst = screenrecord::CloudFitRect(2560, 1080, full);
    CHECK(dst.right - dst.left == 1920);
    CHECK(dst.bottom - dst.top == 810);
    CHECK(dst.top == 135);
    //格子偏移时矩形跟着格子走
    const RECT cell = { 962, 0, 1920, 1080 };
    dst = screenrecord::CloudFitRect(958, 1080, cell);
    CHECK(dst.left == 962);
    CHECK(screenrecord::CloudFitIsOneToOne(958, 1080, dst));
    //尺寸不合法：返回格子本身，不算原尺寸
    dst = screenrecord::CloudFitRect(0, 1080, full);
    CHECK(dst.right == 1920);
    CHECK_FALSE(screenrecord::CloudFitIsOneToOne(0, 1080, dst));
}

TEST_CASE("CFC9 copy frame rows into the canvas", "[cloudrec]")
{
    //4×3 的画布，全 0；2×2 的画面，每个像素的四个字节都等于像素序号 + 1
    std::vector<uint8_t> canvas(4 * 3 * 4, 0);
    std::vector<uint8_t> frame(2 * 2 * 4, 0);
    for (int i = 0; i < 4; ++i)
    {
        for (int b = 0; b < 4; ++b)
            frame[i * 4 + b] = static_cast<uint8_t>(i + 1);
    }
    screenrecord::CloudCopyFrameRows(&canvas[0], 4, 3, &frame[0], 2, 2, 1, 1);
    //(1,1)、(2,1)、(1,2)、(2,2) 依次为 1、2、3、4，其余仍为 0
    CHECK(canvas[(1 * 4 + 1) * 4] == 1);
    CHECK(canvas[(1 * 4 + 2) * 4] == 2);
    CHECK(canvas[(2 * 4 + 1) * 4] == 3);
    CHECK(canvas[(2 * 4 + 2) * 4] == 4);
    CHECK(canvas[(0 * 4 + 0) * 4] == 0);
    CHECK(canvas[(1 * 4 + 3) * 4] == 0);
    CHECK(canvas[(2 * 4 + 0) * 4] == 0);
    //越界：放在 (3,2) 只有左上角一个像素落在画布里
    std::vector<uint8_t> edge(4 * 3 * 4, 0);
    screenrecord::CloudCopyFrameRows(&edge[0], 4, 3, &frame[0], 2, 2, 3, 2);
    CHECK(edge[(2 * 4 + 3) * 4] == 1);
    size_t nonZero = 0;
    for (size_t i = 0; i < edge.size(); ++i)
        nonZero += (edge[i] != 0) ? 1 : 0;
    CHECK(nonZero == 4);
    //完全在画布外：什么都不写
    std::vector<uint8_t> outside(4 * 3 * 4, 0);
    screenrecord::CloudCopyFrameRows(&outside[0], 4, 3, &frame[0], 2, 2, 5, 0);
    screenrecord::CloudCopyFrameRows(&outside[0], 4, 3, &frame[0], 2, 2, -2, 0);
    for (size_t i = 0; i < outside.size(); ++i)
        CHECK(outside[i] == 0);
}

TEST_CASE("CFC10 canvas key and reuse decision", "[cloudrec]")
{
    screenrecord::CloudCanvasKey a;
    a.m_seats.push_back(0);
    a.m_serials.push_back(7);
    a.m_annotVersion = 3;
    a.m_seatVersion = 1;
    screenrecord::CloudCanvasKey b = a;
    CHECK(a == b);
    //画过、没有按时间变化的笔迹、签名相同：沿用
    CHECK(screenrecord::CloudCanvasReusable(true, false, a, b));
    //还没画过、有正在淡出的激光笔：都要重画
    CHECK_FALSE(screenrecord::CloudCanvasReusable(false, false, a, b));
    CHECK_FALSE(screenrecord::CloudCanvasReusable(true, true, a, b));
    //任何一项变了：重画
    b.m_serials[0] = 8;
    CHECK(a != b);
    CHECK_FALSE(screenrecord::CloudCanvasReusable(true, false, a, b));
    b = a;
    b.m_annotVersion = 4;
    CHECK_FALSE(screenrecord::CloudCanvasReusable(true, false, a, b));
    b = a;
    b.m_seatVersion = 2;
    CHECK_FALSE(screenrecord::CloudCanvasReusable(true, false, a, b));
    b = a;
    b.m_seats.push_back(3);
    b.m_serials.push_back(1);
    CHECK_FALSE(screenrecord::CloudCanvasReusable(true, false, a, b));
}

TEST_CASE("CFC11 laser strokes change with time", "[cloudrec]")
{
    std::vector<screenshare::AnnotationStroke> strokes;
    CHECK_FALSE(screenrecord::CloudHasTimedStrokes(strokes));
    screenshare::AnnotationStroke pen;
    pen.m_tool = screenshare::annot_tool_pen;
    strokes.push_back(pen);
    CHECK_FALSE(screenrecord::CloudHasTimedStrokes(strokes));
    screenshare::AnnotationStroke laser;
    laser.m_tool = screenshare::annot_tool_laser;
    strokes.push_back(laser);
    CHECK(screenrecord::CloudHasTimedStrokes(strokes));
}

TEST_CASE("CFP2 cloud record list, op and notify", "[cloudrec]")
{
    using namespace screenrecord;
    CHECK(BuildCloudRecordList(0, 20) == "{\"beforeid\":0,\"limit\":20}");
    CHECK(BuildCloudRecordDownloadToken(42, kCloudFileFrec).find("\"kind\":1") != std::string::npos);
    CHECK(BuildCloudRecordSend(42, 2, 900).find("\"cid\":900") != std::string::npos);
    const std::string up = BuildCloudRecordUploadToken(0, 1000, true, "t", 5000, 1790000000000LL);
    CHECK(up.find("\"audioonly\":1") != std::string::npos);
    CHECK(up.find("\"starttime\":1790000000000") != std::string::npos);
    //已有录像的上传不带新建才要的字段
    CHECK(BuildCloudRecordUploadToken(42, 1000, false, "", 0, 0).find("audioonly") == std::string::npos);

    CloudRecordListResult list;
    REQUIRE(ParseCloudRecordList(
        "{\"code\":0,\"records\":[{\"recordid\":42,\"owner\":5127,\"source\":1,\"title\":\"x\",\"status\":2,"
        "\"bytes\":0,\"frecbytes\":1024,\"audioonly\":0,\"durationms\":60000}],\"hasmore\":1,\"enabled\":1,"
        "\"usedbytes\":1024,\"quotabytes\":53687091200,\"retaindays\":30}", list));
    REQUIRE(list.m_records.size() == 1);
    CHECK(list.m_records[0].m_recordId == 42);
    CHECK(list.m_records[0].m_owner == 5127);
    CHECK(list.m_records[0].m_status == cloudrecord_status_pending);
    CHECK(list.m_records[0].m_frecBytes == 1024);
    CHECK(list.m_hasMore);
    CHECK(list.m_enabled);
    CHECK(list.m_quotaBytes == 53687091200LL);

    CloudRecordOpResult op;
    REQUIRE(ParseCloudRecordOpResult(
        "{\"code\":0,\"op\":3,\"recordid\":42,\"token\":\"tok\",\"host\":\"10.0.0.2\",\"port\":20001,"
        "\"filename\":\"rec_42.frec\",\"bytes\":1024}", op));
    CHECK(op.m_op == cloudrecord_op_download_token);
    CHECK(op.m_token == "tok");
    CHECK(op.m_port == 20001);
    CHECK(op.m_fileName == "rec_42.frec");
    CHECK_FALSE(op.m_hasRecord);
    CHECK_FALSE(ParseCloudRecordOpResult("x", op));

    CloudRecordNotify n;
    REQUIRE(ParseCloudRecordNotify("{\"type\":1,\"recordid\":42,\"record\":{\"recordid\":42,\"status\":3}}", n));
    CHECK(n.m_type == cloudrecord_notify_status);
    CHECK(n.m_hasRecord);
    CHECK(n.m_record.m_status == cloudrecord_status_ready);
    REQUIRE(ParseCloudRecordNotify("{\"type\":3,\"recordid\":\"42\"}", n));
    CHECK(n.m_recordId == 42);
    CHECK_FALSE(n.m_hasRecord);
    CHECK_FALSE(ParseCloudRecordNotify("{\"type\":3}", n));
}

TEST_CASE("CRM1 cloud row state from server status and local work", "[cloudrec]")
{
    using namespace screenrecord;
    CloudRowLocal idle;
    CHECK(CloudRowStateOf(cloudrecord_status_recording, idle) == cloud_row_recording);
    CHECK(CloudRowStateOf(cloudrecord_status_pending, idle) == cloud_row_pending);
    CHECK(CloudRowStateOf(cloudrecord_status_ready, idle) == cloud_row_ready);
    CHECK(CloudRowStateOf(cloudrecord_status_failed, idle) == cloud_row_record_failed);
    CHECK(CloudRowStateOf(cloudrecord_status_uploading, idle) == cloud_row_upload_stalled);
    //不认识的状态按等待合成
    CHECK(CloudRowStateOf(99, idle) == cloud_row_pending);

    //等待合成时看本机：上传优先于合成，合成优先于失败
    CloudRowLocal local;
    local.m_composeFailed = true;
    CHECK(CloudRowStateOf(cloudrecord_status_pending, local) == cloud_row_compose_failed);
    local.m_composing = true;
    CHECK(CloudRowStateOf(cloudrecord_status_pending, local) == cloud_row_composing);
    local.m_uploading = true;
    CHECK(CloudRowStateOf(cloudrecord_status_pending, local) == cloud_row_uploading);
    //保存到云端：本机在传为上传中
    CHECK(CloudRowStateOf(cloudrecord_status_uploading, local) == cloud_row_uploading);
    //服务端已可观看：本机还留着任务也以服务端为准
    CHECK(CloudRowStateOf(cloudrecord_status_ready, local) == cloud_row_ready);
}

TEST_CASE("CRM2 cloud row state text, color, progress and expiry", "[cloudrec]")
{
    using namespace screenrecord;
    const int64_t kNow = 1790000000000LL;
    const int64_t kDay = 24LL * 60 * 60 * 1000;
    //永久保留、还早：不提醒；不足 3 天：向上取整；已过到期时刻：0
    CHECK(CloudDaysLeft(0, kNow) == -1);
    CHECK(CloudDaysLeft(kNow + 10 * kDay, kNow) == -1);
    CHECK(CloudDaysLeft(kNow + 3 * kDay, kNow) == 3);
    CHECK(CloudDaysLeft(kNow + 2 * kDay + 1, kNow) == 3);
    CHECK(CloudDaysLeft(kNow + 1, kNow) == 1);
    CHECK(CloudDaysLeft(kNow - 1, kNow) == 0);

    CHECK(CloudRowStateText(cloud_row_ready, -1, -1) == L"可观看");
    CHECK(CloudRowStateText(cloud_row_ready, -1, 3) == L"3 天后到期");
    CHECK(CloudRowStateText(cloud_row_ready, -1, 0) == L"今天到期");
    CHECK(CloudRowStateText(cloud_row_composing, 45, -1) == L"合成中 45%");
    CHECK(CloudRowStateText(cloud_row_composing, -1, -1) == L"合成中");
    CHECK(CloudRowStateText(cloud_row_uploading, 72, -1) == L"上传中 72%");
    CHECK(CloudRowStateText(cloud_row_pending, -1, -1) == L"等待合成");
    CHECK(CloudRowStateText(cloud_row_compose_failed, -1, -1) == L"合成失败");
    CHECK(CloudRowStateText(cloud_row_record_failed, -1, -1) == L"没有录到内容");
    CHECK(CloudRowStateText(cloud_row_upload_stalled, -1, -1) == L"等待续传");
    CHECK(CloudRowStateText(cloud_row_recording, -1, -1) == L"录制中");
    //快到期只对可观看的录像
    CHECK(CloudRowStateText(cloud_row_pending, -1, 1) == L"等待合成");

    //颜色：可观看为绿、快到期为橙、进行中为蓝、失败为红、等待为灰
    CHECK(CloudRowStateColor(cloud_row_ready, -1) == RGB(31, 157, 85));
    CHECK(CloudRowStateColor(cloud_row_ready, 2) == RGB(217, 130, 43));
    CHECK(CloudRowStateColor(cloud_row_composing, -1) == RGB(45, 108, 223));
    CHECK(CloudRowStateColor(cloud_row_uploading, -1) == RGB(45, 108, 223));
    CHECK(CloudRowStateColor(cloud_row_compose_failed, -1) == RGB(209, 67, 67));
    CHECK(CloudRowStateColor(cloud_row_record_failed, -1) == RGB(209, 67, 67));
    CHECK(CloudRowStateColor(cloud_row_pending, -1) == RGB(138, 145, 156));
    CHECK(CloudRowStateColor(cloud_row_upload_stalled, -1) == RGB(138, 145, 156));

    //进度条：只在合成中、上传中且进度已知时画
    CHECK(CloudRowShowsProgress(cloud_row_composing, 0));
    CHECK(CloudRowShowsProgress(cloud_row_uploading, 100));
    CHECK_FALSE(CloudRowShowsProgress(cloud_row_uploading, -1));
    CHECK_FALSE(CloudRowShowsProgress(cloud_row_ready, 50));
}

TEST_CASE("CRM3 cloud row sub text and actions", "[cloudrec]")
{
    using namespace screenrecord;
    SYSTEMTIME t;
    memset(&t, 0, sizeof(t));
    t.wYear = 2026;
    t.wMonth = 10;
    t.wDay = 6;
    t.wHour = 14;
    t.wMinute = 20;
    const int64_t kDuration = (58 * 60 + 12) * 1000LL;
    const int64_t kBytes = 642148761LL;   //612.4 MB
    CHECK(CloudRowSubText(t, cloudrecord_source_share, false, kDuration, kBytes) ==
          L"2026-10-06 14:20 · 共享录制 · 58:12 · 612.4 MB");
    //录音、本机上传；还没合成的没有大小，还没录完的没有时长
    CHECK(CloudRowSubText(t, cloudrecord_source_upload, true, kDuration, kBytes) ==
          L"2026-10-06 14:20 · 本机上传 · 录音 · 58:12 · 612.4 MB");
    CHECK(CloudRowSubText(t, cloudrecord_source_share, false, kDuration, 0) == L"2026-10-06 14:20 · 共享录制 · 58:12");
    CHECK(CloudRowSubText(t, cloudrecord_source_share, false, 0, 0) == L"2026-10-06 14:20 · 共享录制");

    CHECK(CloudRowActions(cloud_row_ready) ==
          (cloud_action_play | cloud_action_download | cloud_action_send | cloud_action_rename | cloud_action_delete));
    CHECK(CloudRowActions(cloud_row_recording) == cloud_action_rename);
    CHECK(CloudRowActions(cloud_row_compose_failed) == (cloud_action_recompose | cloud_action_rename | cloud_action_delete));
    CHECK(CloudRowActions(cloud_row_pending) == (cloud_action_rename | cloud_action_delete));
    //2026-10-07 起合成中、上传中可以暂停（方案第 14.4 节），见 CRM7
    CHECK(CloudRowActions(cloud_row_composing) == (cloud_action_pause | cloud_action_rename | cloud_action_delete));
    CHECK(CloudRowActions(cloud_row_record_failed) == (cloud_action_rename | cloud_action_delete));
    CHECK(CloudRowActions(cloud_row_upload_stalled) == (cloud_action_rename | cloud_action_delete));
    //只有可观看的能播放、下载、发送
    CHECK((CloudRowActions(cloud_row_uploading) & (cloud_action_play | cloud_action_download | cloud_action_send)) == 0);
}

TEST_CASE("CRM4 cloud usage text, warning and bar", "[cloudrec]")
{
    using namespace screenrecord;
    const int64_t kGb = 1024LL * 1024 * 1024;
    const int64_t kUsed = 12992000000LL;   //12.1 GB
    CHECK(CloudUsageText(true, kUsed, 50 * kGb, 30) == L"已用 12.1 GB / 共 50 GB · 保留 30 天");
    CHECK(CloudUsageText(true, 0, 50 * kGb, 0) == L"已用 0 B / 共 50 GB · 永久保留");
    CHECK(CloudUsageText(false, kUsed, 50 * kGb, 30) == L"管理员没有开通云端录制");

    CHECK_FALSE(CloudUsageWarn(true, kUsed, 50 * kGb));
    CHECK(CloudUsageWarn(true, 50 * kGb, 50 * kGb));
    CHECK(CloudUsageWarn(true, 0, 0));
    CHECK(CloudUsageWarn(false, 0, 50 * kGb));

    CHECK(CloudUsagePermille(0, 50 * kGb) == 0);
    CHECK(CloudUsagePermille(25 * kGb, 50 * kGb) == 500);
    CHECK(CloudUsagePermille(60 * kGb, 50 * kGb) == 1000);
    CHECK(CloudUsagePermille(1, 0) == 1000);
}

TEST_CASE("CRM5 cloud record title on rename", "[cloudrec]")
{
    using namespace screenrecord;
    std::wstring out;
    CHECK(NormalizeCloudTitle(L"周会", L"  产品周会  ", out) == cloud_title_ok);
    CHECK(out == L"产品周会");
    CHECK(NormalizeCloudTitle(L"周会", L" 周会 ", out) == cloud_title_unchanged);
    CHECK(NormalizeCloudTitle(L"周会", L"   ", out) == cloud_title_empty);
    CHECK(NormalizeCloudTitle(L"周会", L"", out) == cloud_title_empty);
    //50 个字可以，51 个字不行
    const std::wstring fifty(kCloudTitleMaxChars, L'录');
    CHECK(NormalizeCloudTitle(L"周会", fifty, out) == cloud_title_ok);
    CHECK(NormalizeCloudTitle(L"周会", fifty + L"像", out) == cloud_title_too_long);
    //代理对算一个字：49 个汉字加一个表情是 50 个字
    std::wstring withEmoji(kCloudTitleMaxChars - 1, L'录');
    withEmoji += L"\U0001F600";
    CHECK(NormalizeCloudTitle(L"周会", withEmoji, out) == cloud_title_ok);
}

TEST_CASE("CRM6 cloud trailing column rect", "[cloudrec]")
{
    using namespace screenrecord;
    const RECT list = { 100, 50, 600, 250 };   //列表 500×200，行高 40
    const int kRowH = 40;
    const int kWidth = 110;
    const int kInset = 11;
    RECT rc;
    //第一行：贴着右边让出命中带
    REQUIRE(CloudTrailingRect(list, 0, kRowH, 0, kWidth, kInset, rc));
    CHECK(rc.right == 600 - kInset);
    CHECK(rc.left == 600 - kInset - kWidth);
    CHECK(rc.top == 50);
    CHECK(rc.bottom == 90);
    //滚动后：按滚动位置上移；只露出一部分的行同样画
    REQUIRE(CloudTrailingRect(list, 2, kRowH, 60, kWidth, kInset, rc));
    CHECK(rc.top == 50 + 80 - 60);
    REQUIRE(CloudTrailingRect(list, 0, kRowH, 20, kWidth, kInset, rc));
    CHECK(rc.top == 30);
    //完全滚出上边或下边：不画
    CHECK_FALSE(CloudTrailingRect(list, 0, kRowH, 40, kWidth, kInset, rc));
    CHECK_FALSE(CloudTrailingRect(list, 5, kRowH, 0, kWidth, kInset, rc));
    //参数非法、列表太窄：不画
    CHECK_FALSE(CloudTrailingRect(list, -1, kRowH, 0, kWidth, kInset, rc));
    CHECK_FALSE(CloudTrailingRect(list, 0, 0, 0, kWidth, kInset, rc));
    CHECK_FALSE(CloudTrailingRect(list, 0, kRowH, 0, 0, kInset, rc));
    const RECT narrow = { 0, 0, 100, 200 };
    CHECK_FALSE(CloudTrailingRect(narrow, 0, kRowH, 0, kWidth, kInset, rc));
}

TEST_CASE("CRM7 paused compose and upload rows", "[cloudrec]")
{
    //2026-10-07（方案第 14.4 节）：合成、上传阶段可以暂停，之后点「继续」接着做；暂停的不随登录自动继续
    using namespace screenrecord;
    CloudRowLocal paused;
    paused.m_paused = true;
    CHECK(CloudRowStateOf(cloudrecord_status_pending, paused) == cloud_row_paused);
    CHECK(CloudRowStateOf(cloudrecord_status_uploading, paused) == cloud_row_paused);
    //已可观看、录制中、录制失败的不受暂停影响
    CHECK(CloudRowStateOf(cloudrecord_status_ready, paused) == cloud_row_ready);
    CHECK(CloudRowStateOf(cloudrecord_status_recording, paused) == cloud_row_recording);
    CHECK(CloudRowStateOf(cloudrecord_status_failed, paused) == cloud_row_record_failed);
    CHECK(CloudRowStateText(cloud_row_paused, -1, -1) == std::wstring(TR(L"已暂停")));
    //颜色与等待合成相同（灰），不画进度条
    CHECK(CloudRowStateColor(cloud_row_paused, -1) == CloudRowStateColor(cloud_row_pending, -1));
    CHECK_FALSE(CloudRowShowsProgress(cloud_row_paused, 40));
    //可做的操作：暂停的可以继续；合成中、上传中可以暂停；其余没有这两项
    CHECK(CloudRowActions(cloud_row_paused) == (cloud_action_resume | cloud_action_rename | cloud_action_delete));
    CHECK((CloudRowActions(cloud_row_uploading) & cloud_action_pause) != 0);
    CHECK((CloudRowActions(cloud_row_composing) & cloud_action_pause) != 0);
    CHECK((CloudRowActions(cloud_row_ready) & (cloud_action_pause | cloud_action_resume)) == 0);
    CHECK((CloudRowActions(cloud_row_pending) & (cloud_action_pause | cloud_action_resume)) == 0);
}

TEST_CASE("CRM8 paused records list round trip", "[cloudrec]")
{
    using namespace screenrecord;
    std::set<int64_t> ids;
    CHECK(EncodePausedCloudRecords(ids).empty() == false);
    std::set<int64_t> back;
    REQUIRE(DecodePausedCloudRecords(EncodePausedCloudRecords(ids), back));
    CHECK(back.empty());
    ids.insert(12);
    ids.insert(9007199254740993LL);
    ids.insert(3);
    REQUIRE(DecodePausedCloudRecords(EncodePausedCloudRecords(ids), back));
    CHECK(back == ids);
    //坏数据：读不出，出参为空
    CHECK_FALSE(DecodePausedCloudRecords("not json", back));
    CHECK(back.empty());
    CHECK_FALSE(DecodePausedCloudRecords("{\"paused\":\"x\"}", back));
}

TEST_CASE("CFP3 pending cloud uploads list and resume decision", "[cloudrec]")
{
    using namespace screenrecord;
    std::vector<PendingCloudUpload> list(2);
    list[0].m_recordId = 42;
    list[0].m_pathUtf8 = u8"D:\\录像\\周会.mp4";
    list[0].m_bytes = 1048576;
    list[0].m_audioOnly = false;
    list[0].m_durationMs = 60000;
    list[0].m_startTime = 1790000000000LL;
    list[0].m_title = u8"周会";
    list[1].m_recordId = 0;   //没有录像 id 的不写
    list[1].m_pathUtf8 = "x";
    const std::string json = BuildPendingUploadsJson(list);
    std::vector<PendingCloudUpload> back;
    REQUIRE(ParsePendingUploadsJson(json, back));
    REQUIRE(back.size() == 1);
    CHECK(back[0].m_recordId == 42);
    CHECK(back[0].m_pathUtf8 == list[0].m_pathUtf8);
    CHECK(back[0].m_bytes == 1048576);
    CHECK_FALSE(back[0].m_audioOnly);
    CHECK(back[0].m_durationMs == 60000);
    CHECK(back[0].m_startTime == 1790000000000LL);
    CHECK(back[0].m_title == u8"周会");

    //空清单、缺字段的条目、不是 JSON
    REQUIRE(ParsePendingUploadsJson("{\"uploads\":[]}", back));
    CHECK(back.empty());
    REQUIRE(ParsePendingUploadsJson("{\"uploads\":[{\"recordid\":7},{\"path\":\"a\"},{\"recordid\":8,\"path\":\"b\"}]}", back));
    REQUIRE(back.size() == 1);
    CHECK(back[0].m_recordId == 8);
    CHECK_FALSE(ParsePendingUploadsJson("not json", back));
    CHECK_FALSE(ParsePendingUploadsJson("{\"x\":1}", back));

    //续传判断：服务端仍是上传中、文件没变才续传
    CHECK(DecidePendingResume(true, cloudrecord_status_uploading, true, true) == pending_resume);
    CHECK(DecidePendingResume(true, cloudrecord_status_uploading, false, true) == pending_resume);
    CHECK(DecidePendingResume(true, cloudrecord_status_uploading, true, false) == pending_drop);
    CHECK(DecidePendingResume(true, cloudrecord_status_ready, true, true) == pending_drop);
    CHECK(DecidePendingResume(true, cloudrecord_status_failed, true, true) == pending_drop);
    //列表里没有：读全了就去掉，还有更早的就先留着
    CHECK(DecidePendingResume(false, 0, true, true) == pending_drop);
    CHECK(DecidePendingResume(false, 0, false, true) == pending_keep);
}
