/**
 *  聊天窗输入框控件 ChatInputRichEdit —— 无窗口富文本控件的聊天定制子类。
 *
 *  它承接的是原先挂在输入框子窗口上的那几段子类过程（那套实现位于已删除的
 *  ChatInputKeyFilter.h / .cpp，可在 2026-08-16 之前的提交里查阅）：回车发送 /
 *  换行、「正在输入」通知、焦点区域上报、以及群聊的「@提醒」键盘与滚轮转发。
 *  输入框改用无窗口富文本控件之后没有子窗口可挂，这些判断改由控件自己的按键、
 *  字符、焦点、滚轮处理函数完成。
 *
 *  滚动条与消息区统一（2026-10-01）：关掉基类自带的覆盖式滚动条（8 像素实色、淡入淡出），
 *  改画与消息区、群成员侧栏同一种细滚动条（ChatScrollThumb.h：5 像素半透明圆角滑块），内容
 *  超出时一直显示，可拖动、可点轨道跳转；右侧内边距相应放宽，文字不会被滑块盖住。
 *
 *  **与聊天窗之间的消息协议原样保留** —— 发什么消息、什么时机发、同步还是异步、
 *  返回值怎么解释，全部与子类过程版本一致。因此聊天窗那一侧接消息的代码不需要
 *  任何改动，这次替换只换实现机制、不换契约。
 *
 *  典型用法（聊天窗构建输入区时）：
 *      std::unique_ptr<ChatInputRichEdit> edit(new ChatInputRichEdit());
 *      edit->SetMultiLine(true);
 *      edit->SetWordWrap(true);
 *      edit->SetNotifyTarget(m_hWnd);
 *      edit->SetSendOnEnterMsg(kSendOnEnterMsg);
 *      edit->SetTypingChangedMsg(kInputChangedMsg);
 *      // 群聊还要：
 *      edit->SetMentionMsgs(kMentionChangedMsg, kMentionNavMsg, kMentionWheelMsg);
 *
 *  balloonwj@qq.com   2026-08-16
 */
#pragma once

#include "../../../third-party/balloonui/balloonui/Controls/Input/DuiRichEdit.h"
#include "ChatClipboardContent.h"   // ChatClipSegment（粘贴 / 拖入的聊天内容片段）
#include "ChatScrollFade.h"         // 细滚动条滑块的淡入淡出（成员需完整类型）

#include <string>
#include <vector>

namespace balloonchatwnd {

/**
 *  聊天输入框控件。除下列定制外，行为与基类完全一致。
 *
 *  几个消息号都默认为 0，表示「不启用该项」—— 单聊窗不设 @ 提醒那三个即可，
 *  不必为此再分出一个子类。
 */
class ChatInputRichEdit : public balloonwjui::DuiRichEdit
{
public:
    ChatInputRichEdit();

    /**
     *  设置接收各类通知的宿主窗口。
     *    hWnd：聊天窗句柄；所有权不转移，调用方须保证其生命周期覆盖本控件。
     *          传 NULL 表示不再通知任何窗口（本控件的其余行为不受影响）。
     */
    void SetNotifyTarget(HWND hWnd);

    /**
     *  设置「按回车发送」时投递给宿主的消息号。
     *    nMsg：自定义消息号；0 表示不启用发送（回车一律当换行处理）。
     *
     *  这次回车究竟是发送还是换行，由 SetSendOnEnterQuery 注册的回调判断；
     *  没注册时按最常见的约定处理（不按 Ctrl 即发送）。
     */
    void SetSendOnEnterMsg(UINT nMsg);

    /**
     *  「这次回车是发送还是换行」的查询回调原型。
     *    bCtrlDown：按下回车时 Ctrl 是否也按着。
     *    pCtx：     注册时传入的上下文，原样透传。
     *    返回：     true 表示发送，false 表示插入换行。
     */
    typedef bool (*SendOnEnterQueryFn)(bool bCtrlDown, void* pCtx);

