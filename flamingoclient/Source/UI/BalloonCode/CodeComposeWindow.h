/**
 *  CodeComposeWindow.h
 *
 *  「发送代码」窗口（docs/代码片段方案.md §3.1、§5.5、§10）：单聊、群聊输入区工具栏的「代码」按钮打开它，粘贴区的
 *  「以代码片段发送」与发送失败的代码片段「重新编辑」也打开它。顶部选语言（默认自动识别）、「行号」「格式化」「批注」，
 *  中间是等宽字体的编辑框（纯文本、不折行）与右侧可以收起的批注栏，底部「取消」「发送」，Ctrl+Enter 发送、Esc 关闭；
 *  编辑框里按 Tab 插入制表符。
 *
 *  批注（§10.5）：选中几行后点「批注」或右键「添加批注…」，批注栏展开并插入一张编辑中的卡片，Ctrl+Enter 确定、Esc 取消；
 *  编辑批注期间代码编辑框只读（批注的行范围在编辑期间不会变）。改了代码之后批注按行比较重新定位；被批注的行铺浅黄底色、
 *  行号栏画橙色竖条。展开批注栏时窗口向右加宽一个批注栏的宽度（放得下时），收起时缩回。
 *
 *  窗口不直接发消息：按「发送」后把代码与语言装进 CodeComposeResult，经 kMsgCodeComposeSend 投给宿主聊天窗，
 *  由宿主走现有的文字发送流程（乐观气泡、引用、回执、失败重发都沿用）。宿主按值持有本窗口对象，关闭时调 Close。
 *
 *  给已经发出的代码片段追加批注不在本窗口里做，而在代码查看窗里（docs/代码片段方案.md 第 12 节）；查看窗同样经
 *  kMsgCodeComposeSend 把要发出的代码交给聊天窗，CodeComposeResult::m_annotate 记着原消息。
 *
 *  窗口私有消息：kMsgCodeComposeSend（WM_APP + 0x87，投给宿主聊天窗）、kMsgCodeComposeDeferred
 *  （WM_APP + 0x88，投给自己、留到当前按键或引擎回调处理完再做的事，wParam 为 CodeComposeDeferredAction），
 *  已登记在开发约定文档 UI 约定第 6 条。定时器 kTimerStatus（编辑框文字变化后延时刷新状态文字）编号取得较大，避开 balloonui 宿主窗口的定时器。
 *
 *  典型用法（聊天窗工具栏「代码」按钮）：
 *      m_codeCompose.Open(m_hWnd, selectedText, m_lpFMGClient->m_UserConfig.GetCodeSnippetLang());
 *
 *  本文件遵循 balloonui 头文件约定：包含它之前调用方须已引入 ATL/WTL（Flamingo 侧由 stdafx.h 保证）。
 *
 *  balloonwj@qq.com   2026-10-06
 */
#pragma once

#include <map>
#include <string>
#include <vector>

#include "../../Common/Code/CodeNotes.h"
#include "../BalloonMain/TitleBarIcon.h"   // ProgramWindowIcons：标题栏的程序图标（开发约定文档 UI 约定第 10 条）
#include "../../../third-party/balloonui/balloonui/Controls/Window/DuiFrameWindow.h"
#include "../../../third-party/balloonui/balloonui/DuiNotify.h"   // WM_DUI_NOTIFY

namespace balloonwjui { class DuiLabel; class DuiComboBox; class DuiButton; class DuiRichEdit; }

