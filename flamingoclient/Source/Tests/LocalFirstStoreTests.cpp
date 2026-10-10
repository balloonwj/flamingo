/**
 *  LocalFirstStoreTests.cpp
 *
 *  "打开聊天窗先显示本地消息库数据"第 3a 步的测试（标签 [localfirst]）：本地库保存每条消息的
 *  原始 JSON，按会话取最近 N 条，再还原成与网络应答完全相同的 net::MessageNotify。
 *
 *  覆盖：
 *    LF1 带字体与引用与 @提醒的文字、图片、带收取状态的文件、合并转发、文件夹记录、已撤回的文字，
 *        经"解析 → 入库 → 按会话取最近 N 条 → 还原"之后，各字段与原始解析结果逐项相同
 *    LF2 取最近 N 条：只取该会话、按 msgid 取最新的 N 条、结果按时间正序；撤回的消息也在其中，
 *        本地标记撤回之后还原出的消息带撤回标记
 *    LF3 没有原始 JSON 的老数据：还原函数返回 false；之后以空的原始 JSON 再次入库，
 *        不会把已存的原始 JSON 冲掉
 *
 *  balloonwj@qq.com   2026-10-01
 */

#include "stdafx.h"

#include "catch_amalgamated.hpp"

#include <string>
#include <vector>
#include <windows.h>

#include "../UI/BalloonHistory/V2MsgStore.h"
#include "../UI/BalloonHistory/V2MsgQuery.h"
#include "../net/MessageProtocolClient.h"

using namespace balloonhistory;

