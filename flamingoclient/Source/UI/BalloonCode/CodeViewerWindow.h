/**
 *  CodeViewerWindow.h
 *
 *  代码查看窗（docs/代码片段方案.md §3.3）：聊天窗、消息记录窗、聊天记录查看窗里点了代码卡片的正文、底栏
 *  「查看全部」或右键「在窗口中查看」时打开。正文是只读的 RichEdit，可以选取、Ctrl+C；默认不折行、可横向滚动；
 *  左侧有行号栏。工具条可以换语言（只改本窗口的着色）、开关自动换行、复制全部、另存为。
 *
 *  带批注的代码片段（docs/代码片段方案.md 第 10.5 节）：右侧是只读的批注栏，打开时默认展开（窗口随之加宽一个批注栏的宽度，
 *  放得下时），可以收起；工具条多出「批注 (N)」，点它展开或收起。被批注的行铺浅黄底色、行号栏画橙色竖条；
 *  点卡片上的「第 3～4 行」在正文里选中那几行，把光标放到被批注的行上时对应的卡片高亮。「复制全部」「另存为」只写代码。
 *
 *  追加批注（docs/代码片段方案.md 第 12 节）：从单聊窗、群聊窗打开、且消息已有 msgid 时，就在本窗口里加批注——工具条的
 *  「添加批注」与正文右键「添加批注…」为选中的行（只放了光标时为光标所在的行）在批注栏里插入编辑卡片；别人的批注只读，
 *  本人的可以修改、删除；底部一行写状态与「发送」，有新增或修改时才能发送，发送时把带批注的新代码片段交给发起打开的
 *  聊天窗（kMsgCodeComposeSend，聊天窗引用原消息发出），随后关闭本窗口；有没发出的批注时关窗先询问。
 *  批注卡片写出作者；打开追加批注的消息时，选中第一条「新」批注的行。
 *
 *  每条消息最多一个窗口：Show 按消息标识复用已打开的窗口，调到前台。窗口在堆上创建、不设 owner，
 *  关闭时在 OnFinalMessage 里释放自己，发起的聊天窗关闭后它照常保留。
 *
 *  窗口私有定时器 kTimerCopied（「已复制」恢复为「复制全部」），编号取得较大，避开 balloonui 宿主窗口自己的定时器。
 *  窗口私有消息 kMsgCodeViewerSelChanged（WM_APP + 0x8E）：投给自己、留到当前引擎回调或控件通知处理完再做的事，
 *  wParam 为 CodeViewerDeferredAction（选区变化后高亮光标所在行的批注、批注卡片上的按钮），已登记在开发约定文档 UI 约定第 6 条。
 *
 *  典型用法（聊天窗处理 WM_CHAT_OPEN_CODE_VIEWER）：
 *      ballooncode::CCodeViewerWindow::Show(m_hWnd, pReq->m_code, pReq->m_lang, pReq->m_key, pReq->m_notes);
 *
 *  本文件遵循 balloonui 头文件约定：包含它之前调用方须已引入 ATL/WTL（Flamingo 侧由 stdafx.h 保证）。
 *
 *  balloonwj@qq.com   2026-10-06
 */
#pragma once

#include <map>
#include <string>
#include <vector>

#include "../../../third-party/balloonui/balloonui/Controls/Window/DuiFrameWindow.h"
#include "../../../third-party/balloonui/balloonui/DuiNotify.h"   // WM_DUI_NOTIFY
#include "../BalloonChatWnd/ChatCodeBlock.h"                      // CodeBlockContent
#include "../BalloonMain/TitleBarIcon.h"                          // ProgramWindowIcons：标题栏与任务栏的程序图标

namespace balloonwjui { class DuiComboBox; class DuiButton; class DuiRichEdit; class DuiLabel; }