namespace ballooncode {

class CodeLineGutter;
class CodeComposeEdit;
class CodeNotesSplitter;
class CodeNotesPane;

//发送代码窗口把要发送的代码交回宿主聊天窗：lParam 为堆上 CodeComposeResult*，宿主处理后 delete；wParam 未用
const UINT kMsgCodeComposeSend = WM_APP + 0x87;

//发送代码窗口投给自己、留到当前按键、引擎回调或控件通知处理完再做的事；wParam 为 CodeComposeDeferredAction，
//lParam 为批注下标（只有 kCodeComposeDeferEditNote、kCodeComposeDeferDeleteNote 用到）
const UINT kMsgCodeComposeDeferred = WM_APP + 0x88;

/**
 *  kMsgCodeComposeDeferred 的 wParam：要做的事。
 */
enum CodeComposeDeferredAction
{
    kCodeComposeDeferSend        = 0,   //按下 Ctrl+Enter 后发送：等这次按键的字符消息处理完再发，换行不会被插进编辑框
    kCodeComposeDeferConfirmNote = 1,   //批注输入框里按下 Ctrl+Enter 后确定这条批注：同样等字符消息处理完
    kCodeComposeDeferSelChanged  = 2,   //代码编辑框的选区变了：引擎回调里不能查询选区，留到这里高亮光标所在行的批注
    //以下三项来自批注卡片上的按钮与链接：它们会重建卡片列表，而被点的控件就在列表里，若在点击通知里直接执行，
    //控件的点击处理函数返回后会访问已释放的自身（DuiLabel 发完通知还要读自己的成员），所以等通知处理完再做。
    //「确定」同样经 kCodeComposeDeferConfirmNote 延后
    kCodeComposeDeferCancelNote  = 3,   //编辑卡片的「取消」：放弃正在编辑的批注
    kCodeComposeDeferEditNote    = 4,   //卡片上的「修改」：lParam 为批注下标
    kCodeComposeDeferDeleteNote  = 5,   //卡片上的「删除」：lParam 为批注下标
};

/**
 *  追加批注的原消息（docs/代码片段方案.md 第 11、12 节）：代码查看窗里给一条已经发出的代码片段追加了批注、交给聊天窗
 *  发出一条新的代码片段时，随 CodeComposeResult 带上。
 */
struct CodeAnnotateParams
{
    long long    m_baseMsgId;        //原消息的服务器 msgid，聊天窗据此引用原消息；0 表示不是追加批注
    long long    m_baseSenderId;     //原消息的发送者 userid：原消息里没写作者的批注，作者就是他
    std::wstring m_baseSenderName;   //原消息发送者的显示名
    long long    m_selfId;           //本人 userid
    std::map<long long, std::wstring> m_authorNames;   //批注作者的显示名（userid 到名字）

    CodeAnnotateParams()
        : m_baseMsgId(0)
        , m_baseSenderId(0)
        , m_selfId(0)
    {
    }
};

/**
 *  kMsgCodeComposeSend 的载荷：要发送的代码。
 */
struct CodeComposeResult
{
    std::wstring m_code;         //规范化后的代码（换行为 LF，去掉了开头与末尾的空白行）
    int          m_lang;         //语言，取值见 codesnippet::CodeLanguage（自动识别时为识别结果）
    bool         m_manualLang;   //语言是用户在下拉框里手选的；宿主据此记住这次的选择（追加批注时恒为 false）
    std::vector<codesnippet::CodeNote> m_notes;   //批注，行号指 m_code 的行（已随去掉的开头空白行平移、已整理）；
                                                  //追加批注时作者都已补全，本次新增或修改的标「新」
    CodeAnnotateParams m_annotate;   //代码查看窗追加批注时为原消息（m_baseMsgId 不为 0）；发送代码窗口交回时为默认值
    std::vector<codesnippet::CodeNote> m_originalNotes;   //追加批注时原消息上的批注（作者已补全），聊天窗没能发出、
                                                          //重新打开查看窗时与 m_notes 一起带回

    CodeComposeResult()
        : m_lang(0)
        , m_manualLang(false)
    {
    }
};

/**
 *  「发送代码」窗口。继承 balloonwjui::DuiFrameWindow，由它提供无边框自绘窗体与标题栏。
 */
class CCodeComposeWindow : public balloonwjui::DuiFrameWindow
{
public:
    CCodeComposeWindow();
    ~CCodeComposeWindow();

    /**
     *  打开窗口；已经打开时调到前台，保留已输入的内容（initialCode 不为空时改为它，批注随之换成 initialNotes）。
     *  @param owner         宿主聊天窗：结果投给它，新建窗口时居中摆在它上面
     *  @param initialCode   预填的代码（如输入框里选中的文字、粘贴识别出的代码）；可以为空
     *  @param initialLangId 预选的语言标识（如 "cpp"）；为空或不认识时选「自动识别」
     *  @param initialNotes  预填的批注（发送失败后重新编辑时带回），行号指 initialCode 的行；可以为空
     *  @return 窗口已显示时返回 true
     */
    bool Open(HWND owner, const std::wstring& initialCode, const std::string& initialLangId,
              const std::vector<codesnippet::CodeNote>& initialNotes = std::vector<codesnippet::CodeNote>());

