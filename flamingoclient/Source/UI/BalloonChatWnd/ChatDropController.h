/**
 *  ChatDropController.h
 *
 *  聊天窗的「从资源管理器拖文件进来」接收器 —— 把 OLE 拖放接入这一摊窗口相关的
 *  杂事收口到一处，单聊窗与群聊窗共用，两边只需实现 IChatDropSink 两个回调。
 *
 *  典型用法（聊天窗）：
 *      // 窗口建好、客户区控件树装好之后
 *      m_dropCtrl.Attach(this, NULL, this);
 *      // 关窗时
 *      m_dropCtrl.Detach();
 *
 *  ------------------------------------------------------------------
 *  为什么第一个参数是宿主而不是窗口句柄
 *
 *  一个窗口只能注册一个拖放目标，而 balloonui 的无窗口控件是共用宿主窗口的：
 *  需要接收拖放的控件会让宿主替自己注册一个分发器（DuiHost::EnableDropDispatch），
 *  典型的就是富文本控件在第一次布局时那一下。它先注册，聊天窗这份就再也注册
 *  不上 —— 2026-08-17 实测栽过：日志里每开一次聊天窗报一条
 *  "窗口级 RegisterDragDrop 失败"，界面上则是拖任何文件进来都没有反应。
 *
 *  故 Attach 收宿主对象而不是光收一个 HWND：注册之前先调
 *  EnableDropDispatch(false) 把窗口让出来，聊天窗这份"整窗接收"才拿得到。
 *  聊天窗里没有别的控件需要单独接收拖放，让出来不损失任何功能。
 *  ------------------------------------------------------------------
 *
 *  ------------------------------------------------------------------
 *  「进入」与「逐帧改判」的分工
 *
 *  DragEnter 的返回值是一锤定音的：返回 false 表示<u>整次拖放</u>拒收，此后
 *  DragOver 不再逐帧询问、Drop 也直接忽略。而光标总是从窗口边缘进来的，发送框
 *  又在窗口下半部分，进入点几乎必然落在上方的消息区。
 *
 *  所以「进入」这一步只判数据格式（是不是文件），一律收下；「这个位置能不能放」
 *  完全由逐帧的 DragOver 决定，最终落点再由 Drop 兜一道底。把落点判定提到
 *  DragEnter 里去，症状就是拖什么进来都没反应 —— 2026-08-17 实测栽过一次。
 *  ------------------------------------------------------------------
 *
 *  ------------------------------------------------------------------
 *  为什么要单独处理输入框那个子窗口
 *
 *  聊天窗的输入框曾经是内嵌真子窗口的富文本控件（现已换成无窗口的），
 *  而 RichEdit 会把自己注册成 OLE 拖放目标（2026-08-11 实测：该子窗口上的
 *  "OleDropTargetInterface" 窗口属性非空）。OLE 投放时找的是光标底下那个窗口的
 *  拖放目标，于是拖到输入框上的文件会被 RichEdit 自己接走 —— 它会试着把文件当
 *  内嵌对象插进正文，而不是发出去。
 *
 *  故 Attach 时对输入框先 RevokeDragDrop 摘掉 RichEdit 那份，<u>再把我们自己的
 *  目标注册上去</u>。之所以不是"摘掉就完事、让 OLE 沿父链找到聊天窗那一份"：父链
 *  回退这件事我没能实测证实（2026-08-11 试过用 DoDragDrop 程序化投放，它的模态
 *  循环一直等不到输入、驱动不起来），与其押在一个没验证的行为上，不如让输入框
 *  区域直接有一份自己的目标 —— 两块区域各注册各的，父链回退成立与否都不影响。
 *
 *  代价是 RichEdit 自带的拖放能力一并失效：输入框内选中文字拖动、从浏览器拖一段
 *  文字进输入框，都不再可用。取舍的理由是"拖文件进来就发"在 IM 里是高频操作，
 *  而输入框内的文字拖动几乎没人用。
 *  ------------------------------------------------------------------
 *
 *  本文件遵循 balloonui 头文件约定：包含它之前调用方须已引入 ATL/WTL。
 *
 *  balloonwj@qq.com   2026-08-11
 */

#pragma once

#include "../../../third-party/balloonui/balloonui/DuiDropTarget.h"
#include "ChatClipboardContent.h"   // ChatClipSegment（从聊天记录拖进来的内容）
#include <string>
#include <vector>

