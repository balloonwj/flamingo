/**
 *  图片文字识别模块（FlamingoOcr.dll）的宿主：登录成功后在后台线程加载 DLL、初始化引擎、查询可用性与识别语言，
 *  结果缓存到进程退出；界面线程随时读取缓存的状态，从不等待 DLL（docs/图片文字识别方案.md 第 5.2、8.1 节）。
 *
 *  状态只会单向变化：未开始 → 加载中 → 可用 / 不可用。「不可用」（DLL 不存在、加载失败、不是本模块、函数表过旧、
 *  初始化失败）时三个识别入口都不显示，之后不再重试；「可用」时入口照常显示，引擎能否识别另由可用性说明
 *  （系统不支持、没有识别语言时点击入口给出提示）。DLL 加载后不卸载。
 *
 *  典型用法：
 *
 *      //登录成功后（MainPanelSessionController）
 *      ocr::COcrModuleHost::GetInstance().StartPreload(dllPath, i18n::GetLanguage());
 *      //显示菜单 / 工具栏时
 *      if (ocr::COcrModuleHost::GetInstance().CanShowEntry()) { ...加上「图片文字识别」... }
 *
 *  本文件是纯逻辑模块，与 FlamingoUnit 测试工程共用，<u>不</u> include stdafx.h。
 *
 *  balloonwj@qq.com   2026-10-05
 */
#pragma once

#include <windows.h>
#include <stdint.h>
#include <string>
#include <vector>

#include "../../../FlamingoOcr/FlamingoOcrApi.h"
#include "../../App/PluginModuleLoader.h"

namespace ocr {

//宿主状态
enum OcrHostState
{
    ocr_host_not_started = 0,   //StartPreload 尚未调用：入口不显示
    ocr_host_loading     = 1,   //预加载线程正在运行：入口暂不显示
    ocr_host_unavailable = 2,   //DLL 不存在、加载失败、不是本模块、函数表过旧或初始化失败：入口不显示，不再重试
    ocr_host_ready       = 3,   //DLL 已加载并初始化：入口显示，引擎能否识别见 OcrHostSnapshot::m_availability
};

/**
 *  一种识别语言。
 */
struct OcrLanguageOption
{
    std::wstring m_tag;    //BCP-47 代码，如 L"zh-Hans-CN"
    std::wstring m_name;   //显示名，由引擎按系统语言给出
};

/**
 *  宿主缓存的模块信息快照，供界面使用。
 */
struct OcrHostSnapshot
{
    int                            m_state;             //宿主状态，取值见 enum OcrHostState
    int                            m_availability;      //引擎可用性，取值见 flamingoocr::OcrAvailability；状态不是 ready 时无意义
    std::wstring                   m_settingsUri;       //「打开系统设置」的地址；引擎没有给出时为空
    std::vector<OcrLanguageOption> m_languages;         //可用的识别语言
    std::wstring                   m_defaultLanguage;   //默认识别语言的代码；没有时为空
    std::wstring                   m_engineName;        //引擎名称
    std::wstring                   m_engineVersion;     //引擎版本
    std::string                    m_failure;           //不可用时的原因（英文，写日志用）

    OcrHostSnapshot();
};

/**
 *  测试用的函数表来源：代替「加载 DLL 并调用 FlamingoOcr_GetApi」。在预加载线程上调用。
 *  @param context 调用方传入的上下文
 *  @param failure [出参] 返回 NULL 时写入英文原因（静态字符串）
 *  @return 函数表；返回 NULL 表示加载失败
 */
typedef const flamingoocr::OcrApi* (*OcrApiProviderFn)(void* context, const char** failure);

/**
 *  识别模块宿主。进程内用 GetInstance() 取唯一实例；单元测试可直接构造独立实例。
 *  除 GetInstance 外的所有方法都是线程安全的。
 */
class COcrModuleHost
{
public:
    COcrModuleHost();

    //等待预加载线程结束（最多 kDestroyWaitMs）后析构；进程内唯一实例从不析构
    ~COcrModuleHost();

    /**
     *  取进程内唯一的实例（堆上分配，进程退出时不析构，避免退出时等待可能卡住的引擎）。
     */
    static COcrModuleHost& GetInstance();

    /**
     *  启动预加载：在后台线程加载 dllPath、初始化、查询可用性。只有第一次调用生效（含 StartPreloadWithProvider），
     *  立即返回。
     *  @param dllPath    FlamingoOcr.dll 的完整路径
     *  @param uiLanguage 当前界面语言代码（如 L"zh-CN"），用于选默认识别语言；可为空
     */
    void StartPreload(const std::wstring& dllPath, const std::wstring& uiLanguage);

    /**
     *  测试用：与 StartPreload 相同，但函数表由 provider 提供，不加载 DLL。
     *  @param provider   函数表来源，在预加载线程上调用，不可为 NULL
     *  @param context    原样交给 provider
     *  @param uiLanguage 当前界面语言代码
     */
    void StartPreloadWithProvider(OcrApiProviderFn provider, void* context, const std::wstring& uiLanguage);

    /**
     *  等待预加载结束（测试用；界面线程不得调用）。
     *  @param timeoutMs 最长等待时间（毫秒）
     *  @return 预加载已结束（状态为 ready 或 unavailable）时返回 true
     */
    bool WaitForPreload(DWORD timeoutMs);

    //当前状态，取值见 enum OcrHostState
    int GetState() const;

    //是否显示识别入口：状态为 ready（无论引擎可用性如何）时为 true
    bool CanShowEntry() const;

    //当前缓存信息的副本
    OcrHostSnapshot GetSnapshot() const;

    /**
     *  已初始化的函数表，供识别线程调用 m_recognize / m_freeResult。
     *  @return 状态为 ready 时返回函数表（归 DLL 所有），否则返回 NULL
     */
    const flamingoocr::OcrApi* GetApi() const;

    //预加载结束后析构时最多等待的时间（毫秒）
    static const DWORD kDestroyWaitMs = 5000;

private:
    COcrModuleHost(const COcrModuleHost&);
    COcrModuleHost& operator=(const COcrModuleHost&);

    //启动预加载线程；已启动过时直接返回
    void Start(OcrApiProviderFn provider, void* context, const std::wstring& dllPath, const std::wstring& uiLanguage);

    //预加载线程入口
    static DWORD WINAPI PreloadThreadProc(LPVOID param);

    //预加载线程的工作：取函数表、初始化、查询并缓存
    void RunPreload();

    //把结果写进缓存（加锁）
    void Finish(int state, const flamingoocr::OcrApi* api, const OcrHostSnapshot& snapshot);

    mutable SRWLOCK            m_lock;          //保护以下全部字段
    bool                       m_started;       //是否已调用过 StartPreload；第一次调用后置为 true，不再变化
    HANDLE                     m_thread;        //预加载线程句柄；线程结束后仍保留到析构时关闭
    OcrApiProviderFn           m_provider;      //测试用的函数表来源；为 NULL 时加载 m_dllPath
    void*                      m_context;       //交给 m_provider 的上下文
    std::wstring               m_dllPath;       //DLL 完整路径
    std::wstring               m_uiLanguage;    //界面语言代码，m_init 期间须保持有效
    balloonapp::CPluginLibrary m_library;       //DLL 本身；只在预加载线程上访问，加载后不卸载
    const flamingoocr::OcrApi* m_api;           //状态为 ready 时的函数表，否则为 NULL
    OcrHostSnapshot            m_snapshot;      //缓存的模块信息
};

} // namespace ocr
