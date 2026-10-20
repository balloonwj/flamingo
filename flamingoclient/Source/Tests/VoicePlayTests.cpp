/**
 *  语音消息播放侧的单元测试（前缀 VM，标签 [voicemsg]），用例清单见 docs/语音消息方案.md 第 9 节（2026-10-04 用户确认）：
 *    - VM13 自动连播挑下一条：紧挨着的下一条（时间分隔行不算）是对方发来、未听、文件已就绪的语音才连播；
 *           中间夹了别的消息、下一条是本人发的或已听的、或还没下载好的都停；
 *    - VM31 点气泡时做什么：就绪的没在播就播放、正在播就停止；下载中提示正在下载；下载失败重新下载；
 *           本人发送中或发送失败的语音也能播放本机录音文件；
 *    - VM32 列表上的播放状态：开始播放时该气泡进入播放中、红点消失、同列表别的停止；进度推进时喇叭图标在
 *           1～3 道声波之间轮换；播完恢复静止；下载失败的气泡重新下载时改回下载中；
 *    - VM33 语音中心的播放调度（模拟 FlamingoMedia 模块，不碰扬声器）：同一时刻只播一条，开播第二条时第一条的
 *           窗口收到「已停止」；旧编号的进度与结束被丢弃；窗口关闭时停止它在播的；开始录音时停止播放；
 *    - VM44 红点同步（消息列表，第 6a 步）：别的窗口里听过一条语音后按 msgid 把列表里这一条标为已听，只改对方发来、
 *           未听的那一条（记录窗一侧见 VoiceHistoryTests.cpp 的 VM44b）。
 *  框架 Catch2 v3。
 *
 *  典型用法：FlamingoUnit.exe "[voicemsg]"
 *
 *  balloonwj@qq.com   2026-10-04
 */
#include "stdafx.h"
#include "catch_amalgamated.hpp"

#include <windows.h>
#include <string>
#include <vector>

#include "../UI/BalloonChatWnd/ChatVoiceBubble.h"
#include "../UI/BalloonChatWnd/ChatMockData.h"
#include "../UI/BalloonChatWnd/ChatMessageList.h"
#include "../Feature/Voice/VoiceHub.h"
#include "../../FlamingoMedia/Api/FlamingoMediaApi.h"

using namespace balloonchatwnd;
using namespace flamingomedia;

namespace {

//测试用的语音时长（秒）与文件字节数
const int kVpDurationSec = 8;
const long long kVpVoiceBytes = 25600;
//模拟模块回报的总时长（毫秒）
const int64_t kVpTotalMs = 8000;

//一条语音消息：对方发来或本人发的，已听与否、状态由调用方给出
ChatMessage voiceRow(long long msgid, bool fromMe, bool played, int state)
{
    ChatMessage m;
    m.fromMe = fromMe;
    m.msgid = msgid;
    m.timestampMs = 1700000000000LL + msgid;
    ApplyVoiceMessage(m, "998877", "md5-" + std::to_string(msgid), kVpDurationSec, kVpVoiceBytes, "", played);
    m.voiceState = state;
    if (state == kVoiceStateReady || state == kVoiceStateSending)
    {
        m.voiceLocalPath = L"C:\\voice\\x.ogg";
    }
    return m;
}

//一条文字消息
ChatMessage textRow(long long msgid)
{
    ChatMessage m = MakeTextMessage(false, L"hi");
    m.msgid = msgid;
    return m;
}

//一行时间分隔（msgid 为 0）
ChatMessage timeRow()
{
    ChatMessage m;
    m.kind = kMsgTime;
    m.text = L"12:00";
    return m;
}

//按 msgid 取列表里的一条
ChatMessage rowOf(const ChatMessageList& list, long long msgid)
{
    ChatMessage out;
    REQUIRE(list.GetMessageByMsgid(msgid, out));
    return out;
}

//模拟的 FlamingoMedia 模块：只记下播放、停止播放的调用，其余方法什么也不做
class FakePlayModule : public IMediaModule
{
public:
    FakePlayModule()
        : m_playOk(true)
        , m_lastToken(0)
        , m_playCount(0)
        , m_stopPlayCount(0)
        , m_recording(false)
    {
    }

