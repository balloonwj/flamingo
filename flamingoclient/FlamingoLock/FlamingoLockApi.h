/**
 *  FlamingoLock.dll 与宿主 Flamingo.exe 之间的接口约定（锁屏模块，方案见 docs/锁屏方案.md）。
 *
 *  宿主在启动阶段用 LoadLibrary 加载本 DLL，按 FLAMINGO_LOCK_FACTORY_NAME 取工厂函数，传入编译时的
 *  kApiVersion 得到模块对象，再调用 Init 交出宿主接口。两边的版本号必须完全一致，否则工厂返回 NULL，
 *  宿主不显示「锁屏」菜单项，其余功能照常。
 *
 *  跨模块只传纯虚接口与基本类型：exe 与 DLL 各自静态链接 CRT，各有独立的堆，不能互相释放对方分配的内存。
 *
 *  典型用法（宿主侧，见 Source/Feature/Lock/LockHost.cpp）：
 *      ILockModule* m = factory(flamingolock::kApiVersion);
 *      m->Init(&host, &params);
 *      ...
 *      m->RequestLock();          // 托盘菜单「锁屏」
 *      ...
 *      m->Shutdown();             // 进程退出前
 *
 *  balloonwj@qq.com   2026-10-06
 */
#pragma once

#include <stdint.h>

namespace flamingolock {

//接口版本号。宿主与 DLL 的这个值必须一致；接口有任何变化都要加 1。
//1：初版（2026-10-06）
const uint32_t kApiVersion = 1;

//DLL 导出的工厂函数名（由 FlamingoLock.def 固定，32 位与 64 位相同）
#define FLAMINGO_LOCK_FACTORY_NAME "FlamingoLock_Create"

//DLL 文件名，宿主据此拼出完整路径
#define FLAMINGO_LOCK_DLL_NAME L"FlamingoLock.dll"

/**
 *  日志级别，与 FlamingoP2P、FlamingoMedia 的取值一致，宿主据此映射到自己的日志级别。
 */
enum LockLogLevel
{
    lock_log_info    = 0,   // 普通信息：锁屏开始、结束、窗口与绘制资源的创建等
    lock_log_warning = 1,   // 警告：可以继续运行的异常，例如某块显示器的窗口没建出来
    lock_log_error   = 2,   // 错误：功能无法继续，例如锁屏线程没有启动起来
};

/**
 *  模块初始化参数。
 */
struct LockInitParams
{
    uint32_t       m_cbSize;    // sizeof(LockInitParams)，DLL 据此确认双方用的是同一版头文件
    const wchar_t* m_appDir;    // 程序所在目录（不带结尾反斜杠），以 L'\0' 结尾；只在 Init 期间有效
};

/**
 *  宿主（Flamingo.exe）提供给模块的服务。由宿主实现，模块只调用、不持有其内存。
 *  除特别说明外，各方法都可能在 DLL 的锁屏线程上调用，宿主须保证线程安全。
 */
class ILockHost
{
public:
    /**
     *  写一行日志。
     *  @param level 日志级别，取 LockLogLevel
     *  @param utf8  已格式化好的一行 UTF-8 文本（已含 "[lock] " 前缀与调用处），以 '\0' 结尾，不可为 NULL
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
     *  读取上次选择的锁屏画面（config\flamingo.ini 的 [lock] animation）。
     *  @param buf   接收配置值（ASCII，如 "honor"）的缓冲区，不可为 NULL
     *  @param chars buf 的容量（字符数，含结尾 '\0'）
     *  @return 读到非空值返回 true；没有配置时返回 false，buf 置为空串
     */
    virtual bool ReadAnimationChoice(char* buf, uint32_t chars) = 0;

    /**
     *  保存本次选择的锁屏画面。
     *  @param token 配置值（ASCII，以 '\0' 结尾），不可为 NULL
     */
    virtual void WriteAnimationChoice(const char* token) = 0;

    /**
     *  锁屏开始或结束。在锁屏线程上调用；宿主只记下状态并把后续处理转到自己的界面线程上进行，
     *  不得在本回调里同步等待界面线程，否则可能与界面线程上正在调用的模块方法相互等待。
     *  @param locked true 表示锁屏已经开始（锁屏窗口已经盖住屏幕），false 表示已经解锁
     */
    virtual void OnLockStateChanged(bool locked) = 0;

protected:
    ~ILockHost()
    {
    }
};

/**
 *  锁屏模块。由 DLL 实现，对象归 DLL 所有，宿主不能释放。
 */
class ILockModule
{
public:
    /**
     *  初始化模块。须在调用其它方法之前、在宿主的界面线程上调用一次。
     *  @param host   宿主接口（借用，须在 Shutdown 返回之前一直有效），不可为 NULL
     *  @param params 初始化参数，不可为 NULL，m_cbSize 必须等于 sizeof(LockInitParams)
     *  @return 成功返回 true；参数非法时返回 false，模块不可用
     */
    virtual bool Init(ILockHost* host, const LockInitParams* params) = 0;

    /**
     *  关闭模块。锁屏或设置密码的窗口还开着时一并结束（锁屏线程退出、窗口销毁、键盘钩子摘除），
     *  返回后不会再回调宿主。宿主的界面线程在进程退出前调用；重复调用安全。
     */
    virtual void Shutdown() = 0;

    /**
     *  取 DLL 编译时的接口版本号，供宿主写日志。
     *  @return kApiVersion
     */
    virtual uint32_t GetApiVersion() const = 0;

    /**
     *  请求锁屏：在锁屏线程上弹出设置临时密码的窗口，用户确认后进入锁屏；用户取消则什么也不做。
     *  立即返回，不等待用户操作。宿主的界面线程调用。
     *  @return 已开始弹出窗口返回 true；设置窗口已经开着或已在锁屏中时返回 false（同时把已开的窗口提到前面）
     */
    virtual bool RequestLock() = 0;

    /**
     *  当前是否处于锁屏中（锁屏窗口已经盖住屏幕、尚未解锁）。任意线程可调用。
     *  @return 锁屏中返回 true
     */
    virtual bool IsLocked() const = 0;

protected:
    ~ILockModule()
    {
    }
};

/**
 *  工厂函数的类型。
 *  @param apiVersion 宿主编译时的 kApiVersion
 *  @return 版本一致时返回模块对象（归 DLL 所有）；不一致返回 NULL
 */
typedef ILockModule* (__cdecl* LockCreateFn)(uint32_t apiVersion);

} // namespace flamingolock