namespace {

//测试会话：单聊，对端 userid
const int     kCtypeSingle = 1;
const int64_t kPeerId = 5308;
//另一个会话，用于验证查询不会混入别的会话
const int64_t kOtherPeerId = 6001;
//LF2 灌入的消息条数与查询条数
const int kLf2Total = 30;
const int kLf2Limit = 10;
//LF2 / LF3 消息的起始 msgid 与时间
const int64_t kBaseMsgid = 1000;
const int64_t kBaseTimestamp = 1790000000000LL;

//每个用例用自己的临时库；文件名带进程 id，两个测试进程同时跑也不冲突
std::wstring FreshTempDb(const wchar_t* name)
{
    wchar_t dir[MAX_PATH] = { 0 };
    ::GetTempPathW(MAX_PATH, dir);
    wchar_t prefix[32] = { 0 };
    ::swprintf_s(prefix, L"%u_", static_cast<unsigned int>(::GetCurrentProcessId()));
    std::wstring path = dir;
    path += prefix;
    path += name;
    ::DeleteFileW(path.c_str());
    //本地库是 WAL 模式：上次异常退出残留的日志文件会被套用到同名新库上，一并删掉
    ::DeleteFileW((path + L"-wal").c_str());
    ::DeleteFileW((path + L"-shm").c_str());
    return path;
}

//LF1 的一页同步应答：六条消息覆盖各种内容类型与可选字段
const char* const kLf1Response =
    "{\"code\":0,\"msg\":\"ok\",\"ctype\":1,\"cid\":\"5308\",\"hasmore\":true,\"peerreadmsgid\":\"0\",\"messages\":["
    //1 文字：带字体、引用一张图片、@ 一个人
    "{\"msgid\":\"701\",\"from\":\"5127\",\"to\":\"5308\",\"clientmsgid\":\"c-701\",\"ctype\":1,\"cid\":\"5308\","
    "\"msgtype\":1,\"timestamp\":1790000001000,"
    "\"content\":{\"text\":\"@韩晓燕 看这张图\","
    "\"font\":{\"bold\":true,\"color\":\"FF0000\",\"italic\":false,\"size\":16,\"strike\":false,\"underline\":true},"
    "\"at\":{\"all\":0,\"users\":[{\"uid\":\"5308\",\"off\":0,\"len\":4}]},"
    "\"quote\":{\"msgid\":\"650\",\"from\":\"5308\",\"name\":\"韩晓燕\",\"msgtype\":2,\"digest\":\"[图片]\","
    "\"fileid\":\"123\",\"md5\":\"55fda72a017078ca20faf533d79e07cb\",\"w\":1858,\"h\":979}}},"
    //2 图片
    "{\"msgid\":\"702\",\"from\":\"5308\",\"to\":\"5127\",\"clientmsgid\":\"c-702\",\"ctype\":1,\"cid\":\"5308\","
    "\"msgtype\":2,\"timestamp\":1790000002000,"
    "\"content\":{\"fileid\":\"121\",\"md5\":\"562967c4a1a7573ecdcb76677f4d0104\",\"width\":1186,\"height\":452,\"size\":113036}},"
    //3 文件：自己发出、对方已接收
    "{\"msgid\":\"703\",\"from\":\"5127\",\"to\":\"5308\",\"clientmsgid\":\"c-703\",\"ctype\":1,\"cid\":\"5308\","
    "\"msgtype\":3,\"timestamp\":1790000003000,\"fileackstate\":1,"
    "\"content\":{\"fileid\":\"122\",\"filename\":\"季度报告.xlsx\",\"filesize\":187207,\"md5\":\"cb0258b7685f759fed5163cfe291408a\"}},"
    //4 合并转发
    "{\"msgid\":\"704\",\"from\":\"5308\",\"to\":\"5127\",\"clientmsgid\":\"c-704\",\"ctype\":1,\"cid\":\"5308\","
    "\"msgtype\":15,\"timestamp\":1790000004000,"
    "\"content\":{\"title\":\"韩晓燕和张勇的聊天记录\",\"messages\":[{\"msgid\":\"10\",\"from\":\"5308\",\"fromname\":\"韩晓燕\","
    "\"msgtype\":1,\"timestamp\":1789999990000,\"content\":{\"text\":\"子消息\"}}]}},"
    //5 文件夹传输记录
    "{\"msgid\":\"705\",\"from\":\"5127\",\"to\":\"5308\",\"clientmsgid\":\"c-705\",\"ctype\":1,\"cid\":\"5308\","
    "\"msgtype\":18,\"timestamp\":1790000005000,"
    "\"content\":{\"sessionid\":\"fs-1\",\"name\":\"设计稿\",\"dirs\":2,\"files\":5,\"size\":1048576,\"result\":3,\"errcode\":0}},"
    //6 已撤回的文字
    "{\"msgid\":\"706\",\"from\":\"5308\",\"to\":\"5127\",\"clientmsgid\":\"c-706\",\"ctype\":1,\"cid\":\"5308\","
    "\"msgtype\":1,\"timestamp\":1790000006000,\"recalled\":true,"
    "\"content\":{\"text\":\"这条撤回了\"}}"
    "]}";

//两条消息逐字段比较；全部相同时返回空串，否则返回第一个不同字段的名字
std::string FirstDifference(const net::MessageNotify& a, const net::MessageNotify& b)
{
    if (a.m_msgid != b.m_msgid || a.m_clientmsgid != b.m_clientmsgid || a.m_ctype != b.m_ctype ||
        a.m_cid != b.m_cid || a.m_from != b.m_from || a.m_to != b.m_to)
        return "header";
    if (a.m_msgtype != b.m_msgtype || a.m_timestamp != b.m_timestamp || a.m_recalled != b.m_recalled)
        return "type / timestamp / recalled";
    if (a.m_text != b.m_text)
        return "text";
    if (a.m_font.m_valid != b.m_font.m_valid || a.m_font.m_size != b.m_font.m_size ||
        a.m_font.m_bold != b.m_font.m_bold || a.m_font.m_italic != b.m_font.m_italic ||
        a.m_font.m_underline != b.m_font.m_underline || a.m_font.m_strike != b.m_font.m_strike ||
        a.m_font.m_colorRgb != b.m_font.m_colorRgb)
        return "font";
    if (a.m_imageFileId != b.m_imageFileId || a.m_imageMd5 != b.m_imageMd5 ||
        a.m_imageWidth != b.m_imageWidth || a.m_imageHeight != b.m_imageHeight ||
        a.m_imageSize != b.m_imageSize)
        return "image";
    if (a.m_fileFileId != b.m_fileFileId || a.m_fileName != b.m_fileName ||
        a.m_fileSize != b.m_fileSize || a.m_fileMd5 != b.m_fileMd5 || a.m_fileAckState != b.m_fileAckState)
        return "file";
    if (a.m_folderSessionId != b.m_folderSessionId || a.m_folderName != b.m_folderName ||
        a.m_folderTotalDirs != b.m_folderTotalDirs || a.m_folderTotalFiles != b.m_folderTotalFiles ||
        a.m_folderTotalSize != b.m_folderTotalSize || a.m_folderResult != b.m_folderResult ||
        a.m_folderErrCode != b.m_folderErrCode)
        return "folder";
    if (a.m_atAll != b.m_atAll || a.m_ats.size() != b.m_ats.size())
        return "ats";
    for (size_t i = 0; i < a.m_ats.size(); ++i)
    {
        if (a.m_ats[i].m_uid != b.m_ats[i].m_uid || a.m_ats[i].m_off != b.m_ats[i].m_off ||
            a.m_ats[i].m_len != b.m_ats[i].m_len)
            return "ats item";
    }
    if (a.m_quote.m_valid != b.m_quote.m_valid || a.m_quote.m_msgid != b.m_quote.m_msgid ||
        a.m_quote.m_from != b.m_quote.m_from || a.m_quote.m_name != b.m_quote.m_name ||
        a.m_quote.m_msgtype != b.m_quote.m_msgtype || a.m_quote.m_digest != b.m_quote.m_digest ||
        a.m_quote.m_fileid != b.m_quote.m_fileid || a.m_quote.m_md5 != b.m_quote.m_md5 ||
        a.m_quote.m_width != b.m_quote.m_width || a.m_quote.m_height != b.m_quote.m_height)
        return "quote";
    if (a.m_fromName != b.m_fromName || a.m_mergedContent != b.m_mergedContent)
        return "merged";
    if (a.m_rawJson != b.m_rawJson)
        return "rawjson";
    return std::string();
}

//造一条只含正文的入库记录，原始 JSON 可选
HistoryRecord MakeTextRecord(int64_t cid, int64_t msgid, bool bWithRaw)
{
    char raw[256];
    sprintf_s(raw, "{\"msgid\":\"%lld\",\"from\":\"5127\",\"to\":\"%lld\",\"ctype\":1,\"cid\":\"%lld\","
                   "\"msgtype\":1,\"timestamp\":%lld,\"content\":{\"text\":\"m%lld\"}}",
              (long long)msgid, (long long)cid, (long long)cid,
              (long long)(kBaseTimestamp + msgid), (long long)msgid);
    net::MessageNotify n;
    REQUIRE(net::MessageProtocolClient::parseMessageNotify(raw, n));
    HistoryRecord rec = RecordFromNotify(n, kCtypeSingle, cid);
    if (!bWithRaw)
        rec.m_raw.clear();
    return rec;
}

} // namespace

