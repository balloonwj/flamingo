/**
 *  FlamingoCaption.dll 与调用方 FlamingoMedia.dll 之间的接口约定（docs/语音字幕方案.md 第 4 节）。
 *
 *  FlamingoCaption.dll 承载「语音字幕」：语音识别（sherpa-onnx + onnxruntime，静态链接）、字幕草稿、字幕编辑器、
 *  导出（硬字幕版、字幕轨版、SRT）以及字幕的绘制。模型在另一个纯资源 DLL FlamingoCaptionModel.dll 里，
 *  由本 DLL 以数据文件方式加载。字幕的使用方（录制、录制管理窗口、屏幕共享）都在 FlamingoMedia.dll 里，
 *  所以由 FlamingoMedia 在第一次用到字幕时加载本 DLL，经唯一的导出函数 FlamingoCaption_Create 取得
 *  ICaptionModule；FlamingoMedia 实现 ICaptionHost 交给本 DLL，日志、界面文字的翻译等再由它转给 exe。
 *  本 DLL 不存在、版本不符或初始化失败时，FlamingoMedia 隐藏全部字幕入口（「加载失败则不开启字幕」）。
 *
 *  接口形态与其他插件 DLL（FlamingoMedia/Api/FlamingoMediaApi.h、FlamingoFileShare/FileShareApi.h）一致：
 *  各自静态链接 CRT、各有独立的堆，因此只使用纯虚类与基本类型：
 *    - 字符串由调用方持有，只在本次调用期间有效，被调方需要保留时自行拷贝；
 *    - 不传递 STL 对象，不在一侧分配、另一侧释放内存；
 *    - 异常不越过模块边界；
 *    - ICaptionModule 的实现对象由 DLL 持有（进程内唯一），ICaptionStream / ICaptionRecording 由 DLL 创建、
 *      调用方用完后调用其 Close / Finish / Cancel 交还，交还后不得再使用。
 *
 *  修改规则：任何一侧增删接口方法、改动参数或结构体布局，都必须把 kApiVersion 加 1。
 *  加载时双方版本号必须完全相同，不做兼容旧版本的处理。
 *
 *  典型用法（FlamingoMedia 侧）：
 *      HMODULE dll = LoadLibraryW(path);
 *      caption::FlamingoCaptionCreateFn create = (caption::FlamingoCaptionCreateFn)GetProcAddress(dll, FLAMINGO_CAPTION_FACTORY_NAME);
 *      caption::ICaptionModule* m = create(caption::kApiVersion);
 *      m->Init(&host, &params);
 *      ...
 *      m->Shutdown();
 *      FreeLibrary(dll);
 *
 *  balloonwj@qq.com   2026-10-06
 */
#pragma once

#include <windows.h>
#include <stdint.h>

