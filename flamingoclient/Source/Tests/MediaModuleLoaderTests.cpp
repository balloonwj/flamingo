/**
 *  FlamingoMedia.dll 加载与模块生命周期单元测试（Suite MDL，标签 [mediadll]），MediaModuleLoaderTests.cpp
 *  覆盖（对应 docs/屏幕共享与录制方案.md 第 1a 步，2026-10-02 与用户确认的用例）：
 *    - M1 屏幕采集类功能的系统版本判定（纯逻辑）；
 *    - M2 接口版本比对（纯逻辑）；
 *    - L1 加载不存在的路径、空路径；
 *    - L2 加载不是本模块的 DLL（系统的 version.dll）；
 *    - L3 接口版本不一致；
 *    - L4 正常加载、初始化、关闭、卸载，连做两轮；
 *    - L5 信令入口对未知命令与非法参数返回「未处理」；
 *    - L6 重复初始化、非法初始化参数、未初始化就关闭、重复关闭；
 *    - L7 文件存在但不是 DLL：按「无法加载」处理，加载前后线程的错误模式不变（2026-10-04 补）。
 *  第 1b 步扩充 L4：初始化日志改经 LOG_INFO 桥接写出，宿主收到的那一行须带调用处函数签名。
 *  L1~L6 真正加载 FlamingoMedia.dll：FlamingoUnit 工程构建后会把同配置的 DLL 复制到测试程序
 *  所在目录（见 FlamingoUnit.vcxproj 末尾的 CopyFlamingoMediaDll）。
 *  框架 Catch2 v3。
 *  balloonwj@qq.com   2026-10-02
 */
#include "catch_amalgamated.hpp"

#include <windows.h>
#include <string>
#include <stdio.h>   // MDL-L7：写一个不是 DLL 的临时文件
#include <vector>

#include "../App/MediaModuleLoader.h"
#include "../../FlamingoMedia/Api/FlamingoMediaApi.h"
#include "../../FlamingoMedia/Common/OsVersionPolicy.h"

using namespace balloonapp;
using namespace flamingomedia;

namespace {

//模块初始化成功后写给宿主的那一行日志所含的固定文字（见 MediaModule.cpp）
static const char* kInitLogMarker = "FlamingoMedia initialized";

//第 1b 步起 DLL 日志经 LOG_INFO 桥接写出，正文前带调用处函数签名；初始化日志的调用处
static const char* kInitLogCallSite = "CMediaModule::Init";

//一个确定不归本模块处理的命令号（音视频与屏幕共享段之外）
static const uint16_t kUnknownCmd = 9999;

//一个属于音视频段的命令号（cmd_call_invite），用于检查非法参数的处理
static const uint16_t kCallInviteCmd = 1701;

//宿主桩报告的界面语言与界面字体（多语言第 3a 步），以及 DLL 据此写出的那一行日志
static const wchar_t* const kStubUiLanguage = L"en-US";
static const wchar_t* const kStubUiFont = L"Segoe UI";
static const char* kUiLanguageLogLine = "FlamingoMedia ui language en-US, ui font Segoe UI.";

//测试用宿主桩：记下 DLL 写来的每一行日志及其级别
class StubHost : public IMediaHost
{
public:
    virtual void Log(int level, const char* text) override
    {
        if (text != NULL)
        {
            m_levels.push_back(level);
            m_lines.push_back(text);
        }
    }

    virtual bool SendSignal(uint16_t cmd, const char* json, uint32_t len) override
    {
        (void)cmd;
        (void)json;
        (void)len;
        return false;
    }

    virtual const unsigned char* GetServerPublicKey() override
    {
        return NULL;
    }

    virtual int64_t GetSelfUserId() override
    {
        return 0;
    }

    virtual bool IsSoundMuted() override
    {
        return true;
    }

    virtual void AppendCallTip(int64_t peerUserId, const wchar_t* tip) override
    {
        (void)peerUserId;
        (void)tip;
    }

    virtual HWND GetMainWindow() override
    {
        return NULL;
    }

    virtual void OnScreenShareStatus(int ctype, int64_t cid, const char* roomId, int64_t hostId,
                                     const wchar_t* hostName, bool active, bool locked, bool forced,
                                     const wchar_t* title) override
    {
        (void)locked;
        (void)forced;
        (void)title;
        (void)ctype;
        (void)cid;
        (void)roomId;
        (void)hostId;
        (void)hostName;
        (void)active;
    }

