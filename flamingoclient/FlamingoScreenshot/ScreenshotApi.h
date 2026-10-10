/**
 *  FlamingoScreenshot.dll 与宿主 Flamingo.exe 之间的接口约定（截图模块，方案见 docs/截图独立模块方案.md）。
 *
 *  宿主在启动阶段用 LoadLibrary 加载本 DLL（未登录时也要能截图），按 FLAMINGO_SCREENSHOT_FACTORY_NAME 取工厂函数，
 *  传入编译时的 kApiVersion 得到模块对象，再调用 Init 交出宿主接口。两边的版本号必须完全一致，否则工厂返回 NULL，
 *  宿主把截图功能当作不可用：托盘菜单不出现「截图」、聊天窗的截图按钮隐藏、截图快捷键不注册。
 *
 *  跨模块只传纯虚接口与基本类型：exe 与 DLL 各自静态链接 CRT，各有独立的堆，不能互相释放对方分配的内存。
 *
 *  典型用法（宿主侧，见 Source/Feature/Screenshot/ScreenshotHost.cpp）：
 *      IScreenshotModule* m = factory(flamingoshot::kApiVersion);
 *      m->Init(&host, &params);
 *      ...
 *      ScreenshotRequest req;  //填好发起窗口、要排除的窗口等
 *      m->Run(&req, &sink);    //模态运行，返回时截图已结束
 *      ...
 *      m->Shutdown();          //进程退出前
 *
 *  balloonwj@qq.com   2026-10-07
 */
#pragma once

#include <windows.h>
#include <stdint.h>

namespace flamingoshot {

//接口版本号。宿主与 DLL 的这个值必须一致；接口有任何变化都要加 1。
//1：初版（2026-10-07，方案第 5 节 S5 的骨架；截图界面在 S6 迁入，接口不变）
const uint32_t kApiVersion = 1;

//DLL 导出的工厂函数名（由 FlamingoScreenshot.def 固定，32 位与 64 位相同）
#define FLAMINGO_SCREENSHOT_FACTORY_NAME "FlamingoScreenshot_Create"

//DLL 文件名，宿主据此拼出完整路径
#define FLAMINGO_SCREENSHOT_DLL_NAME L"FlamingoScreenshot.dll"

/**
 *  日志级别，与 FlamingoLock、FlamingoMedia 的取值一致，宿主据此映射到自己的日志级别。
 */
enum ShotLogLevel
{
    shot_log_info    = 0,   // 普通信息：截图开始、结束、用户选的动作等
    shot_log_warning = 1,   // 警告：可以继续运行的异常，例如抓屏失败、保存文件失败
    shot_log_error   = 2,   // 错误：功能无法继续，例如截图窗口没有建出来
};

/**
 *  模块初始化参数。
 */
struct ScreenshotInitParams
{
    uint32_t       m_cbSize;    // sizeof(ScreenshotInitParams)，DLL 据此确认双方用的是同一版头文件
    const wchar_t* m_appDir;    // 程序所在目录（不带结尾反斜杠），以 L'\0' 结尾；只在 Init 期间有效
};

/**
 *  一次截图的请求。
 */
struct ScreenshotRequest
{
    uint32_t m_cbSize;            // sizeof(ScreenshotRequest)
    HWND     m_owner;             // 发起截图的窗口（聊天窗），可为 NULL（在聊天窗之外发起时）；「识别文字」的结果窗口
                                  // 居中摆在它上面
    HWND     m_excludeHwnd;       // 截图里要去掉的窗口（「截图时隐藏当前窗口」开启时为发起截图的聊天窗），NULL 表示不去掉
    bool     m_startFullScreen;   // true 表示进入截图界面时选区直接为鼠标所在的整块显示器（聊天窗「截取全屏」）
};

/**
 *  截图结果的接收方，由发起截图的一方（聊天窗）实现，每次截图传入。传 NULL 表示不绑定会话：操作栏为
 *  「重选 / 保存 / 取消 / 完成」，「完成」与选区内双击都把截图放进剪贴板。
 *  两个方法都在调用 Run 的线程（宿主的界面线程）上、Run 返回之前调用。
 */
class IScreenshotSink
{
public:
    /**
     *  用户点了「发送」：立即作为图片消息发出。
     *  @param pngPath 已存好的临时 PNG 的完整路径，以 L'\0' 结尾；只在本次调用期间有效，接收方需要时自行复制。
     *                 文件留在临时目录，由接收方（或其上传流程）负责后续
     */
    virtual void OnSend(const wchar_t* pngPath) = 0;

    /**
     *  用户点了「发送到对话框」：把截图插入输入框。
     *  @param pngPath 同 OnSend
     */
    virtual void OnStage(const wchar_t* pngPath) = 0;

protected:
    ~IScreenshotSink()
    {
    }
};

/**
 *  宿主（Flamingo.exe）提供给模块的服务。由宿主实现，模块只调用、不持有其内存。
 *  除特别说明外，各方法都在宿主的界面线程上调用（截图界面运行在调用 Run 的线程上）。
 */
class IScreenshotHost
{
public:
    /**
     *  写一行日志。
     *  @param level 日志级别，取 ShotLogLevel
     *  @param utf8  已格式化好的一行 UTF-8 文本（已含 "[shot] " 前缀与调用处），以 '\0' 结尾，不可为 NULL
     */
    virtual void Log(int level, const char* utf8) = 0;

