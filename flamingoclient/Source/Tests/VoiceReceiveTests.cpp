/**
 *  语音消息接收侧的单元测试（前缀 VM，标签 [voicemsg]），用例清单见 docs/语音消息方案.md 第 9 节（2026-10-04 用户确认）：
 *    - VM22 下载结果按 md5 写回消息列表：同一 md5 的语音气泡全部变为就绪并记下本地路径，失败时变为下载失败，
 *           其它 md5 的语音与非语音消息不变（第 3d-3a 步）；
 *    - VM23 本地库按一批 msgid 查已听：只返回库里有并且已听的，库里没有的、未听的、别的会话的都不返回（第 3d-3b 步）；
 *    - VM24 收到或翻到的语音消息做成语音气泡：字段齐全；本机缓存文件存在且大小相符时为就绪、不需要下载，
 *           文件不存在或大小不符时为下载中、需要下载（第 3d-3c 步）；
 *    - VM25 已听状态的判断：本人发的一律已听；本地库没打开时按已听；本地库可用时查到已听才算已听（第 3d-3c 步）。
 *  框架 Catch2 v3。
 *
 *  典型用法：FlamingoUnit.exe "[voicemsg]"
 *
 *  balloonwj@qq.com   2026-10-04
 */
#include "stdafx.h"
#include "catch_amalgamated.hpp"

#include <windows.h>
#include <set>
#include <string>
#include <vector>

#include "../net/Protocol.h"
#include "../net/MessageProtocolClient.h"
#include "../UI/BalloonChatWnd/ChatVoiceBubble.h"
#include "../UI/BalloonChatWnd/ChatMockData.h"
#include "../UI/BalloonChatWnd/ChatMessageList.h"
#include "../UI/BalloonChatWnd/ChatReceivedVoice.h"
#include "../UI/BalloonHistory/V2MsgQuery.h"
#include "../UI/BalloonHistory/V2MsgStore.h"

using namespace balloonchatwnd;