    virtual bool GetUserDisplayName(int64_t userId, wchar_t* buf, uint32_t bufChars) override
    {
        (void)userId;
        if (buf != NULL && bufChars > 0)
            buf[0] = L'\0';
        return false;
    }

    virtual bool SendFileToConversation(int ctype, int64_t cid, const wchar_t* path) override
    {
        (void)ctype;
        (void)cid;
        (void)path;
        return false;
    }

    virtual bool PickConversationAndSendFile(const wchar_t* path) override
    {
        (void)path;
        return false;
    }

    //多语言（第 3a 步）：不翻译，原样返回原文
    virtual const wchar_t* Translate(const wchar_t* context, const wchar_t* source) override
    {
        (void)context;
        return source;
    }

    //多语言（第 3a 步）：报告固定的语言与字体，用例据此核对 DLL 写出的日志
    virtual bool GetUiLanguage(wchar_t* langBuf, uint32_t langChars, wchar_t* fontBuf, uint32_t fontChars) override
    {
        if (langBuf == NULL || langChars == 0 || fontBuf == NULL || fontChars == 0)
            return false;
        wcsncpy_s(langBuf, langChars, kStubUiLanguage, _TRUNCATE);
        wcsncpy_s(fontBuf, fontChars, kStubUiFont, _TRUNCATE);
        return true;
    }

    //鼠标效果（屏幕共享第 16 期）：用例不保存设置
    virtual void SaveMouseEffectSettings(const char* settingsText) override
    {
        (void)settingsText;
    }

    //语音消息：用例不录音也不播放，以下四个回调不会被调用
    virtual void OnVoiceRecordLevel(int level) override
    {
        (void)level;
    }

    virtual void OnVoiceRecordLimitReached() override
    {
    }

    virtual void OnVoicePlayProgress(uint32_t token, int64_t playedMs, int64_t totalMs) override
    {
        (void)token;
        (void)playedMs;
        (void)totalMs;
    }

    virtual void OnVoicePlayFinished(uint32_t token) override
    {
        (void)token;
    }

    //加载器用例不涉及 P2P：返回 NULL，远程桌面照旧只走中转
    virtual p2p::IP2PService* GetP2PService() override
    {
        return NULL;
    }

    //群通话的四个宿主方法（docs/群音视频通话方案.md 6.2）：加载器用例不调用
    virtual void OnGroupCallStatus(int64_t groupId, const char* roomId, bool active, int media, int count,
                                   int64_t initiatorId, int64_t startTime) override
    {
        (void)groupId;
        (void)roomId;
        (void)active;
        (void)media;
        (void)count;
        (void)initiatorId;
        (void)startTime;
    }

    virtual void OnGroupCallCardChanged(int64_t groupId, int64_t msgId, const char* contentJson) override
    {
        (void)groupId;
        (void)msgId;
        (void)contentJson;
    }

    virtual void RequestGroupCallInvitees(int64_t groupId, const int64_t* excludeIds, uint32_t excludeCount) override
    {
        (void)groupId;
        (void)excludeIds;
        (void)excludeCount;
    }

    virtual int GetGroupMemberRole(int64_t groupId, int64_t userId) override
    {
        (void)groupId;
        (void)userId;
        return -1;
    }

    virtual bool GetUserAvatar(int64_t userId, MediaAvatarInfo* out) override
    {
        (void)userId;
        (void)out;
        return false;
    }

    virtual bool GetGroupAvatar(int64_t groupId, MediaAvatarInfo* out) override
    {
        (void)groupId;
        (void)out;
        return false;
    }

    virtual void OnCallPresence(int ctype, int64_t cid, int media, bool inCall) override
    {
        (void)ctype;
        (void)cid;
        (void)media;
        (void)inCall;
    }

    //录像记录（接口版本 12）：本用例不涉及录制管理，全部按「本地库没有打开」回答
    virtual bool AddRecording(const MediaRecordingInfo* info) override
    {
        (void)info;
        return false;
    }

    virtual uint32_t ListRecordings(MediaRecordingInfo* out, uint32_t capacity) override
    {
        (void)out;
        (void)capacity;
        return 0;
    }

