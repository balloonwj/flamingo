/**
 *  文件共享控制通道应用帧单元测试（Suite SCF，标签 [shareframe]），ShareControlFramingTests.cpp
 *  覆盖（对应 docs/文件共享方案.md 第 1 期第 11 步的 11c，2026-10-02 与用户确认的用例）：
 *    - SCF1 sf_hello ~ sf_bye、st_job 每种帧编码后再解码与原来一致；
 *    - SCF2 截断、长度越界、类型未知、JSON 非法、缺必填字段、非缩略图帧带附件，解码一律失败、不崩溃；
 *    - SCF3 列目录与搜索结果按批拆分后每帧不超过 2 MiB、不超过每批条数，拼回去与原列表一致；
 *    - SCF8 搜索结果帧失败时带结果码、成功时不带（第 2 期第 3 步补充）；
 *    - SCF9 搜索请求帧的 all 与 max（列整棵目录树，第 2 期第 5 步补充）：带时往返一致，普通搜索不带；
 *    - SCF4 缩略图帧的二进制附件完整；
 *    - SCF5 帧类型取值与文件夹传输的 ft_* 不重叠；
 *    - SCF6 列目录按游标逐批构造（第 1 期第 12 步补充）：从 0 起逐批取得的帧与一次拆分的结果逐字相同，
 *           起点等于条目数时为空的末帧，起点越界失败；游标解析只接受空串与不超过 9 位的十进制数字；
 *    - SCF7 st_job_ack（第 1 期第 14 步补充）编码后再解码与原来一致；st_job 与 st_job_ack 的首字节都不属于文件夹传输的
 *           帧类型，文件夹传输状态机据此把它们交给扩展点。
 *  框架 Catch2 v3。
 *  balloonwj@qq.com   2026-10-02
 */
#include "stdafx.h"

#include "catch_amalgamated.hpp"

#include <string>
#include <vector>

#include "../Engine/ShareControlFraming.h"
#include "../../Source/Feature/FolderTransfer/FolderRelayFraming.h"
#include "../../Source/Feature/FolderTransfer/FolderTransferHooks.h"
#include "../../Source/net/Protocol.h"

using namespace fileshare;

namespace {

//一段 UTF-8 中文：「报告」
const char* const kChineseName = "\xE6\x8A\xA5\xE5\x91\x8A.docx";

//SCF3：长名字条目的个数与名字长度。每条约 5 KB，500 条就超过单帧上限，逼出按字节拆分（共约 10 MB）
const size_t kManyEntries = 2000;
const size_t kLongNameChars = 5000;

//SCF3：短名字条目的个数，按每批 500 条应拆成 500 / 500 / 200 三帧
const size_t kShortEntries = 1200;
const size_t kExpectedShortFrames = 3;
const size_t kLastShortFrameEntries = 200;

//SCF4：缩略图附件的字节数（含全部 256 种字节取值）
const size_t kThumbBytes = 100000;

//一种字节取值的个数，用来生成覆盖全部取值的附件
const int kByteValues = 256;

//几个常用的请求号、尺寸与权限
const int64_t kReqId = 12345678901LL;
const int64_t kBigSize = 5000000000LL;
const int64_t kMtime = 1759370000123LL;
const int kThumbEdge = 256;
const int kThumbWidth = 640;
const int kThumbHeight = 480;
const int kPerms = 5;
const int kKindFolder = 2;
const int kDirectionDownload = 1;
const int kErrPathInvalid = 808;

ShareEntry Entry(const std::string& name, bool isDir, int64_t size)
{
    ShareEntry e;
    e.m_name = name;
    e.m_isDir = isDir;
    e.m_size = size;
    e.m_mtime = kMtime;
    e.m_attrs = isDir ? 0 : 1;
    return e;
}

//编码再解码，要求两步都成功
ShareControlMessage RoundTrip(const ShareControlMessage& in)
{
    std::string body;
    REQUIRE(ShareControlFraming::BuildFrame(in, body));
    ShareControlMessage out;
    REQUIRE(ShareControlFraming::DecodeFrame(body.data(), body.size(), out));
    CHECK(out.m_type == in.m_type);
    return out;
}

//按「1 字节类型 + 4 字节大端长度 + 描述符」手工拼一个帧，用来构造畸形输入
std::string RawFrame(int type, const std::string& descriptor, const std::string& binary)
{
    std::string s;
    s.push_back(static_cast<char>(type));
    const uint32_t len = static_cast<uint32_t>(descriptor.size());
    s.push_back(static_cast<char>((len >> 24) & 0xFF));
    s.push_back(static_cast<char>((len >> 16) & 0xFF));
    s.push_back(static_cast<char>((len >> 8) & 0xFF));
    s.push_back(static_cast<char>(len & 0xFF));
    s += descriptor;
    s += binary;
    return s;
}

bool Decodes(const std::string& body)
{
    ShareControlMessage msg;
    return ShareControlFraming::DecodeFrame(body.data(), body.size(), msg);
}

} // namespace

