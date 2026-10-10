/**
 *  BalloonContactWindow.h
 *
 *  联系人资料对话框 —— 基于 balloonui，对照 design/联系人.png 与
 *  design/_decoded 里的 ContactInfoDialog。居中模态风格的独立窗口，
 *  展示某个联系人的资料并提供：标签的增 / 改 / 删、备注名行内编辑、「消息提醒」
 *  下拉框、「添加到收藏」开关、分组下拉、语音通话 / 视频通话 / 历史消息 /
 *  组织架构四个快捷操作按钮、「发消息」「关闭」两个底部按钮。
 *
 *  本窗口自己既没有 socket 也没有通讯录 / 收藏 / 提醒模式这几份缓存，所以提醒
 *  模式、收藏与四个快捷操作一律不在本窗口执行，只把用户的意图回投给主面板
 *  （FMG_MSG_SET_CONTACT_* / FMG_MSG_LOCATE_CONTACT / FMG_MSG_CALL_* /
 *  FMG_MSG_OPEN_HISTORY），由它判定与执行 —— 与备注名、标签的提交是同一套做法。
 *
 *  调试期由命令行 --contact 入口直接弹出，此时没有宿主主面板，各动作
 *  退化为只改内存并记一条日志。
 *
 *  2026-10-07 起一人一窗、可以多个并存：主面板经 ballooncontact::CContactWindowSet（ContactWindowSet.h）
 *  按 userid 管理全部资料窗，再次打开同一个人时调 ActivateExisting 激活已有窗口，不重建界面。本人资料也由
 *  本窗口承担（本人资料对话框已删除）：看本人时「个人资料」区块第一格是可就地编辑的昵称，头像可点击打开
 *  修改头像对话框（FMG_MSG_OPEN_MODIFY_AVATAR 回投主面板）。「备注名」与各资料格的行内编辑都支持回车提交、
 *  Esc 取消、失焦提交。
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

// 「备注名」失焦提交的去抖自投消息。DUIN_KILLFOCUS 不能立即提交 —— 早先文本框内嵌
// 真 Win32 输入框子窗口时，用户在框内点一下会让它短暂失焦再得焦，此刻立即提交就会
// 当场退回查看态、字都打不了。改为 PostMessage 本消息延后一拍，在
// OnRemarkCommitDeferred 里查一下焦点：焦点仍在文本框上说明人还在编辑、跳过；只有
// 焦点真正离开才提交。文本框现已是无窗口控件，这道去抖作为保险继续保留。做法与群
// 资料对话框的 WM_GROUPINFO_CARD_COMMIT 一致。值取 WM_APP 段，与系统消息不冲突。
#define WM_CONTACT_REMARK_COMMIT   (WM_APP + 0x22)

// 「个人资料」格的输入框失去焦点后，延后一拍再判定是否提交（原理同 WM_CONTACT_REMARK_COMMIT）；
// wParam = 发出时的编辑轮次号（m_profileEditGeneration），已换到别的格子或已结束编辑时忽略
#define WM_CONTACT_PROFILE_COMMIT  (WM_APP + 0x23)

// 看本人时点了手机号或座机格的公开范围开关（「仅自己可见 ▾」）：等这次鼠标事件处理完再弹菜单
// （模态菜单在鼠标事件里弹会打乱鼠标捕获）；wParam = 格子的槽位（2026-10-06）
#define WM_CONTACT_VISIBILITY_MENU (WM_APP + 0x24)

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
     *    hMainDlg：主面板的窗口句柄；关闭本窗口时
     *              据此请求退出程序。
     */
    void CreateAndShow(HWND hMainDlg);

    /**
     *  用真实联系人数据打开（或刷新并前置）资料对话框。2026-10-07 起主面板经 CContactWindowSet 只对新建
     *  的窗口调用本函数（一人一窗），已开着的窗口改调 ActivateExisting，不再在同一个窗口里换人重建。
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

    /**
     *  只刷新「消息提醒」下拉框：把模式写进 m_model 并静默改选中项（不发通知），不重建
     *  客户区，因而不会打断正在进行的备注名编辑。主面板在模式表变化后（本机修改、多端
     *  同步推送、会话列表到达）以及特别关注设置被本地拒绝后调用。窗口未打开时只更新模型。
     *    mode：与该联系人单聊会话的提醒模式（Protocol.h 的 enum conversation_notify_mode）。
     */
    void ApplyNotifyMode(int mode);

    /**
     *  只刷新头部头像的状态点与灰彩（离线用户头像置灰，2026-10-03）：写进 m_model 后重新配置
     *  头像控件，不重建客户区，因而不会打断正在进行的备注名编辑。主面板在本窗口展示的联系人
     *  上线 / 下线时调用；展示的是本人时，在本端连接态变化时调用。窗口未打开时只更新模型。
     *    status：    在线状态，取值见 ballooncontact::ContactStatus。
     *    avatarGray：头像是否画成灰度，由主面板按 balloonmain::ShouldGrayUserAvatar 算出。
     */
    void ApplyPeerPresence(int status, bool avatarGray);

    /**
     *  把当前的界面连接态推给本窗口，供快捷操作行的离线门禁使用。
     *
     *  本窗口不自己判断连不连得上服务器：连接态由会话控制器统一算出、经主面板逐窗
     *  推下来，与托盘图标、状态胶囊、各聊天窗同源（做法与
     *  CBalloonChatWindow::ApplyConnectionState 一致）。宿主主面板应当在打开本窗口
     *  之后、以及此后每次连接态变化时各调用一次。
     *
     *    state：界面连接态，取值见 balloonmain::ConnectionUiState
     *           （0=在线 1=正在连接 2=离线）。越界值按"未就绪"处理，一律拦住。
     *
     *  副作用：只更新成员，不重绘、不弹任何提示。
     */
    void ApplyConnectionState(int state);

    /**
     *  记下新窗口摆放时参照的「上一个资料窗」位置（2026-10-07）。须在 OpenForContact 建窗之前调用；建窗时
     *  按 ballooncontact::ComputeContactWindowCascade 摆放：有上一个窗口时相对它错开，否则在工作区居中。
     *  不调用时等同于没有上一个窗口。
     *    hasPrevious：    是否有上一个仍开着的资料窗。
     *    previousTopLeft：上一个资料窗的左上角（屏幕坐标），hasPrevious 为 false 时不用。
     */
    void SetCascadeAnchor(bool hasPrevious, POINT previousTopLeft);

    /**
     *  激活已经开着的本窗口（再次打开同一个人时调用，2026-10-07）：最小化时还原，再前置。不重建界面，
     *  正在编辑的备注名或资料格的内容保留。
     *    beginRemarkEdit：true 表示右键菜单「修改备注」入口，激活后让「备注名」进入编辑态（已在编辑时只把
     *                     焦点交回输入框，内容不变；看本人时没有备注名，忽略）。
     *  副作用：窗口没开时什么都不做。
     */
    void ActivateExisting(bool beginRemarkEdit);

    /**
     *  只更新头部头像（2026-10-07）：把新的头像来源写进模型，来源变了时重新生成头像位图并挂到头像控件上，
     *  不重建客户区，因而不会打断正在进行的编辑。主面板在本人改头像成功、图片头像下载完成后调用。
     *    source：新的头像来源（见 ballooncontact::ContactAvatarSource）。
     *  副作用：窗口没开时只更新模型。
     */
    void ApplyAvatarSource(const ballooncontact::ContactAvatarSource& source);

    /**
     *  「备注名」当前是否处于行内编辑态。供单元测试与主面板查询，无副作用。
     *  返回：在编辑态返回 true。
     */
    bool IsRemarkEditing() const { return m_remarkEditing; }

    /**
     *  「个人资料」区块里正在就地编辑的格子槽位。供单元测试查询，无副作用。
     *  返回：槽位，取值 [0, ballooncontact::kContactProfileSlotCount)；-1 表示没有在编辑。
     */
    int GetEditingProfileSlot() const { return m_profileEditSlot; }

    /**
     *  当前展示的资料模型（只读）。供单元测试与主面板查询，无副作用。
     *  返回：模型引用，生命周期同本窗口对象。
     */
    const ballooncontact::ContactModel& GetModel() const { return m_model; }

    /**
     *  提示框的替换函数类型：本窗口需要给用户一句话提示（资料格不合格、离线拦截）时调用它，而不是弹模态框。
     *    owner：本窗口句柄；text：提示文字。
     */
    typedef void (*PromptHook)(HWND owner, LPCTSTR text);

    /**
     *  换上提示框的替换函数（单元测试在不可见桌面上运行，模态框没有人点，会一直卡住）。正式程序不调用，
     *  保持为 NULL，提示照常用 MessageBox 弹出。
     *    hook：替换函数；NULL 表示恢复为 MessageBox。
     *  副作用：对本进程里所有资料窗生效。
     */
    static void SetPromptHook(PromptHook hook);

    BEGIN_MSG_MAP(CBalloonContactWindow)
        MESSAGE_HANDLER(WM_CLOSE, OnCloseMsg)
        MESSAGE_HANDLER(WM_DUI_NOTIFY, OnDuiNotify)
        MESSAGE_HANDLER(WM_LBUTTONDOWN, OnLButtonDown)
        MESSAGE_HANDLER(WM_CONTACT_REMARK_COMMIT, OnRemarkCommitDeferred)
        MESSAGE_HANDLER(WM_CONTACT_PROFILE_COMMIT, OnProfileCommitDeferred)
        MESSAGE_HANDLER(WM_CONTACT_VISIBILITY_MENU, OnVisibilityMenuMsg)
        CHAIN_MSG_MAP(balloonwjui::DuiFrameWindow)
    END_MSG_MAP()

