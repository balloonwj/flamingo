/**
 *  MarkdownEditorWindow.h
 *
 *  Markdown 编辑器窗口（docs/Markdown消息方案.md 第 11.3 节）：左边编辑、右边实时预览；顶部「打开 / 保存 / 导出 ▾」、
 *  12 个格式按钮与「预览」开关；底部发送对象、「选择…」「发送」。登录前后都能打开，发送相关的能力经
 *  IMarkdownEditorHost 取得，未登录时底部显示「登录后可发送」。
 *
 *  窗口由 CMarkdownEditorManager 创建与持有：可以同时打开多个；窗口销毁时通知管理器释放自己。
 *
 *  快捷键：Ctrl+B / Ctrl+I 加粗 / 斜体，Ctrl+O 打开，Ctrl+S 保存；编辑框有焦点时 Tab 插入制表符（不切换焦点）。
 *
 *  窗口私有消息 kMsgMarkdownEditorDeferred（WM_APP + 0x8B）：按钮点击、快捷键先投给自己，等当前的控件通知或按键
 *  处理完再执行打开 / 保存 / 导出菜单 / 选择对象 / 发送（这些操作会弹出模态框或菜单），已登记在开发约定文档
 *  UI 约定第 6 条。定时器 kTimerPreview（文字变化后延时刷新预览）、kTimerStatus（状态文字过一会儿清除）编号取得
 *  较大，避开 balloonui 宿主窗口的定时器。
 *
 *  本文件遵循 balloonui 头文件约定：包含它之前调用方须已引入 ATL/WTL（Flamingo 侧由 stdafx.h 保证）。
 *
 *  balloonwj@qq.com   2026-10-06
 */
#pragma once

#include <string>
#include <vector>

#include "../../../third-party/balloonui/balloonui/Controls/Window/DuiFrameWindow.h"
#include "../../../third-party/balloonui/balloonui/DuiNotify.h"   // WM_DUI_NOTIFY
#include "MarkdownEditorHost.h"
#include "../BalloonMain/TitleBarIcon.h"   // ProgramWindowIcons

namespace balloonwjui { class DuiLabel; class DuiButton; class DuiRichEdit; class DuiEdit; class DuiScrollView; class DuiSplitter; }
namespace balloonchatwnd { class ChatMarkdownPreview; struct MarkdownEditResult; }

namespace balloonmd {

//编辑器窗口投给自己的延后操作：wParam 为 enum MarkdownEditorAction 的取值，lParam 未用
const UINT kMsgMarkdownEditorDeferred = WM_APP + 0x8B;

//延后执行的操作
enum MarkdownEditorAction
{
    md_editor_action_open        = 1,   //打开本地 .md 文件
    md_editor_action_save        = 2,   //保存（没有文件时另存为）
    md_editor_action_export_menu = 3,   //弹出导出格式菜单
    md_editor_action_pick        = 4,   //打开发送对象选择窗
    md_editor_action_send        = 5,   //发送
    md_editor_action_heading_menu = 6,  //格式条「H」按钮：弹出标题级别菜单（正文、一级 ~ 六级标题）
    md_editor_action_alert_menu  = 7,   //格式条「警告框」按钮：弹出警告框类型菜单（提示、技巧、重要、警告、注意）
    md_editor_action_close_find  = 8,   //查找条的关闭按钮：收起查找条（等按钮的点击通知处理完再隐藏它所在的行）
};

/**
 *  Markdown 编辑器窗口。继承 balloonwjui::DuiFrameWindow（无边框自绘窗体与标题栏），实现 IMarkdownTargetSink
 *  接收选择窗的结果。只在界面线程上使用。
 */
class CMarkdownEditorWindow : public balloonwjui::DuiFrameWindow, public IMarkdownTargetSink
{
public:
    CMarkdownEditorWindow();
    ~CMarkdownEditorWindow();