TEST_CASE("SCF1 每种帧编码后再解码与原来一致", "[shareframe]")
{
    SECTION("sf_hello")
    {
        ShareControlMessage m;
        m.m_type = sf_hello;
        m.m_version = kShareControlVersion;
        m.m_kind = kKindFolder;
        m.m_rootName = kChineseName;
        m.m_includeHidden = true;
        m.m_perms = kPerms;
        const ShareControlMessage r = RoundTrip(m);
        CHECK(r.m_version == kShareControlVersion);
        CHECK(r.m_kind == kKindFolder);
        CHECK(r.m_rootName == kChineseName);
        CHECK(r.m_includeHidden);
        CHECK(r.m_perms == kPerms);
    }

    SECTION("sf_list 与 sf_list_result")
    {
        ShareControlMessage req;
        req.m_type = sf_list;
        req.m_reqId = kReqId;
        req.m_path = "docs/2026";
        req.m_cursor = "500";
        ShareControlMessage r = RoundTrip(req);
        CHECK(r.m_reqId == kReqId);
        CHECK(r.m_path == "docs/2026");
        CHECK(r.m_cursor == "500");

        ShareControlMessage res;
        res.m_type = sf_list_result;
        res.m_reqId = kReqId;
        res.m_code = 0;
        res.m_entries.push_back(Entry(kChineseName, false, kBigSize));
        res.m_entries.push_back(Entry("sub", true, 0));
        res.m_cursor = "502";
        res.m_more = true;
        r = RoundTrip(res);
        REQUIRE(r.m_entries.size() == 2);
        CHECK(r.m_entries[0].m_name == kChineseName);
        CHECK(r.m_entries[0].m_size == kBigSize);
        CHECK(r.m_entries[0].m_mtime == kMtime);
        CHECK(r.m_entries[0].m_attrs == 1);
        CHECK_FALSE(r.m_entries[0].m_isDir);
        CHECK(r.m_entries[1].m_isDir);
        CHECK(r.m_cursor == "502");
        CHECK(r.m_more);
    }

    SECTION("sf_stat 与 sf_stat_result")
    {
        ShareControlMessage req;
        req.m_type = sf_stat;
        req.m_reqId = 1;
        req.m_path = "";
        CHECK(RoundTrip(req).m_path.empty());

        ShareControlMessage res;
        res.m_type = sf_stat_result;
        res.m_reqId = 1;
        res.m_code = 0;
        res.m_entries.push_back(Entry("a.docx", false, kBigSize));
        const ShareControlMessage r = RoundTrip(res);
        REQUIRE(r.m_entries.size() == 1);
        CHECK(r.m_entries[0].m_name == "a.docx");

        //查不到时只回结果码，不带条目
        ShareControlMessage fail;
        fail.m_type = sf_stat_result;
        fail.m_reqId = 2;
        fail.m_code = kErrPathInvalid;
        CHECK(RoundTrip(fail).m_entries.empty());
    }

    SECTION("sf_search、sf_search_result、sf_cancel")
    {
        ShareControlMessage req;
        req.m_type = sf_search;
        req.m_reqId = 3;
        req.m_path = "docs";
        req.m_keyword = kChineseName;
        ShareControlMessage r = RoundTrip(req);
        CHECK(r.m_keyword == kChineseName);

        ShareControlMessage res;
        res.m_type = sf_search_result;
        res.m_reqId = 3;
        res.m_entries.push_back(Entry("docs/a/b.txt", false, 1));
        res.m_done = true;
        res.m_truncated = true;
        r = RoundTrip(res);
        REQUIRE(r.m_entries.size() == 1);
        CHECK(r.m_entries[0].m_name == "docs/a/b.txt");
        CHECK(r.m_done);
        CHECK(r.m_truncated);

        ShareControlMessage cancel;
        cancel.m_type = sf_cancel;
        cancel.m_reqId = 3;
        CHECK(RoundTrip(cancel).m_reqId == 3);
    }

    SECTION("sf_thumb")
    {
        ShareControlMessage req;
        req.m_type = sf_thumb;
        req.m_reqId = 4;
        req.m_path = "pics/a.jpg";
        req.m_maxEdge = kThumbEdge;
        CHECK(RoundTrip(req).m_maxEdge == kThumbEdge);
    }

    SECTION("写操作与结果")
    {
        ShareControlMessage mk;
        mk.m_type = sf_mkdir;
        mk.m_reqId = 5;
        mk.m_path = "docs/new";
        CHECK(RoundTrip(mk).m_path == "docs/new");

        ShareControlMessage rn;
        rn.m_type = sf_rename;
        rn.m_reqId = 6;
        rn.m_path = "docs/a.txt";
        rn.m_newName = kChineseName;
        CHECK(RoundTrip(rn).m_newName == kChineseName);

        ShareControlMessage del;
        del.m_type = sf_delete;
        del.m_reqId = 7;
        del.m_paths.push_back("a");
        del.m_paths.push_back("b/c");
        CHECK(RoundTrip(del).m_paths == del.m_paths);

        ShareControlMessage mv;
        mv.m_type = sf_move;
        mv.m_reqId = 8;
        mv.m_paths.push_back("a");
        mv.m_toDir = "x/y";
        ShareControlMessage r = RoundTrip(mv);
        CHECK(r.m_paths == mv.m_paths);
        CHECK(r.m_toDir == "x/y");

        ShareControlMessage res;
        res.m_type = sf_op_result;
        res.m_reqId = 8;
        res.m_code = 0;
        ShareOpItem ok;
        ok.m_path = "a";
        ShareOpItem bad;
        bad.m_path = "b/c";
        bad.m_code = kErrPathInvalid;
        res.m_items.push_back(ok);
        res.m_items.push_back(bad);
        r = RoundTrip(res);
        REQUIRE(r.m_items.size() == 2);
        CHECK(r.m_items[1].m_path == "b/c");
        CHECK(r.m_items[1].m_code == kErrPathInvalid);
    }

    SECTION("sf_perms、sf_bye、st_job")
    {
        ShareControlMessage p;
        p.m_type = sf_perms;
        p.m_perms = kPerms;
        CHECK(RoundTrip(p).m_perms == kPerms);

        ShareControlMessage bye;
        bye.m_type = sf_bye;
        bye.m_errCode = kErrPathInvalid;
        CHECK(RoundTrip(bye).m_errCode == kErrPathInvalid);

        ShareControlMessage job;
        job.m_type = st_job;
        job.m_direction = kDirectionDownload;
        job.m_paths.push_back("docs/a.txt");
        job.m_paths.push_back(kChineseName);
        job.m_toDir = "";
        const ShareControlMessage r = RoundTrip(job);
        CHECK(r.m_direction == kDirectionDownload);
        CHECK(r.m_paths == job.m_paths);
    }
}

