/**
 *  语音消息录音条的单元测试（前缀 VM，标签 [voicemsg]），用例见 docs/语音消息方案.md 第 9 节与第 3b 步：
 *    - VM16 计时文字：0:00、0:07、1:05，录满后停在 2:00；
 *    - VM17 音量柱：新的音量从右边进来、旧的往左移；音量 0 是最矮的一格、255 是满高；
 *    - VM18 提示文字：麦克风被通话占用、麦克风打不开各有一条；不到 1 秒点发送不发，提示「说话时间太短」；
 *    - VM19 录音文件：录在 ChatVoice 下的 rec_<clientmsgid>.ogg，录完按 md5 改名为 <md5>.ogg；不到 1 秒或取消时
 *           删除文件；录音的通知只转给发起录音的录音条；另一个录音条在录时开始返回「已在录音」；
 *    - VM40 语音中心的暂停与继续（转给模块，模块拒绝继续时保持暂停，继续前停止正在播放的语音），暂停中停止或取消，
 *           丢弃已停止的录音（删新写出的文件，不删缓存里原有的同内容文件）（第 6e 步）；
 *    - VM43 试听播放（录音会话 CVoiceRecordSession）：开始试听会停掉列表里正在播的语音，列表开始播放会停掉试听；
 *           试听播完按钮回到「试听」、时间回到总时长；试听状态下发送、确认放弃、关窗分别发送或删除那段录音（第 6g 步）；
 *    - VM49 暂停中试听（2026-10-05）：语音中心把已录的部分另存成 rec_<id>_paused.ogg 再播放，录音保持暂停；进度写在
 *           计时位置，播完复原；另存失败时提示；试听中继续、发送、取消、关窗都先停止试听并删除另存的文件。
 *  用模拟的 FlamingoMedia 模块，不碰真实麦克风。框架 Catch2 v3。
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

#include "../../FlamingoMedia/Api/FlamingoMediaApi.h"
#include "../Feature/Voice/VoiceHub.h"
#include "../UI/BalloonChatWnd/VoiceRecordBarModel.h"
#include "../UI/BalloonChatWnd/VoiceRecordSession.h"
#include "../UI/BalloonChatWnd/ChatWindowLayout.h"
#include "../../../third-party/balloonui/balloonui/Controls/Layout/DuiLayout.h"
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiButton.h"
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiLabel.h"

using namespace flamingomedia;

namespace {

//模拟的 FlamingoMedia 模块：录音时记下路径，停止时往该路径写一段固定内容并报告给定的时长
class FakeVoiceModule : public IMediaModule
{
public:
    FakeVoiceModule()
        : m_startResult(media_voice_record_ok)
        , m_durationMs(3000)
        , m_recording(false)
        , m_cancelCount(0)
        , m_paused(false)
        , m_pauseCount(0)
        , m_resumeCount(0)
        , m_resumeResult(media_voice_record_ok)
        , m_playCount(0)
        , m_stopPlayCount(0)
        , m_lastToken(0)
        , m_snapshotCount(0)
        , m_snapshotMs(1500)
        , m_snapshotFail(false)
    {
    }

    virtual ~FakeVoiceModule()
    {
    }

    virtual int StartVoiceRecord(const wchar_t* path) override
    {
        if (m_startResult != media_voice_record_ok)
            return m_startResult;
        m_path = path;
        m_recording = true;
        m_paused = false;
        return media_voice_record_ok;
    }

    virtual bool StopVoiceRecord(MediaVoiceRecordInfo* info) override
    {
        if (!m_recording || info == NULL)
            return false;
        m_recording = false;
        m_paused = false;
        //写一段与内容相关的字节，使不同的 m_payload 得到不同的 md5
        HANDLE f = ::CreateFileW(m_path.c_str(), GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        if (f == INVALID_HANDLE_VALUE)
            return false;
        DWORD written = 0;
        ::WriteFile(f, m_payload.c_str(), static_cast<DWORD>(m_payload.size()), &written, NULL);
        ::CloseHandle(f);
        info->m_durationMs = m_durationMs;
        info->m_fileBytes = static_cast<int64_t>(m_payload.size());
        info->m_waveCount = kMediaVoiceWaveBars;
        for (int i = 0; i < kMediaVoiceWaveBars; ++i)
            info->m_wave[i] = static_cast<uint8_t>(i);
        return true;
    }

    virtual void CancelVoiceRecord() override
    {
        m_recording = false;
        m_paused = false;
        ++m_cancelCount;
    }

    virtual bool PauseVoiceRecord() override
    {
        if (!m_recording || m_paused)
            return false;
        m_paused = true;
        ++m_pauseCount;
        return true;
    }

    virtual int ResumeVoiceRecord() override
    {
        if (!m_recording || !m_paused)
            return media_voice_record_not_paused;
        ++m_resumeCount;
        //继续失败（如麦克风被占用）时保持暂停
        if (m_resumeResult == media_voice_record_ok)
            m_paused = false;
        return m_resumeResult;
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

    //暂停中另存已录的部分：写一个小文件（测试据此检查试听结束后它被删掉），报告 m_snapshotMs 的时长
    virtual bool SnapshotVoiceRecord(const wchar_t* path, int64_t* outDurationMs) override
    {
        if (!m_recording || !m_paused || path == NULL || outDurationMs == NULL || m_snapshotFail)
            return false;
        HANDLE f = ::CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        if (f == INVALID_HANDLE_VALUE)
            return false;
        DWORD written = 0;
        ::WriteFile(f, "snap", 4, &written, NULL);
        ::CloseHandle(f);
        ++m_snapshotCount;
        m_snapshotPath = path;
        *outDurationMs = m_snapshotMs;
        return true;
    }

    //以下为与语音无关的方法，用例不调用
    virtual uint32_t GetApiVersion() const override
    {
        return kApiVersion;
    }

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

    //播放：VM40 检查「继续录音前停止播放」时用，只记次数、不出声
    virtual bool PlayVoice(const wchar_t* path, uint32_t token) override
    {
        m_lastPlayPath = (path != NULL) ? path : L"";
        m_lastToken = token;
        ++m_playCount;
        return true;
    }

    virtual void StopVoicePlay() override
    {
        ++m_stopPlayCount;
    }


    int          m_startResult;   //StartVoiceRecord 要返回的结果
    int64_t      m_durationMs;    //StopVoiceRecord 报告的时长（毫秒）
    std::string  m_payload;       //StopVoiceRecord 写进文件的内容
    std::wstring m_path;          //StartVoiceRecord 收到的路径
    bool         m_recording;     //是否在录音
    int          m_cancelCount;   //CancelVoiceRecord 被调用的次数
    bool         m_paused;        //录音是否已暂停
    int          m_pauseCount;    //PauseVoiceRecord 成功的次数
    int          m_resumeCount;   //ResumeVoiceRecord 在暂停中被调用的次数
    int          m_resumeResult;  //ResumeVoiceRecord 在暂停中要返回的结果
    int          m_playCount;     //PlayVoice 被调用的次数
    int          m_stopPlayCount; //StopVoicePlay 被调用的次数
    uint32_t     m_lastToken;     //最近一次 PlayVoice 收到的播放编号（测试据此模拟「播完」）
    std::wstring m_lastPlayPath;  //最近一次 PlayVoice 收到的文件路径
    int          m_snapshotCount; //SnapshotVoiceRecord 成功的次数
    std::wstring m_snapshotPath;  //最近一次 SnapshotVoiceRecord 写出的文件路径
    int64_t      m_snapshotMs;    //SnapshotVoiceRecord 报告的时长（毫秒）
    bool         m_snapshotFail;  //为 true 时 SnapshotVoiceRecord 返回失败（模拟写文件失败）
};

//记录录音条收到的通知
class FakeBarListener : public balloonvoice::IVoiceRecordListener
{
public:
    FakeBarListener()
        : m_levels(0)
        , m_limits(0)
    {
    }

    virtual void OnVoiceRecordLevel(int level) override
    {
        (void)level;
        ++m_levels;
    }

    virtual void OnVoiceRecordLimitReached() override
    {
        ++m_limits;
    }

    int m_levels;   //音量通知次数
    int m_limits;   //录满通知次数
};

//模拟的发起播放的窗口：记下收到的播放结束通知
class FakeHubPlayListener : public balloonvoice::IVoicePlayListener
{
public:
    FakeHubPlayListener()
        : m_ended(0)
        , m_lastReason(-1)
    {
    }

    virtual void OnVoicePlayProgress(const balloonvoice::VoicePlayKey& key, int64_t playedMs, int64_t totalMs) override
    {
        (void)key;
        (void)playedMs;
        (void)totalMs;
    }

    virtual void OnVoicePlayEnded(const balloonvoice::VoicePlayKey& key, int reason) override
    {
        (void)key;
        ++m_ended;
        m_lastReason = reason;
    }

    int m_ended;        //收到播放结束的次数
    int m_lastReason;   //最近一次结束的原因，取值见 balloonvoice::VoicePlayEnd
};

//模拟的聊天窗（录音会话的宿主）：记下显隐、提示与发送
class FakeSessionHost : public balloonchatwnd::IVoiceRecordSessionHost
{
public:
    FakeSessionHost()
        : m_shown(false)
        , m_sendCount(0)
    {
    }

    virtual void ShowVoiceRecordBarUi(bool show) override
    {
        m_shown = show;
    }

    virtual void ShowVoiceRecordToast(const std::wstring& text) override
    {
        m_toasts.push_back(text);
    }

    virtual void SendRecordedVoice(const std::string& clientMsgId,
                                   const balloonvoice::VoiceRecordOutput& out) override
    {
        ++m_sendCount;
        m_sentClientMsgId = clientMsgId;
        m_sentPath = out.m_path;
    }

    bool                      m_shown;            //录音条是否显示
    int                       m_sendCount;        //发送的次数
    std::string               m_sentClientMsgId;  //最近一次发送的客户端消息 id
    std::wstring              m_sentPath;         //最近一次发送的文件路径
    std::vector<std::wstring> m_toasts;           //显示过的提示
};

//测试用的 ChatVoice 目录（系统临时目录下、带进程号），以反斜杠结尾
std::wstring testVoiceDir()
{
    wchar_t dir[MAX_PATH] = { 0 };
    ::GetTempPathW(MAX_PATH, dir);
    wchar_t sub[64] = { 0 };
    ::swprintf_s(sub, L"FlamingoUnit_%u_ChatVoice\\", static_cast<unsigned int>(::GetCurrentProcessId()));
    return std::wstring(dir) + sub;
}

bool exists(const std::wstring& path)
{
    return ::GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES;
}

} // namespace

TEST_CASE("VM16 录音条的计时文字：分:秒，秒补两位，录满后停在 2:00", "[voicemsg]")
{
    CHECK(balloonchatwnd::FormatVoiceElapsed(0) == L"0:00");
    CHECK(balloonchatwnd::FormatVoiceElapsed(999) == L"0:00");
    CHECK(balloonchatwnd::FormatVoiceElapsed(7400) == L"0:07");
    CHECK(balloonchatwnd::FormatVoiceElapsed(65000) == L"1:05");
    CHECK(balloonchatwnd::FormatVoiceElapsed(120000) == L"2:00");
    CHECK(balloonchatwnd::FormatVoiceElapsed(125000) == L"2:00");
    CHECK(balloonchatwnd::FormatVoiceElapsed(-5) == L"0:00");
}

TEST_CASE("VM17 音量柱：新的从右边进来、旧的往左移；音量 0 最矮、255 满高", "[voicemsg]")
{
    balloonchatwnd::VoiceLevelHistory history;
    REQUIRE(history.Levels().size() == static_cast<size_t>(balloonchatwnd::kVoiceBarCount));
    for (size_t i = 0; i < history.Levels().size(); ++i)
        CHECK(history.Levels()[i] == 0);
    history.Push(100);
    history.Push(200);
    CHECK(history.Levels().back() == 200);
    CHECK(history.Levels()[history.Levels().size() - 2] == 100);
    CHECK(history.Levels().front() == 0);
    //越界的音量夹到 0~255
    history.Push(300);
    CHECK(history.Levels().back() == 255);
    history.Push(-3);
    CHECK(history.Levels().back() == 0);
    history.Reset();
    CHECK(history.Levels().back() == 0);
    CHECK(history.Levels().size() == static_cast<size_t>(balloonchatwnd::kVoiceBarCount));

    //行高 24、最矮 2：音量 0 时柱高 2（上下各内缩 11），255 时满高（内缩 0），中间值在两者之间
    CHECK(balloonchatwnd::VoiceBarInset(0, 24, 2) == 11);
    CHECK(balloonchatwnd::VoiceBarInset(255, 24, 2) == 0);
    const int mid = balloonchatwnd::VoiceBarInset(128, 24, 2);
    CHECK(mid > 0);
    CHECK(mid < 11);
}

TEST_CASE("VM18 提示文字：麦克风被占用、打不开各有一条，成功不提示，不到 1 秒提示说话时间太短", "[voicemsg]")
{
    CHECK(balloonchatwnd::VoiceRecordStartHint(media_voice_record_ok).empty());
    CHECK(balloonchatwnd::VoiceRecordStartHint(media_voice_record_mic_busy) == L"通话中不能录制语音消息");
    const std::wstring device = balloonchatwnd::VoiceRecordStartHint(media_voice_record_device_error);
    CHECK(device.find(L"麦克风") != std::wstring::npos);
    CHECK(device != balloonchatwnd::VoiceRecordStartHint(media_voice_record_mic_busy));
    CHECK_FALSE(balloonchatwnd::VoiceRecordStartHint(media_voice_record_not_ready).empty());
    CHECK(balloonchatwnd::VoiceTooShortHint() == L"说话时间太短");
}

TEST_CASE("VM19 录音文件：rec_<clientmsgid>.ogg 录完按 md5 改名；太短或取消时删除；通知只给发起的录音条", "[voicemsg]")
{
    FakeVoiceModule module;
    balloonvoice::CVoiceHub& hub = balloonvoice::CVoiceHub::GetInstance();
    hub.AttachModule(&module);
    const std::wstring dir = testVoiceDir();
    FakeBarListener bar;
    FakeBarListener other;

    SECTION("录完：临时文件在 ChatVoice 下，停止后改名为 <md5>.ogg，临时文件不在了")
    {
        module.m_payload = "voice-payload-1";
        REQUIRE(hub.StartRecord(&bar, dir, "ABCDEF0123") == media_voice_record_ok);
        CHECK(module.m_path == dir + L"rec_ABCDEF0123.ogg");
        CHECK(hub.IsRecordingFor(&bar));
        CHECK_FALSE(hub.IsRecordingFor(&other));
        //通知只转给发起录音的录音条
        hub.OnRecordLevel(100);
        hub.OnRecordLimitReached();
        CHECK(bar.m_levels == 1);
        CHECK(bar.m_limits == 1);
        CHECK(other.m_levels == 0);
        //另一个录音条此时开始：返回「已在录音」
        CHECK(hub.StartRecord(&other, dir, "OTHER") == media_voice_record_already);

        balloonvoice::VoiceRecordOutput out;
        REQUIRE(hub.StopRecord(out) == balloonvoice::voice_stop_ok);
        std::string md5;
        REQUIRE(balloonvoice::ComputeVoiceFileMd5(out.m_path, md5));
        CHECK(out.m_md5 == md5);
        CHECK(md5.size() == 32);
        CHECK(out.m_path == dir + std::wstring(md5.begin(), md5.end()) + L".ogg");
        CHECK(out.m_durationMs == 3000);
        CHECK(out.m_fileBytes == static_cast<int64_t>(module.m_payload.size()));
        CHECK(out.m_wave.size() == static_cast<size_t>(kMediaVoiceWaveBars));
        CHECK(exists(out.m_path));
        CHECK_FALSE(exists(dir + L"rec_ABCDEF0123.ogg"));
        CHECK_FALSE(hub.IsRecordingFor(&bar));
        //停止之后的通知不再转给它
        hub.OnRecordLevel(100);
        CHECK(bar.m_levels == 1);
        ::DeleteFileW(out.m_path.c_str());
    }
    SECTION("不到 1 秒：返回太短，文件已删除")
    {
        module.m_payload = "short";
        module.m_durationMs = 600;
        REQUIRE(hub.StartRecord(&bar, dir, "SHORT01") == media_voice_record_ok);
        balloonvoice::VoiceRecordOutput out;
        CHECK(hub.StopRecord(out) == balloonvoice::voice_stop_too_short);
        CHECK_FALSE(exists(dir + L"rec_SHORT01.ogg"));
    }
    SECTION("取消：模块收到取消，临时文件不存在，之后停止返回没有在录音")
    {
        REQUIRE(hub.StartRecord(&bar, dir, "CANCEL01") == media_voice_record_ok);
        hub.CancelRecord();
        CHECK(module.m_cancelCount == 1);
        CHECK_FALSE(exists(dir + L"rec_CANCEL01.ogg"));
        balloonvoice::VoiceRecordOutput out;
        CHECK(hub.StopRecord(out) == balloonvoice::voice_stop_not_recording);
    }
    SECTION("聊天窗关闭：正在录音的录音条按取消处理；别的录音条不受影响")
    {
        REQUIRE(hub.StartRecord(&bar, dir, "DETACH01") == media_voice_record_ok);
        hub.DetachListener(&other);
        CHECK(hub.IsRecordingFor(&bar));
        hub.DetachListener(&bar);
        CHECK_FALSE(hub.IsRecordingFor(&bar));
        CHECK(module.m_cancelCount == 1);
    }
    SECTION("模块拒绝：麦克风被占用时不进入录音状态")
    {
        module.m_startResult = media_voice_record_mic_busy;
        CHECK(hub.StartRecord(&bar, dir, "BUSY01") == media_voice_record_mic_busy);
        CHECK_FALSE(hub.IsRecordingFor(&bar));
    }
    //模块卸载：之后开始录音返回不可用
    hub.CancelRecord();
    hub.AttachModule(NULL);
    CHECK_FALSE(hub.IsModuleReady());
    CHECK(hub.StartRecord(&bar, dir, "NOMODULE") == media_voice_record_not_ready);
    ::RemoveDirectoryW(dir.c_str());
}

TEST_CASE("VM40 语音中心的暂停、继续与丢弃已停止的录音", "[voicemsg]")
{
    FakeVoiceModule module;
    balloonvoice::CVoiceHub& hub = balloonvoice::CVoiceHub::GetInstance();
    hub.AttachModule(&module);
    const std::wstring dir = testVoiceDir();
    FakeBarListener bar;

    SECTION("暂停与继续转给模块；模块拒绝继续时保持暂停；暂停中停止照常写出文件")
    {
        //没有在录音：暂停失败，继续返回没有已暂停的录音
        CHECK_FALSE(hub.PauseRecord());
        CHECK(hub.ResumeRecord() == media_voice_record_not_paused);

        module.m_payload = "pause-payload-1";
        REQUIRE(hub.StartRecord(&bar, dir, "PAUSE01") == media_voice_record_ok);
        REQUIRE(hub.PauseRecord());
        CHECK(module.m_paused);
        CHECK(module.m_pauseCount == 1);
        CHECK(hub.IsRecordPaused());
        CHECK(hub.IsRecordingFor(&bar));
        CHECK_FALSE(hub.PauseRecord());

        CHECK(hub.ResumeRecord() == media_voice_record_ok);
        CHECK_FALSE(module.m_paused);
        CHECK_FALSE(hub.IsRecordPaused());
        CHECK(hub.ResumeRecord() == media_voice_record_not_paused);

        //暂停期间开始了通话：模块拒绝继续，结果原样返回，保持暂停
        REQUIRE(hub.PauseRecord());
        module.m_resumeResult = media_voice_record_mic_busy;
        CHECK(hub.ResumeRecord() == media_voice_record_mic_busy);
        CHECK(hub.IsRecordPaused());

        //暂停中停止：照常改名为 <md5>.ogg，暂停状态清掉
        balloonvoice::VoiceRecordOutput out;
        REQUIRE(hub.StopRecord(out) == balloonvoice::voice_stop_ok);
        CHECK(exists(out.m_path));
        CHECK_FALSE(hub.IsRecordPaused());
        CHECK_FALSE(hub.IsRecordingFor(&bar));
        ::DeleteFileW(out.m_path.c_str());
    }
    SECTION("暂停中取消：模块收到取消，临时文件不存在，暂停状态清掉")
    {
        REQUIRE(hub.StartRecord(&bar, dir, "PAUSE02") == media_voice_record_ok);
        REQUIRE(hub.PauseRecord());
        hub.CancelRecord();
        CHECK(module.m_cancelCount == 1);
        CHECK_FALSE(hub.IsRecordPaused());
        CHECK_FALSE(exists(dir + L"rec_PAUSE02.ogg"));
        CHECK(hub.ResumeRecord() == media_voice_record_not_paused);
    }
    SECTION("继续录音前停止正在播放的语音，并通知发起播放的窗口「已停止」")
    {
        REQUIRE(hub.StartRecord(&bar, dir, "PAUSE03") == media_voice_record_ok);
        REQUIRE(hub.PauseRecord());
        FakeHubPlayListener window;
        balloonvoice::VoicePlayKey key;
        key.m_msgid = 4071;
        REQUIRE(hub.StartPlay(&window, key, L"C:\\voice\\vm40.ogg"));
        const int stopsBefore = module.m_stopPlayCount;
        CHECK(hub.ResumeRecord() == media_voice_record_ok);
        CHECK_FALSE(hub.IsPlayingFor(&window));
        CHECK(window.m_ended == 1);
        CHECK(window.m_lastReason == balloonvoice::voice_play_end_stopped);
        CHECK(module.m_stopPlayCount == stopsBefore + 1);
        hub.CancelRecord();
    }
    SECTION("丢弃已停止的录音：新写出的文件删掉；缓存里原本就有的同内容文件不删")
    {
        module.m_payload = "discard-payload-1";
        REQUIRE(hub.StartRecord(&bar, dir, "DISC01") == media_voice_record_ok);
        balloonvoice::VoiceRecordOutput fresh;
        REQUIRE(hub.StopRecord(fresh) == balloonvoice::voice_stop_ok);
        CHECK_FALSE(fresh.m_reusedExisting);
        REQUIRE(exists(fresh.m_path));
        CHECK(hub.DiscardRecorded(fresh));
        CHECK_FALSE(exists(fresh.m_path));
        //空结果：什么也不删
        CHECK_FALSE(hub.DiscardRecorded(balloonvoice::VoiceRecordOutput()));

        //同内容的文件原本就在缓存里（如收到过同一段语音）：停止时直接用它，丢弃时不删
        module.m_payload = "discard-payload-2";
        REQUIRE(hub.StartRecord(&bar, dir, "DISC02") == media_voice_record_ok);
        balloonvoice::VoiceRecordOutput first;
        REQUIRE(hub.StopRecord(first) == balloonvoice::voice_stop_ok);
        REQUIRE(hub.StartRecord(&bar, dir, "DISC03") == media_voice_record_ok);
        balloonvoice::VoiceRecordOutput second;
        REQUIRE(hub.StopRecord(second) == balloonvoice::voice_stop_ok);
        CHECK(second.m_reusedExisting);
        CHECK(second.m_path == first.m_path);
        CHECK_FALSE(hub.DiscardRecorded(second));
        CHECK(exists(first.m_path));
        ::DeleteFileW(first.m_path.c_str());
    }
    hub.CancelRecord();
    hub.StopPlay();
    hub.AttachModule(NULL);
    ::RemoveDirectoryW(dir.c_str());
}

namespace {

//单聊窗录音条的按钮编号
balloonchatwnd::VoiceRecordBarIds sessionIds()
{
    balloonchatwnd::VoiceRecordBarIds ids;
    ids.m_pause = balloonchatwnd::kIdVoicePause;
    ids.m_stop = balloonchatwnd::kIdVoiceStop;
    ids.m_preview = balloonchatwnd::kIdVoicePreview;
    ids.m_cancel = balloonchatwnd::kIdVoiceCancel;
    ids.m_send = balloonchatwnd::kIdVoiceSend;
    ids.m_confirm = balloonchatwnd::kIdVoiceConfirm;
    ids.m_back = balloonchatwnd::kIdVoiceBack;
    return ids;
}

//按钮上的文字
CString buttonText(balloonwjui::DuiControl* root, unsigned int id)
{
    balloonwjui::DuiButton* b = dynamic_cast<balloonwjui::DuiButton*>(root->FindCtrlById(id));
    REQUIRE(b != NULL);
    return b->GetText();
}

} // namespace

TEST_CASE("VM43 试听播放：与列表里的语音互相停下，播完按钮复原；试听状态下发送、放弃、关窗", "[voicemsg]")
{
    FakeVoiceModule module;
    balloonvoice::CVoiceHub& hub = balloonvoice::CVoiceHub::GetInstance();
    hub.AttachModule(&module);
    const std::wstring dir = testVoiceDir();
    std::unique_ptr<balloonwjui::DuiControl> root = balloonchatwnd::BuildChatWindowBody(false);
    REQUIRE(root);
    balloonwjui::DuiHBox* holder =
        dynamic_cast<balloonwjui::DuiHBox*>(root->FindCtrlById(balloonchatwnd::kIdVoiceBar));
    REQUIRE(holder != NULL);
    const balloonchatwnd::VoiceRecordBarIds ids = sessionIds();
    FakeSessionHost host;
    balloonchatwnd::CVoiceRecordSession session;
    session.Init(&host, holder, ids);

    SECTION("试听与列表播放互相停下；播完按钮复原；发送那段已停止的录音")
    {
        module.m_payload = "preview-payload-1";
        REQUIRE(session.Start(dir, "PREV01"));
        CHECK(host.m_shown);
        CHECK(session.State() == balloonchatwnd::voice_rec_recording);
        //停止：进入试听，时间文字为总时长（模拟模块报告 3 秒）
        REQUIRE(session.HandleButton(ids.m_stop));
        CHECK(session.State() == balloonchatwnd::voice_rec_preview);
        CHECK(session.Bar().PreviewTimeLabel()->GetText() == L"0:03");

        //开始试听：语音中心在为录音会话播放
        REQUIRE(session.HandleButton(ids.m_preview));
        CHECK(session.Previewing());
        CHECK(hub.IsPlayingFor(&session));
        CHECK(buttonText(root.get(), ids.m_preview) == L"停止试听");
        //进度：时间文字写「播放进度 / 总时长」
        hub.OnPlayProgress(module.m_lastToken, 1000, 3000);
        CHECK(session.Bar().PreviewTimeLabel()->GetText() == L"0:01 / 0:03");

        //列表开始播放一条语音：试听被停下，按钮回到「试听」
        FakeHubPlayListener window;
        balloonvoice::VoicePlayKey key;
        key.m_msgid = 4301;
        REQUIRE(hub.StartPlay(&window, key, L"C:\\voice\\vm43.ogg"));
        CHECK_FALSE(session.Previewing());
        CHECK(buttonText(root.get(), ids.m_preview) == L"试听");
        CHECK(session.Bar().PreviewTimeLabel()->GetText() == L"0:03");

        //再开始试听：列表里正在播的语音被停下，那个窗口收到「已停止」
        REQUIRE(session.HandleButton(ids.m_preview));
        CHECK(window.m_ended == 1);
        CHECK(window.m_lastReason == balloonvoice::voice_play_end_stopped);
        CHECK_FALSE(hub.IsPlayingFor(&window));
        CHECK(hub.IsPlayingFor(&session));

        //试听播完：按钮回到「试听」，时间文字回到总时长
        hub.OnPlayFinished(module.m_lastToken);
        CHECK_FALSE(session.Previewing());
        CHECK(buttonText(root.get(), ids.m_preview) == L"试听");
        CHECK(session.Bar().PreviewTimeLabel()->GetText() == L"0:03");

        //试听中点「停止试听」
        REQUIRE(session.HandleButton(ids.m_preview));
        REQUIRE(session.HandleButton(ids.m_preview));
        CHECK_FALSE(session.Previewing());
        CHECK_FALSE(hub.IsPlayingFor(&session));

        //试听中按 Enter 发送：先停止试听，宿主收到已停止的那段录音，录音条收起
        REQUIRE(session.HandleButton(ids.m_preview));
        CHECK(session.HandleKey(VK_RETURN));
        CHECK_FALSE(hub.IsPlayingFor(&session));
        CHECK(host.m_sendCount == 1);
        CHECK(host.m_sentClientMsgId == "PREV01");
        CHECK(exists(host.m_sentPath));
        CHECK_FALSE(host.m_shown);
        CHECK(session.State() == balloonchatwnd::voice_rec_idle);
        //收起之后 Enter、Esc 交还给窗口
        CHECK_FALSE(session.HandleKey(VK_RETURN));
        CHECK_FALSE(session.HandleKey(VK_ESCAPE));
        ::DeleteFileW(host.m_sentPath.c_str());
    }
    SECTION("试听状态下取消并确认放弃：删除写出的文件，不发送")
    {
        module.m_payload = "preview-payload-2";
        REQUIRE(session.Start(dir, "PREV02"));
        REQUIRE(session.HandleButton(ids.m_stop));
        REQUIRE(session.HandleButton(ids.m_preview));
        REQUIRE(session.HandleButton(ids.m_cancel));
        CHECK(session.State() == balloonchatwnd::voice_rec_confirm_discard);
        CHECK_FALSE(hub.IsPlayingFor(&session));
        //「返回」：回到试听
        REQUIRE(session.HandleButton(ids.m_back));
        CHECK(session.State() == balloonchatwnd::voice_rec_preview);
        REQUIRE(session.HandleKey(VK_ESCAPE));
        CHECK(session.State() == balloonchatwnd::voice_rec_confirm_discard);
        //确认状态下 Enter 被截住但不发送
        CHECK(session.HandleKey(VK_RETURN));
        CHECK(session.State() == balloonchatwnd::voice_rec_confirm_discard);
        REQUIRE(session.HandleButton(ids.m_confirm));
        CHECK(session.State() == balloonchatwnd::voice_rec_idle);
        CHECK(host.m_sendCount == 0);
        CHECK_FALSE(host.m_shown);
        //写出的文件已删掉：目录里没有 .ogg
        WIN32_FIND_DATAW fd;
        HANDLE h = ::FindFirstFileW((dir + L"*.ogg").c_str(), &fd);
        CHECK(h == INVALID_HANDLE_VALUE);
        if (h != INVALID_HANDLE_VALUE)
            ::FindClose(h);
    }
    SECTION("录音中暂停与继续经语音中心转给模块；不到 1 秒点取消直接放弃")
    {
        REQUIRE(session.Start(dir, "PREV03"));
        REQUIRE(session.HandleButton(ids.m_pause));
        CHECK(session.State() == balloonchatwnd::voice_rec_paused);
        CHECK(module.m_paused);
        CHECK(buttonText(root.get(), ids.m_pause) == L"继续");
        //继续被拒绝（麦克风被通话占用）：提示原因，保持暂停
        module.m_resumeResult = media_voice_record_mic_busy;
        REQUIRE(session.HandleButton(ids.m_pause));
        CHECK(session.State() == balloonchatwnd::voice_rec_paused);
        CHECK_FALSE(host.m_toasts.empty());
        module.m_resumeResult = media_voice_record_ok;
        REQUIRE(session.HandleButton(ids.m_pause));
        CHECK(session.State() == balloonchatwnd::voice_rec_recording);
        CHECK_FALSE(module.m_paused);
        CHECK(buttonText(root.get(), ids.m_pause) == L"暂停");
        //刚开始录（不到 1 秒）点取消：直接放弃，不确认
        REQUIRE(session.HandleButton(ids.m_cancel));
        CHECK(session.State() == balloonchatwnd::voice_rec_idle);
        CHECK(module.m_cancelCount == 1);
        CHECK_FALSE(host.m_shown);
    }
    SECTION("关窗：试听状态的录音文件删除，不再接收语音中心的通知")
    {
        module.m_payload = "preview-payload-3";
        REQUIRE(session.Start(dir, "PREV04"));
        REQUIRE(session.HandleButton(ids.m_stop));
        REQUIRE(session.HandleButton(ids.m_preview));
        session.OnWindowClosing();
        CHECK(session.State() == balloonchatwnd::voice_rec_idle);
        CHECK_FALSE(hub.IsPlayingFor(&session));
        WIN32_FIND_DATAW fd;
        HANDLE h = ::FindFirstFileW((dir + L"*.ogg").c_str(), &fd);
        CHECK(h == INVALID_HANDLE_VALUE);
        if (h != INVALID_HANDLE_VALUE)
            ::FindClose(h);
    }
    session.OnWindowClosing();
    hub.StopPlay();
    hub.CancelRecord();
    hub.AttachModule(NULL);
    ::RemoveDirectoryW(dir.c_str());
}

TEST_CASE("VM49 暂停中试听：另存已录的部分再播放，进度写在计时位置，继续、发送、取消、关窗时删除另存的文件", "[voicemsg]")
{
    FakeVoiceModule module;
    balloonvoice::CVoiceHub& hub = balloonvoice::CVoiceHub::GetInstance();
    hub.AttachModule(&module);
    const std::wstring dir = testVoiceDir();
    std::unique_ptr<balloonwjui::DuiControl> root = balloonchatwnd::BuildChatWindowBody(false);
    REQUIRE(root);
    balloonwjui::DuiHBox* holder =
        dynamic_cast<balloonwjui::DuiHBox*>(root->FindCtrlById(balloonchatwnd::kIdVoiceBar));
    REQUIRE(holder != NULL);
    const balloonchatwnd::VoiceRecordBarIds ids = sessionIds();
    FakeSessionHost host;
    balloonchatwnd::CVoiceRecordSession session;
    session.Init(&host, holder, ids);

    SECTION("另存的条件与文件名")
    {
        std::wstring snap;
        int64_t ms = 0;
        //没有在录音、录音中没有暂停：不能另存
        CHECK_FALSE(hub.SnapshotPausedRecord(snap, ms));
        REQUIRE(session.Start(dir, "PAUS01"));
        CHECK_FALSE(hub.SnapshotPausedRecord(snap, ms));
        CHECK(module.m_snapshotCount == 0);
        //文件在录音临时文件旁边：rec_<clientmsgid>_paused.ogg
        CHECK(balloonvoice::VoicePausedSnapshotPath(dir + L"rec_PAUS01.ogg") == dir + L"rec_PAUS01_paused.ogg");
        CHECK(balloonvoice::VoicePausedSnapshotPath(L"C:\\v\\rec_X") == L"C:\\v\\rec_X_paused.ogg");
        session.OnWindowClosing();
    }
    SECTION("暂停中试听：录音保持暂停，播放进度写在计时位置，播完复原，试听中点继续先停止试听并删除另存的文件")
    {
        module.m_snapshotMs = 2000;
        REQUIRE(session.Start(dir, "PAUS02"));
        REQUIRE(session.HandleButton(ids.m_pause));
        CHECK(session.State() == balloonchatwnd::voice_rec_paused);
        CHECK(buttonText(root.get(), ids.m_preview) == L"试听");

        REQUIRE(session.HandleButton(ids.m_preview));
        CHECK(session.Previewing());
        CHECK(session.State() == balloonchatwnd::voice_rec_paused);
        CHECK(module.m_paused);
        CHECK(module.m_snapshotCount == 1);
        CHECK(module.m_snapshotPath == dir + L"rec_PAUS02_paused.ogg");
        CHECK(module.m_lastPlayPath == module.m_snapshotPath);
        CHECK(exists(module.m_snapshotPath));
        CHECK(hub.IsPlayingFor(&session));
        CHECK(buttonText(root.get(), ids.m_preview) == L"停止试听");
        CHECK(session.Bar().PreviewTimeLabel()->IsVisible());
        CHECK_FALSE(session.Bar().TimerLabel()->IsVisible());
        CHECK(session.Bar().PreviewTimeLabel()->GetText() == L"0:00 / 0:02");
        hub.OnPlayProgress(module.m_lastToken, 1000, 2000);
        CHECK(session.Bar().PreviewTimeLabel()->GetText() == L"0:01 / 0:02");

        //播完：按钮回到「试听」，计时位置换回已录时长，仍是已暂停
        hub.OnPlayFinished(module.m_lastToken);
        CHECK_FALSE(session.Previewing());
        CHECK(session.State() == balloonchatwnd::voice_rec_paused);
        CHECK(buttonText(root.get(), ids.m_preview) == L"试听");
        CHECK(session.Bar().TimerLabel()->IsVisible());
        CHECK_FALSE(session.Bar().PreviewTimeLabel()->IsVisible());

        //再试听一次：覆盖同一个文件
        REQUIRE(session.HandleButton(ids.m_preview));
        CHECK(module.m_snapshotCount == 2);
        CHECK(session.Previewing());

        //试听中点「继续」：先停止试听，再继续录音；另存的文件删除
        const int stopsBefore = module.m_stopPlayCount;
        REQUIRE(session.HandleButton(ids.m_pause));
        CHECK(session.State() == balloonchatwnd::voice_rec_recording);
        CHECK_FALSE(session.Previewing());
        CHECK_FALSE(hub.IsPlayingFor(&session));
        CHECK(module.m_stopPlayCount > stopsBefore);
        CHECK(module.m_resumeCount == 1);
        CHECK_FALSE(module.m_paused);
        CHECK_FALSE(exists(module.m_snapshotPath));
        CHECK_FALSE(session.Bar().PreviewTimeLabel()->IsVisible());
        session.OnWindowClosing();
    }
    SECTION("另存失败：提示，按钮仍为「试听」，没有播放")
    {
        module.m_snapshotFail = true;
        REQUIRE(session.Start(dir, "PAUS03"));
        REQUIRE(session.HandleButton(ids.m_pause));
        const size_t toastsBefore = host.m_toasts.size();
        REQUIRE(session.HandleButton(ids.m_preview));
        CHECK_FALSE(session.Previewing());
        CHECK(module.m_playCount == 0);
        CHECK(host.m_toasts.size() == toastsBefore + 1);
        CHECK(buttonText(root.get(), ids.m_preview) == L"试听");
        CHECK(session.State() == balloonchatwnd::voice_rec_paused);
        session.OnWindowClosing();
    }
    SECTION("暂停中试听时发送、取消、关窗：先停止试听，另存的文件删除")
    {
        //Enter 发送：先停止试听，再停止录音并发送已录的全部内容，另存的文件删除
        module.m_payload = "paused-payload-4";
        REQUIRE(session.Start(dir, "PAUS04"));
        REQUIRE(session.HandleButton(ids.m_pause));
        REQUIRE(session.HandleButton(ids.m_preview));
        const std::wstring snap4 = module.m_snapshotPath;
        REQUIRE(exists(snap4));
        CHECK(session.HandleKey(VK_RETURN));
        CHECK(session.State() == balloonchatwnd::voice_rec_idle);
        CHECK_FALSE(session.Previewing());
        CHECK_FALSE(hub.IsPlayingFor(&session));
        CHECK(host.m_sendCount == 1);
        CHECK(host.m_sentClientMsgId == "PAUS04");
        CHECK_FALSE(exists(snap4));
        ::DeleteFileW(host.m_sentPath.c_str());

        //取消：刚录不到 1 秒直接放弃（够 1 秒时确认放弃），另存的文件删除
        REQUIRE(session.Start(dir, "PAUS05"));
        REQUIRE(session.HandleButton(ids.m_pause));
        REQUIRE(session.HandleButton(ids.m_preview));
        const std::wstring snap5 = module.m_snapshotPath;
        REQUIRE(exists(snap5));
        REQUIRE(session.HandleButton(ids.m_cancel));
        if (session.State() == balloonchatwnd::voice_rec_confirm_discard)
            REQUIRE(session.HandleButton(ids.m_confirm));
        CHECK(session.State() == balloonchatwnd::voice_rec_idle);
        CHECK_FALSE(hub.IsPlayingFor(&session));
        CHECK_FALSE(exists(snap5));

        //关窗
        REQUIRE(session.Start(dir, "PAUS06"));
        REQUIRE(session.HandleButton(ids.m_pause));
        REQUIRE(session.HandleButton(ids.m_preview));
        const std::wstring snap6 = module.m_snapshotPath;
        REQUIRE(exists(snap6));
        session.OnWindowClosing();
        CHECK_FALSE(hub.IsPlayingFor(&session));
        CHECK_FALSE(exists(snap6));
    }
    session.OnWindowClosing();
    hub.StopPlay();
    hub.CancelRecord();
    hub.AttachModule(NULL);
    //发送那一节写出的录音已在用例里删除；目录里不应再有 .ogg
    WIN32_FIND_DATAW fd;
    HANDLE h = ::FindFirstFileW((dir + L"*.ogg").c_str(), &fd);
    CHECK(h == INVALID_HANDLE_VALUE);
    if (h != INVALID_HANDLE_VALUE)
        ::FindClose(h);
    ::RemoveDirectoryW(dir.c_str());
}