TEST_CASE("LF1 各类消息经入库、取出、还原之后与原始解析结果逐项相同", "[localfirst]")
{
    net::SyncMessagesResult page;
    REQUIRE(net::MessageProtocolClient::parseSyncMessagesResponse(kLf1Response, page));
    REQUIRE(page.m_messages.size() == 6);
    for (size_t i = 0; i < page.m_messages.size(); ++i)
        REQUIRE_FALSE(page.m_messages[i].m_rawJson.empty());

    CV2MsgStore store;
    REQUIRE(store.Open(FreshTempDb(L"lf1.db").c_str()));
    std::vector<HistoryRecord> recs;
    for (size_t i = 0; i < page.m_messages.size(); ++i)
        recs.push_back(RecordFromNotify(page.m_messages[i], kCtypeSingle, kPeerId));
    REQUIRE(store.UpsertBatch(recs));

    std::vector<HistoryRecord> latest;
    REQUIRE(store.QueryLatest(kCtypeSingle, kPeerId, 50, latest));
    REQUIRE(latest.size() == page.m_messages.size());

    for (size_t i = 0; i < latest.size(); ++i)
    {
        net::MessageNotify restored;
        INFO("message #" << (i + 1) << " msgid " << latest[i].m_msgid);
        REQUIRE(NotifyFromRecord(latest[i], restored));
        CHECK(FirstDifference(page.m_messages[i], restored) == "");
    }
    store.Close();
}

TEST_CASE("LF2 取最近 N 条：只取本会话、取最新的 N 条、按时间正序，撤回状态以本地为准", "[localfirst]")
{
    CV2MsgStore store;
    REQUIRE(store.Open(FreshTempDb(L"lf2.db").c_str()));
    std::vector<HistoryRecord> recs;
    for (int i = 0; i < kLf2Total; ++i)
    {
        recs.push_back(MakeTextRecord(kPeerId, kBaseMsgid + i, true));
        recs.push_back(MakeTextRecord(kOtherPeerId, kBaseMsgid + i, true));
    }
    REQUIRE(store.UpsertBatch(recs));

    //最新一条在本地被标记撤回（实时收到撤回通知时走的就是这个接口）
    const int64_t newest = kBaseMsgid + kLf2Total - 1;
    REQUIRE(store.MarkRecalled(kCtypeSingle, kPeerId, newest));

    std::vector<HistoryRecord> latest;
    REQUIRE(store.QueryLatest(kCtypeSingle, kPeerId, kLf2Limit, latest));
    REQUIRE(latest.size() == (size_t)kLf2Limit);

    bool bAscending = true;
    bool bOnlyThisConversation = true;
    for (size_t i = 0; i < latest.size(); ++i)
    {
        if (latest[i].m_cid != kPeerId)
            bOnlyThisConversation = false;
        if (i > 0 && latest[i].m_msgid <= latest[i - 1].m_msgid)
            bAscending = false;
    }
    CHECK(bOnlyThisConversation);
    CHECK(bAscending);
    CHECK(latest.front().m_msgid == newest - kLf2Limit + 1);
    CHECK(latest.back().m_msgid == newest);

    //撤回的那条仍在结果里，还原出来带撤回标记；其余不带
    CHECK(latest.back().m_recalled);
    net::MessageNotify restoredNewest;
    REQUIRE(NotifyFromRecord(latest.back(), restoredNewest));
    CHECK(restoredNewest.m_recalled);
    net::MessageNotify restoredFirst;
    REQUIRE(NotifyFromRecord(latest.front(), restoredFirst));
    CHECK_FALSE(restoredFirst.m_recalled);
    store.Close();
}