TEST_CASE("SCF2 畸形包体解码失败、不崩溃", "[shareframe]")
{
    //合法帧作对照
    ShareControlMessage m;
    m.m_type = sf_list;
    m.m_reqId = 1;
    m.m_path = "a";
    std::string good;
    REQUIRE(ShareControlFraming::BuildFrame(m, good));
    REQUIRE(Decodes(good));

    //空包体、只有类型、长度字段不完整、截断的描述符
    CHECK_FALSE(Decodes(""));
    CHECK_FALSE(Decodes(good.substr(0, 1)));
    CHECK_FALSE(Decodes(good.substr(0, 3)));
    CHECK_FALSE(Decodes(good.substr(0, good.size() - 2)));
    ShareControlMessage dummy;
    CHECK_FALSE(ShareControlFraming::DecodeFrame(NULL, 10, dummy));

    //长度字段声称的描述符比包体还长
    std::string overflow = good;
    overflow[1] = static_cast<char>(0x7F);
    CHECK_FALSE(Decodes(overflow));

    //类型未知、类型为文件夹传输的 ft_*
    CHECK_FALSE(Decodes(RawFrame(0, "{\"reqid\":1,\"path\":\"a\"}", "")));
    CHECK_FALSE(Decodes(RawFrame(99, "{\"reqid\":1,\"path\":\"a\"}", "")));
    CHECK_FALSE(Decodes(RawFrame(foldertransfer::ft_manifest, "{}", "")));

    //JSON 非法、不是对象
    CHECK_FALSE(Decodes(RawFrame(sf_list, "{\"reqid\":1,", "")));
    CHECK_FALSE(Decodes(RawFrame(sf_list, "[1,2]", "")));

    //缺必填字段、字段类型不对
    CHECK_FALSE(Decodes(RawFrame(sf_list, "{\"path\":\"a\"}", "")));
    CHECK_FALSE(Decodes(RawFrame(sf_list, "{\"reqid\":1}", "")));
    CHECK_FALSE(Decodes(RawFrame(sf_list, "{\"reqid\":\"1\",\"path\":\"a\"}", "")));
    CHECK_FALSE(Decodes(RawFrame(sf_rename, "{\"reqid\":1,\"path\":\"a\"}", "")));
    CHECK_FALSE(Decodes(RawFrame(sf_delete, "{\"reqid\":1,\"paths\":\"a\"}", "")));
    CHECK_FALSE(Decodes(RawFrame(sf_delete, "{\"reqid\":1,\"paths\":[1]}", "")));
    CHECK_FALSE(Decodes(RawFrame(sf_list_result, "{\"reqid\":1,\"code\":0,\"entries\":[{\"isdir\":true}]}", "")));
    CHECK_FALSE(Decodes(RawFrame(sf_hello, "{\"version\":1}", "")));
    CHECK_FALSE(Decodes(RawFrame(st_job, "{\"direction\":3,\"items\":[]}", "")));

    //非缩略图帧带了附件
    CHECK_FALSE(Decodes(RawFrame(sf_list, "{\"reqid\":1,\"path\":\"a\"}", "xyz")));

    //编码一侧：非法类型、非缩略图帧带附件
    ShareControlMessage badType;
    badType.m_type = 99;
    std::string out;
    CHECK_FALSE(ShareControlFraming::BuildFrame(badType, out));
    ShareControlMessage withBinary = m;
    withBinary.m_binary = "x";
    CHECK_FALSE(ShareControlFraming::BuildFrame(withBinary, out));
}

