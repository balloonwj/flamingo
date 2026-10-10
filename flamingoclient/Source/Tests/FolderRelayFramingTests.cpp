/**
 *  文件夹传输应用帧编解码纯逻辑单元测试（Suite FTF），FolderRelayFramingTests.cpp
 *  覆盖：
 *    - 往返一致：九种帧各自「编码 → 解码」后字段逐个还原，分片帧的二进制附件
 *      （含 0x00 与高位字节）按原样还原；
 *    - 零拷贝解码：DecodeFileChunk 回传的偏移与长度与 DecodeFrame 拷出的内容一致；
 *    - 单帧上限：编码侧超过 2 MiB 的分片被拒绝，解码侧超过 2 MiB 的包体被拒绝；
 *    - 清单分段：小预算下拆成多段、段序号与起始序号连续、逐段重组还原整份清单，
 *      空清单产出恰好一段，真实预算下每帧都在上限内；
 *    - 重组的健壮性：总计字段矛盾、起始序号对不上一律报错并重置；容忍发送端重发清单
 *      （2026-10-05 起）：缺了开头的一份与跳号的一份丢弃后等下一份，0 号段从头再来，
 *      收齐后多余的整份丢弃；
 *    - 安全失败：截断的字节串（逐个前缀长度）、非法 JSON、未知帧类型、附件长度
 *      与描述符不符、非分片帧夹带附件，一律返回 false 且不崩溃。
 *  框架 Catch2 v3。本组为纯逻辑，不触及 socket。
 *  balloonwj@qq.com   2026-08-13
 */
#include "catch_amalgamated.hpp"
#include "../Feature/FolderTransfer/FolderRelayFraming.h"

#include <string.h>   // memcmp
#include <string>
#include <vector>

using namespace foldertransfer;

//测试里手工拼一个应用帧：[1 字节帧类型][4 字节大端描述符长度][描述符][附件]。
//独立于产品代码另写一遍布局，等于顺带核对了头文件里写明的字节布局。
static std::string makeRawFrame(unsigned char type, const std::string& descriptor,
                                const std::string& binary)
{
    std::string out;
    out.push_back(static_cast<char>(type));

    unsigned int len = static_cast<unsigned int>(descriptor.size());
    out.push_back(static_cast<char>((len >> 24) & 0xFF));
    out.push_back(static_cast<char>((len >> 16) & 0xFF));
    out.push_back(static_cast<char>((len >> 8) & 0xFF));
    out.push_back(static_cast<char>(len & 0xFF));

    out += descriptor;
    out += binary;
    return out;
}

//把 std::string 形态的应用帧交给 DecodeFrame，省去每处都写一遍指针转换
static bool decodeString(const std::string& frame, FolderRelayInboundFrame& out)
{
    return relayframing::DecodeFrame(reinterpret_cast<const unsigned char*>(frame.data()),
                                     frame.size(), out);
}

//造一份规模可控的清单：dirCount 条目录、fileCount 个文件，路径带中文以覆盖
//jsoncpp 把非 ASCII 转义成 \uXXXX 之后的长度膨胀
static void makeManifest(size_t dirCount, size_t fileCount,
                         std::vector<std::string>& dirs,
                         std::vector<FolderRelayFileEntry>& files)
{
    dirs.clear();
    files.clear();
    for (size_t i = 0; i < dirCount; ++i)
        dirs.push_back("子目录" + std::to_string(i) + "/深层");

    for (size_t i = 0; i < fileCount; ++i)
    {
        FolderRelayFileEntry e;
        e.m_relpath = "子目录" + std::to_string(i % (dirCount + 1)) + "/文件" + std::to_string(i) + ".txt";
        e.m_size = static_cast<int64_t>(i) * 1024 + 1;
        files.push_back(e);
    }
}

