/**
 *  语音消息的单元测试（前缀 VM，标签 [voicemsg]），用例清单见 docs/语音消息方案.md 第 9 节：
 *    - VM1 Ogg Opus 写入后读回：包的个数与内容一致，pre-skip、原始采样率、时长正确，文件头合法，
 *          最后一页按给定的有效采样数裁剪；文件写盘再读回内容不变（第 2a 步）；
 *    - VM2 读取损坏或截断的文件：截断、校验和不对、不是 OggS、不是 Opus、双声道、缺结束页都返回失败，
 *          不崩溃（第 2a 步）。
 *  不碰真实麦克风与扬声器。框架 Catch2 v3。
 *
 *  典型用法：FlamingoUnit.exe "[voicemsg]"
 *
 *  balloonwj@qq.com   2026-10-04
 */
#include "stdafx.h"
#include "catch_amalgamated.hpp"

#include <string.h>
#include <string>
#include <vector>

#include "../../FlamingoMedia/Voice/VoiceOggFile.h"

using namespace voicemsg;

namespace {

//测试用的 Ogg 逻辑流序列号与 pre-skip（48 kHz 计；libopus 16 kHz 语音编码器的 lookahead 104 × 3）
const uint32_t kVmSerial = 0x12345678u;
const int kVmPreSkip = 312;
//VM1 写入的音频包个数（2.4 秒）
const int kVmPacketCount = 120;
//Ogg 页头的固定部分长度、页头里各字段的偏移（RFC 3533 第 6 节）
const size_t kOggHeaderFixedLen = 27;
const size_t kOggHeaderTypeOffset = 5;
const size_t kOggCrcOffset = 22;
const size_t kOggSegmentCountOffset = 26;
//页头类型：第一页、最后一页
const uint8_t kOggBos = 0x02;
const uint8_t kOggEos = 0x04;
//OpusHead 里声道数字段相对包开头的偏移
const size_t kOpusHeadChannelsOffset = 9;

//第 i 个测试包：长度在 60、255、300 字节之间轮换（覆盖一个分段、正好 255 字节要补 0 长分段、跨两个分段），
//内容由序号决定，便于读回时逐字节比较
std::vector<uint8_t> makePacket(int i)
{
    const size_t kLens[] = { 60, 255, 300 };
    const size_t len = kLens[i % 3];
    std::vector<uint8_t> p(len);
    for (size_t k = 0; k < len; ++k)
        p[k] = static_cast<uint8_t>((i * 7 + k) & 0xFF);
    return p;
}

//写一份含 count 个测试包的完整文件；trim 为有效采样数（48 kHz 计），0 表示不裁
std::vector<uint8_t> buildFile(int count, int64_t trim)
{
    VoiceOggWriter writer;
    writer.Begin(kVmSerial, kVoiceSampleRate, kVmPreSkip);
    for (int i = 0; i < count; ++i)
    {
        std::vector<uint8_t> p = makePacket(i);
        writer.AddPacket(&p[0], p.size(), kOpusSamplesPerFrame48k);
    }
    writer.Finish(trim);
    return writer.Bytes();
}

//逐页走一遍文件，记下每页的起始偏移；页头不完整时停下
std::vector<size_t> pageOffsets(const std::vector<uint8_t>& bytes)
{
    std::vector<size_t> offsets;
    size_t pos = 0;
    while (pos + kOggHeaderFixedLen <= bytes.size())
    {
        const size_t segs = bytes[pos + kOggSegmentCountOffset];
        if (pos + kOggHeaderFixedLen + segs > bytes.size())
            break;
        size_t body = 0;
        for (size_t s = 0; s < segs; ++s)
            body += bytes[pos + kOggHeaderFixedLen + s];
        offsets.push_back(pos);
        pos += kOggHeaderFixedLen + segs + body;
    }
    return offsets;
}

//重新计算从 pageStart 开始的这一页的校验和并写回（测试里改了页内容之后用）
void refreshPageCrc(std::vector<uint8_t>& bytes, size_t pageStart)
{
    const size_t segs = bytes[pageStart + kOggSegmentCountOffset];
    size_t body = 0;
    for (size_t s = 0; s < segs; ++s)
        body += bytes[pageStart + kOggHeaderFixedLen + s];
    const size_t pageLen = kOggHeaderFixedLen + segs + body;
    memset(&bytes[pageStart + kOggCrcOffset], 0, 4);
    const uint32_t crc = OggCrc32(&bytes[pageStart], pageLen);
    for (int b = 0; b < 4; ++b)
        bytes[pageStart + kOggCrcOffset + b] = static_cast<uint8_t>((crc >> (8 * b)) & 0xFF);
}

} // namespace