TEST_CASE("SCF3 列目录与搜索结果按批拆分，每帧不超过上限，拼回与原列表一致", "[shareframe]")
{
    std::vector<ShareEntry> entries;
    for (size_t i = 0; i < kManyEntries; ++i)
    {
        std::string name = std::to_string(static_cast<unsigned long long>(i)) + "_" + std::string(kLongNameChars, 'n');
        entries.push_back(Entry(name, (i % 7) == 0, static_cast<int64_t>(i)));
    }

    std::vector<std::string> frames;
    REQUIRE(ShareControlFraming::BuildListResultFrames(kReqId, entries, frames));
    //只按条数拆会得到 kManyEntries / kShareListBatch 帧；帧数更多说明单帧字节上限起了作用
    REQUIRE(frames.size() > kManyEntries / kShareListBatch);

    std::vector<ShareEntry> joined;
    for (size_t i = 0; i < frames.size(); ++i)
    {
        INFO("frame " << i);
        CHECK(frames[i].size() <= kShareControlMaxFrameBytes);
        ShareControlMessage msg;
        REQUIRE(ShareControlFraming::DecodeFrame(frames[i].data(), frames[i].size(), msg));
        CHECK(msg.m_type == sf_list_result);
        CHECK(msg.m_reqId == kReqId);
        CHECK(msg.m_entries.size() <= kShareListBatch);
        CHECK(msg.m_more == (i + 1 < frames.size()));
        joined.insert(joined.end(), msg.m_entries.begin(), msg.m_entries.end());
    }
    REQUIRE(joined.size() == entries.size());
    for (size_t i = 0; i < entries.size(); ++i)
    {
        if (joined[i].m_name != entries[i].m_name || joined[i].m_size != entries[i].m_size)
        {
            FAIL("entry " << i << " differs");
        }
    }

    //短名字条目：按条数拆分
    std::vector<ShareEntry> shortEntries;
    for (size_t i = 0; i < kShortEntries; ++i)
        shortEntries.push_back(Entry(std::to_string(static_cast<unsigned long long>(i)), false, 1));
    std::vector<std::string> byCount;
    REQUIRE(ShareControlFraming::BuildListResultFrames(kReqId, shortEntries, byCount));
    REQUIRE(byCount.size() == kExpectedShortFrames);
    ShareControlMessage lastBatch;
    REQUIRE(ShareControlFraming::DecodeFrame(byCount.back().data(), byCount.back().size(), lastBatch));
    CHECK(lastBatch.m_entries.size() == kLastShortFrameEntries);
    CHECK_FALSE(lastBatch.m_more);

    //空目录也产出一帧
    std::vector<std::string> empty;
    REQUIRE(ShareControlFraming::BuildListResultFrames(kReqId, std::vector<ShareEntry>(), empty));
    REQUIRE(empty.size() == 1);

    //搜索：最后一帧带 done / truncated，前面的 done 为假
    std::vector<std::string> search;
    REQUIRE(ShareControlFraming::BuildSearchResultFrames(kReqId, entries, true, true, search));
    REQUIRE(search.size() > 1);
    size_t hits = 0;
    for (size_t i = 0; i < search.size(); ++i)
    {
        CHECK(search[i].size() <= kShareControlMaxFrameBytes);
        ShareControlMessage msg;
        REQUIRE(ShareControlFraming::DecodeFrame(search[i].data(), search[i].size(), msg));
        const bool last = (i + 1 == search.size());
        CHECK(msg.m_done == last);
        CHECK(msg.m_truncated == last);
        hits += msg.m_entries.size();
    }
    CHECK(hits == entries.size());
}