namespace caption {

//接口版本号：FlamingoMedia 与本 DLL 编译时都取这一个值，加载时必须相同。
//2（2026-10-06）：ICaptionRecording 增加 SetObserver，录制时在屏幕上显示实时字幕
const uint32_t kApiVersion = 2;

//DLL 文件名，与 exe 放在同一目录
#define FLAMINGO_CAPTION_DLL_NAME        L"FlamingoCaption.dll"
#define FLAMINGO_CAPTION_MODEL_DLL_NAME  L"FlamingoCaptionModel.dll"

//导出的工厂函数名（GetProcAddress 使用 ANSI 名称）
#define FLAMINGO_CAPTION_FACTORY_NAME    "FlamingoCaption_Create"

//本 DLL 经 ICaptionHost::Log 写日志时的级别
enum CaptionLogLevel
{
    caption_log_info    = 0,   //一般信息：加载模型、开始与结束识别、导出完成
    caption_log_warning = 1,   //可恢复的问题：模型 DLL 缺失、某次存盘失败
    caption_log_error   = 2,   //错误：识别引擎创建失败、导出失败
};

//识别引擎（模型）的状态
enum CaptionEngineState
{
    caption_engine_idle    = 0,   //还没加载（或空闲后已卸载）；调用 Preload 或创建识别流时开始加载
    caption_engine_loading = 1,   //正在后台加载模型
    caption_engine_ready   = 2,   //已加载，可以识别
    caption_engine_failed  = 3,   //加载失败（模型 DLL 缺失或损坏、本 DLL 编译时没有识别引擎），字幕不可用
};

//识别流的用途：决定识别结果的来源标记与日志
enum CaptionStreamPurpose
{
    caption_purpose_live      = 1,   //屏幕共享时为本人的发言生成实时字幕
    caption_purpose_postrec   = 2,   //事后识别（编辑器里对整段录像重新识别）
};

//录像对应的字幕文件状态（可组合）
enum CaptionFileStatus
{
    caption_file_none    = 0x00,   //没有任何字幕文件
    caption_file_draft   = 0x01,   //有草稿「原名.mp4.caption.json」
    caption_file_hard    = 0x02,   //已导出硬字幕版「原名-字幕.mp4」
    caption_file_track   = 0x04,   //已导出字幕轨版「原名-字幕轨.mp4」
    caption_file_srt     = 0x08,   //已导出「原名.srt」
};

//按账号记住的开关类偏好（ICaptionModule::GetPreference / SetPreference），存在字幕设置里，下次沿用上次的选择
enum CaptionPreference
{
    caption_pref_record_caption = 1,   //录制选项「生成字幕」（识别本人麦克风），第一次默认勾选（第 2 节 D3）
    caption_pref_record_system  = 2,   //原录制选项「为电脑声音生成字幕」；2026-10-06 起不再使用（「生成字幕」同时识别电脑声音），编号保留
    caption_pref_live_self      = 3,   //屏幕共享「为我的发言生成字幕」，第一次默认关闭（D2）
    caption_pref_live_show      = 4,   //屏幕共享观看窗显示别人的字幕，第一次默认显示（D2）
};

//初始化参数。调用方分配、填好 m_cbSize 后传入
struct CaptionInitParams
{
    uint32_t       m_cbSize;   //本结构体的字节数，取 sizeof(CaptionInitParams)；DLL 据此拒绝布局不符的调用方
    const wchar_t* m_appDir;   //exe 所在目录（不带结尾反斜杠），DLL 据此找到 FlamingoCaptionModel.dll 与皮肤资源
};

//开始一次录制的字幕草稿时的参数。调用方分配、填好 m_cbSize 后传入
struct CaptionRecordingParams
{
    uint32_t       m_cbSize;      //本结构体的字节数，取 sizeof(CaptionRecordingParams)
    const wchar_t* m_videoPath;   //录像文件的完整路径（草稿写在它旁边）；不可为 NULL
    const wchar_t* m_selfName;    //本人的显示名，作为麦克风字幕的说话人；可为 NULL（不显示说话人）
    bool           m_mic;         //是否识别本人麦克风
    bool           m_system;      //是否识别电脑声音；与 m_mic 都为 false 时不建识别流，字幕全部经 AddRemoteCaption 加入
                                  //（录制屏幕共享时，本人与别人的字幕都来自房间里的实时字幕）
};

//观看端叠加显示的一行字幕
struct CaptionOverlayLine
{
    const wchar_t* m_speaker;   //说话人；可为 NULL
    const wchar_t* m_text;      //文字；不可为 NULL
    bool           m_partial;   //是否为正在说的中间结果（颜色稍淡）
};

/**
 *  宿主接口：由 FlamingoMedia 实现，本 DLL 调用。
 *  除注明「只在 UI 线程调用」的方法外都可能在任意线程上被调用，实现方须保证线程安全。
 */
class ICaptionHost
{
public:
    /**
     *  写日志。
     *  @param level 级别，取值见 CaptionLogLevel
     *  @param text  日志文字（英文，UTF-8），只在本次调用期间有效
     */
    virtual void Log(int level, const char* text) = 0;

