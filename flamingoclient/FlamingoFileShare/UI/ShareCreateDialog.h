/**
 *  发起共享与调整成员对话框，ShareCreateDialog.h
 *
 *  docs/文件共享方案.md 7.4「发起共享对话框」。同一个窗口类有两种用法：
 *    - 发起共享（OpenForCreate）：选共享的对象（磁盘弹出本机驱动器菜单、文件夹与文件用系统的选择框），选成员与逐人的
 *      五个写权限（单文件共享不显示权限列），可勾选「包含隐藏文件和系统文件」；对象落在敏感位置时确认前弹一次风险提示；
 *      点「发起共享」经 CShareService::CreateShare 发 2201，成功后关闭，被拒时在成员表里标出不满足条件的成员并显示原因；
 *    - 调整成员（OpenForEdit，供「文件共享」窗口使用）：共享对象与「包含隐藏文件」不可改，按已有成员预先勾选，
 *      点「保存」时与原成员比较，经 CShareService::UpdateMembers 发 2204；
 *    - 按选好的对象发起（OpenForTargets，2026-10-03 起，「文件共享」窗口的「发起共享」）：成员就是在宿主选人窗口里选好的人，
 *      不向宿主要会话成员。提交时勾选的是 1 个人就在与他的单聊里发起；多个人就先经宿主 CreateShareGroup 建群，
 *      建群结果（OnShareGroupCreated）到达后在新群里发起；群建好后共享被拒时重试仍在这个群里发起，不再建群。
 *      发起成功后经宿主 OpenConversation 打开对应的单聊或群聊。
 *  共享名称（2026-10-04 起）：「共享内容」下面一行是「共享名称」输入框，分享者可以起名字，不填时用默认名称
 *  「XXX的文件共享」（提示文字就是它）。分享者以外的人只看到这个名字，看不到真实路径；调整成员时显示当前名称、不可改。
 *  候选成员经宿主的 RequestConversationMembers 取得（异步，由 CFileShareModule 转交 OnConversationMembers）：
 *  单聊只有对方一人，固定勾选；群聊为群成员（不含本人）。表头每个权限列是一个勾选框，即「全选此列」。
 *
 *  成员行（2026-10-03 起）依次是勾选框、头像（宿主给的，与主面板联系人列表同一口径，见 ShareAvatar.h）、名字、五个写权限；
 *  点名字等于点勾选框。
 *  控件全部用 balloonui 现成的：勾选框是 Checkbox 风格的 DuiButton，头像是 DuiAvatar，名字是可点击的 DuiLabel
 *  （balloonmain::CClickableLabel），成员表是 DuiScrollView 装 DuiVBox，
 *  「磁盘」的下拉是 DuiMenu（落点由 DuiMenu 自己限制在工作区内，UI 约定第 3 条）。成员行在共享类型变化、
 *  候选成员到达、服务端标出不满足条件的成员时整体重建。
 *
 *  本文件遵循 balloonui 头文件约定：包含它之前调用方须已引入 ATL/WTL（本工程的 stdafx.h）。
 *
 *  典型用法（CFileShareModule::OpenCreateShare）：
 *      dialog.OpenForCreate(owner, &m_service, m_host, ctype, cid);
 *      ...
 *      dialog.OnConversationMembers(code, members, count);   //宿主回送成员时
 *
 *  balloonwj@qq.com   2026-10-03
 */
#pragma once

#include <stdint.h>
#include <string>
#include <vector>

#include "../../../third-party/balloonui/balloonui/Controls/Window/DuiFrameWindow.h"
#include "../../../third-party/balloonui/balloonui/DuiNotify.h"
#include "../../Source/UI/BalloonMain/TitleBarIcon.h"   //ProgramWindowIcons：标题栏与任务栏的程序图标
#include "../FileShareApi.h"
#include "ShareAvatar.h"   //CShareAvatarSource：成员行的头像
#include "../Engine/ShareCreateModel.h"
#include "../Engine/ShareService.h"   //IShareManageObserver

namespace balloonwjui {
class DuiHBox;
class DuiLabel;
class DuiButton;
class DuiEdit;
class DuiVBox;
class DuiScrollView;
}

namespace fileshare {

/**
 *  CShareCreateDialog —— 发起共享与调整成员对话框（非模态，带所有者）。只在 UI 线程使用。
 */
class CShareCreateDialog : public balloonwjui::DuiFrameWindow, public IShareManageObserver
{
public:
    CShareCreateDialog();
    ~CShareCreateDialog();

