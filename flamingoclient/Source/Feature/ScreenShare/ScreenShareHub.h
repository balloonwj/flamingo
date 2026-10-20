/**
 *  ScreenShareHub.h
 *
 *  exe 侧的屏幕共享中心：聊天窗与 FlamingoMedia.dll 之间关于屏幕共享的唯一衔接点。
 *    - 转发三种操作给 DLL：头部「共享屏幕」按钮发起共享、卡片「加入」加入共享、打开聊天窗时查询会话里有无共享；
 *    - 记录 DLL 回报的各会话共享状态（ScreenShareStatusCache），聊天窗据此决定卡片的状态与「加入」按钮是否可点，
 *      群聊窗据此显示横幅。
 *  屏幕共享的状态机、窗口与媒体管线都在 DLL 里（FlamingoMedia/Share/ShareController），本类不解析任何信令。
 *
 *  进程内唯一（GetInstance），所有方法只在 UI 线程调用。模块指针由主面板在 SetMediaModule 时登记，DLL 未加载时
 *  各操作返回 false，聊天窗据 IsModuleReady 隐藏「共享屏幕」按钮。
 *
 *  典型用法（单聊窗头部按钮）：
 *      balloonscreenshare::CScreenShareHub::GetInstance().StartShare(conversation_single, peerId, peerName);
 *
 *  balloonwj@qq.com   2026-10-03
 */
#pragma once

#include <stdint.h>
#include <string>

#include "ScreenShareStatusCache.h"

namespace flamingomedia { class IMediaModule; }

namespace balloonscreenshare {

//exe 侧的屏幕共享中心
class CScreenShareHub
{
public:
    //取进程内唯一的对象（函数内静态变量）
    static CScreenShareHub& GetInstance();

    /**
     *  登记 FlamingoMedia.dll 的模块接口。
     *  @param module 模块接口，不拥有；传 NULL 表示模块不可用（卸载前调用）
     */
    void AttachModule(flamingomedia::IMediaModule* module);

    //模块是否已加载；未加载时聊天窗隐藏「共享屏幕」按钮
    bool IsModuleReady() const;

    /**
     *  发起共享：交给 DLL 弹出共享内容选择窗，选好后才真正发起。
     *  @param ctype            会话类型：conversation_single / conversation_group
     *  @param cid              会话 id：单聊为对方 userid，群聊为群 id
     *  @param conversationName 会话名（对方显示名或群名）
     *  @return 已交给 DLL 返回 true；模块未加载或 DLL 拒绝（已在共享或观看中、系统不支持，DLL 已提示原因）返回 false
     */
    bool StartShare(int ctype, int64_t cid, const CString& conversationName);

    /**
     *  加入某会话里进行中的共享（卡片「加入」）。
     *  @param roomId   房间号（卡片 content 的 roomid）
     *  @param hostName 发起人显示名，用作观看窗标题
     *  @param forced   是否强制模式（第 13 期）：DLL 加入前先请用户确认
     *  @return 已交给 DLL 返回 true；模块未加载或 DLL 拒绝（含用户在确认框里取消）返回 false
     */
    bool JoinShare(int ctype, int64_t cid, const std::string& roomId, const CString& hostName, bool forced);

    /**
     *  凭分享链接加入（屏幕共享第 25 期，聊天里点了「flamingo://screenshare/链接码」）：交给 DLL 查共享概要、确认后加入。
     *  @param url 聊天里点到的整条链接
     *  @return 已交给 DLL 返回 true；模块未加载时提示屏幕共享不可用并返回 false
     */
    bool JoinByLink(const std::wstring& url);

    /**
     *  播放一条云端录像（屏幕共享第 27 期，聊天里点了云端录像卡片的「播放」）：交给 DLL 取下载令牌、下载到本机缓存后
     *  用系统默认播放器打开，缓存里已有时直接打开。
     *  @param owner    提示框的属主窗口（聊天窗）；可为 NULL
     *  @param recordId 录像 id，须大于 0
     *  @param title    录像标题（缓存文件名与提示用）；可为空
     *  @return 已交给 DLL 返回 true；模块未加载时提示无法播放并返回 false，recordId 非法时返回 false
     */
    bool OpenCloudRecord(HWND owner, int64_t recordId, const std::wstring& title);