TEST_CASE("FTF1 清单单段：编码→解码往返一致")
{
    FolderRelayManifestSegment seg;
    seg.m_foldername = "项目资料";
    seg.m_totaldirs  = 2;
    seg.m_totalfiles = 2;
    seg.m_totalsize  = 300;
    seg.m_segindex   = 0;
    seg.m_segcount   = 1;
    seg.m_dirbase    = 0;
    seg.m_filebase   = 0;
    seg.m_dirs.push_back("sub");
    seg.m_dirs.push_back("sub/deep");

    FolderRelayFileEntry f1;
    f1.m_relpath = "sub/a.txt";
    f1.m_size = 100;
    FolderRelayFileEntry f2;
    f2.m_relpath = "sub/deep/中文 名.bin";
    f2.m_size = 200;
    seg.m_files.push_back(f1);
    seg.m_files.push_back(f2);

    std::string frame;
    REQUIRE(relayframing::BuildManifestFrame(seg, frame));

    folder_frame_type type = ft_invalid;
    REQUIRE(relayframing::PeekFrameType(reinterpret_cast<const unsigned char*>(frame.data()),
                                        frame.size(), type));
    CHECK(type == ft_manifest);

    FolderRelayInboundFrame got;
    REQUIRE(decodeString(frame, got));
    REQUIRE(got.m_type == ft_manifest);
    CHECK(got.m_manifest.m_foldername == "项目资料");
    CHECK(got.m_manifest.m_totaldirs == 2);
    CHECK(got.m_manifest.m_totalfiles == 2);
    CHECK(got.m_manifest.m_totalsize == 300);
    CHECK(got.m_manifest.m_segindex == 0);
    CHECK(got.m_manifest.m_segcount == 1);
    CHECK(got.m_manifest.m_dirbase == 0);
    CHECK(got.m_manifest.m_filebase == 0);
    REQUIRE(got.m_manifest.m_dirs.size() == 2);
    CHECK(got.m_manifest.m_dirs[0] == "sub");
    CHECK(got.m_manifest.m_dirs[1] == "sub/deep");
    REQUIRE(got.m_manifest.m_files.size() == 2);
    CHECK(got.m_manifest.m_files[0].m_relpath == "sub/a.txt");
    CHECK(got.m_manifest.m_files[0].m_size == 100);
    CHECK(got.m_manifest.m_files[1].m_relpath == "sub/deep/中文 名.bin");
    CHECK(got.m_manifest.m_files[1].m_size == 200);
}

TEST_CASE("FTF2 清单回执：稀疏续传位点往返一致")
{
    FolderRelayManifestAck ack;
    ack.m_totalfiles = 5;
    FolderRelayResumePoint p1;
    p1.m_index = 1;
    p1.m_offset = 4096;
    FolderRelayResumePoint p2;
    p2.m_index = 4;
    //大于 32 位的偏移，确认 int64 不被截断
    p2.m_offset = 5368709120LL;
    ack.m_points.push_back(p1);
    ack.m_points.push_back(p2);

    std::string frame;
    REQUIRE(relayframing::BuildManifestAckFrame(ack, frame));

    FolderRelayInboundFrame got;
    REQUIRE(decodeString(frame, got));
    REQUIRE(got.m_type == ft_manifest_ack);
    CHECK(got.m_manifestAck.m_totalfiles == 5);
    REQUIRE(got.m_manifestAck.m_points.size() == 2);
    CHECK(got.m_manifestAck.m_points[0].m_index == 1);
    CHECK(got.m_manifestAck.m_points[0].m_offset == 4096);
    CHECK(got.m_manifestAck.m_points[1].m_index == 4);
    CHECK(got.m_manifestAck.m_points[1].m_offset == 5368709120LL);

    //首次传输：位点表为空，表示全部文件都从 0 开始
    FolderRelayManifestAck fresh;
    fresh.m_totalfiles = 3;
    std::string frame2;
    REQUIRE(relayframing::BuildManifestAckFrame(fresh, frame2));
    FolderRelayInboundFrame got2;
    REQUIRE(decodeString(frame2, got2));
    CHECK(got2.m_type == ft_manifest_ack);
    CHECK(got2.m_manifestAck.m_totalfiles == 3);
    CHECK(got2.m_manifestAck.m_points.empty());
}

TEST_CASE("FTF3 文件开始：往返一致，起传偏移越过文件末尾被拒绝")
{
    FolderRelayFileBegin begin;
    begin.m_index = 7;
    begin.m_size = 10737418240LL;   //10 GiB，验证 64 位不丢精度
    begin.m_md5 = "0123456789abcdef0123456789abcdef";
    begin.m_offset = 5368709120LL;

    std::string frame;
    REQUIRE(relayframing::BuildFileBeginFrame(begin, frame));

    FolderRelayInboundFrame got;
    REQUIRE(decodeString(frame, got));
    REQUIRE(got.m_type == ft_file_begin);
    CHECK(got.m_fileBegin.m_index == 7);
    CHECK(got.m_fileBegin.m_size == 10737418240LL);
    CHECK(got.m_fileBegin.m_md5 == "0123456789abcdef0123456789abcdef");
    CHECK(got.m_fileBegin.m_offset == 5368709120LL);

    //起传偏移大于文件大小属于自相矛盾，编码阶段就该拦下
    FolderRelayFileBegin bad;
    bad.m_index = 0;
    bad.m_size = 100;
    bad.m_offset = 101;
    std::string badframe;
    CHECK_FALSE(relayframing::BuildFileBeginFrame(bad, badframe));
    CHECK(badframe.empty());
}

