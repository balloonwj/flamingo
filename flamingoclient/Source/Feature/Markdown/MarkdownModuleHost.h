/**
 *  Markdown 消息渲染模块（FlamingoMarkdown.dll）的宿主：进程启动后在后台线程加载 DLL 并初始化，结果缓存到
 *  进程退出；界面线程随时读取缓存的状态，从不等待 DLL（docs/Markdown消息方案.md 第 6.1 节）。
 *
 *  状态只会单向变化：未开始 → 加载中 → 可用 / 不可用。「不可用」（DLL 不存在、加载失败、不是本模块、函数表过旧、
 *  初始化失败）时客户端不显示任何 Markdown 入口，收到的 Markdown 消息按普通文字显示源文本，之后不再重试。
 *  DLL 加载后不卸载：消息气泡持有的文档要一直可用到进程退出。
 *
 *  加载在 CAppShell::Startup 里、登录之前发起；DLL 只有几百 KB、初始化不做耗时的事，登录完成前早已就绪，
 *  聊天窗不会比它先打开，所以不做「加载完成」的通知。
 *
 *  典型用法：
 *
 *      //进程启动时（CAppShell::Startup）
 *      markdown::CMarkdownModuleHost::GetInstance().StartPreload(dllPath);
 *      //显示工具栏、排版气泡时
 *      if (markdown::CMarkdownModuleHost::GetInstance().CanUse()) { ... }
 *
 *  本文件是纯逻辑模块，与 FlamingoUnit 测试工程共用，<u>不</u> include stdafx.h。
 *
 *  balloonwj@qq.com   2026-10-06
 */
#pragma once

#include <windows.h>
#include <string>

#include "../../../FlamingoMarkdown/FlamingoMarkdownApi.h"
#include "../../App/PluginModuleLoader.h"

namespace markdown {

//宿主状态
enum MarkdownHostState
{
    md_host_not_started = 0,   //StartPreload 尚未调用：入口不显示
    md_host_loading     = 1,   //预加载线程正在运行：入口暂不显示，Markdown 消息暂按纯文字显示
    md_host_unavailable = 2,   //DLL 不存在、加载失败、不是本模块、函数表过旧或初始化失败：入口不显示，不再重试
    md_host_ready       = 3,   //DLL 已加载并初始化：入口显示，Markdown 消息按格式渲染
};

/**
 *  测试用的函数表来源：代替「加载 DLL 并调用 FlamingoMarkdown_GetApi」。在预加载线程上调用。
 *  @param context 调用方传入的上下文
 *  @param failure [出参] 返回 NULL 时写入英文原因（静态字符串）
 *  @return 函数表；返回 NULL 表示加载失败
 */
typedef const flamingomd::MdApi* (*MarkdownApiProviderFn)(void* context, const char** failure);

/**
 *  Markdown 模块宿主。进程内用 GetInstance() 取唯一实例；单元测试可直接构造独立实例。
 *  除 GetInstance 外的所有方法都是线程安全的。
 */
class CMarkdownModuleHost
{
public:
    CMarkdownModuleHost();

    //等待预加载线程结束（最多 kDestroyWaitMs）后析构；进程内唯一实例从不析构
    ~CMarkdownModuleHost();

    /**
     *  取进程内唯一的实例（堆上分配，进程退出时不析构）。
     */
    static CMarkdownModuleHost& GetInstance();

    /**
     *  启动预加载：在后台线程加载 dllPath 并初始化。只有第一次调用生效（含 StartPreloadWithProvider），立即返回。
     *  @param dllPath FlamingoMarkdown.dll 的完整路径
     */
    void StartPreload(const std::wstring& dllPath);

    /**
     *  测试用：与 StartPreload 相同，但函数表由 provider 提供，不加载 DLL。
     *  @param provider 函数表来源，在预加载线程上调用，不可为 NULL
     *  @param context  原样交给 provider
     */
    void StartPreloadWithProvider(MarkdownApiProviderFn provider, void* context);

    /**
     *  等待预加载结束。供单元测试与不登录就开窗的调试入口（--chatwindow 等）使用；正式流程的界面线程不得调用，
     *  正式流程里预加载在登录完成之前早已结束。
     *  @param timeoutMs 最长等待时间（毫秒）
     *  @return 预加载已结束（状态为 ready 或 unavailable）时返回 true
     */
    bool WaitForPreload(DWORD timeoutMs);

    //当前状态，取值见 enum MarkdownHostState
    int GetState() const;

    //是否可以使用：状态为 ready 时为 true
    bool CanUse() const;

    /**
     *  已初始化的函数表。
     *  @return 状态为 ready 时返回函数表（归 DLL 所有，进程退出前一直有效），否则返回 NULL
     */
    const flamingomd::MdApi* GetApi() const;

    //不可用时的原因（英文，写日志与测试用）；其它状态为空串
    std::string GetFailure() const;

    //预加载结束后析构时最多等待的时间（毫秒）
    static const DWORD kDestroyWaitMs = 5000;

private:
    CMarkdownModuleHost(const CMarkdownModuleHost&);
    CMarkdownModuleHost& operator=(const CMarkdownModuleHost&);

    //启动预加载线程；已启动过时直接返回
    void Start(MarkdownApiProviderFn provider, void* context, const std::wstring& dllPath);

    //预加载线程入口
    static DWORD WINAPI PreloadThreadProc(LPVOID param);

    //预加载线程的工作：取函数表、检查大小、初始化
    void RunPreload();

    //把结果写进缓存（加锁）
    void Finish(int state, const flamingomd::MdApi* api, const std::string& failure);

    mutable SRWLOCK            m_lock;       //保护以下全部字段
    bool                       m_started;    //是否已调用过 StartPreload；第一次调用后置为 true，不再变化
    HANDLE                     m_thread;     //预加载线程句柄；线程结束后仍保留到析构时关闭
    MarkdownApiProviderFn      m_provider;   //测试用的函数表来源；为 NULL 时加载 m_dllPath
    void*                      m_context;    //交给 m_provider 的上下文
    std::wstring               m_dllPath;    //DLL 完整路径
    balloonapp::CPluginLibrary m_library;    //DLL 本身；只在预加载线程上访问，加载后不卸载
    int                        m_state;      //状态，取值见 enum MarkdownHostState
    const flamingomd::MdApi*   m_api;        //状态为 ready 时的函数表，否则为 NULL
    std::string                m_failure;    //不可用的原因
};

} // namespace markdown