namespace {

//测试用的本人、对方与另一个会话的对方 userid
const int64_t kVrSelf = 100001;
const int64_t kVrPeer = 100002;
const int64_t kVrOtherPeer = 100003;
//测试用的语音文件字节数
const long long kVrVoiceBytes = 5;

//一条语音消息推送
net::MessageNotify voiceNotify(int64_t msgid, int64_t from, int64_t cid, const std::string& md5)
{
    net::MessageNotify n;
    n.m_msgid = msgid;
    n.m_from = from;
    n.m_ctype = conversation_single;
    n.m_cid = cid;
    n.m_clientmsgid = "C" + std::to_string(msgid);
    n.m_msgtype = message_type_voice;
    n.m_timestamp = 1700000000000LL + msgid;
    n.m_voiceFileId = "998877";
    n.m_voiceMd5 = md5;
    n.m_voiceDuration = 8;
    n.m_voiceSize = kVrVoiceBytes;
    n.m_voiceWave = "AAECAw==";
    return n;
}

//一条处于「下载中」的语音气泡
ChatMessage downloadingVoice(long long msgid, const std::string& md5)
{
    ChatMessage m;
    m.fromMe = false;
    m.msgid = msgid;
    m.timestampMs = 1700000000000LL + msgid;
    ApplyVoiceMessage(m, "998877", md5, 8, kVrVoiceBytes, "", false);
    m.voiceState = kVoiceStateDownloading;
    return m;
}

//每个进程独立的临时路径（文件名带进程号），先删掉旧文件
std::wstring tempPath(const wchar_t* name)
{
    wchar_t dir[MAX_PATH] = { 0 };
    ::GetTempPathW(MAX_PATH, dir);
    wchar_t prefix[32] = { 0 };
    ::swprintf_s(prefix, L"%u_", static_cast<unsigned int>(::GetCurrentProcessId()));
    return std::wstring(dir) + prefix + name;
}

//写一个指定字节数的文件
void writeBytes(const std::wstring& path, size_t bytes)
{
    HANDLE h = ::CreateFileW(path.c_str(), GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    REQUIRE(h != INVALID_HANDLE_VALUE);
    const std::string data(bytes, 'x');
    DWORD written = 0;
    ::WriteFile(h, data.data(), static_cast<DWORD>(data.size()), &written, NULL);
    ::CloseHandle(h);
    REQUIRE(written == static_cast<DWORD>(bytes));
}

} // namespace

TEST_CASE("VM22 下载结果按 md5 写回消息列表：同 md5 的语音都就绪，失败时标为下载失败，其它消息不变", "[voicemsg]")
{
    ChatMessageList list;
    list.AppendMessage(downloadingVoice(101, "md5-a"));
    list.AppendMessage(downloadingVoice(102, "md5-a"));
    list.AppendMessage(downloadingVoice(103, "md5-b"));
    ChatMessage text = MakeTextMessage(false, L"hi");
    text.msgid = 104;
    list.AppendMessage(text);

    //同一段语音被转发过两次：两条气泡都就绪并带上路径
    const CString path(L"C:\\voice\\md5-a.ogg");
    CHECK(list.ApplyVoiceDownloadResult("md5-a", true, path) == 2);
    ChatMessage out;
    REQUIRE(list.GetMessageByMsgid(101, out));
    CHECK(out.voiceState == kVoiceStateReady);
    CHECK(out.voiceLocalPath == path);
    REQUIRE(list.GetMessageByMsgid(102, out));
    CHECK(out.voiceState == kVoiceStateReady);
    CHECK(out.voiceLocalPath == path);
    //别的 md5 不受影响
    REQUIRE(list.GetMessageByMsgid(103, out));
    CHECK(out.voiceState == kVoiceStateDownloading);
    CHECK(out.voiceLocalPath.IsEmpty());
    REQUIRE(list.GetMessageByMsgid(104, out));
    CHECK(out.kind == kMsgText);

    //下载失败
    CHECK(list.ApplyVoiceDownloadResult("md5-b", false, CString()) == 1);
    REQUIRE(list.GetMessageByMsgid(103, out));
    CHECK(out.voiceState == kVoiceStateDownloadFailed);
    //已就绪的那两条不因别的 md5 失败而改变
    REQUIRE(list.GetMessageByMsgid(101, out));
    CHECK(out.voiceState == kVoiceStateReady);

    //列表里没有的 md5、空 md5：一条都不改
    CHECK(list.ApplyVoiceDownloadResult("md5-z", true, path) == 0);
    CHECK(list.ApplyVoiceDownloadResult("", true, path) == 0);
}

TEST_CASE("VM23 本地库按一批 msgid 查已听：只返回库里有并且已听的", "[voicemsg]")
{
    const std::wstring path = tempPath(L"FlamingoUnit_VM23.db");
    ::DeleteFileW(path.c_str());
    ::DeleteFileW((path + L"-wal").c_str());
    ::DeleteFileW((path + L"-shm").c_str());

    balloonhistory::CV2MsgStore store;
    std::set<int64_t> played;
    std::vector<int64_t> candidates;
    candidates.push_back(11);
    //还没打开库：查询失败
    CHECK_FALSE(store.LoadPlayedVoiceIds(conversation_single, kVrPeer, candidates, played));

    REQUIRE(store.Open(path.c_str()));
    //11 对方发的、未听；12 对方发的、已听；13 本人发的（入库即已听）；14 在另一个会话里、已听
    REQUIRE(store.Upsert(balloonhistory::RecordFromNotify(voiceNotify(11, kVrPeer, kVrPeer, "m"),
                                                          conversation_single, kVrPeer, kVrSelf)));
    REQUIRE(store.Upsert(balloonhistory::RecordFromNotify(voiceNotify(12, kVrPeer, kVrPeer, "m"),
                                                          conversation_single, kVrPeer, kVrSelf)));
    REQUIRE(store.MarkVoicePlayed(conversation_single, kVrPeer, 12));
    REQUIRE(store.Upsert(balloonhistory::RecordFromNotify(voiceNotify(13, kVrSelf, kVrPeer, "m"),
                                                          conversation_single, kVrPeer, kVrSelf)));
    REQUIRE(store.Upsert(balloonhistory::RecordFromNotify(voiceNotify(14, kVrOtherPeer, kVrOtherPeer, "m"),
                                                          conversation_single, kVrOtherPeer, kVrSelf)));
    REQUIRE(store.MarkVoicePlayed(conversation_single, kVrOtherPeer, 14));

    candidates.clear();
    candidates.push_back(11);
    candidates.push_back(12);
    candidates.push_back(13);
    candidates.push_back(14);
    //99 库里没有
    candidates.push_back(99);
    REQUIRE(store.LoadPlayedVoiceIds(conversation_single, kVrPeer, candidates, played));
    CHECK(played.size() == 2);
    CHECK(played.count(12) == 1);
    CHECK(played.count(13) == 1);

    //空的候选：查询成功、结果为空（上一次的结果要清掉）
    candidates.clear();
    REQUIRE(store.LoadPlayedVoiceIds(conversation_single, kVrPeer, candidates, played));
    CHECK(played.empty());

    store.Close();
    ::DeleteFileW(path.c_str());
}

TEST_CASE("VM24 收到或翻到的语音消息做成语音气泡：本机有完整文件时就绪，否则下载中、需要下载", "[voicemsg]")
{
    //临时的 ChatVoice 目录（以反斜杠结尾）
    const std::wstring dir = tempPath(L"vm24_voice\\");
    ::CreateDirectoryW(dir.c_str(), NULL);
    const std::string md5 = "0123456789abcdef0123456789abcdef";
    const std::wstring file = dir + L"0123456789abcdef0123456789abcdef.ogg";
    ::DeleteFileW(file.c_str());

    SECTION("本机没有文件：下载中，需要下载，字段齐全")
    {
        bool needDownload = false;
        const ChatMessage m = BuildReceivedVoiceMessage(false, voiceNotify(501, kVrPeer, kVrPeer, md5), false, dir,
                                                        &needDownload);
        CHECK(m.kind == kMsgVoice);
        CHECK_FALSE(m.fromMe);
        CHECK(m.msgid == 501);
        CHECK(m.timestampMs == 1700000000000LL + 501);
        CHECK(m.clientMsgId == "C501");
        CHECK(m.voiceFileId == "998877");
        CHECK(m.voiceMd5 == md5);
        CHECK(m.voiceDuration == 8);
        CHECK(m.voiceSize == kVrVoiceBytes);
        REQUIRE(m.voiceWave.size() == 4);
        CHECK(m.voiceWave[3] == 3);
        CHECK_FALSE(m.voicePlayed);
        CHECK(m.voiceState == kVoiceStateDownloading);
        CHECK(m.voiceLocalPath.IsEmpty());
        CHECK(needDownload);
    }
    SECTION("本机有大小相符的文件：就绪，带上路径，不需要下载")
    {
        writeBytes(file, static_cast<size_t>(kVrVoiceBytes));
        bool needDownload = true;
        const ChatMessage m = BuildReceivedVoiceMessage(false, voiceNotify(502, kVrPeer, kVrPeer, md5), true, dir,
                                                        &needDownload);
        CHECK(m.voiceState == kVoiceStateReady);
        CHECK(m.voiceLocalPath == CString(file.c_str()));
        CHECK(m.voicePlayed);
        CHECK_FALSE(needDownload);
    }
    SECTION("本机文件大小不符（下载到一半留下的残余）：按没有文件处理")
    {
        writeBytes(file, static_cast<size_t>(kVrVoiceBytes) + 1);
        bool needDownload = false;
        const ChatMessage m = BuildReceivedVoiceMessage(false, voiceNotify(503, kVrPeer, kVrPeer, md5), false, dir,
                                                        &needDownload);
        CHECK(m.voiceState == kVoiceStateDownloading);
        CHECK(needDownload);
    }
    SECTION("本人发的：一律已听，画在右侧")
    {
        bool needDownload = false;
        const ChatMessage m = BuildReceivedVoiceMessage(true, voiceNotify(504, kVrSelf, kVrPeer, md5), false, dir,
                                                        &needDownload);
        CHECK(m.fromMe);
        CHECK(m.voicePlayed);
    }
    SECTION("消息里没有 fileid：无法下载，直接标为下载失败")
    {
        net::MessageNotify n = voiceNotify(505, kVrPeer, kVrPeer, md5);
        n.m_voiceFileId.clear();
        bool needDownload = true;
        const ChatMessage m = BuildReceivedVoiceMessage(false, n, false, dir, &needDownload);
        CHECK(m.voiceState == kVoiceStateDownloadFailed);
        CHECK_FALSE(needDownload);
    }

    ::DeleteFileW(file.c_str());
    ::RemoveDirectoryW(dir.c_str());
}

TEST_CASE("VM25 已听状态：本人发的一律已听，本地库没打开时按已听，库可用时查到已听才算已听", "[voicemsg]")
{
    CHECK(ResolveVoicePlayed(true, false, false));
    CHECK(ResolveVoicePlayed(true, true, false));
    //本地库没打开：不画红点，免得每次打开窗口都出现一片无法清除的红点
    CHECK(ResolveVoicePlayed(false, false, false));
    CHECK(ResolveVoicePlayed(false, true, true));
    //库可用、查不到已听（未听或还没入库）：未听
    CHECK_FALSE(ResolveVoicePlayed(false, true, false));

    //没有宿主主面板（调试入口、窗口已关）：查询不出结果，本地库按不可用处理
    VoicePlayedQuery query;
    query.m_ctype = conversation_single;
    query.m_cid = kVrPeer;
    query.m_msgids.push_back(1);
    QueryVoicePlayed(NULL, query);
    CHECK_FALSE(query.m_storeReady);
    CHECK(query.m_played.empty());
}