TEST_CASE("FTF4 分片帧：二进制附件（含 0x00）按原样还原")
{
    //刻意混入 0x00、0xFF 与换行、引号等在文本处理里容易出事的字节
    unsigned char payload[] = { 0x00, 0xFF, 0x22, 0x0A, 0x00, 0x5C, 0x7F, 0x80, 0x00 };

    std::string frame;
    REQUIRE(relayframing::BuildFileChunkFrame(3, 1048576, payload, sizeof(payload), frame));

    FolderRelayInboundFrame got;
    REQUIRE(decodeString(frame, got));
    REQUIRE(got.m_type == ft_file_chunk);
    CHECK(got.m_fileChunk.m_index == 3);
    CHECK(got.m_fileChunk.m_offset == 1048576);
    CHECK(got.m_fileChunk.m_datalen == static_cast<int64_t>(sizeof(payload)));
    REQUIRE(got.m_chunkData.size() == sizeof(payload));
    CHECK(memcmp(got.m_chunkData.data(), payload, sizeof(payload)) == 0);

    //零字节分片（文件末尾恰好对齐时可能出现）也要能往返
    std::string emptyframe;
    REQUIRE(relayframing::BuildFileChunkFrame(3, 2097152, nullptr, 0, emptyframe));
    FolderRelayInboundFrame got2;
    REQUIRE(decodeString(emptyframe, got2));
    CHECK(got2.m_type == ft_file_chunk);
    CHECK(got2.m_fileChunk.m_datalen == 0);
    CHECK(got2.m_chunkData.empty());
}

TEST_CASE("FTF5 分片帧零拷贝解码：DecodeFileChunk 与 DecodeFrame 结果一致")
{
    std::vector<unsigned char> payload(4096);
    for (size_t i = 0; i < payload.size(); ++i)
        payload[i] = static_cast<unsigned char>((i * 37 + 11) & 0xFF);

    std::string frame;
    REQUIRE(relayframing::BuildFileChunkFrame(9, 524288, payload.data(), payload.size(), frame));

    FolderRelayFileChunk info;
    size_t dataOffset = 0;
    REQUIRE(relayframing::DecodeFileChunk(reinterpret_cast<const unsigned char*>(frame.data()),
                                          frame.size(), info, dataOffset));
    CHECK(info.m_index == 9);
    CHECK(info.m_offset == 524288);
    REQUIRE(info.m_datalen == static_cast<int64_t>(payload.size()));
    //偏移 + 长度必须落在帧内，且内容与原始数据逐字节相同
    REQUIRE(dataOffset + static_cast<size_t>(info.m_datalen) == frame.size());
    CHECK(memcmp(frame.data() + dataOffset, payload.data(), payload.size()) == 0);

    //非分片帧不能走这条路径
    std::string endframe;
    REQUIRE(relayframing::BuildFileEndFrame(9, endframe));
    FolderRelayFileChunk info2;
    size_t off2 = 0;
    CHECK_FALSE(relayframing::DecodeFileChunk(reinterpret_cast<const unsigned char*>(endframe.data()),
                                              endframe.size(), info2, off2));
}

TEST_CASE("FTF6 分片回执 / 文件结束：往返一致")
{
    FolderRelayChunkAck ack;
    ack.m_index = 12;
    ack.m_nextoffset = 1073741824LL;
    std::string frame;
    REQUIRE(relayframing::BuildChunkAckFrame(ack, frame));

    FolderRelayInboundFrame got;
    REQUIRE(decodeString(frame, got));
    REQUIRE(got.m_type == ft_chunk_ack);
    CHECK(got.m_chunkAck.m_index == 12);
    CHECK(got.m_chunkAck.m_nextoffset == 1073741824LL);

    std::string endframe;
    REQUIRE(relayframing::BuildFileEndFrame(12, endframe));
    FolderRelayInboundFrame got2;
    REQUIRE(decodeString(endframe, got2));
    REQUIRE(got2.m_type == ft_file_end);
    CHECK(got2.m_fileEnd.m_index == 12);

    //序号为负是调用方的错，编码阶段拦下
    std::string bad;
    CHECK_FALSE(relayframing::BuildFileEndFrame(-1, bad));
}

