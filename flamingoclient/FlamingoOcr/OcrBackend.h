/**
 *  FlamingoOcr.dll 内部的引擎后端接口：导出的函数表（OcrApiTable.cpp）负责参数检查、异常拦截与结果组装，
 *  真正的识别交给实现了本接口的后端。正式 DLL 里的后端是 Windows.Media.Ocr（WinOcr/WinOcrEngine），
 *  单元测试换上假后端，以便在不依赖系统识别组件的情况下验证函数表的行为。
 *
 *  所有方法都由函数表调用，调用约束与 FlamingoOcrApi.h 中对应的导出函数相同；方法可以抛出 C++ 异常，
 *  函数表会在模块边界上把它们转成返回码。
 *
 *  balloonwj@qq.com   2026-10-05
 */
#pragma once

#include "FlamingoOcrApi.h"
#include "Core/OcrResultBuilder.h"

namespace flamingoocr {

/**
 *  识别引擎后端。由 DLL 持有，生命周期与进程相同。
 */
class IOcrBackend
{
public:
    virtual ~IOcrBackend() {}

    /**
     *  初始化引擎。函数表保证只调用一次。
     *  @param uiLanguage 宿主的界面语言代码（如 L"zh-CN"），可为 NULL
     *  @return 引擎能否继续工作；返回 false 时函数表的 m_init 报告失败。引擎只是在本机不可用（如 Win7）
     *          时应返回 true，由 QueryAvailability 报告原因
     */
    virtual bool Init(const wchar_t* uiLanguage) = 0;

    //引擎名称，静态字符串
    virtual const wchar_t* EngineName() const = 0;

    //引擎版本，静态字符串
    virtual const wchar_t* EngineVersion() const = 0;

    /**
     *  查询可用性。
     *  @param settingsUri [出参] 「打开系统设置」的地址，没有时置为 NULL；字符串归后端所有
     *  @return 可用性，取值见 enum OcrAvailability
     */
    virtual int QueryAvailability(const wchar_t** settingsUri) = 0;

    //可用的识别语言个数
    virtual uint32_t LanguageCount() const = 0;

    //第 index 种识别语言的 BCP-47 代码，越界时返回 NULL；字符串归后端所有
    virtual const wchar_t* LanguageTag(uint32_t index) const = 0;

    //第 index 种识别语言的显示名，越界时返回 NULL；字符串归后端所有
    virtual const wchar_t* LanguageName(uint32_t index) const = 0;

    //默认识别语言的 BCP-47 代码，引擎不可用时返回 NULL
    virtual const wchar_t* DefaultLanguage() const = 0;

    /**
     *  识别一张图片。参数已由函数表检查过：image 的字段合法，languageTag 为 NULL 表示默认语言。
     *  @param image       输入图像
     *  @param languageTag 识别语言，NULL 表示默认语言
     *  @param cancelFlag  取消标志，可为 NULL
     *  @param out         [出参] 识别结果
     *  @return 结果码，取值见 enum OcrResultCode
     */
    virtual int Recognize(const OcrImage& image, const wchar_t* languageTag, volatile long* cancelFlag,
                          OcrResultData& out) = 0;
};

} // namespace flamingoocr