TEST_CASE("LF3 没有原始 JSON 的老数据无法还原；以空的原始 JSON 再次入库不会冲掉已存的", "[localfirst]")
{
    CV2MsgStore store;
    REQUIRE(store.Open(FreshTempDb(L"lf3.db").c_str()));

    //老数据：入库时没有原始 JSON
    std::vector<HistoryRecord> old;
    old.push_back(MakeTextRecord(kPeerId, kBaseMsgid, false));
    REQUIRE(store.UpsertBatch(old));
    std::vector<HistoryRecord> latest;
    REQUIRE(store.QueryLatest(kCtypeSingle, kPeerId, kLf2Limit, latest));
    REQUIRE(latest.size() == 1);
    net::MessageNotify restored;
    CHECK_FALSE(NotifyFromRecord(latest[0], restored));

    //补上原始 JSON 之后可以还原
    std::vector<HistoryRecord> withRaw;
    withRaw.push_back(MakeTextRecord(kPeerId, kBaseMsgid, true));
    REQUIRE(store.UpsertBatch(withRaw));
    REQUIRE(store.QueryLatest(kCtypeSingle, kPeerId, kLf2Limit, latest));
    REQUIRE(latest.size() == 1);
    CHECK(NotifyFromRecord(latest[0], restored));

    //再以空的原始 JSON 入库同一条（例如走了不带原始 JSON 的旧路径），已存的原始 JSON 保留
    REQUIRE(store.UpsertBatch(old));
    REQUIRE(store.QueryLatest(kCtypeSingle, kPeerId, kLf2Limit, latest));
    REQUIRE(latest.size() == 1);
    CHECK_FALSE(latest[0].m_raw.empty());
    CHECK(NotifyFromRecord(latest[0], restored));
    store.Close();
}

TEST_CASE("CPF10 本地页的准备耗时：查库与逐条还原各花多久", "[.chatperf]")
{
    //与测试服务端里的消息同一量级：50 条带字体的文字消息，原始 JSON 约两三百字节
    const int kCount = 50;
    CV2MsgStore store;
    REQUIRE(store.Open(FreshTempDb(L"cpf10.db").c_str()));
    std::vector<HistoryRecord> recs;
    for (int i = 0; i < kCount; ++i)
    {
        char raw[512];
        sprintf_s(raw, "{\"msgid\":\"%d\",\"from\":\"5127\",\"to\":\"5308\",\"clientmsgid\":\"c-%d\",\"ctype\":1,"
                       "\"cid\":\"5308\",\"msgtype\":1,\"timestamp\":%lld,\"content\":{\"text\":\"我这边的接口已经联调通了，"
                       "下午可以提测，你那边准备好了吗？\",\"font\":{\"bold\":false,\"color\":\"1A1A1A\",\"italic\":false,"
                       "\"size\":14,\"strike\":false,\"underline\":false}}}",
                  700 + i, 700 + i, 1790000000000LL + i);
        net::MessageNotify n;
        REQUIRE(net::MessageProtocolClient::parseMessageNotify(raw, n));
        recs.push_back(RecordFromNotify(n, kCtypeSingle, kPeerId));
    }
    REQUIRE(store.UpsertBatch(recs));

    const int kRounds = 10;
    double queryMs = 0.0;
    double restoreMs = 0.0;
    for (int r = 0; r < kRounds; ++r)
    {
        std::vector<HistoryRecord> latest;
        LARGE_INTEGER f;
        LARGE_INTEGER t0;
        LARGE_INTEGER t1;
        LARGE_INTEGER t2;
        ::QueryPerformanceFrequency(&f);
        ::QueryPerformanceCounter(&t0);
        REQUIRE(store.QueryLatest(kCtypeSingle, kPeerId, kCount, latest));
        ::QueryPerformanceCounter(&t1);
        for (size_t i = 0; i < latest.size(); ++i)
        {
            net::MessageNotify n;
            NotifyFromRecord(latest[i], n);
        }
        ::QueryPerformanceCounter(&t2);
        queryMs += (double)(t1.QuadPart - t0.QuadPart) * 1000.0 / (double)f.QuadPart;
        restoreMs += (double)(t2.QuadPart - t1.QuadPart) * 1000.0 / (double)f.QuadPart;
    }
    printf("\n[chatperf] CPF10 local page of %d messages (avg of %d rounds): query %.2f ms, restore %.2f ms\n",
           kCount, kRounds, queryMs / kRounds, restoreMs / kRounds);
    fflush(stdout);
    store.Close();
}