    /**
     *  查界面文字的译文。宿主转给自己的语言模块（i18n::Translate）。
     *  @param context 语境，为 NULL 表示没有语境
     *  @param source  简体中文原文
     *  @return 当前界面语言的译文，进程结束前有效；没有译文时返回 source 本身
     */
    virtual const wchar_t* Translate(const wchar_t* context, const wchar_t* source) = 0;

    /**
     *  取宿主当前生效的界面语言代码与界面字体名。
     *  @param langBuf   接收语言代码（如 L"zh-CN"）的缓冲区，不可为 NULL
     *  @param langChars langBuf 的容量（字符数，含结尾 L'\0'）
     *  @param fontBuf   接收界面字体名的缓冲区，不可为 NULL
     *  @param fontChars fontBuf 的容量（字符数，含结尾 L'\0'）
     *  @return 两项都取到返回 true；宿主还没有确定语言时返回 false
     */
    virtual bool GetUiLanguage(wchar_t* langBuf, uint32_t langChars, wchar_t* fontBuf, uint32_t fontChars) = 0;

    /**
     *  操作栏要不要显示「识别文字」（宿主的 OCR 模块已就绪，见 ocr::IsOcrEntryVisible）。每次进入截图界面时取一次。
     *  @return 显示返回 true
     */
    virtual bool IsOcrAvailable() = 0;

    /**
     *  用户点了「识别文字」：截图界面关闭之后调用，宿主打开识别结果窗口并开始识别（ocr::LaunchOcrForImage）。
     *  @param owner  发起截图的窗口（ScreenshotRequest::m_owner），可为 NULL
     *  @param bgra   选区的原始像素（不含标注），32 位 BGRA，自上而下逐行紧排；只在本次调用期间有效，宿主需要时自行复制
     *  @param width  宽（像素），大于 0
     *  @param height 高（像素），大于 0
     *  @return 已开始识别返回 true；OCR 不可用或参数无效时返回 false
     */
    virtual bool LaunchOcrForImage(HWND owner, const uint8_t* bgra, uint32_t width, uint32_t height) = 0;

protected:
    ~IScreenshotHost()
    {
    }
};

/**
 *  截图模块。由 DLL 实现，对象归 DLL 所有，宿主不能释放。
 */
class IScreenshotModule
{
public:
    /**
     *  初始化模块。须在调用其它方法之前、在宿主的界面线程上调用一次。
     *  @param host   宿主接口（借用，须在 Shutdown 返回之前一直有效），不可为 NULL
     *  @param params 初始化参数，不可为 NULL，m_cbSize 必须等于 sizeof(ScreenshotInitParams)
     *  @return 成功返回 true；参数非法时返回 false，模块不可用
     */
    virtual bool Init(IScreenshotHost* host, const ScreenshotInitParams* params) = 0;

    /**
     *  关闭模块，返回后不会再回调宿主。宿主的界面线程在进程退出前调用；重复调用安全。
     */
    virtual void Shutdown() = 0;

    /**
     *  取 DLL 编译时的接口版本号，供宿主写日志。
     *  @return kApiVersion
     */
    virtual uint32_t GetApiVersion() const = 0;

    /**
     *  本进程当前是否正有一次截图在进行（截图界面的局部消息循环尚未结束）。
     */
    virtual bool IsRunning() const = 0;

    /**
     *  模态发起一次截图：抓取整个桌面 → 全屏截图界面 → 局部消息循环直到用户完成。在宿主的界面线程上调用。
     *  复制、保存、取消由模块内部完成；「发送」「发送到对话框」经 sink 交回；「识别文字」经宿主的 LaunchOcrForImage。
     *  @param request 本次截图的请求，不可为 NULL，m_cbSize 必须等于 sizeof(ScreenshotRequest)
     *  @param sink    结果接收方（借用，只在本次调用期间使用），可为 NULL（不绑定会话）
     *  @return 用户选了「发送」或「发送到对话框」返回 true；其它情况（取消、完成、复制、保存、识别文字，已有一次截图在
     *          进行，参数非法，模块未初始化）返回 false
     */
    virtual bool Run(const ScreenshotRequest* request, IScreenshotSink* sink) = 0;

protected:
    ~IScreenshotModule()
    {
    }
};

/**
 *  工厂函数的类型。
 *  @param apiVersion 宿主编译时的 kApiVersion
 *  @return 版本一致时返回模块对象（归 DLL 所有）；不一致返回 NULL
 */
typedef IScreenshotModule* (__cdecl* ScreenshotCreateFn)(uint32_t apiVersion);

} // namespace flamingoshot