    /**
     *  以「发起共享」打开；已打开时只前置。
     *  @param owner   所有者窗口（通常是聊天窗），可为 NULL；不转移所有权
     *  @param service 业务总控（不拥有，须在本窗口关闭之前一直有效），不可为 NULL
     *  @param host    宿主（取会话成员与显示名；不拥有），不可为 NULL
     *  @param ctype   会话类型：1 单聊 2 群聊
     *  @param cid     会话 id：单聊为对方 userid，群聊为群 id
     *  @return 窗口已显示返回 true；参数非法或创建失败返回 false
     */
    bool OpenForCreate(HWND owner, CShareService* service, IFileShareHost* host, int ctype, int64_t cid);

    /**
     *  以「调整成员」打开；已打开时只前置。
     *  @param owner   所有者窗口，可为 NULL
     *  @param service 业务总控（不拥有），不可为 NULL
     *  @param host    宿主（不拥有），不可为 NULL
     *  @param share   要调整的共享（「我的共享」列表里的一项，带全部成员与权限）
     *  @return 窗口已显示返回 true
     */
    bool OpenForEdit(HWND owner, CShareService* service, IFileShareHost* host, const ShareListEntry& share);

    /**
     *  以「按选好的对象发起」打开（「文件共享」窗口的「发起共享」）；已打开时只前置。成员表就是选好的人，全部勾选，
     *  逐人勾选写权限；只选了 1 人时他固定勾选，会话就是与他的单聊；选了多人时会话待定，提交时再决定（见文件头）。
     *  @param owner   所有者窗口（「文件共享」窗口），可为 NULL；不转移所有权
     *  @param service 业务总控（不拥有，须在本窗口关闭之前一直有效），不可为 NULL
     *  @param host    宿主（建群、打开聊天窗；不拥有），不可为 NULL
     *  @param targets 选好的人（userid 与显示名，不含本人），至少 1 人
     *  @return 窗口已显示返回 true；参数非法或创建失败返回 false
     */
    bool OpenForTargets(HWND owner, CShareService* service, IFileShareHost* host, const std::vector<ShareMemberRow>& targets);

    /**
     *  设置以「发起共享」打开时，发起成功后是否经宿主打开（前置）所在会话的聊天窗（2026-10-03 起）。从「文件共享」窗口
     *  发起时为 true，卡片就在那个聊天窗里；从聊天窗的浮层或会话右键菜单发起时为 false。在 OpenForCreate 之前调用，
     *  对已打开的对话框同样生效（以最近一次设置为准）。按选好的对象发起时总是打开，不看这个设置。
     *  @param open 是否打开
     */
    void SetOpenConversationOnSuccess(bool open);

    /**
     *  宿主回送代为建群的结果（按选好的对象发起、勾选了多人时）。成功时在新群里接着发起共享；失败时显示原因，可以重试。
     *  不在等待建群时调用无效果。
     *  @param code    0 成功，否则为服务端的错误码
     *  @param groupId 新群的群 id；失败时为 0
     */
    void OnShareGroupCreated(int code, int64_t groupId);

    /**
     *  关闭窗口：不再接收管理请求的结果（请求本身照常进行）、销毁 HWND。窗口未创建时调用是安全的。
     */
    void Close();

    /**
     *  宿主回送了会话成员。会话与本窗口不符、或本窗口未打开时忽略。
     *  @param ctype   会话类型
     *  @param cid     会话 id
     *  @param code    0 成功，否则为拉取失败的错误码
     *  @param members 成员数组（只在本次调用期间有效），含本人
     *  @param count   成员数
     */
    void OnConversationMembers(int ctype, int64_t cid, int code, const FileShareMemberInfo* members, uint32_t count);

    //IShareManageObserver：发起或调整的结果
    virtual void OnShareManageResult(const ShareManageResult& result) override;

    /**
     *  设置要共享的对象（界面上的三个选择按钮选好后调用，测试直接调用）。只在「发起共享」时有效。
     *  @param path  本机路径：磁盘为 "X:\"，文件夹与文件为完整路径
     *  @param kind  共享类型，share_kind
     *  @param label 磁盘的卷标（其余类型传空串）
     */
    void SetShareObject(const std::wstring& path, int kind, const std::wstring& label);

    /**
     *  确认（「发起共享」或「保存」按钮，测试直接调用）。
     *  @param riskConfirmed 为真时不弹敏感位置的风险提示（视为已确认）；按钮传假
     *  @return 已发出请求返回 true；校验不通过（没选对象、没选成员、没有变化）、用户在风险提示里取消或发送失败返回 false，
     *          原因显示在底部的提示行
     */
    bool Submit(bool riskConfirmed);

