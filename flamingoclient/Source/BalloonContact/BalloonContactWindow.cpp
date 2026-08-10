/**
 *  BalloonContactWindow.cpp
 *
 *  联系人资料对话框的实现。标签增删 + 备注名行内编辑 + 三个开关 +
 *  分组下拉 + 底部按钮。当前以 mock 数据驱动，「发消息」为占位。
 *
 *  balloonwj@qq.com   2026-05-22
 */

#include "stdafx.h"
#include "BalloonContactWindow.h"

#include <memory>

#include "../CustomMsgDef.h"          // WM_CLOSE_MAINDLG
#include "../UserSessionData.h"       // FMG_MSG_OPEN_CHAT（"发消息"路由）/ FMG_MSG_SET_CONTACT_REMARK（备注提交回投）
#include <string>                     // 备注回投的 UTF-8 堆负载
#include "../IULog.h"                 // LOG_INFO / LOG_ERROR / LOG_WARNING
#include "ContactLayout.h"            // BuildContactBody / ContactCtrlId 等
#include "../../balloonui/balloonui/Controls/Basic/DuiAvatar.h"
#include "../../balloonui/balloonui/Controls/Input/DuiEditHost.h"
#include "../../balloonui/balloonui/Controls/Input/DuiSwitch.h"
#include "../../balloonui/balloonui/Controls/Input/DuiComboBox.h"

using namespace balloonwjui;
using namespace ballooncontact;

// ---- 文件作用域常量 -----------------------------------------------------

namespace {

// 联系人资料对话框固定客户区尺寸（逻辑像素）。宽度照设计稿 width:420；
// 高度按各区块排满精确累计（与 ContactLayout.cpp 内嵌 XML 一一对应）：
//   客户区上内边距 18
//   头部行 84 + 头部下边距 spacer 14 + 分隔线 1
//   标签区 vbox 76 + 分隔线 1
//   备注名 / 分组区 vbox 132 + 分隔线 1
//   其它开关区 vbox 170 + 分隔线 1
//   底部按钮行 58
//   客户区下内边距 18
// 合计 = 18 + 99 + 77 + 133 + 171 + 58 + 18 = 574。
// 头部行取 84 而非 64：设计稿头部 4 行文字块（姓名 + 职务 + 账号 +
// 签名）整体约 82px，比头像 64 更高 —— 设计稿 alignItems:center 让头像
// 在该高度内垂直居中，行高不足会裁掉第 4 行签名。
const int kContactWindowWidth  = 420;
const int kContactWindowHeight = 574;

// 标题栏高度（逻辑像素，照设计稿 Modal header 44）。
const int kContactTitleBarHeight = 44;

// 「+ 添加标签」点击时为 mock 追加的标签前缀（真实输入框待接入）。
const TCHAR* const kMockNewTagPrefix = _T("新标签");

// CString（宽字符）转 UTF-8 字节。备注名要跨窗口回投给主面板、并最终进 JSON 报文，
// 两处都以 UTF-8 为准。空串原样返回空串。
std::string CStrToUtf8(const CString& text)
{
    if (text.IsEmpty())
    {
        return std::string();
    }
    const int len = ::WideCharToMultiByte(CP_UTF8, 0, (LPCWSTR)text, -1, NULL, 0, NULL, NULL);
    if (len <= 0)
    {
        return std::string();
    }
    std::string out((size_t)len, '\0');
    ::WideCharToMultiByte(CP_UTF8, 0, (LPCWSTR)text, -1, &out[0], len, NULL, NULL);
    // WideCharToMultiByte 带上了结尾的 '\0'，std::string 自己管长度，去掉它。
    if (!out.empty() && out[out.size() - 1] == '\0')
    {
        out.resize(out.size() - 1);
    }
    return out;
}

} // anonymous namespace

// =======================================================================
// 构造 / 析构
// =======================================================================

CBalloonContactWindow::CBalloonContactWindow(CFlamingoClient* pFMGClient)
    : m_lpFMGClient(pFMGClient)
    , m_hMainDlg(NULL)
    , m_hostPanel(NULL)
    , m_peerUserId(0)
    , m_remarkEditing(false)
{
}

