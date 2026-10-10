/**
 *  屏幕录制控制器，RecordController.h
 *
 *  FlamingoMedia.dll 里录屏的总调度（docs/屏幕共享与录制方案.md 第 6 节、第 7 期）：
 *    - 独立录屏：exe 调 StartRecording（聊天窗工具栏、主面板菜单、托盘菜单、全局快捷键）→ 选择窗（录制模式）选整屏、
 *      窗口或「框选区域」→ 框选遮罩 → 起采集、麦克风、电脑声音、摄像头画中画与 RecordSession → 录制条；
 *    - 共享录制：屏幕共享控制器调 StartShareRecording，画面与声音由共享一侧提供（第 7 期 7d）；
 *    - 结束：录制条「停止」、磁盘空间不够、被录的窗口关闭时停止并保存，弹出完成提示（播放、打开文件夹、发送到会话）。
 *    - 语音字幕（docs/语音字幕方案.md 6.1、14.12）：字幕可用时选择窗提供「生成字幕」；勾了就在开始录制时建一个字幕草稿会话，
 *      麦克风与电脑声音两路都识别，经混音器的分路旁路（CRecordCaptionTap）边录边识别，录制时屏幕上显示实时字幕
 *      （CRecordCaptionLive），录制条可随时关闭；结束时让草稿会话收尾，完成提示里多一个「编辑字幕」。
 *      字幕变为不可用（管理员关闭、退出登录）时识别停止、已识别的部分照常保存，录像本身继续录。
 *  同一时间只有一个录制。所有方法都在 exe 的 UI 线程上调用；写入线程的结束回报经本类的消息窗口转回 UI 线程。
 *
 *  典型用法（CMediaModule 里）：
 *      m_record.reset(new screenrecord::CRecordController());
 *      m_record->Init(host);
 *      m_record->StartRecording(ctype, cid, name);
 *
 *  balloonwj@qq.com   2026-10-03
 */
#pragma once

#include <stdint.h>
#include <string>

#include "RecordBar.h"
#include "RecordCameraWindow.h"   // 录制中的摄像头小窗（2026-10-05）
#include "RecordDoneToast.h"
#include "RecordManagerWindow.h"   // 录制管理窗口（2026-10-05，F9）
#include "RecordMessages.h"
#include "RecordRegionSelector.h"
#include "RecordSession.h"
#include "RecordSources.h"
#include "../Api/FlamingoMediaApi.h"
#include "../Caption/MediaCaptionBridge.h"   // 语音字幕：可用状态、取字幕模块
#include "../Caption/RecordCaptionTap.h"     // 语音字幕：混音器到字幕草稿会话的旁路
#include "../Caption/RecordCaptionLive.h"    // 语音字幕：录屏时屏幕上的实时字幕（14.12）
#include "../Common/MediaToast.h"            // 轻量提示：录制与共享互斥被拦下时（2026-10-07）
#include "../Share/ShareAnnotationOverlay.h"
#include "../Share/ShareBorderWindow.h"
#include "../Share/ShareLocalAnnotator.h"
#include "../Share/ShareMessages.h"
#include "../Share/SharePickerDialog.h"
#include "../Share/ShareWhiteboardWindow.h"

namespace screenrecord {

class CCloudRecordCenter;

//共享录制时，共享控制器提供给录屏控制器的回调
class IShareRecordOwner
{
public:
    virtual ~IShareRecordOwner()
    {
    }