    virtual ~FakePlayModule()
    {
    }

    virtual bool PlayVoice(const wchar_t* path, uint32_t token) override
    {
        if (!m_playOk)
            return false;
        m_lastPath = path;
        m_lastToken = token;
        ++m_playCount;
        return true;
    }

    virtual void StopVoicePlay() override
    {
        ++m_stopPlayCount;
    }

    virtual int StartVoiceRecord(const wchar_t* path) override
    {
        (void)path;
        m_recording = true;
        return media_voice_record_ok;
    }

    virtual bool StopVoiceRecord(MediaVoiceRecordInfo* info) override
    {
        (void)info;
        m_recording = false;
        return false;
    }

    virtual void CancelVoiceRecord() override
    {
        m_recording = false;
    }

    //暂停与继续录音：播放调度的用例不涉及，按「没有可暂停的录音」处理
    virtual bool PauseVoiceRecord() override
    {
        return false;
    }

    virtual int ResumeVoiceRecord() override
    {
        return media_voice_record_not_paused;
    }

    //群通话（docs/群音视频通话方案.md 6.2）：用例不调用
    virtual bool StartGroupCall(int64_t groupId, const wchar_t* groupName, int media, const int64_t* invitees,
                                uint32_t count) override
    {
        (void)groupId;
        (void)groupName;
        (void)media;
        (void)invitees;
        (void)count;
        return false;
    }

    virtual bool JoinGroupCall(int64_t groupId, const wchar_t* groupName, const char* roomId) override
    {
        (void)groupId;
        (void)groupName;
        (void)roomId;
        return false;
    }

    virtual void QueryGroupCall(int64_t groupId) override
    {
        (void)groupId;
    }

    virtual bool InviteToGroupCall(int64_t groupId, const int64_t* userIds, uint32_t count) override
    {
        (void)groupId;
        (void)userIds;
        (void)count;
        return false;
    }

    virtual void OnAvatarsUpdated() override
    {
    }

    virtual bool SnapshotVoiceRecord(const wchar_t* path, int64_t* outDurationMs) override
    {
        (void)path;
        (void)outDurationMs;
        return false;
    }

    //录制管理（接口版本 12）：本用例不涉及
    virtual void ShowRecordManager() override
    {
    }

    //锁屏状态通知（接口版本 13）：本用例不涉及
    virtual void OnCaptionAvailabilityChanged(bool /*available*/) override
    {
    }
    virtual void SetScreenLocked(bool /*locked*/) override
    {
    }

    //凭分享链接加入屏幕共享（接口版本 15）：本用例不涉及
    virtual bool JoinScreenShareByLink(const wchar_t* /*url*/) override
    {
        return false;
    }

    //云端录制（接口第 16 版）：用例不调用
    virtual void OnCloudTransferProgress(uint32_t /*id*/, int64_t /*doneBytes*/, int64_t /*totalBytes*/) override
    {
    }

    virtual void OnCloudTransferFinished(uint32_t /*id*/, int /*result*/) override
    {
    }

    virtual bool HasCloudRecordWork() override
    {
        return false;
    }

    virtual void OpenCloudRecord(int64_t /*recordId*/, const wchar_t* /*title*/) override
    {
    }

    virtual void OnConversationsPicked(uint32_t /*token*/, const MediaConversationRef* /*refs*/, uint32_t /*count*/) override
    {
    }

    //第 17 版：退出前询问云端录像；用例不调用
    virtual int ConfirmCloudRecordExit(int /*kind*/) override
    {
        return media_cloud_exit_keep;
    }

    virtual void SetShareQuality(int /*quality*/) override
    {
    }

    //以下为与语音无关的方法，用例不调用
    virtual uint32_t GetApiVersion() const override
    {
        return kApiVersion;
    }