void CBalloonContactWindow::OpenForContact(HWND hostPanel, UINT peerUserId,
                                           const ballooncontact::ContactModel& model,
                                           bool beginRemarkEdit)
{
    m_hostPanel  = hostPanel;
    m_peerUserId = peerUserId;
    m_model      = model;

    // 已打开：用新数据重建客户区并前置；未打开：走创建流程。
    if (IsWindow())
    {
        InitUi();
        if (IsIconic())
        {
            ShowWindow(SW_RESTORE);
        }
        ::SetForegroundWindow(m_hWnd);
    }
    else
    {
        CreateAndShow(NULL);
    }

    // 右键菜单「修改备注」的入口：窗口就绪后立刻进编辑态。必须放在窗口创建 / 重建
    // 客户区<u>之后</u> —— SetRemarkEditing 要把 Win32 焦点交给编辑框寄宿的 EDIT
    // 子窗，那个子窗是 InitUi 里才建出来的。
    if (beginRemarkEdit && IsWindow())
    {
        SetRemarkEditing(true);
    }
}

void CBalloonContactWindow::RefreshContactData(const ballooncontact::ContactModel& model)
{
    // 未打开就不刷（下次打开会用最新缓存重建）。
    if (!IsWindow())
    {
        return;
    }
    m_model = model;
    InitUi();        // 用新数据重建客户区
    // 不调 SetForegroundWindow —— 后台静默刷新,不抢用户当前焦点。
    ::InvalidateRect(m_hWnd, NULL, FALSE);
}

CBalloonContactWindow::~CBalloonContactWindow()
{
    // 客户区控件树与 HWND 由 DuiFrameWindow 基类析构兜底。
}

// =======================================================================
// 创建 / 显示
// =======================================================================

void CBalloonContactWindow::CreateAndShow(HWND hMainDlg)
{
    m_hMainDlg = hMainDlg;

    if (Create(NULL, CWindow::rcDefault, _T("FlamingoIM"),
               WS_OVERLAPPEDWINDOW, 0) == NULL)
    {
        LOG_ERROR(_T("联系人资料对话框 Create 失败。"));
        return;
    }

    SetTitle(_T("联系人资料"));
    SetTitleBarHeight(kContactTitleBarHeight);
    SetButtons(false, false, true);   // 只保留关闭按钮（居中模态风格）
    SetResizable(false);              // 固定尺寸对话框
    SetMinSize(kContactWindowWidth, kContactWindowHeight);

    InitUi();
    ResizeClient(kContactWindowWidth, kContactWindowHeight);
    CenterWindow();
    ShowWindow(SW_SHOW);
    UpdateWindow();
    ::SetForegroundWindow(m_hWnd);

    LOG_INFO(_T("联系人资料对话框已创建并显示。"));
}

// =======================================================================
// 客户区初始化
// =======================================================================

void CBalloonContactWindow::InitUi()
{
    std::unique_ptr<DuiControl> root = BuildContactBody();
    if (!root)
    {
        LOG_ERROR(_T("BuildContactBody 返回空 —— 联系人资料对话框客户区未能构建。"));
        return;
    }
    SetClientContent(std::move(root));

    DuiControl* content = GetClientContent();
    if (content == NULL)
    {
        LOG_ERROR(_T("SetClientContent 之后 GetClientContent 仍为空。"));
        return;
    }

    // 备注名行内编辑用的文本框 —— XML builder 只建控件对象、不调
    // EnsureCreated，须在 host HWND 就绪后由调用方显式建一次。
    DuiEditHost* remarkEdit = dynamic_cast<DuiEditHost*>(
        content->FindCtrlById(kIdRemarkEdit));
    if (remarkEdit != NULL)
    {
        remarkEdit->SetShowBorder(true);
        remarkEdit->SetMaxLength(32);   // 备注名上限 32 字符
        if (!remarkEdit->EnsureCreated(m_hWnd))
        {
            LOG_WARNING(_T("联系人资料对话框备注名文本框 EnsureCreated 失败。"));
        }
        // 默认处于非编辑态 —— 文本框先隐藏，让出位置给当前值 + 铅笔按钮。
        remarkEdit->SetVisible(false);
    }

    // 把模型数据灌进各控件。
    FillHeader();
    RebuildTags();
    FillGroupCombo();
    FillSwitches();

    // 「备注名」行右侧当前值显示。
    {
        const CString& remark = m_model.Remark();
        SetContactText(content, kIdRemarkValue,
                       remark.IsEmpty() ? _T("未设置") : (LPCTSTR)remark);
    }

    LOG_INFO(_T("联系人资料对话框客户区 UI 初始化完成。"));
}