TEST_CASE("FTF7 文件结果 / 中止 / 完成：往返一致")
{
    FolderRelayFileResult result;
    result.m_index = 2;
    result.m_code = 617;
    result.m_msg = "md5 mismatch";
    std::string frame;
    REQUIRE(relayframing::BuildFileResultFrame(result, frame));

    FolderRelayInboundFrame got;
    REQUIRE(decodeString(frame, got));
    REQUIRE(got.m_type == ft_file_result);
    CHECK(got.m_fileResult.m_index == 2);
    CHECK(got.m_fileResult.m_code == 617);
    CHECK(got.m_fileResult.m_msg == "md5 mismatch");

    FolderRelayAbort abort;
    abort.m_errcode = 615;
    abort.m_errmsg = "接收端磁盘空间不足";
    std::string abortframe;
    REQUIRE(relayframing::BuildAbortFrame(abort, abortframe));
    FolderRelayInboundFrame got2;
    REQUIRE(decodeString(abortframe, got2));
    REQUIRE(got2.m_type == ft_abort);
    CHECK(got2.m_abort.m_errcode == 615);
    CHECK(got2.m_abort.m_errmsg == "接收端磁盘空间不足");

    FolderRelayComplete summary;
    summary.m_totalfiles = 12000;
    summary.m_totalsize = 53687091200LL;
    std::string completeframe;
    REQUIRE(relayframing::BuildCompleteFrame(summary, completeframe));
    FolderRelayInboundFrame got3;
    REQUIRE(decodeString(completeframe, got3));
    REQUIRE(got3.m_type == ft_complete);
    CHECK(got3.m_complete.m_totalfiles == 12000);
    CHECK(got3.m_complete.m_totalsize == 53687091200LL);

    //不带汇总的完成帧（两个字段均为 0）同样合法
    FolderRelayComplete blank;
    std::string blankframe;
    REQUIRE(relayframing::BuildCompleteFrame(blank, blankframe));
    FolderRelayInboundFrame got4;
    REQUIRE(decodeString(blankframe, got4));
    CHECK(got4.m_type == ft_complete);
    CHECK(got4.m_complete.m_totalfiles == 0);
    CHECK(got4.m_complete.m_totalsize == 0);
}

TEST_CASE("FTF8 单帧上限：编码侧拒绝超限分片，恰好压线的分片可用")
{
    //整整 2 MiB 的数据加上帧头与描述符必然超限
    std::vector<unsigned char> tooBig(kFolderRelayMaxBodyBytes, 0x5A);
    std::string frame;
    CHECK_FALSE(relayframing::BuildFileChunkFrame(0, 0, tooBig.data(), tooBig.size(), frame));
    CHECK(frame.empty());

    //一期实际使用的 512 KiB 分片远在上限之内
    std::vector<unsigned char> normal(kFolderRelayChunkBytes, 0x5A);
    std::string frame2;
    REQUIRE(relayframing::BuildFileChunkFrame(0, 0, normal.data(), normal.size(), frame2));
    CHECK(frame2.size() <= kFolderRelayMaxBodyBytes);

    FolderRelayInboundFrame got;
    REQUIRE(decodeString(frame2, got));
    CHECK(got.m_chunkData.size() == kFolderRelayChunkBytes);
}

TEST_CASE("FTF9 单帧上限：解码侧拒绝超过上限的包体")
{
    //构造一个字节数超过上限的"帧"，即便首字节是合法帧类型也不予解析
    std::string oversized = makeRawFrame(static_cast<unsigned char>(ft_file_chunk),
                                         "{\"index\":0,\"offset\":0,\"len\":0}",
                                         std::string(kFolderRelayMaxBodyBytes, 'x'));
    FolderRelayInboundFrame got;
    CHECK_FALSE(decodeString(oversized, got));
    CHECK(got.m_type == ft_invalid);
}

TEST_CASE("FTF10 清单分段：小预算下拆成多段，段序号与起始序号连续")
{
    std::vector<std::string> dirs;
    std::vector<FolderRelayFileEntry> files;
    makeManifest(20, 200, dirs, files);

    //预算取一个刚好放得下分段头部、又只够装少量条目的值，强制拆成多段
    const size_t kTinyBudget = 1024;
    std::vector<FolderRelayManifestSegment> segments;
    REQUIRE(relayframing::SplitManifest("项目资料", dirs, files, kTinyBudget, segments));
    REQUIRE(segments.size() > 1);

    int64_t expectDirBase = 0;
    int64_t expectFileBase = 0;
    for (size_t i = 0; i < segments.size(); ++i)
    {
        CHECK(segments[i].m_segindex == static_cast<int64_t>(i));
        CHECK(segments[i].m_segcount == static_cast<int64_t>(segments.size()));
        CHECK(segments[i].m_foldername == "项目资料");
        CHECK(segments[i].m_totaldirs == static_cast<int64_t>(dirs.size()));
        CHECK(segments[i].m_totalfiles == static_cast<int64_t>(files.size()));
        CHECK(segments[i].m_dirbase == expectDirBase);
        CHECK(segments[i].m_filebase == expectFileBase);
        expectDirBase += static_cast<int64_t>(segments[i].m_dirs.size());
        expectFileBase += static_cast<int64_t>(segments[i].m_files.size());
    }
    //全部分段合起来正好是整份清单，不重不漏
    CHECK(expectDirBase == static_cast<int64_t>(dirs.size()));
    CHECK(expectFileBase == static_cast<int64_t>(files.size()));
}