    //共享录制已结束（停止、磁盘不够或出错）；共享控制器据此发 1731 告诉房间里的人
    virtual void OnShareRecordingEnded() = 0;
};

//录屏控制器；消息窗口只收消息
class CRecordController : public CWindowImpl<CRecordController, CWindow, CWinTraits<0, 0> >,
                          private RecordSessionListener,
                          private screenshare::IShareLocalAnnotatorListener,
                          private IRecordCameraWindowSink,
                          private flamingomedia::ICaptionAvailabilityObserver
{
public:
    CRecordController();
    ~CRecordController();

    /**
     *  初始化：建消息窗口。
     *  @param host 宿主接口（发送文件、主窗口），不拥有，须在 Shutdown 之前一直有效
     */
    bool Init(flamingomedia::IMediaHost* host);

    //停止进行中的录制（保存已录部分）、关闭全部窗口、销毁消息窗口；可重复调用
    void Shutdown();

    /**
     *  设置录制参数（exe 启动时与设置变化时调用）。
     *  @param quality 画质，取值见 RecordQuality
     *  @param folder  保存目录；为空时用「视频」文件夹下的 FlamingoRecords
     */
    void SetSettings(int quality, const std::wstring& folder);

    /**
     *  登记云端录像中心（屏幕共享第 27 期）：选择窗里勾了「保存到云端」时，独立录制录好后经它上传；录制管理窗口的「云端」页同样经它取数据与操作。
     *  @param center 云端录像中心，不拥有；模块关闭云端录像中心之前传 NULL 解除
     */
    void SetCloudCenter(CCloudRecordCenter* center);

    /**
     *  发起独立录屏：弹出选择窗（录制模式）。
     *  @param ctype            从聊天窗发起时为会话类型（录完可一键发到该会话），其它入口传 0
     *  @param cid              会话 id；ctype 为 0 时忽略
     *  @param conversationName 会话名（完成提示的按钮显示「发送给 某某」）
     *  @return 已弹出选择窗返回 true；系统不支持或已在录制时返回 false（已向用户提示）
     */
    bool StartRecording(int ctype, int64_t cid, const std::wstring& conversationName);

    /**
     *  发起共享录制（屏幕共享控制器调用）：画面来自 video，声音由调用方推进 Audio()，标注经 SetShareAnnotations 更新。
     *  @param video       画面来源，不拥有，StopRecording 返回之前须一直有效
     *  @param pip         画中画来源（当前发言人的摄像头）；可为 NULL
     *  @param srcW、srcH  画面来源的尺寸（决定输出尺寸）
     *  @param anchor      录制条摆在哪块显示器上（共享内容或观看窗的矩形）
     *  @param ctype、cid、conversationName 共享所在的会话（录完可一键发到该会话）
     *  @param owner       结束回调，不拥有
     *  @return 已开始返回 true；已在录制、磁盘空间不够时返回 false（已向用户提示）
     */
    bool StartShareRecording(IRecordVideoSource* video, IRecordVideoSource* pip, int srcW, int srcH, const RECT& anchor,
                             int ctype, int64_t cid, const std::wstring& conversationName, IShareRecordOwner* owner);

    //停止并保存进行中的录制（随后弹出完成提示）；没有在录制时什么也不做
    void StopRecording();

    //打开录制管理窗口（系统菜单「录制管理」，2026-10-05）；已经开着时恢复、刷新并激活。尚未初始化时什么也不做
    void ShowManager();

    //是否正在录制（含暂停）
    bool IsRecording() const;

    //是否是共享录制
    bool IsShareRecording() const;

    //已录的时长（不含暂停，毫秒，2026-10-07）：共享录制不再弹出录制条，共享控制器每秒取来显示在主工具条上；没在录制时为 0
    int64_t RecordedMs() const;

    /**
     *  独立录制是否占用中（2026-10-07，录制与共享互斥，docs/屏幕共享与录制方案.md 第 13 节）：正在独立录制（含暂停，不含
     *  共享录制）、录制选择窗开着、或正在框选区域。占用中不能发起屏幕共享。
     */
    bool IsStandaloneBusy() const;

    /**
     *  发起录制被拦下时（2026-10-07，如正在共享屏幕）在发起入口所在的窗口附近弹出提示；不改变录制状态。
     *  @param text 提示文字（已经过 TR 翻译）
     */
    void ShowStartRejected(const std::wstring& text);

    /**
     *  共享录制的字幕草稿会话（语音字幕，docs/语音字幕方案.md 第 9 节）：屏幕共享的实时字幕每句最终结果经它写进草稿。
     *  草稿会话在第一次调用时才建（没人开字幕的录像不留空草稿）；两路识别都不开，字幕全部来自房间。只在 UI 线程调用。
     *  @param recordedMs [出参] 当前录制时刻（毫秒，不含暂停）
     *  @return 没有在录制共享、字幕不可用或草稿会话建不起来时返回 NULL；返回的指针在本类下一次进入消息循环之前有效
     */
    caption::ICaptionRecording* ShareCaptionRecording(int64_t& recordedMs);

    //共享录制的混音器：共享控制器把麦克风、其他人的声音、电脑声音推进来；没有在录制时返回 NULL
    RecordAudioMixer* ShareAudio();

    //共享录制要叠的标注（笔迹变化时由共享控制器调用）
    void SetShareAnnotations(const std::vector<screenshare::AnnotationStroke>& strokes);

    //共享录制的画中画开关（当前发言人有没有开摄像头）
    void SetSharePipEnabled(bool on);

    /**
     *  鼠标效果的设置（第 16 期）：交给独立录屏的画面装饰（立即生效）与录屏条（菜单打勾）。exe 登录后与保存新设置后经模块调用。
     *  @param settings 新设置
     */
    void SetMouseEffects(const screenshare::ShareMouseEffectSettings& settings);

    BEGIN_MSG_MAP(CRecordController)
        MESSAGE_HANDLER(screenshare::kMsgSharePickerResult, OnPickerResultMsg)
        MESSAGE_HANDLER(kMsgRecordRegionResult, OnRegionResultMsg)
        MESSAGE_HANDLER(kMsgRecordBarCommand, OnBarCommandMsg)
        MESSAGE_HANDLER(kMsgRecordStopped, OnStoppedMsg)
        MESSAGE_HANDLER(kMsgRecordDoneCommand, OnDoneCommandMsg)
        MESSAGE_HANDLER(WM_TIMER, OnTimer)
    END_MSG_MAP()

private:
    //写入线程交给 UI 线程的结束信息
    struct StoppedInfo
    {
        std::wstring m_path;         //文件路径
        int64_t      m_durationMs;   //时长
    };

    //RecordSessionListener：写入线程上调用，转投 UI 线程
    virtual void OnRecordStopped(int reason, const std::wstring& path, int64_t durationMs) override;
    //IShareLocalAnnotatorListener（第 21 期）：画笔的笔迹有变化，重画显示层并交给录制合成
    virtual void OnLocalAnnotationsChanged() override;
    //IShareLocalAnnotatorListener（第 21 期）：画笔工具条上点了「退出标注」
    virtual void OnLocalAnnotatorExit() override;
    //IShareLocalAnnotatorListener（2026-10-07）：画笔工具条上的「鼠标」：撤掉或盖回画笔的输入层
    virtual void OnLocalAnnotatorPointerMode(bool on) override;
    //进入或退出画笔模式（第 21 期）：进入时盖上输入层接住鼠标、显示标注工具条；退出时放开鼠标、隐藏工具条，笔迹保留并照常录进视频
    void setPenMode(bool on);
    //被录内容当前的矩形（屏幕坐标）：窗口录制随窗口移动，其余固定
    RECT recordedRect() const;
    //录白板（第 21 期）：打开白板窗口，再按窗口录制它
    void startWhiteboard();

    LRESULT OnPickerResultMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnRegionResultMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnBarCommandMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnStoppedMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnDoneCommandMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnTimer(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

    //开始独立录屏：region 为空矩形时录整个 spec
    void startStandalone(const screenshare::ShareSourceSpec& spec, const RECT& region);
    /**
     *  开始只录声音（第 24 期，选择窗里点了「只录声音」）：按选择窗的勾选启动麦克风与电脑声音（两路都打不开时提示并放弃），
     *  文件为 .m4a；录制条摆在主面板所在显示器的顶部正中，带两条电平条；勾了「生成字幕」时照常边录边识别。
     */
    void startAudioOnly();
    //主面板所在的那块显示器，作为只录声音时录制条与实时字幕的摆放范围
    screenshare::ShareSourceSpec mainMonitorSpec() const;
    //只录声音时刷新录制条的两条电平条（电平定时器调用）
    void updateLevels();
    //录制结束后的收尾：停掉各个来源、关掉录制条与边框
    void cleanupSources();
    /**
     *  独立录屏中打开或关闭一路声音（2026-10-05）：启停这一路的采集与混音器里的那一路，再把状态同步给录制条。
     *  打开失败时提示并保持关闭。共享中的录制不处理（声音由共享房间控制）。
     *  @param source 取值 record_audio_mic 或 record_audio_system
     *  @param on     打开还是关闭
     */
    void setStandaloneAudio(int source, bool on);
    /**
     *  独立录屏中打开或关闭摄像头（2026-10-05）：启停摄像头、显示或关闭摄像头小窗、打开或关闭画中画，再同步给录制条。
     *  打开失败时提示并保持关闭。共享中的录制不处理。
     *  @param on 打开还是关闭
     */
    void setStandaloneCamera(bool on);
    //在被录内容右下角显示摄像头小窗，并按它的位置设画中画
    void showCameraWindow();
    //按摄像头小窗相对被录内容的位置重算画中画摆放（小窗被拖动、被录的窗口移动时）
    void updatePipPlacement();
    //IRecordCameraWindowSink：摄像头小窗被拖动了
    virtual void OnCameraWindowMoved(const RECT& screenRect) override;
    //ICaptionAvailabilityObserver：字幕变为不可用时停止识别（已识别的部分照常保存），录像继续录
    virtual void OnCaptionAvailabilityChanged(bool available) override;
    /**
     *  开始录制之前按选择窗的勾选建字幕草稿会话，并登记混音器的分路旁路。没勾、字幕不可用或建不起来时什么也不做（只记日志）。
     *  @param path 这次录像的文件路径（草稿写在它旁边）
     */
    void beginCaption(const std::wstring& path);
    //共享录制建字幕草稿会话（两路识别都不开，不登记混音器旁路）；字幕不可用或建不起来时什么也不做（只记日志）
    void beginShareCaption(const std::wstring& path);
    /**
     *  结束字幕草稿会话：取消旁路，按 keep 让它收尾保存或丢弃。没有草稿会话时什么也不做。
     *  @param keep       true 收尾保存（录像已生成），false 丢弃（录制没开始或没生成文件）
     *  @param durationMs 录像时长（毫秒），keep 为 true 时有效
     */
    void endCaption(bool keep, int64_t durationMs);
    //把画中画在录像里的位置告诉字幕草稿，字幕据此避开它（有草稿会话时）
    void syncCaptionPip();
    //录制条「字幕：开 / 关」（14.12）：关闭时旁路改送静音、屏幕上的实时字幕隐藏，打开时恢复；同步录制条
    void setCaptionOn(bool on);
    //把两路声音当前是否在录告诉实时字幕（说话人标注与「没有声音可识别」的提示）
    void syncCaptionLiveSources();
    //录制区域当前是否可见：录的是窗口时窗口已关闭或最小化为 false
    bool recordedAreaVisible() const;
    //本人的显示名（字幕草稿里麦克风字幕的说话人）；取不到时为空
    std::wstring selfDisplayName() const;
    /**
     *  生成这次录制的文件路径（目录不存在时创建）；失败返回空串。
     *  @param audioOnly 只录声音（第 24 期）时扩展名为 .m4a
     */
    std::wstring makeOutputPath(bool audioOnly = false);
    /**
     *  录制完成且生成了文件时，经宿主写一条录像记录，供录制管理窗口列出（docs/屏幕共享与录制方案.md 第 11.6 节 F9）。
     *  录制结束时间取当前时间。写入失败（如尚未登录、本地库没有打开）只记日志，不影响完成提示。
     *  @param path       录像文件的完整路径
     *  @param durationMs 时长（毫秒）
     *  @param bytes      文件字节数
     */
    void addRecordingRecord(const std::wstring& path, int64_t durationMs, uint64_t bytes);
    void showError(const wchar_t* text);
    HWND mainWindow() const;

    flamingomedia::IMediaHost* m_host;           //宿主接口（不拥有）
    int                        m_quality;        //画质
    std::wstring               m_folder;         //保存目录（为空时用默认目录）
    //这次录制的会话（录完一键发送用）
    int                        m_ctype;          //会话类型；0 表示没有会话（完成提示里选会话）
    int64_t                    m_cid;            //会话 id
    std::wstring               m_convName;       //会话名
    //独立录屏的选择
    bool                       m_wantMic;        //录麦克风
    bool                       m_wantSystem;     //录电脑声音
    bool                       m_micOn;          //独立录屏时麦克风当前是否在录（2026-10-05 起录制中可开关）
    bool                       m_systemOn;       //独立录屏时电脑声音当前是否在录（同上）
    bool                       m_cameraOn;       //独立录屏时摄像头当前是否开着（2026-10-05 起录制中可开关）
    CRecordCameraWindow        m_cameraWindow;   //摄像头开着时屏幕上可拖动的小窗，录像里的画中画跟着它走
    bool                       m_wantCamera;     //摄像头画中画
    bool                       m_wantCaption;    //选择窗里勾了「生成字幕」（语音字幕）：麦克风与电脑声音两路都识别（2026-10-06 起）
    bool                       m_wantCloud;      //选择窗里勾了「保存到云端」（第 27 期）：独立录制录好后上传；每次选择后重填
    CCloudRecordCenter*        m_cloud;          //云端录像中心（不拥有，SetCloudCenter 登记）；没有时为 NULL
    bool                       m_captionActive;  //这次录制建起了字幕草稿会话（字幕中途变为不可用时仍为 true，表示有草稿）
    bool                       m_lastHasCaption; //最近一次录制有字幕草稿（完成提示的「编辑字幕」用）
    flamingomedia::CRecordCaptionTap m_captionTap;   //混音器到字幕草稿会话的旁路（语音字幕）；共享录制时只用来持有草稿会话
    std::wstring               m_sharePath;      //共享录制的录像路径（语音字幕：收到第一句字幕时据此建草稿会话）；录制结束时清空
    bool                       m_shareCaptionTried;  //这次共享录制是否已尝试建过字幕草稿会话（失败后不再重试）
    flamingomedia::CRecordCaptionLive m_captionLive; //独立录屏时屏幕上的实时字幕（14.12）：Init 时建消息窗口，Shutdown 时销毁
    bool                       m_captionOn;      //录制条「字幕」开关当前是否打开；每次开始录制时为 true
    bool                       m_picking;        //选择窗或框选遮罩开着
    screenshare::ShareSourceSpec m_pendingMonitor;   //框选区域时所在的显示器（框选遮罩返回后用）
    //录制
    RecordSession              m_session;        //这次录制
    RecordCaptureSource        m_capture;        //独立录屏的画面采集
    RecordMicSource            m_mic;            //独立录屏的麦克风
    RecordSystemAudioSource    m_system;         //独立录屏的电脑声音
    RecordCameraSource         m_camera;         //独立录屏的摄像头画中画
    RecordMouseDecorator       m_mouseDecorator; //独立录屏的鼠标效果（第 16 期）：录制线程写每一帧前画上；共享录制不用
    bool                       m_recording;      //是否在录制
    bool                       m_audioOnly;      //这次录制是否只录声音（第 24 期）；录制结束时清除
    bool                       m_shareMode;      //是否是共享录制
    IShareRecordOwner*         m_shareOwner;     //共享录制的结束回调（不拥有）
    std::wstring               m_lastPath;       //最近一次录制的文件路径（完成提示的按钮用）
    //界面
    screenshare::CSharePickerDialog  m_picker;   //选择窗（录制模式）
    CRecordRegionSelector            m_region;   //框选遮罩
    CRecordBar                       m_bar;      //录制条
    screenshare::CShareBorderWindow  m_border;   //自选区域的边框
    CRecordDoneToast                 m_done;     //完成提示
    flamingomedia::CMediaToast       m_noticeToast;   //轻量提示（2026-10-07）：发起录制被拦下时；Shutdown 时销毁
    CRecordManagerWindow             m_manager;  //录制管理窗口（2026-10-05，docs/屏幕共享与录制方案.md 第 11.6 节 F9）
    //画笔（第 21 期）
    screenshare::ShareSourceSpec        m_recordSpec;    //这次独立录屏录的内容（窗口录制时据此跟随窗口）
    RECT                                m_recordRegion;  //这次独立录屏的自选区域；空矩形表示没有
    screenshare::CShareLocalAnnotator   m_pen;           //画笔的本机标注（会话、工具条、命令处理）
    screenshare::CShareAnnotationOverlay m_penOverlay;   //画笔的显示层与输入层（构造时引用 m_pen 的会话，须声明在它之后）
    bool                                m_penMode;       //画笔模式是否开着（鼠标由输入层接住）
    RECT                                m_penRect;       //显示层当前覆盖的矩形（屏幕坐标）
    //白板（第 21 期）
    screenshare::CShareWhiteboardWindow m_board;         //录制用的白板窗口（自带本机标注与工具条）
    bool                                m_recordingBoard;   //这次录的是不是白板（录制结束时关掉白板）
};

} // namespace screenrecord