void CBalloonContactWindow::FillHeader()
{
    DuiControl* content = GetClientContent();
    if (content == NULL)
    {
        return;
    }

    // 头像 —— XML 里已设了占位字母 / 形状 / 状态 / 底色，这里按模型再确认
    // 占位字母与状态（模型未来接真实数据时此处即为唯一灌入点）。
    DuiAvatar* avatar = dynamic_cast<DuiAvatar*>(
        content->FindCtrlById(kIdAvatar));
    if (avatar != NULL)
    {
        avatar->SetName(m_model.AvatarLetter());
        // 模型状态映射到 DuiAvatar 的状态点。
        switch (m_model.Status())
        {
        case kContactStatusOnline:
            avatar->SetStatus(DuiAvatar::StatusOnline);
            break;
        case kContactStatusAway:
            avatar->SetStatus(DuiAvatar::StatusAway);
            break;
        case kContactStatusBusy:
            avatar->SetStatus(DuiAvatar::StatusBusy);
            break;
        case kContactStatusOffline:
            avatar->SetStatus(DuiAvatar::StatusOffline);
            break;
        default:
            avatar->SetStatus(DuiAvatar::StatusNone);
            break;
        }
    }

    // 头部右侧多行资料块 —— 各行文本由模型拼出，交自由函数按 id 配置。

    // 主名：备注名优先，否则真名。
    const CString& remark = m_model.Remark();
    CString primaryName = remark.IsEmpty() ? m_model.Name() : remark;

    // 括号真名：仅当备注名非空（主名 != 真名）时显示。
    CString realNameParen;
    if (!remark.IsEmpty())
    {
        realNameParen = _T("(");
        realNameParen += m_model.Name();
        realNameParen += _T(")");
    }

    // 第 2 行「职务 · 部门」—— 职务与部门均非空才显示。
    CString titleDept;
    if (!m_model.Title().IsEmpty() && !m_model.Dept().IsEmpty())
    {
        titleDept = m_model.Title();
        titleDept += _T(" · ");
        titleDept += m_model.Dept();
    }

    // 第 3 行「账号 XXX · 地区」。
    CString accountRegion;
    accountRegion = _T("账号 ");
    accountRegion += m_model.Account();
    if (!m_model.Region().IsEmpty())
    {
        accountRegion += _T(" · ");
        accountRegion += m_model.Region();
    }

    ConfigureContactHeader(content, primaryName, realNameParen, titleDept,
                           accountRegion, m_model.Signature(), m_model.Account());
}

void CBalloonContactWindow::RebuildTags()
{
    DuiControl* content = GetClientContent();
    if (content == NULL)
    {
        return;
    }

    // 标签流式容器 —— 增删 / 遍历子控件用 DuiControl 基类接口即可，
    // 无需 ContactTagFlow 的完整类型（它是 Layout 文件局部类型）。
    DuiControl* flow = content->FindCtrlById(kIdTagFlow);
    if (flow == NULL)
    {
        return;
    }

    // 清空原有胶囊 + 「+ 添加标签」按钮，整体重建。
    while (!flow->Children().empty())
    {
        flow->RemoveChild(flow->Children().front().get());
    }

    // 按模型标签列表逐个建胶囊；ctrlId = kIdTagChipBase + 下标。
    const int tagCount = m_model.TagCount();
    for (int i = 0; i < tagCount; ++i)
    {
        std::unique_ptr<DuiControl> chip =
            CreateTagChip(m_model.Tag(i), (UINT)(kIdTagChipBase + i));
        flow->AddChild(std::move(chip));
    }

    // 末尾追加「+ 添加标签」按钮。
    std::unique_ptr<DuiControl> addBtn = CreateAddTagButton();
    flow->AddChild(std::move(addBtn));

    // 标签数量变化会影响流式换行 —— 先强制整树重排。
    RelayoutContactBody(content);

    // 再单独对标签容器强制重排：整树重排经各容器的 SetRect 下传，若标签
    // 容器自身矩形未变（标签通常单行、不影响外层高度），SetRect 的
    // EqualRect 判等会短路掉对其 Layout 的调用，新建胶囊便拿不到矩形。
    // ForceLayout 无条件重跑 Layout，确保新胶囊被定位。
    DuiControl* flowToLayout = content->FindCtrlById(kIdTagFlow);
    if (flowToLayout != NULL)
    {
        flowToLayout->ForceLayout(flowToLayout->GetRect());
        flowToLayout->Invalidate();
    }

    LOG_INFO(_T("联系人资料对话框标签区已重建。"));
}

