/**
 *  IMediaHost 的实现：把 FlamingoMedia.dll 需要的宿主能力接到 exe 现有的模块上 ——
 *  日志写进客户端日志（CIULog），信令经主连接按白名单发出（CIUSocket::SendMediaSignal），
 *  服务器公钥取自主连接，用户号与静音设置取自网络客户端（CFlamingoClient），
 *  通话提示经主面板追加到单聊窗。
 *
 *  除 AppendCallTip 只在 UI 线程调用外，其余方法都可能在 DLL 的任意线程上被调用；所用的
 *  CIULog、主连接发送队列、公钥与用户号的读取都是线程安全的。
 *
 *  典型用法（见 AppShell.cpp）：
 *      CMediaHostAdapter host;
 *      host.Attach(&client, &panel);
 *      loader.GetModule()->Init(&host, &params);
 *
 *  balloonwj@qq.com   2026-10-02（1c-1 起接通发信令，新增用户号、静音设置、通话提示）
 */
#pragma once

#include <windows.h>
#include <stdint.h>
#include "../../FlamingoMedia/Api/FlamingoMediaApi.h"

class CFlamingoClient;
class CBalloonMainPanel;

namespace balloonapp {

class CMediaHostAdapter : public flamingomedia::IMediaHost
{
public:
    CMediaHostAdapter();
    ~CMediaHostAdapter();

    /**
     *  登记宿主能力所依赖的 exe 对象。须在把本对象交给模块 Init 之前调用。
     *  @param pClient 网络客户端（取用户号与静音设置），不拥有；须比本对象活得久
     *  @param pPanel  主面板（追加通话提示），不拥有；须比本对象活得久
     */
    void Attach(CFlamingoClient* pClient, CBalloonMainPanel* pPanel);

    /**
     *  把 DLL 的一行日志写进客户端日志文件，日志行的函数签名一栏固定写 FlamingoMedia。
     *  @param level 级别，取值见 flamingomedia::MediaLogLevel；不认识的值按普通信息写
     *  @param text  日志正文（UTF-8 英文），为 NULL 时忽略
     */
    virtual void Log(int level, const char* text) override;

    /**
     *  经主连接发送一条信令（CIUSocket::SendMediaSignal，命令号须在白名单内）。
     *  @param cmd  命令号
     *  @param json 包体（UTF-8 JSON），不要求以 '\0' 结尾
     *  @param len  包体字节数
     *  @return 已放入发送队列返回 true；包体为空或命令号不在白名单返回 false
     */
    virtual bool SendSignal(uint16_t cmd, const char* json, uint32_t len) override;

    /**
     *  取 chatserver 的公钥。
     *  @return 指向公钥的指针（由 CIUSocket 单例持有，进程存续期间有效）；未配置时为 NULL
     */
    virtual const unsigned char* GetServerPublicKey() override;

    /**
     *  取当前登录用户的用户号。
     *  @return 用户号；未 Attach 或未登录时返回 0
     */
    virtual int64_t GetSelfUserId() override;

    /**
     *  用户是否在设置里关闭了提示音。
     *  @return 已静音返回 true；未 Attach 时返回 false
     */
    virtual bool IsSoundMuted() override;

    /**
     *  经主面板在与对方的单聊窗里追加一条通话提示（单聊窗未打开时主面板会先建窗）。只在 UI 线程调用。
     *  @param peerUserId 对方用户号；超出 32 位用户号范围时忽略
     *  @param tip        提示文字，为 NULL 时忽略
     */
    virtual void AppendCallTip(int64_t peerUserId, const wchar_t* tip) override;

    /**
     *  取主面板的窗口句柄（DLL 弹出窗口的居中参照、消息框属主）。只在 UI 线程调用。
     *  @return 主面板句柄；未 Attach 或主面板尚未建窗时返回 NULL
     */
    virtual HWND GetMainWindow() override;

    /**
     *  某会话的屏幕共享状态变化（有人开始、共享结束、查询结果），转给主面板刷新共享卡片与群聊横幅。只在 UI 线程调用。
     *  @param ctype    会话类型
     *  @param cid      从本人角度看的会话 id
     *  @param roomId   房间号；没有共享时为空串
     *  @param hostId   发起人 userid；没有共享时为 0
     *  @param hostName 发起人显示名；不知道时为空串
     *  @param active   该会话现在是否有进行中的共享
     *  @param locked   加入是否需要密码（第 12 期）
     *  @param forced   是否强制模式（第 13 期）
     *  @param title    共享标题（第 15 期）；可为 NULL，按空串处理
     */
    virtual void OnScreenShareStatus(int ctype, int64_t cid, const char* roomId, int64_t hostId,
                                     const wchar_t* hostName, bool active, bool locked, bool forced,
                                     const wchar_t* title) override;