TEST_CASE("SCF4 缩略图帧的二进制附件完整", "[shareframe]")
{
    ShareControlMessage m;
    m.m_type = sf_thumb_result;
    m.m_reqId = 9;
    m.m_code = 0;
    m.m_width = kThumbWidth;
    m.m_height = kThumbHeight;
    m.m_binary.resize(kThumbBytes);
    for (size_t i = 0; i < kThumbBytes; ++i)
        m.m_binary[i] = static_cast<char>(i % kByteValues);

    const ShareControlMessage r = RoundTrip(m);
    CHECK(r.m_width == kThumbWidth);
    CHECK(r.m_height == kThumbHeight);
    CHECK(r.m_binary == m.m_binary);

    //附件过大：超过单帧上限时编码失败，由分享者降低质量重试
    m.m_binary.assign(kShareControlMaxFrameBytes, 'x');
    std::string out;
    CHECK_FALSE(ShareControlFraming::BuildFrame(m, out));
}

TEST_CASE("SCF5 帧类型取值与文件夹传输的 ft_* 不重叠", "[shareframe]")
{
    const int shareTypes[] =
    {
        sf_hello, sf_list, sf_list_result, sf_stat, sf_stat_result, sf_search, sf_search_result, sf_cancel,
        sf_thumb, sf_thumb_result, sf_mkdir, sf_rename, sf_delete, sf_move, sf_op_result, sf_perms, sf_bye, st_job
    };
    for (size_t i = 0; i < sizeof(shareTypes) / sizeof(shareTypes[0]); ++i)
    {
        INFO("type " << shareTypes[i]);
        CHECK((shareTypes[i] < foldertransfer::ft_manifest || shareTypes[i] > foldertransfer::ft_complete));
        CHECK(shareTypes[i] > 0);
        CHECK(shareTypes[i] < kByteValues);
    }
}