void CBalloonContactWindow::FillGroupCombo()
{
    DuiControl* content = GetClientContent();
    if (content == NULL)
    {
        return;
    }

    DuiComboBox* combo = dynamic_cast<DuiComboBox*>(
        content->FindCtrlById(kIdGroupCombo));
    if (combo == NULL)
    {
        return;
    }

    combo->ResetContent();
    const int groupCount = m_model.GroupCount();
    for (int i = 0; i < groupCount; ++i)
    {
        combo->AddString(m_model.Group(i));
    }
    // 选中模型当前分组 —— notify=false，避免初始化时假触发 VALUECHANGED。
    combo->SetCurSel(m_model.SelectedGroup(), false);
}

void CBalloonContactWindow::FillSwitches()
{
    DuiControl* content = GetClientContent();
    if (content == NULL)
    {
        return;
    }

    // 各开关的 ctrlId 与对应模型开关类别。
    const UINT switchIds[kContactSwitchCount] =
    {
        kIdSwitchMute, kIdSwitchPinned
    };
    const int switchKinds[kContactSwitchCount] =
    {
        kContactSwitchMute, kContactSwitchPinned
    };

    for (int i = 0; i < kContactSwitchCount; ++i)
    {
        DuiSwitch* sw = dynamic_cast<DuiSwitch*>(
            content->FindCtrlById(switchIds[i]));
        if (sw != NULL)
        {
            // animated=false、notify=false —— 只是把模型值同步进控件。
            sw->SetChecked(m_model.Switch(switchKinds[i]), false, false);
        }
    }
}

// =======================================================================
// 备注名行内编辑
// =======================================================================

void CBalloonContactWindow::SetRemarkEditing(bool editing)
{
    DuiControl* content = GetClientContent();
    if (content == NULL)
    {
        return;
    }

    m_remarkEditing = editing;

    // 当前值文本与铅笔按钮只需切换显隐 —— 用 DuiControl 基类接口即可，
    // 无需其文件局部具体类型。文本框需要 DuiEditHost 的接口。
    DuiControl* remarkValue = content->FindCtrlById(kIdRemarkValue);
    DuiControl* remarkBtn   = content->FindCtrlById(kIdRemarkEditBtn);
    DuiEditHost* remarkEdit = dynamic_cast<DuiEditHost*>(
        content->FindCtrlById(kIdRemarkEdit));

    if (editing)
    {
        // 进入编辑态：隐藏当前值 + 铅笔，显示文本框并填入当前备注。
        if (remarkValue != NULL)
        {
            remarkValue->SetVisible(false);
        }
        if (remarkBtn != NULL)
        {
            remarkBtn->SetVisible(false);
        }
        if (remarkEdit != NULL)
        {
            remarkEdit->SetText(m_model.Remark());
            remarkEdit->SetVisible(true);
        }
    }
    else
    {
        // 退出编辑态：隐藏文本框，恢复当前值 + 铅笔。
        if (remarkEdit != NULL)
        {
            remarkEdit->SetVisible(false);
        }
        if (remarkValue != NULL)
        {
            remarkValue->SetVisible(true);
        }
        if (remarkBtn != NULL)
        {
            remarkBtn->SetVisible(true);
        }
    }

    // 显隐变化影响行内布局 —— 强制整树重排。
    RelayoutContactBody(content);

    // 把 Win32 键盘焦点直接给到文本框寄宿的 EDIT 子窗。注意：DuiControl::SetFocus
    // 只设 DUI 层的焦点，<u>并不会</u> ::SetFocus 那个真 EDIT —— 光调它的话框显出来了
    // 却打不了字，键盘焦点还留在宿主窗口上。更糟的是此后用户去点那个框，Win32 焦点从
    // 宿主转到子 EDIT，宿主收到 WM_KILLFOCUS → DuiHost 转给 m_pFocus（正是本文本框）
    // → 基类 OnKillFocus 发出 DUIN_KILLFOCUS → 被当成"编辑完了"当场提交退回。
    // 所以这里必须走 GetHostedHwnd + ::SetFocus，并全选文本方便直接覆盖输入。
    // 同样的坑群资料对话框的群名片编辑先踩过（见 BalloonGroupInfoDlg::EnterCardEdit）。
    if (editing && remarkEdit != NULL)
    {
        HWND hEdit = remarkEdit->GetHostedHwnd();
        if (hEdit != NULL)
        {
            ::SetFocus(hEdit);
            ::SendMessage(hEdit, EM_SETSEL, 0, -1);
        }
    }
}