    /**
     *  经主面板取一个用户的显示名（与会话列表同一口径）。只在 UI 线程调用。
     *  @param userId   用户号
     *  @param buf      [出参] 显示名，写不下时截断，以 '\0' 结尾
     *  @param bufChars buf 的容量（字符）
     *  @return 写入了显示名返回 true；参数非法或未 Attach 时返回 false
     */
    virtual bool GetUserDisplayName(int64_t userId, wchar_t* buf, uint32_t bufChars) override;

    //见 IMediaHost::SendFileToConversation：转给主面板打开聊天窗发送
    virtual bool SendFileToConversation(int ctype, int64_t cid, const wchar_t* path) override;

    //见 IMediaHost::SendImageToConversation（第 17 版）：转给主面板打开聊天窗，按图片消息发送
    virtual bool SendImageToConversation(int ctype, int64_t cid, const wchar_t* path) override;

    //见 IMediaHost::PickConversationAndSendFile：转给主面板弹出选会话窗口
    virtual bool PickConversationAndSendFile(const wchar_t* path) override;

    /**
     *  见 IMediaHost::Translate：转给 exe 的语言模块 i18n::Translate。可在任意线程调用。
     *  @param context 上下文，可为 NULL
     *  @param source  简体中文原文；为 NULL 时返回 NULL
     *  @return 当前语言的译文，在进程生命周期内有效；没有译文时返回 source
     */
    virtual const wchar_t* Translate(const wchar_t* context, const wchar_t* source) override;

    /**
     *  见 IMediaHost::GetUiLanguage：取 exe 当前生效的界面语言与界面字体。可在任意线程调用。
     *  @param langBuf   [出参] 语言代码，写不下时截断，以 '\0' 结尾
     *  @param langChars langBuf 的容量（字符）
     *  @param fontBuf   [出参] 界面字体名，写不下时截断，以 '\0' 结尾
     *  @param fontChars fontBuf 的容量（字符）
     *  @return 写入成功返回 true；参数非法返回 false
     */
    virtual bool GetUiLanguage(wchar_t* langBuf, uint32_t langChars, wchar_t* fontBuf, uint32_t fontChars) override;

    /**
     *  见 IMediaHost::SaveMouseEffectSettings（屏幕共享第 16 期）：写进当前账号的配置并保存，再经屏幕共享中心推回模块。
     *  @param settingsText 设置文本（ASCII）；为 NULL 时按空串处理
     */
    virtual void SaveMouseEffectSettings(const char* settingsText) override;

    /**
     *  见 IMediaHost::OnVoiceRecordLevel（语音消息）：转给语音消息中心，再由它转给正在录音的录音条。
     *  @param level 音量 0~255
     */
    virtual void OnVoiceRecordLevel(int level) override;

    //见 IMediaHost::OnVoiceRecordLimitReached（语音消息）：转给语音消息中心，再由它转给正在录音的录音条
    virtual void OnVoiceRecordLimitReached() override;

    /**
     *  见 IMediaHost::OnVoicePlayProgress（语音消息）。气泡播放在第 3e 步接入，在此之前 exe 不发起播放，不会收到。
     *  @param token    PlayVoice 时的编号
     *  @param playedMs 已播放毫秒数
     *  @param totalMs  总毫秒数
     */
    virtual void OnVoicePlayProgress(uint32_t token, int64_t playedMs, int64_t totalMs) override;

    /**
     *  见 IMediaHost::OnVoicePlayFinished（语音消息）。气泡播放在第 3e 步接入。
     *  @param token PlayVoice 时的编号
     */
    virtual void OnVoicePlayFinished(uint32_t token) override;

    /**
     *  见 IMediaHost::GetP2PService（P2P 方案第 6 期）：转交 exe 的 P2P 宿主已加载的服务对象。只在 UI 线程上调用。
     *  @return P2P 服务（借用）；FlamingoP2P.dll 没有加载时为 NULL
     */
    virtual p2p::IP2PService* GetP2PService() override;

    /**
     *  见 IMediaHost::OnGroupCallStatus：记进群通话中心（CGroupCallHub），再让已打开的群聊窗刷新横幅与通话卡片。
     *  只在 UI 线程上调用。
     */
    virtual void OnGroupCallStatus(int64_t groupId, const char* roomId, bool active, int media, int count,
                                   int64_t initiatorId, int64_t startTime) override;

    /**
     *  见 IMediaHost::OnGroupCallCardChanged：改写本地库里那张通话卡片的 content，并刷新已打开的群聊窗。只在 UI 线程上调用。
     */
    virtual void OnGroupCallCardChanged(int64_t groupId, int64_t msgId, const char* contentJson) override;