namespace balloonwjui {
class DuiHost;   // Attach 只需要它的指针，具体定义在实现文件里引
}

namespace balloonchatwnd {

/**
 *  聊天窗要实现的拖放回调。
 */
class IChatDropSink
{
public:
    virtual ~IChatDropSink()
    {
    }

    /**
     *  落点是否落在「发送框」（输入区块）内。
     *
     *  拖放只在发送框接收 —— 落在上面的消息区（接收框）一律拒收，光标显示禁止符。
     *  这是与用户对齐过的交互：拖文件是"要发出去"，语义上属于发送框那一侧。
     *    ptClient：落点，<u>本窗口客户区坐标</u>。
     *  @return 在发送框范围内返回 true。
     */
    virtual bool IsChatDropZone(const POINT& ptClient) = 0;

    /**
     *  拖放提示该显示还是收起（发送框高亮 + 底部提示文字）。光标进入发送框时给
     *  true，离开发送框、拖离窗口或落手后给 false。
     *    visible：true 显示、false 收起。
     */
    virtual void OnChatDropHintChanged(bool visible) = 0;

    /**
     *  用户把文件放下了。回调在 UI 线程执行。
     *    paths：      拖进来的路径列表（可能含目录、失效路径，由调用方用
     *                 ChatDropModel 归类）。
     *    forceAsFile：落手瞬间是否按住了 Shift —— 按住表示图片也按原文件发。
     */
    virtual void OnChatFilesDropped(const std::vector<std::wstring>& paths,
                                    bool forceAsFile) = 0;

    /**
     *  从聊天记录里拖出来的内容（带 Flamingo 聊天内容格式的文字、表情、图片）被放进了
     *  发送框。这种内容插入输入框当前光标处、<u>不直接发送</u>，与粘贴一致（2026-09-30）。
     *  回调在 UI 线程执行。
     *    segs：还原出的聊天内容片段，非空。
     */
    virtual void OnChatContentDropped(const std::vector<ChatClipSegment>& segs) = 0;
};

/**
 *  ChatDropController —— 聊天窗拖放接收器。
 *
 *  一个聊天窗持有一个实例（值成员即可）。它自己不碰业务，只负责把 OLE 那一侧的
 *  事件整理成"提示浮层开 / 关"与"这些路径被放下了"两件事交给 sink。
 */
class ChatDropController
{
public:
    ChatDropController();
    ~ChatDropController();

    /**
     *  绑定到聊天窗。窗口建好、客户区控件树装好之后调用；重复调用会先解绑再绑。
     *    host：     聊天窗宿主对象（聊天窗自身即可，DuiFrameWindow 派生自 DuiHost），
     *               拖放目标注册在它的 HWND 上；<u>本类只引用不持有</u>。
     *    inputHwnd：输入框内嵌的 RICHEDIT50W 子 HWND，会先摘掉 RichEdit 自带的
     *               拖放目标再换上我们的；输入框已换成无窗口控件、没有子窗口时
     *               传 NULL（见文件头说明）。
     *    sink：     回调接收方；<u>本类只引用不持有</u>，其生命期须长于本类。
     *  @return 注册成功返回 true；host 为空 / 窗口未创建、sink 为空或
     *          RegisterDragDrop 失败时返回 false。
     *  副作用：注册之前会调 host->EnableDropDispatch(false)，把窗口上可能已被
     *          balloonui 控件占用的拖放目标让出来（理由见文件头）。
     */
    bool Attach(balloonwjui::DuiHost* host, HWND inputHwnd, IChatDropSink* sink);

    /**
     *  解绑。关窗时调用；未绑定时为空操作。析构函数也会调它。
     */
    void Detach();

    // 当前是否已绑定。
    bool IsAttached() const
    {
        return m_attached;
    }

    /**
     *  取注册在聊天窗上的那份 IDropTarget 实现。
     *
     *  <u>给测试用</u>。拖放最容易出问题的地方是「进入 → 逐帧改判 → 落手」这三步
     *  的衔接：任一步判错，表现都只是"拖进去没反应"，不报错也不崩溃，只能靠人
     *  手动拖才发现。把这个接口露出来，测试才能把三步自己走一遍钉住行为。
     *  业务代码接入拖放只需 Attach + 实现 IChatDropSink，不必碰它。
     *  @return 未绑定时返回 NULL；<u>所有权仍属本类</u>，调用方不得 Release，
     *          也不要在 Detach 之后继续使用。
     */
    IDropTarget* GetFrameDropTarget();

