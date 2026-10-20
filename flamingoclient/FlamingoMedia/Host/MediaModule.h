/**
 *  IMediaModule 的实现：FlamingoMedia.dll 在进程内唯一的模块对象。
 *
 *  导出函数 FlamingoMedia_Create 返回本类单例的地址，exe 经它初始化、转交信令、关闭模块。
 *  第 1a 步只实现模块本身的生命周期（初始化、关闭）、系统版本判定与信令入口；信令入口对
 *  所有命令都返回「未处理」，后续各步迁入通话、远程桌面，新增屏幕共享时再按命令号分发。
 *
 *  线程模型：除 IsFeatureSupported 外，所有方法只在 exe 的 UI 线程上调用，内部不加锁。
 *
 *  balloonwj@qq.com   2026-10-02
 */
#pragma once

#include <memory>
#include "../Api/FlamingoMediaApi.h"

namespace call { class CCallController; }   //一对一通话控制器（本类持有，见 Call/CallController.h）
namespace remotedesktop { class CRdController; class CRdDebugEntries; }   //远程桌面控制器与调试入口（本类持有）
namespace screenshare { class CShareController; }   //屏幕共享控制器（本类持有，见 Share/ShareController.h）
namespace screenrecord { class CRecordController; }   //录屏控制器（本类持有，见 Record/RecordController.h）
namespace screenrecord { class CCloudRecordCenter; }  //云端录像中心（本类持有，见 Record/CloudRecordCenter.h）

namespace flamingomedia { class CVoiceBridge; }   //语音消息的桥接对象（本类持有，定义在 MediaModule.cpp）
namespace groupcall { class CGroupCallController; }   //群通话控制器（本类持有，见 GroupCall/GroupCallController.h）

namespace flamingomedia {

class CMediaModule : public IMediaModule
{
public:
    /**
     *  取进程内唯一的模块对象。对象是函数内静态变量，随 DLL 卸载而析构。
     *  @return 模块对象的引用
     */
    static CMediaModule& Instance();

    //取模块实现的接口版本号，即编译本 DLL 时的 kApiVersion。
    virtual uint32_t GetApiVersion() const override;

    //初始化模块：登记宿主与日志、记录系统版本。重复调用直接返回 true。详见 IMediaModule。
    virtual bool Init(IMediaHost* host, const MediaInitParams* params) override;

    //关闭模块并解除宿主登记。未初始化或重复调用均安全。详见 IMediaModule。
    virtual void Shutdown() override;

    //信令入口。第 1a 步对所有命令返回 false（未处理）。详见 IMediaModule。
    virtual bool OnSignal(uint16_t cmd, const char* json, uint32_t len) override;

    //主连接在线状态变化。第 1a 步只记日志。详见 IMediaModule。
    virtual void OnConnectionStateChanged(bool online) override;

    //查询功能是否可用：屏幕共享与录制按系统版本判定，其余返回 false。详见 IMediaModule。
    virtual bool IsFeatureSupported(int feature) override;

    //用户登出：结束进行中的通话与远程桌面会话。详见 IMediaModule。
    virtual void OnLogout() override;

    //发起一对一通话，交给通话控制器。详见 IMediaModule。
    virtual bool StartCall(int64_t peerUserId, const wchar_t* peerName, int callType) override;

    //发起远程桌面，交给远程桌面控制器。详见 IMediaModule。
    virtual bool StartRemoteDesktop(int64_t peerUserId, const wchar_t* peerName) override;

    //发起屏幕共享（先弹选择窗），交给屏幕共享控制器。详见 IMediaModule。
    virtual bool StartScreenShare(int ctype, int64_t cid, const wchar_t* conversationName) override;

    //加入会话里进行中的屏幕共享，交给屏幕共享控制器。详见 IMediaModule。
    virtual bool JoinScreenShare(int ctype, int64_t cid, const char* roomId, const wchar_t* hostName,
                                 bool forced) override;

    //查询会话里有无进行中的屏幕共享，结果经 IMediaHost::OnScreenShareStatus 回报。详见 IMediaModule。
    virtual void QueryScreenShare(int ctype, int64_t cid) override;

    //见 IMediaModule::StartRecording
    virtual bool StartRecording(int ctype, int64_t cid, const wchar_t* conversationName) override;

    //见 IMediaModule::StopRecording
    virtual void StopRecording() override;

    //见 IMediaModule::IsRecording
    virtual bool IsRecording() override;

    //见 IMediaModule::SetRecordSettings
    virtual void SetRecordSettings(int quality, const wchar_t* folder) override;

    //见 IMediaModule::SetMouseEffectSettings：解析后交给共享控制器（16d 起录屏控制器也用同一份）
    virtual void SetMouseEffectSettings(const char* settingsText) override;

    //运行本模块的调试入口（第 1c 步起为通话的 7 个入口，第 1d 步起加上远程桌面的 8 个）。详见 IMediaModule。
    virtual int RunDebugEntry(const wchar_t* commandLine, HWND hwndHost) override;

    //见 IMediaModule::StartVoiceRecord：麦克风被通话或屏幕共享占用时不录
    virtual int StartVoiceRecord(const wchar_t* path) override;

    //见 IMediaModule::StopVoiceRecord
    virtual bool StopVoiceRecord(MediaVoiceRecordInfo* info) override;

    //见 IMediaModule::CancelVoiceRecord
    virtual void CancelVoiceRecord() override;

    //见 IMediaModule::PlayVoice
    virtual bool PlayVoice(const wchar_t* path, uint32_t token) override;

    //见 IMediaModule::StopVoicePlay
    virtual void StopVoicePlay() override;