    //查询某会话有无进行中的共享（聊天窗打开时调用）；结果经 OnStatus 回来。模块未加载时不做任何事
    void Query(int ctype, int64_t cid);

    /**
     *  发起独立录屏（聊天窗工具栏、主面板菜单、托盘菜单、全局快捷键），交给 FlamingoMedia.dll。
     *  @param ctype            从聊天窗发起时为会话类型（录完可一键发到该会话）；其它入口传 0
     *  @param cid              会话 id；ctype 为 0 时忽略
     *  @param conversationName 会话名
     *  @return 模块已弹出选择窗返回 true；模块没有加载或拒绝时返回 false
     */
    bool StartRecording(int ctype, int64_t cid, const CString& conversationName);

    //停止进行中的录制并保存；没有在录制时什么也不做
    void StopRecording();

    //打开录制管理窗口（聊天窗录制按钮的下拉菜单，docs/语音字幕方案.md 第 2 节第 10 条）；模块未加载时不做任何事
    void ShowRecordManager();

    //是否正在录制
    bool IsRecording() const;

    //模块已加载且系统支持录屏（决定录屏入口是否出现）
    bool IsRecordSupported() const;

    /**
     *  把录制设置交给模块。
     *  @param quality 画质：0 标准，1 高清
     *  @param folder  保存目录；为空时用模块的默认目录
     */
    void SetRecordSettings(int quality, const CString& folder);

    /**
     *  把屏幕共享清晰度交给模块（模块没加载时什么都不做，加载后登录时会再推送一次）。
     *  @param quality 0 为 1080p、1 为 2K（默认）、2 为 4K
     */
    void SetShareQuality(int quality);

    /**
     *  把鼠标效果的设置交给模块（屏幕共享第 16 期，登录后与保存新设置后）。
     *  @param text 设置文本（ASCII，格式见 FlamingoMedia 的 SerializeMouseEffectSettings）；为空时模块按默认设置
     */
    void SetMouseEffectSettings(const CString& text);

    /**
     *  DLL 回报某会话的共享状态（经 CMediaHostAdapter::OnScreenShareStatus 转来），记进状态缓存。
     *  参数含义同 IMediaHost::OnScreenShareStatus；roomId / hostName 可为 NULL，按空串处理。
     */
    void OnStatus(int ctype, int64_t cid, const char* roomId, int64_t hostId, const wchar_t* hostName, bool active,
                  bool locked, bool forced, const wchar_t* title);

    //某张卡片所指那一场共享的状态，取值见 enum ScreenShareRoomState
    int RoomState(int ctype, int64_t cid, const std::string& roomId) const;

    /**
     *  取某个会话进行中的共享（群聊横幅用）。
     *  @return 已知有进行中的共享返回 true
     */
    bool GetActive(int ctype, int64_t cid, ScreenShareActiveInfo& out) const;

    /**
     *  收到「共享标题与描述变了」推送（1734）后更新状态缓存里的这一场共享：换标题，移交主持人后另换主持人。
     *  参数与返回值同 ScreenShareStatusCache::ApplyInfo；返回 true 时调用方刷新群聊横幅。
     */
    bool ApplyInfo(int ctype, int64_t cid, const std::string& roomId, const std::wstring& title, int64_t hostId,
                   const std::wstring& hostName);

    //登出时清空状态缓存；模块登记保持不变
    void ClearStatus();

private:
    CScreenShareHub();

    CScreenShareHub(const CScreenShareHub&);
    CScreenShareHub& operator=(const CScreenShareHub&);

    flamingomedia::IMediaModule* m_module;   //模块接口（不拥有）；未加载时为 NULL
    ScreenShareStatusCache       m_cache;    //各会话的共享状态，本次登录期间有效，登出时清空
};

} // namespace balloonscreenshare
