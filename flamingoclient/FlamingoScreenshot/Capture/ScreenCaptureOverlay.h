/**
 *  ScreenCaptureOverlay.h
 *
 *  截图遮罩窗口（balloonui DuiHost 子类）：发起一次屏幕截图。流程——
 *    ①抓取整个虚拟桌面成一张冻结位图；
 *    ②建一个覆盖全虚拟桌面、置顶、无边框的窗口，把冻结图交给 CaptureCanvas 画；
 *    ③用户框选区域、在操作栏选「复制 / 保存 / 发送 / 重选 / 取消」；
 *    ④复制（剪贴板）/ 保存（PNG 文件）由本类内部完成；「发送」把选区裁出存成临时
 *      PNG 后经回调交回发起方（聊天窗）作为图片消息发出；Esc 等同取消。
 *  2026-10-05 起（docs/截图快捷键与隐藏当前窗口方案.md）：抓屏前可把发起截图的聊天窗排除
 *  在外（CaptureExclusion）；可以全屏选区开始，截图界面里按 Ctrl+A 也会选中鼠标所在的整块
 *  显示器；没有绑定会话（在聊天窗之外按截图快捷键）时操作栏改为「完成」，结果放进剪贴板；
 *  同一时刻只允许一次截图，截图进行中再次调用 Run 直接返回。
 *  2026-10-07 起本文件随截图界面迁入 FlamingoScreenshot.dll（docs/截图独立模块方案.md 第 5 节 S6）：入口改为宿主经
 *  IScreenshotModule::Run 传入的 ScreenshotRequest 与 IScreenshotSink，「识别文字」经宿主服务交给 exe。
 *
 *  以模态方式运行（自带一个局部消息循环，类似旧 CatchScreen 的 DoModal），调用方
 *  在按钮点击处直接构造本对象并 Run 即可，结果同步返回，无需管理异步生命周期。
 *
 *  本文件遵循 balloonui 头文件约定：包含它之前调用方须已引入 ATL/WTL。
 *
 *  balloonwj@qq.com   2026-06-04
 */
#pragma once

#include <vector>
#include "../../../third-party/balloonui/balloonui/DuiHost.h"
#include "CaptureCanvas.h"
#include "../ScreenshotApi.h"   // ScreenshotRequest / IScreenshotSink：宿主传入的请求与结果接收方
#include "ShotBgra.h"            // ShotBgraImage：「识别文字」用的选区原始像素

namespace balloonscreenshot {

// 截图遮罩窗口。
class CScreenCaptureOverlay : public balloonwjui::DuiHost
{
public:
    CScreenCaptureOverlay();
    ~CScreenCaptureOverlay();

    // 本进程当前是否正有一次截图在进行（截图界面的局部消息循环尚未结束）。
    static bool IsRunning();

    BEGIN_MSG_MAP(CScreenCaptureOverlay)
        MESSAGE_HANDLER(WM_DUI_NOTIFY, OnDuiNotify)
        MESSAGE_HANDLER(WM_KEYDOWN, OnKeyDown)
        CHAIN_MSG_MAP(balloonwjui::DuiHost)
    END_MSG_MAP()

    // 模态发起一次截图：抓取整个虚拟桌面 → 全屏浮层 → 局部消息循环直到用户完成。
    //   request：本次截图的请求（发起窗口、要排除的窗口、是否以全屏选区开始），由模块校验过 m_cbSize。
    //   sink：   结果接收方（借用，只在本次调用期间使用）；为 NULL 时不绑定会话，操作栏为「重选 / 保存 / 取消 / 完成」，
    //            「完成」与选区内双击都把截图放进剪贴板。
    //   复制 / 保存 / 取消由本类内部处理；「发送」「发送到对话框」经 sink 交回；「识别文字」关窗后经宿主服务交给 exe。
    //   返回：true=用户选了「发送」或「发送到对话框」（已回调）；false=其它（取消、完成、复制、保存、识别文字，
    //         或已有一次截图在进行而直接返回）。
    bool    Run(const flamingoshot::ScreenshotRequest& request, flamingoshot::IScreenshotSink* sink);

private:
    // 用户在操作栏选择的最终动作。
    enum Action
    {
        kActNone = 0,   // 尚未决定 / 取消
        kActSend,       // 发送（立即发图）
        kActStage,      // 发送到对话框（插入聊天输入框）
        kActCopy,       // 复制到剪贴板
        kActSave,       // 保存为文件
        kActOcr,        // 识别文字（关闭浮层后打开识别结果窗口）
    };

    LRESULT OnDuiNotify(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnKeyDown  (UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

    // 抓取整个虚拟桌面成冻结位图，记录虚拟桌面原点 / 尺寸。失败返回 false。
    bool    CaptureVirtualDesktop();
    // 枚举各显示器的矩形，换算成本窗口客户区坐标（减去虚拟桌面原点）。须在线程处于
    // Per-Monitor-V2 上下文时调用，得到的是物理像素。
    std::vector<RECT> EnumMonitorRectsInClient() const;
    // 当前鼠标位置（本窗口客户区坐标）。
    POINT   CursorInClient() const;
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

    flamingoshot::IScreenshotSink* m_sink;   // 结果接收方（借用，只在 Run 期间有效）；NULL 表示不绑定会话
    bool             m_hasSendTarget;   // 本次截图是否绑定会话（m_sink 不为 NULL）；Run 开始时确定
    HWND             m_ownerHwnd;  // 发起截图的窗口（可为 NULL）；「识别文字」的结果窗口居中摆在它上面。Run 开始时记下
    flamingoshot::ShotBgraImage m_ocrImage;  // 点「识别文字」瞬间从冻结位图取出的选区原始像素（不含标注）；关窗后交给识别
};

} // namespace balloonscreenshot
