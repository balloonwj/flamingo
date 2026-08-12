/**
 *  flamingoAdmin —— 组织节点对话框 NodeModal 实现。
 *
 *  balloonwj@qq.com   2026.05.21
 */
#include "stdafx.h"
#include "Screens/NodeModal.h"
#include "AdminFrame.h"
#include "UiIds.h"
#include "Theme/AdminTheme.h"
#include "Widgets/Widgets.h"      // SolidPanel / SolidHBox
#include "Widgets/UiHelpers.h"    // UiFont

#include "Controls/Layout/DuiLayout.h"
#include "Controls/Basic/DuiLabel.h"
#include "Controls/Basic/DuiButton.h"
#include "Controls/Input/DuiEditHost.h"
#include "Controls/Input/DuiComboBox.h"
#include "DuiNotify.h"

#include <memory>

using namespace balloonwjui;

namespace flamingoadmin {

// theme:: 在 namespace flamingoadmin 内直接解析到 flamingoadmin::theme。

namespace {

// ===========================================================================
// 布局常量（96-dpi 逻辑像素）。与 EmployeeEditDialog 取同一套尺寸，使两个
// 对话框观感一致。
// ===========================================================================
const int kDlgClientWidth   = 520;  // 客户区宽度（不含标题栏）

const int kTitleBarHeight   = 36;   // 自绘标题栏高度

const int kBodyPad          = 20;   // 客户区四周内距
const int kRowGap           = 18;   // 字段行之间间距
const int kColGap           = 16;   // 双列间距
const int kBannerH          = 44;   // 「挂载至 xxx」提示横幅高度
const int kBannerPadX       = 12;   // 横幅内文字左右内距
const int kFieldLabelH      = 16;   // 字段标签行高
const int kFieldLabelGap    = 6;    // 标签到输入控件之间间距
const int kFieldCtrlH       = 34;   // 单行输入控件高度
const int kFieldRowH        = kFieldLabelH + kFieldLabelGap + kFieldCtrlH; // 56

const int kBtnBarH          = 50;   // 底部按钮栏高度
const int kBtnH             = 30;   // 按钮本身高度（交叉轴 fixed，栏高里居中）
const int kBtnW             = 80;   // 按钮宽度
const int kBtnGap           = 10;   // 取消 / 创建之间间距

// 客户区高度按模式算：部门 2 行字段，员工 3 行（姓名+职位 / 邮箱+手机号 / 性别）。
const int kDeptFieldRows    = 2;    // AddDept 的字段行数
const int kUserFieldRows    = 3;    // AddUser 的字段行数

// 字号（磅）沿用 AdminTheme。
const int kFontSizeNormal   = theme::kFontSizeNormal;
const int kFontSizeSmall    = theme::kFontSizeSmall;

// 按模式算客户区高度：内距 + 横幅 + 行间距 + 字段区 + 内距 + 按钮栏。
int ClientHeightFor(NodeModal::Mode mode)
{
    const int rows  = (mode == NodeModal::Mode::AddDept) ? kDeptFieldRows : kUserFieldRows;
    const int formH = rows * kFieldRowH + (rows - 1) * kRowGap;
    return kBodyPad + kBannerH + kRowGap + formH + kBodyPad + kBtnBarH;
}

// ---------------------------------------------------------------------------
// 小工具：批量构造控件树时减少样板代码。与 EmployeeEditDialog 里的同名函数
// 同形（那边在自己的匿名 namespace 里，两者互不可见，故各留一份）。
// ---------------------------------------------------------------------------

// 造一个文字标签。
//   text  ：文字内容；
//   pt    ：字号（磅）；
//   bold  ：是否半粗；
//   color ：文字颜色。
std::unique_ptr<DuiLabel> MakeLabel(LPCTSTR text, int pt, bool bold, COLORREF color)
{
    std::unique_ptr<DuiLabel> lbl(new DuiLabel());
    lbl->SetText(text);
    lbl->SetTextColor(color);
    lbl->SetFont(ui::UiFont(pt, bold));
    lbl->SetTextAlign(DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    return lbl;
}

// 造一个输入框。此处【不】调 EnsureCreated —— host HWND 尚未就绪，
// 由 EnsureFieldEditsCreated 在 SetClientContent 之后统一建。
//   ctrlId      ：控件 ID（UiIds.h）；
//   placeholder ：空值占位字。
std::unique_ptr<DuiEditHost> MakeEdit(int ctrlId, LPCTSTR placeholder)
{
    std::unique_ptr<DuiEditHost> e(new DuiEditHost());
    e->SetCtrlId(ctrlId);
    e->SetPlaceholder(placeholder);
    e->SetShowBorder(true);
    e->SetBgColor(theme::kInk0);
    return e;
}

// 造一个「字段列」：上标签、下输入框，输入框裸指针经 borrowOutEdit 带回。
//   label         ：字段名（如「姓名 *」）；
//   ctrlId        ：输入框 ID；
//   placeholder   ：占位字，可为空串；
//   borrowOutEdit ：输出参数 —— 拿到 DuiEditHost 裸指针供外层读写；所有权仍在控件树里。
std::unique_ptr<DuiControl> MakeFieldColumn(LPCTSTR label, int ctrlId,
                                            LPCTSTR placeholder,
                                            DuiEditHost** borrowOutEdit)
{
    std::unique_ptr<DuiVBox> col(new DuiVBox());
    col->SetGap(kFieldLabelGap);

    col->AddChild(MakeLabel(label, kFontSizeSmall, false, theme::kInk500),
                  DuiLayout::Hint().Fixed(kFieldLabelH));

    std::unique_ptr<DuiEditHost> e = MakeEdit(ctrlId, placeholder);
    *borrowOutEdit = e.get();
    col->AddChild(std::move(e), DuiLayout::Hint().Fixed(kFieldCtrlH));

    return std::unique_ptr<DuiControl>(col.release());
}

// 造「性别」字段列：上标签、下 DuiComboBox。与 MakeFieldColumn 同形，只是
// 底部控件换成下拉框（自绘主体 + 独立 popup HWND，不需要 EnsureCreated）。
std::unique_ptr<DuiControl> MakeGenderColumn(DuiComboBox** borrowOutCombo)
{
    std::unique_ptr<DuiVBox> col(new DuiVBox());
    col->SetGap(kFieldLabelGap);

    col->AddChild(MakeLabel(_T("性别"), kFontSizeSmall, false, theme::kInk500),
                  DuiLayout::Hint().Fixed(kFieldLabelH));

    std::unique_ptr<DuiComboBox> cb(new DuiComboBox());
    cb->SetCtrlId(kIdNodeUserGender);
    cb->AddString(_T("男"));
    cb->AddString(_T("女"));
    cb->AddString(_T("其他"));
    cb->SetCurSel(0, false);
    *borrowOutCombo = cb.get();
    col->AddChild(std::move(cb), DuiLayout::Hint().Fixed(kFieldCtrlH));

    return std::unique_ptr<DuiControl>(col.release());
}

// 造一个按钮。
std::unique_ptr<DuiButton> MakeButton(int ctrlId, LPCTSTR text, DuiButton::Variant variant)
{
    std::unique_ptr<DuiButton> btn(new DuiButton());
    btn->SetCtrlId(ctrlId);
    btn->SetText(text);
    btn->SetVariant(variant);
    btn->SetTextPointSize(kFontSizeNormal);
    return btn;
}

} // anonymous namespace

// ===========================================================================
// NodeModal 公开接口
// ===========================================================================

NodeModal::NodeModal(AdminFrame* frame, HWND parentHwnd, Mode mode,
                     const CString& parentName)
    : m_frame(frame)
    , m_parentHwnd(parentHwnd)
    , m_mode(mode)
    , m_parentName(parentName)
    , m_busy(false)
    , m_editDeptName(nullptr)
    , m_editDeptLeader(nullptr)
    , m_editUserName(nullptr)
    , m_editUserTitle(nullptr)
    , m_editUserEmail(nullptr)
    , m_editUserPhone(nullptr)
    , m_cbUserGender(nullptr)
    , m_btnSubmit(nullptr)
{
}

NodeModal::~NodeModal() = default;

bool NodeModal::CreateAndShow()
{
    // ---- 1. 创建顶层窗口（WS_OVERLAPPEDWINDOW，后续 SetResizable(false) 锁死）
    HWND hw = Create(NULL, CWindow::rcDefault, _T("FlamingoIM Admin"),
                     WS_OVERLAPPEDWINDOW, 0);
    if (hw == NULL || !::IsWindow(hw))
    {
        return false;
    }

    SetTitle(m_mode == Mode::AddDept ? _T("新增部门") : _T("新增员工"));
    SetTitleBarHeight(kTitleBarHeight);
    SetButtons(false, false, true);   // 仅保留关闭按钮
    SetResizable(false);

    // ---- 2. 构造客户区控件树 + SetClientContent
    BuildClientContent();

    // ---- 3. 客户区 HWND 就绪后，逐个 EnsureCreated 真 EDIT 子窗
    EnsureFieldEditsCreated();

    // ---- 4. 锁死尺寸 + 居中 + 真模态（禁主窗）+ 显示
    ResizeClient(kDlgClientWidth, ClientHeightFor(m_mode) + kTitleBarHeight);
    CenterWindow();

    if (m_parentHwnd != NULL && ::IsWindow(m_parentHwnd))
    {
        ::EnableWindow(m_parentHwnd, FALSE);
    }

    ShowWindow(SW_SHOW);
    UpdateWindow();
    ::SetForegroundWindow(m_hWnd);

    return true;
}

void NodeModal::RequestClose()
{
    if (::IsWindow(m_hWnd))
    {
        ::PostMessage(m_hWnd, WM_CLOSE, 0, 0);
    }
}

bool NodeModal::Validate() const
{
    if (m_mode == Mode::AddDept)
    {
        CString n = GetDeptName();
        n.Trim();
        return !n.IsEmpty();
    }

    CString name = GetUserName();
    CString mail = GetUserEmail();
    name.Trim();
    mail.Trim();
    return !name.IsEmpty() && !mail.IsEmpty();
}

CString NodeModal::GetDeptName() const   { return m_editDeptName   ? m_editDeptName->GetText()   : CString(); }
CString NodeModal::GetDeptLeader() const { return m_editDeptLeader ? m_editDeptLeader->GetText() : CString(); }
CString NodeModal::GetUserName() const   { return m_editUserName   ? m_editUserName->GetText()   : CString(); }
CString NodeModal::GetUserTitle() const  { return m_editUserTitle  ? m_editUserTitle->GetText()  : CString(); }
CString NodeModal::GetUserEmail() const  { return m_editUserEmail  ? m_editUserEmail->GetText()  : CString(); }
CString NodeModal::GetUserPhone() const  { return m_editUserPhone  ? m_editUserPhone->GetText()  : CString(); }
CString NodeModal::GetUserGender() const { return m_cbUserGender   ? m_cbUserGender->GetText()   : CString(); }

void NodeModal::SetBusy(bool busy)
{
    m_busy = busy;
    if (m_btnSubmit != nullptr)
    {
        DuiButton* btn = dynamic_cast<DuiButton*>(m_btnSubmit);
        if (btn != nullptr)
        {
            btn->SetText(busy ? _T("创建中…") : _T("创建"));
            btn->SetEnabled(!busy);
        }
    }
}

// ===========================================================================
// NodeModal 私有实现
// ===========================================================================

void NodeModal::BuildClientContent()
{
    // ---- 根 vbox：填满客户区 ----
    std::unique_ptr<DuiVBox> root(new DuiVBox());
    root->SetPadding(kBodyPad);
    root->SetGap(kRowGap);

    // ---- 顶部提示横幅：「挂载至：xxx」----
    // 告诉用户新节点会挂到哪个部门下。挂载目标由调用方在构造时定死，此处只读。
    {
        //SolidPanel 是模板（SolidPanel<TLayout>），横幅内只横排一行文字，
        //故用其 DuiHBox 特化 SolidHBox。
        std::unique_ptr<ui::SolidHBox> banner(new ui::SolidHBox());
        banner->SetFillColor(theme::kInk50);
        banner->SetPadding(kBannerPadX, 0, kBannerPadX, 0);

        CString bannerText;
        if (m_parentName.IsEmpty())
        {
            bannerText = (m_mode == Mode::AddDept) ? _T("挂载至：顶级部门")
                                                   : _T("挂载至：默认部门");
        }
        else
        {
            bannerText.Format(_T("挂载至：%s"), (LPCTSTR)m_parentName);
        }
        banner->AddChild(MakeLabel(bannerText, kFontSizeNormal, false, theme::kInk600),
                         DuiLayout::Hint().Weight(1));

        root->AddChild(std::unique_ptr<DuiControl>(banner.release()),
                       DuiLayout::Hint().Fixed(kBannerH));
    }

    // ---- 字段区：按模式建其中一组 ----
    if (m_mode == Mode::AddDept)
    {
        // 第 1 行：部门名称 *（整行）
        root->AddChild(MakeFieldColumn(_T("部门名称 *"), kIdNodeDeptName,
                                       _T("例如：增长团队"), &m_editDeptName),
                       DuiLayout::Hint().Fixed(kFieldRowH));

        // 第 2 行：部门负责人（整行）
        root->AddChild(MakeFieldColumn(_T("部门负责人"), kIdNodeDeptLead,
                                       _T("负责人姓名（可选）"), &m_editDeptLeader),
                       DuiLayout::Hint().Fixed(kFieldRowH));
    }
    else
    {
        // 第 1 行：姓名* / 职位
        {
            std::unique_ptr<DuiHBox> row(new DuiHBox());
            row->SetGap(kColGap);
            row->AddChild(MakeFieldColumn(_T("姓名 *"), kIdNodeUserName,
                                          _T("员工姓名"), &m_editUserName),
                          DuiLayout::Hint().Weight(1));
            row->AddChild(MakeFieldColumn(_T("职位"), kIdNodeUserTitle,
                                          _T("职位"), &m_editUserTitle),
                          DuiLayout::Hint().Weight(1));
            root->AddChild(std::unique_ptr<DuiControl>(row.release()),
                           DuiLayout::Hint().Fixed(kFieldRowH));
        }

        // 第 2 行：邮箱* / 手机号
        {
            std::unique_ptr<DuiHBox> row(new DuiHBox());
            row->SetGap(kColGap);
            row->AddChild(MakeFieldColumn(_T("邮箱 *"), kIdNodeUserEmail,
                                          _T("name@company.com"), &m_editUserEmail),
                          DuiLayout::Hint().Weight(1));
            row->AddChild(MakeFieldColumn(_T("手机号"), kIdNodeUserPhone,
                                          _T("手机号（可选）"), &m_editUserPhone),
                          DuiLayout::Hint().Weight(1));
            root->AddChild(std::unique_ptr<DuiControl>(row.release()),
                           DuiLayout::Hint().Fixed(kFieldRowH));
        }

        // 第 3 行：性别（占左半列，右半留空以与上面两行对齐）
        {
            std::unique_ptr<DuiHBox> row(new DuiHBox());
            row->SetGap(kColGap);
            row->AddChild(MakeGenderColumn(&m_cbUserGender),
                          DuiLayout::Hint().Weight(1));
            row->AddChild(std::unique_ptr<DuiControl>(new DuiControl()),
                          DuiLayout::Hint().Weight(1));
            root->AddChild(std::unique_ptr<DuiControl>(row.release()),
                           DuiLayout::Hint().Fixed(kFieldRowH));
        }
    }

    // ---- 弹性 spacer：把按钮栏顶到最底 ----
    root->AddChild(std::unique_ptr<DuiControl>(new DuiControl()),
                   DuiLayout::Hint().Weight(1));

    // ---- 按钮栏：上边线 + 右对齐 取消 + 创建 ----
    {
        std::unique_ptr<ui::SolidHBox> bar(new ui::SolidHBox());
        bar->SetEdgeBorder(ui::SolidHBox::kEdgeTop, theme::kInk100);
        bar->SetGap(kBtnGap);
        // 弹性 spacer 把按钮顶到右
        bar->AddChild(std::unique_ptr<DuiControl>(new DuiControl()),
                      DuiLayout::Hint().Weight(1));
        // 按钮交叉轴给 fixed 高度并居中；不给的话默认 AlignFill 会撑满整个栏高。
        bar->AddChild(MakeButton(kIdNodeCancel, _T("取消"),
                                 DuiButton::Variant::Default),
                      DuiLayout::Hint().Fixed(kBtnW, kBtnH)
                                        .AlignC(DuiLayout::AlignCenter));
        std::unique_ptr<DuiButton> submit = MakeButton(kIdNodeSubmit, _T("创建"),
                                                       DuiButton::Variant::Primary);
        m_btnSubmit = submit.get();
        bar->AddChild(std::move(submit),
                      DuiLayout::Hint().Fixed(kBtnW, kBtnH)
                                        .AlignC(DuiLayout::AlignCenter));

        root->AddChild(std::unique_ptr<DuiControl>(bar.release()),
                       DuiLayout::Hint().Fixed(kBtnBarH));
    }

    SetClientContent(std::unique_ptr<DuiControl>(root.release()));
}

void NodeModal::EnsureFieldEditsCreated()
{
    // 只有当前模式建出来的那几个非空，另一组恒为 nullptr，逐个判空即可。
    DuiEditHost* edits[] =
    {
        m_editDeptName, m_editDeptLeader,
        m_editUserName, m_editUserTitle, m_editUserEmail, m_editUserPhone,
    };
    const int n = sizeof(edits) / sizeof(edits[0]);
    for (int i = 0; i < n; ++i)
    {
        if (edits[i] != nullptr)
        {
            edits[i]->EnsureCreated(m_hWnd);
        }
    }
}

void NodeModal::SubmitOrShake()
{
    if (m_busy)
    {
        return;
    }

    if (!Validate())
    {
        if (m_frame != nullptr)
        {
            m_frame->ShowToast(m_mode == Mode::AddDept
                                   ? _T("请填写部门名称")
                                   : _T("请填写员工姓名与邮箱"),
                               AdminFrame::ToastKind::Warn);
        }
        return;
    }

    // 调 AdminFrame 提交入口，本对话框等 HTTP 应答期间 Busy。
    if (m_frame != nullptr)
    {
        SetBusy(true);
        m_frame->OnNodeModalSubmit(this);
    }
}

LRESULT NodeModal::OnDuiNotify(UINT, WPARAM, LPARAM lParam, BOOL& bHandled)
{
    // balloonui 的 WM_DUI_NOTIFY：lParam = const DuiNotify*，结构里带
    // code（通知码）+ ctrlId（控件 ID）+ extra。
    const DuiNotify* n = reinterpret_cast<const DuiNotify*>(lParam);
    if (n == nullptr || n->code != DUIN_CLICK)
    {
        bHandled = FALSE;
        return 0;
    }
    switch (n->ctrlId)
    {
    case kIdNodeCancel:
        // 取消按钮 → 关闭（不提交）。
        RequestClose();
        bHandled = TRUE;
        return 0;
    case kIdNodeSubmit:
        // 创建按钮 → 校验 + 调 AdminFrame 提交。
        SubmitOrShake();
        bHandled = TRUE;
        return 0;
    default:
        bHandled = FALSE;
        return 0;
    }
}

LRESULT NodeModal::OnCloseMsg(UINT, WPARAM, LPARAM, BOOL& bHandled)
{
    // WM_CLOSE 收口：DestroyWindow 触发 WM_DESTROY，真模态解除与通知
    // AdminFrame 清指针都在 OnDestroyMsg 里做。
    DestroyWindow();
    bHandled = TRUE;
    return 0;
}

LRESULT NodeModal::OnDestroyMsg(UINT, WPARAM, LPARAM, BOOL& bHandled)
{
    // ---- 解除真模态：主窗口恢复 Enable 并交回焦点 ----
    if (m_parentHwnd != NULL && ::IsWindow(m_parentHwnd))
    {
        ::EnableWindow(m_parentHwnd, TRUE);
        ::SetForegroundWindow(m_parentHwnd);
    }
    // ---- 通知 AdminFrame 清自己持有的指针并安排延迟 delete ----
    if (m_frame != nullptr)
    {
        m_frame->OnNodeModalDestroyed(this);
    }
    bHandled = FALSE;   // 让 DuiFrameWindow 的 WM_DESTROY 清理继续执行
    return 0;
}

} // namespace flamingoadmin