    virtual bool UpdateRecordingPath(int64_t id, const wchar_t* newPath) override
    {
        (void)id;
        (void)newPath;
        return false;
    }

    virtual bool RemoveRecording(int64_t id) override
    {
        (void)id;
        return false;
    }

    virtual bool IsCaptionAvailable() override
    {
        return false;
    }

    virtual caption::ICaptionModule* GetCaptionService() override
    {
        return NULL;
    }

    //分享链接的「发送邀请到…」（接口版本 15）：本用例不涉及
    virtual bool PickConversationsAndSendText(const wchar_t* /*text*/) override
    {
        return false;
    }

    //云端录制（接口第 16 版）：用例不调用
    virtual bool IsCloudRecordEnabled() override
    {
        return false;
    }

    virtual uint32_t StartCloudTransfer(const flamingomedia::MediaCloudTransfer* /*req*/) override
    {
        return 0;
    }

    virtual void CancelCloudTransfer(uint32_t /*id*/) override
    {
    }

    virtual bool PickConversations(const wchar_t* /*title*/, uint32_t /*token*/) override
    {
        return false;
    }

    //第 17 版：以图片消息发到会话；用例不调用
    virtual bool SendImageToConversation(int /*ctype*/, int64_t /*cid*/, const wchar_t* /*path*/) override
    {
        return false;
    }

    //含指定文字的日志行数
    int CountLinesContaining(const char* marker) const
    {
        int n = 0;
        for (size_t i = 0; i < m_lines.size(); ++i)
        {
            if (m_lines[i].find(marker) != std::string::npos)
            {
                ++n;
            }
        }
        return n;
    }

    //第一条含指定文字的日志的级别；没有时返回 0
    int LevelOfFirstLineContaining(const char* marker) const
    {
        for (size_t i = 0; i < m_lines.size(); ++i)
        {
            if (m_lines[i].find(marker) != std::string::npos)
            {
                return m_levels[i];
            }
        }
        return 0;
    }

    std::vector<int>         m_levels;   //各行日志的级别
    std::vector<std::string> m_lines;    //各行日志正文
};

//测试程序所在目录（不带结尾反斜杠）
static std::wstring ExeDir()
{
    wchar_t buf[MAX_PATH] = { 0 };
    ::GetModuleFileNameW(NULL, buf, MAX_PATH);
    std::wstring s(buf);
    size_t pos = s.find_last_of(L"\\/");
    return (pos == std::wstring::npos) ? s : s.substr(0, pos);
}

//测试程序旁边那份 FlamingoMedia.dll 的完整路径
static std::wstring MediaDllPath()
{
    return ExeDir() + L"\\" + FLAMINGO_MEDIA_DLL_NAME;
}

//合法的初始化参数（appDir 取测试程序所在目录）
static MediaInitParams MakeParams(const std::wstring& appDir)
{
    MediaInitParams p;
    p.m_cbSize = sizeof(MediaInitParams);
    p.m_appDir = appDir.c_str();
    return p;
}

} // namespace

TEST_CASE("MDL-M1 屏幕采集类功能的系统版本判定", "[mediadll]")
{
    //Windows 10 2004 及以上（含 Windows 11，版本号同样报告为 10.0）支持
    CHECK(IsScreenCaptureSupportedBuild(10, 0, 19041));
    CHECK(IsScreenCaptureSupportedBuild(10, 0, 19045));
    CHECK(IsScreenCaptureSupportedBuild(10, 0, 22631));
    //低于 2004 的 Windows 10、以及 Windows 7 / 8.1 不支持
    CHECK_FALSE(IsScreenCaptureSupportedBuild(10, 0, 19040));
    CHECK_FALSE(IsScreenCaptureSupportedBuild(10, 0, 18363));
    CHECK_FALSE(IsScreenCaptureSupportedBuild(6, 1, 7601));
    CHECK_FALSE(IsScreenCaptureSupportedBuild(6, 3, 9600));

    //取当前系统版本号应当成功，且至少是 Windows 7（6.1）
    uint32_t major = 0;
    uint32_t minor = 0;
    uint32_t build = 0;
    REQUIRE(GetOsVersion(major, minor, build));
    CHECK(major >= 6);
    CHECK(build > 0);
}