    /**
     *  见 IMediaHost::RequestGroupCallInvitees：请主面板以群成员为候选弹出选人框，确认后调 IMediaModule::InviteToGroupCall。
     *  只在 UI 线程上调用。
     */
    virtual void RequestGroupCallInvitees(int64_t groupId, const int64_t* excludeIds, uint32_t excludeCount) override;

    /**
     *  见 IMediaHost::GetGroupMemberRole：查主面板已加载的群成员资料。只在 UI 线程上调用。
     *  @return 0 普通成员、1 管理员、2 群主；不知道时 -1
     */
    virtual int GetGroupMemberRole(int64_t groupId, int64_t userId) override;

    /**
     *  见 IMediaHost::GetUserAvatar：取主面板联系人列表同一口径的头像（字形、底色，照片已在本机时的路径）。只在 UI 线程上调用。
     *  @return 已填写返回 true；参数非法、结构体大小不符返回 false
     */
    virtual bool GetUserAvatar(int64_t userId, flamingomedia::MediaAvatarInfo* out) override;

    /**
     *  见 IMediaHost::GetGroupAvatar：取主面板群列表同一口径的群头像。只在 UI 线程上调用。
     *  @return 已填写返回 true；参数非法、结构体大小不符、本人的群列表里没有这个群返回 false
     */
    virtual bool GetGroupAvatar(int64_t groupId, flamingomedia::MediaAvatarInfo* out) override;

    /**
     *  见 IMediaHost::OnCallPresence：转给主面板，刷新各列表里的「通话中」徽章。只在 UI 线程上调用。
     */
    virtual void OnCallPresence(int ctype, int64_t cid, int media, bool inCall) override;

    /**
     *  见 IMediaHost::AddRecording：写入本账号的录像记录本地库（转调 MediaRecordingBridge 的 AddRecordingToStore）。
     *  只在 UI 线程上调用。
     */
    virtual bool AddRecording(const flamingomedia::MediaRecordingInfo* info) override;

    /**
     *  见 IMediaHost::ListRecordings：从录像记录本地库列出记录（转调 ListRecordingsFromStore）。只在 UI 线程上调用。
     */
    virtual uint32_t ListRecordings(flamingomedia::MediaRecordingInfo* out, uint32_t capacity) override;

    /**
     *  见 IMediaHost::UpdateRecordingPath：修改一条录像记录的路径（转调 UpdateRecordingPathInStore）。只在 UI 线程上调用。
     */
    virtual bool UpdateRecordingPath(int64_t id, const wchar_t* newPath) override;

    /**
     *  见 IMediaHost::RemoveRecording：删除一条录像记录（转调 RemoveRecordingFromStore）。只在 UI 线程上调用。
     */
    virtual bool RemoveRecording(int64_t id) override;

    /**
     *  见 IMediaHost::IsCaptionAvailable：转调字幕宿主 CCaptionHost::IsAvailable。只在 UI 线程上调用。
     */
    virtual bool IsCaptionAvailable() override;

    /**
     *  见 IMediaHost::GetCaptionService：转调字幕宿主 CCaptionHost::AcquireModule（必要时加载字幕 DLL）。只在 UI 线程上调用。
     */
    virtual caption::ICaptionModule* GetCaptionService() override;

    /**
     *  见 IMediaHost::PickConversationsAndSendText：转调主面板的 OpenSendTextPicker（屏幕共享第 25 期「发送邀请到…」）。
     *  只在 UI 线程上调用。
     */
    virtual bool PickConversationsAndSendText(const wchar_t* text) override;
    //云端录制（接口第 16 版）：开关取自本人资料里的 features.cloudrecord
    virtual bool IsCloudRecordEnabled() override;
    //云端录像文件的上传与下载交给 ballooncloud::CCloudTransferRunner
    virtual uint32_t StartCloudTransfer(const flamingomedia::MediaCloudTransfer* req) override;
    virtual void CancelCloudTransfer(uint32_t id) override;
    //转发目标选择窗的「选会话」模式，结果经 IMediaModule::OnConversationsPicked 交回
    virtual bool PickConversations(const wchar_t* title, uint32_t token) override;

private:
    //录像记录操作之前，按当前账号的个人目录打开录像记录本地库（已打开同一个库时立即返回，换了账号时改开新库）。
    //主面板登录后也会打开它，这里兜底：保证登录后不论先做什么，录制管理都能读写
    void ensureRecordStore();

    CFlamingoClient*   m_pClient;   //网络客户端（不拥有）；Attach 之前为 NULL
    CBalloonMainPanel* m_pPanel;    //主面板（不拥有）；Attach 之前为 NULL
};

} // namespace balloonapp