namespace ballooncode {

class CodeLineGutter;
class CodeMarkedEdit;
class CodeNotesSplitter;
class CodeNotesPane;

//代码查看窗投给自己、留到当前引擎回调或控件通知处理完再做的事；wParam 为 CodeViewerDeferredAction，
//lParam 为批注下标（只有 kCodeViewerDeferEditNote、kCodeViewerDeferDeleteNote 用到）
const UINT kMsgCodeViewerSelChanged = WM_APP + 0x8E;

/**
 *  kMsgCodeViewerSelChanged 的 wParam：要做的事。
 */
enum CodeViewerDeferredAction
{
    kCodeViewerDeferSelChanged  = 0,   //正文的选区变了：引擎回调里不能查询选区，留到这里高亮光标所在行的批注
    //以下来自批注卡片上的按钮与链接（docs/代码片段方案.md 第 12 节）：它们会重建卡片列表，而被点的控件就在列表里，
    //等点击通知处理完再做（开发约定文档 UI 约定第 13 条）
    kCodeViewerDeferConfirmNote = 1,   //编辑卡片的「确定」，或批注输入框里按了 Ctrl+Enter（等字符消息处理完）
    kCodeViewerDeferCancelNote  = 2,   //编辑卡片的「取消」
    kCodeViewerDeferEditNote    = 3,   //卡片上的「修改」：lParam 为批注下标
    kCodeViewerDeferDeleteNote  = 4,   //卡片上的「删除」：lParam 为批注下标
};

/**
 *  打开代码查看窗时的附加信息（docs/代码片段方案.md 第 11.3 节）。
 */
struct CodeViewerExtras
{
    bool      m_canAnnotate;   //工具条显示「添加批注」：发起打开的是单聊窗或群聊窗、且这条消息已有服务器 msgid 时为 true
    long long m_msgid;         //这条消息的服务器 msgid，点「添加批注」时原样交回聊天窗
    long long m_senderId;      //这条消息的发送者 userid（0 表示不知道），点「添加批注」时原样交回聊天窗
    bool      m_fromMe;        //这条消息是不是本人发的，点「添加批注」时原样交回聊天窗
    std::map<long long, std::wstring> m_authorNames;   //批注作者的显示名（userid 到名字）；为空时批注卡片上不写作者
    long long m_selfId;        //本人 userid（docs/代码片段方案.md 第 12 节）：能追加批注时只有本人的批注能修改、删除
    std::wstring m_senderName; //这条消息发送者的显示名，底部状态文字写「某某的代码」；为空时不写
    std::vector<codesnippet::CodeNote> m_editedNotes;   //没能发出、重新编辑时放回的批注（上一次交给聊天窗的批注）；
                                                        //为空时从这条消息的批注开始
    bool      m_openNotesPane; //打开时展开批注栏，即使还没有批注（聊天卡片右键「添加批注…」打开时为 true）

    CodeViewerExtras()
        : m_canAnnotate(false)
        , m_msgid(0)
        , m_senderId(0)
        , m_fromMe(false)
        , m_selfId(0)
        , m_openNotesPane(false)
    {
    }
};

/**
 *  代码查看窗。继承 balloonwjui::DuiFrameWindow，由它提供无边框自绘窗体与标题栏。
 */
class CCodeViewerWindow : public balloonwjui::DuiFrameWindow
{
public:
    /**
     *  打开某条消息的代码查看窗；这条消息的窗口已经打开时调到前台。
     *  @param anchor 发起打开的窗口：新建窗口时居中摆在它上面；为 NULL 或无效时居中于屏幕
     *  @param code   规范化后的代码（换行为 LF）
     *  @param lang   语言，取值见 codesnippet::CodeLanguage
     *  @param key    消息标识（msgid 的十进制串或 clientMsgId）；为空时每次都新建窗口
     *  @param notes  批注，行号指 code 的行；没有批注时为空，此时不显示批注栏与「批注」按钮
     *  @param extras 附加信息：能否追加批注、批注作者的名字等（docs/代码片段方案.md 第 11.3 节）；窗口已打开时不再更新
     *  @return 窗口已显示时返回 true
     */
    static bool Show(HWND anchor, const std::wstring& code, int lang, const std::string& key,
                     const std::vector<codesnippet::CodeNote>& notes = std::vector<codesnippet::CodeNote>(),
                     const CodeViewerExtras& extras = CodeViewerExtras());

