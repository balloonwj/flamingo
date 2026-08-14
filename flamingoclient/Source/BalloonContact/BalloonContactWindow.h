/**
 *  BalloonContactWindow.h
 *
 *  联系人资料对话框 —— 基于 balloonui，对照 design/联系人.png 与
 *  design/_decoded 里的 ContactInfoDialog。居中模态风格的独立窗口，
 *  展示某个联系人的资料并提供：标签的增 / 改 / 删、备注名行内编辑、消息免打扰 /
 *  置顶聊天两个开关、分组下拉、「发消息」「关闭」两个底部
 *  按钮。
 *
 *  当前以 mock 数据（「江屿白」那一组）填充；真实联系人数据与「发消息」
 *  的真实跳转待后续接入。调试期由 CMainDlg 的 --contact 入口直接弹出。
 *
 *  本文件遵循 balloonui 头文件约定：包含它之前调用方须已引入 ATL/WTL
 *  （Flamingo 侧由 stdafx.h 保证）。
 *
 *  balloonwj@qq.com   2026-05-22
 */

#pragma once

#include <vector>

#include "../../../third-party/balloonui/balloonui/Controls/Window/DuiFrameWindow.h"
#include "ContactModel.h"
#include "TagEditPopup.h"

class CFlamingoClient;

// 「备注名」失焦提交的去抖自投消息。DUIN_KILLFOCUS 不能立即提交 —— 用户在编辑框内
// 点一下会让寄宿的 EDIT 短暂失焦再得焦，此刻立即提交就会当场退回查看态、字都打不了。
// 改为 PostMessage 本消息延后一拍，在 OnRemarkCommitDeferred 里查一下焦点：焦点又回到
// EDIT 说明人还在编辑、跳过；只有焦点真正离开才提交。做法与群资料对话框的
// WM_GROUPINFO_CARD_COMMIT 一致。值取 WM_APP 段，与系统消息不冲突。
#define WM_CONTACT_REMARK_COMMIT   (WM_APP + 0x22)

/**
 *  CBalloonContactWindow
 *
 *  联系人资料对话框窗口。继承 balloonwjui::DuiFrameWindow —— 由它提供
 *  无边框自绘窗体与标题栏。固定尺寸、不可缩放，标题栏只保留关闭按钮。
 */
class CBalloonContactWindow : public balloonwjui::DuiFrameWindow
{
public:
    /**
     *  构造函数。
     *    pFMGClient：所属客户端对象（消息中枢），本类只引用、不持有。
     */
    explicit CBalloonContactWindow(CFlamingoClient* pFMGClient);
    ~CBalloonContactWindow();

    /**
     *  创建并显示联系人资料对话框。
     *    hMainDlg：CMainDlg 的窗口句柄（隐藏的消息中枢）；关闭本窗口时
     *              据此通知 CMainDlg。
     */
    void CreateAndShow(HWND hMainDlg);

    /**
     *  用真实联系人数据打开（或刷新并前置）资料对话框。
     *    hostPanel ：主面板 HWND；窗口内"发消息"按钮经它路由打开单聊。
     *    peerUserId：联系人 userid。
     *    model     ：已填好真实数据的资料模型。
     *    beginRemarkEdit：true 表示打开后立刻让「备注名」那一行进入编辑态并聚焦，
     *                     供主面板右键菜单的「修改备注」直接用；默认 false（普通
     *                     "查看资料"，进来是只读态）。
     */
    void OpenForContact(HWND hostPanel, UINT peerUserId,
                        const ballooncontact::ContactModel& model,
                        bool beginRemarkEdit = false);

    /**
     *  当前正在展示的联系人 userid（0 = 未打开 / 调试入口）。主面板收到 1108 资料
     *  变更推送时据此判断是否要刷新本窗。
     */
    UINT GetPeerUserId() const { return m_peerUserId; }

    /**
     *  后台静默刷新当前展示的联系人资料（<u>不前置、不抢焦点</u>）。仅当窗口已打开时
     *  生效：用新数据重建客户区。用于收到 1108 资料变更推送、且本窗正展示该联系人时
     *  即时更新昵称 / 签名等。
     *    model  已按新值填好的资料模型。
     */
    void RefreshContactData(const ballooncontact::ContactModel& model);

    BEGIN_MSG_MAP(CBalloonContactWindow)
        MESSAGE_HANDLER(WM_CLOSE, OnCloseMsg)
        MESSAGE_HANDLER(WM_DUI_NOTIFY, OnDuiNotify)
        MESSAGE_HANDLER(WM_LBUTTONDOWN, OnLButtonDown)
        MESSAGE_HANDLER(WM_CONTACT_REMARK_COMMIT, OnRemarkCommitDeferred)
        CHAIN_MSG_MAP(balloonwjui::DuiFrameWindow)
    END_MSG_MAP()

private:
    // 构建客户区控件树、把 m_model 的数据灌进各控件。
    void InitUi();