    /**
     *  销毁窗口（宿主关闭时调用；没打开时什么都不做）。
     */
    void Close();

    BEGIN_MSG_MAP(CCodeComposeWindow)
        MESSAGE_HANDLER(WM_CLOSE, OnCloseMsg)
        MESSAGE_HANDLER(WM_DUI_NOTIFY, OnDuiNotify)
        //Tab、Ctrl+Enter、Esc 要赶在 DuiHost 之前处理：DuiHost 把 Tab 当作切换焦点
        MESSAGE_HANDLER(WM_KEYDOWN, OnKeyDownMsg)
        MESSAGE_HANDLER(WM_CHAR, OnCharMsg)
        MESSAGE_HANDLER(WM_TIMER, OnTimer)
        MESSAGE_HANDLER(kMsgCodeComposeDeferred, OnDeferred)
        //销毁时清空借用的控件指针，只旁听、不消费，DuiFrameWindow 照常处理
        MESSAGE_HANDLER(WM_DESTROY, OnDestroyMsg)
        CHAIN_MSG_MAP(balloonwjui::DuiFrameWindow)
    END_MSG_MAP()

private:
    /**
     *  新建窗口：标题栏、图标、最小尺寸，清掉上一次打开时的批注与状态，搭客户区并设为默认尺寸。
     *  @param owner 宿主聊天窗
     *  @param title 窗口标题（已取译文）
     *  @return 建成时返回 true
     */
    bool CreateFrame(HWND owner, const wchar_t* title);

    //搭客户区控件树，捕获各控件指针，填语言下拉框
    void BuildClient();

    //为第 from～to 行添加批注：展开批注栏并插入编辑中的卡片；这几行已有批注时改为修改那一条
    void BeginAddNoteOnLines(int from, int to);

    //编辑框里的文字（换行为 RichEdit 的 \r，未规范化）
    std::wstring EditorText() const;

    //下拉框选的语言；选「自动识别」时 autoDetect 为 true，返回 code 的识别结果
    int ChosenLanguage(const std::wstring& code, bool& autoDetect) const;

    //按编辑框当前内容刷新状态文字与「发送」按钮的可用状态
    void RefreshStatus();

    //显示或隐藏行号栏，同步「行号」勾选框，并对所在横排重排（显隐变化不会自动重排，UI 约定第 5 条）
    void ApplyLineNumbers(bool show);

    //按编辑框当前的行数调整行号栏宽度（位数变化时才重排）
    void UpdateGutterWidth();

    //格式化（docs/代码片段方案.md 第 10.4 节）：有选区时处理选区覆盖的整行，否则处理全文；可撤销；结果写在状态文字里
    void DoFormat();

    //检查后把代码交给宿主，清空编辑框并关闭窗口；为空或超限时只刷新状态
    void DoSend();

    // ---- 批注（docs/代码片段方案.md 第 10.5 节）----

    //发送时的批注：随去掉的开头空白行平移，按规范化后代码的行数整理
    std::vector<codesnippet::CodeNote> NotesForSending() const;

    //把 m_notes 同步到批注栏、行号栏的竖条、编辑框的浅黄底色与「批注」按钮
    void ApplyNotes();

    //刷新「批注」按钮的文字与可用状态、右键菜单「添加批注…」的可用状态
    void UpdateNotesButton();

    //代码改动之后重新定位批注（按行比较 m_notesText 与当前文字）；有批注被删掉时写进状态文字
    void RemapNotes();

    //选区覆盖的行（从 1 起、含两端）；只有光标时为光标所在的一行
    void SelectedLines(int& from, int& to) const;

    //为选中的行添加批注：展开批注栏并插入编辑中的卡片；选中的行已有批注时改为修改那一条
    void BeginAddNote();

    //修改第 index 条批注：展开批注栏，卡片变成输入框
    void BeginEditNote(int index);

    //确定正在编辑的批注；说明为空时：新增的放弃，修改的删除
    void ConfirmNoteEdit();