    /**
     *  创建并显示窗口。
     *  @param sourceUtf8   预填的源文本（UTF-8）；为空时打开空白文档。预填的内容不算未保存的修改
     *  @param target       预选的发送对象；为 NULL 时不预选
     *  @param cascadeIndex 第几个打开的编辑器（从 0 起），据此把窗口相对屏幕中心错开，避免完全重叠
     *  @return 窗口已显示时返回 true
     */
    bool Open(const std::string& sourceUtf8, const MarkdownSendTarget* target, int cascadeIndex);

    //是否有未保存的修改
    bool IsDirty() const;

    //文档名：保存过时为文件名，否则为「未命名」（已翻译），用于退出时的询问
    std::wstring DocumentName() const;

    //登录状态变化后由管理器调用：刷新底部的发送对象说明与按钮可用状态
    void RefreshSendArea();

    //退出程序时由管理器调用：不再询问未保存的修改，直接销毁窗口
    void CloseWithoutPrompt();

    /**
     *  选择窗的结果（IMarkdownTargetSink）：记下发送对象并刷新底部；不立即发送。
     *  @param targets 选定的对象
     */
    void OnMarkdownTargetsPicked(const std::vector<MarkdownSendTarget>& targets) override;

    BEGIN_MSG_MAP(CMarkdownEditorWindow)
        MESSAGE_HANDLER(WM_CLOSE, OnCloseMsg)
        MESSAGE_HANDLER(WM_DUI_NOTIFY, OnDuiNotify)
        //快捷键与 Tab 要赶在 DuiHost 之前处理：DuiHost 把 Tab 当作切换焦点
        MESSAGE_HANDLER(WM_KEYDOWN, OnKeyDownMsg)
        //快捷键产生的控制字符（Ctrl+I 是制表符）与已由列表续项处理的回车不能交给编辑框
        MESSAGE_HANDLER(WM_CHAR, OnCharMsg)
        //带 Alt 的格式快捷键（Alt+Shift+5 删除线）以 WM_SYSKEYDOWN 送来
        MESSAGE_HANDLER(WM_SYSKEYDOWN, OnSysKeyDownMsg)
        //输入法开始 / 结束组字：只记录状态（组字期间的回车不做列表续项），不消费，DuiHost 照常转给编辑框
        MESSAGE_HANDLER(WM_IME_STARTCOMPOSITION, OnImeCompositionMsg)
        MESSAGE_HANDLER(WM_IME_ENDCOMPOSITION, OnImeCompositionMsg)
        MESSAGE_HANDLER(WM_TIMER, OnTimer)
        MESSAGE_HANDLER(kMsgMarkdownEditorDeferred, OnDeferred)
        //销毁时清空借用的控件指针并通知管理器，只旁听、不消费，DuiFrameWindow 照常处理
        MESSAGE_HANDLER(WM_DESTROY, OnDestroyMsg)
        CHAIN_MSG_MAP(balloonwjui::DuiFrameWindow)
    END_MSG_MAP()

protected:
    //窗口彻底销毁之后：通知管理器释放本对象（之后不得再访问任何成员）
    void OnFinalMessage(HWND hWnd) override;

private:
    //搭客户区控件树，捕获各控件指针，设置编辑框字体
    void BuildClient();

    //编辑框里的文字（RichEdit 的段落分隔为 \r）
    std::wstring EditorText() const;

    //当前源文本（UTF-8，换行为 \n）
    std::string SourceUtf8() const;

    //以程序方式设置编辑框文字：不标记为未保存的修改
    void SetEditorText(const std::wstring& text);

    //标记 / 清除未保存的修改，并刷新标题栏
    void SetDirty(bool dirty);

    //刷新标题栏文字（文件名与未保存标记）
    void RefreshCaption();

    //用编辑框当前内容刷新预览
    void RefreshPreview();

    //收起或展开右侧预览
    void SetPreviewVisible(bool visible);

    //执行一个格式命令（取值见 MarkdownEditCommand）
    void ApplyFormat(int command);

    /**
     *  编辑框里按下回车时的列表续项（规则见 MarkdownEditCommands.h 的 ContinueMarkdownListOnEnter）：光标在列表项
     *  或引用里时，以一次可撤销的替换换行并写上标记，或在空项上去掉标记。
     *  @return 已处理返回 true，调用方要丢掉这次回车；不适用时返回 false，回车照常交给编辑框
     */
    bool ContinueListOnEnter();