    /**
     *  落手瞬间是否按住了 Shift（图片改按原文件发）。
     *
     *  用 GetAsyncKeyState 读物理按键状态而不是查线程消息队列：OLE 投放是由拖放
     *  源那一侧跨进程调进来的，本线程的消息队列里未必已经处理过按键消息。
     *  @return 按住 Shift 返回 true。
     */
    static bool IsForceAsFileHeld();

private:
    //以下四个是装给 DuiDropTargetHelper 的回调实现。fromInput 表示事件来自<u>输入框
    //那份目标</u>（此时坐标是输入框客户区的、且落点必然就在发送框内），false 表示来自
    //窗口那份（坐标是窗口客户区的，需要问宿主窗在不在发送框内）。
    bool OnDragEnter(bool fromInput, const POINT& ptClient, bool hasFiles);
    bool OnDragOver(bool fromInput, const POINT& ptClient);
    void OnDragLeave();
    void OnFilesDropped(bool fromInput, const std::vector<CString>& files,
                        const POINT& ptClient);

    //落点在不在发送框内。fromInput 为真时恒为真（输入框本身就是发送框的一部分）。
    bool InDropZone(bool fromInput, const POINT& ptClient) const;

    //按需要通知 sink 显示 / 收起提示浮层；状态没变则不通知，免得拖动过程中每次
    //DragOver 都惊动一次界面。
    void SetHintVisible(bool visible);

    //把上面四个回调装到一个拖放目标上。两块区域（窗口 / 输入框）各有一份目标，
    //但回调完全一样，故抽出来避免两处 std::bind 写重。
    void WireCallbacks(balloonwjui::DuiDropTargetHelper& helper, bool fromInput);

    // ---- 从聊天记录拖进来的内容（Flamingo 聊天内容格式）----
    //
    //balloonui 的拖放辅助类只认文件与位图，拿不到别的格式。窗口上实际注册的是一层包装
    //（ChatContentDropTarget，见 .cpp）：数据里有 Flamingo 格式就由下面几个函数处理，
    //否则把每一步原样转给辅助类原来的目标，拖文件的行为一点不变。
    friend class ChatContentDropTarget;

    //数据对象里有没有 Flamingo 聊天内容格式。
    bool HasChatContent(IDataObject* pdo) const;

    //拖入与移动时判断落点：在发送框内返回 DROPEFFECT_COPY（且拖放源允许复制），否则
    //返回 DROPEFFECT_NONE；并按落点显示 / 收起提示。ptScreen 为 OLE 给的屏幕坐标。
    //  allowed：拖放源允许的效果（DragEnter / DragOver 传入的 *pdwEffect）。
    //  entering：是否为刚拖入窗口的那一次（只用于打日志）。
    DWORD OnContentDragMove(const POINTL& ptScreen, DWORD allowed, bool entering);

    //拖离窗口：收起提示。
    void OnContentDragLeave();

    //松手：落点在发送框内时解析出片段交给 sink 插入输入框，返回 DROPEFFECT_COPY；
    //落点不在发送框内或解析失败时不插入，返回 DROPEFFECT_NONE。
    DWORD OnContentDrop(IDataObject* pdo, const POINTL& ptScreen);

    //实际注册在聊天窗 HWND 上的包装目标（本类持有一份引用，Detach 时释放）；未绑定、
    //或包装注册失败退回辅助类原目标时为 NULL。
    IDropTarget*                     m_pContentTarget;

    balloonwjui::DuiDropTargetHelper m_target;      // 注册在聊天窗 HWND 上的拖放目标
    balloonwjui::DuiDropTargetHelper m_inputTarget; // 注册在输入框子 HWND 上的拖放目标
    IChatDropSink*                   m_sink;        // 回调接收方，引用不持有
    HWND                             m_frameHwnd;   // 聊天窗 HWND；未绑定时为 NULL
    HWND                             m_inputHwnd;   // 输入框子 HWND；未绑定 / 未创建时为 NULL
    bool                             m_attached;    // 是否已绑定
    bool                             m_hintShown;   // 提示浮层当前是否显示（去重用）
};

} // namespace balloonchatwnd