    /**
     *  按消息标识找已打开的代码查看窗（调试入口的自测用它取得窗口与控件树）。
     *  @param key 打开时传给 Show 的消息标识
     *  @return 窗口对象（借用，窗口关闭后失效）；没有打开时返回 NULL
     */
    static CCodeViewerWindow* FindOpened(const std::string& key);

    /**
     *  本次新增或修改、还没发出的批注条数（以打开时的批注为底比较；正在编辑、输入框里已有文字的那一条也算）。
     *  不能追加批注时为 0。关窗时据此询问；调试入口的自测也用它检查。
     */
    int UnsentNoteCount() const;

    BEGIN_MSG_MAP(CCodeViewerWindow)
        MESSAGE_HANDLER(WM_CLOSE, OnCloseMsg)
        MESSAGE_HANDLER(WM_DUI_NOTIFY, OnDuiNotify)
        //Ctrl+Enter、Esc 要赶在 DuiHost 之前处理（批注输入框里确定、放弃批注）
        MESSAGE_HANDLER(WM_KEYDOWN, OnKeyDownMsg)
        MESSAGE_HANDLER(WM_CHAR, OnCharMsg)
        MESSAGE_HANDLER(WM_TIMER, OnTimer)
        MESSAGE_HANDLER(kMsgCodeViewerSelChanged, OnSelChanged)
        CHAIN_MSG_MAP(balloonwjui::DuiFrameWindow)
    END_MSG_MAP()

private:
    /**
     *  只经 Show 在堆上创建。
     *  @param code 规范化后的代码；@param lang 语言；@param key 消息标识；@param notes 批注（按代码行数整理后保存）
     */
    CCodeViewerWindow(const std::wstring& code, int lang, const std::string& key,
                      const std::vector<codesnippet::CodeNote>& notes, const CodeViewerExtras& extras);
    ~CCodeViewerWindow();

    //创建窗口、搭控件树、载入正文并显示；失败时返回 false（调用方负责释放对象）
    bool CreateAndShow(HWND anchor);

    //按当前语言着色，生成 RTF 载入正文，并更新行号栏与标题
    void LoadContent();

    //把代码原文写进剪贴板，「复制全部」暂时改为「已复制」
    void CopyAll();

    //展开或收起批注栏：窗口随之加宽或缩回，「批注 (N)」的文字不变；显隐变化后对分栏重排
    void SetNotesPaneOpen(bool open);

    //在正文里选中第 index 条批注的行并滚到可见，高亮对应的卡片
    void SelectNoteLines(int index);

    //按正文的选区高亮对应的批注卡片并滚到可见
    void HighlightNoteAtCaret();

    // ---- 追加批注（docs/代码片段方案.md 第 12 节）----

    //能否在本窗口里追加批注：打开时允许（附加信息的 m_canAnnotate），且知道本人 userid
    bool CanAnnotate() const;

    //选区覆盖的逻辑行（从 1 起、含两端）；只有光标时为光标所在的一行
    void SelectedLines(int& from, int& to) const;

    //工具条「添加批注」与右键「添加批注…」：为选中的行添加批注
    void BeginAddNote();

    //为第 from～to 行添加批注：展开批注栏并插入编辑卡片；这几行已有本人的批注时改为修改那一条
    void BeginAddNoteOnLines(int from, int to);

    //修改第 index 条批注（只能是本人的）：卡片变成输入框
    void BeginEditNote(int index);

    //确定正在编辑的批注；说明为空时：新增的放弃，修改的删除
    void ConfirmNoteEdit();

    //放弃正在编辑的批注
    void CancelNoteEdit();

    //删除第 index 条批注（只能是本人的）
    void DeleteNote(int index);

    //进入编辑之后：刷新按钮状态，把编辑卡片滚到可见并把焦点交给输入框
    void EnterNoteEditing();