    //Shift+回车：在编辑框里插入一个普通换行（一次可撤销的替换），不做列表续项
    void InsertPlainLineBreak();

    /**
     *  把选区覆盖的行设为指定级别的标题（规则见 MarkdownEditCommands.h 的 SetMarkdownHeadingLevel），一次可撤销的替换。
     *  @param level 0 表示正文，1 ~ 6 为标题级别
     */
    void SetHeadingLevel(int level);

    //弹出标题级别菜单（格式条「H」按钮下方）：正文、一级 ~ 六级标题，当前行的级别打勾，快捷键靠右显示
    void DoHeadingMenu();

    /**
     *  插入警告框，或把光标所在的警告框换成指定类型（规则见 MarkdownEditCommands.h 的 InsertMarkdownAlert），
     *  一次可撤销的替换。
     *  @param type 类型 1 ~ 5（enum MarkdownAlertType）
     */
    void InsertAlert(int type);

    //弹出警告框类型菜单（格式条「警告框」按钮下方）：提示、技巧、重要、警告、注意，类型名（NOTE 等）靠右显示
    void DoAlertMenu();

    /**
     *  显示查找条（docs/Markdown消息方案.md 第 13 节）。编辑框里选中的是单行文字时把它填进查找框，然后让查找框获得焦点
     *  并全选其中的文字，刷新匹配个数。
     *  @param withReplace true 时同时显示替换行（Ctrl+H），false 时只显示查找行（Ctrl+F）
     */
    void OpenFindBar(bool withReplace);

    //收起查找条，焦点回到编辑框（Esc、关闭按钮）
    void CloseFindBar();

    /**
     *  查找下一个或上一个，找到时在编辑框里选中并滚到可见；查找框为空时改为打开查找条。
     *  @param forward true 向后（F3、「下一个」），false 向前（Shift+F3、「上一个」）
     */
    void FindNext(bool forward);

    //替换当前选中的匹配并选中下一个（「替换」按钮、替换框里按回车），一次可撤销的替换
    void ReplaceCurrent();

    //全部替换（「全部替换」按钮），一次可撤销的替换，在查找条上显示替换了几处
    void ReplaceAll();

    //按查找框的文字刷新查找条上的匹配个数（「共 N 处」「无结果」；查找框为空时清空）
    void UpdateFindCount();

    //查找框的文字
    std::wstring FindQuery() const;

    //「区分大小写」是否勾选
    bool FindMatchCase() const;

    //焦点是否在查找框或替换框里（此时格式快捷键不作用于编辑框）
    bool FindBarHasFocus() const;

    /**
     *  按键与字符此刻是否会交给编辑框：以宿主的 DUI 焦点控件（GetDuiFocus，DuiHost 分发按键用的就是它）为准，
     *  而不是控件自己的 IsFocused。弹出菜单会拿走窗口的 Win32 焦点，编辑框的 IsFocused 随之变为 false，
     *  菜单关闭后不一定恢复，而宿主的焦点控件始终没有变（实机核对见 docs/Markdown消息方案.md 13.5 节）。
     */
    bool EditorHasKeyboardFocus() const;

    //丢掉快捷键随后产生的那条控制字符消息（Ctrl+2 产生 0x00、Ctrl+6 产生 0x1E 等）。只在队列里紧挨着的是控制字符
    //时才取走，不会误删用户输入的文字；没有产生字符的组合键（如 Ctrl+1）也不会留下待丢弃的状态
    void DropPendingControlChar();

    //丢掉带 Alt 的快捷键随后产生的那条 WM_SYSCHAR（Alt+Shift+5 产生「%」），否则系统会按菜单助记符处理并发出提示音
    void DropPendingSysChar();

    /**
     *  执行一个格式快捷键（取值见 MarkdownEditCommands.h 的 enum MarkdownShortcut）：标题级别、加粗、斜体、下划线、
     *  行内代码、删除线、链接、清除样式。
     *  @param shortcut 快捷键；md_shortcut_none 时什么都不做
     */
    void ApplyShortcut(int shortcut);