    virtual bool Init(IMediaHost* host, const MediaInitParams* params) override
    {
        (void)host;
        (void)params;
        return true;
    }

    virtual void Shutdown() override
    {
    }

    virtual bool OnSignal(uint16_t cmd, const char* json, uint32_t len) override
    {
        (void)cmd;
        (void)json;
        (void)len;
        return false;
    }

    virtual void OnConnectionStateChanged(bool online) override
    {
        (void)online;
    }

    virtual bool IsFeatureSupported(int feature) override
    {
        (void)feature;
        return true;
    }

    virtual void OnLogout() override
    {
    }

    virtual bool StartCall(int64_t peerUserId, const wchar_t* peerName, int callType) override
    {
        (void)peerUserId;
        (void)peerName;
        (void)callType;
        return false;
    }

    virtual bool StartRemoteDesktop(int64_t peerUserId, const wchar_t* peerName) override
    {
        (void)peerUserId;
        (void)peerName;
        return false;
    }

    virtual bool StartScreenShare(int ctype, int64_t cid, const wchar_t* name) override
    {
        (void)ctype;
        (void)cid;
        (void)name;
        return false;
    }

    virtual bool JoinScreenShare(int ctype, int64_t cid, const char* roomId, const wchar_t* hostName, bool forced) override
    {
        (void)ctype;
        (void)cid;
        (void)roomId;
        (void)hostName;
        (void)forced;
        return false;
    }

    virtual void QueryScreenShare(int ctype, int64_t cid) override
    {
        (void)ctype;
        (void)cid;
    }

    virtual bool StartRecording(int ctype, int64_t cid, const wchar_t* name) override
    {
        (void)ctype;
        (void)cid;
        (void)name;
        return false;
    }

    virtual void StopRecording() override
    {
    }

    virtual bool IsRecording() override
    {
        return false;
    }

    virtual void SetRecordSettings(int quality, const wchar_t* folder) override
    {
        (void)quality;
        (void)folder;
    }

    virtual void SetMouseEffectSettings(const char* settingsText) override
    {
        (void)settingsText;
    }

    virtual int RunDebugEntry(const wchar_t* commandLine, HWND hwndHost) override
    {
        (void)commandLine;
        (void)hwndHost;
        return 0;
    }

public:
    bool         m_playOk;         //PlayVoice 是否返回成功
    std::wstring m_lastPath;       //最近一次 PlayVoice 的文件路径
    uint32_t     m_lastToken;      //最近一次 PlayVoice 的编号
    int          m_playCount;      //PlayVoice 成功的次数
    int          m_stopPlayCount;  //StopVoicePlay 的次数
    bool         m_recording;      //是否在录音
};

//模拟的聊天窗：记下收到的播放通知
class FakePlayListener : public balloonvoice::IVoicePlayListener
{
public:
    FakePlayListener()
        : m_progressCount(0)
        , m_lastPlayedMs(0)
        , m_endCount(0)
        , m_lastEndReason(-1)
    {
    }

    virtual void OnVoicePlayProgress(const balloonvoice::VoicePlayKey& key, int64_t playedMs, int64_t totalMs) override
    {
        (void)totalMs;
        m_lastKey = key;
        m_lastPlayedMs = playedMs;
        ++m_progressCount;
    }

    virtual void OnVoicePlayEnded(const balloonvoice::VoicePlayKey& key, int reason) override
    {
        m_lastKey = key;
        m_lastEndReason = reason;
        ++m_endCount;
    }

public:
    balloonvoice::VoicePlayKey m_lastKey;        //最近一次通知所属的语音
    int                        m_progressCount;  //收到进度的次数
    int64_t                    m_lastPlayedMs;   //最近一次进度
    int                        m_endCount;       //收到结束的次数
    int                        m_lastEndReason;  //最近一次结束的原因，取值见 VoicePlayEnd
};

//一个播放键
balloonvoice::VoicePlayKey playKey(long long msgid)
{
    balloonvoice::VoicePlayKey key;
    key.m_msgid = msgid;
    return key;
}

} // namespace

