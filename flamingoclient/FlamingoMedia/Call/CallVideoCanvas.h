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
 *  2026-10-06 补充两项能力（docs/音视频通话-界面美化方案.md 第 3.4 节，视频态操作条浮在画面上并自动隐藏）：
 *    - SetFloatingBottomLeft：小画面不再放在底部栏里，而是以 Fixed(0) 加入根容器、按父容器矩形浮在左下角，
 *      操作条淡出时小画面仍然可见；
 *    - SetCornerText：在画面右上角画一个半透明的圆角胶囊（视频态用来显示通话时长，操作条隐藏时也看得到）。
 *
 *  balloonwj@qq.com   2026-06-16（2026-07-31 增加可点击与帧交换）
 */
#pragma once

#include "../../../third-party/balloonui/balloonui/DuiControl.h"

#include <atlstr.h>   //CString（占位提示文字）
#include <vector>
#include <stdint.h>

namespace call {

//画面顶部提示条（SetBannerText）的高度（像素）与配色：琥珀色底、深棕色字，在浅灰的语音态画面与黑色的视频画面上都醒目
const int      kCanvasBannerH         = 28;
const COLORREF kCanvasBannerBgColor   = RGB(0xFF, 0xE8, 0xA8);
const COLORREF kCanvasBannerTextColor = RGB(0x6B, 0x45, 0x00);

//右上角胶囊（SetCornerText）的尺寸与配色（像素）：距画面上、右边缘的留白，高度，文字左右内边距，底色不透明度（0~255）
const int      kCanvasCornerMargin    = 12;
const int      kCanvasCornerH         = 24;
const int      kCanvasCornerPadX      = 10;
const int      kCanvasCornerBgAlpha   = 140;
const COLORREF kCanvasCornerTextColor = RGB(0xFF, 0xFF, 0xFF);
//浮在左下角的小画面（SetFloatingBottomLeft）四周 1 像素边框的颜色：小画面与对方画面都是黑底时也能看出边界
const COLORREF kCanvasFloatingBorderColor = RGB(0x80, 0x86, 0x90);

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

    /**
     *  语音态画面（第 10 期 10b）：打开后不画画面帧，改画浅灰底、正中主题蓝圆底白听筒的大图标，下面一行文字取
     *  SetPlaceholderText 设的内容（如「语音通话中」）。调用后须自行 Invalidate。
     *  @param on true 打开语音态画面；false 恢复画面帧 / 黑底占位
     */
    void SetVoiceArt(bool on);

    /**
     *  语音态画面里「语音通话中」下面那一行的文字（通话时长，第 11 期 11b）。调用后须自行 Invalidate。
     *  @param text 文字，如「02:51」；NULL 或空串表示不画
     */
    void SetVoiceSubText(LPCTSTR text);

    /**
     *  没有画面时显示的头像（docs/音视频通话优化方案.md 第 5 项）：语音态用它代替听筒图标；视频态还没收到画面、
     *  或画面已被清空（对方关了摄像头、本人关了摄像头）时，在黑底中央画它，占位提示写在它下面。
     *  调用后须自行 Invalidate。
     *  @param avatar 圆形头像位图（32 位预乘 alpha，借用不拥有，须在本控件销毁或改设之前一直有效）；NULL 表示不画头像
     */
    void SetAvatar(HBITMAP avatar);

    //当前设的头像位图（SetAvatar 设过的值），供单元测试核对
    HBITMAP GetAvatar() const
    {
        return m_avatar;
    }

    /**
     *  画面顶部的一条提示（2026-10-05 起，网络状况标识第 1 期）：琥珀色底、深色字，叠画在语音态、视频画面与占位之上，
     *  目前用于 Debug 版无设备模式下的「调试模式：未使用麦克风和扬声器」。调用后须自行 Invalidate。
     *  @param text 提示文字；NULL 或空串表示不画
     */
    void SetBannerText(LPCTSTR text);

    /**
     *  画面右上角的胶囊文字（2026-10-06 起）：半透明黑底、白字、两端为半圆，视频态用来显示通话时长。顶部提示条
     *  显示时胶囊排在提示条下面。调用后须自行 Invalidate。
     *  @param text 文字；NULL 或空串表示不画
     */
    void SetCornerText(LPCTSTR text);

    //当前的胶囊文字（单元测试用）
    const CString& CornerText() const
    {
        return m_cornerText;
    }

    /**
     *  改为浮在父容器左下角（2026-10-06 起，视频态的小画面）：本控件以 Fixed(0) 加入父容器，Layout 时忽略父布局给的
     *  矩形，改按父容器的整个矩形定位到左下角。
     *  @param marginLeft   距父容器左边缘（像素）
     *  @param marginBottom 距父容器下边缘（像素）
     *  @param width        宽（像素），须大于 0
     *  @param height       高（像素），须大于 0
     */
    void SetFloatingBottomLeft(int marginLeft, int marginBottom, int width, int height);

    //浮动定位时按父容器矩形放到左下角，否则按父布局给的 rcAvail
    virtual void Layout(const RECT& rcAvail) override;

private:
    //自绘：先画主体（PaintBody），再在顶部叠画提示条（m_banner 非空时）
    virtual void OnPaint(HDC hdc, const RECT& rcDirty) override;

    //主体：有帧则等比缩放居中绘制（letterbox 黑边），无帧则填黑底 + 居中占位提示；语音态画语音态画面
    void PaintBody(HDC hdc, const RECT& rc);

    //顶部提示条：高 kCanvasBannerH、琥珀色底，文字单行居中，过长时末尾省略
    void DrawBanner(HDC hdc, const RECT& rc);

    //右上角胶囊：半透明黑底、白字，宽度随文字（GDI+ 画抗锯齿的圆角）
    void DrawCorner(HDC hdc, const RECT& rc);

    //语音态画面：浅灰底 + 圆底听筒图标 + 一行文字（布局见 call::ComputeVoiceArtLayout）
    void DrawVoiceArt(HDC hdc, const RECT& rc);

    //在黑底上居中绘制占位提示文字（m_placeholder 为空则什么都不画）；设了头像时头像在上、文字在下。
    void DrawPlaceholder(HDC hdc, const RECT& rc);

    //把头像位图按透明度缩放画进 rc（正方形）
    void DrawAvatar(HDC hdc, const RECT& rc);

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
    bool                 m_voiceArt;    //是否画语音态画面（见 SetVoiceArt）；打开时不画画面帧
    CString              m_voiceSubText;//语音态画面里文字下面那一行（通话时长）；空串表示不画
    HBITMAP              m_avatar;      //没有画面时显示的头像（借用，见 SetAvatar）；NULL 表示不画
    CString              m_banner;      //画面顶部提示条的文字（见 SetBannerText）；空串表示不画
    CString              m_cornerText;  //右上角胶囊的文字（见 SetCornerText）；空串表示不画
    bool                 m_floating;    //是否浮在父容器左下角（见 SetFloatingBottomLeft）
    int                  m_floatLeft;   //浮动时距父容器左边缘（像素）
    int                  m_floatBottom; //浮动时距父容器下边缘（像素）
    int                  m_floatW;      //浮动时的宽（像素）
    int                  m_floatH;      //浮动时的高（像素）
};

} // namespace call