    //清除选区里的样式（Ctrl+\，规则见 MarkdownEditCommands.h 的 ClearMarkdownInlineStyle）
    void ClearStyle();

    /**
     *  把一次编辑的结果写回编辑框：一次可撤销的替换（Ctrl+Z 一步撤回），再设好选区。没有改动时只设选区。
     *  @param r 编辑结果（MarkdownEditCommands 的各函数算出）
     */
    void ApplyEditResult(const balloonchatwnd::MarkdownEditResult& r);

    //在顶部右侧显示一段短提示，过一会儿自动清除
    void ShowStatus(const std::wstring& text);

    //打开本地 .md 文件（有未保存的修改时先询问）
    void DoOpen();

    /**
     *  保存。
     *  @param saveAs true 时总是弹出另存为对话框
     *  @return 已保存返回 true；用户取消或写文件失败返回 false
     */
    bool DoSave(bool saveAs);

    //弹出导出格式菜单并导出
    void DoExportMenu();

    //打开发送对象选择窗
    void DoPickTargets();

    //检查后把文档发给已选的对象；没有对象时先打开选择窗
    void DoSend();

    /**
     *  有未保存的修改时询问「保存 / 不保存 / 取消」。
     *  @return 可以继续（已保存或选择不保存）返回 true；选择取消或保存失败返回 false
     */
    bool ConfirmDiscardChanges();

    LRESULT OnCloseMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnDuiNotify(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnKeyDownMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnCharMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnSysKeyDownMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnImeCompositionMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnTimer(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnDeferred(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnDestroyMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

private:
    HFONT                           m_editorFont;      //编辑框字体，本类持有，析构时释放（DuiRichEdit 只借用不释放）
    std::wstring                    m_filePath;        //当前文档的完整路径；未保存过时为空
    bool                            m_dirty;           //是否有未保存的修改
    bool                            m_settingText;     //正在以程序方式设置编辑框文字：期间的文字变化不算修改
    bool                            m_previewVisible;  //右侧预览是否展开
    int                             m_savedSplitPx;    //收起预览前编辑区的宽度（像素），展开时恢复；0 表示按一半分
    std::vector<MarkdownSendTarget> m_targets;         //已选的发送对象，按勾选先后排列
    bool                            m_pickerOpen;      //选择窗是否正在为本窗口打开着（销毁前要通知宿主取消）
    bool                            m_swallowCtrlChar; //刚处理了一个 Ctrl 快捷键或回车：接下来那条控制字符消息丢掉
    bool                            m_imeComposing;    //输入法正在组字：此时的回车用来确认候选词，不做列表续项
    bool                            m_findVisible;     //查找条（查找行）是否显示
    balloonmain::ProgramWindowIcons m_programIcons;    //标题栏与任务栏的程序图标，本对象持有，随本对象析构释放

    // ---- 控件（借用指针，控件树持有，随窗口销毁）----
    balloonwjui::DuiRichEdit*           m_pEditor;          //编辑框
    balloonchatwnd::ChatMarkdownPreview* m_pPreview;        //预览控件
    balloonwjui::DuiScrollView*         m_pPreviewScroll;   //预览的滚动视图
    balloonwjui::DuiSplitter*           m_pSplitter;        //分隔条
    balloonwjui::DuiButton*             m_pPreviewToggle;   //「预览」开关
    balloonwjui::DuiLabel*              m_pTargets;         //发送对象说明
    balloonwjui::DuiLabel*              m_pStatus;          //顶部右侧的状态文字
    balloonwjui::DuiButton*             m_pPick;            //「选择…」
    balloonwjui::DuiButton*             m_pSend;            //「发送」
    balloonwjui::DuiEdit*               m_pFindEdit;        //查找框
    balloonwjui::DuiEdit*               m_pReplaceEdit;     //替换框
    balloonwjui::DuiButton*             m_pMatchCase;       //「区分大小写」勾选框
    balloonwjui::DuiLabel*              m_pFindCount;       //查找条上的匹配个数或查找结果
};

} // namespace balloonmd