    // 用 m_model 配置头部资料块（姓名 / 职务 / 账号 / 签名）与部门路径整宽区块，
    // 并把部门路径的折行行数记进 m_deptLineCount。
    void FillHeader();

    // 按 m_deptLineCount 与 m_tagRowCount 把窗口调到该有的高度
    //（SetMinSize + ResizeClient），再重排一次客户区控件树。
    //
    // 部门路径区块与标签区的高度都随内容多少变化，窗口高度必须跟着变，否则多出
    // 来的那几行会把底部按钮挤出窗口。两者的高度必须由同一次计算得出 —— 各算各
    // 的、各自 ResizeClient 的话，后调的那一次会把先调的那一次的增量抹掉。创建、
    // 切换联系人、收到资料变更推送、增删标签这几条路径都要调，少调一处就会在那
    // 条路径上重现按钮被挤出去的问题。
    void ApplyBodyHeight();

    // 按当前联系人刷新窗口标题，形如「韩晓燕888的资料」。名字取真名而非备注名 ——
    // 标题是用来认窗口的，真名比各人自己起的备注更稳定。名字为空时（调试入口或
    // 数据缺失）退回通用标题「联系人资料」，不要出现「的资料」这样缺主语的标题。
    void UpdateWindowTitle();

    // 用 m_model 的标签列表重建标签区的胶囊（连同末尾「+ 添加标签」按钮，标签数
    // 已达上限时改为一行说明文字），重算标签行数并把窗口调到该有的高度。
    void RebuildTags();

    // 用 m_model 的分组列表填充下拉框并选中当前分组。
    void FillGroupCombo();

    // 用 m_model 的开关状态同步三个 DuiSwitch。
    void FillSwitches();

    // 进入 / 退出「备注名」行内编辑态。进入时显示文本框、隐藏当前值与
    // 铅笔，并把 Win32 键盘焦点直接交给文本框寄宿的 EDIT 子窗（只调
    // DuiControl::SetFocus 是不够的，见函数实现里的说明）；退出时反之。
    //   editing：true 进入编辑态；false 退出编辑态。
    void SetRemarkEditing(bool editing);

    /**
     *  提交「备注名」编辑框的内容。
     *
     *  有宿主主面板（正常登录态）时<u>不在本窗口判定</u>该不该发：把用户敲进去的
     *  原始文本回投给主面板（FMG_MSG_SET_CONTACT_REMARK），由它去比对原值、检查
     *  连接态、发 cmd_set_friend_remark 并在失败时回滚 —— 通讯录缓存与 socket 都
     *  在它手上，判定与执行放一处才不会两边规则走岔。主面板处理完会回头调
     *  RefreshContactData 把本窗刷成最终状态。
     *
     *  无宿主主面板（--contact 调试入口）时退化为就地改内存并刷新，好让这条交互
     *  在调试入口下仍能走通、能目检。
     */
    void CommitRemarkEdit();

    // 按 m_model 当前的备注名刷新「备注名」行的显示值与头部（备注会影响头部主名
    // 与括号真名）。备注为空时该行显示"未设置"占位。
    void RefreshRemarkDisplay();

    // 处理某枚标签胶囊的「×」删除点击：把该标签从列表里去掉并提交整份列表。
    //   chipCtrlId：被点击胶囊的 ctrlId（kIdTagChipBase + 标签下标）。
    void OnTagDeleteClicked(UINT chipCtrlId);

    // 处理某枚标签胶囊<u>文字区</u>的点击：在这枚胶囊的位置上打开输入弹层，
    // 带入当前文字并全选，供用户就地改写。
    //   chipCtrlId：被点击胶囊的 ctrlId（kIdTagChipBase + 标签下标）。
    void OnTagEditClicked(UINT chipCtrlId);

    // 处理「+ 添加标签」点击：在按钮位置上打开一个空的输入弹层。
    void OnAddTagClicked();

    /**
     *  输入弹层提交回调的转接函数（C 风格回调没有 this）。
     *    userdata：注册时传入的 CBalloonContactWindow*。
     *    text：输入框里的原始文本（未去首尾空白、未做任何判定）。
     */
    static void OnTagPopupCommitThunk(void* userdata, const CString& text);

    /**
     *  输入弹层提交的实际处理：按 m_editingTagIndex 走"新增"还是"修改"的判定
     *  （ballooncontact::DecideTagAdd / DecideTagEdit），算出新的完整标签列表后
     *  交给 CommitTagList 提交；被判定拦下时弹一条轻量提示。
     *    text：输入框里的原始文本。
     */
    void OnTagPopupCommit(const CString& text);