private:
    // 构建客户区控件树、把 m_model 的数据灌进各控件。
    void InitUi();

    // 用 m_model 配置头部资料块（姓名 / 职务 / 账号 / 签名）与部门路径整宽区块，
    // 并把部门路径的折行行数记进 m_deptLineCount。
    void FillHeader();

    // 按模型填「个人资料」区块（看本人时全部展开，看别人时按 m_profileExpanded 决定收起与否），
    // 并记下显示的行数与各槽位的字段。调用后须 ApplyBodyHeight 重算窗口高度并重排。
    void FillProfile();

    // 点了某个格子的编辑按钮：已有别的格子在编辑时先提交它（提交不合格就留在原格子），再让这一格
    // 进入就地编辑并把焦点交给输入框或下拉框。
    //   slot：槽位，取值 [0, ballooncontact::kContactProfileSlotCount)。
    void BeginProfileEdit(int slot);

    // 提交正在编辑的格子（回车、失去焦点或下拉框选中时调用）：没改动直接退出编辑；不合格时提示并
    // 留在编辑态；有改动且合格时退出编辑、先把新值显示出来，再交主面板发改资料请求。
    void CommitProfileEdit();

    // 取消正在编辑的格子（Esc）：退出编辑，恢复显示原值。
    void CancelProfileEdit();

    // 退出就地编辑（不提交）并重排。
    void EndProfileEdit();

    // 判断某个控件 id 是不是「个人资料」格子的某个部件，是的话给出槽位。
    //   ctrlId：控件 id；part：部件，取值见 ballooncontact::ContactProfileCellPart；
    //   outSlot：[出参] 槽位。返回：是该部件时为 true。
    static bool IsProfilePartId(UINT ctrlId, int part, int& outSlot);

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

    // 用 m_model 的开关状态同步各 DuiSwitch（目前只有「添加到收藏」）。
    void FillSwitches();

    // 用 m_model 的提醒模式置位「消息提醒」下拉框（静默，不发通知）。
    void FillNotifyCombo();

    // 进入 / 退出「备注名」行内编辑态。进入时显示文本框、隐藏当前值与
    // 铅笔，并把焦点交给文本框、全选其内容（文本框是无窗口控件，一次
    // SetFocus 即可同时拿到 DUI 焦点与宿主窗口的键盘焦点，见函数实现里的
    // 说明）；退出时反之。
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

    // 取消「备注名」的行内编辑（Esc）：退出编辑态、不提交，当前值仍显示原备注；下次进入编辑时输入框
    // 重新填入原备注，这次输入的内容丢弃。
    void CancelRemarkEdit();

    // 行内编辑按回车或 Esc 退出之后，把 DUI 焦点从已隐藏的输入框上摘掉，后续按键不再送进看不见的输入框。
    // 输入框随之发出的失焦通知因已不在编辑态而被忽略。
    void ReleaseHiddenEditFocus();

    // 按 m_model 当前的备注名刷新「备注名」行的显示值与头部（备注会影响头部主名
    // 与括号真名）。备注为空时该行显示"未设置"占位。
    void RefreshRemarkDisplay();

    // 点了头部头像：看本人且有宿主主面板时，先过离线门禁，再请主面板打开修改头像对话框
    // （FMG_MSG_OPEN_MODIFY_AVATAR）；看别人或调试入口下只记日志。
    void OnAvatarClicked();

    // 模型的头像来源与已生成位图所依据的来源不同时，重新生成头像位图（彩色一张，灰度一张按需生成），
    // 并挂到头像控件上；来源无效时摘掉位图，头像控件按字形与底色自己画。
    void EnsureAvatarBitmaps();

    // 按模型的灰度标记把彩色或灰度头像位图挂到头像控件上（灰度位图第一次用到时生成）。
    void ApplyAvatarBitmapToControl();

    // 释放持有的头像位图（彩色与灰度两张）。调用前须确保头像控件已不再引用它们。
    void ReleaseAvatarBitmaps();

    // 建窗后把窗口摆到位：按 SetCascadeAnchor 记下的上一个窗口位置错开，没有时在工作区居中，并保证落在
    // 工作区内（显示器取上一个窗口所在的、主面板所在的或主显示器，依次退而求其次）。
    void PlaceInitially();

    // 给用户一句话提示：换上了替换函数（SetPromptHook）时交给它，否则弹 MessageBox。
    //   text：提示文字。
    void ShowPrompt(LPCTSTR text);

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

    /**
     *  某个开关翻转后：先把新状态写回 m_model，再把<u>目标状态</u>回投给主面板。
     *
     *  与备注名、标签的提交同构 —— 本窗口不判定该不该发、也不执行，收藏的缓存与
     *  socket 都在主面板手上。回投的是目标状态而不是
     *  "取反"：用户连点两下而第一条应答还在路上时，按取反算会被同一个旧状态取反两次。
     *  无宿主主面板（--contact 调试入口）时退化为只改内存并记一条日志。
     *
     *    switchCtrlId：被翻转开关的 ctrlId（目前只有 kIdSwitchFavorite，其余值忽略）。
     *    on：翻转后的状态，true 开 / false 关。
     */
    void OnSwitchToggled(UINT switchCtrlId, bool on);

    /**
     *  「消息提醒」下拉框选了一项后：换算成提醒模式写回 m_model，再把<u>目标模式</u>回投
     *  给主面板（FMG_MSG_SET_CONTACT_NOTIFY_MODE），由它检查特别关注上限、更新模式表与
     *  各列表、发协议。与当前模式相同时什么也不做。无宿主主面板（--contact 调试入口）时
     *  退化为只改内存并记一条日志。
     *    index：下拉框的新选中下标（DUIN_VALUECHANGED 的 extra）；负值忽略。
     */
    void OnNotifyModeChosen(int index);

    /**
     *  快捷操作行四个按钮的统一处理：语音通话 / 视频通话 / 历史消息 / 组织架构。
     *
     *  四者都只是把动作回投给主面板，由它去发信令、开历史记录窗、定位通讯录。
     *  语音与视频还要先过离线门禁（通话要走信令通道，离线时发起必然失败）。
     *  「组织架构」投完之后会关闭本窗口 —— 资料窗是居中的独立顶层窗口，会盖住
     *  主面板，不关掉的话用户看不见定位结果。
     *
     *    ctrlId：被点击按钮的 ctrlId（kIdBtnVoice / kIdBtnVideo / kIdBtnHistory /
     *            kIdBtnLocate 四者之一，其余值忽略）。
     */
    void OnQuickActionClicked(UINT ctrlId);

    /**
     *  离线门禁：判定某类操作在当前连接态下是否放行，被拦住时弹一句提示。
     *
     *  判定本身交给 balloonmain::IsActionAllowedInState，不在本窗口就地写一份 ——
     *  规则只该有一处（见 BalloonMain/OnlineCapability.h 的说明）。做法与
     *  CBalloonChatWindow::CheckOnlineFor 完全一致。
     *
     *    action：操作类别，取值见 balloonmain::OnlineActionKind。
     *  返回：true 表示可以执行；false 表示已被拦住（并且已经弹过提示框）。
     */
    bool CheckOnlineFor(int action);

    // 「发消息」按钮：请主面板按 userid 打开单聊并关闭本窗口；调试入口下弹 mock 提示。
    void DoSendMessage();

    // WM_CLOSE：调试入口下关闭即请求退出程序。
    LRESULT OnCloseMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

    // WM_DUI_NOTIFY：处理标签删除 / 添加、备注名编辑、开关翻转、快捷操作与底部按钮点击。
    LRESULT OnDuiNotify(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

    // WM_LBUTTONDOWN：备注名编辑态下，按鼠标落点判断是否点在文本框之外；是则摘掉
    // 文本框的 DUI 焦点，从而走"失焦提交"退出编辑态。文本框是无窗口控件，点它与点
    // 别处的鼠标消息都到达本窗口，且它本身不持有 Win32 焦点，故须在此自行判定落点。
    LRESULT OnLButtonDown(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

    // WM_CONTACT_REMARK_COMMIT（去抖自投）：备注名文本框失焦后延后一拍的提交检查。
    // 若焦点又回到了备注名文本框（用户在框内重新点了一下）则跳过、仍留编辑态；
    // 焦点真正离开才提交。
    LRESULT OnRemarkCommitDeferred(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

    // WM_CONTACT_PROFILE_COMMIT：「个人资料」格的输入框失去焦点后延后一拍到达。焦点已回到输入框、
    // 编辑轮次号对不上或已不在编辑时忽略，否则提交。
    LRESULT OnProfileCommitDeferred(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

    // WM_CONTACT_VISIBILITY_MENU：在公开范围开关下方弹出「仅自己可见 / 对同事可见」菜单；选了另一项时
    // 先把新设置显示出来，再交主面板发改资料请求（与就地编辑同一条提交路径）。wParam = 格子的槽位。
    LRESULT OnVisibilityMenuMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

    // 把一次改资料要提交的字段先写进模型、刷新「个人资料」区块，再交主面板发请求（失败时主面板回滚并
    // 刷新本窗口）。就地编辑与公开范围开关共用。调用前须已确认在线。
    //   fields：要提交的字段。
    void SubmitProfileFields(const net::ProfileUpdateFields& fields);

private:
    CFlamingoClient* m_lpFMGClient;   // 所属客户端（消息中枢），只引用不持有
    HWND             m_hMainDlg;      // 主面板句柄
    HWND             m_hostPanel;     // 主面板 HWND，"发消息"经它路由开单聊；0 表示调试入口
    UINT             m_peerUserId;    // 被查看联系人的 userid；0 表示调试入口(mock)
    ballooncontact::ContactModel m_model;   // 联系人资料数据模型
    bool             m_remarkEditing; // 「备注名」是否处于行内编辑态
    int              m_deptLineCount; // 部门路径整宽区块当前折了几行；0 = 该联系人没有部门。
                                      // 由 FillHeader 写入，ApplyBodyHeight 据它算窗口高度
    int              m_tagRowCount;   // 标签区当前占几行（已夹在 [1, kContactTagMaxRows] 内）。
                                      // 由 RebuildTags 写入，ApplyBodyHeight 据它算窗口高度
    bool             m_profileExpanded; // 看别人时「个人资料」区块是否已点「展开全部」；每次 OpenForContact
                                        // 恢复为收起，资料变更推送触发的刷新保持不变
    int              m_profileRowCount; // 「个人资料」区块当前显示几行；0 表示区块不显示。
                                        // 由 FillProfile 写入，ApplyBodyHeight 据它算窗口高度
    std::vector<int> m_profileSlotFields; // 「个人资料」各格子槽位当前显示的字段（ballooncontact::ContactProfileField），
                                          // -1 表示槽位空着；由 FillProfile 写入，点编辑按钮时据它找到字段
    int              m_profileEditSlot;   // 正在就地编辑的格子槽位；-1 表示没有在编辑。同一时间只编辑一格
    unsigned int     m_profileEditGeneration; // 编辑轮次号，每进入一次编辑加一；用来认出过期的延迟提交消息
    bool             m_profileCommitBusy; // 正在提交（含弹出不合格提示）期间为 true，避免提示框抢走焦点引起重入提交
    ballooncontact::CTagEditPopup m_tagPopup;   // 标签输入弹层，随本窗口存活；未打开时只是个空壳
    int              m_editingTagIndex;         // 输入弹层当前在改哪一枚标签的下标；
                                                // -1 表示这次是"新增标签"而不是"修改标签"
    HBITMAP          m_titleIconBmp;   // 标题栏图标位图（程序图标合成到标题栏底色上）。
                                       // 本类持有，跨"关闭再打开"复用，析构时 DeleteObject
    HICON            m_hTaskIconBig;   // 任务栏 / Alt-Tab 大图标（本类持有，析构 DestroyIcon）
    HICON            m_hTaskIconSmall; // 任务栏 / Alt-Tab 小图标（本类持有，析构 DestroyIcon）
    int              m_connUiState;    // 当前界面连接态（balloonmain::ConnectionUiState：
                                       // 0=在线 1=正在连接 2=离线）。由 ApplyConnectionState
                                       // 从主面板推入，快捷操作行的离线门禁据它判定。
                                       // 初值取"在线"：与各聊天窗一致，宿主尚未推入之前
                                       // 不误拦用户的操作（确实发不出去时，主面板一侧还有
                                       // 一次同样的判定）。
    HBITMAP          m_avatarBmp;      // 头部头像的彩色位图（真实头像：图片或字形），本类持有，析构或来源变化时 DeleteObject；
                                       // NULL 表示模型没有头像来源，头像控件按字形与底色自己画
    HBITMAP          m_avatarGrayBmp;  // m_avatarBmp 的灰度副本（离线时挂上），第一次用到时生成；本类持有，随彩色位图一起释放
    ballooncontact::ContactAvatarSource m_avatarBmpSource; // 生成 m_avatarBmp 时依据的头像来源，用来判断来源是否变了
    bool             m_hasCascadeAnchor; // 建窗时是否有上一个资料窗可参照错开摆放（SetCascadeAnchor 设入）
    POINT            m_cascadeAnchor;    // 上一个资料窗的左上角（屏幕坐标），m_hasCascadeAnchor 为 true 时有效

    static PromptHook s_promptHook;    // 提示框的替换函数（单元测试用），NULL 表示用 MessageBox；进程内全部资料窗共用
};