    /**
     *  注册「回车含义」的查询回调。
     *
     *  之所以由调用方注入、而不是控件自己去读全局的发送模式设置：那个设置存在
     *  配置文件里，读它要牵进配置文件读写与主程序目录这一整条依赖链，而本控件
     *  会被编进单元测试工程 —— 那边刻意不引入这些。把判断挪到调用方之后，控件
     *  只剩纯粹的键盘处理，测试工程也不必为此链接一堆无关模块。
     *
     *    fn：  查询回调；传 NULL 表示恢复默认（不按 Ctrl 即发送）。
     *    pCtx：透传给回调的上下文，可为 NULL。
     */
    void SetSendOnEnterQuery(SendOnEnterQueryFn fn, void* pCtx);

    /**
     *  设置「正在输入」通知的消息号。
     *    nMsg：自定义消息号；0 表示不启用。
     *
     *  投递（异步）给宿主，wParam 为 1 表示输入框失焦（宿主应立即收尾），
     *  为 0 表示普通的内容变化。本控件只报「可能变了」，什么时候真发包由
     *  宿主的节流器决定。
     */
    void SetTypingChangedMsg(UINT nMsg);

    /**
     *  设置群聊「@提醒」用的三个消息号。单聊窗不必调用。
     *    nChangedMsg：内容可能变化时**投递**给宿主，宿主自行重算 @ 前缀。
     *    nNavMsg：    方向键 / 回车 / Tab / Esc 时**同步发送**给宿主，
     *                 wParam 为虚拟键码。宿主返回非 0 表示这次按键归它
     *                 （浮层正显示、用来移动高亮或提交选中），本控件随即
     *                 吞掉该键。用同步发送是因为「吞不吞」必须当场决定。
     *    nWheelMsg：  滚轮时**同步发送**给宿主，wParam 为滚轮增量。宿主返回
     *                 非 0 表示滚轮归浮层，本控件不滚自己。
     *                 滚轮要拐这一道的原因：@ 浮层从不取焦点，滚轮消息全都
     *                 落在输入框上，不转发的话候选列表根本滚不动。
     *  三者任一为 0 表示该项不启用。
     */
    void SetMentionMsgs(UINT nChangedMsg, UINT nNavMsg, UINT nWheelMsg);

    /**
     *  设置粘贴识别用的消息号（docs/代码片段方案.md 第 3.1 节），**投递**给宿主：
     *    · 往空输入框里做了一次普通粘贴（剪贴板里不是 Flamingo 聊天内容）时，粘贴完成之后投递一次，wParam 为 1；
     *    · 内容有变化时投递，wParam 为 0（宿主据此在用户继续输入时收起提示条）。
     *  「空」指什么都没有或只有空白字符。粘贴完成之后才投递，宿主收到时读到的是粘贴后的内容。
     *    nMsg：消息号；0 表示不启用。
     */
    void SetPasteTextMsg(UINT nMsg);

    /**
     *  「把一张图片登记为待发送、并插入输入框当前光标处」的回调原型。粘贴或拖入的聊天
     *  内容里有图片时调用：图片要连同源文件一起登记，发送时才能取回原图，而待发送表归聊天
     *  窗管（见聊天窗的 StageImageInInput），故由调用方注入。
     *    pCtx：注册时传入的上下文，原样透传。
     *    path：图片本地文件完整路径。
     *    返回：插入成功返回 true。
     */
    typedef bool (*StageImageFn)(void* pCtx, const std::wstring& path);

    /**
     *  注册插入图片用的回调。
     *    fn：  回调；传 NULL 表示不支持插入图片（聊天内容里的图片以"[图片]"文字占位）。
     *    pCtx：透传给回调的上下文，可为 NULL；不持有所有权。
     */
    void SetImageStager(StageImageFn fn, void* pCtx);

    /**
     *  按顺序把聊天内容片段插入当前光标处（有选区时先替换选区）。粘贴与拖入 Flamingo 聊天
     *  内容时调用。
     *    · 文字：原样插入；
     *    · 表情：以带编号标记的内联图插入，与从表情面板选的表情完全一样，发送时还原成表情；
     *    · 图片：交给 SetImageStager 注册的回调登记并插入。
     *  表情图片或图片文件找不到、没有注册图片回调时，插入"[表情]" / "[图片]"文字占位，不丢内容。
     *    segs：片段序列。
     *  副作用：修改输入框内容（可撤销）；可能经回调向聊天窗登记待发送图片。
     */
    void InsertClipSegments(const std::vector<ChatClipSegment>& segs);