TEST_CASE("VM13 自动连播挑下一条：紧挨着、对方发来、未听、已就绪的语音才连播", "[voicemsg]")
{
    std::vector<ChatMessage> rows;
    rows.push_back(voiceRow(1, false, true, kVoiceStateReady));     //0 正在播的这一条
    rows.push_back(timeRow());                                      //1 时间分隔行不算隔开
    rows.push_back(voiceRow(2, false, false, kVoiceStateReady));    //2 对方、未听、就绪：连播
    CHECK(PickNextAutoPlayVoice(rows, 0) == 2);

    //下一条已听、本人发的、还没下载好的、下载失败的：都停
    rows[2] = voiceRow(2, false, true, kVoiceStateReady);
    CHECK(PickNextAutoPlayVoice(rows, 0) == -1);
    rows[2] = voiceRow(2, true, false, kVoiceStateReady);
    CHECK(PickNextAutoPlayVoice(rows, 0) == -1);
    rows[2] = voiceRow(2, false, false, kVoiceStateDownloading);
    CHECK(PickNextAutoPlayVoice(rows, 0) == -1);
    rows[2] = voiceRow(2, false, false, kVoiceStateDownloadFailed);
    CHECK(PickNextAutoPlayVoice(rows, 0) == -1);

    //中间夹了一条文字消息：停
    rows[2] = voiceRow(2, false, false, kVoiceStateReady);
    rows.insert(rows.begin() + 2, textRow(3));
    CHECK(PickNextAutoPlayVoice(rows, 0) == -1);

    //已经是最后一条、下标越界：停
    CHECK(PickNextAutoPlayVoice(rows, rows.size() - 1) == -1);
    CHECK(PickNextAutoPlayVoice(rows, rows.size() + 3) == -1);

    //列表上的同一判断：按 msgid 找到正在播的那条，取紧挨着的下一条
    ChatMessageList list;
    list.AppendMessage(voiceRow(11, false, true, kVoiceStateReady));
    list.AppendMessage(voiceRow(12, false, false, kVoiceStateReady));
    list.AppendMessage(voiceRow(13, false, false, kVoiceStateReady));
    ChatMessage next;
    REQUIRE(list.NextAutoPlayVoice(11, "", next));
    CHECK(next.msgid == 12);
    CHECK_FALSE(list.NextAutoPlayVoice(13, "", next));
    CHECK_FALSE(list.NextAutoPlayVoice(99, "", next));
}

TEST_CASE("VM31 点语音气泡：就绪的播放或停止，下载中提示，下载失败重下，本人的也能播", "[voicemsg]")
{
    ChatMessage ready = voiceRow(1, false, false, kVoiceStateReady);
    CHECK(DecideVoiceClick(ready) == kVoiceClickPlay);
    ready.voicePlaying = true;
    CHECK(DecideVoiceClick(ready) == kVoiceClickStop);

    CHECK(DecideVoiceClick(voiceRow(2, false, false, kVoiceStateDownloading)) == kVoiceClickWaitDownload);
    CHECK(DecideVoiceClick(voiceRow(3, false, false, kVoiceStateDownloadFailed)) == kVoiceClickRetryDownload);

    //本人发送中、发送失败的语音：文件就在本机，照样能播
    CHECK(DecideVoiceClick(voiceRow(4, true, true, kVoiceStateSending)) == kVoiceClickPlay);
    ChatMessage failed = voiceRow(5, true, true, kVoiceStateReady);
    failed.sendFailed = true;
    CHECK(DecideVoiceClick(failed) == kVoiceClickPlay);

    //就绪却没有本地路径（不应出现）：按需要重新下载处理
    ChatMessage noPath = voiceRow(6, false, false, kVoiceStateReady);
    noPath.voiceLocalPath.Empty();
    CHECK(DecideVoiceClick(noPath) == kVoiceClickRetryDownload);

    CHECK_FALSE(VoiceDownloadingHint().empty());
    CHECK_FALSE(VoicePlayFailedHint().empty());
}