TEST_CASE("FTF11 清单分段：拆分→封帧→解码→重组，还原出整份清单")
{
    std::vector<std::string> dirs;
    std::vector<FolderRelayFileEntry> files;
    makeManifest(15, 120, dirs, files);

    int64_t expectTotalSize = 0;
    for (size_t i = 0; i < files.size(); ++i)
        expectTotalSize += files[i].m_size;

    const size_t kTinyBudget = 2048;
    std::vector<std::string> frames;
    REQUIRE(relayframing::BuildManifestFrames("项目资料", dirs, files, kTinyBudget, frames));
    REQUIRE(frames.size() > 1);

    FolderRelayManifestAssembler assembler;
    for (size_t i = 0; i < frames.size(); ++i)
    {
        //每个分段帧都必须在中转单帧上限之内，否则根本发不出去
        CHECK(frames[i].size() <= kFolderRelayMaxBodyBytes);

        FolderRelayInboundFrame got;
        REQUIRE(decodeString(frames[i], got));
        REQUIRE(got.m_type == ft_manifest);

        FolderRelayManifestAssembler::FeedResult r = assembler.Feed(got.m_manifest);
        if (i + 1 < frames.size())
            REQUIRE(r == FolderRelayManifestAssembler::folder_manifest_need_more);
        else
            REQUIRE(r == FolderRelayManifestAssembler::folder_manifest_done);
    }

    REQUIRE(assembler.IsComplete());
    CHECK(assembler.GetFolderName() == "项目资料");
    CHECK(assembler.GetTotalSize() == expectTotalSize);
    REQUIRE(assembler.GetDirs().size() == dirs.size());
    REQUIRE(assembler.GetFiles().size() == files.size());
    for (size_t i = 0; i < dirs.size(); ++i)
        CHECK(assembler.GetDirs()[i] == dirs[i]);
    for (size_t i = 0; i < files.size(); ++i)
    {
        CHECK(assembler.GetFiles()[i].m_relpath == files[i].m_relpath);
        CHECK(assembler.GetFiles()[i].m_size == files[i].m_size);
    }
}

TEST_CASE("FTF12 清单分段：真实预算下大清单每帧都在上限内且重组一致")
{
    //一万个文件是方案里 foldermaxfiles 的建议默认值，按真实预算切分
    std::vector<std::string> dirs;
    std::vector<FolderRelayFileEntry> files;
    makeManifest(500, 10000, dirs, files);

    std::vector<std::string> frames;
    REQUIRE(relayframing::BuildManifestFrames("项目资料", dirs, files,
                                              kFolderRelayManifestSegmentBytes, frames));
    REQUIRE(frames.size() >= 1);

    FolderRelayManifestAssembler assembler;
    FolderRelayManifestAssembler::FeedResult last =
        FolderRelayManifestAssembler::folder_manifest_error;
    for (size_t i = 0; i < frames.size(); ++i)
    {
        //切分预算远小于单帧上限，故每帧都应有充足余量
        CHECK(frames[i].size() <= kFolderRelayManifestSegmentBytes);

        FolderRelayInboundFrame got;
        REQUIRE(decodeString(frames[i], got));
        last = assembler.Feed(got.m_manifest);
    }
    REQUIRE(last == FolderRelayManifestAssembler::folder_manifest_done);
    CHECK(assembler.GetDirs().size() == dirs.size());
    CHECK(assembler.GetFiles().size() == files.size());
}

TEST_CASE("FTF13 清单分段：空清单产出恰好一段，重组后为空文件夹")
{
    std::vector<std::string> dirs;
    std::vector<FolderRelayFileEntry> files;

    std::vector<std::string> frames;
    REQUIRE(relayframing::BuildManifestFrames("空文件夹", dirs, files,
                                              kFolderRelayManifestSegmentBytes, frames));
    REQUIRE(frames.size() == 1);

    FolderRelayInboundFrame got;
    REQUIRE(decodeString(frames[0], got));
    REQUIRE(got.m_type == ft_manifest);
    CHECK(got.m_manifest.m_segcount == 1);
    CHECK(got.m_manifest.m_totaldirs == 0);
    CHECK(got.m_manifest.m_totalfiles == 0);

    FolderRelayManifestAssembler assembler;
    REQUIRE(assembler.Feed(got.m_manifest) == FolderRelayManifestAssembler::folder_manifest_done);
    CHECK(assembler.GetFolderName() == "空文件夹");
    CHECK(assembler.GetDirs().empty());
    CHECK(assembler.GetFiles().empty());
    CHECK(assembler.GetTotalSize() == 0);
}