    //结束编辑之后：刷新按钮状态，焦点回到正文
    void LeaveNoteEditing();

    //以打开时的批注为底整理批注：别人的原样保留，本人新增或修改的标「新」（MergeAnnotationRound）
    std::vector<codesnippet::CodeNote> MergedNotes() const;

    //把 m_notes 同步到批注栏、行号栏的竖条、正文的浅黄底色与「批注 (N)」，并刷新底部一行
    void ApplyNotes();

    //刷新底部一行的状态文字与「发送」，以及「添加批注」按钮与右键菜单项的可用状态
    void RefreshAnnotateBar();

    //「发送」：把带批注的新代码片段交给发起打开的聊天窗，随后关闭本窗口；聊天窗已经关闭时提示用户
    void SendAnnotation();

    //窗口销毁后的最后一条消息：从登记表里移除并释放自己
    void OnFinalMessage(HWND hWnd) override;

    LRESULT OnCloseMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnDuiNotify(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnKeyDownMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnCharMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnTimer(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnSelChanged(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

private:
    std::wstring                     m_code;      //规范化后的代码原文
    int                              m_lang;      //当前着色用的语言（下拉框可改），取值见 codesnippet::CodeLanguage
    std::string                      m_key;       //消息标识，登记表的键；为空时不登记
    balloonchatwnd::CodeBlockContent m_content;   //按当前语言准备好的内容（着色结果）
    std::vector<codesnippet::CodeNote> m_notes;   //批注（已按代码行数整理），行号从 1 起
    std::vector<long> m_lineStarts;               //各逻辑行在正文里的起始字符位置（BuildLineStartPositions 的结果）
    int m_paneGrownPx;                            //展开批注栏时窗口加宽的像素数，收起时据此缩回；没有加宽时为 0
    int m_messageLang;                            //消息本身的语言（下拉框改的只是本窗口的着色），追加批注时交回聊天窗
    CodeViewerExtras m_extras;                    //打开时的附加信息（docs/代码片段方案.md 第 11.3 节）
    HWND m_anchor;                                //发起打开的窗口（借用），追加批注时把要发出的代码片段交给它；可能已经关闭
    std::vector<codesnippet::CodeNote> m_originalNotes;   //能追加批注时这条消息上的批注（作者已补全），整理批注、判断有没有
                                                          //新增或修改时以它为底
    bool m_swallowEnterChar;                      //刚在批注输入框里按了 Ctrl+Enter：接下来那条换行字符消息丢掉
    bool m_closeConfirmed;                        //已经发出（或已确认放弃）：关闭时不再询问
    balloonmain::ProgramWindowIcons m_programIcons;   //标题栏与任务栏的程序图标（开发约定文档 UI 约定第 10 条），本对象持有

    // ---- 控件（借用指针，控件树持有，随窗口销毁）----
    balloonwjui::DuiComboBox* m_pLanguage;   //语言下拉框
    balloonwjui::DuiButton*   m_pWrap;       //「自动换行」勾选框
    balloonwjui::DuiButton*   m_pCopy;       //「复制全部」
    CodeMarkedEdit*           m_pEdit;       //正文编辑框（被批注的行铺浅黄底色）
    CodeLineGutter*           m_pGutter;     //行号栏
    balloonwjui::DuiButton*   m_pNotes;      //「批注 (N)」；没有批注时隐藏
    balloonwjui::DuiButton*   m_pAnnotate;   //「添加批注」；不能追加批注时隐藏
    balloonwjui::DuiControl*  m_pAnnotateBar;     //底部一行（状态文字与「发送」）；不能追加批注时隐藏
    balloonwjui::DuiLabel*    m_pAnnotateStatus;  //底部一行的状态文字
    balloonwjui::DuiButton*   m_pAnnotateSend;    //底部一行的「发送」
    CodeNotesSplitter*        m_pSplit;      //正文与批注栏之间的左右分栏
    CodeNotesPane*            m_pNotesPane;  //右侧只读的批注栏
};

} // namespace ballooncode
