/**
 *  识别引擎（模型）的加载与卸载（docs/语音字幕方案.md 4.2、5.2、5.3、14.3），CaptionEngine.h
 *
 *  在后台线程里加载模型：以数据文件方式打开 FlamingoCaptionModel.dll，取出 encoder（ORT 格式）、decoder、joiner、
 *  tokens 四个资源，前三个经补丁版 sherpa-onnx 的 SherpaOnnxRegisterMemoryFile 登记为内存文件，tokens 经
 *  tokens_buf 传入，并指定 model_type（避免 sherpa-onnx 为读模型类型把 encoder 多加载一遍），创建一个流式识别器。
 *  全部识别流共用这一个识别器，由识别线程（CaptionWorker）串行使用。没有识别流超过 kCaptionIdleUnloadMs 后，
 *  识别线程调用 Unload 释放（常驻内存约 250 MB）。
 *
 *  编译时没有 sherpa-onnx 静态库（工程未定义 CAPTION_HAS_ENGINE）时，状态恒为 caption_engine_failed。
 *
 *  线程：Preload、State 任意线程；Recognizer、Unload 只在识别线程调用；Shutdown 在 UI 线程调用。
 *
 *  典型用法：
 *      CaptionEngine::Instance().SetModelPath(appDir + L"\\FlamingoCaptionModel.dll");
 *      CaptionEngine::Instance().Preload();
 *      if (CaptionEngine::Instance().State() == caption_engine_ready) { ... }
 *
 *  balloonwj@qq.com   2026-10-06
 */
#pragma once

#include <windows.h>
#include <stdint.h>
#include <atomic>
#include <mutex>
#include <string>
#include <thread>

namespace caption {

//没有识别流多久之后卸载模型（毫秒）：5 分钟
const int64_t kCaptionIdleUnloadMs = 5 * 60 * 1000;

//识别引擎（进程内唯一）
class CaptionEngine
{
public:
    static CaptionEngine& Instance();

    //是否编译了识别引擎（工程定义了 CAPTION_HAS_ENGINE）
    static bool HasEngine();

    //设置模型 DLL 的完整路径（模块 Init 时调用一次，在 Preload 之前）
    void SetModelPath(const std::wstring& modelDllPath);

    //在后台线程开始加载（已加载、正在加载或已确定失败时什么都不做）；任意线程
    void Preload();

    //状态，取值见 CaptionEngineState；任意线程
    int State() const;

    //识别器（SherpaOnnxOnlineRecognizer*，状态为 caption_engine_ready 时有效）；只在识别线程调用
    const void* Recognizer() const;

    //卸载模型，状态回到 caption_engine_idle；只在识别线程、没有识别流时调用
    void Unload();

    //等加载线程结束并卸载模型；模块 Shutdown 时在 UI 线程调用
    void Shutdown();

private:
    CaptionEngine();
    ~CaptionEngine();
    CaptionEngine(const CaptionEngine&);
    CaptionEngine& operator=(const CaptionEngine&);

    //加载线程的主体
    void LoadOnLoaderThread();

    //释放识别器与模型 DLL（调用方持有 m_mutex）
    void ReleaseLocked();

    mutable std::mutex m_mutex;          //保护下面除 m_state 外的成员
    std::atomic<int>   m_state;          //状态，取值见 CaptionEngineState
    std::wstring       m_modelPath;      //模型 DLL 的完整路径
    std::thread        m_loader;         //加载线程；加载完成后在下一次 Preload 或 Shutdown 时 join
    HMODULE            m_modelModule;    //以数据文件方式打开的模型 DLL；识别器存续期间不能释放（资源内存在其中）
    const void*        m_recognizer;     //识别器（SherpaOnnxOnlineRecognizer*），未加载时为 NULL
};

} // namespace caption