    /**
     *  提交一份完整的标签列表。
     *
     *  有宿主主面板（正常登录态）时<u>不在本窗口判定</u>该不该发：把整份列表回投
     *  给主面板（FMG_MSG_SET_CONTACT_TAGS），由它去比对原值、检查连接态、发
     *  cmd_set_contact_tags 并在失败时回滚 —— 通讯录缓存与 socket 都在它手上，
     *  判定与执行放一处才不会两边规则走岔。主面板处理完会回头调 RefreshContactData
     *  把本窗刷成最终状态。做法与备注名提交（CommitRemarkEdit）完全同构。
     *
     *  无宿主主面板（--contact 调试入口）时退化为就地改内存并重建标签区，好让这
     *  条交互在调试入口下仍能走通、能人工核对界面。
     *
     *    tags：改好的完整标签列表；空列表表示清空该联系人的全部标签。
     */
    void CommitTagList(const std::vector<CString>& tags);

    /**
     *  把某个标签控件的矩形换算成屏幕矩形，供输入弹层精确盖在它上面。
     *    ctrlId：目标控件的 ctrlId（某枚胶囊或「+ 添加标签」按钮）。
     *    outRectScreen：出参，该控件的屏幕矩形。
     *  返回：true 表示找到了控件且矩形有效；false 表示没找到，调用方应放弃本次操作。
     */
    bool GetTagCtrlScreenRect(UINT ctrlId, RECT& outRectScreen);

    // 某个开关翻转后，把新状态写回 m_model。
    //   switchCtrlId：被翻转开关的 ctrlId；on：翻转后的状态。
    void OnSwitchToggled(UINT switchCtrlId, bool on);

    // 「发消息」按钮（当前为 mock：弹框占位 + 打日志）。
    void DoSendMessage();

    // WM_CLOSE：调试入口下关闭即结束程序，转交 CMainDlg。
    LRESULT OnCloseMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

    // WM_DUI_NOTIFY：处理标签删除 / 添加、备注名编辑、开关翻转、按钮点击。
    LRESULT OnDuiNotify(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

    // WM_LBUTTONDOWN：备注名编辑态下点到子 EDIT 以外的 DUI 区域时，把焦点收回
    // 宿主窗口，逼子 EDIT 失焦，从而走"失焦提交"退出编辑态。balloonui 的 DUI 控件
    // 不是独立 HWND，点它们不会自动夺走子 EDIT 的 Win32 焦点，故须在此显式逼失焦。
    LRESULT OnLButtonDown(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

    // WM_CONTACT_REMARK_COMMIT（去抖自投）：备注名文本框失焦后延后一拍的提交检查。
    // 若焦点又回到了 EDIT 子窗（用户在框内重新点了一下）则跳过、仍留编辑态；
    // 焦点真正离开才提交。
    LRESULT OnRemarkCommitDeferred(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

private:
    CFlamingoClient* m_lpFMGClient;   // 所属客户端（消息中枢），只引用不持有
    HWND             m_hMainDlg;      // CMainDlg 句柄（隐藏的消息中枢）
    HWND             m_hostPanel;     // 主面板 HWND，"发消息"经它路由开单聊；0 表示调试入口
    UINT             m_peerUserId;    // 被查看联系人的 userid；0 表示调试入口(mock)
    ballooncontact::ContactModel m_model;   // 联系人资料数据模型
    bool             m_remarkEditing; // 「备注名」是否处于行内编辑态
    int              m_deptLineCount; // 部门路径整宽区块当前折了几行；0 = 该联系人没有部门。
                                      // 由 FillHeader 写入，ApplyBodyHeight 据它算窗口高度
    int              m_tagRowCount;   // 标签区当前占几行（已夹在 [1, kContactTagMaxRows] 内）。
                                      // 由 RebuildTags 写入，ApplyBodyHeight 据它算窗口高度
    ballooncontact::CTagEditPopup m_tagPopup;   // 标签输入弹层，随本窗口存活；未打开时只是个空壳
    int              m_editingTagIndex;         // 输入弹层当前在改哪一枚标签的下标；
                                                // -1 表示这次是"新增标签"而不是"修改标签"
    HBITMAP          m_titleIconBmp;   // 标题栏图标位图（程序图标合成到标题栏底色上）。
                                       // 本类持有，跨"关闭再打开"复用，析构时 DeleteObject
    HICON            m_hTaskIconBig;   // 任务栏 / Alt-Tab 大图标（本类持有，析构 DestroyIcon）
    HICON            m_hTaskIconSmall; // 任务栏 / Alt-Tab 小图标（本类持有，析构 DestroyIcon）
};
