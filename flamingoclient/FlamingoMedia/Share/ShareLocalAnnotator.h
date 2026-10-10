/**
 *  本机标注，ShareLocalAnnotator.h
 *
 *  只给本人用、不经网络的标注（第 21 期，docs/屏幕共享与录制方案.md 第 2 节第 39、41 条）：独立录屏的「画笔」与录制用的白板都用它。
 *  它持有一个没有发送通道的标注会话（本人坐发起人的座位，始终能画、能清空）、一条本机模式的标注工具条（工具与屏幕共享相同，
 *  多一个「清空」、没有权限按钮）、一个标注文字输入框，并实现标注输入接口：
 *    - 工具条的命令、文字输入框的结果经本类的消息窗口处理，规则与屏幕共享控制器相同（选择工具下样式改到选中对象、双击改文字等）；
 *    - 每 50 毫秒推进一次激光笔的淡出；
 *    - 笔迹或选择有变化时回调 OnLocalAnnotationsChanged，调用方据此重画显示层、交给录制合成；
 *    - 工具条上点了「退出标注」时回调 OnLocalAnnotatorExit。
 *  所有方法都在 UI 线程上调用。
 *
 *  典型用法（录屏控制器里）：
 *      m_pen.Start(this, 1.0);
 *      m_penOverlay.SetInputMode(true, &m_pen);
 *      m_pen.ShowBar(recordedRect);
 *
 *  balloonwj@qq.com   2026-10-05
 */
#pragma once

#include <stdint.h>
#include "ShareAnnotationBar.h"
#include "ShareAnnotationMenus.h"
#include "ShareAnnotationOverlay.h"
#include "ShareAnnotationSession.h"
#include "ShareMessages.h"
#include "ShareTextInputDialog.h"

namespace screenshare {

//本机标注的回调（UI 线程）
class IShareLocalAnnotatorListener
{
public:
    virtual ~IShareLocalAnnotatorListener()
    {
    }

    //笔迹或选择提示有变化：重画显示层、把笔迹交给录制
    virtual void OnLocalAnnotationsChanged() = 0;

    //工具条上点了「退出标注」
    virtual void OnLocalAnnotatorExit() = 0;

    /**
     *  进入或退出「鼠标」状态（2026-10-07，工具条最左边的按钮）：录屏画笔据此撤掉或盖回输入层；白板不需要处理
     *  （「鼠标」状态下本类不处理白板画布交来的鼠标动作）。
     *  @param on true 进入「鼠标」状态
     */
    virtual void OnLocalAnnotatorPointerMode(bool on)
    {
        (void)on;
    }
};

//本机标注：会话、工具条、文字输入框与命令处理；消息窗口只收消息
class CShareLocalAnnotator : public CWindowImpl<CShareLocalAnnotator, CWindow, CWinTraits<0, 0> >,
                             public IShareAnnotationInput
{
public:
    CShareLocalAnnotator();
    ~CShareLocalAnnotator();

    /**
     *  开始：建消息窗口、开始一个没有发送通道的会话，沿用上次的工具条状态。
     *  @param listener   回调，不拥有，须在 Stop 之前一直有效
     *  @param widthScale 线宽与字号的缩放（白板用，见 ShareAnnotationSession::SetWidthScale）；1 为不缩放
     *  @return 消息窗口建好返回 true
     */
    bool Start(IShareLocalAnnotatorListener* listener, double widthScale);

    //结束：关工具条与输入框、清空笔迹、销毁消息窗口；可重复调用
    void Stop();

    //是否已开始
    bool IsRunning() const;

    /**
     *  显示工具条：在 anchor 下方居中，放不下时放进 anchor 底部（见 CShareAnnotationBar::Show）。
     *  @param anchor 参照矩形（屏幕坐标）：录屏为被录的内容，白板为白板窗口
     */
    void ShowBar(const RECT& anchor);

    //隐藏工具条（退出画笔模式时）；笔迹保留
    void HideBar();

    //按「允许截图」开关重新设置工具条能否被采集（2026-10-08）；工具条没显示时什么也不做
    void ReapplyBarCapture();

    //参照矩形变了（被录的窗口或白板窗口移动、改变大小）：工具条跟着挪
    void MoveBar(const RECT& anchor);

    //工具条是否显示
    bool IsBarVisible() const;

    //把工具条提到所有置顶窗口的最上面（输入层重新盖上来之后调用，免得工具条点不到）
    void BringBarToTop();

    //是否处于「鼠标」状态（2026-10-07）
    bool IsPointerMode() const;

    //会话（笔迹、选择、工具）
    ShareAnnotationSession& Session();
    const ShareAnnotationSession& Session() const;

    //IShareAnnotationInput：显示层的输入层或白板画布上的鼠标动作
    virtual void OnAnnotPointer(int phase, const AnnotationPoint& pt, int tolerance, double aspect, POINT screenPt) override;
    //IShareAnnotationInput：Delete 删除选中的对象，Esc 取消选择
    virtual bool OnAnnotKey(UINT vk) override;
    //IShareAnnotationInput：当前工具的光标，按工具与颜色缓存
    virtual HCURSOR AnnotCursor() override;

    BEGIN_MSG_MAP(CShareLocalAnnotator)
        MESSAGE_HANDLER(kMsgShareAnnotCommand, OnCommandMsg)
        MESSAGE_HANDLER(kMsgShareAnnotText, OnTextMsg)
        MESSAGE_HANDLER(WM_TIMER, OnTimer)
    END_MSG_MAP()

private:
    LRESULT OnCommandMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnTextMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnTimer(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    //通知调用方重画
    void changed();
    //进入或退出「鼠标」状态：同步工具条、通知调用方（2026-10-07）
    void setPointerMode(bool on);

    ShareAnnotationSession          m_session;       //本人的标注（没有发送通道）
    CShareAnnotationBar             m_bar;           //标注工具条（本机模式）
    CShareTextInputDialog           m_textDialog;    //标注文字输入框
    AnnotBarState                   m_state;         //工具条状态，命令先记在这里再整体交给会话
    IShareLocalAnnotatorListener*   m_listener;      //回调（不拥有）
    AnnotationPoint                 m_textPoint;     //新加文字的位置（归一化坐标）
    AnnotationRef                   m_editRef;       //文字输入框正在修改的对象；空引用表示新加文字
    HCURSOR                         m_cursor;        //当前工具的光标（本类建、析构或换工具时释放）
    int                             m_cursorTool;    //m_cursor 对应的工具
    uint32_t                        m_cursorColor;   //m_cursor 对应的颜色
    RECT                            m_anchor;        //工具条的参照矩形（屏幕坐标）
    bool                            m_barVisible;    //工具条是否显示
    bool                            m_pointerMode;   //是否处于「鼠标」状态（2026-10-07）：是时不处理鼠标动作，光标为系统箭头
    AnnotationPoint                 m_hoverPt;       //最近一次鼠标的位置（归一化坐标），选择工具据此决定光标
    int                             m_hoverTol;      //同上，那一次的命中容差
    bool                            m_hoverValid;    //m_hoverPt 是否有效（光标离开后为 false）
};

} // namespace screenshare
