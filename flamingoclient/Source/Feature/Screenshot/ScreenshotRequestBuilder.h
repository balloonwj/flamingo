/**
 *  ScreenshotRequestBuilder.h
 *
 *  exe 一侧发起截图时用的选项，以及把它转成 FlamingoScreenshot.dll 的请求与结果接收方（docs/截图独立模块方案.md
 *  第 3.3 节）。截图界面迁进 DLL 之前，聊天窗直接构造 CScreenCaptureOverlay、传入两个回调函数；迁移之后聊天窗仍然
 *  填同样的 ScreenshotOptions，由这里转成 flamingoshot::ScreenshotRequest，并把两个回调包装成 IScreenshotSink。
 *  全部是纯逻辑，不加载 DLL，FlamingoUnit 直接验证（用例 SH2）。
 *
 *  典型用法：
 *      ScreenshotOptions opts;  //聊天窗填好要排除的窗口、两个回调与上下文
 *      flamingoshot::ScreenshotRequest req = MakeScreenshotRequest(owner, opts);
 *      CScreenshotCallbackSink sink(opts);
 *      host.Run(req, sink.HasTarget() ? &sink : NULL);
 *
 *  balloonwj@qq.com   2026-10-07
 */
#pragma once

#include <windows.h>
#include "../../../FlamingoScreenshot/ScreenshotApi.h"

namespace balloonscreenshot {

// 「发送」回调原型：用户在截图操作栏点「发送」或「发送到对话框」时调用，把已存好的临时 PNG 文件路径交回发起方。
//   ctx：    发起截图时传入的上下文（一般为聊天窗 this）；
//   pngPath：临时 PNG 文件全路径，只在回调期间有效；文件留在临时目录，由发起方（或其上传流程）负责后续。
typedef void (*ScreenshotSendFn)(void* ctx, const wchar_t* pngPath);

// 一次截图的选项（2026-10-05 起，docs/截图快捷键与隐藏当前窗口方案.md；2026-10-07 随截图界面迁进 DLL 移到本文件）。
struct ScreenshotOptions
{
    ScreenshotOptions();   // 全部置空：不排除窗口、不以全屏选区开始、不绑定会话

    HWND             m_excludeHwnd;      // 截图里要去掉的窗口（发起截图的聊天窗，借用）；NULL=不去掉任何窗口
    bool             m_startFullScreen;  // true=进入截图界面时选区直接为鼠标所在的整块显示器
    ScreenshotSendFn m_sendFn;           // 「发送」回调；与 m_stageFn 都为 NULL 时视为不绑定会话，操作栏改为「完成」
    ScreenshotSendFn m_stageFn;          // 「发送到对话框」回调
    void*            m_ctx;              // 两个回调共用的上下文（一般为聊天窗 this）
};

/**
 *  把选项转成交给 FlamingoScreenshot.dll 的请求。
 *  @param owner 发起截图的窗口（聊天窗），可为 NULL（在聊天窗之外发起时）；「识别文字」的结果窗口居中摆在它上面
 *  @param opts  选项
 *  @return 填好 m_cbSize 的请求
 */
flamingoshot::ScreenshotRequest MakeScreenshotRequest(HWND owner, const ScreenshotOptions& opts);

/**
 *  把选项里的两个回调包装成 DLL 的结果接收方。只在一次截图期间使用，在栈上构造即可。
 */
class CScreenshotCallbackSink : public flamingoshot::IScreenshotSink
{
public:
    /**
     *  @param opts 选项；只记下两个回调与上下文，不保存 opts 本身
     */
    explicit CScreenshotCallbackSink(const ScreenshotOptions& opts);

    /**
     *  是否绑定了会话（两个回调至少有一个）。为 false 时调用方应传 NULL 给 DLL，让操作栏改为「完成」。
     */
    bool HasTarget() const;

    // 「发送」：转给 m_sendFn；没有设置时什么也不做
    void OnSend(const wchar_t* pngPath) override;

    // 「发送到对话框」：转给 m_stageFn；没有设置时什么也不做
    void OnStage(const wchar_t* pngPath) override;

private:
    ScreenshotSendFn m_sendFn;    // 「发送」回调，可为 NULL
    ScreenshotSendFn m_stageFn;   // 「发送到对话框」回调，可为 NULL
    void*            m_ctx;       // 两个回调共用的上下文（借用）
};

} // namespace balloonscreenshot