void CBalloonContactWindow::CommitRemarkEdit()
{
    DuiControl* content = GetClientContent();
    if (content == NULL)
    {
        return;
    }

    DuiEditHost* remarkEdit = dynamic_cast<DuiEditHost*>(
        content->FindCtrlById(kIdRemarkEdit));
    if (remarkEdit == NULL)
    {
        SetRemarkEditing(false);
        return;
    }
    const CString edited = remarkEdit->GetText();

    // 退出编辑态。
    SetRemarkEditing(false);

    // 有宿主主面板（正常登录态）时，"该不该发、发不出去怎么办、失败怎么退回来"
    // 全部交给主面板判定与执行 —— 它才同时握着通讯录缓存（原值）、连接态与 socket。
    // 本窗口只负责把用户敲进去的原始文本报上去，不在这里再做一份判定，免得两处
    // 规则日后走岔。主面板处理完会回头调 RefreshContactData 把本窗刷成最终状态。
    if (m_hostPanel != NULL && ::IsWindow(m_hostPanel) && m_peerUserId != 0)
    {
        // 跨窗口传 CString 不安全（生命周期与线程），堆上传一份 UTF-8 字节，
        // 由主面板侧 delete —— 与本工程其余"窗口→主面板"的回投一致。
        std::string* pRemark = new std::string(CStrToUtf8(edited));
        ::PostMessage(m_hostPanel, FMG_MSG_SET_CONTACT_REMARK,
                      (WPARAM)m_peerUserId, (LPARAM)pRemark);
        // 备注名可能含用户隐私，日志只记长度不记内容。
        LOG_INFO(_T("联系人资料对话框提交备注: userid=%u, 长度=%d。"),
                 m_peerUserId, edited.GetLength());
        return;
    }

    // 调试入口（--contact）没有宿主主面板，也没有真实连接：就地改内存并刷新，
    // 好让这条交互在调试入口下仍然能走通、能目检。
    CString trimmed = edited;
    trimmed.Trim();
    m_model.SetRemark(trimmed);
    RefreshRemarkDisplay();
    LOG_INFO(_T("联系人资料对话框（无宿主主面板）：备注仅就地更新，未发协议。"));
}

void CBalloonContactWindow::RefreshRemarkDisplay()
{
    DuiControl* content = GetClientContent();
    if (content == NULL)
    {
        return;
    }

    // 备注名那一行的当前值；空则显示"未设置"占位，别留一片空白让人以为控件坏了。
    const CString& remark = m_model.Remark();
    SetContactText(content, kIdRemarkValue,
                   remark.IsEmpty() ? _T("未设置") : (LPCTSTR)remark);

    // 头部也要刷：备注名会影响第 1 行的主名与括号里的真名。
    FillHeader();
}

// =======================================================================
// 标签增删
// =======================================================================