TEST_CASE("VM32 列表上的播放状态：播放中、红点消失、同列表只播一条、图标轮换、播完恢复", "[voicemsg]")
{
    ChatMessageList list;
    list.AppendMessage(voiceRow(21, false, false, kVoiceStateReady));
    list.AppendMessage(voiceRow(22, false, false, kVoiceStateReady));
    list.AppendMessage(voiceRow(23, false, false, kVoiceStateDownloadFailed));

    REQUIRE(list.SetVoicePlaying(21, "", true));
    CHECK(rowOf(list, 21).voicePlaying);
    //开始播放即算已听，红点消失
    CHECK(rowOf(list, 21).voicePlayed);

    //同一列表开播另一条：前一条停止
    REQUIRE(list.SetVoicePlaying(22, "", true));
    CHECK(rowOf(list, 22).voicePlaying);
    CHECK_FALSE(rowOf(list, 21).voicePlaying);

    //进度推进时图标轮换：每 kVoicePlayFrameMs 毫秒换一帧，三帧循环
    CHECK(VoicePlayFrame(0) == 0);
    CHECK(VoicePlayFrame(kVoicePlayFrameMs) == 1);
    CHECK(VoicePlayFrame(kVoicePlayFrameMs * 2 + 1) == 2);
    CHECK(VoicePlayFrame(kVoicePlayFrameMs * 3) == 0);
    REQUIRE(list.SetVoicePlayFrame(22, "", 2));
    CHECK(rowOf(list, 22).voicePlayFrame == 2);

    //播完：恢复静止，图标回到第一帧
    REQUIRE(list.SetVoicePlaying(22, "", false));
    CHECK_FALSE(rowOf(list, 22).voicePlaying);
    CHECK(rowOf(list, 22).voicePlayFrame == 0);

    //全部停止（窗口关闭、开始录音时）
    REQUIRE(list.SetVoicePlaying(21, "", true));
    list.StopAllVoicePlaying();
    CHECK_FALSE(rowOf(list, 21).voicePlaying);

    //下载失败的重新下载：改回下载中；只改该 md5 的、下载失败的那几条
    CHECK(list.MarkVoiceDownloading("md5-23") == 1);
    CHECK(rowOf(list, 23).voiceState == kVoiceStateDownloading);
    CHECK(list.MarkVoiceDownloading("md5-21") == 0);

    //列表里没有的：不改
    CHECK_FALSE(list.SetVoicePlaying(99, "", true));
}

