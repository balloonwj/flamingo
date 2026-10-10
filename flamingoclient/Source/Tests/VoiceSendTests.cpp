/**
 *  语音消息发送侧的单元测试（前缀 VM，标签 [voicemsg]），用例清单见 docs/语音消息方案.md 第 9 节（2026-10-04 用户确认）：
 *    - VM26 录音结束后本地「发送中」气泡的构造：本人、clientMsgId、发送中、时长向上取整到秒、40 个波形值、本地路径、
 *           已听、msgid 为 0（第 3c 步）；
 *    - VM27 回执与失败：成功回执回填 msgid 并去掉发送中的圆环；失败时标红色感叹号与原因；已回填的不再改；
 *    - VM28 重发：按 clientMsgId 把失败的语音气泡改回发送中、清掉失败原因、位置不变；没有这条或不是失败状态时不改；
 *           窗口重发时按 clientMsgId 取得那条语音（第 3c-3 步补充）；
 *    - VM29 能否重发：本人发的、失败的、带 clientMsgId、本机录音文件还在才能重发；文件不在时有提示文字；
 *           发送失败的语音右键菜单有「重新发送」、没有「复制」，发送成功的语音没有「重新发送」（第 3c-3 步补充）；
 *    - VM30 清理缓存：勾选「聊天文件」时，清理清单里包含 ChatVoice 目录；
 *    - VM45 语音发送失败的文字：没有错误码时「语音发送失败」，有错误码时「语音发送失败（错误码 N）」（第 6c 步）。
 *  上传任务、1503 与发送结果的转发要连服务器，留到第 5 期联调验证。框架 Catch2 v3。
 *
 *  典型用法：FlamingoUnit.exe "[voicemsg]"
 *
 *  balloonwj@qq.com   2026-10-04
 */
#include "stdafx.h"
#include "catch_amalgamated.hpp"

#include <windows.h>
#include <memory>
#include <string>
#include <vector>

#include "../UI/BalloonChatWnd/ChatVoiceBubble.h"
#include "../UI/BalloonChatWnd/ChatMockData.h"
#include "../UI/BalloonChatWnd/ChatBubble.h"
#include "../UI/BalloonChatWnd/ChatMessageList.h"
#include "../UI/BalloonChatWnd/ChatMessageMenu.h"
#include "../UI/BalloonChatWnd/ChatReceivedVoice.h"
#include "../UI/BalloonSettings/CacheCleanPlan.h"
#include "../net/VoiceContent.h"

using namespace balloonchatwnd;

