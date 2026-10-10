/**
 *  语音字幕宿主的纯逻辑部分（docs/语音字幕方案.md 第 4.1 节、第 2 节 D5 / D6）：字幕当前能否使用、
 *  该不该加载字幕 DLL、按账号保存的字幕设置文件放在哪里，以及读写这个文件。不依赖窗口与 DLL，单测见
 *  Tests/CaptionHostTests.cpp（标签 [captionhost]）。CCaptionHost（CaptionHost.h）据此决定加载与通知。
 *
 *  典型用法：
 *      captionhost::CaptionGateState s;
 *      s.m_loggedIn = true;
 *      s.m_featureEnabled = true;
 *      bool usable = captionhost::IsCaptionAvailable(s);
 *      std::wstring path = captionhost::SettingsFilePath(personalDir);
 *
 *  balloonwj@qq.com   2026-10-06
 */
#pragma once

#include <string>

namespace captionhost {

//字幕设置文件名：放在当前账号的个人目录下，内容为 UTF-8 JSON（格式见 FlamingoCaption/Model/CaptionSettings.h）
extern const wchar_t* const kCaptionSettingsFileName;

//字幕设置文件的大小上限（字节）：替换表再长也到不了这个量级，超过时视为损坏、不读取
const unsigned long kCaptionSettingsMaxBytes = 1024 * 1024;

//决定字幕能否使用的几项状态
struct CaptionGateState
{
    bool m_loggedIn;        //是否已登录（退出登录后一律不可用）
    bool m_featureEnabled;  //本企业的字幕开关（登录应答 features.caption，运行中经 1013 更新）
    bool m_loadFailed;      //本进程是否已经加载 FlamingoCaption.dll 失败过（失败后不再重试，D6）

    CaptionGateState()
        : m_loggedIn(false)
        , m_featureEnabled(false)
        , m_loadFailed(false)
    {
    }
};

/**
 *  字幕当前能否使用：已登录、开关打开、DLL 没有加载失败过。
 *  @param state 当前状态
 *  @return 可以使用返回 true
 */
bool IsCaptionAvailable(const CaptionGateState& state);

/**
 *  取字幕模块时该不该加载 DLL：能使用且尚未加载。
 *  @param state    当前状态
 *  @param isLoaded DLL 是否已经加载并初始化成功
 *  @return 需要加载返回 true
 */
bool ShouldLoadCaptionDll(const CaptionGateState& state, bool isLoaded);

/**
 *  字幕设置文件的完整路径。
 *  @param personalDir 当前账号的个人目录（CUserMgr::GetPersonalFolder 的结果，带不带结尾反斜杠都可以）
 *  @return 个人目录下的 kCaptionSettingsFileName；个人目录为空时返回空串
 */
std::wstring SettingsFilePath(const std::wstring& personalDir);

/**
 *  读字幕设置文件。
 *  @param path 文件路径
 *  @param text [出参] 文件内容（UTF-8，去掉开头的 BOM）；读不到时清空
 *  @return 文件存在、大小不超过 kCaptionSettingsMaxBytes 且读取成功时返回 true
 */
bool ReadSettingsFile(const std::wstring& path, std::string& text);

/**
 *  写字幕设置文件：先写同目录的临时文件再替换，写到一半断电也不会留下半个文件。
 *  @param path 文件路径
 *  @param text 文件内容（UTF-8）
 *  @return 写入并替换成功返回 true
 */
bool WriteSettingsFile(const std::wstring& path, const std::string& text);

} // namespace captionhost