    /**
     *  当前细滚动条滑块的矩形（宿主客户区坐标）。内容没有超出可视区、或排版引擎不可用时
     *  返回空矩形（全 0）。几何与消息区相同，见 ComputeChatScrollThumb。无副作用。
     */
    RECT ScrollThumbRect() const;

    // 是否正按住细滚动条的滑块拖动（在滚动条命中带里按下左键之后、松开之前为 true）。
    bool IsDraggingScrollThumb() const { return m_thumbDragging; }

    // 绘制时向排版引擎查询滚动状态的累计次数（仅供测试核对"没有变化时绘制不再查询"）。
    unsigned long PaintScrollQueryCount() const { return m_thumbQueryCount; }

    // ---- 基类覆写 ----
    bool OnKeyDown(UINT vk, UINT flags) override;
    bool OnChar(TCHAR ch) override;
    bool OnSetFocus() override;
    bool OnKillFocus() override;
    // 落在细滚动条命中带里时开始拖动滑块（点在轨道空白处时滑块中心先跳到点击处），
    // 否则照常交给基类（定位光标、开始选择文字）。
    bool OnLButtonDown(POINT pt, UINT mkFlags) override;
    // 拖动滑块时按指针位置滚动；否则报告鼠标是否在滚动条命中带上（悬停时滑块淡入并保持可见），再交给基类。
    bool OnMouseMove(POINT pt, UINT mkFlags) override;
    // 鼠标离开本控件（2026-10-04 起）：滑块开始淡出，再交给基类。返回值同基类。
    bool OnMouseLeave() override;
    // 结束拖动滑块，否则交给基类。
    bool OnLButtonUp(POINT pt, UINT mkFlags) override;
    bool OnMouseWheel(POINT pt, short zDelta, UINT mkFlags) override;
    bool OnRawMessage(UINT uMsg, WPARAM wParam, LPARAM lParam,
                      LRESULT& lResult) override;
    // 先画基类内容，内容超出可视区时再在右侧画细滚动条滑块。滑块矩形取自缓存，只在缓存
    // 标脏时向引擎查询一次（见 RefreshThumbCache）；滑块位置变了时补请求一次重画。
    void OnPaint(HDC hdc, const RECT& rcDirty) override;
    // 记下上内边距（细滚动条的纵向范围从文字区上沿算起），再交给基类。
    void SetMargins(int left, int top, int right, int bottom) override;
    HRESULT TxSiteNotify(DWORD iNotify, void* pv) override;
    // 滚动状态变化（打字换行、滚轮、拖动）时把滑块缓存标脏并请求重画，滑块的位置与长度随之更新。
    void TxSiteScrollInfoChanged(bool bVertical) override;

protected:
    // 右键菜单「粘贴」：剪贴板里有 Flamingo 聊天内容时按片段插入（与 Ctrl+V 同口径），
    // 其余命令交给基类。
    bool OnContextMenuCommand(UINT nId) override;

private:
    //剪贴板里有 Flamingo 聊天内容时按片段粘贴并返回 true；没有、只读或读取失败时返回
    //false，调用方继续走原来的粘贴。
    bool TryPasteChatContent();

    //剪贴板里没有文字、只有图片时（截图「完成」、系统截图工具），把图片存成临时 PNG，交给 SetImageStager 注册的回调
    //按截图「发送到对话框」的路径插入，并返回 true。RichEdit 原生粘贴进来的位图发送时只读出一个空格，消息发不出去
    //（bugs.md BUG-106，docs/截图独立模块方案.md 第 7 节）。剪贴板里不是只有图片、没有注册插图回调或输入框只读时
    //返回 false，调用方继续走原来的粘贴；只有图片却没能存成 PNG 时也返回 true（不交给 RichEdit），日志里记一条警告。
    bool TryPasteClipboardImage();

    //告诉宿主「用户现在在发送框操作」。宿主据此决定 Ctrl+C 归谁。
    void NotifyFocusInput();

    //输入框里是否什么都没有或只有空白字符（粘贴识别用）。
    bool IsInputBlank() const;