namespace {

//测试用的语音文件字节数
const long long kVsVoiceBytes = 5;
//测试用的录音时长（毫秒），向上取整后为 9 秒
const int kVsDurationMs = 8200;
//向上取整后的秒数
const int kVsDurationSec = 9;
//波形值的步长：第 i 根柱子取 i * kVsWaveStep，用来确认波形原样带进气泡
const int kVsWaveStep = 6;

//一组 40 个、各不相同的波形值
std::vector<unsigned char> sampleWave()
{
    std::vector<unsigned char> wave;
    for (int i = 0; i < net::kVoiceWaveBars; ++i)
    {
        wave.push_back(static_cast<unsigned char>(i * kVsWaveStep));
    }
    return wave;
}

//一条发送中的本人语音
ChatMessage sendingVoice(const std::string& clientMsgId)
{
    return MakeSendingVoiceMessage(clientMsgId, kVsDurationMs, sampleWave(), L"C:\\voice\\abc.ogg", "abc",
                                   kVsVoiceBytes);
}

//按 clientMsgId 在列表里找一条消息，并给出它在列表里的下标
bool findByClientMsgId(const ChatMessageList& list, const std::string& clientMsgId, ChatMessage& out, size_t& index)
{
    const std::vector<std::unique_ptr<balloonwjui::DuiControl> >& kids = list.Children();
    for (size_t i = 0; i < kids.size(); ++i)
    {
        const ChatBubble* b = dynamic_cast<const ChatBubble*>(kids[i].get());
        if (b != NULL && b->GetMessage().clientMsgId == clientMsgId)
        {
            out = b->GetMessage();
            index = i;
            return true;
        }
    }
    return false;
}

//每个进程独立的临时文件路径（文件名带进程号）
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

TEST_CASE("VM26 录音结束后的本地发送中气泡：本人、clientMsgId、发送中、时长、波形、路径、已听", "[voicemsg]")
{
    const ChatMessage m = sendingVoice("CMID-26");
    CHECK(m.kind == kMsgVoice);
    CHECK(m.fromMe);
    CHECK(m.clientMsgId == "CMID-26");
    CHECK(m.msgid == 0);
    CHECK(m.voiceState == kVoiceStateSending);
    CHECK_FALSE(m.sendFailed);
    //8200 毫秒向上取整为 9 秒
    CHECK(m.voiceDuration == kVsDurationSec);
    CHECK(m.voiceWave == sampleWave());
    CHECK(m.voiceLocalPath == CString(L"C:\\voice\\abc.ogg"));
    CHECK(m.voiceMd5 == "abc");
    CHECK(m.voiceSize == kVsVoiceBytes);
    CHECK(m.voicePlayed);
    CHECK_FALSE(m.text.IsEmpty());

    //波形个数不对（录音器异常）时画均匀波形，气泡上的柱子数不变
    std::vector<unsigned char> shortWave;
    shortWave.push_back(1);
    const ChatMessage odd = MakeSendingVoiceMessage("CMID-26b", kVsDurationMs, shortWave, L"C:\\voice\\x.ogg", "x", 1);
    CHECK(odd.voiceWave.size() == static_cast<size_t>(net::kVoiceWaveBars));
}

TEST_CASE("VM27 语音的回执与失败：成功回填 msgid 并去掉圆环，失败标红色感叹号与原因，已回填的不再改", "[voicemsg]")
{
    ChatMessageList list;
    list.AppendMessage(sendingVoice("CM-A"));
    list.AppendMessage(sendingVoice("CM-B"));

    REQUIRE(list.BackfillSentMessage("CM-A", 7001, 1700000000000LL));
    ChatMessage out;
    REQUIRE(list.GetMessageByMsgid(7001, out));
    CHECK(out.voiceState == kVoiceStateReady);
    CHECK_FALSE(out.sendFailed);

    const CString reason(L"reason");
    REQUIRE(list.MarkSendFailed("CM-B", reason));
    size_t index = 0;
    REQUIRE(findByClientMsgId(list, "CM-B", out, index));
    CHECK(out.sendFailed);
    CHECK(out.failReason == reason);
    //失败后不再画发送中的圆环，改画红色感叹号（ChatBubble 按 sendFailed 画）
    CHECK(out.voiceState == kVoiceStateReady);

    //已经回填过 msgid 的那条，再收到失败也不改
    CHECK_FALSE(list.MarkSendFailed("CM-A", CString(L"late")));
    REQUIRE(list.GetMessageByMsgid(7001, out));
    CHECK_FALSE(out.sendFailed);
}

TEST_CASE("VM28 语音重发：失败的气泡改回发送中、清掉失败原因、位置不变", "[voicemsg]")
{
    ChatMessageList list;
    ChatMessage before = MakeTextMessage(true, L"before");
    before.msgid = 101;
    list.AppendMessage(before);
    list.AppendMessage(sendingVoice("CM-B"));
    ChatMessage after = MakeTextMessage(true, L"after");
    after.msgid = 102;
    list.AppendMessage(after);
    REQUIRE(list.MarkSendFailed("CM-B", CString(L"reason")));

    ChatMessage out;
    size_t indexBefore = 0;
    REQUIRE(findByClientMsgId(list, "CM-B", out, indexBefore));

    REQUIRE(list.RestartVoiceSendByClientMsgId("CM-B"));
    size_t indexAfter = 0;
    REQUIRE(findByClientMsgId(list, "CM-B", out, indexAfter));
    CHECK(indexAfter == indexBefore);
    CHECK(out.voiceState == kVoiceStateSending);
    CHECK_FALSE(out.sendFailed);
    CHECK(out.failReason.IsEmpty());

    //窗口重发时按 clientMsgId 取那条语音（取录音文件路径、时长与波形）
    ChatMessage found;
    REQUIRE(FindVoiceByClientMsgId(list, "CM-B", found));
    CHECK(found.voiceLocalPath == CString(L"C:\\voice\\abc.ogg"));
    CHECK_FALSE(FindVoiceByClientMsgId(list, "CM-Z", found));
    CHECK_FALSE(FindVoiceByClientMsgId(list, "", found));

    //已经在发送中（不是失败状态）、列表里没有、空 clientMsgId：都不改
    CHECK_FALSE(list.RestartVoiceSendByClientMsgId("CM-B"));
    CHECK_FALSE(list.RestartVoiceSendByClientMsgId("CM-Z"));
    CHECK_FALSE(list.RestartVoiceSendByClientMsgId(""));
}

TEST_CASE("VM29 语音能否重发：本人、失败、有 clientMsgId、录音文件还在", "[voicemsg]")
{
    const std::wstring path = tempPath(L"vm29_voice.ogg");
    writeBytes(path, static_cast<size_t>(kVsVoiceBytes));

    ChatMessage m = MakeSendingVoiceMessage("CM-29", kVsDurationMs, sampleWave(), path, "m", kVsVoiceBytes);
    m.sendFailed = true;
    CHECK(CanResendVoice(m));

    ChatMessage notFailed = m;
    notFailed.sendFailed = false;
    CHECK_FALSE(CanResendVoice(notFailed));
    ChatMessage peer = m;
    peer.fromMe = false;
    CHECK_FALSE(CanResendVoice(peer));
    ChatMessage noId = m;
    noId.clientMsgId.clear();
    CHECK_FALSE(CanResendVoice(noId));

    //右键菜单：发送失败的语音有「重新发送」、没有「复制」；发送成功的语音没有「重新发送」
    const std::vector<ChatMessageMenuEntry> failedMenu = BuildChatMessageMenuModel(
        kChatMenuKindVoice, 0, false, false, false, false, false, true, false);
    bool hasResend = false;
    bool hasCopy = false;
    for (size_t i = 0; i < failedMenu.size(); ++i)
    {
        hasResend = hasResend || (!failedMenu[i].isSeparator && failedMenu[i].id == kChatMsgMenuResendVoice);
        hasCopy = hasCopy || (!failedMenu[i].isSeparator && failedMenu[i].id == kChatMsgMenuCopy);
    }
    CHECK(hasResend);
    CHECK_FALSE(hasCopy);
    const std::vector<ChatMessageMenuEntry> sentMenu = BuildChatMessageMenuModel(
        kChatMenuKindVoice, 0, false, true, true, false, true, false, false);
    for (size_t i = 0; i < sentMenu.size(); ++i)
    {
        CHECK((sentMenu[i].isSeparator || sentMenu[i].id != kChatMsgMenuResendVoice));
    }

    //录音文件被清理缓存删掉：不能重发，界面给出提示
    ::DeleteFileW(path.c_str());
    CHECK_FALSE(CanResendVoice(m));
    CHECK_FALSE(VoiceFileMissingHint().empty());
}

TEST_CASE("VM30 清理缓存：勾选「聊天文件」时一并清理 ChatVoice", "[voicemsg]")
{
    const std::wstring root = L"E:\\data\\";
    const std::vector<balloonsettings::CacheCleanEntry> plan = balloonsettings::BuildCacheCleanPlan(root, L"MY0113");
    const std::wstring voiceDir = L"E:\\data\\Users\\MY0113\\ChatVoice\\";
    bool inChatFile = false;
    bool elsewhere = false;
    for (size_t i = 0; i < plan.size(); ++i)
    {
        for (size_t k = 0; k < plan[i].m_dirs.size(); ++k)
        {
            if (plan[i].m_dirs[k] != voiceDir)
            {
                continue;
            }
            if (plan[i].m_category == balloonsettings::kCacheChatFile)
            {
                inChatFile = true;
            }
            else
            {
                elsewhere = true;
            }
        }
    }
    CHECK(inChatFile);
    CHECK_FALSE(elsewhere);
    CHECK(balloonsettings::IsSafeToDeleteCacheDir(voiceDir, root));
}

TEST_CASE("VM45 语音发送失败的文字：「语音发送失败」，有错误码时带上错误码", "[voicemsg]")
{
    const int kUploadFailCode = 600;   //error_code_upload_fail：第 5 期联调停掉 fileserver 时得到的错误码
    CHECK(VoiceSendFailureText(0) == L"语音发送失败");
    CHECK(VoiceSendFailureText(kUploadFailCode) == L"语音发送失败（错误码 600）");
    //气泡下方的原因行直接用，不带句号
    const std::wstring text = VoiceSendFailureText(kUploadFailCode);
    REQUIRE_FALSE(text.empty());
    CHECK(text[text.size() - 1] != L'。');
}