    //成员表（测试经它勾选成员与权限，改完调 RefreshMemberRows 同步界面）
    CShareMemberTable& GetMemberTable();

    //按成员表重建成员行与表头勾选框
    void RefreshMemberRows();

    /**
     *  宿主通知有用户的头像变了（2026-10-03 起）：窗口打开着时重建成员行，重建时重新向宿主取各人的头像。勾选与权限保留
     *  （它们在成员表里，不在控件上）。窗口没打开时不做任何事。
     */
    void RefreshAvatars();

    //底部提示行的文字（测试用）
    std::wstring GetStatusText() const;

    /**
     *  填写「共享名称」框（测试用，相当于用户输入；2026-10-04 起）。窗口没打开时不做任何事。
     *  @param name 名称框里的文字；可以为空或只有空白，发起时按 ShareCreateModel::ResolveShareName 处理
     */
    void SetShareNameText(const std::wstring& name);

    /**
     *  「共享名称」框的当前文字与提示文字（测试用）。提示文字就是没起名字时使用的默认名称。
     *  @param text        [出参] 框里的文字
     *  @param placeholder [出参] 提示文字
     *  @param enabled     [出参] 框是否可编辑（调整成员时不可改名）
     *  @return 窗口没打开时返回 false，出参不变
     */
    bool GetShareNameState(std::wstring& text, std::wstring& placeholder, bool& enabled) const;

    //是否正在等待服务端应答或代为建群的结果（测试用）
    bool IsBusy() const;

    //所属会话（模块据此转交会话成员）
    int GetCtype() const;
    int64_t GetCid() const;

    BEGIN_MSG_MAP(CShareCreateDialog)
        MESSAGE_HANDLER(WM_CLOSE, OnCloseMsg)
        MESSAGE_HANDLER(WM_DUI_NOTIFY, OnDuiNotify)
        MESSAGE_HANDLER(WM_DPICHANGED, OnDpiChangedMsg)
        CHAIN_MSG_MAP(balloonwjui::DuiFrameWindow)
    END_MSG_MAP()

private:
    //WM_CLOSE：点标题栏关闭按钮或按 Alt+F4 时关闭窗口
    LRESULT OnCloseMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

