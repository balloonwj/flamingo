/**
 *  通话视频画面控件（自绘 DuiControl），CallVideoCanvas.h
 *
 *  第三期视频通话窗的画面显示面：一个 balloonui 自绘控件，持有「最近一帧 BGRA 图像」，
 *  OnPaint 时按控件矩形等比缩放（letterbox，留黑边不拉伸变形）把该帧画出来。远端对方画面
 *  用一个大的本控件铺满承载；本端摄像头预览用一个小的本控件（画中画 PiP）承载。
 *
 *  做法照搬已被接受的远程桌面观看窗 RdViewerCanvas（DuiControl 子类 + OnPaint 里 StretchDIBits
 *  缩放绘制解码帧）；区别仅在于本控件自持帧缓冲、且做等比 letterbox。
 *
 *  线程约定：SetFrame 与 OnPaint **都只在 UI 线程调用**——视频解码 / 采集线程拿到帧后须先
 *  PostMessage 把帧转投到 UI 线程，再在 UI 线程消息处理里调 SetFrame + Invalidate。故内部帧
 *  缓冲无需加锁（与 RdViewerCanvas 同：apply / draw 均在 UI 线程）。
 *
 *  本控件只做「拿 BGRA 缓冲画到 HDC」，不碰设备 / 网络 / 编解码，便于在通话窗与调试入口里复用。
 *  其依赖的 NV12→BGRA 转换是 VideoFrameConvert（已有单测）。
 *
 *  2026-07-31 补充「可点击」能力：视频通话窗要支持把自己 / 对方对调成大画面显示，故本控件可
 *  设成可交互（SetInteractive）—— 此时鼠标移上去是手型光标、单击冒 DUIN_CLICK、双击冒
 *  DUIN_DBLCLK 给宿主窗口，由宿主决定怎么对调；对调时用 SwapFrame 直接交换两块画面已持有的
 *  帧，画面即时互换、不必等下一帧到来。
 *
 *  balloonwj@qq.com   2026-06-16（2026-07-31 增加可点击与帧交换）
 */
#pragma once

#include "../../balloonui/balloonui/DuiControl.h"

#include <atlstr.h>   //CString（占位提示文字）
#include <vector>
#include <stdint.h>

namespace call {

//通话视频画面控件：持最近一帧 BGRA，OnPaint 等比缩放绘制；无帧时填黑底。
class CallVideoCanvas : public balloonwjui::DuiControl
{
public:
    CallVideoCanvas();
    ~CallVideoCanvas();

    /**
     *  设置 / 更新要显示的画面帧（**仅 UI 线程调用**）。内部拷贝一份；调用后须自行 Invalidate
     *  触发重绘。传 BGRA、自上而下、每像素 4 字节、行紧排（stride = w*4）。
     *  @param bgra BGRA 像素起始（w*h*4 字节，自上而下）
     *  @param w    画面宽（像素，>0）
     *  @param h    画面高（像素，>0）
     */
    void SetFrame(const uint8_t* bgra, int w, int h);

    /**
     *  与另一块画面控件交换各自持有的帧（**仅 UI 线程调用**）。用于"把自己 / 对方对调成大画面"：
     *  对调瞬间两边画面立刻互换，不必等各自的下一帧到来（对端无画面时也不会出现残留的旧画面）。
     *  交换后调用方须对两块控件各调一次 Invalidate 触发重绘。
     *  @param other 另一块画面控件（一般是大画面 ←→ 小画面）
     */
    void SwapFrame(CallVideoCanvas& other);

    //是否已收到至少一帧（用于上层判断是否还在等首帧）。
    bool HasFrame() const
    {
        return m_frameW > 0 && m_frameH > 0;
    }

    /**
     *  设置本控件是否可交互。可交互时：鼠标移上去显示手型光标，单击冒 DUIN_CLICK、双击冒
     *  DUIN_DBLCLK（均带本控件 SetCtrlId 设的 ctrlId）给宿主窗口。默认不可交互（纯显示）。
     *  注意：双击通知依赖宿主 DuiHost::EnableDoubleClick(true)（窗口类未带 CS_DBLCLKS）。
     *  @param interactive true=可点击；false=纯显示，不响应鼠标
     */
    void SetInteractive(bool interactive);
    bool IsInteractive() const
    {
        return m_interactive;
    }

    /**
     *  给本控件挂一段鼠标悬停提示（如"点击切换大 / 小画面"）。内部走 DuiToolTipMgr 注册，
     *  控件析构时自动取消注册，调用方无须自己 Unregister。
     *  @param text 提示文字；传 NULL 或空串表示清除已挂的提示
     */
    void SetTooltipText(LPCTSTR text);

    /**
     *  设置"尚无画面"时画在黑底中央的占位提示（如"正在打开摄像头…""摄像头不可用"）。一旦收到
     *  画面帧就只画画面、不再画提示；帧被清空（SetFrame(NULL,0,0)）后提示重新露出。
     *  支持用 '\n' 分行。调用后须自行 Invalidate。
     *  @param text 提示文字；传 NULL 或空串表示不画提示（纯黑底）
     */
    void SetPlaceholderText(LPCTSTR text);

private:
    //自绘：有帧则等比缩放居中绘制（letterbox 黑边），无帧则填黑底 + 居中占位提示。
    virtual void OnPaint(HDC hdc, const RECT& rcDirty) override;

    //在黑底上居中绘制占位提示文字（m_placeholder 为空则什么都不画）。
    void DrawPlaceholder(HDC hdc, const RECT& rc);

    //以下鼠标处理仅在 m_interactive 为 true 时生效，否则原样交回基类（不消费事件）。
    virtual bool OnLButtonDown(POINT pt, UINT mkFlags) override;
    virtual bool OnLButtonUp(POINT pt, UINT mkFlags) override;
    virtual bool OnLButtonDblClk(POINT pt, UINT mkFlags) override;
    virtual bool OnSetCursor(POINT pt) override;

private:
    std::vector<uint8_t> m_frame;       //最近一帧 BGRA（自上而下，w*h*4 字节）；仅 UI 线程访问
    int                  m_frameW;      //帧宽（像素）；0 表示尚无帧
    int                  m_frameH;      //帧高（像素）；0 表示尚无帧
    bool                 m_interactive; //是否可点击（见 SetInteractive）；false 时不响应鼠标
    bool                 m_pressed;     //左键是否已在本控件内按下（按下→抬起都在控件内才算一次点击）
    bool                 m_tooltipSet;  //是否已向 DuiToolTipMgr 注册过提示（析构时据此决定要不要取消注册）
    CString              m_placeholder; //尚无画面时画在黑底中央的占位提示；空串表示不画
};

} // namespace call