void CBalloonContactWindow::OnTagDeleteClicked(UINT chipCtrlId)
{
    // 胶囊 ctrlId = kIdTagChipBase + 标签下标 —— 反推下标。
    if (chipCtrlId < (UINT)kIdTagChipBase)
    {
        return;
    }
    const int index = (int)(chipCtrlId - (UINT)kIdTagChipBase);

    m_model.RemoveTag(index);
    RebuildTags();   // 删除后下标整体前移，整区重建最稳妥

    LOG_INFO(_T("联系人资料对话框已删除一个标签。"));
}

void CBalloonContactWindow::OnAddTagClicked()
{
    // mock：真实「弹输入框」待接入，此处按当前标签数生成一个占位标签名，
    // 保证「增」这条路径功能可用、可被测试与人工核验。
    CString newTag;
    newTag.Format(_T("%s%d"), kMockNewTagPrefix, m_model.TagCount() + 1);

    if (m_model.AddTag(newTag))
    {
        RebuildTags();
        LOG_INFO(_T("联系人资料对话框已添加一个标签（mock）。"));
    }
}

// =======================================================================
// 开关 / 按钮
// =======================================================================

void CBalloonContactWindow::OnSwitchToggled(UINT switchCtrlId, bool on)
{
    int kind = -1;
    switch (switchCtrlId)
    {
    case kIdSwitchMute:
        kind = kContactSwitchMute;
        break;
    case kIdSwitchPinned:
        kind = kContactSwitchPinned;
        break;
    default:
        return;
    }

    m_model.SetSwitch(kind, on);
    LOG_INFO(_T("联系人资料对话框开关状态已更新。"));
}

void CBalloonContactWindow::DoSendMessage()
{
    // 真实入口（m_hostPanel 有效）：请主面板按 userid 打开单聊，并关闭本资料窗。
    if (m_hostPanel != NULL && m_peerUserId != 0)
    {
        ::PostMessage(m_hostPanel, FMG_MSG_OPEN_CHAT, (WPARAM)m_peerUserId, 0);
        LOG_INFO("联系人资料对话框「发消息」: 请求打开单聊, userid=%u。", m_peerUserId);
        if (IsWindow())
        {
            DestroyWindow();
        }
        return;
    }

    // 调试入口（无主面板绑定）：保留 mock 占位提示。
    CString msg;
    const CString& remark = m_model.Remark();
    const CString primaryName = remark.IsEmpty() ? m_model.Name() : remark;
    msg.Format(_T("向「%s」发消息\n\n（调试入口，当前为 mock 演示）"),
               (LPCTSTR)primaryName);
    ::MessageBox(m_hWnd, msg, _T("联系人资料"), MB_OK | MB_ICONINFORMATION);
    LOG_INFO(_T("联系人资料对话框「发消息」被点击（mock）。"));
}

// =======================================================================
// 消息处理
// =======================================================================

LRESULT CBalloonContactWindow::OnCloseMsg(UINT /*uMsg*/, WPARAM /*wParam*/,
                                          LPARAM /*lParam*/, BOOL& /*bHandled*/)
{
    // 调试入口（--contact）下，本窗口是唯一可见窗口，关闭即退出程序，
    // 交由 CMainDlg 统一收尾。
    if (m_hMainDlg != NULL && ::IsWindow(m_hMainDlg))
    {
        ::PostMessage(m_hMainDlg, WM_CLOSE_MAINDLG, 0, 0);
    }
    if (IsWindow())
    {
        DestroyWindow();
    }
    return 0;
}