TEST_CASE("FTF14 清单分段：预算小到装不下分段头部时如实失败")
{
    std::vector<std::string> dirs;
    std::vector<FolderRelayFileEntry> files;
    makeManifest(2, 2, dirs, files);

    std::vector<FolderRelayManifestSegment> segments;
    //16 字节连固定字段都写不下
    CHECK_FALSE(relayframing::SplitManifest("项目资料", dirs, files, 16, segments));
    CHECK(segments.empty());

    std::vector<std::string> frames;
    CHECK_FALSE(relayframing::BuildManifestFrames("项目资料", dirs, files, 16, frames));
    CHECK(frames.empty());
}

namespace {

//把一整份清单的分段依次喂进重组器，返回最后一段的结果
FolderRelayManifestAssembler::FeedResult feedAll(FolderRelayManifestAssembler& a,
                                                 const std::vector<FolderRelayManifestSegment>& segments)
{
    FolderRelayManifestAssembler::FeedResult last = FolderRelayManifestAssembler::folder_manifest_error;
    for (size_t i = 0; i < segments.size(); ++i)
        last = a.Feed(segments[i]);
    return last;
}

} // namespace

//2026-10-05 之前这里要求「首段不是 0 号、跳号、重复、收齐后再来一段」一律报错，前提是中转通道有序可靠；
//实际上 fileserver 在两端都报到之前收到的帧直接丢弃，发送端因此要在回执到达前重发清单，重组器改为容忍重发
TEST_CASE("FTF15 清单重组：容忍重发——缺了开头的一份丢弃、跳号丢弃后等下一份、0 号段从头再来、收齐后多余的整份丢弃")
{
    std::vector<std::string> dirs;
    std::vector<FolderRelayFileEntry> files;
    makeManifest(10, 60, dirs, files);

    std::vector<FolderRelayManifestSegment> segments;
    REQUIRE(relayframing::SplitManifest("项目资料", dirs, files, 1024, segments));
    REQUIRE(segments.size() >= 3);

    //首段不是 0 号：这一份的开头被 fileserver 丢了，丢弃本段、继续等；随后重发的完整一份照常收齐
    {
        FolderRelayManifestAssembler a;
        CHECK(a.Feed(segments[1]) == FolderRelayManifestAssembler::folder_manifest_ignored);
        CHECK_FALSE(a.IsComplete());
        CHECK(a.GetDirs().empty());
        CHECK(feedAll(a, segments) == FolderRelayManifestAssembler::folder_manifest_done);
        CHECK(a.GetDirs().size() == dirs.size());
        CHECK(a.GetFiles().size() == files.size());
    }

    //跳号：0 号之后直接来 2 号，这一份中间断了。丢弃已累积的部分，等下一份
    {
        FolderRelayManifestAssembler a;
        REQUIRE(a.Feed(segments[0]) == FolderRelayManifestAssembler::folder_manifest_need_more);
        CHECK(a.Feed(segments[2]) == FolderRelayManifestAssembler::folder_manifest_ignored);
        CHECK(a.GetDirs().empty());
        CHECK(a.GetFiles().empty());
        CHECK(feedAll(a, segments) == FolderRelayManifestAssembler::folder_manifest_done);
        CHECK(a.GetFiles().size() == files.size());
    }

    //又来一个 0 号段：发送端开始重发了，从头重新累积，不报错，也不会把两份叠在一起
    {
        FolderRelayManifestAssembler a;
        REQUIRE(a.Feed(segments[0]) == FolderRelayManifestAssembler::folder_manifest_need_more);
        CHECK(a.Feed(segments[0]) == FolderRelayManifestAssembler::folder_manifest_need_more);
        for (size_t i = 1; i + 1 < segments.size(); ++i)
            REQUIRE(a.Feed(segments[i]) == FolderRelayManifestAssembler::folder_manifest_need_more);
        CHECK(a.Feed(segments[segments.size() - 1]) == FolderRelayManifestAssembler::folder_manifest_done);
        CHECK(a.GetDirs().size() == dirs.size());
        CHECK(a.GetFiles().size() == files.size());
    }

    //收齐之后又来一整份：逐段丢弃，已收齐的结果不受影响
    {
        FolderRelayManifestAssembler a;
        REQUIRE(feedAll(a, segments) == FolderRelayManifestAssembler::folder_manifest_done);
        for (size_t i = 0; i < segments.size(); ++i)
            CHECK(a.Feed(segments[i]) == FolderRelayManifestAssembler::folder_manifest_ignored);
        CHECK(a.IsComplete());
        CHECK(a.GetDirs().size() == dirs.size());
        CHECK(a.GetFiles().size() == files.size());
    }
}