TEST_CASE("SCF6 列目录按游标逐批构造", "[shareframe]")
{
    //长名字条目逼出按字节拆分，短名字条目按条数拆分；两种情况下逐批构造都要与一次拆分逐字相同
    std::vector<ShareEntry> longEntries;
    for (size_t i = 0; i < kManyEntries; ++i)
    {
        std::string name = std::to_string(static_cast<unsigned long long>(i)) + "_" + std::string(kLongNameChars, 'n');
        longEntries.push_back(Entry(name, false, static_cast<int64_t>(i)));
    }
    std::vector<ShareEntry> shortEntries;
    for (size_t i = 0; i < kShortEntries; ++i)
        shortEntries.push_back(Entry(std::to_string(static_cast<unsigned long long>(i)), false, 1));

    const std::vector<ShareEntry>* lists[] = { &longEntries, &shortEntries };
    for (size_t l = 0; l < sizeof(lists) / sizeof(lists[0]); ++l)
    {
        const std::vector<ShareEntry>& entries = *lists[l];
        std::vector<std::string> whole;
        REQUIRE(ShareControlFraming::BuildListResultFrames(kReqId, entries, whole));

        std::vector<std::string> pulled;
        size_t start = 0;
        while (true)
        {
            std::string frame;
            size_t next = 0;
            REQUIRE(ShareControlFraming::BuildListResultBatch(kReqId, entries, start, frame, next));
            REQUIRE(next > start);
            pulled.push_back(frame);

            //写进帧里的游标就是 next，访问者带着它来取下一批
            ShareControlMessage msg;
            REQUIRE(ShareControlFraming::DecodeFrame(frame.data(), frame.size(), msg));
            size_t cursor = 0;
            REQUIRE(ShareControlFraming::ParseListCursor(msg.m_cursor, cursor));
            CHECK(cursor == next);
            CHECK(msg.m_more == (next < entries.size()));
            if (!msg.m_more)
                break;
            start = cursor;
        }
        CHECK(pulled == whole);
    }

    //起点等于条目数：空列表、more 为假；起点越界：失败
    std::string frame;
    size_t next = 0;
    REQUIRE(ShareControlFraming::BuildListResultBatch(kReqId, shortEntries, shortEntries.size(), frame, next));
    ShareControlMessage tail;
    REQUIRE(ShareControlFraming::DecodeFrame(frame.data(), frame.size(), tail));
    CHECK(tail.m_entries.empty());
    CHECK_FALSE(tail.m_more);
    CHECK(next == shortEntries.size());
    CHECK_FALSE(ShareControlFraming::BuildListResultBatch(kReqId, shortEntries, shortEntries.size() + 1, frame, next));

    //游标解析
    size_t offset = 1;
    CHECK(ShareControlFraming::ParseListCursor("", offset));
    CHECK(offset == 0);
    CHECK(ShareControlFraming::ParseListCursor("500", offset));
    CHECK(offset == 500);
    CHECK(ShareControlFraming::ParseListCursor("999999999", offset));
    CHECK(offset == 999999999u);
    CHECK_FALSE(ShareControlFraming::ParseListCursor("1000000000", offset));
    CHECK_FALSE(ShareControlFraming::ParseListCursor("-1", offset));
    CHECK_FALSE(ShareControlFraming::ParseListCursor("12a", offset));
    CHECK_FALSE(ShareControlFraming::ParseListCursor(" 1", offset));
}