LRESULT CBalloonContactWindow::OnDuiNotify(UINT /*uMsg*/, WPARAM /*wParam*/,
                                           LPARAM lParam, BOOL& bHandled)
{
    const DuiNotify* pNotify = reinterpret_cast<const DuiNotify*>(lParam);
    if (pNotify == NULL)
    {
        bHandled = FALSE;
        return 0;
    }

    // ---- 点击类通知 ----
    if (pNotify->code == DUIN_CLICK)
    {
        // 标签胶囊的「×」删除 —— ctrlId 落在 [kIdTagChipBase, ...] 区间。
        if (pNotify->ctrlId >= (UINT)kIdTagChipBase)
        {
            OnTagDeleteClicked(pNotify->ctrlId);
            return 0;
        }

        switch (pNotify->ctrlId)
        {
        case kIdAddTagButton:
            OnAddTagClicked();
            return 0;
        case kIdRemarkEditBtn:
            // 点铅笔 —— 进入备注名行内编辑态。
            SetRemarkEditing(true);
            return 0;
        case kIdBtnClose:
            // 「关闭」按钮 —— 等价标题栏关闭。
            PostMessage(WM_CLOSE, 0, 0);
            return 0;
        case kIdBtnSend:
            DoSendMessage();
            return 0;
        default:
            break;
        }
    }

    // ---- 状态变化类通知 ----
    if (pNotify->code == DUIN_VALUECHANGED)
    {
        // 开关翻转 —— extra 为新状态（1 开 / 0 关）。
        if (pNotify->ctrlId == kIdSwitchMute
            || pNotify->ctrlId == kIdSwitchPinned)
        {
            OnSwitchToggled(pNotify->ctrlId, pNotify->extra != 0);
            return 0;
        }

        // 分组下拉选择变化 —— extra >= 0 为新选中下标。
        if (pNotify->ctrlId == kIdGroupCombo)
        {
            if (pNotify->extra >= 0)
            {
                m_model.SetSelectedGroup((int)pNotify->extra);
                LOG_INFO(_T("联系人资料对话框分组已切换。"));
            }
            return 0;
        }
    }

    // ---- 备注名文本框失焦 —— 去抖后视为提交编辑 ----
    // 不能就地提交：用户在编辑框内点一下，EDIT 会短暂失焦再得焦，立即提交就成了
    // "一点就退回"。延后一拍投消息，由 OnRemarkCommitDeferred 查焦点再定夺。
    if (pNotify->code == DUIN_KILLFOCUS && pNotify->ctrlId == kIdRemarkEdit)
    {
        if (m_remarkEditing && IsWindow())
        {
            ::PostMessage(m_hWnd, WM_CONTACT_REMARK_COMMIT, 0, 0);
        }
        return 0;
    }

    // 其它通知交回 DuiFrameWindow 链。
    bHandled = FALSE;
    return 0;
}

LRESULT CBalloonContactWindow::OnLButtonDown(UINT /*uMsg*/, WPARAM /*wParam*/,
                                             LPARAM /*lParam*/, BOOL& bHandled)
{
    // 备注名编辑态下，本消息只会在点击子 EDIT <u>之外</u>的 DUI 区域时到达宿主
    //（点子 EDIT 的鼠标消息由系统直送该子窗口，不经宿主）。把焦点收回宿主逼子 EDIT
    // 失焦 → EN_KILLFOCUS → DUIN_KILLFOCUS → 去抖提交，达成"点别处自动退出编辑并保存"。
    if (m_remarkEditing && IsWindow())
    {
        ::SetFocus(m_hWnd);
    }
    bHandled = FALSE;   // 继续交给 DuiFrameWindow / DuiHost 正常处理这次点击
    return 0;
}

LRESULT CBalloonContactWindow::OnRemarkCommitDeferred(UINT /*uMsg*/, WPARAM /*wParam*/,
                                                      LPARAM /*lParam*/, BOOL& /*bHandled*/)
{
    if (!m_remarkEditing)
    {
        return 0;   // 已不在编辑态（例如已被别处提交），无须处理
    }

    // 焦点又回到了备注名 EDIT 子窗（用户在编辑框内重新点击导致的短暂失焦 → 得焦）：
    // 说明人还在编辑，跳过这次提交。焦点真正离开了才提交。
    DuiControl* content = GetClientContent();
    if (content != NULL)
    {
        DuiEditHost* remarkEdit = dynamic_cast<DuiEditHost*>(
            content->FindCtrlById(kIdRemarkEdit));
        if (remarkEdit != NULL
            && remarkEdit->GetHostedHwnd() != NULL
            && ::GetFocus() == remarkEdit->GetHostedHwnd())
        {
            return 0;
        }
    }

    CommitRemarkEdit();
    return 0;
}