    /**
     *  取界面文字的译文（与 IMediaHost::Translate 相同）。
     *  @return 译文，至少在下一次调用前有效；没有译文时返回 source 本身
     */
    virtual const wchar_t* Translate(const wchar_t* context, const wchar_t* source) = 0;

    /**
     *  取当前界面语言与界面字体（与 IMediaHost::GetUiLanguage 相同）。
     *  @return 缓冲区不够或取不到时返回 false
     */
    virtual bool GetUiLanguage(wchar_t* langBuf, uint32_t langChars, wchar_t* fontBuf, uint32_t fontChars) = 0;

    /**
     *  保存按账号的字幕设置（替换表、各处默认勾选项），只在 UI 线程调用。
     *  @param settingsJson 设置的 JSON 文本（UTF-8），只在本次调用期间有效
     */
    virtual void SaveSettings(const char* settingsJson) = 0;

    /**
     *  请用户选一个会话并把文件发过去（导出完成后的「发到会话」），只在 UI 线程调用。
     *  @param path 文件完整路径
     *  @return 已发起（用户可能在选择窗里取消）返回 true
     */
    virtual bool PickConversationAndSendFile(const wchar_t* path) = 0;

    //exe 的主窗口（编辑器的所有者窗口取不到时用它），只在 UI 线程调用
    virtual HWND GetMainWindow() = 0;

protected:
    virtual ~ICaptionHost()
    {
    }
};

/**
 *  识别流的结果回调：由调用方实现，在本 DLL 的识别线程上调用，实现方不得阻塞，需要改界面时自行转到 UI 线程。
 */
class ICaptionStreamListener
{
public:
    /**
     *  正在说的这句的中间结果（每次都是这句话到目前为止的全部文字）。
     *  @param text 文字，只在本次调用期间有效
     */
    virtual void OnCaptionPartial(const wchar_t* text) = 0;

    /**
     *  一句话定稿（已整理空白、已应用替换表）。
     *  @param text    文字，只在本次调用期间有效
     *  @param startMs 这句话的开始时刻（毫秒，与推入声音时的时间戳同一时间轴）
     *  @param endMs   这句话的结束时刻（毫秒）
     */
    virtual void OnCaptionFinal(const wchar_t* text, int64_t startMs, int64_t endMs) = 0;

protected:
    virtual ~ICaptionStreamListener()
    {
    }
};

/**
 *  一路识别流（由 ICaptionModule::CreateStream 创建）。
 */
class ICaptionStream
{
public:
    /**
     *  推入一段声音（任意线程，不阻塞；内部排队，由识别线程处理）。模型还在加载时先缓存，加载完后补识别。
     *  @param mono48k 48 kHz 单声道 16 位采样，只在本次调用期间有效
     *  @param samples 采样数
     *  @param ptsMs   这段声音第一个采样的时刻（毫秒）；连续推入时应与上一段首尾相接
     */
    virtual void PushPcm(const int16_t* mono48k, uint32_t samples, int64_t ptsMs) = 0;

    /**
     *  结束：立即返回（不等模型加载或积压的声音处理完，界面不会卡住）。返回后不再有任何回调（此刻若正在回调，
     *  等这次回调结束），尚未定稿的最后一句不再回调；本对象由 DLL 在后台收尾后释放，调用方不得再使用。任意线程。
     */
    virtual void Close() = 0;

protected:
    virtual ~ICaptionStream()
    {
    }
};

/**
 *  录制草稿的实时识别结果（录制中在屏幕上显示字幕用，docs/语音字幕方案.md 14.12）：由调用方实现，在本 DLL 的识别线程上
 *  调用，实现方不得阻塞，需要改界面时自行转到 UI 线程。
 */
class ICaptionRecordingObserver
{
public:
    /**
     *  某一路正在说的这句的中间结果（每次都是这句话到目前为止的全部文字）。
     *  @param source 哪一路，取值同 Model/CaptionCue.h 的 CaptionSource：caption_source_mic 或 caption_source_system
     *  @param text   文字，只在本次调用期间有效
     */
    virtual void OnRecordingPartial(int source, const wchar_t* text) = 0;