TEST_CASE("SCF7 st_job_ack 与文件夹传输帧的区分", "[shareframe]")
{
    ShareControlMessage ack;
    ack.m_type = st_job_ack;
    ack.m_code = kErrPathInvalid;
    ShareControlMessage out = RoundTrip(ack);
    CHECK(out.m_code == kErrPathInvalid);

    ShareControlMessage job;
    job.m_type = st_job;
    job.m_direction = kDirectionDownload;
    job.m_paths.push_back("docs");
    std::string jobFrame;
    REQUIRE(ShareControlFraming::BuildFrame(job, jobFrame));
    std::string ackFrame;
    REQUIRE(ShareControlFraming::BuildFrame(ack, ackFrame));
    CHECK_FALSE(foldertransfer::IsFolderFrameTypeByte(static_cast<unsigned char>(jobFrame[0])));
    CHECK_FALSE(foldertransfer::IsFolderFrameTypeByte(static_cast<unsigned char>(ackFrame[0])));
    CHECK(foldertransfer::IsFolderFrameTypeByte(static_cast<unsigned char>(foldertransfer::ft_manifest)));
    CHECK(foldertransfer::IsFolderFrameTypeByte(static_cast<unsigned char>(foldertransfer::ft_complete)));
}

TEST_CASE("SCF8 搜索结果帧的结果码（第 2 期第 3 步）", "[shareframe]")
{
    ShareControlMessage failed;
    failed.m_type = sf_search_result;
    failed.m_reqId = 9;
    failed.m_done = true;
    failed.m_code = error_code_share_path_not_found;
    std::string frame;
    REQUIRE(ShareControlFraming::BuildFrame(failed, frame));
    ShareControlMessage back;
    REQUIRE(ShareControlFraming::DecodeFrame(frame.data(), frame.size(), back));
    CHECK(back.m_code == error_code_share_path_not_found);
    CHECK(back.m_done);

    //成功时不带结果码，解码为 0（第 1 期的帧格式不变）
    ShareControlMessage ok;
    ok.m_type = sf_search_result;
    ok.m_reqId = 9;
    REQUIRE(ShareControlFraming::BuildFrame(ok, frame));
    CHECK(frame.find("\"code\"") == std::string::npos);
    REQUIRE(ShareControlFraming::DecodeFrame(frame.data(), frame.size(), back));
    CHECK(back.m_code == 0);
}

TEST_CASE("SCF9 搜索请求帧的 all 与 max（第 2 期第 5 步）", "[shareframe]")
{
    const int kMax = 5001;
    ShareControlMessage tree;
    tree.m_type = sf_search;
    tree.m_reqId = 3;
    tree.m_path = "docs";
    tree.m_matchAll = true;
    tree.m_maxItems = kMax;
    std::string frame;
    REQUIRE(ShareControlFraming::BuildFrame(tree, frame));
    ShareControlMessage back;
    REQUIRE(ShareControlFraming::DecodeFrame(frame.data(), frame.size(), back));
    CHECK(back.m_matchAll);
    CHECK(back.m_maxItems == kMax);
    CHECK(back.m_path == "docs");

    ShareControlMessage plain;
    plain.m_type = sf_search;
    plain.m_reqId = 3;
    plain.m_keyword = "a";
    REQUIRE(ShareControlFraming::BuildFrame(plain, frame));
    CHECK(frame.find("\"all\"") == std::string::npos);
    CHECK(frame.find("\"max\"") == std::string::npos);
    REQUIRE(ShareControlFraming::DecodeFrame(frame.data(), frame.size(), back));
    CHECK_FALSE(back.m_matchAll);
    CHECK(back.m_maxItems == 0);
}