TEST_CASE("FTF16 清单重组：段间字段矛盾、起始序号对不上、总计与实际不符，一律报错")
{
    std::vector<std::string> dirs;
    std::vector<FolderRelayFileEntry> files;
    makeManifest(6, 40, dirs, files);

    std::vector<FolderRelayManifestSegment> segments;
    REQUIRE(relayframing::SplitManifest("项目资料", dirs, files, 1024, segments));
    REQUIRE(segments.size() >= 2);

    //后续段的文件夹名与第一段不一致
    {
        FolderRelayManifestAssembler a;
        REQUIRE(a.Feed(segments[0]) == FolderRelayManifestAssembler::folder_manifest_need_more);
        FolderRelayManifestSegment tampered = segments[1];
        tampered.m_foldername = "另一个文件夹";
        CHECK(a.Feed(tampered) == FolderRelayManifestAssembler::folder_manifest_error);
    }

    //后续段的起始序号与已累积条数对不上
    {
        FolderRelayManifestAssembler a;
        REQUIRE(a.Feed(segments[0]) == FolderRelayManifestAssembler::folder_manifest_need_more);
        FolderRelayManifestSegment tampered = segments[1];
        tampered.m_filebase += 1;
        CHECK(a.Feed(tampered) == FolderRelayManifestAssembler::folder_manifest_error);
    }

    //单段自身就不自洽：段号越出段数范围
    {
        FolderRelayManifestAssembler a;
        FolderRelayManifestSegment tampered = segments[0];
        tampered.m_segindex = tampered.m_segcount;
        CHECK(a.Feed(tampered) == FolderRelayManifestAssembler::folder_manifest_error);
    }

    //总计字节数与逐条累加对不上：末段收齐时才暴露
    {
        std::vector<FolderRelayManifestSegment> tampered = segments;
        for (size_t i = 0; i < tampered.size(); ++i)
            tampered[i].m_totalsize += 1;

        FolderRelayManifestAssembler a;
        FolderRelayManifestAssembler::FeedResult r =
            FolderRelayManifestAssembler::folder_manifest_error;
        for (size_t i = 0; i < tampered.size(); ++i)
            r = a.Feed(tampered[i]);
        CHECK(r == FolderRelayManifestAssembler::folder_manifest_error);
    }
}

TEST_CASE("FTF17 安全失败：截断的字节串（逐个前缀长度）一律失败且不崩溃")
{
    //分片帧（带附件）与清单帧（不带附件）两条路径都要覆盖
    unsigned char payload[64];
    for (size_t i = 0; i < sizeof(payload); ++i)
        payload[i] = static_cast<unsigned char>(i);

    std::string chunkframe;
    REQUIRE(relayframing::BuildFileChunkFrame(1, 0, payload, sizeof(payload), chunkframe));

    std::vector<std::string> dirs;
    std::vector<FolderRelayFileEntry> files;
    makeManifest(3, 8, dirs, files);
    std::vector<std::string> manifestFrames;
    REQUIRE(relayframing::BuildManifestFrames("项目资料", dirs, files,
                                              kFolderRelayManifestSegmentBytes, manifestFrames));
    REQUIRE(manifestFrames.size() == 1);

    const std::string* cases[2] = { &chunkframe, &manifestFrames[0] };
    for (size_t c = 0; c < 2; ++c)
    {
        const std::string& full = *cases[c];
        for (size_t n = 0; n < full.size(); ++n)
        {
            FolderRelayInboundFrame got;
            CHECK_FALSE(relayframing::DecodeFrame(
                reinterpret_cast<const unsigned char*>(full.data()), n, got));
            CHECK(got.m_type == ft_invalid);
        }
        //完整长度当然要成功，作为上面循环的对照
        FolderRelayInboundFrame ok;
        CHECK(decodeString(full, ok));
    }

    //空指针与零长度
    FolderRelayInboundFrame got;
    CHECK_FALSE(relayframing::DecodeFrame(nullptr, 0, got));
    CHECK_FALSE(relayframing::DecodeFrame(nullptr, 100, got));
    folder_frame_type type = ft_manifest;
    CHECK_FALSE(relayframing::PeekFrameType(nullptr, 0, type));
    CHECK(type == ft_invalid);
}