    /**
     *  某一路的一句定稿（已整理空白、已应用替换表），调用时这句已经装配进草稿。
     *  @param source 哪一路，取值同上
     *  @param text   文字，只在本次调用期间有效
     */
    virtual void OnRecordingFinal(int source, const wchar_t* text) = 0;

protected:
    virtual ~ICaptionRecordingObserver()
    {
    }
};

/**
 *  一次录制的字幕草稿（由 ICaptionModule::BeginRecording 创建）：内部按需建麦克风、电脑声音两路识别流，
 *  把结果装配成字幕、定期写草稿文件。
 */
class ICaptionRecording
{
public:
    /**
     *  推入本人麦克风的一段声音（录制写入线程，按录制时间轴；暂停期间不推）。
     *  @param mono48k 48 kHz 单声道 16 位采样
     *  @param samples 采样数
     *  @param ptsMs   这段声音在录像中的时刻（毫秒）
     */
    virtual void PushMic(const int16_t* mono48k, uint32_t samples, int64_t ptsMs) = 0;

    //推入电脑声音的一段声音，参数同 PushMic
    virtual void PushSystem(const int16_t* mono48k, uint32_t samples, int64_t ptsMs) = 0;

    /**
     *  加入一条别人的字幕（录制屏幕共享时，收到的字幕帧已换算到录像时间轴），只在 UI 线程调用。
     */
    virtual void AddRemoteCaption(const wchar_t* speaker, const wchar_t* text, int64_t startMs, int64_t endMs) = 0;

    /**
     *  记下画中画在画面里的位置（归一化到 0～1），排版字幕时避开它，只在 UI 线程调用。
     *  @param present 为 false 表示没有画中画，其余参数忽略
     */
    virtual void SetPip(bool present, double left, double top, double right, double bottom) = 0;

    /**
     *  录制正常结束：立即返回；DLL 在后台把已推入的声音识别完，写最终草稿，然后释放本对象。调用方不得再使用它。
     *  只在 UI 线程调用。
     *  @param durationMs 录像时长（毫秒）
     */
    virtual void Finish(int64_t durationMs) = 0;

    //录制作废（录像被删除）：立即返回；停止识别、删除草稿，然后由 DLL 释放本对象。只在 UI 线程调用
    virtual void Cancel() = 0;

    /**
     *  登记实时识别结果的观察者（第 2 版接口），只在 UI 线程调用。本函数返回之后不会再调用之前登记的观察者，所以调用方
     *  销毁观察者之前须先传 NULL 取消登记；Finish、Cancel 也会自动取消登记。
     *  @param observer 观察者（不拥有）；NULL 表示取消登记
     */
    virtual void SetObserver(ICaptionRecordingObserver* observer) = 0;

protected:
    virtual ~ICaptionRecording()
    {
    }
};

/**
 *  模块接口：由本 DLL 实现，FlamingoMedia 调用。除注明的之外只在 UI 线程调用。
 */
class ICaptionModule
{
public:
    //编译 DLL 时的 kApiVersion
    virtual uint32_t GetApiVersion() const = 0;

    /**
     *  初始化。
     *  @param host   宿主接口（不拥有，须在 Shutdown 返回前一直有效）
     *  @param params 初始化参数
     *  @return 参数不对时返回 false
     */
    virtual bool Init(ICaptionHost* host, const CaptionInitParams* params) = 0;

    //关闭：关掉编辑器、停止全部识别、卸载模型。之后不得再调用除 GetApiVersion 外的任何方法
    virtual void Shutdown() = 0;