    //见 IMediaModule::PauseVoiceRecord：关闭麦克风采集，已录的内容保留
    virtual bool PauseVoiceRecord() override;

    //见 IMediaModule::ResumeVoiceRecord：麦克风被通话或屏幕共享占用时不继续
    virtual int ResumeVoiceRecord() override;

    //见 IMediaModule::StartGroupCall：交给群通话控制器
    virtual bool StartGroupCall(int64_t groupId, const wchar_t* groupName, int media, const int64_t* invitees,
                                uint32_t count) override;

    //见 IMediaModule::JoinGroupCall：交给群通话控制器
    virtual bool JoinGroupCall(int64_t groupId, const wchar_t* groupName, const char* roomId) override;

    //见 IMediaModule::QueryGroupCall：交给群通话控制器，结果经 IMediaHost::OnGroupCallStatus 回报
    virtual void QueryGroupCall(int64_t groupId) override;

    //见 IMediaModule::InviteToGroupCall：交给群通话控制器
    virtual bool InviteToGroupCall(int64_t groupId, const int64_t* userIds, uint32_t count) override;

    //见 IMediaModule::OnAvatarsUpdated：转给一对一通话与群通话控制器，各自重新取已打开窗口里的头像
    virtual void OnAvatarsUpdated() override;

    //见 IMediaModule::SnapshotVoiceRecord：把已暂停的录音里已录的部分另存成文件，供暂停中试听
    virtual bool SnapshotVoiceRecord(const wchar_t* path, int64_t* outDurationMs) override;

    //见 IMediaModule::ShowRecordManager：打开录制管理窗口（docs/屏幕共享与录制方案.md 第 11.6 节 F9）
    virtual void ShowRecordManager() override;

    //见 IMediaModule::SetScreenLocked：登记锁屏状态并转给通话、远程桌面、屏幕共享控制器（docs/锁屏方案.md 第 4.9 节）
    virtual void SetScreenLocked(bool locked) override;

    //见 IMediaModule::OnCaptionAvailabilityChanged：交给字幕衔接对象，由它通知录屏、屏幕共享控制器（docs/语音字幕方案.md D5）
    virtual void OnCaptionAvailabilityChanged(bool available) override;
    //见 IMediaModule::JoinScreenShareByLink：交给屏幕共享控制器，先查共享概要再确认加入（第 25 期）
    virtual bool JoinScreenShareByLink(const wchar_t* url) override;
    //以下五个见 IMediaModule 第 16 版的说明（云端录制第 27 期），都交给云端录像中心
    virtual void OnCloudTransferProgress(uint32_t id, int64_t doneBytes, int64_t totalBytes) override;
    virtual void OnCloudTransferFinished(uint32_t id, int result) override;
    virtual bool HasCloudRecordWork() override;
    virtual void OpenCloudRecord(int64_t recordId, const wchar_t* title) override;
    virtual void OnConversationsPicked(uint32_t token, const MediaConversationRef* refs, uint32_t count) override;
    //见 IMediaModule::ConfirmCloudRecordExit（第 17 版，方案第 14.4 节）：弹「云端录像」对话框，选择放弃时请云端录像中心删除
    virtual int ConfirmCloudRecordExit(int kind) override;
    //见 IMediaModule::SetShareQuality（第 18 版）：交给共享控制器，由它交给主持人与共享者两条推流管线
    virtual void SetShareQuality(int quality) override;

private:
    CMediaModule();
    ~CMediaModule();

    CMediaModule(const CMediaModule&) = delete;
    CMediaModule& operator=(const CMediaModule&) = delete;

    IMediaHost* m_host;          //宿主接口（不拥有）；Init 时登记，Shutdown 时置空
    bool        m_initialized;   //是否已初始化且尚未关闭；Init 置 true，Shutdown 置 false

    //一对一通话控制器（本类拥有）：Init 时创建，Shutdown 时收尾并释放；未初始化时为空
    std::unique_ptr<call::CCallController> m_call;

    //远程桌面控制器（本类拥有）：Init 时创建，Shutdown 时收尾并释放；未初始化时为空
    std::unique_ptr<remotedesktop::CRdController> m_rd;

    //远程桌面调试入口（本类拥有，持有调试入口留下的预览窗口）：Init 时创建，Shutdown 时销毁窗口并释放
    std::unique_ptr<remotedesktop::CRdDebugEntries> m_rdDebug;

    //屏幕共享控制器（本类拥有）：Init 时创建，Shutdown 时收尾并释放；未初始化时为空
    std::unique_ptr<screenshare::CShareController> m_share;

    //录屏控制器（本类拥有）：先于屏幕共享控制器创建（共享录制要用它），晚于它关闭；未初始化时为空
    std::unique_ptr<screenrecord::CRecordController> m_record;

    //语音消息（本类拥有）：在通话、共享、录屏控制器之后创建，最先关闭；建不出消息窗口时语音消息不可用
    std::unique_ptr<CVoiceBridge> m_voice;

    //群通话控制器（本类拥有）：Init 时创建，Shutdown 时收尾并释放；建不出消息窗口时为空，群通话不可用
    std::unique_ptr<groupcall::CGroupCallController> m_groupCall;

    //云端录像中心（本类拥有，第 27 期）：Init 时创建，Shutdown 时取消合成与传输后释放；未初始化时为空
    std::unique_ptr<screenrecord::CCloudRecordCenter> m_cloud;

    /**
     *  正在群通话里时，一对一通话、屏幕共享在本地拦住并提示（服务端同样会拒绝，这里只是省一次往返、提示更明确）。
     *  @return 正在群通话里（已提示用户）返回 true
     */
    bool RejectIfInGroupCall();
};

} // namespace flamingomedia
