/**
 *  ChatScrollFade.h
 *
 *  聊天窗一族自绘细滚动条的淡入淡出控制器（2026-10-04 起）。时序与 balloonui DuiScrollBar 的自动隐藏完全一致：
 *  滚动或鼠标移进滚动条命中带时用 DuiScrollBar::kFadeInMs 淡入；最后一次操作之后 kIdleHideMs 内没有新的操作就用
 *  kFadeOutMs 淡出；鼠标悬停在命中带上、或正在拖动滑块时一直保持可见，离开 / 松开后重新计时。动画由 balloonui
 *  的 DuiAnimMgr 推进（它自带 16ms 定时器），宿主窗口不需要转发 WM_TIMER。
 *
 *  使用它的是自己画滑块、本身不是 DuiScrollView 的控件：聊天消息区（ChatMessageList）、群成员侧栏
 *  （GroupMemberList）、输入框（ChatInputRichEdit）。DuiScrollView 及其子类直接用库里的滚动条，不需要它。
 *
 *  典型用法（宿主控件里）：
 *      m_scrollFade.SetOwner(this);                                    //构造时
 *      m_scrollFade.OnScrolled();                                      //用户滚动之后（滚轮、键盘翻页）
 *      m_scrollFade.SetHover(HitChatScrollBand(pt, thumb, view));      //OnMouseMove
 *      m_scrollFade.OnOwnerMouseLeave();                               //OnMouseLeave
 *      m_scrollFade.SetDragging(true);  / SetDragging(false);          //开始 / 结束拖动滑块
 *      PaintChatScrollThumb(g, thumb, m_scrollFade.Alpha());           //OnPaint
 *
 *  本文件遵循 balloonui 头文件约定：包含它之前调用方须已引入 ATL/WTL。
 *
 *  balloonwj@qq.com   2026-10-04
 */
#pragma once

#include <memory>

namespace balloonwjui { class DuiControl; }

namespace balloonchatwnd {

/**
 *  一条自绘细滚动条的淡入淡出状态。宿主控件持有一个成员，在滚动、悬停、拖动时通知它，绘制时取 Alpha()。
 *  不可拷贝；只在界面线程上使用。
 */
class CChatScrollFade
{
public:
    CChatScrollFade();

    // 析构：作废还在动画管理器里排着的回调，此后它们触发时什么也不做
    ~CChatScrollFade();

    CChatScrollFade(const CChatScrollFade&) = delete;
    CChatScrollFade& operator=(const CChatScrollFade&) = delete;

    /**
     *  设置宿主控件：不透明度变化时调用它的 Invalidate 重画。
     *    owner：宿主控件，借用，须比本对象活得长（通常本对象就是它的成员）；NULL 表示不通知。
     */
    void SetOwner(balloonwjui::DuiControl* owner);

    // 用户滚动了（滚轮、键盘翻页）：淡入，并重新开始空闲计时
    void OnScrolled();

    /**
     *  鼠标是否在滚动条命中带上。进入时淡入并保持可见；离开时重新开始空闲计时，到期淡出。
     *  状态没变时什么也不做。
     *    hover：true 表示在命中带上。
     */
    void SetHover(bool hover);

    /**
     *  是否正在按住滑块拖动。开始拖动时淡入并保持可见；结束时重新开始空闲计时。
     *    dragging：true 表示开始拖动，false 表示结束。
     */
    void SetDragging(bool dragging);

    // 鼠标离开了宿主控件：不再悬停在命中带上，立即开始淡出（正在拖动时什么也不做）
    void OnOwnerMouseLeave();

    // 当前不透明度：0 完全看不见，1 完全显示（再乘以滑块自身的半透明度）
    float Alpha() const { return m_alpha; }

    // 是否悬停在命中带上（供单元测试断言）
    bool IsHover() const { return m_hover; }

    // 是否正在拖动滑块（供单元测试断言）
    bool IsDragging() const { return m_dragging; }

private:
    // 动画回调持有的令牌：作废旧令牌即可让排着的旧回调失效，动画管理器没有按对象取消的接口
    struct Token
    {
        CChatScrollFade* m_owner;   // 令牌所属的控制器
        bool             m_alive;   // 是否仍有效；作废后回调什么也不做

        Token() : m_owner(NULL), m_alive(false) {}
    };

    // 淡入淡出动画每一帧的回调：把不透明度设为当前值并请宿主重画
    struct FadeSetter
    {
        std::shared_ptr<Token> m_token;   // 发起动画时的令牌
        void operator()(double value) const;
    };

    // 淡入淡出动画结束的回调：把不透明度落到目标值
    struct FadeDone
    {
        std::shared_ptr<Token> m_token;   // 发起动画时的令牌
        float                  m_to;      // 目标不透明度
        void operator()() const;
    };

    // 空闲计时动画每一帧的回调：什么也不做，只占用时间
    struct IdleSetter
    {
        void operator()(double value) const;
    };

    // 空闲计时结束的回调：仍悬停或拖动时再等一轮，否则开始淡出
    struct IdleDone
    {
        std::shared_ptr<Token> m_token;   // 发起计时时的令牌
        int                    m_delayMs; // 再等一轮时的时长（毫秒）
        void operator()() const;
    };

    // 淡入到 1 并重新开始空闲计时
    void Show();

    // 开始淡出到 0；正在拖动时什么也不做
    void StartFadeOut();

    // 作废当前令牌（排着的回调随之失效），换一个新令牌
    void CancelAnims();

    // 发起一段不透明度动画；当前值已是目标时什么也不做
    void StartFade(float to, int durationMs);

    // 发起一段空闲计时
    void StartIdle(int delayMs);

    // 请宿主控件重画
    void InvalidateOwner();

    balloonwjui::DuiControl* m_owner;     // 宿主控件，借用；NULL 时不通知重画
    float                    m_alpha;     // 当前不透明度 0～1；新建时为 0（看不见）
    bool                     m_hover;     // 鼠标是否悬停在命中带上
    bool                     m_dragging;  // 是否正在拖动滑块
    std::shared_ptr<Token>   m_token;     // 当前有效的令牌，CancelAnims 时换新
};

} // namespace balloonchatwnd