TEST_CASE("FTF18 安全失败：非法 JSON 与描述符长度越界")
{
    //描述符不是合法 JSON
    std::string bad1 = makeRawFrame(static_cast<unsigned char>(ft_file_end), "{not json", "");
    FolderRelayInboundFrame got;
    CHECK_FALSE(decodeString(bad1, got));

    //描述符是合法 JSON 但顶层不是对象
    std::string bad2 = makeRawFrame(static_cast<unsigned char>(ft_file_end), "[1,2,3]", "");
    CHECK_FALSE(decodeString(bad2, got));

    //描述符为空串
    std::string bad3 = makeRawFrame(static_cast<unsigned char>(ft_abort), "", "");
    CHECK_FALSE(decodeString(bad3, got));

    //描述符长度域自报 1000 字节，实际只有几个字节，越出包体范围
    std::string bad4;
    bad4.push_back(static_cast<char>(ft_file_end));
    bad4.push_back(static_cast<char>(0x00));
    bad4.push_back(static_cast<char>(0x00));
    bad4.push_back(static_cast<char>(0x03));
    bad4.push_back(static_cast<char>(0xE8));
    bad4 += "{\"index\":1}";
    CHECK_FALSE(decodeString(bad4, got));

    //清单帧缺少 segs 字段：无从判断该收几段，只能整帧判失败
    std::string bad5 = makeRawFrame(static_cast<unsigned char>(ft_manifest),
                                    "{\"name\":\"x\",\"totaldirs\":0,\"totalfiles\":0,"
                                    "\"totalsize\":0,\"seg\":0,\"dirbase\":0,\"filebase\":0}", "");
    CHECK_FALSE(decodeString(bad5, got));

    //清单帧的 files 元素类型不对
    std::string bad6 = makeRawFrame(static_cast<unsigned char>(ft_manifest),
                                    "{\"name\":\"x\",\"totaldirs\":0,\"totalfiles\":1,"
                                    "\"totalsize\":0,\"seg\":0,\"segs\":1,\"dirbase\":0,"
                                    "\"filebase\":0,\"files\":[123]}", "");
    CHECK_FALSE(decodeString(bad6, got));
}

TEST_CASE("FTF19 安全失败：未知帧类型")
{
    //0 是保留的非法值
    std::string bad1 = makeRawFrame(0, "{}", "");
    FolderRelayInboundFrame got;
    CHECK_FALSE(decodeString(bad1, got));

    //超出已定义帧类型上界
    std::string bad2 = makeRawFrame(static_cast<unsigned char>(ft_complete) + 1, "{}", "");
    CHECK_FALSE(decodeString(bad2, got));

    std::string bad3 = makeRawFrame(0xFF, "{}", "");
    CHECK_FALSE(decodeString(bad3, got));

    folder_frame_type type = ft_manifest;
    CHECK_FALSE(relayframing::PeekFrameType(reinterpret_cast<const unsigned char*>(bad3.data()),
                                            bad3.size(), type));
    CHECK(type == ft_invalid);
}

TEST_CASE("FTF20 安全失败：附件长度与描述符不符、非分片帧夹带附件")
{
    //描述符说有 8 字节附件，实际只给 4 字节
    std::string shortAttach = makeRawFrame(static_cast<unsigned char>(ft_file_chunk),
                                           "{\"index\":0,\"offset\":0,\"len\":8}", "abcd");
    FolderRelayInboundFrame got;
    CHECK_FALSE(decodeString(shortAttach, got));

    //描述符说有 4 字节附件，实际给了 8 字节
    std::string longAttach = makeRawFrame(static_cast<unsigned char>(ft_file_chunk),
                                          "{\"index\":0,\"offset\":0,\"len\":4}", "abcdefgh");
    CHECK_FALSE(decodeString(longAttach, got));

    //描述符干脆没有 len 字段
    std::string noLen = makeRawFrame(static_cast<unsigned char>(ft_file_chunk),
                                     "{\"index\":0,\"offset\":0}", "abcd");
    CHECK_FALSE(decodeString(noLen, got));

    //零拷贝解码路径同样要拦下长度不符的帧
    FolderRelayFileChunk info;
    size_t off = 0;
    CHECK_FALSE(relayframing::DecodeFileChunk(
        reinterpret_cast<const unsigned char*>(shortAttach.data()), shortAttach.size(), info, off));

    //非分片帧不允许夹带附件字节
    std::string extra = makeRawFrame(static_cast<unsigned char>(ft_file_end),
                                     "{\"index\":1}", std::string("\x00\x01", 2));
    CHECK_FALSE(decodeString(extra, got));

    //把合法分片帧尾部截掉一个字节，等价于附件被截断
    unsigned char payload[16];
    for (size_t i = 0; i < sizeof(payload); ++i)
        payload[i] = static_cast<unsigned char>(i + 1);
    std::string good;
    REQUIRE(relayframing::BuildFileChunkFrame(0, 0, payload, sizeof(payload), good));
    std::string truncated = good.substr(0, good.size() - 1);
    CHECK_FALSE(decodeString(truncated, got));
}
