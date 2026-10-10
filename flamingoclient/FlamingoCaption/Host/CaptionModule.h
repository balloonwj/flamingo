/**
 *  FlamingoCaption.dll 的模块对象（ICaptionModule 的实现），CaptionModule.h
 *
 *  进程内唯一，由导出函数 FlamingoCaption_Create 返回。负责：
 *    - Init / Shutdown：日志宿主、GDI+、多语言（TR 经宿主翻译）、识别引擎的模型路径、识别线程的启停；
 *    - 按账号的字幕设置（SetSettings 推入，SaveSettings 经宿主保存）；
 *    - 把识别流、录制草稿、编辑器、文件状态、字幕绘制等请求转给各部分。
 *
 *  典型用法：见 CaptionApi.h。
 *
 *  balloonwj@qq.com   2026-10-06
 */
#pragma once

#include <mutex>
#include <string>
#include <vector>

#include "../CaptionApi.h"
#include "../Model/CaptionSettings.h"

namespace caption {

//模块对象
class CaptionEditorWindow;

class CCaptionModule : public ICaptionModule
{
public:
    static CCaptionModule& Instance();

    //见 ICaptionModule
    virtual uint32_t GetApiVersion() const override;
    virtual bool Init(ICaptionHost* host, const CaptionInitParams* params) override;
    virtual void Shutdown() override;
    virtual void SetSettings(const char* settingsJson) override;
    virtual void Preload() override;
    virtual int GetEngineState() override;
    virtual ICaptionStream* CreateStream(int purpose, ICaptionStreamListener* listener) override;
    virtual ICaptionRecording* BeginRecording(const CaptionRecordingParams* params) override;
    virtual bool OpenEditor(HWND owner, const wchar_t* videoPath) override;
    virtual int GetFileStatus(const wchar_t* videoPath) override;
    virtual void OnVideoRenamed(const wchar_t* oldPath, const wchar_t* newPath) override;
    virtual void OnVideoDeleted(const wchar_t* videoPath) override;
    virtual bool GetPreference(int pref) override;
    virtual void SetPreference(int pref, bool value) override;
    virtual bool RenderOverlay(const CaptionOverlayLine* lines, uint32_t count, uint32_t width, uint32_t height,
                               uint32_t* bgra, int fontPx) override;

    //当前设置的副本（UI 线程）
    CaptionSettings GetSettings();

    //修改设置并经宿主保存（UI 线程）
    void UpdateSettings(const CaptionSettings& settings);

    //宿主接口（Init 之后有效）
    ICaptionHost* Host() const;

    //exe 所在目录（不带结尾反斜杠）
    const std::wstring& AppDir() const;

private:
    CCaptionModule();
    CCaptionModule(const CCaptionModule&);
    CCaptionModule& operator=(const CCaptionModule&);

    //i18n::InitFromHost 要的翻译函数：转给宿主
    static const wchar_t* TranslateThunk(const wchar_t* context, const wchar_t* source);

    ICaptionHost*   m_host;          //宿主（不拥有），Init 到 Shutdown 之间有效
    std::wstring    m_appDir;        //exe 所在目录
    ULONG_PTR       m_gdiplusToken;  //GdiplusStartup 的令牌，0 表示未启动
    std::mutex      m_settingsMutex; //保护 m_settings
    CaptionSettings m_settings;      //按账号的字幕设置
    bool            m_inited;        //是否已 Init
    bool            m_mfStarted;     //Init 时是否成功调用了 MFStartup（字幕编辑器的预览要用 Media Foundation）
    std::vector<CaptionEditorWindow*> m_editors;   //打开过的字幕编辑器窗口（本类持有，Shutdown 时关闭并释放）
};

} // namespace caption