    /**
     *  推送按账号的字幕设置（登录后、设置变化后）。
     *  @param settingsJson 设置的 JSON 文本（UTF-8）；为 NULL 或空串表示恢复默认
     */
    virtual void SetSettings(const char* settingsJson) = 0;

    //在后台开始加载模型（已加载或正在加载时什么都不做），任意线程
    virtual void Preload() = 0;

    //识别引擎的状态，取值见 CaptionEngineState，任意线程
    virtual int GetEngineState() = 0;

    /**
     *  建一路识别流（模型未加载时自动开始加载）。
     *  @param purpose  用途，取值见 CaptionStreamPurpose
     *  @param listener 结果回调（不拥有，须在 Close 返回前一直有效）
     *  @return 识别流；引擎已确定不可用（caption_engine_failed）时返回 NULL
     */
    virtual ICaptionStream* CreateStream(int purpose, ICaptionStreamListener* listener) = 0;

    /**
     *  开始一次录制的字幕草稿。
     *  @return 草稿会话；参数不对或引擎已确定不可用时返回 NULL
     */
    virtual ICaptionRecording* BeginRecording(const CaptionRecordingParams* params) = 0;

    /**
     *  打开某个录像的字幕编辑器（已打开时激活它）。
     *  @param owner     所有者窗口，可为 NULL
     *  @param videoPath 录像完整路径
     *  @return 文件不存在等原因打不开时返回 false
     */
    virtual bool OpenEditor(HWND owner, const wchar_t* videoPath) = 0;

    //录像的字幕文件状态，取值为 CaptionFileStatus 的组合，任意线程
    virtual int GetFileStatus(const wchar_t* videoPath) = 0;

    //录像被改名：草稿与导出的字幕文件一并改名（录制管理窗口调用）
    virtual void OnVideoRenamed(const wchar_t* oldPath, const wchar_t* newPath) = 0;

    //录像被删除：删除它的草稿（导出的文件是独立的录像，不删）
    virtual void OnVideoDeleted(const wchar_t* videoPath) = 0;

    /**
     *  读一项开关类偏好。
     *  @param pref 偏好，取值见 CaptionPreference
     *  @return 当前值；不认识的取值返回 false
     */
    virtual bool GetPreference(int pref) = 0;

    /**
     *  改一项开关类偏好，值有变化时经 ICaptionHost::SaveSettings 保存。
     *  @param pref  偏好，取值见 CaptionPreference；不认识的取值忽略
     *  @param value 新值
     */
    virtual void SetPreference(int pref, bool value) = 0;

    /**
     *  把若干行字幕画成一块透明图层（观看窗叠加用）：预乘透明度的 BGRA，自上而下，宽 width、高 height，
     *  文字底部居中、白字黑边，说话人用黄色。
     *  @param lines  要画的行（从上到下）
     *  @param count  行数
     *  @param width  图层宽（像素）
     *  @param height 图层高（像素）
     *  @param bgra   [出参] 至少 width × height 个像素，由调用方分配
     *  @param fontPx 字号（像素）
     *  @return 参数不对时返回 false
     */
    virtual bool RenderOverlay(const CaptionOverlayLine* lines, uint32_t count, uint32_t width, uint32_t height,
                               uint32_t* bgra, int fontPx) = 0;

protected:
    virtual ~ICaptionModule()
    {
    }
};

//导出函数 FlamingoCaption_Create 的类型。apiVersion 传调用方编译时的 kApiVersion；版本不同时返回 NULL
typedef ICaptionModule* (__cdecl* FlamingoCaptionCreateFn)(uint32_t apiVersion);

/**
 *  判断双方的接口版本是否兼容：必须完全相同。
 */
inline bool IsApiVersionCompatible(uint32_t hostVersion, uint32_t moduleVersion)
{
    return hostVersion == moduleVersion;
}

} // namespace caption