    //WM_DUI_NOTIFY：按钮点击与勾选框状态变化
    LRESULT OnDuiNotify(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

    //WM_DPICHANGED：窗口挪到 DPI 不同的显示器上时，按新 DPI 的字体重新确定权限列宽并重建成员行；不拦截消息，基类照常处理
    LRESULT OnDpiChangedMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

    //两种用法共用的建窗部分；成功返回 true
    bool CreateCommon(HWND owner, const wchar_t* title);

    //构建客户区控件树。每次创建窗口后调用一次
    void BuildContent();

    //按 font（表头勾选框所用的字体）量出权限列宽，存入 m_permColumnWidth 并更新表头五列的宽度；成员行的列宽要等
    //下一次 RefreshMemberRows 重建时才生效
    void UpdatePermColumnWidth(HFONT font);

    //「磁盘」：弹出本机驱动器菜单；「文件夹」「文件」：弹出系统的选择框
    void OnPickDisk();
    void OnPickFolderOrFile(bool folder);

    //成员行或表头的勾选框变化
    void OnMemberCheckChanged(UINT ctrlId, bool checked);

    //点了成员行的名字：等于点该成员的勾选框，切换是否勾选（固定成员不变）。row 为成员表里的行号
    void OnMemberNameClicked(size_t row);

    //按成员表同步各勾选框的勾选与可用状态（不重建控件）
    void SyncCheckStates();

    //刷新共享对象一行的文字与「包含隐藏文件」的显隐
    void UpdateObjectRow();

    //设置底部提示行；isError 为真时用红字
    void SetStatus(const std::wstring& text, bool isError);

    //把候选成员装进成员表（发起时单聊对方固定勾选；调整时按已有成员预先勾选）
    void ApplyCandidates(const std::vector<ShareMemberRow>& candidates);

    //按选好的对象发起、勾选了多人：请宿主建群（成员是勾选的人），进入等待。请求发不出时显示原因。返回是否已发出
    bool RequestGroup(const std::vector<ShareMemberPerm>& members);

    //结果码对应的提示文字（没有应答时说服务器没有响应）
    static std::wstring ResultText(int code);

    //实际使用的共享名称：按「共享名称」框里的文字经 ShareCreateModel::ResolveShareName 得出，没填时为默认名称。
    //发起共享与多人共享建群的群名都用它（2026-10-04 起不再用取自真实路径的 DisplayName）
    std::wstring CurrentShareName() const;

    //一行成员的控件
    struct MemberRowCtrls
    {
        balloonwjui::DuiButton* m_check;                                 //成员勾选框（文字为显示名）
        balloonwjui::DuiButton* m_perms[kShareMemberPermColumnCount];     //五个写权限勾选框；单文件共享时为 NULL
    };

private:
    CShareService*               m_service;       //业务总控（不拥有）；窗口打开期间有效
    IFileShareHost*              m_host;          //宿主（不拥有）；窗口打开期间有效
    bool                         m_editMode;      //是否为「调整成员」
    bool                         m_targetMode;    //是否为「按选好的对象发起」（「文件共享」窗口的「发起共享」）
    bool                         m_waitingGroup;  //按选好的对象发起时，是否已请宿主建群、正在等待结果
    bool                         m_groupCreated;  //按选好的对象发起时，群是否已建好（建好后 m_cid 为该群，重试不再建群）
    bool                         m_openConversationOnSuccess; //以「发起共享」打开时，成功后是否打开所在会话的聊天窗（见 SetOpenConversationOnSuccess）
    int                          m_ctype;         //会话类型
    int64_t                      m_cid;           //会话 id
    int64_t                      m_shareId;       //调整成员时的共享 id；发起时为 0
    std::wstring                 m_path;          //共享对象的本机路径；发起时未选为空
    int                          m_kind;          //共享类型；发起时未选为 0
    std::wstring                 m_label;         //磁盘的卷标
    std::wstring                 m_selfName;      //分享者本人的名字（昵称，昵称为空时为账号），默认共享名称「XXX的文件共享」用它；
                                                  //每次打开窗口时向宿主取，取不到时为空（默认名称改为「我的文件共享」）
    std::string                  m_editName;      //调整成员时共享的名称（UTF-8），显示在不可编辑的「共享名称」框里
    std::string                  m_editDisplayPath; //调整成员时共享的显示路径（UTF-8）
    std::vector<ShareMemberPerm> m_before;        //调整成员时的原成员与权限
    CShareMemberTable            m_table;         //成员与逐人权限
    bool                         m_membersLoaded; //候选成员是否已到达
    uint32_t                     m_requestId;     //正在等待结果的管理请求编号；没有时为 0
    std::vector<MemberRowCtrls>  m_rowCtrls;      //各成员行的控件，与 m_table.Rows() 一一对应
    int                          m_permColumnWidth; //表头与成员行每个权限列的宽度，单位：像素；建窗与 DPI 变化时按字体量出

    //以下控件归窗口的控件树所有，窗口创建后有效、销毁后失效
    balloonwjui::DuiVBox*        m_root;          //客户区根容器
    balloonwjui::DuiLabel*       m_objectLabel;   //共享对象的路径与类型
    balloonwjui::DuiHBox*        m_pickRow;       //共享对象一行（调整成员时隐藏三个选择按钮后对它 ForceLayout）
    balloonwjui::DuiButton*      m_pickDisk;      //「磁盘」
    balloonwjui::DuiButton*      m_pickFolder;    //「文件夹」
    balloonwjui::DuiButton*      m_pickFile;      //「文件」
    balloonwjui::DuiEdit*        m_nameEdit;      //「共享名称」输入框；提示文字为默认名称，调整成员时显示当前名称且不可编辑
    balloonwjui::DuiButton*      m_hiddenCheck;   //「包含隐藏文件和系统文件」
    balloonwjui::DuiLabel*       m_sectionLabel;  //成员表上方的分组标题（单文件共享时说明没有写权限）
    balloonwjui::DuiHBox*        m_header;        //成员表的表头行
    balloonwjui::DuiButton*      m_allCheck;      //表头「成员」：全选或全不选
    balloonwjui::DuiButton*      m_columnChecks[kShareMemberPermColumnCount];   //表头各权限列的「全选此列」
    balloonwjui::DuiScrollView*  m_scroll;        //成员表的滚动视口
    balloonwjui::DuiLabel*       m_status;        //底部提示行
    balloonwjui::DuiButton*      m_okButton;      //「发起共享」或「保存」
    balloonmain::ProgramWindowIcons m_programIcons;  //标题栏与任务栏 / Alt-Tab 的程序图标，本对象持有，跨关闭再打开复用
    CShareAvatarSource           m_avatars;       //成员行头像的来源与位图（位图归它所有，成员行的头像控件借用）；关闭窗口后释放位图
};

} // namespace fileshare