    //告诉宿主「刚往空输入框里粘贴了文字」（粘贴识别，wParam 为 1）。没有设置消息号时什么都不做。
    void NotifyPastedIntoBlank();

    //投递一次「正在输入」通知。
    //  bKillFocus：true 表示这次是失焦引发的，宿主应立即收尾。
    void NotifyTyping(bool bKillFocus);

    //宿主窗口是否可用。
    bool HasTarget() const;

    //算细滚动条用到的量：可视区矩形（横向为整个控件、纵向为文字区）、可视高度、内容总高、
    //当前滚动位置（像素）。排版引擎不可用或可视高度不大于 0 时返回 false。
    bool ThumbGeometry(RECT& view, int& viewH, int& contentH, int& scrollY) const;

    //把内容滚动到"滑块上沿位于 thumbTop"对应的位置，并请求重画。
    //  thumbTop：滑块上沿的目标纵坐标（宿主客户区坐标），超出轨道时按轨道两端算。
    void ScrollToThumbTop(int thumbTop);

    //缓存标脏或控件矩形变了时，向引擎查询一次滚动状态、更新缓存的滑块矩形；否则什么都不做。
    //  返回：本次确实查询过时返回 true。
    //不能每次绘制都查询：内容放得下时，查询会让基类再做一次内容测量，测量又让引擎请求重画，
    //每次绘制都查就成了"绘制 → 测量 → 重画"的死循环，消息队列里永远有待处理的 WM_PAINT，
    //计时器消息排不上，聊天窗消息区的平滑滚动因此停摆（2026-10-01 实测，表现为滚轮失效）
    bool RefreshThumbCache();

    HWND m_hNotifyTarget;      // 宿主聊天窗；不持有所有权
    UINT m_nSendOnEnterMsg;    // 按回车发送时投递的消息号；0 = 不启用
    UINT m_nTypingChangedMsg;  // 「正在输入」通知的消息号；0 = 不启用
    UINT m_nMentionChangedMsg; // @提醒：内容变化，投递；0 = 不启用
    UINT m_nMentionNavMsg;     // @提醒：导航键，同步发送；0 = 不启用
    UINT m_nMentionWheelMsg;   // @提醒：滚轮，同步发送；0 = 不启用
    UINT m_nPasteTextMsg;      // 粘贴识别（往空输入框粘贴之后、内容变化时），投递；0 = 不启用

    //是否正处于输入法合成期。合成期内的回车是「候选词上屏」，不能当成发送。
    bool m_bImeComposing;

    SendOnEnterQueryFn m_pfnSendQuery;   // 回车含义的查询回调；NULL = 用默认判断
    void*              m_pSendQueryCtx;  // 上述回调的上下文；不持有所有权

    StageImageFn       m_pfnStageImage;  // 插入图片的回调；NULL = 图片以"[图片]"文字占位
    void*              m_pStageImageCtx; // 上述回调的上下文；不持有所有权

    int  m_marginTop;          // 当前上内边距（像素），由 SetMargins 记下；细滚动条的纵向范围从文字区上沿算起
    bool m_thumbDragging;      // 是否正按住细滚动条的滑块拖动
    int  m_thumbDragOffsetY;   // 拖动时按下点相对滑块上沿的偏移（像素），拖动中保持不变，滑块不跳

    bool m_thumbCacheDirty;    // 滑块缓存是否需要重新查询：滚动状态或内容变化时置位，查询后清除
    bool m_thumbQuerying;      // 正在向引擎查询滚动状态；期间引擎若回调"滚动状态变化"则忽略，免得查询本身把缓存标脏
    RECT m_thumbCacheRect;     // 缓存的滑块矩形（宿主客户区坐标）；空矩形表示内容放得下、没有滑块
    RECT m_thumbCacheItem;     // 查询时的控件矩形；控件矩形变了（缩放、重排）也要重新查询
    unsigned long m_thumbQueryCount;   // 绘制时向引擎查询滚动状态的累计次数，仅供测试
    CChatScrollFade m_scrollFade;      // 细滚动条滑块的淡入淡出（2026-10-04 起，与 balloonui 滚动条同一时序），随本控件销毁
};

} // namespace balloonchatwnd