TEST_CASE("VM1 Ogg Opus 写入后读回：包、pre-skip、采样率、时长一致，文件头合法", "[voicemsg]")
{
    const std::vector<uint8_t> bytes = buildFile(kVmPacketCount, 0);
    REQUIRE(bytes.size() > kOggHeaderFixedLen);

    //页结构：两个头页 + 每 kOggPacketsPerPage 个包一页；第一页带开始标志，最后一页带结束标志
    const std::vector<size_t> pages = pageOffsets(bytes);
    const size_t audioPages = (kVmPacketCount + kOggPacketsPerPage - 1) / kOggPacketsPerPage;
    REQUIRE(pages.size() == 2 + audioPages);
    CHECK(memcmp(&bytes[0], "OggS", 4) == 0);
    CHECK((bytes[kOggHeaderTypeOffset] & kOggBos) != 0);
    CHECK((bytes[pages.back() + kOggHeaderTypeOffset] & kOggEos) != 0);
    //第一页只有一个分段，OpusHead 紧跟在 28 字节的页头之后
    CHECK(memcmp(&bytes[kOggHeaderFixedLen + 1], "OpusHead", 8) == 0);

    VoiceOggReader reader;
    REQUIRE(reader.Parse(bytes));
    REQUIRE(reader.Packets().size() == static_cast<size_t>(kVmPacketCount));
    for (int i = 0; i < kVmPacketCount; ++i)
        CHECK(reader.Packets()[i] == makePacket(i));
    CHECK(reader.PreSkip48k() == kVmPreSkip);
    CHECK(reader.InputSampleRate() == static_cast<uint32_t>(kVoiceSampleRate));
    //granule 是解码出的总采样数（RFC 7845 第 4 节），有效采样数要减去开头丢弃的 pre-skip
    const int64_t decoded = static_cast<int64_t>(kVmPacketCount) * kOpusSamplesPerFrame48k;
    CHECK(reader.TotalSamples48k() == decoded - kVmPreSkip);
    CHECK(reader.DurationMs() == (decoded - kVmPreSkip) * kMillisPerSecond / kOpusGranuleRate);

    //裁掉最后一帧里补的静音：有效采样数按给定值计算
    const int64_t trimmed = decoded - kVmPreSkip - 500;
    VoiceOggReader trimmedReader;
    REQUIRE(trimmedReader.Parse(buildFile(kVmPacketCount, trimmed)));
    CHECK(trimmedReader.TotalSamples48k() == trimmed);

    //写盘再读回，内容不变
    wchar_t tempDir[MAX_PATH] = { 0 };
    REQUIRE(::GetTempPathW(MAX_PATH, tempDir) > 0);
    const std::wstring path = std::wstring(tempDir) + L"FlamingoUnit_VM1.ogg";
    REQUIRE(SaveBytesToFile(path, bytes));
    std::vector<uint8_t> loaded;
    const size_t kMaxBytes = 1024 * 1024;
    REQUIRE(LoadBytesFromFile(path, loaded, kMaxBytes));
    CHECK(loaded == bytes);
    //超过上限时拒绝读取
    CHECK_FALSE(LoadBytesFromFile(path, loaded, bytes.size() - 1));
    ::DeleteFileW(path.c_str());
}

TEST_CASE("VM2 读取损坏或截断的文件：返回失败，不崩溃", "[voicemsg]")
{
    const std::vector<uint8_t> good = buildFile(kVmPacketCount, 0);
    const std::vector<size_t> pages = pageOffsets(good);
    REQUIRE(pages.size() >= 3);
    VoiceOggReader reader;
    REQUIRE(reader.Parse(good));

    SECTION("空内容与随机字节")
    {
        CHECK_FALSE(reader.Parse(std::vector<uint8_t>()));
        CHECK(reader.Packets().empty());
        std::vector<uint8_t> noise(1000);
        for (size_t i = 0; i < noise.size(); ++i)
            noise[i] = static_cast<uint8_t>((i * 37 + 11) & 0xFF);
        CHECK_FALSE(reader.Parse(noise));
    }
    SECTION("截断：少了结尾几个字节、只剩半个页头、分段表声明的页体不存在")
    {
        std::vector<uint8_t> cut(good.begin(), good.end() - 10);
        CHECK_FALSE(reader.Parse(cut));
        std::vector<uint8_t> half(good.begin(), good.begin() + 20);
        CHECK_FALSE(reader.Parse(half));
        std::vector<uint8_t> noBody(good.begin(), good.begin() + kOggHeaderFixedLen + 1);
        CHECK_FALSE(reader.Parse(noBody));
    }
    SECTION("页体里改了一个字节，校验和对不上")
    {
        std::vector<uint8_t> bad = good;
        bad[pages[2] + kOggHeaderFixedLen + 20] ^= 0x5A;
        CHECK_FALSE(reader.Parse(bad));
    }
    SECTION("不是 OggS 开头")
    {
        std::vector<uint8_t> bad = good;
        bad[0] = 'X';
        CHECK_FALSE(reader.Parse(bad));
    }
    SECTION("第一个包不是 OpusHead（校验和已重新计算）")
    {
        std::vector<uint8_t> bad = good;
        bad[kOggHeaderFixedLen + 1 + 7] = 'X';
        refreshPageCrc(bad, 0);
        CHECK_FALSE(reader.Parse(bad));
    }
    SECTION("OpusHead 写的是双声道（校验和已重新计算）")
    {
        std::vector<uint8_t> bad = good;
        bad[kOggHeaderFixedLen + 1 + kOpusHeadChannelsOffset] = 2;
        refreshPageCrc(bad, 0);
        CHECK_FALSE(reader.Parse(bad));
    }
    SECTION("最后一页没有结束标志（校验和已重新计算）")
    {
        std::vector<uint8_t> bad = good;
        bad[pages.back() + kOggHeaderTypeOffset] = 0;
        refreshPageCrc(bad, pages.back());
        CHECK_FALSE(reader.Parse(bad));
    }
    SECTION("改坏之后，同一个读取器还能正常解析好的文件")
    {
        std::vector<uint8_t> bad = good;
        bad[0] = 'X';
        CHECK_FALSE(reader.Parse(bad));
        CHECK(reader.Parse(good));
        CHECK(reader.Packets().size() == static_cast<size_t>(kVmPacketCount));
    }
}
