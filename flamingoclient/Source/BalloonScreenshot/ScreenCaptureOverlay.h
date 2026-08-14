/**
 *  ScreenCaptureOverlay.h
 *
 *  截图遮罩窗口（balloonui DuiHost 子类）：发起一次屏幕截图。流程——
 *    ①抓取整个虚拟桌面成一张冻结位图；
 *    ②建一个覆盖全虚拟桌面、置顶、无边框的窗口，把冻结图交给 CaptureCanvas 画；
 *    ③用户框选区域、在操作栏选「复制 / 保存 / 发送 / 重选 / 取消」；
 *    ④复制（剪贴板）/ 保存（PNG 文件）由本类内部完成；「发送」把选区裁出存成临时
 *      PNG 后经回调交回发起方（聊天窗）作为图片消息发出；Esc 等同取消。
 *
 *  以模态方式运行（自带一个局部消息循环，类似旧 CatchScreen 的 DoModal），调用方
 *  在按钮点击处直接构造本对象并 Run 即可，结果同步返回，无需管理异步生命周期。
 *
 *  本文件遵循 balloonui 头文件约定：包含它之前调用方须已引入 ATL/WTL。
 *
 *  balloonwj@qq.com   2026-06-04
 */
#pragma once

#include "../../../third-party/balloonui/balloonui/DuiHost.h"
#include "CaptureCanvas.h"

namespace balloonscreenshot {

// 「发送」回调原型：用户在截图操作栏点「发送」时调用，把已存好的临时 PNG 文件路径
// 交回发起方。路径为 UTF-16；文件留在临时目录由发起方（或其上传流程）负责后续。
//   ctx：    Run 时传入的上下文（一般为聊天窗 this）；
//   pngPath：临时 PNG 文件全路径。
typedef void (*ScreenshotSendFn)(void* ctx, const wchar_t* pngPath);

// 截图遮罩窗口。
class CScreenCaptureOverlay : public balloonwjui::DuiHost
{
public:
    CScreenCaptureOverlay();
    ~CScreenCaptureOverlay();

    BEGIN_MSG_MAP(CScreenCaptureOverlay)
        MESSAGE_HANDLER(WM_DUI_NOTIFY, OnDuiNotify)
        MESSAGE_HANDLER(WM_KEYDOWN, OnKeyDown)
        CHAIN_MSG_MAP(balloonwjui::DuiHost)
    END_MSG_MAP()

    // 模态发起一次截图。捕获桌面 → 全屏浮层 → 局部消息循环直到用户完成。
    //   ownerHwnd：发起窗口（聊天窗 m_hWnd），用于回到前台时的归属；可为 NULL。
    //   sendFn：  用户选「发送」时回调（传出临时 PNG 路径，立即发图）。可为 NULL。
    //   stageFn： 用户选「发送到对话框」时回调（传出临时 PNG 路径，插入输入框）。可为 NULL。
    //   ctx：     两个回调共用的上下文（一般为聊天窗 this）。
    //   复制 / 保存 / 取消由本类内部处理、不回调。
    //   返回：true=用户选了「发送」或「发送到对话框」（已回调）；false=其它（取消等）。
    bool    Run(HWND ownerHwnd, ScreenshotSendFn sendFn, ScreenshotSendFn stageFn, void* ctx);

private:
    // 用户在操作栏选择的最终动作。
    enum Action
    {
        kActNone = 0,   // 尚未决定 / 取消
        kActSend,       // 发送（立即发图）
        kActStage,      // 发送到对话框（插入聊天输入框）
        kActCopy,       // 复制到剪贴板
        kActSave,       // 保存为文件
    };

    LRESULT OnDuiNotify(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnKeyDown  (UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

    // 抓取整个虚拟桌面成冻结位图，记录虚拟桌面原点 / 尺寸。失败返回 false。
    bool    CaptureVirtualDesktop();
    // 把当前选区从冻结位图里裁出一张新位图（调用方负责 DeleteObject）。无可用选区返回 NULL。
    HBITMAP CropSelection();
    // 复制 / 保存 / 发送 三个动作的具体实现（都基于 CropSelection）。
    void    DoCopyToClipboard();
    void    DoSaveToFile();
    void    DoSendToChat();

private:
    HBITMAP        m_frozen;       // 冻结的整桌面位图（本类持有 / 销毁）
    HBITMAP        m_resultCrop;   // 用户点动作瞬间裁好的选区位图（本类持有 / 销毁；关窗后再用）
    int            m_vx;           // 虚拟桌面原点 x（屏幕坐标）
    int            m_vy;           // 虚拟桌面原点 y
    int            m_vw;           // 虚拟桌面宽
    int            m_vh;           // 虚拟桌面高

    CaptureCanvas* m_canvas;       // 画布根控件（所有权移交 DuiHost 控件树后留裸指针访问）

    bool           m_done;         // 模态循环结束标志
    Action         m_action;       // 用户最终动作

    ScreenshotSendFn m_sendFn;     // 「发送」回调（立即发图）
    ScreenshotSendFn m_stageFn;    // 「发送到对话框」回调（插入输入框）
    void*            m_sendCtx;    // 两个回调共用的上下文
};

} // namespace balloonscreenshot