TEST_CASE("VM33 语音中心的播放调度：只播一条、丢弃旧编号、窗口关闭与开始录音时停止", "[voicemsg]")
{
    FakePlayModule module;
    balloonvoice::CVoiceHub& hub = balloonvoice::CVoiceHub::GetInstance();
    hub.AttachModule(&module);
    FakePlayListener windowA;
    FakePlayListener windowB;

    //窗口 A 播第一条
    REQUIRE(hub.StartPlay(&windowA, playKey(31), L"C:\\voice\\a.ogg"));
    CHECK(module.m_playCount == 1);
    CHECK(module.m_lastPath == L"C:\\voice\\a.ogg");
    CHECK(module.m_lastToken != 0);
    CHECK(hub.IsPlayingFor(&windowA));
    const uint32_t tokenA = module.m_lastToken;

    //进度转给 A
    hub.OnPlayProgress(tokenA, 1000, kVpTotalMs);
    CHECK(windowA.m_progressCount == 1);
    CHECK(windowA.m_lastPlayedMs == 1000);
    CHECK(windowA.m_lastKey.m_msgid == 31);

    //窗口 B 播另一条：A 收到「已停止」，编号换新
    REQUIRE(hub.StartPlay(&windowB, playKey(32), L"C:\\voice\\b.ogg"));
    CHECK(windowA.m_endCount == 1);
    CHECK(windowA.m_lastEndReason == balloonvoice::voice_play_end_stopped);
    CHECK(module.m_lastToken != tokenA);
    const uint32_t tokenB = module.m_lastToken;
    CHECK_FALSE(hub.IsPlayingFor(&windowA));
    CHECK(hub.IsPlayingFor(&windowB));

    //A 那一次的旧编号：进度与结束都丢弃
    hub.OnPlayProgress(tokenA, 2000, kVpTotalMs);
    hub.OnPlayFinished(tokenA);
    CHECK(windowA.m_progressCount == 1);
    CHECK(windowA.m_endCount == 1);
    CHECK(windowB.m_endCount == 0);

    //B 播完：收到「播完」（可以连播）
    hub.OnPlayFinished(tokenB);
    CHECK(windowB.m_endCount == 1);
    CHECK(windowB.m_lastEndReason == balloonvoice::voice_play_end_finished);
    CHECK_FALSE(hub.IsPlayingFor(&windowB));

    //窗口关闭：停止它在播的，不再给它发通知
    REQUIRE(hub.StartPlay(&windowA, playKey(33), L"C:\\voice\\c.ogg"));
    const int stopsBefore = module.m_stopPlayCount;
    const int endsBefore = windowA.m_endCount;
    hub.DetachPlayListener(&windowA);
    CHECK(module.m_stopPlayCount == stopsBefore + 1);
    CHECK(windowA.m_endCount == endsBefore);
    CHECK_FALSE(hub.IsPlayingFor(&windowA));

    //开始录音：先停止播放，播放的窗口收到「已停止」
    REQUIRE(hub.StartPlay(&windowB, playKey(34), L"C:\\voice\\d.ogg"));
    struct NullRecordListener : public balloonvoice::IVoiceRecordListener
    {
        virtual void OnVoiceRecordLevel(int level) override
        {
            (void)level;
        }
        virtual void OnVoiceRecordLimitReached() override
        {
        }
    };
    NullRecordListener recorder;
    wchar_t tempDir[MAX_PATH] = { 0 };
    ::GetTempPathW(MAX_PATH, tempDir);
    hub.StartRecord(&recorder, std::wstring(tempDir), "VM33REC");
    CHECK(windowB.m_lastEndReason == balloonvoice::voice_play_end_stopped);
    CHECK_FALSE(hub.IsPlayingFor(&windowB));
    hub.CancelRecord();

    //模块打不开扬声器：返回失败，不进入播放
    module.m_playOk = false;
    CHECK_FALSE(hub.StartPlay(&windowA, playKey(35), L"C:\\voice\\e.ogg"));
    CHECK_FALSE(hub.IsPlayingFor(&windowA));

    hub.AttachModule(NULL);
}

TEST_CASE("VM44 红点同步（消息列表）：别处听过后按 msgid 把这一条标为已听", "[voicemsg]")
{
    ChatMessageList list;
    list.AppendMessage(voiceRow(41, false, false, kVoiceStateReady));   //对方发来、未听
    list.AppendMessage(voiceRow(42, false, false, kVoiceStateReady));   //对方发来、未听（不应被连带改掉）
    list.AppendMessage(voiceRow(43, true, true, kVoiceStateReady));     //本人发的
    list.AppendMessage(textRow(44));                                    //文字消息

    //对方发来的未听语音：改为已听，只改这一条
    CHECK(list.MarkVoicePlayedByMsgid(41));
    CHECK(rowOf(list, 41).voicePlayed);
    CHECK_FALSE(rowOf(list, 41).voicePlaying);
    CHECK_FALSE(rowOf(list, 42).voicePlayed);

    //已听的再标一次、本人发的、文字消息、不存在的、msgid 为 0：都返回 false
    CHECK_FALSE(list.MarkVoicePlayedByMsgid(41));
    CHECK_FALSE(list.MarkVoicePlayedByMsgid(43));
    CHECK_FALSE(list.MarkVoicePlayedByMsgid(44));
    CHECK_FALSE(list.MarkVoicePlayedByMsgid(99));
    CHECK_FALSE(list.MarkVoicePlayedByMsgid(0));
    CHECK_FALSE(rowOf(list, 42).voicePlayed);
}