TEST_CASE("MDL-M2 接口版本比对", "[mediadll]")
{
    CHECK(IsApiVersionCompatible(kApiVersion, kApiVersion));
    CHECK_FALSE(IsApiVersionCompatible(kApiVersion, kApiVersion + 1));
    CHECK_FALSE(IsApiVersionCompatible(kApiVersion + 1, kApiVersion));
}

TEST_CASE("MDL-L1 加载不存在的路径与空路径", "[mediadll]")
{
    CMediaModuleLoader loader;
    std::wstring missing = ExeDir() + L"\\NoSuchDir_mdl\\FlamingoMedia.dll";
    CHECK(loader.Load(missing.c_str(), kApiVersion) == media_load_not_found);
    CHECK_FALSE(loader.IsLoaded());
    CHECK(loader.GetModule() == NULL);

    CHECK(loader.Load(NULL, kApiVersion) == media_load_bad_path);
    CHECK(loader.Load(L"", kApiVersion) == media_load_bad_path);
    CHECK_FALSE(loader.IsLoaded());

    //每个结果都有自己的英文描述，不会落到 "unknown"
    const MediaLoadResult all[] = { media_load_ok, media_load_bad_path, media_load_not_found, media_load_failed,
                                    media_load_entry_missing, media_load_version_mismatch };
    for (size_t i = 0; i < sizeof(all) / sizeof(all[0]); ++i)
    {
        CHECK(std::string(CMediaModuleLoader::DescribeResult(all[i])) != "unknown");
    }
    //卸载未加载的加载器是安全的
    loader.Unload();
}

TEST_CASE("MDL-L2 加载不是本模块的 DLL", "[mediadll]")
{
    wchar_t sysDir[MAX_PATH] = { 0 };
    REQUIRE(::GetSystemDirectoryW(sysDir, MAX_PATH) > 0);
    std::wstring path = std::wstring(sysDir) + L"\\version.dll";
    CMediaModuleLoader loader;
    CHECK(loader.Load(path.c_str(), kApiVersion) == media_load_entry_missing);
    CHECK_FALSE(loader.IsLoaded());
}

TEST_CASE("MDL-L3 接口版本不一致", "[mediadll]")
{
    std::wstring path = MediaDllPath();
    REQUIRE(::GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES);
    CMediaModuleLoader loader;
    CHECK(loader.Load(path.c_str(), kApiVersion + 1) == media_load_version_mismatch);
    CHECK_FALSE(loader.IsLoaded());
    CHECK(loader.GetModule() == NULL);
}

TEST_CASE("MDL-L4 正常加载、初始化、关闭、卸载，连做两轮", "[mediadll]")
{
    std::wstring path = MediaDllPath();
    std::wstring dir = ExeDir();
    for (int round = 0; round < 2; ++round)
    {
        INFO("round " << round);
        CMediaModuleLoader loader;
        REQUIRE(loader.Load(path.c_str(), kApiVersion) == media_load_ok);
        REQUIRE(loader.IsLoaded());
        IMediaModule* m = loader.GetModule();
        REQUIRE(m != NULL);
        CHECK(m->GetApiVersion() == kApiVersion);

        StubHost host;
        MediaInitParams params = MakeParams(dir);
        CHECK(m->Init(&host, &params));
        CHECK(host.CountLinesContaining(kInitLogMarker) == 1);
        CHECK(host.LevelOfFirstLineContaining(kInitLogMarker) == media_log_info);
        //1b：这一行经 LOG_INFO 桥接写出，带调用处函数签名
        CHECK(host.LevelOfFirstLineContaining(kInitLogCallSite) == media_log_info);
        //多语言第 3a 步：初始化时向宿主取了界面语言与字体
        CHECK(host.CountLinesContaining(kUiLanguageLogLine) == 1);

        //功能查询与系统版本策略一致；不认识的功能返回 false
        uint32_t major = 0;
        uint32_t minor = 0;
        uint32_t build = 0;
        REQUIRE(GetOsVersion(major, minor, build));
        bool expect = IsScreenCaptureSupportedBuild(major, minor, build);
        CHECK(m->IsFeatureSupported(media_feature_screen_share) == expect);
        CHECK(m->IsFeatureSupported(media_feature_screen_record) == expect);
        CHECK_FALSE(m->IsFeatureSupported(0));

        m->Shutdown();
        loader.Unload();
        CHECK_FALSE(loader.IsLoaded());
        CHECK(loader.GetModule() == NULL);
    }
}