    //放弃正在编辑的批注
    void CancelNoteEdit();

    //批注栏进入编辑之后：代码编辑框改为只读，刷新按钮状态，把编辑卡片滚到可见并把焦点交给输入框
    void EnterNoteEditing();

    //批注栏结束编辑之后：代码编辑框恢复可写，刷新按钮状态，焦点回到代码编辑框
    void LeaveNoteEditing();

    //删除第 index 条批注，状态文字显示「已删除 1 条批注」与「撤销」
    void DeleteNote(int index);

    //恢复刚删掉的那条批注（与现有批注相交时放弃）
    void UndoDeleteNote();

    //显示或隐藏「撤销」链接，并对所在横排重排（显隐变化不会自动重排，UI 约定第 5 条）
    void ShowUndoLink(bool show);

    //在代码里选中第 index 条批注的行并滚到可见，高亮对应的卡片
    void SelectNoteLines(int index);

    //按光标所在的行高亮对应的卡片并滚到可见
    void HighlightNoteAtCaret();

    /**
     *  展开或收起批注栏。展开时窗口向右加宽一个批注栏的宽度（未最大化且加宽后仍在工作区内时），收起时缩回加宽的部分。
     *  @param open       true 展开
     *  @param saveConfig 是否把展开状态记进用户配置（用户操作时记，打开窗口时按批注自动设置不记）
     */
    void SetNotesPaneOpen(bool open, bool saveConfig);

    LRESULT OnCloseMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnDuiNotify(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnKeyDownMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnCharMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnTimer(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnDeferred(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnDestroyMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

private:
    HWND   m_owner;                //宿主聊天窗（借用），结果投给它
    HFONT  m_editorFont;           //编辑框的等宽字体，本类持有，析构时释放（DuiRichEdit 只借用不释放）
    int    m_fontPx;               //编辑框与行号栏的字号（像素，已按窗口 DPI 换算）
    bool   m_swallowEnterChar;     //刚按了 Ctrl+Enter：接下来那条换行字符消息丢掉，不插进编辑框
    std::wstring m_statusNote;     //格式化、删除批注等操作之后显示在状态文字里的结果；内容再被改动后清空
    std::wstring m_statusNoteText; //显示 m_statusNote 时编辑框里的文字，用来判断内容有没有再被改动

    // ---- 批注 ----
    std::vector<codesnippet::CodeNote> m_notes;   //当前的批注，行号指编辑框里的行（从 1 起）
    std::wstring m_notesText;      //m_notes 的行号所对应的编辑框文字；文字变了就按行比较重新定位
    bool   m_hasDeletedNote;       //有可以撤销的删除（「撤销」链接显示期间为 true）
    codesnippet::CodeNote m_deletedNote;   //刚删掉的那条批注，供「撤销」恢复
    int    m_paneGrownPx;          //展开批注栏时窗口加宽的像素数，收起时据此缩回；没有加宽时为 0
    balloonmain::ProgramWindowIcons m_programIcons;   //标题栏的程序图标，本对象持有，窗口关闭再打开时复用

    // ---- 控件（借用指针，控件树持有，随窗口销毁）----
    balloonwjui::DuiComboBox* m_pLanguage;   //语言下拉框，第 0 项为「自动识别」
    balloonwjui::DuiLabel*    m_pStatus;     //状态文字
    balloonwjui::DuiRichEdit* m_pEditor;     //代码编辑框
    CodeComposeEdit*          m_pCodeEdit;   //同一个编辑框，按子类取用（被批注行的底色、选区变化通知）
    balloonwjui::DuiButton*   m_pSend;       //「发送」
    balloonwjui::DuiButton*   m_pLineNumbers;   //「行号」勾选框
    CodeLineGutter*           m_pGutter;     //编辑框左侧的行号栏
    balloonwjui::DuiButton*   m_pNotesButton;   //「批注」按钮
    balloonwjui::DuiLabel*    m_pUndoLink;   //状态文字右侧的「撤销」链接
    CodeNotesSplitter*        m_pSplit;      //代码区与批注栏之间的左右分栏
    CodeNotesPane*            m_pNotesPane;  //右侧的批注栏
};

} // namespace ballooncode