TEST_CASE("MDL-L5 信令入口对未知命令与非法参数返回未处理", "[mediadll]")
{
    std::wstring path = MediaDllPath();
    std::wstring dir = ExeDir();
    CMediaModuleLoader loader;
    REQUIRE(loader.Load(path.c_str(), kApiVersion) == media_load_ok);
    IMediaModule* m = loader.GetModule();

    //未初始化时任何信令都不处理
    CHECK_FALSE(m->OnSignal(kCallInviteCmd, "{}", 2));

    StubHost host;
    MediaInitParams params = MakeParams(dir);
    REQUIRE(m->Init(&host, &params));
    CHECK_FALSE(m->OnSignal(kUnknownCmd, "{}", 2));
    CHECK_FALSE(m->OnSignal(kCallInviteCmd, NULL, 0));
    CHECK_FALSE(m->OnSignal(kCallInviteCmd, NULL, 2));
    CHECK_FALSE(m->OnSignal(kCallInviteCmd, "{}", 0));
    m->Shutdown();
    loader.Unload();
}

TEST_CASE("MDL-L6 重复初始化、非法参数、未初始化就关闭", "[mediadll]")
{
    std::wstring path = MediaDllPath();
    std::wstring dir = ExeDir();
    CMediaModuleLoader loader;
    REQUIRE(loader.Load(path.c_str(), kApiVersion) == media_load_ok);
    IMediaModule* m = loader.GetModule();

    //未初始化就关闭、重复关闭：不崩溃
    m->Shutdown();
    m->Shutdown();

    StubHost host;
    MediaInitParams params = MakeParams(dir);
    //非法参数：宿主为空、参数为空、结构体大小不符、目录为空
    CHECK_FALSE(m->Init(NULL, &params));
    CHECK_FALSE(m->Init(&host, NULL));
    MediaInitParams badSize = params;
    badSize.m_cbSize = 0;
    CHECK_FALSE(m->Init(&host, &badSize));
    MediaInitParams noDir = params;
    noDir.m_appDir = NULL;
    CHECK_FALSE(m->Init(&host, &noDir));
    CHECK(host.CountLinesContaining(kInitLogMarker) == 0);

    //重复初始化：都返回成功，但只初始化一次
    CHECK(m->Init(&host, &params));
    CHECK(m->Init(&host, &params));
    CHECK(host.CountLinesContaining(kInitLogMarker) == 1);

    m->Shutdown();
    m->Shutdown();
    //关闭之后可以再次初始化
    CHECK(m->Init(&host, &params));
    CHECK(host.CountLinesContaining(kInitLogMarker) == 2);
    m->Shutdown();
    loader.Unload();
}

TEST_CASE("MDL-L7 文件存在但不是 DLL：按无法加载处理，不改变线程的错误模式", "[mediadll]")
{
    //2026-10-04 起加载期间屏蔽系统的「损坏的映像」对话框。测试进程已整体屏蔽错误框
    //（TestCrashDialogSetup.cpp），这里只核对返回值与错误模式的恢复；不弹框由 tmp/i18n_phase5/fallback_check.ps1
    //对真实程序做端到端检查
    wchar_t tempDir[MAX_PATH] = { 0 };
    REQUIRE(::GetTempPathW(MAX_PATH, tempDir) > 0);
    const std::wstring path = std::wstring(tempDir) + L"FlamingoMediaL7NotDll.dll";
    FILE* fp = NULL;
    REQUIRE(_wfopen_s(&fp, path.c_str(), L"wb") == 0);
    fputs("this is not a dll", fp);
    fclose(fp);

    const DWORD modeBefore = ::GetThreadErrorMode();
    CMediaModuleLoader loader;
    const MediaLoadResult result = loader.Load(path.c_str(), kApiVersion);
    const DWORD modeAfter = ::GetThreadErrorMode();
    ::DeleteFileW(path.c_str());

    CHECK(result == media_load_failed);
    CHECK_FALSE(loader.IsLoaded());
    CHECK(modeAfter == modeBefore);
}
